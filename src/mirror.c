#include "mirror.h"

#include "display.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

enum active_window {
  FRAMEBUFFER,
  X_BUFFER,
};

const int BUFF_SIZE = DISPLAY_PIXEL_COUNT * 2;

struct manager_info_t {
  Display* display;
  Window window;
  enum active_window active;
  uint8_t* framebuffer;
};

int close_threads = 0;

void* active_screen_manager(void* info_ptr) {
  struct manager_info_t* info = info_ptr;
  while(!close_threads) {
    if(info->display == NULL) {
      info->display = XOpenDisplay(":0");
      if(info->display) {
        info->window = DefaultRootWindow(info->display);
	XWindowAttributes xwa;
	XGetWindowAttributes(info->display, info->window, &xwa);
	if(   xwa.width  != DISPLAY_HORIZONTAL
	   || xwa.height != DISPLAY_VERTICAL
	   || xwa.depth  != 16) {
	  fprintf(stderr, "X window has unsupported format %d bit %dx%d,"
		  " must be 16 bit 320x240\n",
		  xwa.depth, xwa.width, xwa.height);
	  return NULL;
	}
	info->active = X_BUFFER;
      }
    }
  }
  return NULL;
}

void* screen_renderer(void* info_ptr) {
  struct manager_info_t* info = info_ptr;
  while(!close_threads) {
    if(info->active == FRAMEBUFFER) {
      display_draw(info->framebuffer, BUFF_SIZE, 0);
    } else {
      if(info->display == NULL) {
	fprintf(stderr, "can't draw x window with unopened display!\n");
	return NULL;
      }
      XImage *img = XGetImage(info->display, info->window, 0, 0, DISPLAY_HORIZONTAL, DISPLAY_VERTICAL,
			      AllPlanes, ZPixmap);
      if(img == NULL) {
	fprintf(stderr, "failed to get X image!\n");
	return NULL;
      }
      display_draw((uint8_t*)img->data, BUFF_SIZE, 0);
      XDestroyImage(img);
    }
  }
  return NULL;
}

void mirror_display() {
  display_hardware_reset();
  display_sleep(DISPLAY_DISABLE);
  
  display_set_colour_format(COLOUR_FORMAT_16_BIT);
  display_set_address_options(
    ADDRESS_FLIP_HORIZONTAL | ADDRESS_HORIZONTAL_ORIENTATION | ADDRESS_COLOUR_LITTLE_ENDIAN);
  
  display_invert(DISPLAY_ENABLE);
  
  display_set_draw_area_full();
  
  display_on(DISPLAY_ENABLE);
  display_backlight(DISPLAY_ENABLE);

  int fb = open("/dev/fb0", O_RDONLY);
  if(fb < 0) {
    fprintf(stderr, "Failed to open framebuffer fb0, %s\n", strerror(errno));
    return;
  }
  
  uint8_t* screen_data = mmap(0, BUFF_SIZE, PROT_READ, MAP_SHARED, fb, 0);
  if(screen_data == MAP_FAILED) {
    fprintf(stderr, "Failed to map screen data %s\n", strerror(errno));
    return;
  }

  struct manager_info_t info;
  info.active = FRAMEBUFFER;
  info.display = NULL;
  info.framebuffer = screen_data;
  
  pthread_t manager_thread, screen_renderer_thread;
  int failed = pthread_create(&manager_thread, NULL, active_screen_manager, &info);
  if(failed) {
    fprintf(stderr, "failed to open screen manager thread! %s\n", strerror(failed));
    return;
  }
  failed = pthread_create(&screen_renderer_thread, NULL, screen_renderer, &info);
  if(failed) {
    fprintf(stderr, "failed to open screen render thread! %s\n", strerror(failed));
    return;
  }

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  int sig;
  sigprocmask(SIG_BLOCK, &sigset, NULL);
  failed = sigwait(&sigset, &sig);
  if(failed) {
    fprintf(stderr, "failed to wait for interrupt signal! %s\n", strerror(failed));
  }
  close_threads = 1;
  
  pthread_join(manager_thread, NULL);
  pthread_join(screen_renderer_thread, NULL);
  
  munmap(screen_data, BUFF_SIZE);
  close(fb);
  if(info.display != NULL)
    XCloseDisplay(info.display);
}
