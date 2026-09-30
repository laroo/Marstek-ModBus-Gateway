/**
 * Marstek.cpp - Marstek Venus-E battery Modbus RTU interface
 *
 * Implementation of Modbus polling and charge/discharge control for the
 * Marstek Venus-E v2.0 over RS485. Register map:
 * specs/Modbus Table - Modbus Datasheet.csv
 */

#include "marstek.h"
#include <HardwareSerial.h>

// ============================================================================
// MODBUS REGISTER ADDRESSES
// ============================================================================
static constexpr uint16_t REG_BATTERY_BASE      = 0x7D64; // 32100..32103: V, I, P
static constexpr uint16_t REG_SW_VERSION        = 0x76C0; // 30400: software version (0.01)
static constexpr uint16_t REG_FW_VERSION        = 0x76C1; // 30401: firmware version
static constexpr uint16_t REG_MAC               = 0x76C2; // 30402: device MAC (6 regs)
static constexpr uint16_t REG_AC_BASE           = 0x7DC8; // 32200..32204: V, I, P, freq
static constexpr uint16_t REG_RS485_CONTROL     = 0xA410; // 42000: RS485 control mode
static constexpr uint16_t REG_FORCE_MODE        = 0xA41A; // 42010: forcible charge/discharge
static constexpr uint16_t REG_CHARGE_POWER      = 0xA424; // 42020: forcible charge power (W)
static constexpr uint16_t REG_DISCHARGE_POWER   = 0xA425; // 42021: forcible discharge power (W)

static constexpr uint16_t CTRL_MODE_ENABLE      = 0x55AA;
static constexpr uint16_t CTRL_MODE_DISABLE     = 0x55BB;
static constexpr uint16_t FORCE_STOP            = 0x0000;
static constexpr uint16_t FORCE_CHARGE          = 0x0001;
static constexpr uint16_t FORCE_DISCHARGE       = 0x0002;

static constexpr uint16_t MAX_POWER_W           = 2500;

// ============================================================================
// MARSTEK CLASS IMPLEMENTATION
// ============================================================================

Marstek::Marstek()
    : _controlMode(MarstekControlMode::Idle),
      _telemetryIntervalMs(4000),
      _lastTelemetryMs(0),
      _consecutiveErrors(0),
      _offline(false),
      _healthy(false),
      _initialized(false) {
    Serial.println("[MARSTEK] Marstek constructor called");
}

Marstek::~Marstek() {
    Serial.println("[MARSTEK] Marstek destructor called");
}

void Marstek::initialize(unsigned long telemetryIntervalMs) {
    _telemetryIntervalMs = telemetryIntervalMs;

    // MAX485 direction pins, start in receive mode
    pinMode(RS485_DE_PIN, OUTPUT);
    pinMode(RS485_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);
    digitalWrite(RS485_RE_PIN, LOW);

    Serial2.begin(RS485_BAUD_RATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    _node.begin(MODBUS_SLAVE_ID, Serial2);
    _node.preTransmission(_preTransmission);
    _node.postTransmission(_postTransmission);

    _initialized = true;
    Serial.println("[MARSTEK] Initialized - RS485 @ 115200 8N1, slave ID 1");
}

bool Marstek::update() {
    if (!_initialized) {
        return false;
    }

    unsigned long now = millis();
    unsigned long interval = _offline ? OFFLINE_RETRY_MS : _telemetryIntervalMs;
    if (_lastTelemetryMs == 0 || now - _lastTelemetryMs >= interval) {
        _lastTelemetryMs = now;

        if (_offline) {
            // Cheap probe: single register read until the slave responds
            if (_probe()) {
                _markOnline();
                return true;
            }
            return false;
        }

        _pollTelemetry();
        return _healthy;
    }
    return false;
}

// ============================================================================
// COMMANDS
// ============================================================================

bool Marstek::handleCommand(const String& rawCmd) {
    String cmd = rawCmd;
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.startsWith("charge=")) {
        int watts = cmd.substring(7).toInt();
        if (watts < 0 || watts > MAX_POWER_W) {
            Serial.println("[MARSTEK] Charge power must be 0-2500 W");
            return false;
        }
        return watts == 0 ? stopControl() : setChargePower((uint16_t)watts);
    }

    if (cmd.startsWith("discharge=")) {
        int watts = cmd.substring(10).toInt();
        if (watts < 0 || watts > MAX_POWER_W) {
            Serial.println("[MARSTEK] Discharge power must be 0-2500 W");
            return false;
        }
        return watts == 0 ? stopControl() : setDischargePower((uint16_t)watts);
    }

    if (cmd == "stop") {
        return stopControl();
    }

    Serial.print("[MARSTEK] Unknown command: ");
    Serial.print(cmd);
    Serial.println(" (use: charge=<watts>, discharge=<watts>, stop)");
    return false;
}

bool Marstek::setChargePower(uint16_t watts) {
    if (watts > MAX_POWER_W) {
        Serial.println("[MARSTEK] Charge power must be 0-2500 W");
        return false;
    }

    Serial.printf("[MARSTEK] Starting charge at %u W\n", watts);
    if (!_writeRegister(REG_RS485_CONTROL, CTRL_MODE_ENABLE)) return false;
    if (!_writeRegister(REG_CHARGE_POWER, watts)) return false;
    if (!_writeRegister(REG_FORCE_MODE, FORCE_CHARGE)) return false;
    _controlMode = MarstekControlMode::Charging;
    _markOnline();
    Serial.println("[MARSTEK] Charge active");
    return true;
}

bool Marstek::setDischargePower(uint16_t watts) {
    if (watts > MAX_POWER_W) {
        Serial.println("[MARSTEK] Discharge power must be 0-2500 W");
        return false;
    }

    Serial.printf("[MARSTEK] Starting discharge at %u W\n", watts);
    if (!_writeRegister(REG_RS485_CONTROL, CTRL_MODE_ENABLE)) return false;
    if (!_writeRegister(REG_DISCHARGE_POWER, watts)) return false;
    if (!_writeRegister(REG_FORCE_MODE, FORCE_DISCHARGE)) return false;
    _controlMode = MarstekControlMode::Discharging;
    _markOnline();
    Serial.println("[MARSTEK] Discharge active");
    return true;
}

bool Marstek::stopControl() {
    Serial.println("[MARSTEK] Stopping control");
    bool ok = _writeRegister(REG_FORCE_MODE, FORCE_STOP);
    ok = _writeRegister(REG_RS485_CONTROL, CTRL_MODE_DISABLE) && ok;
    if (ok) {
        _controlMode = MarstekControlMode::Idle;
        _markOnline();
        Serial.println("[MARSTEK] Control stopped");
    }
    return ok;
}

String Marstek::getControlModeString() const {
    switch (_controlMode) {
        case MarstekControlMode::Charging:    return "CHARGING";
        case MarstekControlMode::Discharging: return "DISCHARGING";
        case MarstekControlMode::Idle:
        default:                            return "IDLE";
    }
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void Marstek::_pollTelemetry() {
    uint8_t result;
    bool ok = true;

    // Battery voltage, current and power (registers 32100..32103)
    result = _node.readHoldingRegisters(REG_BATTERY_BASE, 4);
    if (result == _node.ku8MBSuccess) {
        _telemetry.batteryVoltage = _node.getResponseBuffer(0) / 100.0f;
        _telemetry.batteryCurrent = (int16_t)_node.getResponseBuffer(1) / 100.0f;
        _telemetry.batteryPower = ((int32_t)(int16_t)_node.getResponseBuffer(2) << 16)
                                  | _node.getResponseBuffer(3);
        Serial.printf("[MARSTEK] Vbatt: %.2f V, Ibatt: %.2f A, Pbatt: %ld W\n",
                      _telemetry.batteryVoltage, _telemetry.batteryCurrent,
                      (long)_telemetry.batteryPower);
    } else {
        Serial.printf("[MARSTEK] Battery read error: 0x%02X\n", result);
        ok = false;
    }

    // Software version (30400, u16, 0.01 scale)
    result = _node.readHoldingRegisters(REG_SW_VERSION, 1);
    if (result == _node.ku8MBSuccess) {
        _telemetry.softwareVersion = _node.getResponseBuffer(0);
    } else {
        Serial.printf("[MARSTEK] Software version read error: 0x%02X\n", result);
        ok = false;
    }

    // Firmware version (30401, u16)
    result = _node.readHoldingRegisters(REG_FW_VERSION, 1);
    if (result == _node.ku8MBSuccess) {
        _telemetry.firmwareVersion = _node.getResponseBuffer(0);
    } else {
        Serial.printf("[MARSTEK] Firmware version read error: 0x%02X\n", result);
        ok = false;
    }

    // Device MAC address (30402, char[12] / 6 registers)
    result = _node.readHoldingRegisters(REG_MAC, 6);
    if (result == _node.ku8MBSuccess) {
        for (uint8_t i = 0; i < 6; i++) {
            uint16_t reg = _node.getResponseBuffer(i);
            _telemetry.mac[i * 2] = reg >> 8;
            _telemetry.mac[i * 2 + 1] = reg & 0xFF;
        }
        _telemetry.mac[12] = '\0';
    } else {
        Serial.printf("[MARSTEK] MAC read error: 0x%02X\n", result);
        ok = false;
    }

    // AC measurements (32200..32204): voltage, current, power, frequency
    result = _node.readHoldingRegisters(REG_AC_BASE, 5);
    if (result == _node.ku8MBSuccess) {
        _telemetry.acVoltage = _node.getResponseBuffer(0) / 10.0f;
        _telemetry.acCurrent = _node.getResponseBuffer(1) / 100.0f;
        _telemetry.acPower = ((int32_t)(int16_t)_node.getResponseBuffer(2) << 16)
                             | _node.getResponseBuffer(3);
        _telemetry.acFrequency = _node.getResponseBuffer(4) / 100.0f;
        Serial.printf("[MARSTEK] Vac: %.1f V, Iac: %.2f A, Pac: %ld W, Fac: %.2f Hz\n",
                      _telemetry.acVoltage, _telemetry.acCurrent,
                      (long)_telemetry.acPower, _telemetry.acFrequency);
    } else {
        Serial.printf("[MARSTEK] AC read error: 0x%02X\n", result);
        ok = false;
    }

    if (ok) {
        _telemetry.lastUpdateMs = millis();
        _telemetry.valid = true;
        _consecutiveErrors = 0;
    } else {
        _consecutiveErrors++;
        if (_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS && !_offline) {
            _offline = true;
            Serial.println("[MARSTEK] No Modbus slave responding, going offline"
                           " (probe every 30 s)");
        }
    }
    _healthy = ok;
}

bool Marstek::_probe() {
    uint8_t result = _node.readHoldingRegisters(REG_BATTERY_BASE, 4);
    if (result == _node.ku8MBSuccess) {
        Serial.println("[MARSTEK] Modbus probe succeeded");
        return true;
    }
    Serial.printf("[MARSTEK] Modbus probe failed: 0x%02X\n", result);
    return false;
}

void Marstek::_markOnline() {
    _consecutiveErrors = 0;
    _healthy = true;
    if (_offline) {
        _offline = false;
        _lastTelemetryMs = 0; // force a full poll on next update()
        Serial.println("[MARSTEK] Modbus slave responding, telemetry resumed");
    }
}

void Marstek::setIdleCallback(void (*callback)()) {
    _node.idle(callback);
}

bool Marstek::_writeRegister(uint16_t address, uint16_t value) {
    uint8_t result = _node.writeSingleRegister(address, value);
    if (result != _node.ku8MBSuccess) {
        Serial.printf("[MARSTEK] Write 0x%04X = 0x%04X failed: 0x%02X\n",
                      address, value, result);
        _healthy = false;
        return false;
    }
    return true;
}

void Marstek::_preTransmission() {
    // Switch MAX485 to transmit mode
    digitalWrite(RS485_DE_PIN, HIGH);
    digitalWrite(RS485_RE_PIN, HIGH);
}

void Marstek::_postTransmission() {
    // Switch MAX485 back to receive mode
    digitalWrite(RS485_DE_PIN, LOW);
    digitalWrite(RS485_RE_PIN, LOW);
}
