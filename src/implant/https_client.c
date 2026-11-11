#include <arpa/inet.h>
#include <limits.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "https_client.h"

/* Seconds a stalled connect, send or receive is allowed to take. */
#define IO_TIMEOUT_SEC 10

/* Room for the request line and headers. Path and host are short, and the
   body is sent separately so it never has to fit here. */
#define REQUEST_HEAD_MAX 512

/* Growable byte buffer. */
struct buffer
{
  char *data;
  size_t size;
  size_t capacity;
};

static int buffer_reserve(struct buffer *buffer, size_t extra)
{
  size_t needed;
  size_t capacity;

  if (extra > SIZE_MAX - buffer->size)
  {
    return -1;
  }
  needed = buffer->size + extra;
  if (needed <= buffer->capacity)
  {
    return 0;
  }

  capacity = buffer->capacity == 0 ? 2048 : buffer->capacity;
  while (capacity < needed)
  {
    if (capacity > SIZE_MAX / 2)
    {
      return -1;
    }
    capacity *= 2;
  }

  char *grown = realloc(buffer->data, capacity);
  if (grown == NULL)
  {
    return -1;
  }
  buffer->data = grown;
  buffer->capacity = capacity;
  return 0;
}

/* Print the reason for the most recent OpenSSL failure. */
static void report_tls_failure(const char *what)
{
  unsigned long code = ERR_get_error();

  if (code == 0)
  {
    fprintf(stderr, "%s\n", what);
    return;
  }
  const char *reason = ERR_reason_error_string(code);
  fprintf(stderr, "%s: %s\n", what, reason != NULL ? reason : "unknown error");
}

static int is_ip_literal(const char *host)
{
  struct in_addr ipv4;
  struct in6_addr ipv6;

  if (inet_pton(AF_INET, host, &ipv4) == 1)
  {
    return 1;
  }
  return inet_pton(AF_INET6, host, &ipv6) == 1;
}

/* Connect to host:port, applying timeouts so a dead server cannot hang the
   implant forever. */
static int connect_to(const char *host, int port)
{
  struct addrinfo hints;
  struct addrinfo *addresses = NULL;
  struct addrinfo *candidate;
  char service[16];
  int fd = -1;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  (void)snprintf(service, sizeof(service), "%d", port);

  if (getaddrinfo(host, service, &hints, &addresses) != 0)
  {
    return -1;
  }

  for (candidate = addresses; candidate != NULL; candidate = candidate->ai_next)
  {
    struct timeval timeout;

    fd = socket(candidate->ai_family, candidate->ai_socktype,
                candidate->ai_protocol);
    if (fd < 0)
    {
      continue;
    }

    timeout.tv_sec = IO_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0)
    {
      break;
    }
    (void)close(fd);
    fd = -1;
  }

  freeaddrinfo(addresses);
  return fd;
}

static int tls_send_all(SSL *ssl, const char *data, size_t length)
{
  size_t sent = 0;

  while (sent < length)
  {
    size_t chunk = length - sent;
    if (chunk > (size_t)INT_MAX)
    {
      chunk = (size_t)INT_MAX;
    }

    int written = SSL_write(ssl, data + sent, (int)chunk);
    if (written <= 0)
    {
      return -1;
    }
    sent += (size_t)written;
  }
  return 0;
}

/* Read until the peer closes, which the request asks for with
   'Connection: close'. Keeps the buffer NUL-terminated for parsing. */
static int tls_read_all(SSL *ssl, struct buffer *out)
{
  char chunk[4096];

  for (;;)
  {
    int got = SSL_read(ssl, chunk, (int)sizeof(chunk));
    if (got > 0)
    {
      if (buffer_reserve(out, (size_t)got + 1) != 0)
      {
        return -1;
      }
      memcpy(out->data + out->size, chunk, (size_t)got);
      out->size += (size_t)got;
      out->data[out->size] = '\0';
      continue;
    }

    int error = SSL_get_error(ssl, got);
    if (error == SSL_ERROR_ZERO_RETURN)
    {
      return 0;
    }
    /* A clean EOF without close_notify is normal for HTTP/1.1 servers. */
    if (error == SSL_ERROR_SYSCALL && got == 0)
    {
      return 0;
    }
    return -1;
  }
}

static int parse_status(const struct buffer *raw)
{
  const char *space;
  long status;

  if (raw->size < 12 || strncmp(raw->data, "HTTP/1.", 7) != 0)
  {
    return -1;
  }

  space = strchr(raw->data, ' ');
  if (space == NULL)
  {
    return -1;
  }

  status = strtol(space + 1, NULL, 10);
  return (int)status;
}

/* Body starts after the blank line that ends the headers. */
static char *find_body(const struct buffer *raw, size_t *body_len)
{
  size_t index;

  for (index = 0; index + 3 < raw->size; ++index)
  {
    if (memcmp(raw->data + index, "\r\n\r\n", 4) == 0)
    {
      size_t offset = index + 4;
      *body_len = raw->size - offset;
      return raw->data + offset;
    }
  }
  return NULL;
}

int https_post(const char *host, int port, const char *path, const char *body,
               size_t body_len, const char *ca_file, char **out_body,
               size_t *out_len)
{
  int status = -1;
  int fd = -1;
  SSL_CTX *ctx = NULL;
  SSL *ssl = NULL;
  struct buffer raw = {0};
  char request[REQUEST_HEAD_MAX];
  int request_len;

  if (host == NULL || path == NULL || body == NULL)
  {
    return -1;
  }

  request_len = snprintf(request, sizeof(request),
                         "POST %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Content-Type: application/octet-stream\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         path, host, body_len);
  if (request_len < 0 || (size_t)request_len >= sizeof(request))
  {
    return -1;
  }

  ctx = SSL_CTX_new(TLS_client_method());
  if (ctx == NULL)
  {
    report_tls_failure("could not create TLS context");
    goto cleanup;
  }
  (void)SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
  (void)SSL_CTX_set_default_verify_paths(ctx);

  if (ca_file != NULL && SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1)
  {
    report_tls_failure(ca_file);
    goto cleanup;
  }

  fd = connect_to(host, port);
  if (fd < 0)
  {
    fprintf(stderr, "could not connect to %s:%d\n", host, port);
    goto cleanup;
  }

  ssl = SSL_new(ctx);
  if (ssl == NULL)
  {
    report_tls_failure("could not create TLS session");
    goto cleanup;
  }
  if (SSL_set_fd(ssl, fd) != 1)
  {
    report_tls_failure("could not attach socket to TLS session");
    goto cleanup;
  }

  /* Pin the identity, not just the chain: an IP literal is matched against the
     certificate's IP SAN, a name against its DNS SAN (with SNI). */
  {
    X509_VERIFY_PARAM *param = SSL_get0_param(ssl);

    if (is_ip_literal(host))
    {
      if (X509_VERIFY_PARAM_set1_ip_asc(param, host) != 1)
      {
        report_tls_failure("could not set expected IP address");
        goto cleanup;
      }
    }
    else
    {
      if (X509_VERIFY_PARAM_set1_host(param, host, 0) != 1)
      {
        report_tls_failure("could not set expected host name");
        goto cleanup;
      }
      if (SSL_set_tlsext_host_name(ssl, host) != 1)
      {
        report_tls_failure("could not set SNI host name");
        goto cleanup;
      }
    }
  }

  if (SSL_connect(ssl) != 1)
  {
    report_tls_failure("TLS handshake failed");
    goto cleanup;
  }

  {
    long verify = SSL_get_verify_result(ssl);
    if (verify != X509_V_OK)
    {
      fprintf(stderr, "certificate rejected: %s\n",
              X509_verify_cert_error_string(verify));
      goto cleanup;
    }
  }

  if (tls_send_all(ssl, request, (size_t)request_len) != 0 ||
      tls_send_all(ssl, body, body_len) != 0)
  {
    fprintf(stderr, "failed to send request\n");
    goto cleanup;
  }

  if (tls_read_all(ssl, &raw) != 0)
  {
    fprintf(stderr, "failed to read response\n");
    goto cleanup;
  }

  status = parse_status(&raw);
  if (status < 0)
  {
    fprintf(stderr, "bad response\n");
    goto cleanup;
  }

  {
    size_t response_body_len = 0;
    char *response_body = find_body(&raw, &response_body_len);

    if (response_body == NULL)
    {
      fprintf(stderr, "response had no header terminator\n");
      status = -1;
      goto cleanup;
    }

    if (out_body != NULL && response_body_len != 0)
    {
      char *copy = malloc(response_body_len);
      if (copy == NULL)
      {
        status = -1;
        goto cleanup;
      }
      memcpy(copy, response_body, response_body_len);
      *out_body = copy;
    }
    else if (out_body != NULL)
    {
      *out_body = NULL;
    }
    if (out_len != NULL)
    {
      *out_len = response_body_len;
    }
  }

cleanup:
  if (ssl != NULL)
  {
    (void)SSL_shutdown(ssl);
    SSL_free(ssl);
  }
  if (fd >= 0)
  {
    (void)close(fd);
  }
  if (ctx != NULL)
  {
    SSL_CTX_free(ctx);
  }
  free(raw.data);
  return status;
}
