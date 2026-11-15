#include "operatorConsole.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "serverOutput.hpp"

namespace
{
constexpr const char *kPrompt = "c2_server> ";

// Read a session id the way the console writes it (hex). Zero is rejected
// because the protocol reserves it for "no session".
bool parse_session_id(const std::string &text, std::uint32_t &id)
{
    if (text.empty())
        return false;

    char *end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    if (end == text.c_str() || *end != '\0' ||
        value > static_cast<unsigned long>(UINT32_MAX))
        return false;

    id = static_cast<std::uint32_t>(value);
    return id != 0;
}

// List every session the teamserver has seen, marking the one commands go to.
// The age is what tells a live implant from one that has stopped beaconing.
void print_sessions(const std::vector<SessionInfo> &sessions,
                    std::uint32_t selected)
{
    if (sessions.empty())
    {
        write_message("[*] no sessions yet");
        return;
    }

    std::string listing = "[*] " + std::to_string(sessions.size()) +
                          " session(s), * = receives commands";
    for (const SessionInfo &info : sessions)
    {
        listing += "\n  ";
        listing += info.id == selected ? "* " : "  ";
        listing += hex_id(info.id);
        listing += "  ";
        listing += std::to_string(info.quiet.count() / 1000);
        listing += "s ago";
        if (info.busy)
            listing += "  (command out, no result yet)";
    }
    write_message(listing);
}

// The verbs. Each returns true to keep reading input, false to stop.
bool do_sessions(SessionStore &store, const std::atomic<std::uint32_t> &selected)
{
    print_sessions(store.snapshot(), selected.load());
    return true;
}

bool do_use(SessionStore &store, std::atomic<std::uint32_t> &selected,
            const std::string &argument)
{
    std::uint32_t id = 0;
    if (!parse_session_id(argument, id))
    {
        write_message("[-] use <hex id>; run `sessions` for the ids");
        return true;
    }

    // A typo would otherwise select a session that can never collect anything.
    bool known = false;
    for (const SessionInfo &info : store.snapshot())
        known = known || info.id == id;

    if (!known)
    {
        write_message("[-] no session " + hex_id(id) + "; run `sessions`");
        return true;
    }

    selected = id;
    write_message("[*] using " + hex_id(id));
    return true;
}

// `exit` follows the selection; reaching every session takes an explicit
// `exit all`, so one stray line cannot take a whole engagement down.
bool do_exit(SessionStore &store, const std::atomic<std::uint32_t> &selected,
             const std::string &argument)
{
    if (argument == "all")
    {
        const std::vector<SessionInfo> sessions = store.snapshot();
        for (const SessionInfo &info : sessions)
            store.request_exit(info.id);
        write_message("[*] asked " + std::to_string(sessions.size()) +
                      " session(s) to exit");
        return true;
    }

    std::uint32_t id = selected.load();
    if (!argument.empty() && !parse_session_id(argument, id))
    {
        write_message("[-] exit, exit <hex id> or exit all");
        return true;
    }

    if (id == 0)
    {
        write_message("[-] no session selected; run `sessions` then `use <id>`");
        return true;
    }

    store.request_exit(id);
    write_message("[*] asked " + hex_id(id) + " to exit");
    return true;
}

bool do_command(SessionStore &store, std::uint32_t selected,
                const std::string &line)
{
    if (selected == 0)
    {
        write_message("[-] no session selected; run `sessions` then `use <id>`");
        return true;
    }

    if (!store.queue(selected, line))
        write_message("[-] session " + hex_id(selected) + " is gone");
    return true;
}

// Split a line into verb and argument, then route it.
bool dispatch(SessionStore &store, std::atomic<std::uint32_t> &selected,
              const std::string &line)
{
    const std::size_t space = line.find(' ');
    const std::string verb = line.substr(0, space);
    const std::string argument =
        space == std::string::npos ? std::string() : line.substr(space + 1);

    if (verb == "sessions")
        return do_sessions(store, selected);
    if (verb == "use")
        return do_use(store, selected, argument);
    if (verb == "exit")
        return do_exit(store, selected, argument);
    // Quit ends the prompt, not the teamserver: listen keeps serving.
    if (verb == "quit")
        return false;

    return do_command(store, selected.load(), line);
}
} // namespace

void operator_input(SessionStore &store, std::atomic<std::uint32_t> &selected)
{
    std::string line;

    for (;;)
    {
        draw_prompt(kPrompt);
        if (!std::getline(std::cin, line))
            return;
        prompt_answered();

        if (line.empty())
            continue;
        if (!dispatch(store, selected, line))
            return;
    }
}
