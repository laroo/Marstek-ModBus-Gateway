/**
 * Marstek.h - Marstek Venus-E battery Modbus RTU interface
 *
 * Wraps ModbusMaster over an RS485 transceiver (MAX485) and keeps a
 * telemetry snapshot updated. Accepts charge/discharge/stop commands,
 * shared by serial, MQTT and the web interface.
 */

#ifndef Marstek_h
#define Marstek_h

#include "Arduino.h"
#include <ModbusMaster.h>

// ============================================================================
// RS485 PIN DEFINITIONS (WT32-ETH01)
// ============================================================================
// GPIO23/22 are not usable on the WT32-ETH01 (internal Ethernet MDC/RMII),
// so DE/RE are mapped to GPIO15/14 (schematic NET_OUTPUT1/NET_OUTPUT2).
constexpr int RS485_RX_PIN = 32;      // to MAX485 RO (Receiver Output)
constexpr int RS485_TX_PIN = 33;      // to MAX485 DI (Driver Input)
constexpr int RS485_DE_PIN = 15;      // to MAX485 DE (Driver Enable)
constexpr int RS485_RE_PIN = 14;      // to MAX485 RE (Receiver Enable)
constexpr long RS485_BAUD_RATE = 115200;
constexpr uint8_t MODBUS_SLAVE_ID = 1;

// ============================================================================
// CONTROL MODE ENUMERATION
// ============================================================================
enum class MarstekControlMode : uint8_t {
    Idle,           // RS485 control disabled
    Charging,       // Forcible charge active
    Discharging     // Forcible discharge active
};

// ============================================================================
// TELEMETRY SNAPSHOT
// ============================================================================
struct MarstekTelemetry {
    float batteryVoltage = 0.0f;        // V   (32100, u16, 0.01 V)
    float batteryCurrent = 0.0f;        // A   (32101, s16, 0.01 A)
    int32_t batteryPower = 0;           // W   (32102-32103, s32)
    float acVoltage = 0.0f;             // V   (32200, u16, 0.1 V)
    float acCurrent = 0.0f;             // A   (32201, u16, 0.01 A)
    int32_t acPower = 0;                // W   (32202-32203, s32)
    float acFrequency = 0.0f;           // Hz  (32204, u16, 0.01 Hz)
    uint16_t softwareVersion = 0;       //     (30400, u16, 0.01)
    uint16_t firmwareVersion = 0;       //     (30401, u16)
    char mac[13] = {0};                 //     (30402, char[12])
    unsigned long lastUpdateMs = 0;     // millis() of last successful poll
    bool valid = false;                 // true after first successful poll
};

// ============================================================================
// MARSTEK CLASS DECLARATION
// ============================================================================
class Marstek {
public:
    Marstek();
    ~Marstek();

    /**
     * Initialize RS485 serial, direction pins and Modbus master.
     * Must be called after Serial is started.
     * @param telemetryIntervalMs Interval between telemetry polls
     */
    void initialize(unsigned long telemetryIntervalMs = 4000);

    /**
     * Poll telemetry when the interval has elapsed.
     * Should be called regularly in main loop.
     * @return true if a telemetry poll completed successfully this call
     */
    bool update();

    /**
     * Parse and execute a command string (case-insensitive):
     *   charge=<0-2500>   start forcible charge (0 = stop)
     *   discharge=<0-2500> start forcible discharge (0 = stop)
     *   stop              stop charge/discharge, disable RS485 control
     * @return true if the command was recognized and executed
     */
    bool handleCommand(const String& cmd);

    /**
     * Start forcible charge at the given power.
     * @return true if all Modbus writes succeeded
     */
    bool setChargePower(uint16_t watts);

    /**
     * Start forcible discharge at the given power.
     * @return true if all Modbus writes succeeded
     */
    bool setDischargePower(uint16_t watts);

    /**
     * Stop charge/discharge and disable RS485 control mode.
     * @return true if all Modbus writes succeeded
     */
    bool stopControl();

    const MarstekTelemetry& getTelemetry() const { return _telemetry; }
    MarstekControlMode getControlMode() const { return _controlMode; }
    String getControlModeString() const;

    /**
     * Communication health: true while the last telemetry poll succeeded.
     */
    bool isHealthy() const { return _healthy; }

private:
    ModbusMaster _node;
    MarstekTelemetry _telemetry;
    MarstekControlMode _controlMode;
    unsigned long _telemetryIntervalMs;
    unsigned long _lastTelemetryMs;
    bool _healthy;
    bool _initialized;

    void _pollTelemetry();
    bool _writeRegister(uint16_t address, uint16_t value);

    // MAX485 direction control (C-style callbacks for ModbusMaster)
    static void _preTransmission();
    static void _postTransmission();
};

#endif // Marstek_h
