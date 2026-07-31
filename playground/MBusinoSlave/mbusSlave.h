/*
 * MBusSlave - M-Bus Slave Simulator
 * 
 * Listens for M-Bus master frames and responds
 * with a fixed Wärmezähler telegram.
 * 
 * M-Bus protocol (IEC 62056-21):
 * - 2400 baud, 8E1
 * - Short frame: 0x10 | C | A | CHK | 0x16
 * - Long frame: 0x68 | L | L | 0x68 | C | A | CI | Data... | CHK | 0x16
 * - ACK: 0xE5
 */

#ifndef MBUS_SLAVE_H
#define MBUS_SLAVE_H

// --- Fixed response telegram (EFE Wärmezähler from MBusinoNano5S line 667) ---
// Exact copy of the commented-out test telegram. Address byte [5] is patched at runtime.
// Checksum byte [191] is recomputed at runtime.
const uint8_t FIXED_TELEGRAM[] = {
  0x68,0xC1,0xC1,0x68,0x08,0x00,0x72,0x09,0x34,0x75,0x73,
  0xC5,0x14,0x00,0x0D,0x43,0x00,0x00,0x00,0x04,0x78,0x41,
  0x63,0x65,0x04,0x04,0x06,0xAA,0x29,0x00,0x00,0x04,0x13,
  0x40,0xA1,0x75,0x00,0x04,0x2B,0x00,0x00,0x00,0x00,0x14,
  0x2B,0x3C,0xF3,0x00,0x00,0x04,0x3B,0x48,0x06,0x00,0x00,
  0x14,0x3B,0x4E,0x0E,0x00,0x00,0x02,0x5B,0x19,0x00,0x02,
  0x5F,0x19,0x00,0x02,0x61,0xFA,0xFF,0x02,0x23,0xAC,0x08,
  0x04,0x6D,0x03,0x2A,0xF1,0x2A,0x44,0x06,0x92,0x0C,0x00,
  0x00,0x44,0x13,0x2D,0x9B,0x1C,0x00,0x42,0x6C,0xDF,0x2C,
  0x01,0xFD,0x17,0x00,0x03,0xFD,0x0C,0x05,0x00,0x00,0x84,
  0x10,0x06,0x1A,0x00,0x00,0x00,0xC4,0x10,0x06,0x05,0x00,
  0x00,0x00,0x84,0x20,0x06,0x00,0x00,0x00,0x00,0xC4,0x20,
  0x06,0x00,0x00,0x00,0x00,0x84,0x30,0x06,0x00,0x00,0x00,
  0x00,0xC4,0x30,0x06,0x00,0x00,0x00,0x00,0x84,0x40,0x13,
  0x00,0x00,0x00,0x00,0xC4,0x40,0x13,0x00,0x00,0x00,0x00,
  0x84,0x80,0x40,0x13,0x00,0x00,0x00,0x00,0xC4,0x80,0x40,
  0x13,0x00,0x00,0x00,0x00,0x84,0xC0,0x40,0x13,0x00,0x00,
  0x00,0x00,0xC4,0xC0,0x40,0x13,0x00,0x00,0x00,0x00,
  0x75,                     // Checksum placeholder (recomputed at runtime)
  0x16                      // Stop
};

#define TELEGRAM_LEN sizeof(FIXED_TELEGRAM)

// --- M-Bus Slave State ---
enum SlaveState {
  SLAVE_IDLE,
  SLAVE_WAIT_FRAME,
  SLAVE_FRAME_RECEIVED
};

// --- Frame buffer ---
#define RX_BUF_SIZE 32
uint8_t rxBuf[RX_BUF_SIZE];
uint8_t rxBufIdx = 0;
SlaveState slaveState = SLAVE_IDLE;
unsigned long frameStartTime = 0;

// --- Forward declarations ---
void processMbus();
bool readShortFrame();
void handleShortFrame();
void sendAck();
void sendDataResponse();
uint8_t calcChecksum(uint8_t* data, uint8_t len);

// --- Compute checksum for the fixed telegram with current address ---
// The telegram has a placeholder checksum at position [TELEGRAM_LEN-2]
// We patch address at [5] and recompute checksum
void prepareTelegram(uint8_t* buf) {
  memcpy(buf, FIXED_TELEGRAM, TELEGRAM_LEN);
  buf[5] = slaveAddress;  // Patch address
  // Checksum = sum of bytes from C-field (pos 4) to last data byte (pos TELEGRAM_LEN-3)
  uint8_t chk = 0;
  for (uint8_t i = 4; i < TELEGRAM_LEN - 2; i++) {
    chk += buf[i];
  }
  buf[TELEGRAM_LEN - 2] = chk;
}

// --- Calculate checksum for arbitrary data ---
uint8_t calcChecksum(uint8_t* data, uint8_t len) {
  uint8_t chk = 0;
  for (uint8_t i = 0; i < len; i++) {
    chk += data[i];
  }
  return chk;
}

// --- Read and validate a short frame from M-Bus ---
// Returns true if a valid short frame was received for this slave
bool readShortFrame() {
  if (MbusSerial.available() < 1) return false;

  // Wait for frame start or ACK
  unsigned long start = millis();
  while (millis() - start < 100) {
    if (MbusSerial.available()) {
      uint8_t b = MbusSerial.read();

      // ACK received (0xE5) - shouldn't happen from master, ignore
      if (b == 0xE5) {
        if (debugMode) Serial.println("[SLAVE] ACK received (ignored)");
        return false;
      }

      // Short frame start
      if (b == 0x10) {
        rxBuf[0] = 0x10;
        rxBufIdx = 1;

        // Read remaining 4 bytes: C, A, CHK, 0x16
        unsigned long byteStart = millis();
        while (rxBufIdx < 5 && millis() - byteStart < 200) {
          if (MbusSerial.available()) {
            rxBuf[rxBufIdx++] = MbusSerial.read();
            byteStart = millis();
          }
        }

        if (rxBufIdx != 5) {
          if (debugMode) Serial.println("[SLAVE] Short frame incomplete");
          return false;
        }

        // Validate stop byte
        if (rxBuf[4] != 0x16) {
          if (debugMode) Serial.println("[SLAVE] Bad stop byte");
          return false;
        }

        // Validate checksum: CHK = C + A
        uint8_t chk = rxBuf[1] + rxBuf[2];
        if (rxBuf[3] != chk) {
          if (debugMode) {
            Serial.printf("[SLAVE] Checksum error: expected 0x%02X, got 0x%02X\n", chk, rxBuf[3]);
          }
          badFrameCount++;
          return false;
        }

        // Check address: own address or broadcast
        uint8_t addr = rxBuf[2];
        if (addr != slaveAddress && addr != BROADCAST_ADDR) {
          addressMismatchCount++;
          if (debugMode) {
            Serial.printf("[SLAVE] Address mismatch: got 0x%02X, expected 0x%02X\n", addr, slaveAddress);
          }
          return false;
        }

        return true;
      }
    }
  }
  return false;
}

// --- Send ACK (0xE5) ---
void sendAck() {
  MbusSerial.write(ACK_BYTE);
  normalizeCount++;
  if (debugMode) {
    Serial.printf("[SLAVE] ACK sent to address 0x%02X\n", rxBuf[2]);
  }
}

// --- Send data response with fixed telegram ---
void sendDataResponse() {
  uint8_t telegram[TELEGRAM_LEN];
  uint8_t addr = rxBuf[2];

  if (addr == BROADCAST_ADDR) {
    // Address 254: send ORIGINAL telegram absolutely unchanged
    memcpy(telegram, FIXED_TELEGRAM, TELEGRAM_LEN);
    Serial.printf("[SLAVE] RAW telegram to address 254 (no patch):\n");
  } else {
    // Normal: patch address and recompute checksum
    prepareTelegram(telegram);
    Serial.printf("[SLAVE] Patched telegram to address %d:\n", addr);
  }

  // Debug: dump full telegram before sending
  for (uint16_t i = 0; i < TELEGRAM_LEN; i++) {
    Serial.printf("%02X", telegram[i]);
  }
  Serial.println();
  Serial.flush();

  // Send the complete frame
  MbusSerial.write(telegram, TELEGRAM_LEN);
  dataRequestCount++;

  if (debugMode) {
    Serial.printf("[SLAVE] Data response sent (%d bytes) to address 0x%02X\n",
                  TELEGRAM_LEN, addr);
  }
}

// --- Main M-Bus processing (called from loop) ---
void processMbus() {
  // Try to read a short frame
  if (!readShortFrame()) return;

  requestCount++;
  lastRequestTime = millis();

  uint8_t cField = rxBuf[1];
  uint8_t addr = rxBuf[2];

  // LED blink
  digitalWrite(LED_PIN, HIGH);

  switch (cField) {
    case C_FIELD_NKE:  // 0x40 - SND-NKE (Normalize)
      lastRequestType = 1;
      sendAck();
      break;

    case C_FIELD_REQ_UD1:      // 0x5B - REQ_UD1 (no FCB)
    case C_FIELD_REQ_UD1_FCB:  // 0x7B - REQ_UD1 (with FCB)
      lastRequestType = 2;
      sendDataResponse();
      break;

    default:
      if (debugMode) {
        Serial.printf("[SLAVE] Unknown C-field: 0x%02X\n", cField);
      }
      break;
  }

  delay(10);  // Brief pause before LED off
  digitalWrite(LED_PIN, LOW);
}

#endif // MBUS_SLAVE_H
