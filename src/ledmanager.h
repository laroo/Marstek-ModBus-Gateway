/**
 * LEDManager.h - Marstek Modbus Gateway LED Manager Header
 *
 * Defines the LEDManager class interface for the two status LEDs.
 * LEDs are wired active-low (anode to VCC via resistor, cathode to GPIO).
 *
 * The LEDs show boot progress and runtime state:
 *   - Phase::Boot           red blinks (1 s period) until network init starts
 *   - Phase::NetConnecting  red solid while DHCP / link comes up
 *   - Phase::MqttConnecting green blinks fast (200 ms period) until MQTT connects
 *   - Phase::Online         green solid; blipGreen() dips it on each publish
 * A Modbus/network error turns the red LED on in the MqttConnecting and
 * Online phases via setError().
 */

#ifndef LEDManager_h
#define LEDManager_h

#include "Arduino.h"

// ============================================================================
// LED MANAGER CLASS DECLARATION
// ============================================================================
class LEDManager {
public:
    // Boot/connection progress phases
    enum class Phase : uint8_t {
        Boot,           // red blink, before network init
        NetConnecting,  // red solid, DHCP/link in progress
        MqttConnecting, // green fast blink, IP up but MQTT not connected
        Online          // green solid, MQTT connected
    };

    /**
     * Constructor - Initialize LED manager
     * @param redPin GPIO pin for red LED (error indicator)
     * @param greenPin GPIO pin for green LED (activity indicator)
     * @param activeLow true when a LOW level turns the LED on
     */
    LEDManager(int redPin, int greenPin, bool activeLow = true);

    /**
     * Destructor - Clean up resources
     */
    ~LEDManager();

    /**
     * Initialize LED manager and GPIO pins
     */
    void initialize();

    /**
     * Update LED states (drives phase blinking and blip expiry)
     * Should be called regularly in main loop
     */
    void update();

    /**
     * Set the boot/connection phase
     * @param phase New phase
     */
    void setPhase(Phase phase);

    /**
     * Brief green dip (e.g. on MQTT publish). Only visible in Online phase.
     * @param ms Dip duration in milliseconds
     */
    void blipGreen(uint16_t ms = 75);

    /**
     * Error indicator: solid red while active
     * @param active true while the error condition is present
     */
    void setError(bool active);

    /**
     * Turn off both LEDs
     */
    void allOff();

private:
    int _redPin;                // Red LED pin (error indicator)
    int _greenPin;              // Green LED pin (activity indicator)
    bool _activeLow;            // LED polarity
    bool _initialized;          // Flag indicating initialization complete
    Phase _phase;               // Current boot/connection phase
    bool _error;                // Error indicator active (red solid)
    bool _blinkOn;              // Current blink output state
    unsigned long _blinkLast;   // millis() of last blink toggle
    unsigned long _greenBlipUntil; // millis() when the publish blip ends
    bool _redOn;                // Last applied red output
    bool _greenOn;              // Last applied green output

    bool _blink(unsigned long now, uint16_t halfPeriodMs);
    void _apply(bool redOn, bool greenOn);
    void _writeRed(bool on);
    void _writeGreen(bool on);
};

#endif // LEDManager_h
