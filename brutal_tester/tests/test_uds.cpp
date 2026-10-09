#include "../../src/transport/local/uds.h"
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <sys/socket.h>

int test_uds_all() {
    std::string socket_path = "/tmp/test_uds.sock";

    int server_fd = uds::create_uds_socket(socket_path);
    if (server_fd < 0) return 1;

    int client_fd = uds::connect_uds(socket_path);
    if (client_fd < 0) {
        close(server_fd);
        return 2;
    }

    const char* msg = "hello";
    ssize_t sent = send(client_fd, msg, strlen(msg), 0);
    if (sent != (ssize_t)strlen(msg)) {
        close(client_fd);
        close(server_fd);
        return 3;
    }

    char buffer[256];
    int client2 = accept(server_fd, nullptr, nullptr);
    if (client2 < 0) {
        close(client_fd);
        close(server_fd);
        return 4;
    }

    ssize_t recvd = recv(client2, buffer, sizeof(buffer), 0);
    if (recvd != sent) {
        close(client2);
        close(client_fd);
        close(server_fd);
        return 5;
    }
    buffer[recvd] = '\0';
    if (strcmp(buffer, msg) != 0) {
        close(client2);
        close(client_fd);
        close(server_fd);
        return 6;
    }

    close(client2);
    close(client_fd);
    close(server_fd);
    unlink(socket_path.c_str());

    return 0;
}
