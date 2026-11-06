#pragma once

// Force the compiler to pack data tightly without padding
struct __attribute__((packed)) C2_HEADER {
  unsigned int magic; // Magic number to identify the protocol
  unsigned int command_id;
  unsigned int payload_size; // Size of the payload following the header
};

// Define the command types for the protocol
typedef enum {
  CMD_CHECK_IN = 100,
  CMD_EXECUTE_COMMAND = 101,
  CMD_EXIT = 102,
} COMMAND_ID;
