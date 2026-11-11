#ifndef CHECK_IN_HTTPS_CLIENT_H
#define CHECK_IN_HTTPS_CLIENT_H

#include <stddef.h>

/* POST 'body_len' bytes from 'body' to https://host:port/path.
   The teamserver certificate is pinned to 'ca_file' and must also match 'host'
   (by IP address when 'host' is a literal, otherwise by DNS name with SNI).

   On success the response body is copied into a malloc'd buffer handed back
   through 'out_body' ('out_len' bytes in size); the caller must free() it.
   Either out parameter may be NULL.

   Returns the HTTP status code, or -1 on any transport or TLS failure. */
int https_post(const char *host, int port, const char *path, const char *body,
               size_t body_len, const char *ca_file, char **out_body,
               size_t *out_len);

#endif
