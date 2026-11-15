# Builds with or without the server: neither subject needs httplib.
add_executable(c2_tests
    tests.cpp
    "${PROJECT_SOURCE_DIR}/src/server/endpoints/sessionStore.cpp")
set_target_properties(c2_tests PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
target_include_directories(c2_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/common"
    "${PROJECT_SOURCE_DIR}/src/server/endpoints")
target_link_libraries(c2_tests PRIVATE
    c2_common project_warnings Threads::Threads)

add_test(NAME c2_tests COMMAND c2_tests)
