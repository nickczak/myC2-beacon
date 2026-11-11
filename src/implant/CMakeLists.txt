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

# The shipped artifact should not be triageable from its own bytes:
#   -s                  drops .symtab, so internal function names are gone
#   -fno-ident          stops GCC recording its version in .comment
#   --build-id=none     drops the linker's build id note
# objcopy then removes what strip cannot: the .comment section the C runtime
# objects contribute, and Red Hat's annobin sections, which record the exact
# compiler build and the flags used ("RV:running gcc 16.1.1 ...").
#
# Skipped for sanitizer builds, where symbols are the point.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT ENABLE_SANITIZERS)
    target_compile_options(agent PRIVATE -fno-ident)
    target_link_options(agent PRIVATE -s -Wl,--build-id=none)

    find_program(OBJCOPY_EXECUTABLE objcopy)
    if(OBJCOPY_EXECUTABLE)
        add_custom_command(TARGET agent POST_BUILD
            COMMAND "${OBJCOPY_EXECUTABLE}"
                    --remove-section=.comment
                    --remove-section=.note.gnu.build-id
                    --remove-section=.annobin.notes
                    --remove-section=.gnu.build.attributes
                    "$<TARGET_FILE:agent>"
            COMMENT "Stripping metadata from agent"
            VERBATIM)
    else()
        message(WARNING "objcopy not found; agent will keep its .comment section")
    endif()
endif()
