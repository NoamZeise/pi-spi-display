#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <wiringPi.h>
#include <wiringPiSPI.h>

#include "display_consts.h"
#include "pi_wiring_consts.h"

void msleep(unsigned int ms) { usleep(ms * 1000); }

void hardware_reset() {
    digitalWrite(RESET_PIN, LOW);
    usleep(10);
    digitalWrite(RESET_PIN, HIGH);
    msleep(10);
}

void command_mode() { digitalWrite(DATA_COMMAND_PIN, LOW); }

void data_mode() { digitalWrite(DATA_COMMAND_PIN, HIGH); }

void send_command(uint8_t cmd) {
    command_mode();
    if (wiringPiSPIDataRW(SPI_CHANNEL, &cmd, 1) == -1) {
	printf("failed to send command to spi: %s\n", strerror(errno));
    }	
    data_mode();
}

void send_byte(uint8_t b) { wiringPiSPIDataRW(SPI_CHANNEL, &b, 1); }

static uint8_t display_transfer_buffer[DISPLAY_TRANSFER_BUFFER_SIZE];

void send_buffer(uint8_t *buff, unsigned int size) {
    if (size == 0)
	return;

    int transfers = size / DISPLAY_TRANSFER_BUFFER_SIZE;
    int remainder = size - (transfers * DISPLAY_TRANSFER_BUFFER_SIZE);
    for (int i = 0; i < transfers; i++) {
	memcpy(display_transfer_buffer, &buff[i * DISPLAY_TRANSFER_BUFFER_SIZE],
	       DISPLAY_TRANSFER_BUFFER_SIZE);
	wiringPiSPIDataRW(SPI_CHANNEL, display_transfer_buffer, DISPLAY_TRANSFER_BUFFER_SIZE);
    }
    if (remainder > 0) {
	memcpy(display_transfer_buffer,
	       &buff[transfers * DISPLAY_TRANSFER_BUFFER_SIZE], remainder);
	wiringPiSPIDataRW(SPI_CHANNEL, display_transfer_buffer, remainder);
    }
}

void colour_format_set() {
    send_command(COLOUR_FORMAT_SET);
    send_byte(0x55);
    msleep(10);
}

void screen_address_set(uint8_t cmd, uint16_t start, uint16_t end) {
    send_command(cmd);
    uint8_t buff[4];
    buff[0] = start >> 8;
    buff[1] = start & 0xFF;
    buff[2] = end >> 8;
    buff[3] = end & 0xFF;
    wiringPiSPIDataRW(SPI_CHANNEL, buff, 4);
}

void partial_area_set(int start, int end) {
    send_command(PARTIAL_AREA_SET);
    uint8_t buff[4];
    buff[0] = start >> 8;
    buff[1] = start;
    buff[2] = end >> 8;
    buff[3] = end;
    wiringPiSPIDataRW(SPI_CHANNEL, buff, 4);
}

void init_wiring_pi() {
    int res = wiringPiSetupGpio();
    if (res) {
	printf("failed to init gpio! %s\n", strerror(res));
	exit(res);
    }

    if (wiringPiSPISetupMode(SPI_CHANNEL, DISPLAY_SPI_FREQUENCY, DISPLAY_SPI_MODE) < 0) {
	fprintf(stderr, "failed to open spi bus: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }

    pinMode(DATA_COMMAND_PIN, OUTPUT);
    pinMode(RESET_PIN, OUTPUT);
    pinMode(BACKLIGHT_PIN, OUTPUT);

    printf("wiring pi initialised\n");    
}

int main() {   
    init_wiring_pi();

    digitalWrite(BACKLIGHT_PIN, HIGH);

    printf("resetting display\n");
    hardware_reset();

    
    send_command(SOFTWARE_RESET);
    msleep(5);

    send_command(SLEEP_OUT_MODE);
    msleep(5);

    screen_address_set(COLUMN_ADDRESS_SET, 0, DISPLAY_HEIGHT);
    screen_address_set(ROW_ADDRESS_SET, 0, DISPLAY_WIDTH);
	
    send_command(DISPLAY_ON);

    send_command(WRITE_RAM);

    int size = DISPLAY_WIDTH * DISPLAY_HEIGHT * 3;
    uint8_t data[size];
    memset(data, 0xFF, size);
    send_buffer(data, size);         

    for (int i = 0; i <= 0xFF; i++) {
	msleep(10);
	send_command(WRITE_RAM);
	memset(data, 0xFF - i, size);
        send_buffer(data, size);
	send_command(NO_OPERATION);
    }
    send_command(NO_OPERATION);

    digitalWrite(BACKLIGHT_PIN, LOW);
    wiringPiSPIClose(SPI_CHANNEL);
    printf("program end\n");
    return 0;
}
