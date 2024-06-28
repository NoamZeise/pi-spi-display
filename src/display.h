#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <stdint.h>

/// Library to interface with ST7789 using a raspberry pi
/// Uses pins and SPI interface defined in 'pi_wiring_consts.h'

#define DISPLAY_HORIZONTAL 320
#define DISPLAY_VERTICAL 240
#define DISPLAY_PIXEL_COUNT 240 * 320

enum display_option {
  DISPLAY_ENABLE = 1,
  DISPLAY_DISABLE = 0,
};

/// init gpio and spi pins
/// returns -1 on error
int display_open();

/// close spi connection
void display_close();

// perform a hardware reset, takes ~10ms
void display_hardware_reset();

// turn the backlight on or off
void display_backlight(enum display_option option);

// resets the display to default setting, takes ~5ms
void display_software_reset();

// in sleep mode display display enters minimum power mode
void display_sleep(enum display_option option);

// whether or not to display anything on the screen;
void display_on(enum display_option option);

// whether or not to invert the display
void display_invert(enum display_option option);

// set only a section of the display active between start and end locations
void display_set_partial(uint16_t start, uint16_t end);

// turn on full display
void display_disable_partial();

enum display_address_flags {
  ADDRESS_FLIP_HORIZONTAL        = 0b10000000,
  ADDRESS_FLIP_VERTICAL          = 0b01000000,
  ADDRESS_HORIZONTAL_ORIENTATION = 0b00100000,
  ADDRESS_SWAP_COLOUR_ORDER      = 0b00001000,
  ADDRESS_REFRESH_BOTTOM_TO_TOP  = 0b00010000,
  ADDRESS_REFRESH_RIGHT_TO_LEFT  = 0b00000100,
  ADDRESS_COLOUR_LITTLE_ENDIAN   = 0b00000001,
};
// change the orientation of the display and how colour data is read, 0 for default
void display_set_address_options(enum display_address_flags flags);

enum display_colour_format {
  // 4-4-4 RRRRGGGGBBBB
  COLOUR_FORMAT_12_BIT = 0b01010011,
  // 5-6-5 RRRRRGGGGGGBBBBB
  COLOUR_FORMAT_16_BIT = 0b01010101,
  // 6-6-6 RRRRRRXXGGGGGGXXBBBBBBXX
  COLOUR_FORMAT_18_BIT = 0b01010110,
};
// change the colour bit depth 
void display_set_colour_format(enum display_colour_format format);

// specify area of the screen write commands will write to
void display_set_draw_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

// set draw area to whole screen
void display_set_draw_area_full();

enum display_draw_flags {
  // move to 0, 0 of draw area before drawing
  DONT_RESET_DRAW_LOCATION = 1,
  // display will only update one a command is recieved after drawing
  // so this option will not pass a NO OPERATION so display update is delayed
  // until next command is sent
  DONT_FLUSH_DRAW = 1 << 1,
};
// draw pixel data to the display, must be a whole number of pixels
void display_draw(uint8_t *colour_data, unsigned int size, enum display_draw_flags flags);

#endif
