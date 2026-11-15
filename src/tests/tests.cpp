// No test framework: two subjects (the wire format and the store's delivery
// rules) and one binary is less machinery than a dependency.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "frame.h"
#include "sessionStore.hpp"

namespace
{
int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char *what, int line)
{
    ++g_checks;
    if (!ok)
    {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

std::string frame_for(std::uint32_t session, std::uint32_t command,
                      const std::string &payload)
{
    std::size_t len = 0;
    char *frame =
        frame_encode(session, command, payload.data(), payload.size(), &len);
    const std::string body(frame, len);
    std::free(frame);
    return body;
}

void test_frame()
{
    const std::string body = frame_for(0x5f3a91c2, CMD_EXECUTE_SHELL, "id -u");
    CHECK(body.size() == 21u);

    C2_HEADER header{};
    CHECK(frame_decode(body.data(), body.size(), &header));
    CHECK(header.magic == CHECK_IN_MAGIC);
    CHECK(header.command_id == CMD_EXECUTE_SHELL);
    CHECK(header.payload_size == 5u);
    CHECK(header.session_id == 0x5f3a91c2u);
    CHECK(std::string(frame_payload(body.data()), 5) == "id -u");

    // Shorter than a header, wrong magic, zero session id, and a payload_size
    // that does not account for the rest of the body are all rejected.
    CHECK(!frame_decode(body.data(), sizeof(C2_HEADER) - 1, &header));

    std::string wrong_magic = body;
    wrong_magic[0] = 'B';
    CHECK(!frame_decode(wrong_magic.data(), wrong_magic.size(), &header));

    const std::string no_session = frame_for(0, CMD_CHECK_IN, "");
    CHECK(!frame_decode(no_session.data(), no_session.size(), &header));

    const std::string clipped = body.substr(0, body.size() - 1);
    CHECK(!frame_decode(clipped.data(), clipped.size(), &header));

    const std::string extended = body + "x";
    CHECK(!frame_decode(extended.data(), extended.size(), &header));

    // A bare header is a valid frame: that is what a check-in sends.
    const std::string bare = frame_for(7, CMD_EXIT, "");
    CHECK(bare.size() == sizeof(C2_HEADER));
    CHECK(frame_decode(bare.data(), bare.size(), &header));
    CHECK(header.payload_size == 0u);
}

void test_delivery()
{
    SessionStore store;
    std::string command;

    store.check_in(1);
    CHECK(store.queue(1, "id -u"));
    CHECK(store.queue(1, "whoami"));

    // One command at a time: the second waits for the first to be answered.
    CHECK(store.take(1, command) == Pending::Command);
    CHECK(command == "id -u");
    CHECK(store.take(1, command) == Pending::Resend);
    CHECK(command == "id -u");

    CHECK(store.acknowledge(1) == "id -u");
    CHECK(store.take(1, command) == Pending::Command);
    CHECK(command == "whoami");
    CHECK(store.acknowledge(1) == "whoami");

    CHECK(store.take(1, command) == Pending::None);
    CHECK(store.acknowledge(1).empty());

    // An unknown id is answered like an empty queue, so asking for work cannot
    // create a session.
    CHECK(store.take(99, command) == Pending::None);
    CHECK(store.snapshot().size() == 1u);
}

void test_unanswered_is_dropped()
{
    SessionStore store;
    std::string command;

    store.check_in(2);
    CHECK(store.queue(2, "sleep 1"));

    CHECK(store.take(2, command) == Pending::Command);
    CHECK(store.take(2, command) == Pending::Resend);
    CHECK(store.take(2, command) == Pending::Resend);
    CHECK(store.take(2, command) == Pending::Dropped);
    CHECK(command == "sleep 1");

    // Dropped, not requeued: it must not come back a fourth time.
    CHECK(store.take(2, command) == Pending::None);
}

void test_exit_is_sticky()
{
    SessionStore store;
    std::string command;

    store.check_in(4);
    store.request_exit(4);
    CHECK(store.take(4, command) == Pending::Exit);
    CHECK(store.take(4, command) == Pending::Exit);

    // An unknown id cannot be given an exit flag.
    store.request_exit(99);
    CHECK(store.snapshot().size() == 1u);
}

void test_quiet_session_is_reaped()
{
    SessionStore store(std::chrono::milliseconds(20));
    std::string command;

    store.check_in(3);
    CHECK(store.queue(3, "id -u"));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // The sweep runs before the lookup. Queueing for a session the sweep has
    // just erased must fail rather than touch the freed node.
    CHECK(!store.queue(3, "whoami"));
    CHECK(store.take(3, command) == Pending::None);
    CHECK(store.snapshot().empty());
}

void test_listing_reaps_too()
{
    SessionStore store(std::chrono::milliseconds(20));

    store.check_in(5);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // A listing is where a dead session is most visible, so it sweeps as well.
    CHECK(store.snapshot().empty());

    // A live session still lists, with its own id.
    store.check_in(6);
    const auto listing = store.snapshot();
    CHECK(listing.size() == 1u);
    CHECK(listing[0].id == 6u);
}

void test_result_keeps_session_alive()
{
    SessionStore store(std::chrono::milliseconds(20));
    std::string command;

    store.check_in(7);
    CHECK(store.queue(7, "sleep 60"));
    CHECK(store.take(7, command) == Pending::Command);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // A result arriving long after the check-in is itself proof of life: the
    // session must not be reaped out from under the reply, or the label and the
    // queued commands go with it.
    CHECK(store.acknowledge(7) == "sleep 60");
    CHECK(store.snapshot().size() == 1u);
}
} // namespace

int main()
{
    test_frame();
    test_delivery();
    test_unanswered_is_dropped();
    test_exit_is_sticky();
    test_quiet_session_is_reaped();
    test_listing_reaps_too();
    test_result_keeps_session_alive();

    if (g_failures == 0)
    {
        std::printf("PASS (%d checks)\n", g_checks);
        return 0;
    }

    std::fprintf(stderr, "FAIL (%d of %d checks)\n", g_failures, g_checks);
    return 1;
}
