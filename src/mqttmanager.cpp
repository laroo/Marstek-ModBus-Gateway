/**
 * MQTTManager.cpp - Marstek Modbus Gateway MQTT Manager Implementation
 *
 * Implementation of MQTT communication over Ethernet for remote battery
 * monitoring and control using PubSubClient.
 */

#include "Arduino.h"
#include "mqttmanager.h"
#include <ETH.h>

// Base topic prefix for all published values
static const char* MQTT_BASE_TOPIC = "marstek-modbus-gateway";

// Static instance pointer for callback handling
MQTTManager* MQTTManager::_instance = nullptr;

// ============================================================================
// MQTT MANAGER CLASS IMPLEMENTATION
// ============================================================================

MQTTManager::MQTTManager(const char* broker, int port, const char* clientId,
                         const char* statusTopic, const char* commandTopic,
                         const char* username, const char* password)
    : _port(port), _ethClient(nullptr), _mqttClient(nullptr),
      _initialized(false), _autoPublishEnabled(true),
      _lastPublish(0), _lastConnectionAttempt(0), _reconnectAttempts(0),
      _marstek(nullptr), _useAuth(false) {

    // Copy configuration strings
    strncpy(_broker, broker, sizeof(_broker) - 1);
    _broker[sizeof(_broker) - 1] = '\0';

    strncpy(_clientId, clientId, sizeof(_clientId) - 1);
    _clientId[sizeof(_clientId) - 1] = '\0';

    strncpy(_statusTopic, statusTopic, sizeof(_statusTopic) - 1);
    _statusTopic[sizeof(_statusTopic) - 1] = '\0';

    strncpy(_commandTopic, commandTopic, sizeof(_commandTopic) - 1);
    _commandTopic[sizeof(_commandTopic) - 1] = '\0';

    // Copy authentication credentials if provided
    if (username != nullptr && password != nullptr) {
        strncpy(_username, username, sizeof(_username) - 1);
        _username[sizeof(_username) - 1] = '\0';

        strncpy(_password, password, sizeof(_password) - 1);
        _password[sizeof(_password) - 1] = '\0';

        _useAuth = true;
        Serial.println("[MQTT] Authentication enabled");
    } else {
        _username[0] = '\0';
        _password[0] = '\0';
        Serial.println("[MQTT] Authentication disabled");
    }

    // Set static instance for callback handling
    _instance = this;

    Serial.println("[MQTT] MQTTManager constructor called");
}

MQTTManager::~MQTTManager() {
    Serial.println("[MQTT] MQTTManager destructor called");

    if (_mqttClient) {
        _mqttClient->disconnect();
        delete _mqttClient;
    }

    _instance = nullptr;
}

void MQTTManager::initialize(NetworkClient* activeClient) {
    Serial.println("[MQTT] Initializing MQTT manager...");

    _ethClient = activeClient;

    _mqttClient = new PubSubClient;
    if (!_mqttClient) {
        Serial.println("[ERROR] Failed to create MQTT client");
        return;
    }

    // Configure MQTT client
    _mqttClient->setServer(_broker, _port);
    _mqttClient->setCallback(_messageCallback);

    _initialized = true;

    Serial.print("[MQTT] MQTT manager initialized - Broker: ");
    Serial.print(_broker);
    Serial.print(":");
    Serial.println(_port);
    Serial.print("[MQTT] Client ID: ");
    Serial.println(_clientId);
    Serial.print("[MQTT] Status topic: ");
    Serial.println(_statusTopic);
    Serial.print("[MQTT] Command topic: ");
    Serial.println(_commandTopic);

    // Schedule periodic status publishing (runs when connected)
    if (_autoPublishEnabled) {
        _publishTimer.every(10000, [](void* manager) -> bool {
            return static_cast<MQTTManager*>(manager)->_publishTimerCallback(nullptr);
        }, this);
        Serial.println("[MQTT] Automatic status publishing enabled (10-second interval)");
    }
}

void MQTTManager::update() {
    if (!_initialized) {
        return;
    }

    if (!_ethClient) {
        return;
    }

    // Tick timers
    _publishTimer.tick();
    _reconnectTimer.tick();

    // Update the client reference in case connection switched between Ethernet/WiFi
    _mqttClient->setClient(*_ethClient);

    // Handle MQTT client loop
    if (_mqttClient->connected()) {
        _mqttClient->loop();
    } else {
        // Connection lost, attempt reconnection
        unsigned long currentTime = millis();
        if (currentTime - _lastConnectionAttempt >= 10000) { // Retry every 10 seconds
            Serial.println("[MQTT] Connection lost, attempting reconnection...");
            connect();
        }
    }
}

bool MQTTManager::connect() {
    if (!_initialized || !_mqttClient) {
        Serial.println("[ERROR] MQTT manager not initialized");
        return false;
    }

    _lastConnectionAttempt = millis();

    Serial.print("[MQTT] Attempting to connect to broker: ");
    Serial.print(_broker);
    Serial.print(":");
    Serial.println(_port);

    // Attempt MQTT connection with or without authentication
    bool connected;
    if (_useAuth) {
        Serial.println("[MQTT] Connecting with authentication...");
        connected = _mqttClient->connect(_clientId, _username, _password);
    } else {
        Serial.println("[MQTT] Connecting without authentication...");
        connected = _mqttClient->connect(_clientId);
    }

    if (connected) {
        Serial.println("[MQTT] Connected to broker successfully");
        _reconnectAttempts = 0;

        // Subscribe to command topic
        if (_mqttClient->subscribe(_commandTopic)) {
            Serial.print("[MQTT] Subscribed to command topic: ");
            Serial.println(_commandTopic);
        } else {
            Serial.print("[ERROR] Failed to subscribe to command topic: ");
            Serial.println(_commandTopic);
        }

        _logConnectionStatus();

        // Publish current status immediately after (re)connecting
        publishStatus();

    } else {
        _reconnectAttempts++;
        Serial.print("[ERROR] MQTT connection failed, rc=");
        Serial.print(_mqttClient->state());
        Serial.print(", attempt #");
        Serial.println(_reconnectAttempts);

        // Set up reconnection timer if not already running
        if (_reconnectAttempts == 1) {
            _reconnectTimer.every(30000, [](void* manager) -> bool {
                return static_cast<MQTTManager*>(manager)->_reconnectTimerCallback(nullptr);
            }, this);
        }
    }

    return connected;
}

bool MQTTManager::publishStatus() {
    if (!_initialized || !_mqttClient || !_mqttClient->connected()) {
        Serial.println("[ERROR] MQTT not connected, cannot publish status");
        return false;
    }

    bool statePublished = false;
    bool clientIdPublished = false;
    bool uptimePublished = false;
    char buf[24];

    if (_marstek) {
        const MarstekTelemetry& t = _marstek->getTelemetry();
        String mode = _marstek->getControlModeString();

        statePublished = _mqttClient->publish(_statusTopic, mode.c_str());
        _publishTelemetry("modbus/healthy", _marstek->isHealthy() ? "1" : "0");

        snprintf(buf, sizeof(buf), "%.2f", t.batteryVoltage);
        _publishTelemetry("battery/voltage", buf);
        snprintf(buf, sizeof(buf), "%.2f", t.batteryCurrent);
        _publishTelemetry("battery/current", buf);
        snprintf(buf, sizeof(buf), "%ld", (long)t.batteryPower);
        _publishTelemetry("battery/power", buf);

        snprintf(buf, sizeof(buf), "%.1f", t.acVoltage);
        _publishTelemetry("ac/voltage", buf);
        snprintf(buf, sizeof(buf), "%.2f", t.acCurrent);
        _publishTelemetry("ac/current", buf);
        snprintf(buf, sizeof(buf), "%ld", (long)t.acPower);
        _publishTelemetry("ac/power", buf);
        snprintf(buf, sizeof(buf), "%.2f", t.acFrequency);
        _publishTelemetry("ac/frequency", buf);

        snprintf(buf, sizeof(buf), "%.2f", t.softwareVersion / 100.0f);
        _publishTelemetry("software_version", buf);
        snprintf(buf, sizeof(buf), "%u", t.firmwareVersion);
        _publishTelemetry("firmware_version", buf);
        _publishTelemetry("mac", t.mac);
    } else {
        statePublished = _mqttClient->publish(_statusTopic, "MISSING");
    }
    clientIdPublished = _mqttClient->publish("marstek-modbus-gateway/client_id", _clientId);
    uptimePublished = _mqttClient->publish("marstek-modbus-gateway/uptime", String(millis() / 1000).c_str());

    bool success = false;
    if (statePublished && clientIdPublished && uptimePublished) {
        Serial.println("Published MQTT message(s)");
        _lastPublish = millis();
        success = true;
    } else {
        Serial.println("Failed to publish MQTT message(s)");
        success = false;
    }

    return success;
}

bool MQTTManager::isConnected() {
    return _initialized && _mqttClient && _mqttClient->connected();
}

void MQTTManager::setClient(NetworkClient* client) {
    _ethClient = client;
}

void MQTTManager::setMarstekController(Marstek* marstek) {
    _marstek = marstek;
    Serial.println("[MQTT] Marstek controller reference set");
}

void MQTTManager::setAutoPublish(bool enabled) {
    _autoPublishEnabled = enabled;
    Serial.print("[MQTT] Automatic publishing ");
    Serial.println(enabled ? "enabled" : "disabled");
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

void MQTTManager::_publishTelemetry(const char* subTopic, const char* payload) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", MQTT_BASE_TOPIC, subTopic);
    _mqttClient->publish(topic, payload);
}

void MQTTManager::_publishTelemetry(const char* subTopic, const String& payload) {
    _publishTelemetry(subTopic, payload.c_str());
}

void MQTTManager::_onMessageReceived(char* topic, byte* payload, unsigned int length) {
    // Convert payload to string
    String command;
    command.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) {
        command += (char)payload[i];
    }

    Serial.print("[MQTT] Message received on topic: ");
    Serial.println(topic);
    Serial.print("[MQTT] Payload: ");
    Serial.println(command);

    // Handle command if it's on the command topic
    if (strcmp(topic, _commandTopic) == 0) {
        _handleCommand(command);
    }
}

void MQTTManager::_messageCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->_onMessageReceived(topic, payload, length);
    }
}

bool MQTTManager::_publishTimerCallback(void* argument) {
    // Publish current battery status
    if (_marstek && isConnected()) {
        publishStatus();
    }
    return true; // Continue periodic publishing
}

bool MQTTManager::_reconnectTimerCallback(void* argument) {
    // Attempt reconnection
    if (!isConnected()) {
        connect();
    }

    // Stop timer if connected, continue if still disconnected
    return !isConnected();
}

void MQTTManager::_handleCommand(const String& command) {
    _logCommandReceived(command);

    if (!_marstek) {
        Serial.println("[ERROR] No Marstek controller available for command handling");
        return;
    }

    if (_marstek->handleCommand(command)) {
        publishStatus();
    }
}

void MQTTManager::_logConnectionStatus() {
    Serial.println("[MQTT] Connection status:");
    Serial.print("  MQTT: ");
    Serial.println(isConnected() ? "Connected" : "Disconnected");
    Serial.print("  Broker: ");
    Serial.print(_broker);
    Serial.print(":");
    Serial.println(_port);
}

void MQTTManager::_logCommandReceived(const String& command) {
    Serial.print("[MQTT] Command received: ");
    Serial.println(command);
}
