#include <errno.h>
#include <string.h>
#include <sys/socket.h>

#include "net.h"

int send_response(int fd, const char *response)
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
