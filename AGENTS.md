# AGENTS.md

## Project Overview

ESP32-ETH01 (WT32-ETH01) PlatformIO project: a gateway that bridges a Marstek
Venus-E v2.0 battery (RS485 / Modbus RTU) to the local network via Ethernet,
exposing telemetry and control over MQTT and a small web dashboard.
Only the ESP32 firmware is in scope. Ignore the `librepcb/`, `home-assistant/`,
and `enclosure/` directories.

## Specs

- `specs/Modbus Table - Modbus Datasheet.csv` — Modbus register map
- `specs/Duravolt-Plug-in-Battery-Modbus.pdf` — protocol reference
- `specs/2._Definition_of_Product_RS-485_Interface.pdf` — product interface spec
- `specs/requirements.md`, `specs/design.md`, `specs/tasks.md` — (empty placeholders)

## Source Code

All firmware source files are in `src/`:

- `Marstek-ModBus-Gateway.ino.cpp` — main sketch (setup/loop), Ethernet+WiFi
  connection management, wiring of the managers
- `marstek.cpp` / `marstek.h` — `Marstek` class: ModbusMaster wrapper over RS485,
  keeps a `MarstekTelemetry` snapshot updated, accepts commands
  (`charge=<W>`, `discharge=<W>`, `stop`)
- `ledmanager.cpp` / `ledmanager.h` — `LEDManager`: two active-low LEDs;
  green blips on Modbus activity, red solid while Modbus/network error
- `mqttmanager.cpp` / `mqttmanager.h` — `MQTTManager`: PubSubClient over
  Ethernet/WiFi, publishes `marstek-modbus-gateway/*` topics, subscribes to
  the command topic
- `webmanager.cpp` / `webmanager.h` — HTTP dashboard (`/`, `/status` JSON,
  `/charge`, `/discharge`, `/stop`) + ElegantOTA
- `config.h` — compile-time configuration (MQTT, OTA) via build flags
- `Marstek-ModBus-Gateway.ino.cpp.old` — original gate-controller sketch,
  kept for reference only (not compiled)

## Hardware Pins

- RS485 (Serial2): RX=GPIO32 (RO), TX=GPIO33 (DI), DE=GPIO15, RE=GPIO14
  — GPIO23/22 are NOT usable (internal Ethernet MDC / RMII on WT32-ETH01)
- LEDs (active-low, per PCB): red=GPIO17 (error), green=GPIO5 (activity)
- Ethernet PHY (internal): LAN8720, MDC=23, MDIO=18, power=16

## Build Configuration

- `platformio.ini` — PlatformIO project config with two environments: `esp32` and `esp32_wokwi`
- `private_config.ini` — local build flags (gitignored), copy from `private_config.template.ini`
- `private_config.template.ini` — template for `private_config.ini`

The `esp32` environment requires `private_config.ini` for build flags (`OTA_USERNAME`, `OTA_PASSWORD`, MQTT settings).
The `esp32_wokwi` environment has its own hardcoded build flags for simulation.

## How to Compile

1. Activate the Python virtual environment: `source .venv/bin/activate`
2. Build all environments: `pio run`
3. Build a specific environment: `pio run -e esp32` or `pio run -e esp32_wokwi`

## Wokwi Simulation

- `wokwi/diagram.json` — simulation wiring (MAX485 emulator chip, LEDs, buttons)
- `wokwi/max485-marstek.chip.c` — custom chip emulating the battery's Modbus slave
- Build chip: `wokwi-cli chip compile wokwi/max485-marstek.chip.c`
- Run: `pio run -e esp32_wokwi` then `wokwi-cli wokwi`
- In simulation there is no Ethernet, so the firmware falls back to WiFi
  ("Wokwi-GUEST", channel 6)

## Key Technical Details

- **Platform**: ESP32 (espressif32 via pioarduino)
- **Framework**: Arduino
- **Python**: 3.13 (see `.python-version`), venv in `.venv/`
- **Board**: esp32dev (build), WT32-ETH01 (real hardware)
- **Serial baud rate**: 115200 (both USB monitor and RS485/Modbus, 8N1)
