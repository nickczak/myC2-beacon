#pragma once

#include <atomic>
#include <cstdint>

#include "sessionStore.hpp"

// Read operator commands until `quit` or end of input. Sessions are selected
// here, and every other line is queued for the selected one.
void operator_input(SessionStore &store, std::atomic<std::uint32_t> &selected);
