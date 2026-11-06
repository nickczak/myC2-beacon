cmake_minimum_required(VERSION 3.16)

project(myc2_beacon VERSION 0.1.0 LANGUAGES C CXX)

option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(BUILD_EXAMPLES "Build TCP examples and the HTTPS test server" OFF)

find_package(CURL 7.85 REQUIRED)
add_executable(http_demo examples/http/http_demo.c src/https.cpp)
target_include_directories(http_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(http_demo PRIVATE CURL::libcurl)

set(project_targets http_demo)

if(BUILD_EXAMPLES)
    find_package(Threads REQUIRED)
    find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS SSL Crypto)
    add_executable(myc2_beacon
        examples/main.c
        examples/tcp/client.c
        examples/tcp/server.c
        examples/tcp/net.c
        examples/common/timing.c
    )
    target_include_directories(myc2_beacon PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/tcp"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/common"
    )
    target_link_libraries(myc2_beacon PRIVATE Threads::Threads)
    target_compile_definitions(myc2_beacon PRIVATE APP_VERSION="${PROJECT_VERSION}")

    add_executable(http_test_server examples/http/http_test_server.c)
    target_link_libraries(http_test_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    list(APPEND project_targets myc2_beacon http_test_server)
endif()

foreach(project_target IN LISTS project_targets)
    set_target_properties(${project_target} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND
       CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${project_target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        )
        if(ENABLE_SANITIZERS)
            target_compile_options(${project_target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${project_target} PRIVATE -fsanitize=address,undefined)
        endif()
    elseif(ENABLE_SANITIZERS)
        message(FATAL_ERROR "ENABLE_SANITIZERS requires GCC or Clang")
    endif()
endforeach()
