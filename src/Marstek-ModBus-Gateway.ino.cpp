
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

// Callback functions to toggle RS485 transmit/receive direction
void preTransmission() {
  // Set to Transmit mode
  Serial.println(F("[RS485] preTransmission"));
  digitalWrite(MAX485_DE, HIGH);
  digitalWrite(MAX485_RE, HIGH);
}

void postTransmission() {
  // Set to Receive mode
  Serial.println(F("[RS485] postTransmission"));
  digitalWrite(MAX485_DE, LOW);
  digitalWrite(MAX485_RE, LOW);
}


void setup(void) {
  pinMode(MAX485_DE, OUTPUT);
  pinMode(MAX485_RE, OUTPUT);

  // Init in receive mode
  digitalWrite(MAX485_DE, LOW);
  digitalWrite(MAX485_RE, LOW);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);

  Serial.begin(115200);
  Serial.println(F("[RS485] Init serial..."));

  Serial485.begin(MODBUS_BAUD_RATE, SERIAL_8N1, BOARD_485_RX, BOARD_485_TX);
  
  // Modbus slave ID 1
  node.begin(1, Serial485);
  
  // Callbacks allow us to configure the RS485 transceiver correctly
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);


  //   for (int b : modbus_read_request) {
  //     Serial485.write(b);
  //   }
  // Serial.println(F("Sent 485 data"));
}

void loop() {
  // Read battery voltage, current and power (registers 32100..32103)
  uint8_t result = node.readHoldingRegisters(0x7D64, 4);
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
    Serial.printf("Modbus error: 0x%02X\n", result);
  }

  delay(1000);
}

