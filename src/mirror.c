#include "mirror.h"

#include "display.h"
#include "time.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <X11/extensions/dpms.h>

#define COLOUR_BYTES 2

#define BUFF_SIZE DISPLAY_PIXEL_COUNT * COLOUR_BYTES

#define FRAMEBUFFER_FILE "/dev/fb0"
// pass NULL to use DISPLAY env var
#define X_DISPLAY ":0"

enum active_window {
  FRAMEBUFFER,
  X_BUFFER,
  SLEEPING,
};

struct manager_info_t {
  Display* display;
  Window window;
  enum active_window active;
  uint8_t *framebuffer;
  int mouse_active;
};

int close_threads = 0;

jmp_buf x_err_env;
// stop x server errors from killing the program
static int x_error_handler(Display *dpy) { longjmp(x_err_env, 1); }

int get_x_tty();
int get_active_tty();

void* active_screen_manager(void* info_ptr) {
  struct manager_info_t *info = info_ptr;
  int Xtty = -1;
  int invalid_x = 0;
  int sleeping = 0;
  XSetIOErrorHandler(x_error_handler);
  while(!close_threads) {
    sleep(1);
    sleeping = 0;
    if (info->display == NULL && !invalid_x) {
      Xtty = -1;
      info->display = XOpenDisplay(X_DISPLAY);
      if(info->display) {
        info->window = DefaultRootWindow(info->display);
	XWindowAttributes xwa;
	XGetWindowAttributes(info->display, info->window, &xwa);
	if(   xwa.width  != DISPLAY_HORIZONTAL
	   || xwa.height != DISPLAY_VERTICAL
	   || xwa.depth  != 8 * COLOUR_BYTES) {
	  fprintf(stderr, "X window has unsupported format %d bit %dx%d,"
		  " must be %d bit %d x %d\n",
		  xwa.depth, xwa.width, xwa.height,
		  COLOUR_BYTES * 8, DISPLAY_HORIZONTAL, DISPLAY_VERTICAL);
	  invalid_x = 1;
	  XCloseDisplay(info->display);
	  info->display = NULL;
	  continue;
        }
        Xtty = get_x_tty();
      }
    } else if(!invalid_x) {
      CARD16 power;
      BOOL enabled;
      if(DPMSInfo(info->display, &power, &enabled))
	sleeping = enabled && (power == DPMSModeOff);
    }

    if (sleeping) {
      if (info->active != SLEEPING) {
        info->active = SLEEPING;
        display_brightness(0);
        sleep(1); // wait for render thread to stop
	display_sleep(DISPLAY_ENABLE);
      }
    } else {
      if (info->active == SLEEPING) {	
	display_sleep(DISPLAY_DISABLE);
        display_brightness(MAX_BRIGHTNESS);
	info->active = FRAMEBUFFER;
      }
      if(Xtty != -1) {
	info->active = get_active_tty() == Xtty ? X_BUFFER : FRAMEBUFFER;	
      }
    }
  }
  if (info->display != NULL) {
    XCloseDisplay(info->display);
    info->display = NULL;
  }
  return NULL;
}

void* screen_renderer(void* info_ptr) {
  struct manager_info_t* info = info_ptr;
  uint8_t screen_data[BUFF_SIZE];
  while (!close_threads) {
    if (info->active == SLEEPING) {
      sleep(1);
    } else if(info->active == FRAMEBUFFER) {
      memcpy(screen_data, info->framebuffer, BUFF_SIZE);
      display_draw(screen_data, BUFF_SIZE, 0);
    } else {
      //time_point t = get_time();
      if (info->display == NULL)
	goto x_draw_failed;
      if(setjmp(x_err_env))
	goto x_draw_failed;
      else {
	XImage *img = XGetImage(info->display, info->window,
				0, 0, DISPLAY_HORIZONTAL, DISPLAY_VERTICAL,
				AllPlanes, ZPixmap);
	if(img == NULL)
          goto x_draw_failed;
	if(info->mouse_active) {
	  int x, y, rootx, rooty;
	  unsigned int mask;
	  Window root, child;
	  XQueryPointer(info->display, info->window, &root, &child, &rootx,
			&rooty, &x, &y, &mask);
	  for (int xp = x; xp < x + 10 && xp < DISPLAY_HORIZONTAL;
	       xp++) {
	    for (int yp = y;
		 yp < y + 10 && yp < DISPLAY_VERTICAL; yp++) {
	      for (int b = 0; b < COLOUR_BYTES; b++) {
		int screenp = yp * DISPLAY_HORIZONTAL * COLOUR_BYTES +
		  xp * COLOUR_BYTES + b;
		((uint8_t *)img->data)[screenp] = 0x00;
	      }
	    }
	  }
	}
	display_draw((uint8_t*)img->data, BUFF_SIZE, 0);
        XDestroyImage(img);
	//print_elapsed(t);
      }
      continue;
    x_draw_failed:
      info->display = NULL;
      info->active = FRAMEBUFFER;
    }
  }
  return NULL;
}

int map_framebuffer(uint8_t** screen_data);

void mirror_display() {
  display_combined_setup(COLOUR_FORMAT_16_BIT,
    ADDRESS_FLIP_HORIZONTAL | ADDRESS_HORIZONTAL_ORIENTATION | ADDRESS_COLOUR_LITTLE_ENDIAN);
  display_brightness(MAX_BRIGHTNESS/1.5);

  struct manager_info_t info;
  info.active = FRAMEBUFFER;
  info.display = NULL;
  info.mouse_active = 0;
  int fb = map_framebuffer(&info.framebuffer);
  if(fb == -1) return;

  XInitThreads();
  
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
  if(failed)
    fprintf(stderr, "failed to wait for interrupt signal! %s\n", strerror(failed));
  
  close_threads = 1;
  
  if((failed = pthread_join(manager_thread, NULL)))
    fprintf(stderr, "failed to join manager thread %s\n", strerror(failed));
  if((failed = pthread_join(screen_renderer_thread, NULL)))
    fprintf(stderr, "failed to join screen render thread %s\n", strerror(failed));
  
  munmap(info.framebuffer, BUFF_SIZE);
  close(fb);

  display_software_reset();
  display_brightness(0);
}


/// ---- Helpers ----

int get_active_tty() {
  char name[5];
  name[4] = '\0';
  int t = open("/sys/class/tty/tty0/active", O_RDONLY);
  if (t == -1) {
    fprintf(stderr, "failed to check which tty was active %s\n", strerror(errno));
  } else {
    int rd = read(t, name, 4);
    close(t);
    if(rd == 4) {
      return name[3] - '0';
    }
  }
  return -1;
}

int get_x_tty() {
  FILE *f = popen("ps -e -o tty -o fname | grep Xorg", "r");
  if(f != NULL) {
    char name[6];
    name[5] = '\0';
    if (fgets(name, 5, f) == NULL) {
      printf("couldn't determine which tty X is running on\n");      
    } else {
      pclose(f);
      return name[3] - '0';
    }
  } else {
    fprintf(stderr, "failed to run command to determine x's tty\n");
  }
  return -1;
}

int map_framebuffer(uint8_t** screen_data) {  
  int fb = open(FRAMEBUFFER_FILE, O_RDONLY);
  if(fb < 0) {
    fprintf(stderr, "Failed to open framebuffer, %s\n", strerror(errno));
    return -1;
  }  
  *screen_data = mmap(0, BUFF_SIZE, PROT_READ, MAP_SHARED, fb, 0);
  if(screen_data == MAP_FAILED) {
    fprintf(stderr, "Failed to map screen data %s\n", strerror(errno));
    return -1;
    close(fb);
  }
  return fb;
}


