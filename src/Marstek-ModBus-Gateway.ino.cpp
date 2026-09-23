/**
 * Marstek ModBus Gateway
 *
 * ESP32-ETH01 (WT32-ETH01) bridge between a Marstek Venus-E battery
 * (RS485 / Modbus RTU) and the local network:
 *   - MQTT telemetry publishing and command input
 *   - Web dashboard with controls + ElegantOTA
 *   - LED status indicators (green = Modbus activity, red = error)
 *   - Serial diagnostics at 115200 baud
 *
 * Serial commands: charge=<watts>, discharge=<watts>, stop
 */

#include "Arduino.h"
#include <PubSubClient.h>
#include <arduino-timer.h>
#include <EthernetESP32.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <SPI.h>
#include <Network.h>

#include "config.h"
#include "marstek.h"
#include "ledmanager.h"
#include "mqttmanager.h"
#include "webmanager.h"

// WT32-ETH01 internal LAN8720 PHY pins: MDC=23, MDIO=18, power=16
EMACDriver driver(ETH_PHY_LAN8720, 23, 18, 16);

EthernetClient ethClient;
WiFiClient wifiClient;

// Pointer to the active client (Ethernet or WiFi)
NetworkClient* activeClient = nullptr;

// Connection check result (used by network event handler)
int connectionStatus = 0;

// ============================================================================
// CONFIGURATION STRUCTURE
// ============================================================================
struct Config {
  char clientId[32]; // Random client ID generated at startup

  // Timing Settings
  unsigned long telemetryInterval = 4000;  // Modbus telemetry poll interval
  unsigned long publishInterval = 10000;   // MQTT status publish interval

  // GPIO Pins (active-low LEDs, per PCB schematic)
  int redLedPin = 17;
  int greenLedPin = 5;
};

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
Config config;
Marstek *marstek = nullptr;
LEDManager *ledManager = nullptr;
MQTTManager *mqttManager = nullptr;

// Timer for main loop management
auto mainTimer = timer_create_default();

// ============================================================================
// NETWORK EVENT HANDLER
// ============================================================================
// WARNING: This function is called from a separate FreeRTOS task (thread)!
void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
  Serial.printf("[Network-event] event: %d\n", event);

  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("[ETH] Ethernet started");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("[ETH] Ethernet stopped");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("[ETH] Ethernet connected - Link UP");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[ETH] Ethernet disconnected - Link DOWN");
      connectionStatus = 0;
      activeClient = nullptr;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("[ETH] Obtained IP address: ");
      Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
      Serial.print("[ETH] Gateway: ");
      Serial.println(IPAddress(info.got_ip.ip_info.gw.addr));
      Serial.print("[ETH] Netmask: ");
      Serial.println(IPAddress(info.got_ip.ip_info.netmask.addr));
      connectionStatus = 1;
      activeClient = &ethClient;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
      Serial.println("[ETH] Ethernet IPv6 is preferred");
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WiFi] WiFi client started");
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      Serial.println("[WiFi] WiFi client stopped");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WiFi] Connected to access point");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WiFi] Disconnected from WiFi access point");
      if (connectionStatus == 2) {
        connectionStatus = 0;
        activeClient = nullptr;
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WiFi] Obtained IP address: ");
      Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
      connectionStatus = 2;
      activeClient = &wifiClient;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      Serial.println("[WiFi] Lost IP address");
      if (connectionStatus == 2) {
        connectionStatus = 0;
        activeClient = nullptr;
      }
      break;
    default:
      break;
  }
}

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
int checkConnection();
bool checkConnectionCallback(void *);
bool reportConnectionStatusCallback(void *);
NetworkClient* getActiveClient();

// ============================================================================
// SETUP FUNCTION
// ============================================================================
void setup() {
  // Initialize serial communication at 115200 baud
  Serial.begin(115200);
  delay(100); // Allow serial to initialize

  Serial.println("[INIT] Marstek ModBus Gateway starting...");
  Serial.print("[INIT] Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  // Generate random client ID for MQTT
  randomSeed(analogRead(0));
  sprintf(config.clientId, "marstek_gw_%06X", random(0xFFFFFF));
  Serial.print("[INIT] MQTT Client ID: ");
  Serial.println(config.clientId);

  // Initialize LED Manager
  ledManager = new LEDManager(config.redLedPin, config.greenLedPin);
  if (ledManager) {
    ledManager->initialize();
    Serial.println("[INIT] LED manager initialized");
  } else {
    Serial.println("[ERROR] Failed to initialize LED manager");
  }

  // Initialize Marstek Modbus controller
  marstek = new Marstek();
  if (marstek) {
    marstek->initialize(config.telemetryInterval);
    Serial.println("[INIT] Marstek controller created and initialized");
  } else {
    Serial.println("[ERROR] Failed to create Marstek controller");
  }

  // Register network event listener
  Network.onEvent(onNetworkEvent);
  Serial.println("[INIT] Network event listener registered");

  Ethernet.init(driver);

  Serial.println("Initialize Ethernet with DHCP:");
  if (Ethernet.begin()) {
    Serial.print("  DHCP assigned IP ");
    Serial.println(Ethernet.localIP());
  } else {
    Serial.println("Failed to configure Ethernet using DHCP");
  }

  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet hardware was not found.");
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("Ethernet cable is not connected.");
  }

  // Initialize MQTT Manager
  mqttManager = new MQTTManager(MQTT_BROKER, MQTT_PORT,
                                config.clientId, MQTT_TOPIC_STATUS,
                                MQTT_TOPIC_COMMAND, MQTT_USERNAME, MQTT_PASSWORD);
  if (mqttManager) {
    mqttManager->initialize(activeClient);

    // Set Marstek controller reference for telemetry and command handling
    if (marstek) {
      mqttManager->setMarstekController(marstek);
    }

    Serial.println("[INIT] MQTT manager initialized");
  } else {
    Serial.println("[ERROR] Failed to initialize MQTT manager");
  }

  Serial.println("[INIT] Setting up web server...");
  setupWebServer(config.clientId);

  // Schedule connection check every 5 seconds
  mainTimer.every(5000, checkConnectionCallback);
  Serial.println("[INIT] Connection check scheduled every 5 seconds");

  // Schedule connection status reporting every 5 seconds
  mainTimer.every(5000, reportConnectionStatusCallback);
  Serial.println("[INIT] Connection status reporting scheduled every 5 seconds");

  Serial.println("[INIT] System initialization complete");
  Serial.println("[INIT] Commands: charge=<watts>, discharge=<watts>, stop");
  Serial.println("======================================");
}

// Timer callback for connection checking
bool checkConnectionCallback(void *) {
  connectionStatus = checkConnection();
  return true; // Repeat the timer
}

// Timer callback for reporting connection status
bool reportConnectionStatusCallback(void *) {
  if (connectionStatus == 1) {
    Serial.println("Connected to Ethernet");
  }
  else if (connectionStatus == 2) {
    Serial.println("Connected to Wi-Fi");
  }
  else {
    Serial.println("Not Connected");
  }
  return true; // Repeat the timer
}

int checkConnection() {
  // Check if Ethernet is available
  if (ethClient.connected() && (Ethernet.linkStatus() == LinkON)) {
    activeClient = &ethClient;
    return 1;
  }
  else if (Ethernet.linkStatus() == LinkON) {
    // Use Ethernet connection
    if (!ethClient.connected()) {
      Serial.println("Connecting via Ethernet...");
      WiFi.disconnect();
      activeClient = &ethClient;
      return 1;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    activeClient = &wifiClient;
    return 2;
  }

  // Use Wi-Fi connection (Wokwi simulation has no Ethernet)
  WiFi.begin("Wokwi-GUEST", "", 6);
  Serial.println("Connecting via Wi-Fi...");
  for (int i = 0; i < 50; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(" CONNECTED");
      break;
    }
    delay(100);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to Wi-Fi");
    activeClient = &wifiClient;
    return 2;
  }

  Serial.println("Wi-Fi not Connected");
  activeClient = nullptr;
  return 0;
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {

  // Handle serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0 && marstek) {
      if (marstek->handleCommand(cmd) && ledManager) {
        ledManager->flashGreen();
      }
    }
  }

  // Update Marstek controller (polls telemetry on interval)
  if (marstek) {
    if (marstek->update() && ledManager) {
      ledManager->flashGreen();
    }
  }

  // Network-dependent services
  if (activeClient && connectionStatus > 0) {
    loopWebServer();

    // Update MQTT manager
    if (mqttManager) {
      mqttManager->setClient(activeClient);
      mqttManager->update();
    }
  }

  // Error indicator: red while Modbus unhealthy or no network
  if (ledManager) {
    bool modbusError = marstek && !marstek->isHealthy();
    bool networkError = connectionStatus == 0;
    ledManager->setError(modbusError || networkError);
    ledManager->update();
  }

  // Tick main timer for any scheduled tasks
  mainTimer.tick();

  // Small delay to prevent excessive CPU usage
  delay(10);
}

// ============================================================================
// GET ACTIVE CLIENT
// ============================================================================
/**
 * Returns a pointer to the active network client (Ethernet or WiFi)
 * based on the current connection status.
 *
 * @return NetworkClient* Pointer to active client, or nullptr if no connection
 */
NetworkClient* getActiveClient() {
  return activeClient;
}
