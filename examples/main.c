#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "server.h"

static void print_usage(FILE *stream)
{
    fputs("Usage: myc2_beacon [--help | --version |\n"
          "                      --server [--bind IPv4 | --localhost] | "
          "--client [--host IPv4]]\n"
          "\n"
          "Heartbeat client/server.\n"
          "\n"
          "Options:\n"
          "  --help     Show this help message\n"
          "  --version  Show the application version\n"
          "  --client Send a heartbeat every second to port 8080\n"
          "  --server Listen for heartbeats on port 8080\n"
          "  --bind IPv4 Set the server's local IPv4 address (default: "
          "127.0.0.1)\n"
          "  --host IPv4 Set the client's destination IPv4 address (default: "
          "127.0.0.1)\n"
          "  --localhost Listen for heartbeats on localhost (127.0.0.1)\n",
          stream);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0))
    {
        print_usage(stdout);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--version") == 0)
    {
        puts("myc2_beacon " APP_VERSION);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--server") == 0)
    {
        return run_server("127.0.0.1");
    }

    if (argc == 3 && strcmp(argv[1], "--server") == 0 &&
        strcmp(argv[2], "--localhost") == 0)
    {
        return run_server("127.0.0.1");
    }

    if (argc == 4 && strcmp(argv[1], "--server") == 0 &&
        strcmp(argv[2], "--bind") == 0)
    {
        return run_server(argv[3]);
    }

    if (argc == 3 && strcmp(argv[1], "--client") == 0 &&
        strcmp(argv[2], "--localhost") == 0)
    {
        return run_client("127.0.0.1");
    }

    if (argc == 4 && strcmp(argv[1], "--client") == 0 &&
        strcmp(argv[2], "--host") == 0)
    {
        return run_client(argv[3]);
    }

    fputs("Error: unsupported arguments.\n", stderr);
    print_usage(stderr);
    return EXIT_FAILURE;
}
