// Wokwi Custom Chip - Marstek Venus E v2.0 Modbus RTU Slave Emulator
//
// Emulates the Modbus RTU protocol of a Marstek Venus E v2.0 battery
// system behind an RS-485 transceiver. The chip receives Modbus RTU
// requests over UART and responds with the requested register data.
//

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MODBUS_SLAVE_ID 1

// Marstek Venus E v2.0 default: 115200 baud, 8N1.
// Must stay in sync with the ESP32 firmware.
#define MODBUS_BAUD_RATE 115200

// 3.5 character time at the configured baud rate (microseconds).
// At 115200 baud this is ~0.3 ms; use a slightly larger threshold.
#define MODBUS_INTER_FRAME_US 500

typedef struct {
  uart_dev_t uart0;
  uint8_t modbusBuffer[256];
  uint16_t bufferIndex;
  uint64_t last_byte_time;
  // RS485 control registers (42000..42021)
  uint16_t control_mode;      // 42000: 0x55AA enabled, 0x55BB disabled
  uint16_t force_mode;        // 42010: 0=stop, 1=charge, 2=discharge
  uint16_t charge_power;      // 42020 W
  uint16_t discharge_power;   // 42021 W
} chip_state_t;


static void on_uart_rx_data(void *user_data, uint8_t byte);
static void on_uart_write_done(void *user_data);
static void chip_timer_callback(void *user_data);

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  const uart_config_t uart_config = {
    .tx = pin_init("RO", INPUT_PULLUP),
    .rx = pin_init("DI", INPUT),
    .baud_rate = MODBUS_BAUD_RATE,
    .rx_data = on_uart_rx_data,
    .write_done = on_uart_write_done,
    .user_data = chip,
  };
  chip->uart0 = uart_init(&uart_config);
  chip->bufferIndex = 0;
  chip->last_byte_time = 0;

  chip->control_mode = 0x55BB;
  chip->force_mode = 0;
  chip->charge_power = 0;
  chip->discharge_power = 0;

  const timer_config_t t_config = {
    .callback = chip_timer_callback,
    .user_data = chip,
  };

  timer_t timer = timer_init(&t_config);
  // Poll frequently enough to detect the end of a Modbus frame.
  timer_start(timer, 500, true);

  printf("MAX485/Marstek chip initialized at %d baud\n", MODBUS_BAUD_RATE);
}

// CRC-16 (Modbus RTU, polynomial 0x8005, init 0xFFFF)
static uint16_t modbus_crc16(const uint8_t *data, uint16_t length) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

static void send_modbus_response(uart_dev_t uart0, uint8_t slave_id,
                                  uint8_t function_code, const uint8_t *data,
                                  uint16_t data_length) {
  uint8_t response[256];
  uint16_t pos = 0;

  response[pos++] = slave_id;
  response[pos++] = function_code;
  response[pos++] = (uint8_t)data_length;
  for (uint16_t i = 0; i < data_length; i++) {
    response[pos++] = data[i];
  }

  uint16_t crc = modbus_crc16(response, pos);
  response[pos++] = crc & 0xFF;
  response[pos++] = (crc >> 8) & 0xFF;

  printf("[TX] %02X %02X (len=%u)\n", slave_id, function_code, data_length);
  uart_write(uart0, response, pos);
}

static void send_modbus_raw_response(uart_dev_t uart0, const uint8_t *data,
                                      uint16_t data_length) {
  uint8_t response[256];
  memcpy(response, data, data_length);

  uint16_t crc = modbus_crc16(data, data_length);
  response[data_length++] = crc & 0xFF;
  response[data_length++] = (crc >> 8) & 0xFF;

  printf("[TX] raw %02X %02X\n", data[0], data[1]);
  uart_write(uart0, response, data_length);
}

static void send_modbus_exception(uart_dev_t uart0, uint8_t slave_id,
                                   uint8_t function_code, uint8_t exception_code) {
  uint8_t response[5];
  response[0] = slave_id;
  response[1] = function_code | 0x80;
  response[2] = exception_code;

  uint16_t crc = modbus_crc16(response, 3);
  response[3] = crc & 0xFF;
  response[4] = (crc >> 8) & 0xFF;

  printf("[TX] Exception 0x%02X for function 0x%02X\n", exception_code, function_code);
  uart_write(uart0, response, 5);
}

static uint16_t get_ac_voltage(void) {
  return 2300; // 230.0 V, scale 0.1 V
}

static uint16_t get_ac_frequency(void) {
  return 5000; // 50.00 Hz, scale 0.01 Hz
}

static uint16_t get_ac_current_raw(const chip_state_t *chip) {
  uint16_t active_power = 0;
  if (chip->force_mode == 1) active_power = chip->charge_power;
  else if (chip->force_mode == 2) active_power = chip->discharge_power;
  if (active_power == 0) return 0;
  // I = P / V, scale 0.01 A
  return (uint16_t)((active_power / 230.0) * 100.0 + 0.5);
}

static int32_t get_ac_power(const chip_state_t *chip) {
  if (chip->force_mode == 1) return -(int32_t)chip->charge_power; // charging draws from grid
  if (chip->force_mode == 2) return (int32_t)chip->discharge_power; // discharging feeds grid
  return 0;
}

static void handle_read_holding_registers(uart_dev_t uart0, chip_state_t *chip,
                                           uint8_t slave_id, uint8_t function_code,
                                           uint16_t address, uint16_t quantity) {
  uint8_t response_data[256];
  uint16_t response_length = 0;

  switch (address) {
    // Device name (31000 / 0x7918), 20 bytes / 10 registers
    case 0x7918: {
      if (quantity != 10) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03); // Illegal data value
        return;
      }
      response_length = 20;
      memcpy(response_data, "BI_2.5_2.5", 10);
      memset(response_data + 10, 0, 10);
      break;
    }

    // Battery voltage (average) (32100 / 0x7D64), u16, 0.01 V
    case 0x7D64: {
      if (quantity == 1) {
        response_length = 2;
        response_data[0] = 0x14; // 5120 -> 51.20 V
        response_data[1] = 0x00;
      } else if (quantity == 4) {
        // Convenience: voltage + current + power in one read (32100..32103)
        response_length = 8;
        response_data[0] = 0x14; response_data[1] = 0x00; // 32100 voltage
        response_data[2] = 0x05; response_data[3] = 0xDE; // 32101 current 1502
        response_data[4] = 0x00; response_data[5] = 0x00; // 32102 power high
        response_data[6] = 0x09; response_data[7] = 0xC4; // 32103 power low 2500
      } else {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      break;
    }

    // Battery current (average) (32101 / 0x7D65), s16, 0.01 A
    case 0x7D65: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 2;
      response_data[0] = 0x05; // 1502 -> 15.02 A
      response_data[1] = 0xDE;
      break;
    }

    // Battery power (32102 / 0x7D66), s32, 1 W (spans 2 registers)
    case 0x7D66: {
      if (quantity != 2) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 4;
      response_data[0] = 0x00; response_data[1] = 0x00; // high word
      response_data[2] = 0x09; response_data[3] = 0xC4; // low word 2500 W
      break;
    }

    // Software version (30400 / 0x76C0), u16, 0.01 scale
    case 0x76C0: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 2;
      response_data[0] = 0x00; // 0x0067 -> 1.03
      response_data[1] = 0x67;
      break;
    }

    // Firmware version (30401 / 0x76C1), u16
    case 0x76C1: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 2;
      response_data[0] = 0x00; // 0x0099 -> 153
      response_data[1] = 0x99;
      break;
    }

    // Device MAC address (30402 / 0x76C2), char[12] / 6 registers
    case 0x76C2: {
      if (quantity != 6) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 12;
      memcpy(response_data, "A1B2C3D4E5F6", 12);
      break;
    }

    // RS485 control mode (42000 / 0xA410)
    case 0xA410: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 2;
      response_data[0] = chip->control_mode >> 8;
      response_data[1] = chip->control_mode & 0xFF;
      break;
    }

    // Forcible charge/discharge (42010 / 0xA41A)
    case 0xA41A: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 2;
      response_data[0] = chip->force_mode >> 8;
      response_data[1] = chip->force_mode & 0xFF;
      break;
    }

    // Forcible charge power (42020 / 0xA424)
    case 0xA424: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 2;
      response_data[0] = chip->charge_power >> 8;
      response_data[1] = chip->charge_power & 0xFF;
      break;
    }

    // Forcible discharge power (42021 / 0xA425)
    case 0xA425: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      response_length = 2;
      response_data[0] = chip->discharge_power >> 8;
      response_data[1] = chip->discharge_power & 0xFF;
      break;
    }

    // AC voltage (32200 / 0x7DC8), u16, 0.1 V
    case 0x7DC8: {
      if (quantity == 1) {
        uint16_t voltage = get_ac_voltage();
        response_length = 2;
        response_data[0] = voltage >> 8;
        response_data[1] = voltage & 0xFF;
      } else if (quantity == 5) {
        // Convenience: 32200..32204 (voltage, current, power, ?, frequency)
        uint16_t voltage = get_ac_voltage();
        uint16_t current = get_ac_current_raw(chip);
        int32_t power = get_ac_power(chip);
        uint16_t frequency = get_ac_frequency();
        response_length = 10;
        response_data[0] = voltage >> 8;       response_data[1] = voltage & 0xFF;        // 32200
        response_data[2] = current >> 8;       response_data[3] = current & 0xFF;        // 32201
        response_data[4] = (power >> 24) & 0xFF; response_data[5] = (power >> 16) & 0xFF; // 32202
        response_data[6] = (power >> 8) & 0xFF;  response_data[7] = power & 0xFF;           // 32203
        response_data[8] = frequency >> 8;     response_data[9] = frequency & 0xFF;       // 32204
      } else {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      break;
    }

    // AC current (32201 / 0x7DC9), u16, 0.01 A
    case 0x7DC9: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      uint16_t current = get_ac_current_raw(chip);
      response_length = 2;
      response_data[0] = current >> 8;
      response_data[1] = current & 0xFF;
      break;
    }

    // AC power (32202 / 0x7DCA), s32, W (spans 2 registers)
    case 0x7DCA: {
      if (quantity != 2) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      int32_t power = get_ac_power(chip);
      response_length = 4;
      response_data[0] = (power >> 24) & 0xFF;
      response_data[1] = (power >> 16) & 0xFF;
      response_data[2] = (power >> 8) & 0xFF;
      response_data[3] = power & 0xFF;
      break;
    }

    // AC frequency (32204 / 0x7DCC), u16, 0.01 Hz
    case 0x7DCC: {
      if (quantity != 1) {
        send_modbus_exception(uart0, slave_id, function_code, 0x03);
        return;
      }
      uint16_t frequency = get_ac_frequency();
      response_length = 2;
      response_data[0] = frequency >> 8;
      response_data[1] = frequency & 0xFF;
      break;
    }

    default: {
      send_modbus_exception(uart0, slave_id, function_code, 0x02); // Illegal data address
      return;
    }
  }

  send_modbus_response(uart0, slave_id, function_code, response_data, response_length);
}

static void handle_write_single_register(uart_dev_t uart0, chip_state_t *chip,
                                          uint8_t slave_id, uint8_t function_code,
                                          uint8_t *buffer, uint16_t length) {
  if (length != 8) {
    send_modbus_exception(uart0, slave_id, function_code, 0x03);
    return;
  }

  uint16_t address = ((uint16_t)buffer[2] << 8) | buffer[3];
  uint16_t value = ((uint16_t)buffer[4] << 8) | buffer[5];

  switch (address) {
    case 0xA410: chip->control_mode = value; break;
    case 0xA41A: chip->force_mode = value; break;
    case 0xA424: chip->charge_power = value; break;
    case 0xA425: chip->discharge_power = value; break;
    default: {
      send_modbus_exception(uart0, slave_id, function_code, 0x02);
      return;
    }
  }

  printf("[REG] Wrote 0x%04X to 0x%04X (control=0x%04X mode=%u charge=%u discharge=%u)\n",
         value, address, chip->control_mode, chip->force_mode,
         chip->charge_power, chip->discharge_power);

  send_modbus_raw_response(uart0, buffer, 6);
}

static void process_modbus_frame(chip_state_t *chip, uint8_t *buffer, uint16_t length) {
  if (length < 8) {
    printf("Frame too short (%hu bytes)\n", length);
    return;
  }

  uint8_t slave_id = buffer[0];
  uint8_t function_code = buffer[1];
  uint16_t address = ((uint16_t)buffer[2] << 8) | buffer[3];
  uint16_t quantity = ((uint16_t)buffer[4] << 8) | buffer[5];

  // Verify CRC over the request (all bytes except the trailing CRC)
  uint16_t received_crc = ((uint16_t)buffer[length - 1] << 8) | buffer[length - 2];
  uint16_t calculated_crc = modbus_crc16(buffer, length - 2);
  if (received_crc != calculated_crc) {
    printf("CRC mismatch: received 0x%04X, calculated 0x%04X\n", received_crc, calculated_crc);
    return;
  }

  if (slave_id != MODBUS_SLAVE_ID) {
    printf("Ignoring request for slave %d\n", slave_id);
    return;
  }

  printf("[RX] Slave %d FC 0x%02X Addr 0x%04X Qty 0x%04X\n",
         slave_id, function_code, address, quantity);

  switch (function_code) {
    case 0x03:
      handle_read_holding_registers(chip->uart0, chip, slave_id, function_code, address, quantity);
      break;
    case 0x06:
      handle_write_single_register(chip->uart0, chip, slave_id, function_code, buffer, length);
      break;
    default:
      send_modbus_exception(chip->uart0, slave_id, function_code, 0x01); // Illegal function
      break;
  }
}


static void chip_timer_callback(void *user_data) {
  chip_state_t *chip = (chip_state_t*)user_data;
  uint64_t current_time = get_sim_nanos() / 1000; // microseconds

  if (chip->last_byte_time && (current_time - chip->last_byte_time) >= MODBUS_INTER_FRAME_US) {
    if (chip->bufferIndex > 0) {
      printf("Frame complete: %hu bytes\n", chip->bufferIndex);
      process_modbus_frame(chip, chip->modbusBuffer, chip->bufferIndex);
    }
    chip->bufferIndex = 0;
    chip->last_byte_time = 0;
  }
}

static void on_uart_rx_data(void *user_data, uint8_t byte) {
  chip_state_t *chip = (chip_state_t*)user_data;

  if (chip->bufferIndex < sizeof(chip->modbusBuffer)) {
    chip->modbusBuffer[chip->bufferIndex++] = byte;
  }

  chip->last_byte_time = get_sim_nanos() / 1000; // microseconds
}

static void on_uart_write_done(void *user_data) {
  chip_state_t *chip = (chip_state_t*)user_data;
  (void)chip;
}
