#ifndef C2_FRAME_H
#define C2_FRAME_H

#include <stdbool.h>
#include <stddef.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Both binaries must agree on this size; break the build if they stop. */
#if defined(__cplusplus)
static_assert(sizeof(struct C2_HEADER) == 16, "C2_HEADER must stay 16 bytes");
#else
_Static_assert(sizeof(struct C2_HEADER) == 16, "C2_HEADER must stay 16 bytes");
#endif

/* One malloc'd header || payload block, or NULL. *out_len gets the frame size
   and the caller frees it. A null payload with zero length sends a bare
   header. */
char *frame_encode(uint32_t session_id, uint32_t command_id,
                   const void *payload, size_t payload_len, size_t *out_len);

/* True when the frame is self-consistent. Whether it is addressed to the caller
   is the caller's check. */
bool frame_decode(const char *body, size_t body_len, struct C2_HEADER *header);

/* body + header. */
const char *frame_payload(const char *body);

#ifdef __cplusplus
}
#endif

#endif
