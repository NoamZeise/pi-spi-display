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

void print_elapsed(time_point t1) {
  time_point t2 = get_time();
  double elapsed = (t2.real_s - t1.real_s);
  elapsed += (t2.real_us - t1.real_us) * 1e-6;
  double cpu = (double)(t2.cpu - t1.cpu)
    / CLOCKS_PER_SEC;
  printf("cpu: %f real: %f\n", cpu, elapsed);	
}
