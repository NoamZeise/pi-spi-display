#include "time.h"

#include <time.h> // clock()
#include <sys/time.h> // gettimeofday()
#include <stdio.h> // printf()

time_point get_time() {
  time_point t;
  struct timeval real;
  gettimeofday(&real, 0);
  t.real_s = real.tv_sec;
  t.real_us = real.tv_usec;
  t.cpu = clock();
  return t;
}

time_point time_zero() {
  time_point t;
  t.cpu = 0;
  t.real_s = 0;
  t.real_us = 0;
  return t;
}

double cpu_time_s(time_point t1, time_point t2) {
  return (double)(t2.cpu - t1.cpu) / CLOCKS_PER_SEC;
}

double real_time_s(time_point t1, time_point t2) {
  double elapsed = (t2.real_s - t1.real_s);
  elapsed += (t2.real_us - t1.real_us) * 1e-6;
  return elapsed;
}

void print_elapsed(time_point t1) {
  time_point t2 = get_time();
  printf("cpu: %f real: %f\n", cpu_time_s(t1, t2), real_time_s(t1, t2));
}
