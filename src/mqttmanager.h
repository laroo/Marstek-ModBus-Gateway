/**
 * MQTTManager.h - Marstek Modbus Gateway MQTT Manager Header
 *
 * Defines the MQTTManager class interface for MQTT communication
 * over Ethernet for remote battery monitoring and control.
 */

#ifndef MQTTManager_h
#define MQTTManager_h

#include "Arduino.h"
#include <Network.h>
#include <PubSubClient.h>
#include <arduino-timer.h>
#include "marstek.h"

// ============================================================================
// MQTT MANAGER CLASS DECLARATION
// ============================================================================
class MQTTManager {
public:
    /**
     * Constructor - Initialize MQTT manager
     * @param broker MQTT broker hostname
     * @param port MQTT broker port
     * @param clientId Unique client identifier
     * @param statusTopic Topic for publishing battery status
     * @param commandTopic Topic for subscribing to commands
     * @param username MQTT username (optional, pass nullptr if not needed)
     * @param password MQTT password (optional, pass nullptr if not needed)
     */
    MQTTManager(const char* broker, int port, const char* clientId,
                const char* statusTopic, const char* commandTopic,
                const char* username = nullptr, const char* password = nullptr);

    /**
     * Destructor - Clean up resources
     */
    ~MQTTManager();

    /**
     * Initialize MQTT manager
     * @param activeClient Network client to use (Ethernet or WiFi)
     */
    void initialize(NetworkClient* activeClient);

    /**
     * Update MQTT connection and handle messages
     * Should be called regularly in main loop
     */
    void update();

    /**
     * Connect to MQTT broker
     * @return true if connection successful
     */
    bool connect();

    /**
     * Publish battery status to MQTT broker
     * @return true if publish successful
     */
    bool publishStatus();

    void setClient(NetworkClient* client);

    /**
     * Check if MQTT client is connected
     * @return true if connected to broker
     */
    bool isConnected();

    /**
     * Set Marstek controller reference for telemetry and command handling
     * @param marstek Pointer to Marstek controller instance
     */
    void setMarstekController(Marstek* marstek);

    /**
     * Enable/disable automatic status publishing
     * @param enabled true to enable periodic publishing
     */
    void setAutoPublish(bool enabled);

private:
    // MQTT configuration
    char _broker[64];           // MQTT broker hostname
    int _port;                  // MQTT broker port
    char _clientId[32];         // Unique client ID
    char _statusTopic[64];      // Status publishing topic
    char _commandTopic[64];     // Command subscription topic
    char _username[64];         // MQTT username (empty if not used)
    char _password[64];         // MQTT password (empty if not used)
    bool _useAuth;              // Flag indicating if authentication is enabled

    // Network and MQTT clients
    NetworkClient* _ethClient;    // Network client for MQTT connection
    PubSubClient* _mqttClient;  // MQTT client for broker communication

    // Timer management
    Timer<> _publishTimer;      // Timer for periodic status publishing
    Timer<> _reconnectTimer;    // Timer for connection retry attempts

    // State tracking
    bool _initialized;          // Flag indicating initialization complete
    bool _autoPublishEnabled;   // Flag for automatic status publishing
    unsigned long _lastPublish; // Timestamp of last status publish
    unsigned long _lastConnectionAttempt; // Timestamp of last connection attempt
    int _reconnectAttempts;     // Number of consecutive reconnection attempts

    // Marstek controller reference
    Marstek* _marstek;          // Pointer to Marstek controller for telemetry/commands

    // Private methods
    void _onMessageReceived(char* topic, byte* payload, unsigned int length);
    static void _messageCallback(char* topic, byte* payload, unsigned int length);
    bool _publishTimerCallback(void* argument);
    bool _reconnectTimerCallback(void* argument);
    void _handleCommand(const String& command);
    void _logConnectionStatus();
    void _logCommandReceived(const String& command);
    void _publishTelemetry(const char* subTopic, const char* payload);
    void _publishTelemetry(const char* subTopic, const String& payload);

    // Static instance pointer for callback handling
    static MQTTManager* _instance;
};

#endif // MQTTManager_h
