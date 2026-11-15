#include "frame.h"

#include <stdlib.h>
#include <string.h>

char *frame_encode(uint32_t session_id, uint32_t command_id,
                   const void *payload, size_t payload_len, size_t *out_len)
{
  struct C2_HEADER header;
  const size_t frame_len = sizeof header + payload_len;
  char *frame;

  if (payload_len > UINT32_MAX || (payload == NULL && payload_len > 0))
    return NULL;

  frame = malloc(frame_len);
  if (frame == NULL)
    return NULL;

  header.magic = CHECK_IN_MAGIC;
  header.command_id = command_id;
  header.payload_size = (uint32_t)payload_len;
  header.session_id = session_id;

  memcpy(frame, &header, sizeof header);
  if (payload_len > 0)
    memcpy(frame + sizeof header, payload, payload_len);

  *out_len = frame_len;
  return frame;
}

bool frame_decode(const char *body, size_t body_len, struct C2_HEADER *header)
{
  struct C2_HEADER parsed;

  if (body == NULL || body_len < sizeof parsed)
    return false;

  memcpy(&parsed, body, sizeof parsed);

  if (parsed.magic != CHECK_IN_MAGIC || parsed.session_id == 0)
    return false;

  /* The declared payload must account for the rest of the body exactly. */
  if ((size_t)parsed.payload_size + sizeof parsed != body_len)
    return false;

  *header = parsed;
  return true;
}

const char *frame_payload(const char *body)
{
  return body + sizeof(struct C2_HEADER);
}
