# ESP32 NeoPixel Stand 70 Pixel

PlatformIO firmware for a WEMOS LOLIN S3 Mini based NeoPixel stand controller with:

- ESP32-S3 Mini target using the ESP32-S3FH4R2 package
- 4 MB flash with OTA partitioning
- 2 MB PSRAM enabled and used for larger heap allocations
- local web UI derived from the ESP32 Notifier project interface
- Wi-Fi station mode plus fallback AP configuration mode
- MQTT command and state bridge
- GitHub Releases based OTA updates
- OLED status display support for SSD1306 and SH1106
- persisted settings in Preferences / NVS

## Hardware

Target board:

- WEMOS LOLIN S3 Mini
- ESP32-S3FH4R2
- 512 KB internal SRAM on-chip
- 4 MB flash
- 2 MB PSRAM

Default light-strip wiring:

| Function | GPIO | Notes |
|---|---:|---|
| NeoPixel data | 10 | Default built-in NeoPixel output on LOLIN S3 Mini |
| Status LED | 48 | On-board LED for status indication |
| Battery ADC | 4 | Optional external battery divider input |
| OLED SDA | 8 | Optional I2C OLED |
| OLED SCL | 9 | Optional I2C OLED |

Recommended strip wiring:

1. The default firmware target uses the LOLIN S3 Mini built-in NeoPixel on GPIO10.
2. For an external strip, connect the selected GPIO to the strip DIN through a 330 to 470 ohm series resistor.
3. Power the strip from a dedicated 5V supply sized for the LED count.
4. Tie ESP32 ground and LED power ground together.
5. Add a bulk capacitor across the strip power rails near the strip input.
6. For 70 pixels at full white, do not power the strip from the ESP32 board.

ESP32-S3 Mini pinout sketch:

![ESP32-S3 Mini Pinout](Docs/esp32-s3_pinout.jpeg)

## Project Photos

DIY Multicolor Floor Lamp modified to use the ESP32-S3 Mini board.

### Lamp Box

![DIY Multicolor Floor Lamp Box](3D/DIY_Multicolor_Floor_Lamp.jpeg)

### Lamp Assembled View

![DIY Multicolor Floor Lamp Assembled](3D/DIY_Multicolor_Floor_Lamp1.jpeg)

### 3D Printed Controller Casing

![ESP32-S3 Mini Controller Casing](3D/controller.jpeg)

## STL Files

STLs for printing are in `3D/STL`:

- `3D/STL/Neopixel_Stand_Controller_Bot.stl`
- `3D/STL/Neopixel_Stand_Controller_Top.stl`

Why GPIO10 by default:

- it matches the LOLIN S3 Mini built-in RGB LED wiring
- it allows the default build to work immediately on-board without external strip wiring
- you can still choose another saved GPIO from the Light tab for an external strip

## Defaults

This repository is configured for the GitHub project:

- owner: `elik745i`
- repository: `ESP32-Neopixel-Stand-70Pixels`
- default pixel count: `70`
- default NeoPixel data pin: `GPIO10`
- default OTA asset name: `esp32-neopixel-stand-70pixel-vX.Y.Z.bin`

## Features

The web UI keeps the notifier project shell and swaps the Audio tab for Light control.

Current light controls:

- power on or off
- brightness
- effect selection from the full runtime WS2812FX effect list
- manual per-pixel override over MQTT or the local HTTP API
- effect speed
- effect intensity placeholder in UI is currently disabled because the bundled WS2812FX version does not expose a compatible runtime control
- power limiter in amps
- pixel count
- primary and secondary colors
- inline Light tab effect preview

Current infrastructure retained from the notifier baseline:

- Wi-Fi onboarding and AP fallback
- MQTT connectivity and Home Assistant discovery/state topics
- GitHub release OTA update checks and installs
- OLED status screen
- saved settings in NVS

Recent Home Assistant integration improvements:

- MQTT light discovery now exposes a real HA light entity with power, brightness, RGB color, and effect control
- Home Assistant effect options are generated from the runtime effect list instead of a static subset
- helper entities for primary color and light effect report live state instead of remaining `unknown`
- per-pixel automation can publish directly to dedicated MQTT topics without adding another Home Assistant entity

## PSRAM Optimization

This firmware explicitly enables PSRAM-aware behavior on boot:

- `BOARD_HAS_PSRAM` is defined in the S3 build profiles
- larger heap allocations are allowed to spill into external RAM via `heap_caps_malloc_extmem_enable(1024)`
- boot logs print detected total and free PSRAM

This keeps internal SRAM available for timing-sensitive runtime work while placing larger dynamic allocations into external memory when possible.

## OTA Update Flow

OTA checks are aligned to this repository's GitHub Releases.

The firmware queries:

- `https://api.github.com/repos/elik745i/ESP32-Neopixel-Stand-70Pixels/releases/latest`
- `https://api.github.com/repos/elik745i/ESP32-Neopixel-Stand-70Pixels/releases?per_page=10`

The default release asset template is:

- `esp32-neopixel-stand-70pixel-${version}.bin`

For version `v0.1.0`, the expected OTA asset is:

- `esp32-neopixel-stand-70pixel-v0.1.0.bin`

During OTA install, the OLED progress indicator is mirrored to the NeoPixel strip. Progress fills from pixel `0` upward, so with the default floor-lamp mapping the strip fills from bottom to top. The firmware already reboots automatically after a successful install.

## Per-Pixel Control

Home Assistant can drive individual pixels through MQTT automations without any extra firmware configuration beyond the existing MQTT settings.

MQTT topics:

- assuming the default base topic `esp32_neopixel_stand_70pixel`, `esp32_neopixel_stand_70pixel/cmd/pixel/<index>` sets one pixel to a color payload such as `#ff0000`, `ff0000`, or `off`
- assuming the default base topic `esp32_neopixel_stand_70pixel`, `esp32_neopixel_stand_70pixel/cmd/pixels` accepts a JSON body for multi-pixel updates, clear, and restore operations

Single-pixel example:

```yaml
action: mqtt.publish
data:
  topic: esp32_neopixel_stand_70pixel/cmd/pixel/0
  payload: "#00ff00"
```

Batch example:

```yaml
action: mqtt.publish
data:
  topic: esp32_neopixel_stand_70pixel/cmd/pixels
  payload: >-
    {
      "pixels": [
        {"index": 0, "color": "#ff0000"},
        {"index": 1, "color": "#00ff00"},
        {"index": 2, "color": "#0000ff"}
      ]
    }
```

Supported JSON fields on `cmd/pixels`:

- `pixels`: array of `{ "index": <n>, "color": "#rrggbb" }`
- `ranges`: array of `{ "start": <first>, "end": <last>, "color": "#rrggbb" }`
- `clear`: `true` clears the current manual buffer before applying the rest of the payload
- `restore`: `true` exits manual pixel mode and returns to the active effect or saved light settings

Local HTTP API for testing from the web side:

- `POST /api/light/pixels`

Example request body:

```json
{
	"ranges": [
		{"start": 0, "end": 9, "color": "#ffffff"}
	]
}
```

Manual per-pixel mode temporarily overrides WS2812FX effects. A normal light command, play request, or `{"restore":true}` payload returns the strip to the regular effect renderer.

## Build

Build the default LOLIN S3 Mini target:

```powershell
pio run
```

Upload:

```powershell
pio run -t upload
```

Open monitor:

```powershell
pio device monitor -b 115200
```

## Release Process

Typical release flow for this repository:

1. Build with `pio run`.
2. Rename or copy `.pio/build/lolin_s3_mini_neopixel/firmware.bin` to `esp32-neopixel-stand-70pixel-vX.Y.Z.bin`.
3. Create a Git tag matching the firmware version, for example `v0.1.0`.
4. Publish a GitHub Release and upload that renamed `.bin` asset.
5. The device Firmware tab can then discover and install that release over OTA.

## Project Layout

- `platformio.ini`
- `include/default_config.h`
- `include/settings_schema.h`
- `include/version.h`
- `src/main.cpp`
- `src/light_player.cpp`
- `src/settings_manager.cpp`
- `src/wifi_manager.cpp`
- `src/mqtt_manager.cpp`
- `src/ota_manager.cpp`
- `src/web_server.cpp`
- `src/display_manager.cpp`
- `web/index.html`
- `web/style.css`
- `web/app.js`

## Status

Current firmware version in this working tree:

- `v0.1.6`

Recent changes included in this version:

- mirror OTA progress to the NeoPixel strip from pixel `0` upward and keep automatic reboot after successful install
- add dedicated MQTT and HTTP per-pixel control paths for Home Assistant automations and local testing
- load the full runtime effect catalog into the Light tab instead of using a limited hardcoded list
- add an inline pixel animation preview in the Light tab for live effect visualization
- expose the strip to Home Assistant as a proper MQTT light with brightness, RGB color, and runtime effect selection
- publish effect and color helper entity state so Home Assistant no longer shows those values as `unknown`

Current default hardware profile in this working tree:

- `lolin_s3_mini_neopixel`
