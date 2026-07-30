/*
# MBusino Slave Simulator
# Simulates an M-Bus slave device (e.g. Wärmezähler)
# for testing MBusino master without a real slave.
#
# Based on MBusinoNano5S (ESP32 C3 SuperMini)
# Listens for M-Bus master requests and responds
# with a fixed telegram (EFE Wärmezähler).
#
# Web GUI: Configure slave address (1-254)
#
# https://github.com/Zeppelin500/MBusino/
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#define SLAVE_VERSION "1.0.0"

// M-Bus UART (same as MBusinoNano5S)
HardwareSerial MbusSerial(1);

// Pin definitions (ESP32 C3 SuperMini)
// GPIO 18/19 = USB D-/D+ — DO NOT USE for GPIO!
// GPIO 20/21 = UART1 (M-Bus)
#define MBUS_RX_PIN 20
#define MBUS_TX_PIN 21
#define LED_PIN LED_BUILTIN

// M-Bus protocol constants
#define MBUS_BAUD 2400
#define FRAME_START_SHORT 0x10
#define FRAME_START_LONG 0x68
#define FRAME_STOP 0x16
#define ACK_BYTE 0xE5
#define BROADCAST_ADDR 0xFE

// C-field commands from master
#define C_FIELD_NKE 0x40      // SND-NKE (Normalize)
#define C_FIELD_REQ_UD1 0x5B  // REQ_UD1 (no FCB)
#define C_FIELD_REQ_UD1_FCB 0x7B  // REQ_UD1 (with FCB)

// C-field response to master
#define C_FIELD_RESPONSE 0x08  // RSP_UD (Response from slave, no error)

// EEPROM layout
#define EEPROM_SIZE 512
#define EEPROM_ADDR_FLAG 0
#define EEPROM_ADDR_VALUE 1
#define EEPROM_MAGIC 0xAB
#define EEPROM_WIFI_FLAG 2
#define EEPROM_WIFI_SSID_LEN 3
#define EEPROM_WIFI_SSID 4       // 4..35 (max 32 chars)
#define EEPROM_WIFI_PASS_LEN 36
#define EEPROM_WIFI_PASS 37       // 37..99 (max 63 chars)
#define EEPROM_MAGIC_WIFI 0xCD

// WiFi defaults
#define AP_SSID "MBusinoSlave"
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
uint8_t lastRequestType = 0;  // 0=none, 1=NKE, 2=REQ_UD1
bool debugMode = false;

// Include modules
#include "html.h"
#include "guiServer.h"
#include "mbusSlave.h"

// --- WiFi Setup ---
void setupWiFi() {
  Serial.println("WiFi: connecting...");

  // Try saved credentials first
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
    WiFi.begin();  // fall back to NVS credentials
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
    // Set address: "a42" or "a42\n"
    String input = Serial.readStringUntil('\n');
    input.trim();
    int addr = input.toInt();
    if (addr >= 1 && addr <= 254) {
      slaveAddress = (uint8_t)addr;
      EEPROM.begin(EEPROM_SIZE);
      EEPROM.write(EEPROM_ADDR_FLAG, EEPROM_MAGIC);
      EEPROM.write(EEPROM_ADDR_VALUE, slaveAddress);
      EEPROM.commit();
      Serial.printf("Address set to %d (0x%02X) — saved to EEPROM\n", slaveAddress, slaveAddress);
    } else {
      Serial.println("Invalid address (1-254)");
    }
  }
  else if (c == 's' || c == 'S') {
    Serial.println("=== Slave Status ===");
    Serial.printf("Address: %d (0x%02X)\n", slaveAddress, slaveAddress);
    Serial.printf("Total requests: %lu\n", requestCount);
    Serial.printf("Normalize (NKE): %lu\n", normalizeCount);
    Serial.printf("Data requests (REQ_UD1): %lu\n", dataRequestCount);
    Serial.printf("Address mismatches: %lu\n", addressMismatchCount);
    Serial.printf("Bad frames: %lu\n", badFrameCount);
    if (lastRequestTime > 0) {
      Serial.printf("Last request: %lu ms ago\n", millis() - lastRequestTime);
    }
    Serial.printf("Debug: %s\n", debugMode ? "ON" : "OFF");
    Serial.printf("WiFi: %s\n", apMode ? "AP" : "STA");
    Serial.println("====================");
  }
  else if (c == 'd' || c == 'D') {
    debugMode = !debugMode;
    Serial.printf("Debug mode: %s\n", debugMode ? "ON" : "OFF");
  }
}

// --- Arduino Setup ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== MBusino Slave Simulator ===");
  Serial.printf("Version: %s\n", SLAVE_VERSION);
  Serial.flush();

  // LED
  Serial.println("[BOOT] LED setup..."); Serial.flush();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("[BOOT] LED done"); Serial.flush();

  // EEPROM - load saved address
  Serial.println("[BOOT] EEPROM..."); Serial.flush();
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(EEPROM_ADDR_FLAG) == EEPROM_MAGIC) {
    uint8_t saved = EEPROM.read(EEPROM_ADDR_VALUE);
    if (saved >= 1 && saved <= 254) {
      slaveAddress = saved;
    }
  }
  Serial.printf("[BOOT] Slave address: %d (0x%02X)\n", slaveAddress, slaveAddress);
  Serial.flush();

  // M-Bus UART
  Serial.println("[BOOT] MbusSerial.begin..."); Serial.flush();
  MbusSerial.begin(MBUS_BAUD, SERIAL_8E1, MBUS_RX_PIN, MBUS_TX_PIN);
  Serial.println("[BOOT] MbusSerial done"); Serial.flush();

  // WiFi
  Serial.println("[BOOT] WiFi..."); Serial.flush();
  setupWiFi();
  Serial.println("[BOOT] WiFi done"); Serial.flush();

  // Web server
  Serial.println("[BOOT] WebServer..."); Serial.flush();
  setupWebServer();
  Serial.println("[BOOT] WebServer done"); Serial.flush();

  // OTA
  ArduinoOTA.setPassword("mbusino");
  ArduinoOTA.begin();

  Serial.println("=== Slave ready, listening on M-Bus ===");
  Serial.println("Commands: 'a<number>' = set address, 's' = status, 'd' = toggle debug");
  Serial.println();
}

// --- Arduino Loop ---
void loop() {
  ArduinoOTA.handle();

  if (apMode) {
    dnsServer.processNextRequest();
  }

  // Handle serial commands
  handleSerialCommands();

  // Process M-Bus
  processMbus();
}
