#ifndef PI_WIRING_CONSTS_H
#define PI_WIRING_CONSTS_H

const int SPI_CHANNEL = 0; // connected to pi spi ce0
// the size of the pi's spi buffer (default is 4096, max is 65536)
#define DISPLAY_TRANSFER_BUFFER_SIZE 65536

const int BACKLIGHT_PIN = 22;
const int RESET_PIN = 24;
const int DATA_COMMAND_PIN = 25;

#endif
