#include "display.h"

int spi_handle = -1;

void msleep(unsigned int ms) { usleep(ms * 1000); }

void display_hardware_reset() {
  digitalWrite(RESET_PIN, LOW);
  usleep(10);
  digitalWrite(RESET_PIN, HIGH);
  msleep(10);
}

void command_mode() { digitalWrite(DATA_COMMAND_PIN, LOW); }

void data_mode() { digitalWrite(DATA_COMMAND_PIN, HIGH); }

void raw_send_buffer(uint8_t *buff, unsigned int size) {
  if (wiringPiSPIDataRW(SPI_CHANNEL, buff, size) == -1) {
    printf("failed to send data over spi: %s\n", strerror(errno));
  }	
}

void send_byte(uint8_t b) { raw_send_buffer(&b, 1); }

void send_command(uint8_t cmd) {
  command_mode();
  send_byte(cmd);
  data_mode();
}
