# Tasks — Multi-Battery Local Control

Derived from `specs/dispatcher-requirements.md`. Status: [ ] open / [x] done.

## Data / investigation

- [ ] Check MQTT broker for an instantaneous P1 **export/delivery** topic
      (e.g. `actual_delivery`, `lX_instant_power_delivery`). If none exists,
      fall back per §4 "Inputs" of the requirements doc.
- [ ] Confirm P1 units are kW at runtime (subscribe to
      `sensors/power/p1meter/actual_consumption` during known load).
- [ ] **Enphase Envoy-S integration** — the site has an Envoy-S gateway with
      16× IQ8Plus micro inverters, not yet on MQTT. Investigate its local
      API (`/production.json`, `/ivp/meters`): if it is the *metered*
      variant its consumption CTs report net import/export directly;
      otherwise PV production power at minimum. Publish to MQTT or poll
      from the dispatcher.
- [ ] Confirm Venus-E v2 real limits: max charge/discharge watts and
      minimum useful power; confirm `charge=`/`discharge=` setpoints are
      AC-side watts.

## Gateway firmware (this repo)

- [ ] Per-device MQTT topic prefix via build flag
      (`MQTT_BASE_TOPIC="marstek/batt-1"`) + unique `MQTT_CLIENT_ID`;
      replaces hardcoded `marstek-modbus-gateway` in `mqttmanager.cpp`.
- [ ] Read + publish battery SoC (reg 32104) and total energy (reg 32105):
      extend `MarstekTelemetry`, `_pollTelemetry()`, `publishStatus()`.
- [ ] Command watchdog: `stopControl()` / 0 W when no command within
      `command_timeout_s` (~15–30 s) while under RS485 control.
- [ ] MQTT LWT `availability` topic per gateway (online/offline).

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
