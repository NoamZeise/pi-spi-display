#ifndef DISPLAY_TIME_H
#define DISPLAY_TIME_H

typedef struct time_point {
  // unix time
  unsigned int real_s;
  unsigned int real_us;
  // ticks
  unsigned int cpu;
} time_point;

time_point time_zero();

time_point get_time();

double cpu_time_s(time_point t1, time_point t2);

double real_time_s(time_point t1, time_point t2);

void print_elapsed(time_point);


#endif
