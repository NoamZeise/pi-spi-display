#include <stdint.h> // uint8_t
#include <stdio.h> // printf
#include <unistd.h> // sleep
#include <string.h> // strerror
#include <stdlib.h> // exit
#include <errno.h> // errno

#include <wiringPi.h>
#include <wiringPiSPI.h>

#include "display_consts.h"
#include "pi_wiring_consts.h"
#include "time.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

int spi_handle = -1;

void msleep(unsigned int ms) { usleep(ms * 1000); }

void hardware_reset() {
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

static uint8_t display_transfer_buffer[DISPLAY_TRANSFER_BUFFER_SIZE];

void send_buffer(uint8_t *buff, unsigned int size) {
  if (size == 0)
    return;
  int transfers = size / DISPLAY_TRANSFER_BUFFER_SIZE;
  int remainder = size - (transfers * DISPLAY_TRANSFER_BUFFER_SIZE);
  for (int i = 0; i < transfers; i++) {
    memcpy(display_transfer_buffer, &buff[i * DISPLAY_TRANSFER_BUFFER_SIZE],
           DISPLAY_TRANSFER_BUFFER_SIZE);
    raw_send_buffer(display_transfer_buffer, DISPLAY_TRANSFER_BUFFER_SIZE);
  }
  if (remainder > 0) {
    memcpy(display_transfer_buffer,
           &buff[transfers * DISPLAY_TRANSFER_BUFFER_SIZE], remainder);
    raw_send_buffer(display_transfer_buffer, remainder);
  }
}

void screen_address_set(uint8_t cmd, uint16_t start, uint16_t end) {
  send_command(cmd);
  uint8_t buff[4];
  buff[0] = start >> 8;
  buff[1] = start & 0xFF;
  buff[2] = end >> 8;
  buff[3] = end & 0xFF;
  send_buffer(buff, 4);
}

void partial_area_set(int start, int end) {
  send_command(PARTIAL_AREA_SET);
  uint8_t buff[4];
  buff[0] = start >> 8;
  buff[1] = start;
  buff[2] = end >> 8;
  buff[3] = end;
  send_buffer(buff, 4);
}

void init_wiring_pi() {
  int res = wiringPiSetupGpio();
  if (res) {
    printf("failed to init gpio! %s\n", strerror(res));
    exit(res);
  }

  spi_handle = wiringPiSPISetupMode(SPI_CHANNEL, DISPLAY_SPI_FREQUENCY, DISPLAY_SPI_MODE);
  if (spi_handle < 0) {
    fprintf(stderr, "failed to open spi bus: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }

  pinMode(DATA_COMMAND_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(BACKLIGHT_PIN, OUTPUT);
}

int main() {
  init_wiring_pi(); 

  hardware_reset();
    
  send_command(SOFTWARE_RESET);
  msleep(5);

  send_command(SLEEP_OUT_MODE);
  msleep(5);

  send_command(COLOUR_FORMAT_SET);
  send_byte(0x55);

  screen_address_set(COLUMN_ADDRESS_SET, 0, DISPLAY_WIDTH);
  screen_address_set(ROW_ADDRESS_SET, 0, DISPLAY_HEIGHT);

  send_command(MEMORY_ACCESS_CONTROL);
  send_byte(0b10100000);

  send_command(INVERT_ON);

  send_command(WRITE_RAM);
  int size = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2;
  uint8_t data[size];
  memset(data, 0xFF, size);
  send_buffer(data, size);

  digitalWrite(BACKLIGHT_PIN, HIGH);
  send_command(DISPLAY_ON);

  int px = 0, py = 0, pw = 50, ph = 50;
  int dx = 1, dy = 1;

  Display *display = XOpenDisplay(":0");
  
  if (display) {
    Window win = DefaultRootWindow(display);

    XWindowAttributes xwa;
    XGetWindowAttributes(display, win, &xwa);
    printf("%d x %d %d bits\n", xwa.width, xwa.height, xwa.depth);
    
    for (;;) {
      XImage *img = XGetImage(display, win, 0, 0, 320, 240, AllPlanes, ZPixmap);
      //      printf("%d %x %x %x\n", img->bits_per_pixel, img->red_mask, img->green_mask, img->blue_mask);
      for(int i = 0; i < DISPLAY_HEIGHT * DISPLAY_WIDTH; i++) {
	uint16_t c = ((img->data[i*4 + 0] & 0b11111000) << 8)
	  | ((img->data[i*4 + 1] & 0b11111100) << 3)
	  | ((img->data[i*4 + 2] & 0b11111000) >> 3);
	data[i*2] = c >> 8;
	data[i*2 + 1] = c;
      }
      //memcpy(data, img->data, size);
      XDestroyImage(img);
      
      send_command(WRITE_RAM);
      send_buffer(data, size);
      send_command(NO_OPERATION);
    }
    XCloseDisplay(display);
  } else {
    printf("failed to open display %d\n", display);
  int fb = open("/dev/fb0", O_RDONLY);
  if (fb < 0) {
    printf("failed to open display framebuffer, %s\n", strerror(errno));
    exit(-1);
  }

  uint8_t *screen_data =
      mmap(0, size, PROT_READ, MAP_SHARED, fb, 0);
  if (screen_data == MAP_FAILED) {
    printf("failed to map display data to buffer%s\n", strerror(errno));
    exit(-1);
  }

  for (;;) {
    /*memset(data, 0xFF, size);
    px += dx;
    py += dy;

    if (px <= 0 || px + pw >= DISPLAY_WIDTH)
      dx *= -1;
    if (py <= 0 || py + ph >= DISPLAY_HEIGHT)
      dy *= -1;

    for (int x = px; x < px + pw; x++) {
      for (int y = py; y < py + ph; y++) {
        int col = ((x - px)/2 << 11) & 0b1111100000000000 |
                  (y - py)/2 << 5 & 0b0000011111100000;
          data[x * DISPLAY_HEIGHT * 2 + y * 2] = col >> 8;
          data[x * DISPLAY_HEIGHT * 2 + y * 2 + 1] = col;
        }
        }*/
    memcpy(data, screen_data, size);
	
    send_command(WRITE_RAM);
    send_buffer(data, size);
    send_command(NO_OPERATION);
  }
      }

  printf("done!\n");
  
  sleep(1);    

  digitalWrite(BACKLIGHT_PIN, LOW);
  wiringPiSPIClose(SPI_CHANNEL);
  printf("program end\n");
  return 0;
}
