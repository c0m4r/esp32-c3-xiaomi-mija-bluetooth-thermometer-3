#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <vector>

// The service and characteristic UUIDs for the Xiaomi Thermometer
static BLEUUID serviceUUID("ebe0ccb0-7a0a-4b0c-8a1a-6ff2997da3a6");
static BLEUUID charUUID("ebe0ccc1-7a0a-4b0c-8a1a-6ff2997da3a6");

#include "secrets.h"
#include <PubSubClient.h>
#include <WiFi.h>

int sleepTime = 900;     // Deep sleep duration in seconds
int connectTimeout = 10; // Connection timeout per device in seconds

// Known devices - add your thermometer MACs here
struct KnownDevice {
  const char *mac;
  const char *name;
};

KnownDevice knownDevices[] = {{"a4:c1:38:xx:xx:xx", "living_room"},
                              {"a4:c1:38:yy:yy:yy", "bedroom"}};
const int numDevices = sizeof(knownDevices) / sizeof(knownDevices[0]);

// Structure to cache sensor data
struct SensorData {
  String address;
  String name;
  float temperature;
  float humidity;
  float voltage;
  bool valid;
};

// Cache for all sensor readings
std::vector<SensorData> sensorCache;

// Global flag to track data reception
static volatile bool dataReceived = false;

// Global variables to store latest reading (temporary)
static float lastTemp = 0.0;
static float lastHum = 0.0;
static float lastVolt = 0.0;

WiFiClient espClient;
PubSubClient client(espClient);

void setupWifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed!");
  }
}

bool reconnectMqtt() {
  int attempts = 0;
  while (!client.connected() && attempts < 3) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      return true;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
      attempts++;
    }
  }
  return client.connected();
}

static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic,
                           uint8_t *pData, size_t length, bool isNotify) {
  if (length == 5) {
    uint16_t tempRaw = pData[0] | (pData[1] << 8);
    lastTemp = tempRaw / 100.0;
    uint8_t humidity = pData[2];
    lastHum = (float)humidity;
    uint16_t voltRaw = pData[3] | (pData[4] << 8);
    lastVolt = voltRaw / 1000.0;

    Serial.printf("Parsed -> Temp: %.2f C, Humidity: %.0f %%, Volt: %.3f V\n",
                  lastTemp, lastHum, lastVolt);
    dataReceived = true;
  }
}

// Send Home Assistant MQTT Discovery messages for a sensor
void sendDiscovery(String name, String mac) {
  String deviceId = mac;
  deviceId.replace(":", "");

  char topic[150];
  char payload[600];

  // Temperature sensor discovery
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_temp/config",
           deviceId.c_str());
  snprintf(payload, sizeof(payload),
           "{"
           "\"name\":\"%s Temperature\","
           "\"state_topic\":\"home/sensors/%s/temperature\","
           "\"unit_of_measurement\":\"°C\","
           "\"device_class\":\"temperature\","
           "\"unique_id\":\"%s_temp\","
           "\"device\":{"
           "\"identifiers\":[\"%s\"],"
           "\"name\":\"%s Thermometer\","
           "\"model\":\"LYWSD03MMC\","
           "\"manufacturer\":\"Xiaomi\""
           "}"
           "}",
           name.c_str(), name.c_str(), deviceId.c_str(), deviceId.c_str(),
           name.c_str());
  client.publish(topic, payload, true);
  Serial.printf("Discovery sent: %s\n", topic);

  // Humidity sensor discovery
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_hum/config",
           deviceId.c_str());
  snprintf(payload, sizeof(payload),
           "{"
           "\"name\":\"%s Humidity\","
           "\"state_topic\":\"home/sensors/%s/humidity\","
           "\"unit_of_measurement\":\"%%\","
           "\"device_class\":\"humidity\","
           "\"unique_id\":\"%s_hum\","
           "\"device\":{"
           "\"identifiers\":[\"%s\"],"
           "\"name\":\"%s Thermometer\","
           "\"model\":\"LYWSD03MMC\","
           "\"manufacturer\":\"Xiaomi\""
           "}"
           "}",
           name.c_str(), name.c_str(), deviceId.c_str(), deviceId.c_str(),
           name.c_str());
  client.publish(topic, payload, true);

  // Voltage sensor discovery
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_volt/config",
           deviceId.c_str());
  snprintf(payload, sizeof(payload),
           "{"
           "\"name\":\"%s Battery Voltage\","
           "\"state_topic\":\"home/sensors/%s/voltage\","
           "\"unit_of_measurement\":\"V\","
           "\"device_class\":\"voltage\","
           "\"unique_id\":\"%s_volt\","
           "\"device\":{"
           "\"identifiers\":[\"%s\"],"
           "\"name\":\"%s Thermometer\","
           "\"model\":\"LYWSD03MMC\","
           "\"manufacturer\":\"Xiaomi\""
           "}"
           "}",
           name.c_str(), name.c_str(), deviceId.c_str(), deviceId.c_str(),
           name.c_str());
  client.publish(topic, payload, true);

  Serial.printf("Discovery complete for %s\n", name.c_str());
}

void publishAllCachedData() {
  for (const auto &sensor : sensorCache) {
    if (!sensor.valid)
      continue;

    if (!client.connected()) {
      if (!reconnectMqtt()) {
        Serial.println("MQTT connection failed, skipping publish");
        return;
      }
    }
    client.loop();

    sendDiscovery(sensor.name, sensor.address);
    delay(100);

    char topic[100];
    char payload[20];

    snprintf(topic, sizeof(topic), "home/sensors/%s/temperature",
             sensor.name.c_str());
    snprintf(payload, sizeof(payload), "%.2f", sensor.temperature);
    client.publish(topic, payload);
    Serial.printf("Published: %s = %s\n", topic, payload);

    snprintf(topic, sizeof(topic), "home/sensors/%s/humidity",
             sensor.name.c_str());
    snprintf(payload, sizeof(payload), "%.0f", sensor.humidity);
    client.publish(topic, payload);
    Serial.printf("Published: %s = %s\n", topic, payload);

    snprintf(topic, sizeof(topic), "home/sensors/%s/voltage",
             sensor.name.c_str());
    snprintf(payload, sizeof(payload), "%.3f", sensor.voltage);
    client.publish(topic, payload);
    Serial.printf("Published: %s = %s\n", topic, payload);

    Serial.printf("Published all data for %s\n", sensor.name.c_str());
    client.loop();
  }

  delay(500);
  client.loop();
}

// Timeout wrapper for BLE connection operations
bool connectWithTimeout(BLEClient *pClient, const char *address,
                        int timeoutMs) {
  unsigned long startTime = millis();

  // Set shorter MTU to speed up connection
  pClient->setClientCallbacks(nullptr);

  // Try to connect - this can sometimes hang
  bool connected = pClient->connect(BLEAddress(address), BLE_ADDR_TYPE_PUBLIC);

  if (!connected) {
    return false;
  }

  // Verify we're actually connected (sometimes connect returns true but isn't
  // stable)
  if (!pClient->isConnected()) {
    return false;
  }

  return true;
}

bool connectAndRead(const char *address, const char *name) {
  Serial.printf("Connecting to %s (%s)... ", name, address);

  BLEClient *pClient = BLEDevice::createClient();

  unsigned long connectStart = millis();
  bool connected = false;

  // Attempt connection with overall timeout protection
  connected = pClient->connect(BLEAddress(address), BLE_ADDR_TYPE_PUBLIC);

  // Check if connection took too long or failed
  if (!connected || (millis() - connectStart > connectTimeout * 1000)) {
    if (connected) {
      Serial.println("Connection timeout - cleaning up");
      pClient->disconnect();
    } else {
      Serial.println("Failed to connect");
    }
    delete pClient;
    return false;
  }

  // Double-check connection status
  if (!pClient->isConnected()) {
    Serial.println("Connection lost immediately");
    delete pClient;
    return false;
  }

  Serial.println("Connected");

  // Get service with timeout check
  unsigned long serviceStart = millis();
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);

  if (millis() - serviceStart > 5000) {
    Serial.println("Service discovery timeout");
    pClient->disconnect();
    delete pClient;
    return false;
  }

  if (pRemoteService == nullptr) {
    Serial.println("Service not found");
    pClient->disconnect();
    delete pClient;
    return false;
  }

  // Get characteristic with timeout check
  unsigned long charStart = millis();
  BLERemoteCharacteristic *pRemoteCharacteristic =
      pRemoteService->getCharacteristic(charUUID);

  if (millis() - charStart > 5000) {
    Serial.println("Characteristic discovery timeout");
    pClient->disconnect();
    delete pClient;
    return false;
  }

  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Characteristic not found");
    pClient->disconnect();
    delete pClient;
    return false;
  }

  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->registerForNotify(notifyCallback);

    Serial.println("Waiting for data...");
    dataReceived = false;
    unsigned long startTime = millis();
    while (!dataReceived && millis() - startTime < 10000) {
      delay(100);
      // Also check if we lost connection
      if (!pClient->isConnected()) {
        Serial.println("Connection lost while waiting for data");
        delete pClient;
        return false;
      }
    }

    if (!dataReceived) {
      Serial.println("Timeout: No data received");
    }
  } else {
    Serial.println("Cannot notify");
  }

  pClient->disconnect();
  delete pClient;
  return dataReceived;
}

void goToSleep(int seconds) {
  Serial.printf("Going to deep sleep for %d seconds...\n", seconds);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n========================================");
  Serial.println("BLE Temperature Gateway - Starting cycle");
  Serial.println("========================================");

  // ========================================
  // PHASE 1: BLE - Connect to all known devices
  // ========================================
  Serial.println("\n=== PHASE 1: BLE Direct Connect ===");

  BLEDevice::init("");

  // Debug scan - find any LYWSD03MMC devices and print their MACs
  Serial.println("\n--- Debug Scan (looking for new thermometers) ---");
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  BLEScanResults *scanResults = pBLEScan->start(10, false);

  int foundCount = 0;
  for (int i = 0; i < scanResults->getCount(); i++) {
    BLEAdvertisedDevice device = scanResults->getDevice(i);
    if (device.getName() == "LYWSD03MMC") {
      Serial.printf("Found LYWSD03MMC: %s\n",
                    device.getAddress().toString().c_str());
      foundCount++;
    }
  }
  Serial.printf("Debug scan complete: found %d LYWSD03MMC device(s)\n",
                foundCount);
  pBLEScan->clearResults();
  Serial.println("--- End Debug Scan ---\n");

  // Now connect to known devices
  Serial.printf("Attempting to connect to %d known devices\n", numDevices);
  sensorCache.clear();

  for (int i = 0; i < numDevices; i++) {
    SensorData data;
    data.address = knownDevices[i].mac;
    data.name = knownDevices[i].name;
    data.valid = false;

    if (connectAndRead(knownDevices[i].mac, knownDevices[i].name)) {
      data.temperature = lastTemp;
      data.humidity = lastHum;
      data.voltage = lastVolt;
      data.valid = true;
      Serial.printf("✓ Cached data for %s\n", data.name.c_str());
    } else {
      Serial.printf("✗ Failed to get data for %s\n", data.name.c_str());
    }
    sensorCache.push_back(data);
    delay(500);
  }

  // Stop BLE
  Serial.println("Stopping BLE...");
  BLEDevice::deinit(false);
  delay(500);

  // Count valid readings
  int validReadings = 0;
  for (const auto &s : sensorCache) {
    if (s.valid)
      validReadings++;
  }
  Serial.printf("Successfully read %d/%d devices\n", validReadings, numDevices);

  // ========================================
  // PHASE 2: WiFi - Connect and send MQTT
  // ========================================
  if (validReadings > 0) {
    Serial.println("\n=== PHASE 2: WiFi & MQTT ===");

    setupWifi();

    if (WiFi.status() == WL_CONNECTED) {
      client.setServer(MQTT_BROKER, MQTT_PORT);
      client.setBufferSize(512);
      publishAllCachedData();
      client.disconnect();
      delay(100);
    } else {
      Serial.println("Skipping MQTT - WiFi not connected");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    Serial.println("\nNo valid readings to publish, skipping WiFi phase");
  }

  // ========================================
  // PHASE 3: Deep Sleep
  // ========================================
  Serial.println("\n=== PHASE 3: Deep Sleep ===");
  goToSleep(sleepTime);
}

void loop() {
  // Never reached - we use deep sleep
}
