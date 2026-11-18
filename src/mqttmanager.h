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
     *
     * Topics are derived from rootTopic:
     *   <root>/gateway/<gatewayId>/...  gateway heartbeat, always published
     *   <root>/<battery-mac>/...        battery telemetry + command subscription,
     *                                   appears once Modbus returns the MAC
     * @param broker MQTT broker hostname
     * @param port MQTT broker port
     * @param clientId Unique client identifier
     * @param gatewayId Stable gateway identifier (ESP32 MAC), used in topics
     * @param rootTopic Root topic prefix, e.g. "marstek"
     * @param username MQTT username (optional, pass nullptr if not needed)
     * @param password MQTT password (optional, pass nullptr if not needed)
     */
    MQTTManager(const char* broker, int port, const char* clientId,
                const char* gatewayId, const char* rootTopic,
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

    /**
     * Register a callback invoked after each successful publishStatus batch
     * @param callback Function called on publish activity
     */
    void setActivityCallback(void (*callback)());

private:
    // MQTT configuration
    char _broker[64];           // MQTT broker hostname
    int _port;                  // MQTT broker port
    char _clientId[40];         // Unique client ID
    char _gatewayId[16];        // Gateway identifier (ESP32 MAC hex)
    char _rootTopic[32];        // Root topic prefix (e.g. "marstek")
    char _batteryMac[13];       // Last seen battery MAC (empty until polled)
    bool _batteryCmdSubscribed; // Command topic subscribed for _batteryMac
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
    void (*_activityCallback)(); // Called after a successful publish batch

    // Private methods
    void _onMessageReceived(char* topic, byte* payload, unsigned int length);
    static void _messageCallback(char* topic, byte* payload, unsigned int length);
    bool _publishTimerCallback(void* argument);
    bool _reconnectTimerCallback(void* argument);
    void _handleCommand(const String& command);
    void _logConnectionStatus();
    void _logCommandReceived(const String& command);
    bool _publishGateway(const char* subTopic, const char* payload, bool retained = false);
    bool _publishBattery(const char* subTopic, const char* payload, bool retained = false);
    void _ensureBatterySubscription(const char* batteryMac);

    // Static instance pointer for callback handling
    static MQTTManager* _instance;
};

#endif // MQTTManager_h
