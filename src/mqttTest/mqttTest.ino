/*
 * MBusino MQTT Stress Test
 * Board: MakerGO C3 SuperMini (esp32:esp32:makergo_c3_supermini)
 */

#include <SPI.h>
#include <Arduino.h>
#include <Wire.h>
#include <ETH.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

#define ETH_PHY_TYPE     ETH_PHY_W5500
#define ETH_PHY_ADDR     0
#define ETH_PHY_CS       7
#define ETH_PHY_IRQ      5
#define ETH_PHY_RST      6
#define ETH_PHY_SPI_HOST SPI2_HOST
#define ETH_PHY_SPI_SCK  4
#define ETH_PHY_SPI_MISO 12
#define ETH_PHY_SPI_MOSI 11

const char* mqtt_server = "192.168.1.8";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "mqttUser";
const char* mqtt_pass   = "mqttPassword";
const char* mqtt_topic  = "mbusino/stress-test";

WiFiClient ethClient;
PubSubClient mqtt(ethClient);
bool eth_connected = false;
bool mqtt_connected = false;
unsigned long msg_sent = 0;
unsigned long last_stats = 0;

void ethEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH Connected, IP: ");
      Serial.println(ETH.localIP());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      mqtt_connected = false;
      break;
    default:
      break;
  }
}

void WiFiEvent(WiFiEvent_t event) { }

AsyncWebServer server(80); // dummy server, keeps AsyncTCP alive

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n=== MBusino MQTT Stress Test ===");

  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin("dummy_ssid", "dummy_pass");

  Network.onEvent(ethEvent);
  delay(100);
  ETH.setAutoNegotiation(false);
  ETH.setFullDuplex(true);
  ETH.setLinkSpeed(10);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, ETH_PHY_SPI_HOST, ETH_PHY_SPI_SCK, ETH_PHY_SPI_MISO, ETH_PHY_SPI_MOSI);
  delay(2000);

  Serial.println("Ready");
}

void loop() {
  if (!eth_connected) {
    delay(500);
    return;
  }

  if (!mqtt_connected) {
    Serial.print("MQTT...");
    mqtt.setServer(mqtt_server, mqtt_port);
    mqtt.setBufferSize(2048);
    if (mqtt.connect("mbusino-stress-test", mqtt_user, mqtt_pass)) {
      mqtt_connected = true;
      Serial.println("OK");
    } else {
      Serial.print("fail ");
      Serial.println(mqtt.state());
      delay(2000);
      return;
    }
  }

  mqtt.loop();

  static unsigned long last_msg = 0;
  if (millis() - last_msg >= 1000) {
    last_msg = millis();
    char p[128];
    snprintf(p, sizeof(p), "{\"m\":%lu,\"t\":%lu}", msg_sent, millis());
    if (mqtt.publish(mqtt_topic, p)) {
      msg_sent++;
    } else {
      Serial.println("pub fail");
      mqtt_connected = false;
    }
  }

  if (millis() - last_stats >= 10000) {
    last_stats = millis();
    Serial.printf("[Stats] sent: %lu\n", msg_sent);
    msg_sent = 0;
  }
}
