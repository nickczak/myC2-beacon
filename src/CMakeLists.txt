# Headers shared by both halves of the framework (protocol.h lives here).
add_library(c2_common INTERFACE)
target_include_directories(c2_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(implant)

# The teamserver pulls in cpp-httplib and OpenSSL; the implant build can skip
# both, which is what the container build does.
if(BUILD_SERVER)
    add_subdirectory(server)
endif()
