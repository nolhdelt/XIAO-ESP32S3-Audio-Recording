/*
 * XIAO ESP32S3 Sense - Live microphone streaming over WebSocket
 * ----------------------------------------------------------------
 * Press the BOOT button (built into the XIAO board, GPIO0) once to
 * start streaming live audio to a WebSocket relay server. Press it
 * again to stop.
 *
 * Pair this with:
 *   - server/live_audio_server.py  (relay server, run on your laptop)
 *   - server/listen.html           (browser page that plays the audio)
 *
 * Required library (Arduino IDE: Tools > Manage Libraries...):
 *   - "ArduinoWebsockets" by Gil Maimon
 *
 * Before uploading:
 *   1. Copy config.h.example to config.h in this same folder.
 *   2. Fill in your WiFi SSID/password (2.4GHz network/band only) and
 *      WS_SERVER_URL in config.h.
 */

#include <ESP_I2S.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>

#include "config.h"

using namespace websockets;

I2SClass I2S;

// ---------------- Pin configuration ----------------

// Onboard PDM microphone pins (XIAO ESP32S3 Sense, fixed by hardware).
#define I2S_CLK_PIN 42
#define I2S_DATA_PIN 41

// BOOT button, built into the XIAO board (active low, no wiring needed).
#define BUTTON_PIN 0

// ---------------- Audio configuration ----------------

const uint32_t SAMPLE_RATE = 16000;
const uint16_t BITS_PER_SAMPLE = 16;

// ---------------- State ----------------

volatile bool buttonPressedFlag = false;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 300;

bool isStreaming = false;
WebsocketsClient client;

void IRAM_ATTR onButtonPress() {
  buttonPressedFlag = true;
}

// ---------------- Setup ----------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  I2S.setPinsPdmRx(I2S_CLK_PIN, I2S_DATA_PIN);
  if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("ERROR: failed to initialize microphone (I2S). Halting.");
    while (1) delay(1000);
  }

  connectWiFi();

  Serial.println("Ready.");
}

// ---------------- Main loop ----------------

void loop() {
  if (buttonPressedFlag) {
    unsigned long now = millis();
    buttonPressedFlag = false;
    if (now - lastDebounceTime > DEBOUNCE_MS) {
      lastDebounceTime = now;
      toggleStreaming();
    }
  }

  if (isStreaming) {
    client.poll();
    streamChunk();
  }
}

void toggleStreaming() {
  if (!isStreaming) {
    startStreaming();
  } else {
    stopStreaming();
  }
}

// ---------------- Streaming ----------------

void startStreaming() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Cannot start streaming: no WiFi connection.");
      return;
    }
  }

  Serial.print("Connecting to ");
  Serial.println(WS_SERVER_URL);

  if (!client.connect(WS_SERVER_URL)) {
    Serial.println("WebSocket connection failed.");
    return;
  }

  isStreaming = true;
  Serial.println("connected. Streaming live.");
}

void stopStreaming() {
  isStreaming = false;
  client.close();
  Serial.println("Streaming stopped.");
}

void streamChunk() {
  static int16_t buffer[512];
  size_t bytesRead = I2S.readBytes((char *)buffer, sizeof(buffer));
  if (bytesRead > 0 && client.available()) {
    client.sendBinary((const char *)buffer, bytesRead);
  }
}

// ---------------- WiFi ----------------

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
    Serial.println("\nWiFi connection failed (will retry when you start streaming).");
  }
}
