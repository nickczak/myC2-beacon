cmake_minimum_required(VERSION 3.16)
project(check_in_server VERSION 0.1.0 LANGUAGES C CXX)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

option(BUILD_SERVER "Build the teamserver (fetches cpp-httplib on first configure)" ON)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

if(BUILD_SERVER)
# cpp-httplib v0.66.0 (tested commit cdcd2ac6e6d5548b5a9130fb39ab76d2c6c614b0),
# fetched into build/_deps rather than committed. Override for an offline or
# locally patched copy with -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=<path>.
include(FetchContent)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
    GIT_TAG v0.66.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)

# Fail the configure instead of silently building a server with no HTTPS.
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
# Consumed as a library, not installed as a system package.
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)
endif()

# Targets are declared in subdirectories, but keep the binaries at
# build/server and build/agent.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Warning flags shared by every target in the project.
add_library(project_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(ENABLE_SANITIZERS)
        # Fail at configure if the sanitizer runtime cannot be linked, rather
        # than with an opaque "cannot find libasan.so" from the linker.
        include(CheckCSourceCompiles)
        set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
        check_c_source_compiles("int main(void) { return 0; }"
                                HAVE_SANITIZER_RUNTIME)
        unset(CMAKE_REQUIRED_FLAGS)
        if(NOT HAVE_SANITIZER_RUNTIME)
            message(FATAL_ERROR
                "ENABLE_SANITIZERS=ON but the compiler cannot link "
                "-fsanitize=address,undefined. Install the sanitizer runtime "
                "(libasan/libubsan on Fedora) and reconfigure.")
        endif()
        target_compile_options(project_warnings INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(project_warnings INTERFACE
            -fsanitize=address,undefined)
    endif()
elseif(ENABLE_SANITIZERS)
    message(FATAL_ERROR "ENABLE_SANITIZERS requires GCC or Clang")
endif()

add_subdirectory(src)
