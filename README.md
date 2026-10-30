# myC2-beacon

C17 local learning simulator. It runs a fixed uptime task three times and exits.
It also provides a localhost heartbeat client and server; there is no command execution.

## Requirements

- Linux with a C17 compiler (GCC or Clang)
- CMake 3.16 or newer

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/myc2_beacon --help
./build/myc2_beacon --version
./build/myc2_beacon --simulate
```

The simulation prints its elapsed runtime immediately, waits one second between
iterations, and stops after the third result. Uptime uses a monotonic clock so
changes to the system date do not affect it.

To follow the code in `src/main.c`, start with `main`, which selects the CLI
action. `run_simulation` runs the fixed task and prints results;
`monotonic_seconds` reads elapsed time, and `wait_one_second` pauses the loop.

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer are available with GCC or Clang:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize
./build-sanitize/myc2_beacon --help
```

## Layout

```text
CMakeLists.txt   Build configuration and compiler warnings
src/main.c       CLI entry point, fixed local task, and timing helpers
```

Build outputs are ignored by Git. No external dependencies are required.

## Local heartbeat demo (Linux)

Start the server in one terminal:

```sh
./build/myc2_beacon --server
```

Start the client in another:

```sh
./build/myc2_beacon --client
```

The client sends `HEARTBEAT` every second and prints each `OK` acknowledgment.
The server logs connections, heartbeat acknowledgments, and disconnects or idle
timeouts. Both use localhost port 8080. Stop either process with Ctrl+C.
The client exits with an error if the connection fails or a reply times out.
