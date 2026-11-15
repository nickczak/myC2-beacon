#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// One implant, identified by the session_id it stamps on every frame.
struct Session
{
    std::deque<std::string> pending;
    std::string in_flight; // served, not yet acknowledged by a result
    bool has_in_flight = false;
    unsigned served_count = 0;
    bool exit_requested = false;
    std::chrono::steady_clock::time_point last_seen;
};

// A session as the console lists it.
struct SessionInfo
{
    std::uint32_t id = 0;
    std::chrono::milliseconds quiet{0}; // since the last sign of life
    bool busy = false;                  // a command is out, no result yet
};

// What draining a session's queue on a check-in produced.
enum class Pending
{
    None,
    Command, // a queued command, now in flight
    Resend,  // the command already in flight, sent again
    Dropped, // in flight and unanswered too many times; discard it
    Exit
};

class SessionStore
{
public:
    // The TTL is a parameter so a test can reap without waiting a minute.
    explicit SessionStore(
        std::chrono::milliseconds ttl = std::chrono::milliseconds(60000));

    void check_in(std::uint32_t id);
    Pending take(std::uint32_t id, std::string &command);

    // A result upload proves the implant is alive and closes the command it was
    // served. Returns that command, or "" when nothing was in flight.
    std::string acknowledge(std::uint32_t id);

    bool queue(std::uint32_t id, const std::string &command);
    void request_exit(std::uint32_t id);

    // Non-const on purpose: listing is what drops the sessions that went quiet.
    std::vector<SessionInfo> snapshot();

private:
    std::size_t reap_locked();

    mutable std::mutex mutex_;
    std::map<std::uint32_t, Session> sessions_;
    std::chrono::milliseconds ttl_;
};
