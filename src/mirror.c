#include "mirror.h"

#include "display.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>

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

#define BUFF_SIZE DISPLAY_PIXEL_COUNT * 2

struct manager_info_t {
  Display* display;
  Window window;
  Atom window_delete;
  enum active_window active;
  uint8_t *framebuffer;
};

int close_threads = 0;

jmp_buf x_err_env;
// stop x server errors from killing the program
static int x_error_handler(Display *dpy) { longjmp(x_err_env, 1); }

void* active_screen_manager(void* info_ptr) {
  struct manager_info_t *info = info_ptr;
  int Xtty = -1;
  while(!close_threads) {
    sleep(1);
    if (info->display == NULL) {
      Xtty = -1;
      info->display = XOpenDisplay(":0");
      if(info->display) {
        info->window = DefaultRootWindow(info->display);
	info->window_delete = XInternAtom(info->display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(info->display, info->window, &info->window_delete, 1);
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

        FILE *f = popen("ps -e -o tty -o fname | grep Xorg", "r");
	if(f != NULL) {
	  int n = 0;
	  char name[6];
	  name[5] = '\0';
          if (fgets(name, 5, f) == NULL) {
            printf("couldn't determine which tty X is running on\n");
	    Xtty = -1;
	  } else {
	    pclose(f);
	    Xtty = name[3] - '0';
	    //  printf("x: %d, %s\n", Xtty, name);
	  }
	}
      }
    }
    char name[5];
    name[4] = '\0';
    int t = open("/sys/class/tty/tty0/active", O_RDONLY);
    if (t == -1) {
      fprintf(stderr, "failed to check which tty was active %s\n", strerror(errno));
    } else {
      int rd = read(t, name, 4);
      close(t);
      if(rd == 4) {
	int tty = name[3] - '0';
	//	printf("%d == %d ?, %s\n", tty, Xtty, name);
	info->active = tty == Xtty ? X_BUFFER : FRAMEBUFFER;
      }
    }
  }
  if (info->display != NULL) {
    XCloseDisplay(info->display);
    info->display = NULL;
  }
  return NULL;
}

static uint8_t screen_data[BUFF_SIZE];

void* screen_renderer(void* info_ptr) {
  struct manager_info_t* info = info_ptr;  
  while(!close_threads) {
    if(info->active == FRAMEBUFFER) {
      memcpy(screen_data, info->framebuffer, BUFF_SIZE);
      display_draw(screen_data, BUFF_SIZE, 0);
    } else {
      if (info->display == NULL) {
	fprintf(stderr, "can't draw x window with unopened display!\n");
	return NULL;
      }
      if(setjmp(x_err_env)) {
	printf("error caught\n");
	info->display = NULL;
        info->active = FRAMEBUFFER;
	continue;
      } else {
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

  XSetIOErrorHandler(x_error_handler);

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
}
