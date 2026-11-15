#pragma once

#include <cstdint>
#include <string>

// The console thread, httplib's request threads and command results all write
// to one stdout. Unserialised writes interleave mid-line -- a log line landing
// inside "c2_server> " -- so every write goes through here.
//
// Nothing in this module parses or decides anything: it only formats.

// One whole message. If the console is sat at a prompt, the message starts on
// a fresh line instead of continuing the prompt.
void write_message(const std::string &text);

// Draw the prompt the next line of input follows.
void draw_prompt(const std::string &prompt);

// Report that the input line was read, so a message arriving before the next
// prompt does not insert a blank line first.
void prompt_answered();

// Session ids print the way `use` and `exit` take them.
std::string hex_id(std::uint32_t id);

// Implant output is arbitrary bytes. Everything below 0x20 except tab and
// newline is rendered as ^X, and 0x7f as ^?, so a command cannot rewrite the
// operator's terminal. Bytes 0x80-0xff are left alone, which keeps UTF-8 intact.
std::string escape_control(const std::string &data);
