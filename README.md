## ESP32-C3 and Xiaomi Mija Bluetooth Thermometer 3

BLE Temperature Gateway for Home Assistant

ESP32-based gateway that reads temperature, humidity, and battery voltage from Xiaomi LYWSD03MMC Bluetooth thermometers and publishes the data to Home Assistant via MQTT.

## Features

- **Direct BLE connection** to known devices by MAC address (more reliable than scanning)
- **MQTT auto-discovery** - sensors appear automatically in Home Assistant
- **Deep sleep** between readings for low power consumption
- **BT/WiFi separation** - BLE and WiFi never run simultaneously to avoid interference
- **Debug scan** to discover new thermometer MAC addresses

## Hardware

- ESP32 board (tested on ESP32-C3)
- Xiaomi LYWSD03MMC thermometers (with stock or custom firmware)

## Setup

### 1. Configure WiFi and MQTT

Edit `secrets.h` with your credentials:

```c
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"

#define MQTT_BROKER "192.168.1.x"  // Your Home Assistant IP
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_user"
#define MQTT_PASS "mqtt_password"
```

### 2. Add Your Thermometers

In `blue_temp_ha.ino`, add your thermometer MAC addresses to the `knownDevices` array:

```cpp
KnownDevice knownDevices[] = {
  {"a4:c1:38:xx:xx:xx", "living_room"},
  {"a4:c1:38:yy:yy:yy", "bedroom"},
  // Add more devices here
};
```

**Finding MAC addresses:** Flash the code and check the Serial Monitor. The debug scan will print any LYWSD03MMC devices it finds.

### 3. Configure Timing (Optional)

```cpp
int sleepTime = 900;      // Deep sleep duration in seconds (default: 15 min)
int connectTimeout = 10;  // BLE connection timeout per device in seconds
```

### 4. Flash and Run

1. Open in Arduino IDE
2. Select your ESP32 board
3. Upload
4. Open Serial Monitor (115200 baud) to verify operation

## Home Assistant

### MQTT Broker Setup

1. Go to **Settings → Add-ons → Add-on Store**
2. Search for **Mosquitto broker** and install it
3. Start the add-on and enable "Start on boot"
4. Go to **Settings → Devices & Services → Add Integration**
5. Search for **MQTT** and add it
6. Use `localhost` as the broker address if running on the same machine

Create an MQTT user:
1. Go to **Settings → People → Users**
2. Create a new user for MQTT (e.g., `esp32`)
3. Use these credentials in your `secrets.h`

### Auto-discovered Sensors

Sensors will automatically appear in Home Assistant under **Settings → Devices & Services → MQTT**.

Each thermometer creates three sensors:
- `{name} Temperature` (°C)
- `{name} Humidity` (%)
- `{name} Battery Voltage` (V)

## How It Works

```
┌─────────────────────────────────────────────────────────┐
│  1. Wake from deep sleep                                │
│  2. Initialize BLE                                      │
│  3. Debug scan (10s) - print any LYWSD03MMC found       │
│  4. Connect to each known device, read data, cache it   │
│  5. Stop BLE                                            │
│  6. Connect to WiFi                                     │
│  7. Publish all cached data to MQTT                     │
│  8. Disconnect WiFi                                     │
│  9. Deep sleep for sleepTime seconds                    │
│  10. Repeat                                             │
└─────────────────────────────────────────────────────────┘
```

## Dependencies

- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [PubSubClient](https://github.com/knolleary/pubsubclient) (MQTT library)

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No devices found in debug scan | Move ESP32 closer to thermometers, ensure batteries are good |
| Connection failures | Increase `connectTimeout`, check if thermometer is paired to another device |
| MQTT not connecting | Verify credentials in `secrets.h`, check MQTT broker is running |
| Sensors show "Unknown" in HA | Wait for next reading cycle, check MQTT topics in HA developer tools |

## License

WTFPL
