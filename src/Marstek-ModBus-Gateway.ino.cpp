
/*
 * RS485 Modbus for Marstek Venus-E v2.0
 *
 *
 * Wiring:
 *   A ---> RS485 A (RS485 line)
 *   B ---> RS485 B (RS485 line)
 *   DI --> MAX485_TX
 *   DE --> MAX485_DE
 *   RE --> MAX485_RE
 *   RO --> MAX485_RX
 *
 * Reference Example and Source:
 *   https://github.com/openopen114/Arduino_Modbus_viaRS485/blob/master/ArduinoCode/ModbusMaster_viaRS485/ModbusMaster_viaRS485.ino
 */


#include <ModbusMaster.h>
#include <HardwareSerial.h>

constexpr int BOARD_485_RX = 32;  // yellow wire to RO (Receiver Output)
constexpr int BOARD_485_TX = 33;  // brown wire to DI (Driver Input)
constexpr int MAX485_DE = 23;  // blue wire to DE (Driver Enable)
constexpr int MAX485_RE = 22;  // pink wire to RE (Receiver Enable)
constexpr int MODBUS_BAUD_RATE = 115200;

// Define the UART2 default pins
#define RX2_PIN 16
#define TX2_PIN 17

// connection pins
#define MAX485_RX 32
#define MAX485_TX 33
// #define MAX485_DE 23
// #define MAX485_RE 22

// led to indicate the action
#define LED_RED 5
#define LED_GREEN 17

// number of retry
#define RETRY 20

const String firmware_version = "v0.0.1";
// #define Serial485 Serial2
HardwareSerial& Serial485 = Serial2;

ModbusMaster node;

unsigned long lastTelemetryMs = 0;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 4000;

constexpr unsigned long LED_BLINK_MS = 150;
unsigned long redLedOffTime = 0;
unsigned long greenLedOffTime = 0;

// Callback functions to toggle RS485 transmit/receive direction
void preTransmission() {
  // Set to Transmit mode
  // Serial.println(F("[RS485] preTransmission"));
  digitalWrite(MAX485_DE, HIGH);
  digitalWrite(MAX485_RE, HIGH);
}

void postTransmission() {
  // Set to Receive mode
  // Serial.println(F("[RS485] postTransmission"));
  digitalWrite(MAX485_DE, LOW);
  digitalWrite(MAX485_RE, LOW);
}

void triggerRedLed() {
  digitalWrite(LED_RED, LOW); // active low
  redLedOffTime = millis() + LED_BLINK_MS;
}

void triggerGreenLed() {
  digitalWrite(LED_GREEN, LOW); // active low
  greenLedOffTime = millis() + LED_BLINK_MS;
}

void updateLeds() {
  unsigned long now = millis();
  if (redLedOffTime && now >= redLedOffTime) {
    digitalWrite(LED_RED, HIGH);
    redLedOffTime = 0;
  }
  if (greenLedOffTime && now >= greenLedOffTime) {
    digitalWrite(LED_GREEN, HIGH);
    greenLedOffTime = 0;
  }
}

bool writeModbusRegister(uint16_t address, uint16_t value) {
  uint8_t result = node.writeSingleRegister(address, value);
  if (result != node.ku8MBSuccess) {
    Serial.printf("Modbus write 0x%04X = 0x%04X failed: 0x%02X\n", address, value, result);
    return false;
  }
  return true;
}

void setChargePower(uint16_t watts) {
  if (watts > 2500) {
    Serial.println("Charge power must be 0-2500 W");
    return;
  }

  Serial.printf("Starting charge at %u W\n", watts);
  if (!writeModbusRegister(0xA410, 0x55AA)) return; // enable RS485 control mode
  if (!writeModbusRegister(0xA424, watts)) return;    // charge power
  if (!writeModbusRegister(0xA41A, 0x0001)) return;   // charge mode
  Serial.println("Charge active");
}

void setDischargePower(uint16_t watts) {
  if (watts > 2500) {
    Serial.println("Discharge power must be 0-2500 W");
    return;
  }

  Serial.printf("Starting discharge at %u W\n", watts);
  if (!writeModbusRegister(0xA410, 0x55AA)) return; // enable RS485 control mode
  if (!writeModbusRegister(0xA425, watts)) return;  // discharge power
  if (!writeModbusRegister(0xA41A, 0x0002)) return;   // discharge mode
  Serial.println("Discharge active");
}

void stopControl() {
  Serial.println("Stopping control");
  writeModbusRegister(0xA41A, 0x0000); // stop charge/discharge
  writeModbusRegister(0xA410, 0x55BB); // disable RS485 control mode
  Serial.println("Control stopped");
}

void handleSerialCommand(const String& cmd) {
  triggerRedLed();

  if (cmd.startsWith("charge=")) {
    int watts = cmd.substring(7).toInt();
    if (watts < 0 || watts > 2500) {
      Serial.println("Charge power must be 0-2500 W");
      return;
    }
    if (watts == 0) {
      stopControl();
    } else {
      setChargePower((uint16_t)watts);
    }
  } else if (cmd.startsWith("discharge=")) {
    int watts = cmd.substring(10).toInt();
    if (watts < 0 || watts > 2500) {
      Serial.println("Discharge power must be 0-2500 W");
      return;
    }
    if (watts == 0) {
      stopControl();
    } else {
      setDischargePower((uint16_t)watts);
    }
  } else if (cmd == "stop") {
    stopControl();
  } else {
    Serial.println("Unknown command. Use: charge=<watts>, discharge=<watts>, stop");
  }
}

void readTelemetry() {
  triggerGreenLed();

  uint8_t result;

  // Read battery voltage, current and power (registers 32100..32103)
  result = node.readHoldingRegisters(0x7D64, 4);
  if (result == node.ku8MBSuccess) {
    uint16_t voltage_raw = node.getResponseBuffer(0);  // 32100, 0.01 V
    uint16_t current_raw = node.getResponseBuffer(1);  // 32101, 0.01 A (s16)
    uint16_t power_high  = node.getResponseBuffer(2);  // 32102, s32 high word
    uint16_t power_low   = node.getResponseBuffer(3);  // 32103, s32 low word

    float voltage = voltage_raw / 100.0f;
    float current = (int16_t)current_raw / 100.0f;
    int32_t power = ((int32_t)(int16_t)power_high << 16) | power_low;

    Serial.printf("Vbatt: %.2f V\n", voltage);
    Serial.printf("Ibatt: %.2f A\n", current);
    Serial.printf("Pbatt: %ld W\n", power);
  } else {
    Serial.printf("Battery read error: 0x%02X\n", result);
  }

  // Read software version (30400 / 0x76C0), u16, 0.01 scale
  result = node.readHoldingRegisters(0x76C0, 1);
  if (result == node.ku8MBSuccess) {
    uint16_t sw_version = node.getResponseBuffer(0);
    Serial.printf("Software version: 0x%04X (%.2f)\n", sw_version, sw_version / 100.0f);
  } else {
    Serial.printf("Software version read error: 0x%02X\n", result);
  }

  // Read firmware version (30401 / 0x76C1), u16
  result = node.readHoldingRegisters(0x76C1, 1);
  if (result == node.ku8MBSuccess) {
    uint16_t fw_version = node.getResponseBuffer(0);
    Serial.printf("Firmware version: 0x%04X (%u)\n", fw_version, fw_version);
  } else {
    Serial.printf("Firmware version read error: 0x%02X\n", result);
  }

  // Read device MAC address (30402 / 0x76C2), char[12] / 6 registers
  result = node.readHoldingRegisters(0x76C2, 6);
  if (result == node.ku8MBSuccess) {
    char mac[13];
    for (uint8_t i = 0; i < 6; i++) {
      uint16_t reg = node.getResponseBuffer(i);
      mac[i * 2] = reg >> 8;
      mac[i * 2 + 1] = reg & 0xFF;
    }
    mac[12] = '\0';
    Serial.printf("MAC: %s\n", mac);
  } else {
    Serial.printf("MAC read error: 0x%02X\n", result);
  }

  // Read AC measurements (32200..32204): voltage, current, power, frequency
  result = node.readHoldingRegisters(0x7DC8, 5);
  if (result == node.ku8MBSuccess) {
    uint16_t ac_voltage_raw = node.getResponseBuffer(0);  // 32200, 0.1 V
    uint16_t ac_current_raw = node.getResponseBuffer(1);  // 32201, 0.01 A
    uint16_t ac_power_high  = node.getResponseBuffer(2);  // 32202, s32 high word
    uint16_t ac_power_low   = node.getResponseBuffer(3);  // 32203, s32 low word
    uint16_t ac_freq_raw    = node.getResponseBuffer(4);  // 32204, 0.01 Hz

    float ac_voltage = ac_voltage_raw / 10.0f;
    float ac_current = ac_current_raw / 100.0f;
    int32_t ac_power = ((int32_t)(int16_t)ac_power_high << 16) | ac_power_low;
    float ac_frequency = ac_freq_raw / 100.0f;

    Serial.printf("Vac: %.1f V\n", ac_voltage);
    Serial.printf("Iac: %.2f A\n", ac_current);
    Serial.printf("Pac: %ld W\n", ac_power);
    Serial.printf("Fac: %.2f Hz\n", ac_frequency);
  } else {
    Serial.printf("AC read error: 0x%02X\n", result);
  }
}

void setup(void) {
  pinMode(MAX485_DE, OUTPUT);
  pinMode(MAX485_RE, OUTPUT);

  // Init in receive mode
  digitalWrite(MAX485_DE, LOW);
  digitalWrite(MAX485_RE, LOW);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, HIGH);  // Active low
  digitalWrite(LED_GREEN, HIGH); // Active low

  Serial.begin(115200);
  Serial.println(F("[RS485] Init serial..."));

  Serial485.begin(MODBUS_BAUD_RATE, SERIAL_8N1, BOARD_485_RX, BOARD_485_TX);

  // Modbus slave ID 1
  node.begin(1, Serial485);

  // Callbacks allow us to configure the RS485 transceiver correctly
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println(F("Ready. Commands: charge=<watts>, discharge=<watts>, stop"));
}

void loop() {
  updateLeds();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      handleSerialCommand(cmd);
    }
  }

  unsigned long now = millis();
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    readTelemetry();
  }

  delay(100);
}
