#include "serverOutput.hpp"

#include <iostream>
#include <mutex>
#include <sstream>

namespace
{
std::mutex g_output_mutex;

// Whether a prompt is on screen with no newline after it yet.
bool g_prompt_pending = false;
} // namespace

void write_message(const std::string &text)
{
    std::lock_guard<std::mutex> guard(g_output_mutex);

    if (g_prompt_pending)
    {
        std::cout << '\n';
        g_prompt_pending = false;
    }

    std::cout << text;
    if (text.empty() || text.back() != '\n')
        std::cout << '\n';
    std::cout << std::flush;
}

void draw_prompt(const std::string &prompt)
{
    std::lock_guard<std::mutex> guard(g_output_mutex);

    std::cout << prompt << std::flush;
    g_prompt_pending = true;
}

void prompt_answered()
{
    std::lock_guard<std::mutex> guard(g_output_mutex);
    g_prompt_pending = false;
}

std::string hex_id(std::uint32_t id)
{
    std::ostringstream out;
    out << std::hex << id;
    return out.str();
}

std::string escape_control(const std::string &data)
{
    std::string out;
    out.reserve(data.size());

    for (const char ch : data)
    {
        const unsigned char byte = static_cast<unsigned char>(ch);

        if (byte < 0x20 && byte != '\n' && byte != '\t')
        {
            out += '^';
            out += static_cast<char>(byte ^ 0x40);
        }
        else if (byte == 0x7f)
        {
            out += "^?";
        }
        else
        {
            out += ch;
        }
    }
    return out;
}
