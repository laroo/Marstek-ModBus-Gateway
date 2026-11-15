# Marstek ModBus Gateway — PCB (v1.0)

LibrePCB project for the hardware of the Marstek ModBus Gateway: a small board
that connects a Marstek Venus-E v2.0 battery (RS485 / Modbus RTU) to the local
network over Ethernet. The firmware lives in `../src`.

## Description

- **MCU / network**: WT32-ETH01 module (ESP32 + LAN8720 Ethernet)
- **RS485**: MAX485 transceiver module on Serial2
  (RX = GPIO32, TX = GPIO33, DE = GPIO15, RE = GPIO14).
  The RO line is level-shifted from 5 V to 3.3 V (R3/R4 divider) with a
  pull-up (R6) to keep the line idle-high when the receiver is disabled.
- **Power**: 24 V DC input (J5) -> LM2596 buck module (U3) set to 5 V.
  Jumper J6 selects the 5 V source (see below).
- **Status LEDs** (active-low): red = GPIO17 (error), green = GPIO5 (activity)
- **Programming**: UART header J1 (3V3 / RX / TX / GND), BOOT jumper J2
  (GPIO0 to GND) and reset button S1. OTA updates are supported by the firmware.

## Connectors

| Ref | Function |
|-----|----------|
| J1  | UART flash/debug header: 3V3, RX, TX, GND |
| J2  | BOOT jumper: short to hold GPIO0 low while resetting (flash mode) |
| J3  | RS485 screw terminal: GND, A, B, 5 V (Modbus) |
| J4  | RS485 JST-XH-6 to the battery: pin 6 = A, pin 5 = B, pin 4 = GND, pin 1 = Modbus 5 V |
| J5  | 24 V DC input (screw terminal, mind the polarity on the silkscreen) |
| J6  | 5 V source selection: `External` = DC-DC output feeds the gateway, `Modbus` = gateway is fed from / shares the Modbus 5 V |

## Design notes

Parts were chosen from what was available on hand, so some choices are
pragmatic rather than optimal.

### 5 V rail hold-up (R5, D1, C2, C3)

R5 (2.2 Ω) is in parallel with D1 (1N4007) between the 5 V rail and the
capacitor bank (C2 1000 µF, C3 100 nF).

- **Charging**: the capacitors charge slowly through R5, which limits the
  inrush current at power-on.
- **Discharging**: when the rail dips, D1 lets the capacitors feed the rail
  directly (fast return), stabilising it against load bursts (WiFi/Ethernet,
  RS485 transmit).
- Limitations: a 1N4007 only conducts after about 0.6-0.7 V of sag (a Schottky
  would react sooner), and at 2.2 Ω the peak charge current is about 2.3 A
  (decaying with a time constant of about 2.2 ms). A larger R5 (4.7-10 Ω) would
  reduce that if the DC-DC module struggles at start-up.

### EN reset delay (R7, C1, S1)

R7 (10 kΩ) pulls EN up to 3V3 and C1 (47 µF) slows its rise to roughly
0.5 s. The 3.3 V and 5 V rails are not stable at power-on, and this delay holds
the ESP32 in reset until they have settled. Because the pull-up is taken from
the module's own 3V3 rail, EN can never be released before 3V3 is present.
S1 pulls EN to GND for a manual reset. A dedicated supervisor IC would be more
robust, but the RC network works with the parts at hand.

## Before first power-up

1. Set the LM2596 module output to **5.0 V** with its trimmer *before*
   connecting the rest of the board (J6 open, measure OUT+/OUT-).
2. Do not fit both J6 jumpers if the battery also supplies 5 V on the RS485
   connector.
3. Do not power the board from J1's 3V3 pin while the 24 V supply is connected.

## Flashing

1. Connect a 3.3 V USB-UART adapter to J1 (TX/RX crossed as labelled).
2. Fit the BOOT jumper (J2), press RESET (S1), then flash.
3. Remove the jumper and reset again.

## Layout

- `schematics/`, `boards/`, `circuit/` — LibrePCB project data
- `output/v1.0/` — exported Gerbers, schematic and board images

## License

No license set.
