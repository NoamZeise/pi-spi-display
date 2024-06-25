#ifndef DISPLAY_TIME_H
#define DISPLAY_TIME_H

typedef struct time_point {
  // unix time
  unsigned int real_s;
  unsigned int real_us;
  // ticks
  unsigned int cpu;  
} time_point;

time_point get_time();

void print_elapsed(time_point);


#endif
