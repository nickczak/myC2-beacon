#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>

static int monotonic_seconds(double *seconds)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    *seconds = (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
    return 0;
}

static int wait_one_second(void)
{
    struct timespec remaining = {1, 0};
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
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

static int send_response(int fd, const char *response)
{
    size_t sent = 0;
    const size_t length = strlen(response);
    while (sent < length) {
        ssize_t n = send(fd, response + sent, length - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void *handle_client(void *argument)
{
    int fd = *(int *)argument;
    free(argument);
    printf("[server] Client connected (socket %d)\n", fd);
    char buffer[BUFSIZ];
    size_t used = 0;
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Socket timeout");
        close(fd);
        return NULL;
    }
    for (;;) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (ch == '\n') {
            int heartbeat = used == sizeof("HEARTBEAT") - 1 &&
                            memcmp(buffer, "HEARTBEAT", used) == 0;
            if (send_response(fd, heartbeat ? "OK\n" : "ERROR unknown message\n") != 0)
                break;
            printf("[server] Client %d: %s\n", fd,
                   heartbeat ? "Heartbeat received; sent OK" : "Unknown message rejected");
            used = 0;
        } else if (used < sizeof(buffer)) {
            buffer[used++] = ch;
        } else {
            (void)send_response(fd, "ERROR message too long\n");
            break;
        }
    }
    printf("[server] Client disconnected or timed out (socket %d)\n", fd);
    close(fd);
    return NULL;
}

static int run_server(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation");
        return EXIT_FAILURE;
    }
    int opt = 1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(8080);
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ||
        bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(sock, 16) < 0) {
        perror("Server setup");
        close(sock);
        return EXIT_FAILURE;
    }
    puts("[server] Listening on 127.0.0.1:8080 (5-second idle timeout)");
    fflush(stdout);
    for (;;) {
        int fd = accept(sock, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("Accept");
            close(sock);
            return EXIT_FAILURE;
        }
        int *argument = malloc(sizeof(*argument));
        if (argument == NULL) {
            close(fd);
            continue;
        }
        *argument = fd;
        pthread_t thread;
        int error = pthread_create(&thread, NULL, handle_client, argument);
        if (error != 0) {
            fprintf(stderr, "Client thread: %s\n", strerror(error));
            free(argument);
            close(fd);
            continue;
        }
        (void)pthread_detach(thread);
    }
}

static int run_client(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation");
        return EXIT_FAILURE;
    }
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(8080);
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
        connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Client setup");
        close(sock);
        return EXIT_FAILURE;
    }
    puts("[client] Connected to 127.0.0.1:8080; heartbeat every second");
    for (;;) {
        if (send_response(sock, "HEARTBEAT\n") != 0) {
            perror("Send heartbeat");
            break;
        }
        char reply[3];
        size_t used = 0;
        while (used < sizeof(reply)) {
            ssize_t n = recv(sock, reply + used, sizeof(reply) - used, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                fputs("[client] Server disconnected or response timed out\n", stderr);
                close(sock);
                return EXIT_FAILURE;
            }
            used += (size_t)n;
        }
        if (memcmp(reply, "OK\n", sizeof(reply)) != 0) {
            fputs("[client] Unexpected server response\n", stderr);
            break;
        }
        puts("[client] Heartbeat sent; received OK");
        if (wait_one_second() != 0) {
            perror("Heartbeat timer");
            break;
        }
    }
    close(sock);
    return EXIT_FAILURE;
}

static void print_usage(FILE *stream)
{
    fputs("Usage: myc2_beacon [--help | --version | --simulate | --server | --client]\n"
          "\n"
          "Local learning simulator and localhost heartbeat server.\n"
          "\n"
          "Options:\n"
          "  --help     Show this help message\n"
          "  --version  Show the application version\n"
          "  --simulate Run 3 local uptime tasks, 1 second apart\n"
          "  --client Send a heartbeat every second to 127.0.0.1:8080\n"
          "  --server Listen for heartbeats on 127.0.0.1:8080\n", stream);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

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

    if (argc == 2 && strcmp(argv[1], "--server") == 0) {
        return run_server();
    }

    if (argc == 2 && strcmp(argv[1], "--client") == 0) {
        return run_client();
    }

    fputs("Error: unsupported arguments.\n", stderr);
    print_usage(stderr);
    return EXIT_FAILURE;
}
