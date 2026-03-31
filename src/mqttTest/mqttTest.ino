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

// --- Ethernet W5500 Config (same as MBusinoNano) ---
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
const int MSGS_PER_SECOND = 500;
const int BATCH_SIZE = 20;           // messages per batch
const int BATCH_DELAY_MS = (1000 * BATCH_SIZE) / MSGS_PER_SECOND; // ms between batches

// --- Globals ---
WiFiClient ethClient;
PubSubClient mqtt(ethClient);
bool eth_connected = false;
unsigned long msg_sent = 0;
unsigned long msg_failed = 0;
unsigned long last_stats = 0;

// --- Ethernet event handler ---
void onEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH Got IP: ");
      Serial.println(ETH.localIP());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting MQTT...");
    if (mqtt.connect("mbusino-stress-test", mqtt_user, mqtt_pass)) {
      Serial.println("connected!");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 2s...");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== MBusino MQTT Stress Test ===");
  Serial.printf("Target: %d messages/second\n", MSGS_PER_SECOND);
  Serial.printf("Batch: %d msgs every %d ms\n", BATCH_SIZE, BATCH_DELAY_MS);
  Serial.printf("Topic: %s\n", mqtt_topic);

  WiFi.mode(WIFI_STA);
  WiFi.begin("dummy", "dummy"); // needed for event system

  Network.onEvent(onEvent);
  delay(100);

  // Ethernet with auto-negotiation disabled (10M full-duplex)
  ETH.setAutoNegotiation(false);
  ETH.setFullDuplex(true);
  ETH.setLinkSpeed(10);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, ETH_PHY_SPI_HOST, ETH_PHY_SPI_SCK, ETH_PHY_SPI_MISO, ETH_PHY_SPI_MOSI);
  delay(2000);

  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setBufferSize(2048);
}

void loop() {
  if (!eth_connected) {
    delay(1000);
    return;
  }

  if (!mqtt.connected()) {
    reconnectMQTT();
  }
  mqtt.loop();

  // Send a batch of messages
  for (int i = 0; i < BATCH_SIZE; i++) {
    char payload[256];
    snprintf(payload, sizeof(payload),
      "{\"msg\":%lu,\"ts\":%lu,\"energy_wh\":%lu,\"voltage_l1\":%u,"
      "\"current_a\":%.2f,\"power_w\":%lu}",
      msg_sent,
      millis(),
      random(0, 999999),
      random(22000, 24000),
      random(0, 100) / 10.0,
      random(0, 5000)
    );

    if (mqtt.publish(mqtt_topic, payload)) {
      msg_sent++;
    } else {
      msg_failed++;
    }
  }

  // Delay between batches — feeds the watchdog
  delay(BATCH_DELAY_MS);
  yield();

  // Print stats every 5 seconds
  if (millis() - last_stats >= 5000) {
    last_stats = millis();
    float actual_rate = (float)msg_sent / 5.0;
    Serial.printf("[Stats] Sent: %lu | Failed: %lu | Rate: %.0f msg/s | ETH: %s | IP: %s\n",
      msg_sent, msg_failed, actual_rate,
      eth_connected ? "UP" : "DOWN",
      ETH.localIP().toString().c_str()
    );
    msg_sent = 0;
    msg_failed = 0;
  }
}
