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
#include <sys/ioctl.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/dpms.h>

#define COLOUR_BYTES 2

#define BUFF_SIZE DISPLAY_PIXEL_COUNT * COLOUR_BYTES

#define FRAMES_UNTIL_MOUSE_GONE 60 * 5

#define FRAMEBUFFER_FILE "/dev/fb0"
// pass NULL to use DISPLAY env var
#define X_DISPLAY ":0.0"

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
};

int map_framebuffer(uint8_t **screen_data);

int close_threads = 0;

void *active_screen_manager(void *info_ptr);
void* screen_renderer(void* info_ptr);

void mirror_display() {
  struct manager_info_t info;
  info.active = FRAMEBUFFER;
  info.display = NULL;
  int fb = map_framebuffer(&info.framebuffer);
  if (fb == -1)
    return;

  display_combined_setup(COLOUR_FORMAT_16_BIT,
			 ADDRESS_FLIP_HORIZONTAL | ADDRESS_HORIZONTAL_ORIENTATION | ADDRESS_COLOUR_LITTLE_ENDIAN);
  display_brightness(MAX_BRIGHTNESS/1.5);

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

  // wait until we get an interrupt signal

  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  int sig;
  sigprocmask(SIG_BLOCK, &sigset, NULL);
  failed = sigwait(&sigset, &sig);
  if(failed)
    fprintf(stderr, "failed to wait for interrupt signal! %s\n", strerror(failed));

  // then close all the threads, clean up and exit

  close_threads = 1;
  
  if((failed = pthread_join(manager_thread, NULL)))
    fprintf(stderr, "failed to join manager thread %s\n", strerror(failed));
  if((failed = pthread_join(screen_renderer_thread, NULL)))
    fprintf(stderr, "failed to join screen render thread %s\n", strerror(failed));
  
  munmap(info.framebuffer, BUFF_SIZE);
  close(fb);

  display_lock();
  display_software_reset();
  display_brightness(0);
  display_unlock();
}

/// ---- Manager Thread ----

jmp_buf x_err_env;
// stop x server errors from killing the program
static int x_error_handler(Display *dpy) { longjmp(x_err_env, 1); }

enum open_x_state {
  OPENED_X,
  UNAVAILABLE_X,
  UNSUPPORTED_X,
};
enum open_x_state try_open_x(Window* window, Display** display);

int get_x_tty();
int get_active_tty();

int is_display_sleeping(Display *display);
void update_sleep_state(int sleeping, enum active_window* state);

void* active_screen_manager(void* info_ptr) {
  struct manager_info_t *info = info_ptr;
  int Xtty = -1;
  int unsupported_x = 0;
  XSetIOErrorHandler(x_error_handler);
  while(!close_threads) {
    if (info->display == NULL && !unsupported_x) {
      Xtty = -1;
      
      switch (try_open_x(&info->window, &info->display)) {	
      case OPENED_X:
        Xtty = get_x_tty();
        break;

      case UNSUPPORTED_X:
        unsupported_x = 1;
        break;

      case UNAVAILABLE_X:
	break;
      }
    }

    int display_sleeping = is_display_sleeping(info->display);
    update_sleep_state(display_sleeping, &info->active);

    if (!display_sleeping && Xtty != -1)
      info->active = get_active_tty() == Xtty ? X_BUFFER : FRAMEBUFFER;
    sleep(1);
  }
  if (info->display != NULL) {
    XCloseDisplay(info->display);
    info->display = NULL;
  }
  return NULL;
}


/// ---- Renderer Thread ----

void get_mouse_pos(Display *display, Window window, int *x, int *y);
void draw_mouse(uint8_t *data, int x, int y);

void* screen_renderer(void* info_ptr) {
  struct manager_info_t* info = info_ptr;
  static uint8_t screen_data[BUFF_SIZE];
  int mouse_x = -1;
  int mouse_y = -1;
  int static_mouse_frames = FRAMES_UNTIL_MOUSE_GONE;
  while (!close_threads) {
    if (info->active == SLEEPING) {
      sleep(1);
      mouse_x = -1;
      mouse_y = -1;
    } else if(info->active == FRAMEBUFFER) {
      memcpy(screen_data, info->framebuffer, BUFF_SIZE);
      display_lock();
      display_draw(screen_data, BUFF_SIZE, 0);
      display_unlock();
      mouse_x = -1;
      mouse_y = -1;
    } else if(info->active == X_BUFFER) {
      if (info->display == NULL)
	goto x_draw_failed;
      if(setjmp(x_err_env))
        goto x_draw_failed;
      
      XImage *img = XGetImage(info->display, info->window,
			      0, 0, DISPLAY_HORIZONTAL, DISPLAY_VERTICAL,
			      AllPlanes, ZPixmap);
      if(img == NULL)
        goto x_draw_failed;
      
      int x, y;
      get_mouse_pos(info->display, info->window, &x, &y);
      int moved = !((x == mouse_x || mouse_x == -1) &&
                    (y == mouse_y || mouse_y == -1));
      if (moved)
        static_mouse_frames = 0;
      mouse_x = x;
      mouse_y = y;
      if (static_mouse_frames < FRAMES_UNTIL_MOUSE_GONE) {
        draw_mouse((uint8_t *)img->data, x, y);
        if (!moved)
	  static_mouse_frames++;
      } 

      display_lock();
      display_draw((uint8_t*)img->data, BUFF_SIZE, 0);
      display_unlock();
      XDestroyImage(img);
      continue;
    x_draw_failed:
      info->active = FRAMEBUFFER;
      info->display = NULL;
    }
  }
  return NULL;
}


/// ---- Helpers ----

int map_framebuffer(uint8_t** screen_data) {  
  int fb = open(FRAMEBUFFER_FILE, O_RDONLY);
  if(fb < 0) {
    fprintf(stderr, "Failed to open framebuffer, %s\n", strerror(errno));
    return -1;
  }

  struct fb_var_screeninfo info;
  if (ioctl(fb, FBIOGET_VSCREENINFO, &info) == -1) {
    fprintf(stderr, "Failed to get framebuffer info, %s\n", strerror(errno));
    close(fb);
    return -1;
  }
  if(info.xres != DISPLAY_HORIZONTAL || info.yres != DISPLAY_VERTICAL) {
    printf("framebuffer res did not match display - fb: %d x %d - display: %d x %d\n",
	   info.xres, info.yres, DISPLAY_HORIZONTAL, DISPLAY_VERTICAL);
    close(fb);
    return -1;
  }
  
  *screen_data = mmap(0, BUFF_SIZE, PROT_READ, MAP_SHARED, fb, 0);
  if(screen_data == MAP_FAILED) {
    fprintf(stderr, "Failed to map screen data %s\n", strerror(errno));
    close(fb);
    return -1;
  }
  return fb;
}


/// ---- Manager Thread Helpers ----

int get_active_tty() {
  char name[5];
  name[4] = '\0';
  int t = open("/sys/class/tty/tty0/active", O_RDONLY);
  if (t == -1) {
    fprintf(stderr, "failed to check which tty was active %s\n", strerror(errno));
  } else {
    int rd = read(t, name, 4);
    close(t);
    if(rd == 4)
      return name[3] - '0';
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
  } else
    fprintf(stderr, "failed to run command to determine x's tty\n");
  return -1;
}

enum open_x_state try_open_x(Window* window, Display** display) {
  *display = XOpenDisplay(X_DISPLAY);
  if (!*display)
    return UNAVAILABLE_X;
  *window = DefaultRootWindow(*display);
  XWindowAttributes xwa;
  XGetWindowAttributes(*display, *window, &xwa);
  if(xwa.width  != DISPLAY_HORIZONTAL
     || xwa.height != DISPLAY_VERTICAL
     || xwa.depth  != 8 * COLOUR_BYTES) {
    fprintf(stderr, "X window has unsupported format %d bit %dx%d,"
	    " must be %d bit %d x %d\n",
	    xwa.depth, xwa.width, xwa.height,
	    COLOUR_BYTES * 8, DISPLAY_HORIZONTAL, DISPLAY_VERTICAL);
    XCloseDisplay(*display);
    *display = NULL;
    return UNSUPPORTED_X;
  }
  return OPENED_X;
}

int is_display_sleeping(Display *display) {
  if (display == NULL)
    return 0;
  CARD16 power;
  BOOL dpms_enabled;
  if(DPMSInfo(display, &power, &dpms_enabled))
    return dpms_enabled && (power == DPMSModeOff);
  return 0;
}

void update_sleep_state(int sleeping, enum active_window* state) {
  if (sleeping && *state != SLEEPING) {
    *state = SLEEPING;
    sleep(1); // wait for render thread to stop
    display_lock();
    display_sleep(DISPLAY_ENABLE);
    display_unlock();
  } else if (!sleeping && *state == SLEEPING) {
    display_lock();
    display_sleep(DISPLAY_DISABLE);
    display_unlock();
    *state = FRAMEBUFFER;
  }
}


/// ---- Draw Thread Helpers ----

void get_mouse_pos(Display *display, Window window, int *x, int *y) {
  int rootx, rooty;
  unsigned int mask;
  Window root, child;
  XQueryPointer(display, window, &root, &child, &rootx,
		&rooty, x, y, &mask);
}

void draw_mouse(uint8_t *data, int x, int y) {
  int size = 10;
  int width = 2;
  for (int x_pos = x; x_pos < x + size && x_pos < DISPLAY_HORIZONTAL;
       x_pos++) {
    for (int y_pos = y; y_pos < y + size && y_pos < DISPLAY_VERTICAL; y_pos++) {
      int mouse_x = x_pos - x;
      int mouse_y = y_pos - y;
      if (mouse_x + mouse_y > size)
        continue;

      uint8_t col = 0xFF;
      if (mouse_x < width || mouse_y < width || mouse_x + mouse_y > size - width)
	col = 0x00;
      
      for (int b = 0; b < COLOUR_BYTES; b++) {
	int screenp = y_pos * DISPLAY_HORIZONTAL * COLOUR_BYTES +
	  x_pos * COLOUR_BYTES + b;
	data[screenp] = col;
      }
    }
  }
}
