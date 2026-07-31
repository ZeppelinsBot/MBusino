/*
 * MBusino Slave Simulator — BARE VERSION (no WiFi)
 * 
 * Stripped down to UART + LED only.
 * No WiFi, no WebServer, no OTA, no DNS.
 * Goal: rule out WiFi interrupt jitter on UART TX.
 *
 * Responds to address 254 with original EFE telegram unchanged.
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <EEPROM.h>

#define SLAVE_VERSION "1.2.0-bare"

// M-Bus UART
HardwareSerial MbusSerial(1);

// Pin definitions (ESP32 C3 SuperMini)
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

// Global state
uint8_t slaveAddress = 1;

// Stats
uint32_t requestCount = 0;
uint32_t normalizeCount = 0;
uint32_t dataRequestCount = 0;
uint32_t addressMismatchCount = 0;
uint32_t badFrameCount = 0;
unsigned long lastRequestTime = 0;
uint8_t lastRequestType = 0;
bool debugMode = true;

// Include mbusSlave module
#include "mbusSlave.h"

// --- Serial Commands ---
void handleSerialCommands() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 's' || c == 'S') {
    Serial.println("=== Slave Status (BARE) ===");
    Serial.printf("Address: %d (0x%02X)\n", slaveAddress, slaveAddress);
    Serial.printf("Requests: %lu | NKE: %lu | Data: %lu\n", requestCount, normalizeCount, dataRequestCount);
    Serial.printf("Addr mismatch: %lu | Bad frames: %lu\n", addressMismatchCount, badFrameCount);
    Serial.printf("WiFi: NONE (bare mode)\n");
    Serial.println("===========================");
  }
  else if (c == 'd' || c == 'D') {
    debugMode = !debugMode;
    Serial.printf("Debug: %s\n", debugMode ? "ON" : "OFF");
  }
}

// --- Arduino Setup ---
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== MBusino Slave BARE (no WiFi) ===");
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
  Serial.println("M-Bus UART ready (2400 8E1, GPIO 20/21)");
  Serial.println("Commands: 's' = status, 'd' = toggle debug");
  Serial.println();
}

// --- Arduino Loop ---
void loop() {
  handleSerialCommands();
  processMbus();
}
