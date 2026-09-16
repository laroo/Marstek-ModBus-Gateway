# MAX485 Marstek emulator chip

Wokwi custom chip that emulates the Modbus RTU slave side of a **Marstek Venus E v2.0** battery system behind an RS-485 transceiver.

## Build

```bash
wokwi-cli chip compile wokwi/max485-marstek.chip.c
```

## Run simulation

```bash
# Build the ESP32 firmware for Wokwi first
pio run --environment esp32_wokwi

# Compile and run the custom chip simulation
wokwi-cli chip compile wokwi/max485-marstek.chip.c
wokwi-cli wokwi
```

## Supported Modbus registers

Only function code **0x03** (Read Holding Registers) is implemented at the moment.

| Register (DEC) | Register (HEX) | Name | Type | Implemented | Notes |
|---|---|---|---|---|---|
| 31000 | `0x7918` | Device name | `char[20]` | Yes | Returns `"BI_2.5_2.5"` padded with zeros; read quantity must be `10` registers |
| 30400 | `0x76C0` | Software version | `u16`, 0.01 | Yes | Returns `0x0067` (103 -> 1.03); read quantity must be `1` |
| 30401 | `0x76C1` | Firmware version | `u16` | Yes | Returns `0x0099` (153); read quantity must be `1` |
| 30402 | `0x76C2` | Device MAC address | `char[12]` | Yes | Returns `"A1B2C3D4E5F6"`; read quantity must be `6` registers |
| 32100 | `0x7D64` | Battery voltage (average) | `u16`, 0.01 V | Yes | Returns `5120` (51.20 V); supports quantity `1` or `4` |
| 32101 | `0x7D65` | Battery current (average) | `s16`, 0.01 A | Yes | Returns `1502` (15.02 A); read quantity must be `1` |
| 32102 | `0x7D66` | Battery power | `s32`, 1 W | Yes | Returns `2500` W; spans registers `32102` and `32103`; read quantity must be `2` |

Reading address `0x7D64` with quantity `4` returns voltage, current and power in a single response (registers `32100..32103`).

## Findings and open issues

1. **Baud rate**: chip and ESP32 firmware are now both set to **115200 baud, 8N1**, matching the Marstek datasheet.
2. **Static/example values**: all register values are hard-coded examples taken from the CSV. A more dynamic state machine (charging/discharging/idle) is needed later.
3. **Only FC 0x03 supported**: write functions (0x06, 0x10) and read input registers (0x04) return an illegal-function exception.
4. **RE/DE pins not emulated**: the chip does not model the MAX485 direction-control behaviour; it simply receives on `DI` and transmits on `RO`. This matches the Wokwi simulation because the ESP32 already drives RE/DE.
5. **CRC validation**: incoming frames are CRC-checked and responses include a valid Modbus RTU CRC-16.
6. **Firmware loop**: `src/Marstek-ModBus-Gateway.ino.cpp` now reads registers `32100..32103` and prints voltage, current and power instead of sending raw bytes.

## TODO — implement all function ID registers

Based on `specs/Modbus Table - Modbus Datasheet.csv`:

### Read holding registers (FC 0x03)

- [x] 31000 — Device name
- [x] 32100 — Battery voltage (average)
- [x] 32101 — Battery current (average)
- [x] 32102 — Battery power
- [ ] 30000..30010 — Average/grid measurements
- [ ] 30100, 30200, 30300..30303 — Status / WiFi / BT / Cloud
- [ ] 30399 — BMS version (same as 31102)
- [x] 30400, 30401, 30402 — Software/firmware version + MAC
- [ ] 30500..30800 — Misc status
- [ ] 31100..31102 — Soft/firmware/BMS version
- [ ] 31200 — SN code
- [ ] 32104, 32105 — Battery SoC / total energy
- [ ] 32200..32204 — AC measurements
- [ ] 32300..32302 — AC off-grid measurements
- [ ] 33000..33010 — Energy counters
- [ ] 35000..35011 — Temperatures
- [ ] 35100 — Inverter state
- [ ] 35110..35112 — Charge/discharge limits
- [ ] 36000..36104 — Alarm / fault words
- [ ] 37000..37013 — Misc / cell information
- [ ] 41000, 41001, 41010, 41100, 41200, 41500, 41516 — Control / config
- [ ] 42000, 42010..42021, 43000..43129 — RS485 control / time-of-use settings
- [ ] 44000..44100, 45603, 45604 — Cutoff / grid standards / unknown

### Write functions

- [ ] FC 0x06 — Write single register (e.g. 41000 restart, 41100 Modbus address)
- [ ] FC 0x10 — Write multiple registers (e.g. time-of-use slots, WiFi credentials)

## Reference

- Modbus register table: `../specs/Modbus Table - Modbus Datasheet.csv`
- ESP32 firmware: `../src/Marstek-ModBus-Gateway.ino.cpp`
