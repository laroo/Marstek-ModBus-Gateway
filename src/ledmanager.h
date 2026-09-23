/**
 * LEDManager.h - Marstek Modbus Gateway LED Manager Header
 *
 * Defines the LEDManager class interface for the two status LEDs.
 * LEDs are wired active-low (anode to VCC via resistor, cathode to GPIO).
 *
 * Intended usage:
 *   - flashGreen() on successful Modbus telemetry polls / commands
 *   - setError(true) while a Modbus or network error is present (solid red)
 */

#ifndef LEDManager_h
#define LEDManager_h

#include "Arduino.h"

// ============================================================================
// LED MANAGER CLASS DECLARATION
// ============================================================================
class LEDManager {
public:
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
     * Update LED states (expires one-shot flashes)
     * Should be called regularly in main loop
     */
    void update();

    /**
     * Brief green flash (e.g. successful telemetry poll or command)
     * @param ms Flash duration in milliseconds
     */
    void flashGreen(uint16_t ms = 150);

    /**
     * Brief red flash (e.g. command received)
     * @param ms Flash duration in milliseconds
     */
    void flashRed(uint16_t ms = 150);

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
    bool _error;                // Error indicator active (red solid)
    unsigned long _redOffTime;  // millis() when red flash expires (0 = none)
    unsigned long _greenOffTime;// millis() when green flash expires (0 = none)

    void _writeRed(bool on);
    void _writeGreen(bool on);
};

#endif // LEDManager_h
