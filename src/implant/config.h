#ifndef C2_IMPLANT_CONFIG_H
#define C2_IMPLANT_CONFIG_H

#include <stdint.h>

struct target_config
{
  const char *host;
  int port;
  const char *ca_file;
  /* Identity of this process, drawn once. Every frame carries it so a result is
     attributed to the check-in that was handed the command. */
  uint32_t session_id;
};

/* Fill in the defaults; nothing here talks to the network. */
void config_init(struct target_config *target);

/* Draw this process's identity. Call once, after seeding and parsing. */
void config_draw_identity(struct target_config *target);

/* Returns 0 to continue, 1 when help was printed, -1 on a bad argument. */
int config_parse_args(int argc, char *argv[], struct target_config *target);

#endif
