#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <time.h>

#include "timing.h"

int monotonic_seconds(double *seconds)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    *seconds = (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
    return 0;
}

int wait_one_second(void)
{
    struct timespec remaining = {1, 0};
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}
