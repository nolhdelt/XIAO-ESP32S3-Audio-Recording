/*
 * XIAO ESP32S3 Sense - BOOT-button WAV recorder, no SD card required
 * ----------------------------------------------------------------
 * Hardware: Seeed XIAO ESP32S3 SENSE (needs the Sense expansion board
 *           for the onboard PDM microphone + PSRAM).
 *
 * Behavior:
 *   - Press the BOOT button (built into the XIAO board) once -> start
 *     recording into PSRAM (no SD card involved at all).
 *   - Press it again -> stop recording, build the WAV file in memory,
 *     and upload it over WiFi via HTTP POST to your computer.
 *
 * Because there's no SD card, the ENTIRE recording has to fit in
 * PSRAM at once, and if the WiFi upload fails there's nothing to fall
 * back on - the recording is lost. MAX_RECORD_SECONDS below caps how
 * long a single recording can be, to keep it well within the Sense
 * board's 8MB of PSRAM.
 *
 * Requires PSRAM to be enabled: Tools > PSRAM > OPI PSRAM in the
 * Arduino IDE (required here, not optional).
 *
 * Before uploading:
 *   1. Copy config.h.example to config.h in this same folder.
 *   2. Fill in your WiFi SSID/password and SERVER_URL in config.h.
 *   3. Run server/receiver.py (see repo README) on a machine on the
 *      same network, or point SERVER_URL at your own HTTP endpoint.
 */

#include <I2S.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "config.h"

// ---------------- Pin configuration ----------------

// BOOT button, built into the XIAO board (active low, no wiring needed).
#define BUTTON_PIN 0

// Onboard PDM microphone pins (XIAO ESP32S3 Sense, fixed by hardware).
#define I2S_CLK_PIN 42
#define I2S_DATA_PIN 41

// ---------------- Audio configuration ----------------

const uint32_t SAMPLE_RATE = 16000;
const uint16_t BITS_PER_SAMPLE = 16;
const uint16_t NUM_CHANNELS = 1;

// Longest recording this will allow, in seconds. Raise this if you need
// longer clips and have confirmed enough free PSRAM (roughly 32KB per
// second of audio at these settings; the Sense board has 8MB total,
// shared with the rest of the program).
const uint32_t MAX_RECORD_SECONDS = 60;

const uint32_t BYTES_PER_SECOND = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
const uint32_t MAX_AUDIO_BYTES = MAX_RECORD_SECONDS * BYTES_PER_SECOND;
const uint32_t WAV_HEADER_SIZE = 44;

// ---------------- State ----------------

volatile bool buttonPressedFlag = false;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 300;

bool isRecording = false;

// Holds the 44-byte WAV header followed by raw audio data, so the whole
// thing can be uploaded in a single contiguous POST body.
uint8_t *recordingBuffer = nullptr;
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

  I2S.setAllPins(-1, I2S_CLK_PIN, I2S_DATA_PIN, -1, -1);
  if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE, BITS_PER_SAMPLE)) {
    Serial.println("ERROR: failed to initialize microphone (I2S). Halting.");
    while (1) delay(1000);
  }

  recordingBuffer = (uint8_t *)ps_malloc(WAV_HEADER_SIZE + MAX_AUDIO_BYTES);
  if (!recordingBuffer) {
    Serial.println("ERROR: failed to allocate PSRAM buffer. Is PSRAM enabled "
                    "(Tools > PSRAM > OPI PSRAM)? Halting.");
    while (1) delay(1000);
  }

  connectWiFi();

  Serial.printf("Ready. Up to %u seconds can be recorded per clip.\n", MAX_RECORD_SECONDS);
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
  totalAudioBytes = 0;
  isRecording = true;
  Serial.println("Recording started (buffering in PSRAM).");
}

void recordChunk() {
  if (totalAudioBytes >= MAX_AUDIO_BYTES) {
    Serial.println("Max recording length reached - stopping automatically.");
    stopRecordingAndUpload();
    return;
  }

  static int16_t chunk[512];
  size_t bytesRead = I2S.readBytes((char *)chunk, sizeof(chunk));
  if (bytesRead == 0) {
    return;
  }

  uint32_t spaceLeft = MAX_AUDIO_BYTES - totalAudioBytes;
  size_t toCopy = min((uint32_t)bytesRead, spaceLeft);
  memcpy(recordingBuffer + WAV_HEADER_SIZE + totalAudioBytes, chunk, toCopy);
  totalAudioBytes += toCopy;
}

void stopRecordingAndUpload() {
  isRecording = false;

  writeWavHeader(recordingBuffer, totalAudioBytes);

  Serial.printf("Recording stopped: %u bytes (%.1f sec).\n",
                totalAudioBytes, totalAudioBytes / (float)BYTES_PER_SECOND);

  uploadRecording();
}

void writeWavHeader(uint8_t *dst, uint32_t dataSize) {
  uint32_t byteRate = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
  uint16_t blockAlign = NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
  uint32_t chunkSize = 36 + dataSize;
  uint32_t subchunk1Size = 16;
  uint16_t audioFormat = 1; // PCM

  memcpy(dst, "RIFF", 4);
  memcpy(dst + 4, &chunkSize, 4);
  memcpy(dst + 8, "WAVE", 4);
  memcpy(dst + 12, "fmt ", 4);
  memcpy(dst + 16, &subchunk1Size, 4);
  memcpy(dst + 20, &audioFormat, 2);
  memcpy(dst + 22, &NUM_CHANNELS, 2);
  memcpy(dst + 24, &SAMPLE_RATE, 4);
  memcpy(dst + 28, &byteRate, 4);
  memcpy(dst + 32, &blockAlign, 2);
  memcpy(dst + 34, &BITS_PER_SAMPLE, 2);
  memcpy(dst + 36, "data", 4);
  memcpy(dst + 40, &dataSize, 4);
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

void uploadRecording() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, attempting reconnect...");
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Upload failed: no WiFi connection. Recording is lost "
                      "(no SD card to fall back on).");
      return;
    }
  }

  uint32_t totalSize = WAV_HEADER_SIZE + totalAudioBytes;

  char filename[32];
  snprintf(filename, sizeof(filename), "rec_%lu.wav", millis());

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Filename", filename);

  Serial.printf("Uploading %s (%u bytes) to %s\n", filename, totalSize, SERVER_URL);

  int httpCode = http.POST(recordingBuffer, totalSize);

  if (httpCode > 0) {
    Serial.printf("Upload finished, server responded: %d\n", httpCode);
    String resp = http.getString();
    if (resp.length()) {
      Serial.println("Server response: " + resp);
    }
  } else {
    Serial.printf("Upload failed: %s\n", http.errorToString(httpCode).c_str());
    Serial.println("Recording is lost (no SD card to fall back on).");
  }

  http.end();
}
