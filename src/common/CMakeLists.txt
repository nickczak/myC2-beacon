# The shared layer: the wire format both halves compile against. Plain C so the
# implant can link it; the server wraps the same functions in C++.
add_library(c2_common STATIC frame.c)
set_target_properties(c2_common PROPERTIES
    C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_include_directories(c2_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(c2_common PRIVATE project_warnings)
