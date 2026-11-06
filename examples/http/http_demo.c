#include "https.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [CA-certificate.pem]\n", argv[0]);
        return EXIT_FAILURE;
    }
    return POST("https://localhost:8443/heartbeat",
                "{\"message\":\"hello from my lab\"}",
                argc == 2 ? argv[1] : NULL);
}
