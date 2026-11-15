#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "beacon.h"
#include "config.h"

int main(int argc, char *argv[])
{
  struct target_config target;
  int parsed;

  /* Seed once: without this every instance would draw the same sequence. */
  srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

  config_init(&target);
  parsed = config_parse_args(argc, argv, &target);
  if (parsed != 0)
  {
    return parsed > 0 ? 0 : 2;
  }

  /* One draw for the life of the process, before the first beacon. */
  config_draw_identity(&target);

  while (beacon_once(&target) == 0)
  {
    beacon_wait();
  }

  return 0;
}
