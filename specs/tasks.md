# Tasks — Multi-Battery Local Control

Derived from `specs/dispatcher-requirements.md`. Status: [ ] open / [x] done.

## Data / investigation

- [x] Check MQTT broker for an instantaneous P1 **export/delivery** topic
      — resolved: P1 gateway now publishes `sensors/power/p1meter/json`
      with `power_returned` (see requirements §3). Per-field fallback:
      `actual_returndelivery` (verify on broker if ever needed).
- [ ] Confirm P1 units are kW at runtime (subscribe to
      `sensors/power/p1meter/json` and check `power_delivered` /
      `power_returned` during known load).
- [ ] **Enphase Envoy-S integration** (fallback only, now that the P1
      JSON topic exists) — the site has an Envoy-S gateway with
      16× IQ8Plus micro inverters, not yet on MQTT. Investigate its local
      API (`/production.json`, `/ivp/meters`): if it is the *metered*
      variant its consumption CTs report net import/export directly;
      otherwise PV production power at minimum. Publish to MQTT or poll
      from the dispatcher.
- [ ] Confirm Venus-E v2 real limits: max charge/discharge watts and
      minimum useful power; confirm `charge=`/`discharge=` setpoints are
      AC-side watts.

## Gateway firmware (this repo)

- [x] Per-device MQTT topics: `marstek/gateway/<esp-mac>/` heartbeat +
      `marstek/<batt-mac>/` telemetry (implemented; `MQTT_ROOT_TOPIC`
      build flag, ESP32 efuse MAC as client/gateway id).
- [x] Read + publish battery SoC (reg 32104) and total energy (reg 32105):
      `battery/soc` + `battery/energy`.
- [ ] Command watchdog: `stopControl()` / 0 W when no command within
      `command_timeout_s` (~15–30 s) while under RS485 control.
- [x] MQTT LWT `marstek/gateway/<esp-mac>/availability` (online/offline).

## Dispatcher service (new Docker component)

- [ ] Skeleton: Python + paho-mqtt, Dockerfile, config via env/YAML,
      `restart: unless-stopped`.
- [ ] Subscriptions: `marstek/+/+` telemetry, P1 topics, mode/override.
- [ ] Allocation core (pure function, unit-tested): lead-charger =
      lowest SoC, lead-discharger = highest SoC, sticky until SoC bounds,
      cascade when demand exceeds one unit's output.
- [ ] Stability: deadband, hysteresis, min dwell time, setpoint delta
      threshold.
- [ ] Fail-safes: stale P1 → 0 W; unhealthy/stale gateway → exclude;
      mode `off` → stop all.
- [ ] Publish `dispatcher/state` JSON + HA MQTT discovery sensors; LWT.
- [ ] Integration test against Wokwi-simulated battery(es).

## Home Assistant

- [ ] Dashboard: per-battery SoC/power/mode, P1 net, dispatcher state.
- [ ] Controls bound to `dispatcher/mode` and `dispatcher/override/<id>`.
