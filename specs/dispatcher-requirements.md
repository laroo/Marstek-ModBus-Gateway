# Multi-Battery Dispatch — Requirements

## 1. Context & Goal

Three Marstek Venus-E v2.0 batteries, each behind its own ESP32-ETH01
Modbus↔MQTT gateway (this firmware). The Marstek cloud is out of the loop —
it responds too slowly and schedules batteries randomly.

Goal: fully local, structured control over MQTT:

- **Charge from PV surplus only** (zero-export charging).
- **Discharge to cover house load only** — never feed into the grid
  (net import stays ≈ 0 W).
- **Lead-battery rotation**: charge the *lowest-SoC* battery to full,
  discharge the *highest-SoC* battery to empty. Batteries take full turns
  instead of shallow-cycling in parallel; the fleet cycles in a structured
  way while the net meter stays ≈ 0.
- House power usage comes from a **P1 meter already on MQTT**.
- **Home Assistant is monitoring/visualization only** — plus optional
  manual overrides. All control logic lives in a **dedicated Docker
  service** ("dispatcher").

## 2. Architecture

```
        ┌──────────┐   telemetry (per gateway)   ┌─────────────┐
        │ Gateway 1│ ──────────────────────────► │             │
        │ Gateway 2│ ──────────────────────────► │  DISPATCHER │──┐
        │ Gateway 3│ ──────────────────────────► │  (Docker)   │  │ commands
        └──────────┘                              │             │◄─┘ (per gateway)
        P1 meter ──── house net power ──────────► └──────┬──────┘
                                                        │ state/availability
        Home Assistant ◄───── sensors / overrides ──────┘
```

- One dispatcher instance controls all 3 batteries.
- Each gateway is a dumb actuator: publishes telemetry, executes
  `charge=<W>` / `discharge=<W>` / `stop` commands.
- The dispatcher never talks Modbus directly — MQTT only.

## 3. MQTT Topic Scheme (target)

Per-device prefix — requires a firmware change (see §5):

```
marstek/<id>/status              Idle|Charging|Discharging|MISSING
marstek/<id>/battery/voltage|current|power
marstek/<id>/battery/soc         ← new (register 32104)
marstek/<id>/battery/energy      ← new (register 32105)
marstek/<id>/ac/voltage|current|power|frequency
marstek/<id>/modbus/healthy      0|1
marstek/<id>/availability        online|offline  (LWT)
marstek/<id>/command             charge=<W>|discharge=<W>|stop  (subscribed)

sensors/power/p1meter/*          P1 meter, per-field DSMR topics (existing):
  actual_consumption             instantaneous import, ~kW (see units note)
  l1|l2|l3_instant_power_usage   per-phase import (published as 11_/12_/13_*)
  l1|l2|l3_instant_power_current per-phase current, A
  l1|l2|l3_voltage               per-phase voltage, V
  consumption_*_tarif            cumulative import counters, kWh
  returndelivery_*_tarif         cumulative export counters, kWh
  actual_tarif_group             active tariff group
  gas_meter_m3                   gas counter

dispatcher/state                 JSON: mode, per-battery decision, setpoints
dispatcher/mode                  auto|manual|off  (retained, settable)
dispatcher/override/<id>         charge=<W>|discharge=<W>|stop|auto
```

**P1 units note**: observed example `actual_consumption=1.282` with
per-phase usage 0.881+0.291+0.109 (=1.281) indicates the values are **kW**
(≈1282 W total; per-phase W ≈ V×A×pf also matches). Dispatcher treats them
as kW by default but keeps a `p1_unit` config flag and a runtime sanity
check (e.g. flag values < ~5 W when phases show current flowing).

## 4. Functional Requirements — Dispatcher

### Inputs

- Per-gateway telemetry (SoC, battery power, AC power, health, availability).
- P1 net power `P_net`: positive = importing, negative = exporting/surplus.
  Derived from `sensors/power/p1meter/actual_consumption` (import) and an
  instantaneous export source — **TBD**: the observed topic list has only
  cumulative `returndelivery_*_tarif` counters, no instantaneous export
  field. Options, in order of preference:
  1. Check the broker for a delivery/return topic (e.g. `actual_delivery`,
     `lX_instant_power_delivery`) — many DSMR publishers emit one.
  2. **Enphase Envoy-S local API** (exists on-site, 16× IQ8Plus, not on
     MQTT yet — see `specs/tasks.md`). If it is the metered variant its
     consumption CTs report net power incl. export directly; otherwise PV
     production + P1 import still bounds the surplus.
  3. Last resort: cumulative `returndelivery_*_tarif` counters
     differentiated over time (coarse, lagging).
- Mode/override topics (HA or manual MQTT).

### Control loop (every ~2–5 s, configurable)

1. **Deadband**: |P_net| < `deadband_w` (default ~50 W) → hold current
   allocation; no commands sent.
2. **Deficit** (P_net > 0): discharge mode.
   - Designated discharger = healthy battery with **highest SoC**.
   - **Stickiness**: once appointed, it stays the discharger until SoC ≤
     `soc_min` (or it goes unhealthy), then the next-highest takes over.
   - Demand > its max output → additionally enable the next-highest-SoC
     battery; cascade until demand is met or fleet is saturated.
   - Total discharge setpoint ≤ P_net (never export).
3. **Surplus** (P_net < 0): charge mode.
   - Designated charger = healthy battery with **lowest SoC**.
   - Stickiness until SoC ≥ `soc_max`, then next-lowest.
   - Surplus > its max charge power → spill to next-lowest battery.
   - Total charge setpoint ≤ |P_net| (never import).
4. **Idle batteries**: explicit 0 W / `stop` — never left at a stale setpoint.

### Stability requirements

- Hysteresis on mode switching (charge↔discharge) — e.g. require the new
  mode condition to hold for N consecutive cycles or a margin beyond the
  deadband.
- Minimum dwell time per battery in a state (no flapping).
- Rate-limit setpoint changes; only re-send a command when the target
  differs by > `setpoint_delta_w`.

### Configurable parameters

Per battery: id/topic prefix, max charge W, max discharge W, min/max SoC,
min useful power (below which the battery is set idle rather than running
at e.g. 30 W). Global: broker/auth, P1 topic, loop interval, deadband,
hysteresis, dwell times.

### Overrides & output

- `auto` (default): run the allocation above.
- `manual`: dispatcher only forwards per-battery override commands.
- `off`: send `stop` to all batteries once, then hands off.
- Publish `dispatcher/state` JSON (mode, P_net, per-battery setpoint and
  reason) + HA MQTT discovery sensors so HA dashboards work out of the box.
- MQTT LWT on the dispatcher so gateways could (optionally) detect a dead
  controller even without commands — belt & braces next to the gateway
  watchdog below.

## 5. Functional Requirements — Gateway Firmware (this repo)

New work needed to support the dispatcher:

1. **Per-device topic prefix** — build flag e.g.
   `MQTT_BASE_TOPIC="marstek/batt-1"` + unique `MQTT_CLIENT_ID`, replacing
   the hardcoded `marstek-modbus-gateway` prefix in `mqttmanager.cpp`.
2. **Publish SoC** — add `batterySoc` (reg 32104, u16) and `batteryEnergy`
   (reg 32105) to `MarstekTelemetry` and `publishStatus()`. Required for
   the SoC-based allocation.
3. **Command watchdog** — if no command arrives within `command_timeout_s`
   (default ~15–30 s) while under RS485 control, call `stopControl()` /
   set 0 W. This is the fail-safe: **controller or broker down → batteries
   go idle**, never stuck at a stale setpoint.
4. **MQTT LWT** — per-gateway `availability` topic (`online`/`offline`).

## 6. Non-Functional

- **Packaging**: single Docker container, `restart: unless-stopped`,
  config via env vars or mounted YAML. Suggested: Python 3 + paho-mqtt
  (small; the allocation logic is a pure, testable function).
- **Latency**: react to P1 changes within ~5 s end-to-end (P1 typically
  updates every 1 s; gateway telemetry poll is 4 s).
- **Observability**: structured logging of every decision (why each
  battery got its setpoint); optional metrics.
- **Testability**: allocation core unit-testable without MQTT; integration-
  testable against the existing Wokwi MAX485 chip simulation.

## 7. Failure Handling

| Failure | Behavior |
|---|---|
| Gateway/battery offline or `modbus/healthy=0` | Excluded from allocation; next battery takes over; state flagged |
| P1 data stale (> `p1_stale_s`) | Stop discharging (set 0 W) — never risk uncontrolled export/import |
| Dispatcher crashes | Gateways hit command watchdog → idle (§5.3) |
| Broker down | Same — watchdog → idle; gateways auto-reconnect |
| Telemetry stale on one battery | Treated as offline for allocation purposes |

## 8. Open Questions

- ~~P1 topic/payload format~~ — resolved: per-field DSMR topics under
  `sensors/power/p1meter/`, values in ~kW (see §3).
- **Instantaneous export power** — not confirmed on the P1 publisher yet;
  see "Inputs" in §4 for the fallback chain incl. Envoy-S.
- "Fully charge one battery and discharge another" — interpreted as
  **alternating phases** driven by net power. If *simultaneous*
  charge-A/discharge-B (energy shuffling between batteries) is ever wanted,
  that's an explicit extra mode with round-trip losses — confirm.
- Real per-battery limits: Venus-E v2 max charge/discharge W (~2500 W?),
  and the minimum power worth running at.
- Docker host: same machine as HA/Mosquitto? Broker auth/TLS?
- Battery DC vs AC power sign conventions — verify `charge`/`discharge`
  setpoints are AC-side watts (dispatcher assumes AC).

## 9. Out of Scope

- Marstek cloud API integration.
- Implementing control logic inside Home Assistant automations.
- openEMS / EMHASS adoption — revisit only if day-ahead tariff or PV-
  forecast optimization is wanted later (they would compute a *total*
  setpoint; this dispatcher still does the 3-battery split).
