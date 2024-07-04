#include "display.h"

#include <stdint.h>
#include <stdio.h>  // printf
#include <string.h> // strerror
#include <unistd.h> // sleep
#include <errno.h>
#include <stdlib.h> // exit

#include <pthread.h>

#include <wiringPi.h>
#include <wiringPiSPI.h>

#include "display_consts.h"
#include "pi_wiring_consts.h"
#include "time.h"

#define BRIGHTNESS_CLOCK_DIVISOR 100

void msleep(unsigned int ms) { usleep(ms * 1000); }

void raw_send_buffer(uint8_t *buff, unsigned int size);

void send_byte(uint8_t b) { raw_send_buffer(&b, 1); }

void send_4_bytes(uint16_t d1, uint16_t d2);

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
  // is idle mode enabled
  enum display_option idle_mode;
  // true when x is column address
  enum display_option horizontal;
  enum display_option little_endian;

  int previous_brightness;

  enum display_colour_format colour_format;
  int bits_per_pixel;
  // current draw area
  uint16_t column_start;
  uint16_t column_width;
  uint16_t row_start;
  uint16_t row_width;
} display_state_t;

static display_state_t display_state;

void reset_display_state();


/// ---- Api Implementation ----


int display_open() {
  int result = wiringPiSetupGpio();
  if (result) {
    fprintf(stderr, "Failed to init pi gpio pins: %s\n", strerror(errno));
    return -1;
  }

  pinMode(DATA_COMMAND_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);

  int spi_handle = wiringPiSPIxSetupMode(
      SPI_CHIP_ENABLE, SPI_CHANNEL, DISPLAY_SPI_FREQUENCY, DISPLAY_SPI_MODE);
  if (spi_handle < 0) {
    fprintf(stderr, "Failed to init spi: %s\n", strerror(errno));
    return -1;
  }
  display_brightness(0);
  return 0;
}

void display_close() {
  wiringPiSPIxClose(SPI_CHIP_ENABLE, SPI_CHANNEL);
}

void display_hardware_reset() {
  digitalWrite(RESET_PIN, LOW);
  usleep(10);
  digitalWrite(RESET_PIN, HIGH);
  msleep(10);
  reset_display_state();
}

void display_brightness(unsigned int brightness) {
  if(brightness > MAX_BRIGHTNESS)
    brightness = MAX_BRIGHTNESS;
  if(brightness == 0) {
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, 0);
  } else if(brightness == MAX_BRIGHTNESS) {
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, 1);
  } else {
      pinMode(BACKLIGHT_PIN, PWM_OUTPUT);
      pwmSetMode(PWM_MODE_MS);
      pwmSetClock(BRIGHTNESS_CLOCK_DIVISOR);
      pwmSetRange(MAX_BRIGHTNESS);
      pwmWrite(BACKLIGHT_PIN, brightness);
  }
  if (brightness != 0)
    display_state.previous_brightness = brightness;
}

pthread_mutex_t display_mut;

void display_lock() {
  pthread_mutex_lock(&display_mut);
}

void display_unlock() {
  pthread_mutex_unlock(&display_mut);
}

void display_software_reset() {
  send_command(SOFTWARE_RESET);
  msleep(5);
  reset_display_state();
}

void display_sleep(enum display_option state) {
  if (state == display_state.sleep_mode)
    return;
  time_point t = get_time();
  double elapsed = real_time_s(display_state.last_sleep_change, t);
  // need to wait 120 sec after last sleep state change
  double wait = 120 - (elapsed * 1000);
  if (wait >= 0)
    msleep(wait);
  if (state == DISPLAY_ENABLE) {
    display_brightness(0);
    send_command(SLEEP_IN_MODE);
  } else {
    display_brightness(display_state.previous_brightness);
    send_command(SLEEP_OUT_MODE);
  }
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
  send_4_bytes(start, end);
}

void display_disable_partial() {
  if (display_state.partial_mode != DISPLAY_ENABLE)
    return;
  send_command(NORMAL_MODE);
  display_state.partial_mode = DISPLAY_DISABLE;
}

void display_idle_mode(enum display_option option) {
  if(option == display_state.idle_mode)
    return;
  if(option == DISPLAY_ENABLE)
    send_command(IDLE_MODE_ON);
  else
    send_command(IDLE_MODE_OFF);
  display_state.idle_mode = option;
}

void display_set_address_options(enum display_address_flags flags) {
  send_command(MEMORY_ACCESS_CONTROL);
  send_byte(flags);
  display_state.horizontal = ((flags & ADDRESS_HORIZONTAL_ORIENTATION) > 0);
  enum display_option little_endian = ((flags & ADDRESS_COLOUR_LITTLE_ENDIAN) > 0);
  if (little_endian == display_state.little_endian)
    return;
  send_command(RAM_CONTROL);
  uint8_t data[2];
  // default state of ram control, just changing endianess bit
  data[0] = 0x00;
  data[1] = 0xF0 | (little_endian ? 0b00001000 : 0);
  send_buffer(data, 2);
  display_state.little_endian = little_endian;
}

void display_set_colour_format(enum display_colour_format format) {
  if (format == display_state.colour_format)
    return;
  send_command(COLOUR_FORMAT_SET);
  send_byte(format);
  display_state.colour_format = format;
  switch (display_state.colour_format) {
  default:
    fprintf(stderr, "unrecognised colour format!\n");
    exit(-1);
  case COLOUR_FORMAT_12_BIT:
    display_state.bits_per_pixel = 12;
    break;
  case COLOUR_FORMAT_16_BIT:
    display_state.bits_per_pixel = 16;
    break;
  case COLOUR_FORMAT_18_BIT:
    display_state.bits_per_pixel = 24;
    break;    
  }
}

void send_draw_area(uint16_t column_start, uint16_t column_width, uint16_t column_max,
                    uint16_t row_start,    uint16_t row_width,    uint16_t row_max);

void display_set_draw_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if (display_state.horizontal)
    send_draw_area(x, w, DISPLAY_HORIZONTAL, y, h, DISPLAY_VERTICAL);
  else
    send_draw_area(x, w, DISPLAY_VERTICAL, y, h, DISPLAY_HORIZONTAL);
}

void display_set_draw_area_full() {
  if (display_state.horizontal)
    display_set_draw_area(0, 0, DISPLAY_HORIZONTAL, DISPLAY_VERTICAL);
  else  
    display_set_draw_area(0, 0, DISPLAY_VERTICAL, DISPLAY_HORIZONTAL);
}

void display_draw(uint8_t *colour_data, unsigned int size,
                  enum display_draw_flags flags) {  
  if (size * 8 > (unsigned int)display_state.column_width
      * display_state.row_width
      * display_state.bits_per_pixel) {
    fprintf(stderr,
            "colour data passed was greater than draw area (%d by %d)\n",
            display_state.column_width, display_state.row_width);
    exit(-1);
  }
  if (size * 8 % display_state.bits_per_pixel != 0) {
    fprintf(stderr,
            "colour data passed did not have a whole number of pixels!"
            "pixel width: %d bits, bits passed: %d\n",
            display_state.bits_per_pixel, size * 8);
    exit(-1);
  }
  if (flags & DONT_RESET_DRAW_LOCATION)
    send_command(WRITE_RAM_CONTINUE);
  else
    send_command(WRITE_RAM);
  send_buffer(colour_data, size);
  if (!(flags & DONT_FLUSH_DRAW))
    send_command(NO_OPERATION);
}

void display_combined_setup(enum display_colour_format colour_format,
			    enum display_address_flags address_flags) {
  display_hardware_reset();
  display_sleep(DISPLAY_DISABLE);
  
  display_set_colour_format(colour_format);
  display_set_address_options(address_flags);
  
  display_invert(DISPLAY_ENABLE);
  
  display_set_draw_area_full();
  
  display_on(DISPLAY_ENABLE);
  display_brightness(MAX_BRIGHTNESS);
}


/// ---- Helper Definitions ----


void reset_display_state() {
  display_state.sleep_mode = DISPLAY_ENABLE;
  display_state.on = DISPLAY_DISABLE;
  display_state.invert = DISPLAY_DISABLE;
  display_state.last_sleep_change = time_zero();
  display_state.partial_mode = DISPLAY_DISABLE;
  display_state.idle_mode = DISPLAY_DISABLE;
  display_state.horizontal = DISPLAY_DISABLE;
  display_state.little_endian = DISPLAY_DISABLE;
  display_state.colour_format = COLOUR_FORMAT_18_BIT;
  display_state.bits_per_pixel = 24;

  display_state.previous_brightness = MAX_BRIGHTNESS;
  
  display_state.column_start = 0;
  display_state.column_width = 0;
  display_state.row_start = 0;
  display_state.row_width = 0;
}

void raw_send_buffer(uint8_t *buff, unsigned int size) {
  if (wiringPiSPIxDataRW(SPI_CHIP_ENABLE, SPI_CHANNEL, buff, size) == -1) {
    fprintf(stderr, "Failed to send data over spi: %s\n", strerror(errno));
  }
}

void fill_2_bytes(uint8_t *arr, uint16_t data) {
  arr[0] = data >> 8;
  arr[1] = data;
}

void send_4_bytes(uint16_t d1, uint16_t d2) {
  uint8_t data[4];
  fill_2_bytes(data, d1);
  fill_2_bytes(&data[2], d2);
  send_buffer(data, 4);
}

void command_mode() { digitalWrite(DATA_COMMAND_PIN, LOW); }

void data_mode() { digitalWrite(DATA_COMMAND_PIN, HIGH); }

void send_command(enum display_command_byte cmd) {
  command_mode();
  send_byte(cmd);
  data_mode();
}

void send_buffer(uint8_t *buff, unsigned int size) {
  if (size == 0)
    return;
  int transfers = size / SPI_BUFFER_SIZE;
  int remainder = size % SPI_BUFFER_SIZE;
  for (int i = 0; i < transfers; i++)
    raw_send_buffer(&buff[i * SPI_BUFFER_SIZE], SPI_BUFFER_SIZE);
  if (remainder > 0)
    raw_send_buffer(&buff[transfers * SPI_BUFFER_SIZE], remainder);
}


int check_dimension_invalid(uint16_t start, uint16_t size, uint16_t max) {
  return ((size == 0) | (start > max)) || ((unsigned int)start + size > max);
}

void send_draw_area(uint16_t column_start, uint16_t column_width, uint16_t column_max,
                    uint16_t row_start,    uint16_t row_width,    uint16_t row_max) {
  if (check_dimension_invalid(column_start, column_width, column_max) ||
      check_dimension_invalid(row_start, row_width, row_max)) {
    fprintf(stderr,
            "Draw Area out of range! screen is %d by %d "
            "draw area is %d+%d by %d+%d\n",
            column_max, row_max, column_start, column_width, row_start, row_width);    
    exit(-1);
  }
  display_state.column_start = column_start;
  display_state.column_width = column_width;
  display_state.row_start = row_start;
  display_state.row_width = row_width;
  send_command(COLUMN_ADDRESS_SET);
  send_4_bytes(column_start, column_start + column_width - 1);
  send_command(ROW_ADDRESS_SET);
  send_4_bytes(row_start, row_start + row_width - 1);
}
