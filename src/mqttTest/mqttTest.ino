/*
 * MBusino MQTT Stress Test
 * 
 * Sends a configurable number of MQTT messages per second
 * to test Ethernet throughput at 10BASE-T / 100BASE-T.
 * 
 * Board: MakerGO C3 SuperMini (esp32:esp32:makergo_c3_supermini)
 * Uses W5500 Ethernet module via SPI.
 */

#include <ETH.h>
#include <WiFi.h>
#include <PubSubClient.h>

// --- Ethernet W5500 Config ---
#define ETH_PHY_TYPE     ETH_PHY_W5500
#define ETH_PHY_ADDR     0
#define ETH_PHY_CS       7
#define ETH_PHY_IRQ      5
#define ETH_PHY_RST      6
#define ETH_PHY_SPI_HOST SPI2_HOST
#define ETH_PHY_SPI_SCK  4
#define ETH_PHY_SPI_MISO 12
#define ETH_PHY_SPI_MOSI 11

// --- MQTT Config ---
const char* mqtt_server = "192.168.1.8";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "mqttUser";
const char* mqtt_pass   = "mqttPassword";
const char* mqtt_topic  = "mbusino/stress-test";

// --- Test Config ---
const int MSGS_PER_SECOND = 1; // start with 1, increase later

// --- Globals ---
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

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n=== MBusino MQTT Stress Test ===");
  Serial.printf("Target: %d msg/s\n", MSGS_PER_SECOND);

  Network.onEvent(ethEvent);

  WiFi.mode(WIFI_STA);
  WiFi.begin("dummy_ssid", "dummy_pass");
  for (int i = 0; i < 20; i++) { delay(50); yield(); }

  // Feed watchdog heavily before ETH.begin
  for (int i = 0; i < 10; i++) { yield(); delay(100); }

  ETH.setAutoNegotiation(false);
  ETH.setFullDuplex(true);
  ETH.setLinkSpeed(10);
  for (int i = 0; i < 10; i++) { yield(); delay(50); }
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, ETH_PHY_SPI_HOST, ETH_PHY_SPI_SCK, ETH_PHY_SPI_MISO, ETH_PHY_SPI_MOSI);

  Serial.println("Waiting for ETH link...");
}

void loop() {
  static bool ready = false;

  // Phase 1: Wait for Ethernet
  if (!eth_connected) {
    delay(500);
    return;
  }

  // Phase 2: Connect MQTT (once)
  if (!mqtt_connected) {
    Serial.print("Connecting MQTT...");
    mqtt.setServer(mqtt_server, mqtt_port);
    mqtt.setBufferSize(2048);
    if (mqtt.connect("mbusino-stress-test", mqtt_user, mqtt_pass)) {
      mqtt_connected = true;
      ready = true;
      Serial.println("OK");
    } else {
      Serial.print("failed (");
      Serial.print(mqtt.state());
      Serial.println("), retry in 2s");
      delay(2000);
      return;
    }
  }

  // Phase 3: Send messages
  mqtt.loop();

  static unsigned long last_msg = 0;
  unsigned long interval = 1000 / MSGS_PER_SECOND;

  if (millis() - last_msg >= interval) {
    last_msg = millis();

    char payload[256];
    snprintf(payload, sizeof(payload),
      "{\"msg\":%lu,\"ts\":%lu,\"e\":%lu,\"v\":%u}",
      msg_sent, millis(), random(0, 999999), random(22000, 24000));

    if (mqtt.publish(mqtt_topic, payload)) {
      msg_sent++;
    } else {
      Serial.println("MQTT publish failed!");
      mqtt_connected = false; // reconnect
    }
  }

  // Stats every 10s
  if (millis() - last_stats >= 10000) {
    last_stats = millis();
    Serial.printf("[Stats] sent: %lu | ETH: %s | MQTT: %s\n",
      msg_sent,
      eth_connected ? "UP" : "DOWN",
      mqtt_connected ? "OK" : "OFF");
    msg_sent = 0;
  }
}
