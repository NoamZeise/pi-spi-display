#include "display.h"

#include <stdint.h>
#include <stdio.h>  // printf
#include <string.h> // strerror
#include <unistd.h> // sleep
#include <errno.h>
#include <stdlib.h> // exit

#include <wiringPi.h>
#include <wiringPiSPI.h>

#include "display_consts.h"
#include "pi_wiring_consts.h"
#include "time.h"

void msleep(unsigned int ms) { usleep(ms * 1000); }

void raw_send_buffer(uint8_t *buff, unsigned int size);

void send_byte(uint8_t b) { raw_send_buffer(&b, 1); }

void send_2_bytes(uint16_t data);

void send_command(enum display_command_byte cmd);

void send_buffer(uint8_t *buff, unsigned int size);


typedef struct display_state_t {
  // sleep state
  enum display_option sleep_mode;
  time_point last_sleep_change;
  // is the screen being shown
  enum display_option on;
  // is the screen inverted
  enum display_option invert;
  // is partial mode enabled
  enum display_option partial_mode;
  // current draw area
  uint16_t column_start;
  uint16_t column_end;
  uint16_t row_start;
  uint16_t row_end;
} display_state_t;

static display_state_t display_state;

void reset_display_state();


/// ---- Api Implementation ----


int display_init() {
  int result = wiringPiSetupGpio();
  if (result) {
    fprintf(stderr, "Failed to init pi gpio pins: %s\n", strerror(errno));
    return -1;
  }

  pinMode(DATA_COMMAND_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(BACKLIGHT_PIN, OUTPUT);

  int spi_handle = wiringPiSPIxSetupMode(
      SPI_CHIP_ENABLE, SPI_CHANNEL, DISPLAY_SPI_FREQUENCY, DISPLAY_SPI_MODE);
  if (spi_handle < 0) {
    fprintf(stderr, "Failed to init spi: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

void display_free() {
  wiringPiSPIxClose(SPI_CHIP_ENABLE, SPI_CHANNEL);
}

void display_hardware_reset() {
  digitalWrite(RESET_PIN, LOW);
  usleep(10);
  digitalWrite(RESET_PIN, HIGH);
  msleep(10);
  reset_display_state();
}

void display_software_reset() {
  send_command(SOFTWARE_RESET);
  msleep(5);
  reset_display_state();
}

void display_set_sleep(enum display_option state) {
  if (state == display_state.sleep_mode)
    return;
  time_point t;
  double elapsed = real_time_s(display_state.last_sleep_change, t);
  // need to wait 120 sec after last sleep state change
  double wait = 120 - (elapsed * 1000);
  if (wait >= 0)
    msleep(wait);
  if (state == DISPLAY_ENABLE)
    send_command(SLEEP_IN_MODE);
  else
    send_command(SLEEP_OUT_MODE);
  msleep(5);
  display_state.last_sleep_change = t;
  display_state.sleep_mode = state;
}

void display_on(enum display_option option) {
  if (option == display_state.on)
    return;
  if (option == DISPLAY_ENABLE)
    send_command(DISPLAY_ON);
  else
    send_command(DISPLAY_OFF);
  display_state.on = option;
}

void display_invert(enum display_option option) {
  if (option == display_state.invert)
    return;
  if (option == DISPLAY_ENABLE)
    send_command(INVERT_ON);
  else
    send_command(INVERT_OFF);
  display_state.invert = option;
}

void display_set_partial(uint16_t start, uint16_t end) {
  if (display_state.partial_mode != DISPLAY_ENABLE)
    send_command(PARTIAL_MODE);
  display_state.partial_mode = DISPLAY_ENABLE;
  send_command(PARTIAL_AREA_SET);
  send_2_bytes(start);
  send_2_bytes(end);
}

void display_disable_partial() {
  if (display_state.partial_mode != DISPLAY_ENABLE)
    return;
  send_command(NORMAL_MODE);
  display_state.partial_mode = DISPLAY_DISABLE;
}

void display_set_draw_area(int x, int y, int w, int h) {
  
}


/// ---- Helper Definitions ----


void reset_display_state() {
  display_state.sleep_mode = DISPLAY_ENABLE;
  display_state.on = DISPLAY_DISABLE;
  display_state.invert = DISPLAY_DISABLE;
  display_state.last_sleep_change = time_zero();
  display_state.partial_mode = DISPLAY_DISABLE;
  display_state.column_start = 0;
  display_state.column_end = 0;
  display_state.row_start = 0;
  display_state.row_end = 0;
}

void raw_send_buffer(uint8_t *buff, unsigned int size) {
  if (wiringPiSPIxDataRW(SPI_CHIP_ENABLE, SPI_CHANNEL, buff, size) == -1) {
    fprintf(stderr, "Failed to send data over spi: %s\n", strerror(errno));
  }
}

void send_2_bytes(uint16_t data) {
  send_byte(data >> 8);
  send_byte(data);
}

void command_mode() { digitalWrite(DATA_COMMAND_PIN, LOW); }

void data_mode() { digitalWrite(DATA_COMMAND_PIN, HIGH); }

void send_command(enum display_command_byte cmd) {
  command_mode();
  send_byte(cmd);
  data_mode();
}

static uint8_t display_transfer_buffer[SPI_BUFFER_SIZE];

void send_buffer(uint8_t *buff, unsigned int size) {
  if (size == 0)
    return;
  int transfers = size / SPI_BUFFER_SIZE;
  int remainder = size - (transfers * SPI_BUFFER_SIZE);
  for (int i = 0; i < transfers; i++) {
    memcpy(display_transfer_buffer, &buff[i * SPI_BUFFER_SIZE],
           SPI_BUFFER_SIZE);
    raw_send_buffer(display_transfer_buffer, SPI_BUFFER_SIZE);
  }
  if (remainder > 0) {
    memcpy(display_transfer_buffer,
           &buff[transfers * SPI_BUFFER_SIZE], remainder);
    raw_send_buffer(display_transfer_buffer, remainder);
  }
}
