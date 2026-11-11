add_executable(server server.cpp)
set_target_properties(server PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_link_libraries(server PRIVATE
    c2_common project_warnings httplib::httplib Threads::Threads)

# The endpoint implementations add the rest of the server's sources.
add_subdirectory(endpoints)

# Recreate the self-signed TLS certificate on a clean clone (certs/ is
# gitignored, so a fresh checkout has none). The server loads certs/server.pem
# and certs/server.key relative to its working directory, so they are generated
# into the source tree, and only when at least one of them is missing.
find_program(OPENSSL_EXECUTABLE openssl)
if(NOT OPENSSL_EXECUTABLE)
    message(FATAL_ERROR "openssl not found; required to generate certs/server.pem")
endif()
set(CERT_DIR "${PROJECT_SOURCE_DIR}/certs")
set(CERT_FILE "${CERT_DIR}/server.pem")
set(KEY_FILE "${CERT_DIR}/server.key")

# The certificate has to name the address a victim actually connects to, or
# verification fails on the remote side. Set these when deploying off-loopback:
#   cmake -S . -B build -DSERVER_IP=192.168.50.50
set(SERVER_IP "" CACHE STRING "Extra IP the certificate is valid for")
set(SERVER_HOST "" CACHE STRING "Extra DNS name the certificate is valid for")
set(CERT_SAN "DNS:localhost,IP:127.0.0.1")
if(SERVER_HOST)
    string(APPEND CERT_SAN ",DNS:${SERVER_HOST}")
endif()
if(SERVER_IP)
    string(APPEND CERT_SAN ",IP:${SERVER_IP}")
endif()
# Rewritten only when the SAN string changes, so reconfiguring without changing
# it does not force the certificate to be regenerated.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cert_san.txt.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cert_san.txt" @ONLY)

add_custom_command(
    OUTPUT "${CERT_FILE}" "${KEY_FILE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CERT_DIR}"
    COMMAND "${OPENSSL_EXECUTABLE}" req -x509 -newkey rsa:2048 -nodes
            -keyout "${KEY_FILE}" -out "${CERT_FILE}"
            -days 365 -subj "/CN=localhost"
            -addext "subjectAltName=${CERT_SAN}"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/cert_san.txt"
    COMMENT "Generating self-signed TLS certificate for ${CERT_SAN}"
    VERBATIM)
add_custom_target(server_certs DEPENDS "${CERT_FILE}" "${KEY_FILE}")
add_dependencies(server server_certs)
