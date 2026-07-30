/*
 * MBusino Slave Simulator - Web Server Routes
 */

#ifndef GUI_SERVER_H
#define GUI_SERVER_H

// --- Web Server Setup ---
void setupWebServer() {

  // Main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    char buf[4096];
    snprintf(buf, sizeof(buf), index_html, SLAVE_VERSION, slaveAddress);
    request->send(200, "text/html", buf);
  });

  // Get stats as JSON
  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    char json[512];
    unsigned long ago = lastRequestTime > 0 ? millis() - lastRequestTime : 0;
    snprintf(json, sizeof(json),
      "{\"requests\":%lu,\"normalize\":%lu,\"dataRequests\":%lu,"
      "\"badFrames\":%lu,\"mismatches\":%lu,\"lastRequest\":%lu,"
      "\"address\":%d,\"wifi\":\"%s\",\"freeHeap\":%lu}",
      requestCount, normalizeCount, dataRequestCount,
      badFrameCount, addressMismatchCount, ago,
      slaveAddress, WiFi.SSID().c_str(), ESP.getFreeHeap()
    );
    request->send(200, "application/json", json);
  });

  // Set address
  server.on("/setAddress", HTTP_GET, [](AsyncWebServerRequest *request) {
    String addrStr = request->arg("addr");
    Serial.printf("[WEB] setAddress called, addr=%s\n", addrStr.c_str());
    if (addrStr.length() > 0) {
      int addr = addrStr.toInt();
      if (addr >= 1 && addr <= 254) {
        slaveAddress = (uint8_t)addr;
        EEPROM.begin(EEPROM_SIZE);
        EEPROM.write(EEPROM_ADDR_FLAG, EEPROM_MAGIC);
        EEPROM.write(EEPROM_ADDR_VALUE, slaveAddress);
        EEPROM.commit();
        Serial.printf("[WEB] Address changed to %d (0x%02X)\n", slaveAddress, slaveAddress);
        request->send(200, "text/plain", "ok");
        return;
      }
    }
    request->send(400, "text/plain", "invalid address");
  });

  // OTA Update
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", update_html);
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(
        200, "text/plain", ok ? "Update OK, restarting..." : "Update FAILED"
      );
      response->addHeader("Connection", "close");
      request->send(response);
      if (ok) {
        delay(500);
        ESP.restart();
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      if (!index) {
        Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
      }
      if (!Update.hasError()) {
        Update.write(data, len);
      }
      if (final) {
        Update.end(true);
      }
    }
  );

  server.begin();
  Serial.println("Web server started");
}

#endif // GUI_SERVER_H
