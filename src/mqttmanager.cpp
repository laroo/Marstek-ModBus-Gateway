/**
 * MQTTManager.cpp - Marstek Modbus Gateway MQTT Manager Implementation
 *
 * Implementation of MQTT communication over Ethernet for remote battery
 * monitoring and control using PubSubClient.
 */

#include "Arduino.h"
#include "mqttmanager.h"
#include <ETH.h>

// Static instance pointer for callback handling
MQTTManager* MQTTManager::_instance = nullptr;

// ============================================================================
// MQTT MANAGER CLASS IMPLEMENTATION
// ============================================================================

MQTTManager::MQTTManager(const char* broker, int port, const char* clientId,
                         const char* gatewayId, const char* rootTopic,
                         const char* username, const char* password)
    : _port(port), _batteryCmdSubscribed(false), _ethClient(nullptr),
      _mqttClient(nullptr), _initialized(false), _autoPublishEnabled(true),
      _lastPublish(0), _lastConnectionAttempt(0), _reconnectAttempts(0),
      _marstek(nullptr), _activityCallback(nullptr), _useAuth(false) {

    // Copy configuration strings
    strncpy(_broker, broker, sizeof(_broker) - 1);
    _broker[sizeof(_broker) - 1] = '\0';

    strncpy(_clientId, clientId, sizeof(_clientId) - 1);
    _clientId[sizeof(_clientId) - 1] = '\0';

    strncpy(_gatewayId, gatewayId, sizeof(_gatewayId) - 1);
    _gatewayId[sizeof(_gatewayId) - 1] = '\0';

    strncpy(_rootTopic, rootTopic, sizeof(_rootTopic) - 1);
    _rootTopic[sizeof(_rootTopic) - 1] = '\0';

    _batteryMac[0] = '\0';

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
    Serial.print("[MQTT] Gateway topic: ");
    Serial.print(_rootTopic);
    Serial.print("/gateway/");
    Serial.println(_gatewayId);

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

    // LWT: broker publishes "offline" (retained) if we drop ungracefully
    char willTopic[96];
    snprintf(willTopic, sizeof(willTopic), "%s/gateway/%s/availability",
             _rootTopic, _gatewayId);

    // Attempt MQTT connection with or without authentication
    bool connected;
    if (_useAuth) {
        Serial.println("[MQTT] Connecting with authentication...");
        connected = _mqttClient->connect(_clientId, _username, _password,
                                         willTopic, 0, true, "offline");
    } else {
        Serial.println("[MQTT] Connecting without authentication...");
        connected = _mqttClient->connect(_clientId, willTopic, 0, true, "offline");
    }

    if (connected) {
        Serial.println("[MQTT] Connected to broker successfully");
        _reconnectAttempts = 0;
        _batteryCmdSubscribed = false;

        _mqttClient->publish(willTopic, "online", true);

        // Retained gateway identity so consumers can map gateway <-> battery
        char info[160];
        snprintf(info, sizeof(info),
                 "{\"client_id\":\"%s\",\"gateway_id\":\"%s\",\"battery_mac\":\"%s\"}",
                 _clientId, _gatewayId, _batteryMac);
        _publishGateway("info", info, true);

        // Re-subscribe to the battery command topic if already discovered
        if (_batteryMac[0] != '\0') {
            _batteryCmdSubscribed = false;
            _ensureBatterySubscription(_batteryMac);
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

    char buf[24];

    // Gateway heartbeat: always published, even without a battery
    snprintf(buf, sizeof(buf), "%lu", millis() / 1000);
    _publishGateway("uptime", buf);
    _publishGateway("modbus/healthy",
                    (_marstek && _marstek->isHealthy()) ? "1" : "0");

    bool statePublished = false;
    const bool batteryKnown =
        _marstek && _marstek->getTelemetry().valid &&
        _marstek->getTelemetry().mac[0] != '\0';

    if (batteryKnown) {
        const MarstekTelemetry& t = _marstek->getTelemetry();

        _ensureBatterySubscription(t.mac);
        _publishGateway("battery_mac", t.mac, true);

        String mode = _marstek->getControlModeString();
        statePublished = _publishBattery("status", mode.c_str());
        _publishBattery("modbus/healthy", _marstek->isHealthy() ? "1" : "0");

        snprintf(buf, sizeof(buf), "%.2f", t.batteryVoltage);
        _publishBattery("battery/voltage", buf);
        snprintf(buf, sizeof(buf), "%.2f", t.batteryCurrent);
        _publishBattery("battery/current", buf);
        snprintf(buf, sizeof(buf), "%ld", (long)t.batteryPower);
        _publishBattery("battery/power", buf);
        snprintf(buf, sizeof(buf), "%u", t.batterySoc);
        _publishBattery("battery/soc", buf);
        snprintf(buf, sizeof(buf), "%.3f", t.batteryEnergy);
        _publishBattery("battery/energy", buf);

        snprintf(buf, sizeof(buf), "%.1f", t.acVoltage);
        _publishBattery("ac/voltage", buf);
        snprintf(buf, sizeof(buf), "%.2f", t.acCurrent);
        _publishBattery("ac/current", buf);
        snprintf(buf, sizeof(buf), "%ld", (long)t.acPower);
        _publishBattery("ac/power", buf);
        snprintf(buf, sizeof(buf), "%.2f", t.acFrequency);
        _publishBattery("ac/frequency", buf);

        snprintf(buf, sizeof(buf), "%.2f", t.softwareVersion / 100.0f);
        _publishBattery("software_version", buf);
        snprintf(buf, sizeof(buf), "%u", t.firmwareVersion);
        _publishBattery("firmware_version", buf);
    } else {
        // No battery identified yet; gateway heartbeat is enough
        statePublished = true;
    }

    bool success = false;
    if (statePublished) {
        Serial.println("Published MQTT message(s)");
        _lastPublish = millis();
        success = true;
        if (_activityCallback) {
            _activityCallback();
        }
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

void MQTTManager::setActivityCallback(void (*callback)()) {
    _activityCallback = callback;
}

// ============================================================================
// PRIVATE METHODS
// ============================================================================

bool MQTTManager::_publishGateway(const char* subTopic, const char* payload, bool retained) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/gateway/%s/%s", _rootTopic, _gatewayId, subTopic);
    return _mqttClient->publish(topic, payload, retained);
}

bool MQTTManager::_publishBattery(const char* subTopic, const char* payload, bool retained) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s/%s", _rootTopic, _batteryMac, subTopic);
    return _mqttClient->publish(topic, payload, retained);
}

void MQTTManager::_ensureBatterySubscription(const char* batteryMac) {
    if (strcmp(_batteryMac, batteryMac) != 0) {
        strncpy(_batteryMac, batteryMac, sizeof(_batteryMac) - 1);
        _batteryMac[sizeof(_batteryMac) - 1] = '\0';
        _batteryCmdSubscribed = false;

        // Refresh the retained identity record with the learned battery MAC
        char info[160];
        snprintf(info, sizeof(info),
                 "{\"client_id\":\"%s\",\"gateway_id\":\"%s\",\"battery_mac\":\"%s\"}",
                 _clientId, _gatewayId, _batteryMac);
        _publishGateway("info", info, true);
    }

    if (_batteryCmdSubscribed || !_mqttClient->connected()) {
        return;
    }

    char commandTopic[96];
    snprintf(commandTopic, sizeof(commandTopic), "%s/%s/command", _rootTopic, _batteryMac);
    _batteryCmdSubscribed = _mqttClient->subscribe(commandTopic);
    if (_batteryCmdSubscribed) {
        Serial.print("[MQTT] Subscribed to command topic: ");
        Serial.println(commandTopic);
    } else {
        Serial.print("[ERROR] Failed to subscribe to command topic: ");
        Serial.println(commandTopic);
    }
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

    // Handle command if it's on the battery command topic
    char commandTopic[96];
    snprintf(commandTopic, sizeof(commandTopic), "%s/%s/command", _rootTopic, _batteryMac);
    if (strcmp(topic, commandTopic) == 0) {
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
