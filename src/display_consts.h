#ifndef DISPLAY_CONSTS_H
#define DISPLAY_CONSTS_H

#include <stdint.h>

#define DISPLAY_SPI_FREQUENCY 32000000
#define DISPLAY_SPI_MODE 0
#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240
// the number of data bytes that can be send in one call to spi write
#define DISPLAY_TRANSFER_BUFFER_SIZE 4096

/// --- Display Commands ---

const uint8_t NO_OPERATION = 0x00;
const uint8_t SOFTWARE_RESET = 0x01;

const uint8_t SLEEP_IN_MODE = 0x10;
const uint8_t SLEEP_OUT_MODE = 0x11;

const uint8_t PARTIAL_MODE = 0x12;
const uint8_t NORMAL_MODE  = 0x13;

const uint8_t INVERT_OFF = 0x20;
const uint8_t INVERT_ON = 0x21;

const uint8_t DISPLAY_ON = 0x29;

const uint8_t COLUMN_ADDRESS_SET = 0x2a;
const uint8_t ROW_ADDRESS_SET    = 0x2b;
const uint8_t WRITE_RAM          = 0x2c;

const uint8_t PARTIAL_AREA_SET = 0x30;

const uint8_t MEMORY_ACCESS_CONTROL = 0x36;

const uint8_t IDLE_MODE_OFF = 0x38;
const uint8_t IDLE_MODE_ON = 0x39;

const uint8_t COLOUR_FORMAT_SET = 0x3a;

const uint8_t DISPLAY_BRIGHTNESS_SET = 0x51;
const uint8_t DISPLAY_BRIGHTNESS_CONTROL = 0x53;

#endif
