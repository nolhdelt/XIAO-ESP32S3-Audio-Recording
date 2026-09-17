/*
 * XIAO ESP32S3 Sense - Push-button WAV recorder with WiFi upload
 * ----------------------------------------------------------------
 * Hardware: Seeed XIAO ESP32S3 SENSE (needs the Sense expansion board
 *           for the onboard PDM microphone + microSD slot).
 *
 * Behavior:
 *   - Press the external button once  -> start recording to a .wav
 *     file on the microSD card.
 *   - Press it again                  -> stop recording, finalize the
 *     WAV header, then upload the file over WiFi via HTTP POST.
 *
 * Wiring:
 *   - Button: one leg to any free GPIO (BUTTON_PIN below, default D1 /
 *     GPIO2), the other leg to GND. No external resistor needed -
 *     the pin is configured as INPUT_PULLUP.
 *   - Microphone and SD card are built into the Sense board, wired to
 *     fixed pins (see below) - nothing to connect for those.
 *
 * Required boards/libraries (Arduino IDE):
 *   - Board package: "esp32" by Espressif Systems (v3.x), board
 *     "XIAO_ESP32S3".
 *   - Libraries used below (I2S.h, SD_MMC.h, WiFi.h, HTTPClient.h) all
 *     ship with the esp32 board package - no extra installs needed.
 *
 * Before uploading:
 *   1. Copy config.h.example to config.h in this same folder.
 *   2. Fill in your WiFi SSID/password and SERVER_URL in config.h.
 *   3. Run server/receiver.py (see repo README) on a machine on the
 *      same network, or point SERVER_URL at your own HTTP endpoint.
 */

#include <I2S.h>
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>
#include <HTTPClient.h>

#include "config.h"

// ---------------- Pin configuration ----------------

// External push button (wire between this pin and GND).
#define BUTTON_PIN D1

// Onboard PDM microphone pins (XIAO ESP32S3 Sense, fixed by hardware).
#define I2S_CLK_PIN 42
#define I2S_DATA_PIN 41

// Onboard microSD slot pins, 1-bit SDMMC mode (XIAO ESP32S3 Sense, fixed).
#define SD_MMC_CLK 7
#define SD_MMC_CMD 9
#define SD_MMC_D0 8

// ---------------- Audio configuration ----------------

const uint32_t SAMPLE_RATE = 16000;
const uint16_t BITS_PER_SAMPLE = 16;
const uint16_t NUM_CHANNELS = 1;

// ---------------- State ----------------

volatile bool buttonPressedFlag = false;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 300;

bool isRecording = false;
File wavFile;
String currentFileName;
uint32_t totalAudioBytes = 0;

void IRAM_ATTR onButtonPress() {
  buttonPressedFlag = true;
}

// ---------------- Setup ----------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  // Microphone (PDM over I2S)
  I2S.setAllPins(-1, I2S_CLK_PIN, I2S_DATA_PIN, -1, -1);
  if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE, BITS_PER_SAMPLE)) {
    Serial.println("ERROR: failed to initialize microphone (I2S). Halting.");
    while (1) delay(1000);
  }

  // microSD card
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("ERROR: SD card mount failed. Halting.");
    while (1) delay(1000);
  }

  connectWiFi();

  Serial.println("Ready. Press the button to start recording.");
}

// ---------------- Main loop ----------------

void loop() {
  if (buttonPressedFlag) {
    unsigned long now = millis();
    buttonPressedFlag = false;
    if (now - lastDebounceTime > DEBOUNCE_MS) {
      lastDebounceTime = now;
      toggleRecording();
    }
  }

  if (isRecording) {
    recordChunk();
  }
}

void toggleRecording() {
  if (!isRecording) {
    startRecording();
  } else {
    stopRecordingAndUpload();
  }
}

// ---------------- Recording ----------------

void startRecording() {
  currentFileName = "/rec_" + String(millis()) + ".wav";
  wavFile = SD_MMC.open(currentFileName, FILE_WRITE);
  if (!wavFile) {
    Serial.println("ERROR: failed to create file on SD card.");
    return;
  }

  writeWavHeader(wavFile, 0); // placeholder sizes, patched in stopRecordingAndUpload()
  totalAudioBytes = 0;
  isRecording = true;
  Serial.println("Recording started: " + currentFileName);
}

void recordChunk() {
  static int16_t buffer[512];
  size_t bytesRead = I2S.readBytes((char *)buffer, sizeof(buffer));
  if (bytesRead > 0) {
    wavFile.write((uint8_t *)buffer, bytesRead);
    totalAudioBytes += bytesRead;
  }
}

void stopRecordingAndUpload() {
  isRecording = false;

  // Go back and write the real sizes now that we know the data length.
  writeWavHeader(wavFile, totalAudioBytes);
  wavFile.close();

  Serial.printf("Recording stopped: %s (%u bytes of audio)\n",
                currentFileName.c_str(), totalAudioBytes);

  uploadFile(currentFileName);
}

void writeWavHeader(File &f, uint32_t dataSize) {
  uint32_t byteRate = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
  uint16_t blockAlign = NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
  uint32_t chunkSize = 36 + dataSize;
  uint32_t subchunk1Size = 16;
  uint16_t audioFormat = 1; // PCM

  f.seek(0);
  f.write((const uint8_t *)"RIFF", 4);
  f.write((const uint8_t *)&chunkSize, 4);
  f.write((const uint8_t *)"WAVE", 4);
  f.write((const uint8_t *)"fmt ", 4);
  f.write((const uint8_t *)&subchunk1Size, 4);
  f.write((const uint8_t *)&audioFormat, 2);
  f.write((const uint8_t *)&NUM_CHANNELS, 2);
  f.write((const uint8_t *)&SAMPLE_RATE, 4);
  f.write((const uint8_t *)&byteRate, 4);
  f.write((const uint8_t *)&blockAlign, 2);
  f.write((const uint8_t *)&BITS_PER_SAMPLE, 2);
  f.write((const uint8_t *)"data", 4);
  f.write((const uint8_t *)&dataSize, 4);
}

// ---------------- WiFi / upload ----------------

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed (will retry before the next upload).");
  }
}

void uploadFile(const String &path) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, attempting reconnect...");
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Upload skipped: no WiFi connection. File remains on SD card.");
      return;
    }
  }

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.println("ERROR: could not reopen file for upload: " + path);
    return;
  }
  size_t fileSize = f.size();

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Filename", path.substring(1)); // strip leading '/'

  Serial.println("Uploading " + path + " (" + String(fileSize) + " bytes) to " + String(SERVER_URL));

  // Streams directly from the SD file, so the whole recording never
  // has to fit in RAM at once.
  int httpCode = http.sendRequest("POST", &f, fileSize);
  f.close();

  if (httpCode > 0) {
    Serial.printf("Upload finished, server responded: %d\n", httpCode);
    String resp = http.getString();
    if (resp.length()) {
      Serial.println("Server response: " + resp);
    }
#if DELETE_AFTER_UPLOAD
    if (httpCode == 200) {
      SD_MMC.remove(path);
      Serial.println("Local file deleted after successful upload.");
    }
#endif
  } else {
    Serial.printf("Upload failed: %s\n", http.errorToString(httpCode).c_str());
    Serial.println("File remains on SD card for retry.");
  }

  http.end();
}
