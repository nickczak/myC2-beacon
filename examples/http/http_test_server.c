#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* One-request localhost TLS fixture, not a general-purpose HTTP server. */
static int handle_request(SSL *tls)
{
    int fd = SSL_get_fd(tls);
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) <
            0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Socket timeout");
        return EXIT_FAILURE;
    }

    if (SSL_accept(tls) != 1)
    {
        fputs("TLS handshake failed\n", stderr);
        ERR_print_errors_fp(stderr);
        return EXIT_FAILURE;
    }

    char request[8192];
    size_t used = 0;
    char *end = NULL;
    while (end == NULL && used < sizeof(request) - 1)
    {
        int n =
            SSL_read(tls, request + used, (int)(sizeof(request) - 1 - used));
        if (n <= 0)
        {
            fputs("Incomplete request headers or receive timeout\n", stderr);
            return EXIT_FAILURE;
        }
        used += (size_t)n;
        request[used] = '\0';
        end = strstr(request, "\r\n\r\n");
    }
    if (end == NULL)
    {
        fputs("Request headers too large\n", stderr);
        return EXIT_FAILURE;
    }
    size_t body_offset = (size_t)(end - request) + 4;
    *end = '\0';
    char *save = NULL;
    char *line = strtok_r(request, "\r\n", &save);
    if (line == NULL || strcmp(line, "POST /heartbeat HTTP/1.1") != 0)
    {
        fputs("Expected POST /heartbeat HTTP/1.1\n", stderr);
        return EXIT_FAILURE;
    }

    size_t body_length = 0;
    int have_length = 0;
    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL)
    {
        if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 ||
            strncasecmp(line, "Expect:", 7) == 0)
        {
            fputs("Transfer-Encoding and Expect are unsupported by this "
                  "fixture\n",
                  stderr);
            return EXIT_FAILURE;
        }
        if (strncasecmp(line, "Content-Length:", 15) == 0)
        {
            char *value = line + 15;
            while (*value == ' ' || *value == '\t')
                ++value;
            char *tail;
            errno = 0;
            unsigned long length = strtoul(value, &tail, 10);
            while (*tail == ' ' || *tail == '\t')
                ++tail;
            if (have_length || *value < '0' || *value > '9' || errno != 0 ||
                *tail != '\0' || length == 0 || length > 1024)
            {
                fputs(
                    "Invalid or duplicate Content-Length (limit: 1024 bytes)\n",
                    stderr);
                return EXIT_FAILURE;
            }
            body_length = (size_t)length;
            have_length = 1;
        }
    }
    if (!have_length || body_length > sizeof(request) - body_offset)
    {
        fputs("Missing Content-Length or oversized request\n", stderr);
        return EXIT_FAILURE;
    }
    while (used - body_offset < body_length)
    {
        int n = SSL_read(tls, request + used,
                         (int)(body_offset + body_length - used));
        if (n <= 0)
        {
            fputs("Incomplete request body or receive timeout\n", stderr);
            return EXIT_FAILURE;
        }
        used += (size_t)n;
    }
    printf("Received POST /heartbeat (%zu bytes): ", body_length);
    (void)fwrite(request + body_offset, 1, body_length, stdout);
    putchar('\n');

    const char response[] = "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: 16\r\n"
                            "Connection: close\r\n\r\n"
                            "{\"status\":\"ok\"}\n";
    size_t sent = 0;
    while (sent < sizeof(response) - 1)
    {
        int n =
            SSL_write(tls, response + sent, (int)(sizeof(response) - 1 - sent));
        if (n <= 0)
        {
            perror("Send response");
            return EXIT_FAILURE;
        }
        sent += (size_t)n;
    }
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s server-cert.pem server-key.pem\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("Ignore SIGPIPE");
        return EXIT_FAILURE;
    }

    int status = EXIT_FAILURE;
    int listener = -1;
    int fd = -1;
    SSL *tls = NULL;
    SSL_CTX *context = SSL_CTX_new(TLS_server_method());
    if (context == NULL ||
        SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_use_certificate_chain_file(context, argv[1]) != 1 ||
        SSL_CTX_use_PrivateKey_file(context, argv[2], SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(context) != 1)
    {
        fputs("TLS certificate/key configuration failed\n", stderr);
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
    {
        perror("Socket creation");
        goto cleanup;
    }
    int reuse = 1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(8443);
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
            0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 1) < 0)
    {
        perror("HTTPS test server setup");
        goto cleanup;
    }
    puts("Listening on https://localhost:8443 (IPv4 loopback; one request)");
    fflush(stdout);
    do
    {
        fd = accept(listener, NULL, NULL);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
    {
        perror("Accept");
        goto cleanup;
    }
    tls = SSL_new(context);
    if (tls == NULL || SSL_set_fd(tls, fd) != 1)
    {
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }
    status = handle_request(tls);
    if (status == EXIT_SUCCESS)
    {
        (void)SSL_shutdown(tls);
    }

cleanup:
    SSL_free(tls);
    if (fd >= 0)
        close(fd);
    if (listener >= 0)
        close(listener);
    SSL_CTX_free(context);
    return status;
}
