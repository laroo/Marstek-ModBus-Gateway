
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
constexpr int MODBUS_BAUD_RATE = 9600;

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

bool state = true;

auto modbus_read_request = {
  0x01, 0x03, 0x00, 0x01, 0x00, 0x02, 0x05, 0xCB
};

void loop() {
  while (Serial485.available()) {
    byte b = Serial485.read();

    Serial.printf("Incoming MAX485 data: %02X \n", b);
  }


  uint8_t result;
  uint16_t data[6];
  
  // Toggle the coil at address 0x0002 (Manual Load Control)
  // result = node.writeSingleCoil(0x0002, state);
  // state = !state;

  // // Read 16 registers starting at 0x3100)
  // result = node.readInputRegisters(0x3100, 16);
  // if (result == node.ku8MBSuccess)
  // {
  //   Serial.print("Vbatt: ");
  //   Serial.println(node.getResponseBuffer(0x04)/100.0f);
  //   Serial.print("Vload: ");
  //   Serial.println(node.getResponseBuffer(0xC0)/100.0f);
  //   Serial.print("Pload: ");
  //   Serial.println((node.getResponseBuffer(0x0D) +
  //                   node.getResponseBuffer(0x0E) << 16)/100.0f);
  // }

  preTransmission();
  for (int b : modbus_read_request) {
    Serial485.write(b);
  }
  Serial485.flush();
  postTransmission();

  delay(1000);
}

