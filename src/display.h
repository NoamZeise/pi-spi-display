#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

/// Library to interface with ST7789 using a raspberry pi
/// Uses pins and SPI interface defined in 'pi_wiring_consts.h'

#include <stdint.h>

enum display_option {
  DISPLAY_ENABLE = 1,
  DISPLAY_DISABLE = 0,
};

/// init gpio and spi pins
/// returns -1 on error
int display_init();

/// close spi connection
void display_deinit();

// perform a hardware reset, takes ~10ms
void display_hardware_reset();

// resets the display to default setting, takes ~5ms
void display_software_reset();

// in sleep mode display display enters minimum power mode
void display_set_sleep(enum display_option option);

// whether or not to display anything on the screen;
void display_on(enum display_option option);

// whether or not to invert the display
void display_invert(enum display_option option);

// set only a section of the display active between start and end locations
void display_set_partial(uint16_t start, uint16_t end);

// turn on full display
void display_disable_partial();

// specify area of the screen write commands will write to
void display_set_draw_area(int x, int y, int w, int h);

#endif
