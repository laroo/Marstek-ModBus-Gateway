# Marstek ModBus Gateway


![GitHub Forks](https://img.shields.io/github/forks/laroo/Marstek-ModBus-Gateway?style=flat-square&color=blue)
![GitHub Stars](https://img.shields.io/github/stars/laroo/Marstek-ModBus-Gateway?style=flat-square&color=yellow)
![LibrePCB Powered](https://img.shields.io/badge/Powered%20By-LibrePCB-red?style=flat-square)  

**Direct Ethernet-to-Modbus integration for Marstek battery systems, providing local monitoring and control via MQTT**

Marstek ModBus Gateway is a custom hardware gateway designed to bridge the Marstek Venus E v2.0 home battery energy storage system with Home Assistant and local automation systems via a hardwired Ethernet connection. Powered by an ESP32-ETH01 microcontroller and an RS485 transceiver, this dedicated PCB reads Modbus telemetry directly from the battery and translates it into MQTT messages for low-latency, cloud-free monitoring and active charge/discharge control.

## Key Features

- Hardwired Reliability: Utilizes the onboard Ethernet interface of the ESP32-ETH01 for fast, stable network communication without Wi-Fi dropouts.

- Direct Modbus to MQTT: Communicates with the Marstek Venus E v2.0 via RS485/Modbus RTU and publishes metrics and control entities to your local MQTT broker.

- Home Assistant Integration: Enables automatic MQTT discovery for seamless setup of battery state of charge (SoC), power flow, and charge/discharge overrides within Home Assistant.

- Compact Custom PCB: Integrates power regulation, RS485 transceiver protection/isolation, and clean mounting for the ESP32-ETH01 module.

## Technical Architecture

- MCU: ESP32-ETH01 (WT32-ETH01)

- Hardware Interface: RS485 (Modbus RTU)

- Network Protocol: Ethernet / MQTT (with Home Assistant MQTT Discovery)

- Target Device: Marstek Venus E v2.0 Battery System

## Primary Use Cases

- Off-peak grid charging and peak-shaving automated via Home Assistant MQTT triggers.

- Solar excess storage optimization and real-time power steering.

- Local telemetry logging and full battery management without relying on vendor cloud APIs.

## Changelog

v1.0
- Initial release

## MQTT Configuration

### MQTT Topics

TBD - will be updated when the gateway is fully implemented

```
marstek-modbus-gateway/state = CHARGING
```

### MQTT Commands

TBD - will be updated when the gateway is fully implemented

```
marstek-modbus-gateway/command = CHARGE
marstek-modbus-gateway/command = DISCHARGE
marstek-modbus-gateway/command = PAUSE
```

## Home Assistant Configuration

TODO

## Schematics and PCB

Used LibrePCB to design the schematics and PCB.

![Schematic](MarstekModBusGateway_Schematics_v1.0.png)

![PCB](MarstekModBusGateway_PCB_v1.0.png)

Gerber files for the PCB can be found [here](librepcb/output/v1.0/gerber).

# Input/Output

| Pin | Direction | Function | Note |
|----|----------|----------|------|
| GPIO 2 | Input | Sensor 1 | Optional sensor |

## Code

The firmware is built on the Arduino framework using PlatformIO and implements a robust gate control system with the following key features:

- **MQTT Integration**: Remote control and status monitoring via MQTT over Ethernet (W5500)
- **Visual Feedback**: LED system indicating communication status
- **ModBus**: RS485 communication for industrial automation
- **Diagnostics**: Serial output at 115200 baud for debugging and monitoring


### Platform IO Setup

```
pyenv local 3.13
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### Build

```
pio run
```

### Test

```
pio test -v -e native
```

Run a single test:

```
TEST_FILTER=test_mytest pio test -v -e native
```

### Upload (Serial)

```
pio run --target upload -v
```

### OTA (Over-the-Air) Update

The firmware includes [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) for wireless firmware updates over the network.

1. Open a browser and navigate to `http://<device-ip>/update`
2. Authenticate with the configured `OTA_USERNAME` and `OTA_PASSWORD`
3. Select the firmware binary (`.pio/build/esp32/firmware.bin`) and upload

OTA credentials are set as compile-time build flags in `private_config.ini`:

```ini
'-D OTA_USERNAME=youruser'
'-D OTA_PASSWORD=yourpassword'
```

### Web Interface

The device runs an HTTP server on port 80 with the following endpoints:

| Endpoint | Description |
|----------|-------------|
| `/` | Device identification |
