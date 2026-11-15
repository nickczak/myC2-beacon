#include "sessionStore.hpp"

#include <utility>

namespace
{
// A command is sent at most this many times before it is dropped. A check-in
// that follows an unanswered one means the result upload never arrived.
constexpr unsigned kMaxSendAttempts = 3;
} // namespace

SessionStore::SessionStore(std::chrono::milliseconds ttl) : ttl_(ttl) {}

std::size_t SessionStore::reap_locked()
{
    const auto now = std::chrono::steady_clock::now();
    std::size_t removed = 0;

    for (auto it = sessions_.begin(); it != sessions_.end();)
    {
        if (now - it->second.last_seen > ttl_)
        {
            it = sessions_.erase(it);
            ++removed;
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

void SessionStore::check_in(std::uint32_t id)
{
    std::lock_guard<std::mutex> guard(mutex_);

    // Sweeping here rather than on a timer: every beacon is a chance to drop
    // the sessions that went silent, so no extra thread is needed.
    reap_locked();

    sessions_[id].last_seen = std::chrono::steady_clock::now();
}

Pending SessionStore::take(std::uint32_t id, std::string &command)
{
    std::lock_guard<std::mutex> guard(mutex_);

    // Sweep before the lookup, never after: the sweep may erase the very
    // session this call is about, and an iterator into an erased node dangles.
    reap_locked();

    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return Pending::None;

    Session &session = it->second;
    if (session.exit_requested)
        return Pending::Exit;

    if (session.has_in_flight)
    {
        command = session.in_flight;
        if (session.served_count >= kMaxSendAttempts)
        {
            session.in_flight.clear();
            session.has_in_flight = false;
            session.served_count = 0;
            return Pending::Dropped;
        }

        ++session.served_count;
        return Pending::Resend;
    }

    if (session.pending.empty())
        return Pending::None;

    session.in_flight = std::move(session.pending.front());
    session.pending.pop_front();
    session.has_in_flight = true;
    session.served_count = 1;
    command = session.in_flight;
    return Pending::Command;
}

std::string SessionStore::acknowledge(std::uint32_t id)
{
    std::lock_guard<std::mutex> guard(mutex_);

    // Deliberately no sweep: the upload is itself proof of life, and a command
    // that ran for minutes must not lose its session before its output lands.
    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return {};

    it->second.last_seen = std::chrono::steady_clock::now();

    std::string command = std::move(it->second.in_flight);
    it->second.in_flight.clear();
    it->second.has_in_flight = false;
    it->second.served_count = 0;
    return command;
}

bool SessionStore::queue(std::uint32_t id, const std::string &command)
{
    std::lock_guard<std::mutex> guard(mutex_);
    reap_locked();

    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return false;

    it->second.pending.push_back(command);
    return true;
}

void SessionStore::request_exit(std::uint32_t id)
{
    std::lock_guard<std::mutex> guard(mutex_);
    reap_locked();

    auto it = sessions_.find(id);
    if (it != sessions_.end())
        it->second.exit_requested = true;
}

std::vector<SessionInfo> SessionStore::snapshot()
{
    std::lock_guard<std::mutex> guard(mutex_);

    // The listing is where a dead session shows up, so it sweeps too.
    reap_locked();

    const auto now = std::chrono::steady_clock::now();
    std::vector<SessionInfo> out;
    out.reserve(sessions_.size());
    for (const auto &entry : sessions_)
    {
        SessionInfo info;
        info.id = entry.first;
        info.quiet = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - entry.second.last_seen);
        info.busy = entry.second.has_in_flight;
        out.push_back(info);
    }
    return out;
}
