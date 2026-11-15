#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <openssl/rand.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8443
#define DEFAULT_CA_FILE "certs/server.pem"

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

void config_init(struct target_config *target)
{
  target->host = DEFAULT_HOST;
  target->port = DEFAULT_PORT;
  target->ca_file = DEFAULT_CA_FILE;
  target->session_id = 0;
}

void config_draw_identity(struct target_config *target)
{
  uint32_t id = 0;

  if (RAND_bytes((unsigned char *)&id, (int)sizeof id) != 1 || id == 0)
  {
    /* Fall back to something unique rather than refusing to run. */
    id = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16) ^ (uint32_t)rand();
    if (id == 0)
    {
      id = 1;
    }
  }
  target->session_id = id;
}

int config_parse_args(int argc, char *argv[], struct target_config *target)
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
