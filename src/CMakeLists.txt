# The layer both halves compile against; declared before them so c2_common
# exists when they link it.
add_subdirectory(common)

add_subdirectory(implant)

add_subdirectory(tests)

# The teamserver pulls in cpp-httplib and OpenSSL; the implant build can skip
# both, which is what the container build does.
if(BUILD_SERVER)
    add_subdirectory(server)
endif()
