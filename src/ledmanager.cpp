/**
 * LEDManager.cpp - Marstek Modbus Gateway LED Manager Implementation
 *
 * Two active-low status LEDs: green flashes on Modbus activity,
 * red is solid while an error condition is present.
 */

#include "ledmanager.h"

// ============================================================================
// LED MANAGER CLASS IMPLEMENTATION
// ============================================================================

LEDManager::LEDManager(int redPin, int greenPin, bool activeLow)
    : _redPin(redPin), _greenPin(greenPin), _activeLow(activeLow),
      _initialized(false), _error(false),
      _redOffTime(0), _greenOffTime(0) {
    Serial.println("[LED] LEDManager constructor called");
}

LEDManager::~LEDManager() {
    Serial.println("[LED] LEDManager destructor called");
    allOff();
}

void LEDManager::initialize() {
    Serial.println("[LED] Initializing LED manager...");

    pinMode(_redPin, OUTPUT);
    pinMode(_greenPin, OUTPUT);
    allOff();

    _initialized = true;

    Serial.printf("[LED] LED manager initialized - Red pin: %d, Green pin: %d (active %s)\n",
                  _redPin, _greenPin, _activeLow ? "low" : "high");
}

void LEDManager::update() {
    if (!_initialized) {
        return;
    }

    unsigned long now = millis();
    if (_greenOffTime && now >= _greenOffTime) {
        _greenOffTime = 0;
        _writeGreen(false);
    }
    if (_redOffTime && now >= _redOffTime) {
        _redOffTime = 0;
        _writeRed(_error); // restore error state after a flash
    }
}

void LEDManager::flashGreen(uint16_t ms) {
    if (!_initialized) {
        return;
    }
    _greenOffTime = millis() + ms;
    _writeGreen(true);
}

void LEDManager::flashRed(uint16_t ms) {
    if (!_initialized) {
        return;
    }
    _redOffTime = millis() + ms;
    _writeRed(true);
}

void LEDManager::setError(bool active) {
    if (!_initialized) {
        return;
    }
    if (_error == active) {
        return;
    }
    _error = active;
    if (!_redOffTime) {
        _writeRed(active);
    }
    Serial.printf("[LED] Error indicator %s\n", active ? "ON" : "OFF");
}

void LEDManager::allOff() {
    _error = false;
    _redOffTime = 0;
    _greenOffTime = 0;
    _writeRed(false);
    _writeGreen(false);
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void LEDManager::_writeRed(bool on) {
    digitalWrite(_redPin, (on == _activeLow) ? LOW : HIGH);
}

void LEDManager::_writeGreen(bool on) {
    digitalWrite(_greenPin, (on == _activeLow) ? LOW : HIGH);
}
