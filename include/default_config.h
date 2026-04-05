#pragma once

#include <stdint.h>

#ifndef APP_DEFAULT_STATUS_LED_PIN
#define APP_DEFAULT_STATUS_LED_PIN 48
#endif

#ifndef APP_DEFAULT_BATTERY_ADC_PIN
#define APP_DEFAULT_BATTERY_ADC_PIN 4
#endif

#ifndef APP_DEFAULT_NEOPIXEL_PIN
#define APP_DEFAULT_NEOPIXEL_PIN 16
#endif

#ifndef APP_DEFAULT_OLED_SDA_PIN
#define APP_DEFAULT_OLED_SDA_PIN 8
#endif

#ifndef APP_DEFAULT_OLED_SCL_PIN
#define APP_DEFAULT_OLED_SCL_PIN 9
#endif

namespace DefaultConfig {

constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr bool WIFI_AP_FALLBACK_ENABLED = true;
constexpr char WIFI_AP_SSID_PREFIX[] = "ESP32-NeoPixel";
constexpr char WIFI_AP_PASSWORD[] = "configureme";

constexpr char DEVICE_NAME[] = "esp32-neopixel-stand-70pixel";
constexpr char FRIENDLY_NAME[] = "ESP32 NeoPixel Stand 70 Pixel";

constexpr char MQTT_HOST[] = "";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char MQTT_BASE_TOPIC[] = "esp32_neopixel_stand_70pixel";
constexpr bool MQTT_DISCOVERY_ENABLED = true;

constexpr char OTA_OWNER[] = "elik745i";
constexpr char OTA_REPOSITORY[] = "ESP32-Neopixel-Stand-70Pixels";
constexpr char OTA_CHANNEL[] = "stable";
constexpr char OTA_ASSET_TEMPLATE[] = "esp32-neopixel-stand-70pixel-${version}.bin";
constexpr char OTA_MANIFEST_URL[] = "";
constexpr bool OTA_ALLOW_INSECURE_TLS = true;

constexpr float BATTERY_CALIBRATION = 3.866f;
constexpr uint32_t BATTERY_UPDATE_INTERVAL_MS = 10000;
constexpr uint16_t BATTERY_MOVING_AVERAGE_WINDOW = 10;

constexpr uint8_t DEFAULT_VOLUME_PERCENT = 40;
constexpr bool DEFAULT_AUDIO_MUTED = false;
constexpr uint8_t DEFAULT_NEOPIXEL_DATA_PIN = APP_DEFAULT_NEOPIXEL_PIN;
constexpr uint16_t DEFAULT_PIXEL_COUNT = 70;
constexpr float DEFAULT_POWER_LIMITER_AMPS = 2.0f;
constexpr uint16_t MAX_PIXEL_COUNT = 600;
constexpr bool LOW_BATTERY_SLEEP_ENABLED = false;
constexpr uint8_t LOW_BATTERY_SLEEP_THRESHOLD_PERCENT = 20;
constexpr uint16_t LOW_BATTERY_WAKE_INTERVAL_MINUTES = 15;

constexpr bool WEB_AUTH_ENABLED = false;
constexpr char WEB_USERNAME[] = "admin";
constexpr char WEB_PASSWORD[] = "admin";

constexpr bool OLED_ENABLED = true;
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;
constexpr char OLED_DRIVER[] = "ssd1306";
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint16_t OLED_ROTATION = 0;
constexpr uint8_t OLED_SDA_PIN = APP_DEFAULT_OLED_SDA_PIN;
constexpr uint8_t OLED_SCL_PIN = APP_DEFAULT_OLED_SCL_PIN;
constexpr int8_t OLED_RESET_PIN = -1;
constexpr uint16_t OLED_DIM_TIMEOUT_SECONDS = 0;

constexpr uint8_t STATUS_LED_PIN = APP_DEFAULT_STATUS_LED_PIN;
constexpr uint8_t BATTERY_ADC_PIN = APP_DEFAULT_BATTERY_ADC_PIN;
constexpr uint8_t NEOPIXEL_PIN = APP_DEFAULT_NEOPIXEL_PIN;

}  // namespace DefaultConfig
