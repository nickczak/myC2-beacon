#ifndef CHECK_IN_PROTOCOL_H
#define CHECK_IN_PROTOCOL_H

#include <stdint.h>

#define CHECK_IN_MAGIC UINT32_C(0x41414141)
#define CMD_CHECK_IN UINT32_C(100)
#define CMD_EXIT UINT32_C(101)
#define CMD_EXECUTE_SHELL UINT32_C(102)

/* Each field is transmitted in network byte order. No payload follows. */
struct C2_HEADER {
  uint32_t magic;
  uint32_t command_id;
  uint32_t payload_size;
};

#endif
