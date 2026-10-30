#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static int monotonic_seconds(double *seconds)
{
#if defined(_WIN32)
    *seconds = (double)GetTickCount64() / 1000.0;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    *seconds = (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
#endif
    return 0;
}

static int wait_one_second(void)
{
#if defined(_WIN32)
    Sleep(1000);
#else
    struct timespec remaining = {1, 0};
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
#endif
    return 0;
}

static int run_simulation(void)
{
    const unsigned int iterations = 3;
    double started;
    if (monotonic_seconds(&started) != 0) {
        fputs("Error: could not read the monotonic clock.\n", stderr);
        return EXIT_FAILURE;
    }

    puts("[simulator] Starting local demonstration (3 iterations)");
    for (unsigned int iteration = 1; iteration <= iterations; ++iteration) {
        double now;
        if (monotonic_seconds(&now) != 0) {
            fputs("Error: could not read the monotonic clock.\n", stderr);
            return EXIT_FAILURE;
        }

        /* This task is fixed in the program; there is no external task input. */
        printf("[simulator] Iteration %u/%u: task=report_uptime\n",
               iteration, iterations);
        printf("[simulator] Result: uptime=%.2f seconds\n", now - started);

        if (iteration < iterations) {
            puts("[simulator] Waiting 1 second");
            fflush(stdout);
            if (wait_one_second() != 0) {
                fputs("Error: timer failed.\n", stderr);
                return EXIT_FAILURE;
            }
        }
    }

    puts("[simulator] Finished");
    return EXIT_SUCCESS;
}

static void print_usage(FILE *stream)
{
    fputs("Usage: myc2_beacon [--help | --version | --simulate]\n"
          "\n"
          "Local learning simulator. No networking or command execution.\n"
          "\n"
          "Options:\n"
          "  --help     Show this help message\n"
          "  --version  Show the application version\n"
          "  --simulate Run 3 local uptime tasks, 1 second apart\n", stream);
}

int main(int argc, char *argv[])
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_usage(stdout);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("myc2_beacon " APP_VERSION);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--simulate") == 0) {
        return run_simulation();
    }

    fputs("Error: unsupported arguments.\n", stderr);
    print_usage(stderr);
    return EXIT_FAILURE;
}
