# Compiled into the server target declared one directory up.
target_sources(server PRIVATE
    beaconEndpoints.cpp
    beaconEndpoints.hpp
    sessionStore.cpp
    sessionStore.hpp)
target_include_directories(server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
