#include <httplib.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "beaconEndpoints.hpp"
#include "operatorConsole.hpp"
#include "serverOutput.hpp"
#include "sessionStore.hpp"

namespace
{
constexpr const char *kCertPath = "certs/server.pem";
constexpr const char *kKeyPath = "certs/server.key";

// Report each request so beacons are visible in the console. Called from
// httplib's connection threads.
void log_request(const httplib::Request &req, const httplib::Response &res)
{
    write_message("[*] " + req.method + " " + req.path + " -> " +
                  std::to_string(res.status));
}
} // namespace

int main()
{
    // The console thread reads the store and the selection, so they cannot live
    // on this frame: a detached thread outliving main() is a dangling read.
    struct ConsoleState
    {
        SessionStore store;
        std::atomic<std::uint32_t> selected{0};
    };
    const auto console = std::make_shared<ConsoleState>();

    httplib::SSLServer server(kCertPath, kKeyPath);
    if (!server.is_valid())
    {
        // Before any thread exists, so this needs no serialising.
        std::cerr << "[-] Failed to load " << kCertPath << " / " << kKeyPath
                  << "\n";
        return 1;
    }

    server.set_payload_max_length(kMaxPayload);
    server.set_logger(log_request);
    register_all_beacon_endpoints(server, console->store);

    // Bind before the console starts: a port already in use fails here, while
    // there is still no thread to leave blocked on stdin.
    if (!server.bind_to_port("0.0.0.0", kPort))
    {
        std::cerr << "[-] Failed to bind 0.0.0.0:" << kPort << "\n";
        return 1;
    }

    // Quit ends the console thread, not the teamserver: listen keeps serving
    // until the process is stopped.
    std::thread input_thread([console] {
        (void)operator_input(console->store, console->selected);
    });
    input_thread.detach();

    write_message("[*] Linux C++ Teamserver listening on https://0.0.0.0:" +
                  std::to_string(kPort));

    if (!server.listen_after_bind())
    {
        std::cerr << "[-] Failed to listen on port " << kPort << "\n";
        return 1;
    }
    return 0;
}
