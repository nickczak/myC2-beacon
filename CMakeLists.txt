cmake_minimum_required(VERSION 3.16)

project(myc2_beacon VERSION 0.1.0 LANGUAGES C)

option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

find_package(Threads REQUIRED)

add_executable(myc2_beacon src/main.c)
target_link_libraries(myc2_beacon PRIVATE Threads::Threads)
set_target_properties(myc2_beacon PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
target_compile_definitions(myc2_beacon PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(myc2_beacon PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
    if(ENABLE_SANITIZERS)
        target_compile_options(myc2_beacon PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer
        )
        target_link_options(myc2_beacon PRIVATE -fsanitize=address,undefined)
    endif()
elseif(ENABLE_SANITIZERS)
    message(FATAL_ERROR "ENABLE_SANITIZERS requires GCC or Clang")
endif()
