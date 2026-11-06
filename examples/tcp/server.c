#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server.h"
#include "net.h"

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

int run_server(const char *local_address)
{
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    if (inet_pton(AF_INET, local_address, &address.sin_addr) != 1)
    {
        fprintf(stderr, "Error: invalid bind IPv4 address: %s\n", local_address);
        return EXIT_FAILURE;
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation");
        return EXIT_FAILURE;
    }
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ||
        bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(sock, 16) < 0) {
        perror("Server setup");
        close(sock);
        return EXIT_FAILURE;
    }
    printf("[server] Listening on %s:8080 (5-second idle timeout)\n", local_address);
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
