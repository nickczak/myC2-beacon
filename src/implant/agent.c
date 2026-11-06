#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.h"

#define PORT 8080

int main(int argc, char *argv[])
{
  int sock = 0;
  struct sockaddr_in serv_addr;

  // Create socket
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
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
  struct C2_HEADER header;
  header.magic = 0x41414141; // 'AAAA' in hex
  header.command_id = CMD_CHECK_IN;
  header.payload_length = 0; // No payload for check-in
  
  // Send stealth packet directly over the wire
  send(sock, &header, sizeof(header), 0);

  close(sock);
  return 0;
}
