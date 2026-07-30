/*
 * MBusino Slave Simulator - Web Server Routes
 */

#ifndef GUI_SERVER_H
#define GUI_SERVER_H

// --- Web Server Setup ---
void setupWebServer() {

  // Main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
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

  // Set address — use getParam() which parses query string automatically
  server.on("/setAddress", HTTP_GET, [](AsyncWebServerRequest *request) {
    int addr = -1;
    if (request->hasParam("addr")) {
      addr = request->getParam("addr")->value().toInt();
    }
    Serial.printf("[WEB] /setAddress addr=%d (params: %d)\n", addr, request->params());
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
    request->send(400, "text/plain", "invalid address");
  });

  // Set WiFi credentials
  server.on("/setWifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    String url = request->url();
    // Parse ssid= and pass= from query string
    int ssidStart = url.indexOf("ssid=");
    int passStart = url.indexOf("pass=");
    if (ssidStart < 0) {
      request->send(400, "text/plain", "missing ssid");
      return;
    }
    ssidStart += 5;
    int ssidEnd = url.indexOf('&', ssidStart);
    if (ssidEnd < 0) ssidEnd = url.length();
    String ssid = url.substring(ssidStart, ssidEnd);

    String pass = "";
    if (passStart >= 0) {
      passStart += 5;
      int passEnd = url.indexOf('&', passStart);
      if (passEnd < 0) passEnd = url.length();
      pass = url.substring(passStart, passEnd);
    }

    // URL-decode basic chars
    ssid.replace("%20", " ");
    pass.replace("%20", " ");
    ssid.replace("+", " ");
    pass.replace("+", " ");

    Serial.printf("[WEB] /setWifi ssid='%s' pass='%s'\n", ssid.c_str(), "***");

    if (ssid.length() == 0 || ssid.length() > 32) {
      request->send(400, "text/plain", "invalid ssid");
      return;
    }
    if (pass.length() > 63) {
      request->send(400, "text/plain", "pass too long");
      return;
    }

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.write(EEPROM_WIFI_FLAG, EEPROM_MAGIC_WIFI);
    EEPROM.write(EEPROM_WIFI_SSID_LEN, ssid.length());
    for (uint8_t i = 0; i < ssid.length(); i++) EEPROM.write(EEPROM_WIFI_SSID + i, ssid[i]);
    EEPROM.write(EEPROM_WIFI_PASS_LEN, pass.length());
    for (uint8_t i = 0; i < pass.length(); i++) EEPROM.write(EEPROM_WIFI_PASS + i, pass[i]);
    EEPROM.commit();

    request->send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
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
