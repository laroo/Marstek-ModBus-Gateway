#pragma once

// Stringify helper macros
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// OTA credentials (passed as bare identifiers via -D flags, stringified here)
#ifndef OTA_USERNAME
#error Need to define OTA_USERNAME
#endif
#ifndef OTA_PASSWORD
#error Need to define OTA_PASSWORD
#endif
#define OTA_USERNAME_STR TOSTRING(OTA_USERNAME)
#define OTA_PASSWORD_STR TOSTRING(OTA_PASSWORD)

// MQTT broker
#ifndef MQTT_BROKER
#define MQTT_BROKER "test.mosquitto.org"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif


#ifndef MQTT_USERNAME
#define MQTT_USERNAME nullptr
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD nullptr
#endif

// MQTT root topic prefix; per-device subtopics are derived from it:
//   <root>/gateway/<esp-mac>/...  gateway heartbeat (uptime, availability, modbus)
//   <root>/<battery-mac>/...      battery telemetry + command subscription
#ifndef MQTT_ROOT_TOPIC
#define MQTT_ROOT_TOPIC "marstek"
#endif
