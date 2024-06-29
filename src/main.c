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

void draw_screen() {

    const int size = DISPLAY_PIXEL_COUNT * 2;
  uint8_t data[size];
  memset(data, 0x00, size);
  display_set_draw_area_full();
  display_draw(data, size, DONT_FLUSH_DRAW);

  display_on(DISPLAY_ENABLE);
  
  Display *display = XOpenDisplay(":0");
  
  if (display) {
    Window win = DefaultRootWindow(display);

    XWindowAttributes xwa;
    XGetWindowAttributes(display, win, &xwa);
    printf("%d x %d %d bits\n", xwa.width, xwa.height, xwa.depth);
    
    for (;;) {
      XImage *img = XGetImage(display, win, 0, 0, 320, 240, AllPlanes, ZPixmap);
      display_draw((uint8_t*)img->data, size, 0);
      XDestroyImage(img);
    }
    XCloseDisplay(display);
  } else {
    int fb = open("/dev/fb0", O_RDONLY);
    if (fb < 0) {
      printf("failed to open display framebuffer, %s\n", strerror(errno));
      return;
    }

    uint8_t *screen_data =
      mmap(0, size, PROT_READ, MAP_SHARED, fb, 0);
    if (screen_data == MAP_FAILED) {
      printf("failed to map display data to buffer%s\n", strerror(errno));
      return;
    }

    for (;;) {
      memcpy(data, screen_data, size);
      display_draw(data, size, 0);
    }
  }

  display_backlight(DISPLAY_DISABLE); 
}

int main() {
  if (display_open() == -1)
    return -1;

  display_hardware_reset();

  display_sleep(DISPLAY_DISABLE);

  display_set_colour_format(COLOUR_FORMAT_16_BIT);

  display_set_address_options(
    ADDRESS_FLIP_HORIZONTAL | ADDRESS_HORIZONTAL_ORIENTATION | ADDRESS_COLOUR_LITTLE_ENDIAN);

  display_invert(DISPLAY_ENABLE);

  display_backlight(DISPLAY_ENABLE);

  test();
  //draw_screen();

  mirror_display();
  
  display_close();
  return 0;
}
