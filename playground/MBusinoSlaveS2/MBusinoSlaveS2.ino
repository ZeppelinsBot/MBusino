/*
 * MBusino Slave Simulator — S2 Mini Version
 * 
 * Simulates an M-Bus slave device for testing MBusino master.
 * Based on Lolin S2 Mini (ESP32-S2) with 40MHz crystal.
 * 
 * https://github.com/Zeppelin500/MBusino/
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

// No esp_pm needed — S2 Mini has 40MHz crystal, stable UART clock

#define SLAVE_VERSION "1.1.0-S2"

// M-Bus UART
HardwareSerial MbusSerial(1);

// Pin definitions (Lolin S2 Mini)
// GPIO 20 = RX, GPIO 21 = TX (same as MBusino5S S2 Mini)
#define MBUS_RX_PIN 20
#define MBUS_TX_PIN 21
#define LED_PIN LED_BUILTIN

// M-Bus protocol constants
#define MBUS_BAUD 2400
#define FRAME_STOP 0x16
#define ACK_BYTE 0xE5
#define BROADCAST_ADDR 0xFE

// C-field commands from master
#define C_FIELD_NKE 0x40
#define C_FIELD_REQ_UD1 0x5B
#define C_FIELD_REQ_UD1_FCB 0x7B

// EEPROM layout
#define EEPROM_SIZE 512
#define EEPROM_ADDR_FLAG 0
#define EEPROM_ADDR_VALUE 1
#define EEPROM_MAGIC 0xAB
#define EEPROM_WIFI_FLAG 2
#define EEPROM_WIFI_SSID_LEN 3
#define EEPROM_WIFI_SSID 4
#define EEPROM_WIFI_PASS_LEN 36
#define EEPROM_WIFI_PASS 37
#define EEPROM_MAGIC_WIFI 0xCD

// WiFi defaults
#define AP_SSID "MBusinoSlaveS2"
#define WIFI_TIMEOUT_MS 15000

// Global state
uint8_t slaveAddress = 1;
AsyncWebServer server(80);
DNSServer dnsServer;
bool apMode = false;

// Stats
uint32_t requestCount = 0;
uint32_t normalizeCount = 0;
uint32_t dataRequestCount = 0;
uint32_t addressMismatchCount = 0;
uint32_t badFrameCount = 0;
unsigned long lastRequestTime = 0;
uint8_t lastRequestType = 0;
bool debugMode = false;

// Include modules
#include "html.h"
#include "guiServer.h"
#include "mbusSlave.h"

// --- WiFi Setup ---
void setupWiFi() {
  Serial.println("WiFi: connecting...");
  char savedSsid[33] = {0};
  char savedPass[64] = {0};
  bool hasWifiCreds = false;

  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(EEPROM_WIFI_FLAG) == EEPROM_MAGIC_WIFI) {
    uint8_t ssidLen = EEPROM.read(EEPROM_WIFI_SSID_LEN);
    uint8_t passLen = EEPROM.read(EEPROM_WIFI_PASS_LEN);
    if (ssidLen > 0 && ssidLen <= 32) {
      for (uint8_t i = 0; i < ssidLen; i++) savedSsid[i] = EEPROM.read(EEPROM_WIFI_SSID + i);
      savedSsid[ssidLen] = 0;
      for (uint8_t i = 0; i < passLen && i < 63; i++) savedPass[i] = EEPROM.read(EEPROM_WIFI_PASS + i);
      savedPass[passLen] = 0;
      hasWifiCreds = true;
      Serial.printf("WiFi: found saved SSID '%s'\n", savedSsid);
    }
  }

  WiFi.mode(WIFI_STA);
  if (hasWifiCreds) {
    WiFi.begin(savedSsid, savedPass);
  } else {
    WiFi.begin();
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi: connected, IP=%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi: no connection, starting AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    apMode = true;
    Serial.printf("WiFi: AP mode, IP=%s\n", WiFi.softAPIP().toString().c_str());
    dnsServer.start(53, "*", WiFi.softAPIP());
  }
}

// --- Serial Commands ---
void handleSerialCommands() {
  if (!Serial.available()) return;
  char c = Serial.read();

  if (c == 'a') {
    String input = Serial.readStringUntil('\n');
    input.trim();
    int addr = input.toInt();
    if (addr >= 1 && addr <= 250) {
      slaveAddress = (uint8_t)addr;
      EEPROM.begin(EEPROM_SIZE);
      EEPROM.write(EEPROM_ADDR_FLAG, EEPROM_MAGIC);
      EEPROM.write(EEPROM_ADDR_VALUE, slaveAddress);
      EEPROM.commit();
      Serial.printf("Address set to %d (0x%02X) — saved to EEPROM\n", slaveAddress, slaveAddress);
    } else {
      Serial.println("Invalid address (1-250)");
    }
  }
  else if (c == 's' || c == 'S') {
    Serial.println("=== Slave Status (S2 Mini) ===");
    Serial.printf("Address: %d (0x%02X)\n", slaveAddress, slaveAddress);
    Serial.printf("Requests: %lu | NKE: %lu | Data: %lu\n", requestCount, normalizeCount, dataRequestCount);
    Serial.printf("Addr mismatch: %lu | Bad frames: %lu\n", addressMismatchCount, badFrameCount);
    Serial.printf("Debug: %s | WiFi: %s\n", debugMode ? "ON" : "OFF", apMode ? "AP" : "STA");
    Serial.println("==============================");
  }
  else if (c == 'd' || c == 'D') {
    debugMode = !debugMode;
    Serial.printf("Debug: %s\n", debugMode ? "ON" : "OFF");
  }
}

// --- Arduino Setup ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== MBusino Slave Simulator (S2 Mini) ===");
  Serial.printf("Version: %s\n", SLAVE_VERSION);
  Serial.flush();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // EEPROM - load saved address
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(EEPROM_ADDR_FLAG) == EEPROM_MAGIC) {
    uint8_t saved = EEPROM.read(EEPROM_ADDR_VALUE);
    if (saved >= 1 && saved <= 250) {
      slaveAddress = saved;
    }
  }
  Serial.printf("Slave address: %d (0x%02X)\n", slaveAddress, slaveAddress);

  // M-Bus UART
  MbusSerial.setRxBufferSize(271);
  MbusSerial.setTxBufferSize(271);
  MbusSerial.begin(MBUS_BAUD, SERIAL_8E1, MBUS_RX_PIN, MBUS_TX_PIN);
  Serial.printf("M-Bus UART ready (2400 8E1, GPIO %d/%d)\n", MBUS_RX_PIN, MBUS_TX_PIN);

  // WiFi + WebServer + OTA
  setupWiFi();
  setupWebServer();
  ArduinoOTA.setPassword("mbusino");
  ArduinoOTA.begin();

  Serial.println("=== Slave ready ===");
  Serial.println("Commands: 'a<addr>' = set address, 's' = status, 'd' = debug");
}

// --- Arduino Loop ---
void loop() {
  ArduinoOTA.handle();
  if (apMode) dnsServer.processNextRequest();
  handleSerialCommands();
  processMbus();
}
