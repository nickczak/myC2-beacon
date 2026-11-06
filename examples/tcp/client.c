#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "client.h"
#include "net.h"
#include "timing.h"

int run_client(const char *server_address)
{
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    if (inet_pton(AF_INET, server_address, &address.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid server address: %s\n", server_address);
        return EXIT_FAILURE;
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation");
        return EXIT_FAILURE;
    }
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) <
            0 ||
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) <
            0 ||
        connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Client setup");
        close(sock);
        return EXIT_FAILURE;
    }
    printf("[client] Connected to %s:8080; heartbeat every second\n",
           server_address);
    for (;;)
    {
        if (send_response(sock, "HEARTBEAT\n") != 0)
        {
            perror("Send heartbeat");
            break;
        }
        char reply[3];
        size_t used = 0;
        while (used < sizeof(reply))
        {
            ssize_t n = recv(sock, reply + used, sizeof(reply) - used, 0);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
            {
                fputs("[client] Server disconnected or response timed out\n",
                      stderr);
                close(sock);
                return EXIT_FAILURE;
            }
            used += (size_t)n;
        }
        if (memcmp(reply, "OK\n", sizeof(reply)) != 0)
        {
            fputs("[client] Unexpected server response\n", stderr);
            break;
        }
        puts("[client] Heartbeat sent; received OK");
        if (wait_one_second() != 0)
        {
            perror("Heartbeat timer");
            break;
        }
    }
    close(sock);
    return EXIT_FAILURE;
}
