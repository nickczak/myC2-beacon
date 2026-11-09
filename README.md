<p align="center">
  <img src="assets/myC2_logo.png" alt="myC2 logo" width="491">
</p>

<h4 align="center">A Micro Stealth C2 Framework built in C/C++</h4>

---

### Description 
myC2 is a small C2 framework aimed at stealthy operations. It is designed to be small, fast, and efficient. 
### Features
- Coming soon...

# Design Goals

# Stealth

### The Implant

### The Server

### The Client

# Disclaimer

## Local check-in demo

The current server and client perform a single localhost check-in and then exit.
They do not execute commands. The three 32-bit header fields use network byte
order; clients sending the earlier native-byte-order header need updating.

Build the server (requires CMake, a C++17 compiler, and clang-tidy):

```sh
cmake -S . -B build
cmake --build build
./build/c2_server
```

`c2_server` is the only executable target. Its `lint` dependency runs clang-tidy
on both `src/server/server.cpp` and `src/implant/agent.c` on every build, even
when the executable is up to date. GCC/Clang compiler warnings and clang-tidy
findings fail the build. To explicitly disable linting, configure with
`-DENABLE_LINTING=OFF`. Formatting preferences in `.clang-format` are separate;
formatting is not enforced by this build.

In another terminal, compile and run the one-shot Linux check-in client:

```sh
cc -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -Iinclude src/implant/agent.c -o /tmp/check-in-client
/tmp/check-in-client
```

The server listens on `127.0.0.1:8080`, accepts one connection, validates its
header, prints `Check-in received`, and exits. Restart it for each test.
A connected client has a five-second receive idle timeout.
