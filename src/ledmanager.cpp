/**
 * LEDManager.cpp - Marstek Modbus Gateway LED Manager Implementation
 *
 * Two active-low status LEDs showing boot progress and runtime state;
 * see ledmanager.h for the phase descriptions.
 */

#include "ledmanager.h"

// ============================================================================
// LED MANAGER CLASS IMPLEMENTATION
// ============================================================================

LEDManager::LEDManager(int redPin, int greenPin, bool activeLow)
    : _redPin(redPin), _greenPin(greenPin), _activeLow(activeLow),
      _initialized(false), _phase(Phase::Boot), _error(false),
      _blinkOn(false), _blinkLast(0), _greenBlipUntil(0),
      _redOn(false), _greenOn(false) {
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
    bool red = false;
    bool green = false;

    switch (_phase) {
        case Phase::Boot:
            red = _blink(now, 500);  // 1 s period
            break;
        case Phase::NetConnecting:
            red = true;
            break;
        case Phase::MqttConnecting:
            green = _blink(now, 100);  // 200 ms period
            red = _error;
            break;
        case Phase::Online:
            green = !(_greenBlipUntil && (long)(now - _greenBlipUntil) < 0);
            red = _error;
            break;
    }

    _apply(red, green);
}

void LEDManager::setPhase(Phase phase) {
    if (!_initialized || phase == _phase) {
        return;
    }
    _phase = phase;
    _blinkOn = true;  // start each blink phase with the LED on
    _blinkLast = millis();
    Serial.printf("[LED] Phase -> %d\n", (int)phase);
}

void LEDManager::blipGreen(uint16_t ms) {
    if (!_initialized || _phase != Phase::Online) {
        return;
    }
    _greenBlipUntil = millis() + ms;
    _apply(_error, false);
}

void LEDManager::setError(bool active) {
    if (!_initialized || _error == active) {
        return;
    }
    _error = active;
    Serial.printf("[LED] Error indicator %s\n", active ? "ON" : "OFF");
}

void LEDManager::allOff() {
    _error = false;
    _greenBlipUntil = 0;
    _apply(false, false);
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

bool LEDManager::_blink(unsigned long now, uint16_t halfPeriodMs) {
    if (now - _blinkLast >= halfPeriodMs) {
        _blinkLast = now;
        _blinkOn = !_blinkOn;
    }
    return _blinkOn;
}

void LEDManager::_apply(bool redOn, bool greenOn) {
    if (redOn != _redOn) {
        _redOn = redOn;
        _writeRed(redOn);
    }
    if (greenOn != _greenOn) {
        _greenOn = greenOn;
        _writeGreen(greenOn);
    }
}

void LEDManager::_writeRed(bool on) {
    digitalWrite(_redPin, (on == _activeLow) ? LOW : HIGH);
}

void LEDManager::_writeGreen(bool on) {
    digitalWrite(_greenPin, (on == _activeLow) ? LOW : HIGH);
}
