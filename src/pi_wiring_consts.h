#ifndef PI_WIRING_CONSTS_H
#define PI_WIRING_CONSTS_H

// whether use spi0 or spi1
const int SPI_CHANNEL = 0;
// which spi chip enable pin is used
const int SPI_CHIP_ENABLE = 0;
// the size of the pi's spi buffer (default is 4096, max is 65536)
// can be modified in pi boot settings
#define SPI_BUFFER_SIZE 65536

// GPIO pins used for the other display inputs
const int BACKLIGHT_PIN = 22;
const int RESET_PIN = 24;
const int DATA_COMMAND_PIN = 25;

#endif
