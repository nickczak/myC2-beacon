#include "frame_post.h"

#include <stdio.h>
#include <stdlib.h>

#include "frame.h"
#include "https_client.h"

int post_frame(const struct target_config *target, const char *path,
               uint32_t command_id, const char *payload, size_t payload_len,
               char **out_body, size_t *out_len)
{
  size_t frame_len = 0;
  char *frame = frame_encode(target->session_id, command_id, payload,
                             payload_len, &frame_len);
  int status;

  if (frame == NULL)
  {
    fprintf(stderr, "allocation failed\n");
    return -1;
  }

  status = https_post(target->host, target->port, path, frame, frame_len,
                      target->ca_file, out_body, out_len);

  free(frame);
  return status;
}
