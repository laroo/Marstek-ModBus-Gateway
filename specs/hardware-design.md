# Hardware Design — RS485 Power & Interface

Target: WT32-ETH01 powered from the Marstek Venus-E RS485 port (DP1710-5P),
with a MAX485-class transceiver (HW-097 module or onboard equivalent).

## Marstek connector (DP1710-5P)

| Pin | Signal | Wire color |
|-----|--------|------------|
| 1 | 485-A | Yellow |
| 2 | 485-B | Blue |
| 3 | 485-GND | Black |
| 4 | +5V out | Red |
| 5 | GND | Black |

No official current rating published; community projects run ESP32 + WiFi
adapters (Elfin EW11, M5Stack Atom, Shelly) directly from it.

## Schematic

```
  Marstek RS485 plug (DP1710-5P)                       WT32-ETH01
  ┌──────────────────┐                              ┌────────────────────────────┐
  │  pin4  +5V ──────┼───[R1]───●──────── +5V rail ─│──► 5V                      │
  │                  │  2.2Ω    │                   │         ┌────────────┐     │
  │  pin5  GND ──────┼───┐      │                   │         │ onboard    │     │
  │                  │   │     ═╪═ C1               │         │   LDO      │     │
  │  pin1  485-A ────┼───┼──┐  ═╪═ 470-1000µF       │         │   → 3V3 ───┼── 3V3
  │  pin2  485-B ────┼───┼──┤   │  (+100nF)         │         └────────────┘     │
  │  pin3  485-GND   │   │  │   │                   │                            │
  └──────────────────┘   │  │   │                   │   EN ─────┐                │
                         │  │   │                   └───────────┼────────────────┘
                    to MAX485   │                               │
                    A/B lines   │                            3V3│
                                │                             ┌─┴─┐
                                │                             │   │ R2 10k
                                │                             └─┬─┘ (pull-up)
                                │                               │
                                │              ═╪═ C2 ──────────┤
                                │              ═╪═ 10–47µF      │
                                │               │               │
                                │             ┌─┴─┐ S2          │
                                │             │   │ reset btn   │
                                │             └─┬─┘             │
                                └───────────────┴───────────────┴── GND
```

Simulate the R1 + C1 inrush/reservoir behavior:
[Falstad circuit sim](https://www.falstad.com/circuit/circuitjs.html?ctz=DwYwlgTgBAZgvAIgIwKgFwM6IAwDpsEECsqYIiSeATAVQOx0DM2AHFQGwCcndqIARoiLZUAB0EIALI1QA3CENQBbTEICmAWiQoAfACgoUYBigAPRC2xRLUJJMnWRsHGIoIA9PsPBo55HXZHKCpJK0tUeAQnBQQqXCoPLyMAdzNESRYgm3DnKMSDFLTkeyCkdjCnSJFPAuBUvzKK2wDHCJca73r0zMYiKltyqF6E3OqkuqLGob6h7AdhtryOoxAi5gd6fvXbe0Wncij8bBQoMFkcI5OMGKdZABNELVwkIkl2RiROSSJ2SjKqFiaJAyZYTPwhJrbHJVfKdNZzHbzBHQ9rjLoIbbDWYbOgjGGg9GYmaSMrTPGo2qE5FWEmBFFLca+dKkiFQWkDSouKAxFAEorsqbs+ljSn8lmhNmkhajWFGMD8nrEzLCvgAVwuJCgSnOCBIoNEk0GNim0siMi1ai5GAO0UtyEIDtlwANfiISH6NjdWz6i3NSjtTmtXIgdsoDoITvRXrJksCpopcL8Qqs0ZV+qKGgyQUzivJGOUAdQQby3ND4YjfL8OZjWfjDNFVazNibnPr3gA9lA1AA7RC8KAYUSIdiSRamc1DvJJbwGnWBm1HAgBKicRhfIgfKh66dGWeIc3FvAOnjsH5bp3udvjTs9igHycjscTtw751QOdFhcO5er9eb7cFDO777p+FzHgEZ4Ad4l7Xl2vYloOw6jrk46uLECCvnuCEHJQspATqB5foQJ6QReV61AAhncABWXZ3qgahgFyt4IAASmoGBgBgaAUd2IBqFAAAU7YABZKBgACUyjwScSgNBGA5oIg7GcdxvH8fkwDuOAED6EAA)

## MAX485 logic side → ESP32 GPIO

```
  HW-097 (5V)                                      ESP32
  ┌──────────┐
  │  RO ─────┼──[R3 1k]───●─────────────── GPIO32 (RS485_RX)
  │          │            │
  │          │          ═╪═ (node)        │
  │          │            │
  │          │           [R4 2k]
  │          │            │
  │          │           GND
  │          │
  │  DI ─────┼──────────────────────────── GPIO33 (RS485_TX)
  │  DE ─────┼──────────────────────────── GPIO15 (RS485_DE)
  │  /RE ────┼──────────────────────────── GPIO14 (RS485_RE)
  │  VCC ────┼── +5V rail (after R1)
  │  GND ────┼── GND
  │  A ──────┼── Marstek pin1
  │  B ──────┼── Marstek pin2
  └──────────┘
```

| Signal | Direction | Connection |
|--------|-----------|------------|
| RO → GPIO32 | module → ESP32 | **through divider** R3=1k (top) / R4=2k (bottom): 5V → 3.33V |
| DI → GPIO33 | ESP32 → module | direct (MAX485 VIH = 2V, accepts 3.3V logic) |
| DE → GPIO15 | ESP32 → module | direct |
| /RE → GPIO14 | ESP32 → module | direct |
| VCC | — | +5V rail |
| GND | — | common ground |

## BOM

| Ref | Value | Purpose |
|-----|-------|---------|
| R1 | 2.2Ω (1–4.7Ω ok), ≥0.25W | Inrush limit for C1; Marstek sees ~2A peak instead of a short |
| C1 | 470–1000µF ≥10V electrolytic + 100nF ceramic | Reservoir for ESP32 burst currents |
| R2 | 10k | EN pull-up (exists as R25 on current PCB) |
| C2 | 22µF (10–47µF range) | EN boot delay: ~100–500ms, lets C1 charge before ESP32 starts |
| R3 | 1k | RO divider top |
| R4 | 2k | RO divider bottom |
| S2 | tact switch | Reset (EN→GND); also discharges C2 = clean delayed reboot |

## Notes

- Tie 485-GND (pin 3) and power GND (pin 5) together at the connector —
  bus reference and supply ground must be the same node.
- **Do not** power the board from Marstek 5V and the 24V rail at the same time.
- With EN low the ESP32 draws ~nothing, so C1 charges in ~5–25ms through R1;
  the C2 delay covers it with large margin.
- A voltage supervisor (e.g. CAT809 4.63V variant on the 5V rail driving EN)
  can replace R2+C2 for precise brownout protection.
- GPIO12/15/2 are ESP32 strapping pins — keep external loads off them or ensure
  they read the right level at boot. GPIO15 is used for DE here; the MAX485 DE
  input idles whatever its module pull does — fine since firmware drives it
  low early in setup().
- If respinning the transceiver onto the PCB: use a 3.3V part (MAX3485 /
  SP3485) — eliminates the R3/R4 divider entirely and runs off the 3V3 rail.
- DE and /RE may be tied together and driven from one GPIO if a pin is ever
  needed; current firmware drives them separately on 15/14.
