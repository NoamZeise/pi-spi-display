#include <stdio.h> // printf
#include <string.h> // memset, memcpy
#include <errno.h>

#include "display.h"
#include "mirror.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

void test() {
  display_hardware_reset();

  display_sleep(DISPLAY_DISABLE);

  display_set_colour_format(COLOUR_FORMAT_16_BIT);

  display_set_address_options(
    ADDRESS_FLIP_HORIZONTAL | ADDRESS_HORIZONTAL_ORIENTATION | ADDRESS_COLOUR_LITTLE_ENDIAN);

  display_invert(DISPLAY_ENABLE);

  display_brightness(MAX_BRIGHTNESS);
  display_on(DISPLAY_ENABLE);
  
  const int size = DISPLAY_PIXEL_COUNT * 2;
  uint8_t data[size];
  memset(data, 0xAA, size);
  display_set_draw_area_full();
  display_draw(data, size, DONT_FLUSH_DRAW);

  int size2 = 80 * 40 * 2;
  display_set_draw_area(40, 40, 80, 40);
  uint8_t data2[size2];
  memset(data2, 0xFF, size2);
  display_draw(data2, size2, 0);
}

int main() {
  if (display_open() == -1)
    return -1;

  //test();
  mirror_display();
  
  display_close();
  return 0;
}
