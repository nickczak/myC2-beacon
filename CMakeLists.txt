cmake_minimum_required(VERSION 3.16)
project(check_in_server VERSION 0.1.0 LANGUAGES C CXX)

option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(ENABLE_LINTING "Run clang-tidy on the server and check-in client during builds" ON)

add_executable(c2_server src/server/server.cpp)
target_include_directories(c2_server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(c2_server PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(c2_server PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    if(ENABLE_SANITIZERS)
        target_compile_options(c2_server PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(c2_server PRIVATE -fsanitize=address,undefined)
    endif()
elseif(ENABLE_SANITIZERS)
    message(FATAL_ERROR "ENABLE_SANITIZERS requires GCC or Clang")
endif()

if(ENABLE_LINTING)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "clang-tidy is required; install it or set ENABLE_LINTING=OFF")
    endif()
    # A dependency runs on every build, even when no compilation is needed.
    add_custom_target(lint
        COMMAND "${CLANG_TIDY_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/server/server.cpp"
            --checks=-*,clang-analyzer-*,bugprone-* --warnings-as-errors=*
            -- -std=c++17 "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        COMMAND "${CLANG_TIDY_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/implant/agent.c"
            --checks=-*,clang-analyzer-*,bugprone-* --warnings-as-errors=*
            -- -std=c17 -D_POSIX_C_SOURCE=200809L "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        VERBATIM)
    add_dependencies(c2_server lint)
endif()
