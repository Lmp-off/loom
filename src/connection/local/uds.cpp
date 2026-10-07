#include "uds.h"
#include "../../global.h"
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <signal.h>
#include "../../discovery/cache.h"
#include <arpa/inet.h>
#include <netinet/in.h>

namespace uds{

int connect_uds(const std::string& path) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    path.copy(addr.sun_path, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

int create_uds_server(const std::string& path) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    path.copy(addr.sun_path, sizeof(addr.sun_path) - 1);
    
    unlink(path.c_str());
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

void stop_uds_listener() {
    g_running = false;
}

void listener_thread(const std::string& socket_path) {
    int server_sock = create_uds_server(socket_path);
    if (server_sock < 0) return;
    
    while (g_running) {
        int client_fd = accept(server_sock, nullptr, nullptr);
        if (client_fd >= 0) {
            char buffer[4096];
            int n = recv(client_fd, buffer, sizeof(buffer), 0);
            if (n > 0) {
                buffer[n] = '\0';
                std::cout << "получено: " << buffer << std::endl;
            }
            close(client_fd);
        }
    }
    
    close(server_sock);
    unlink(socket_path.c_str());
}

uint8_t send_to_module(const char* module_name, const char* message){
    std::string module_name_str = std::string(module_name);
    Cache& cache = get_cache();
    endpoint ep;
    
    if (!cache.get(module_name_str, ep)) {
        std::cerr << "Модуль не найден в кеше: " << module_name << std::endl;
        return -1;
    }
    
    if (ep.type == CONNECTION_UDS) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "Ошибка создания сокета" << std::endl;
            return -2;
        }
        
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        ep.address.copy(addr.sun_path, sizeof(addr.sun_path) - 1);
        
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Ошибка подключения к " << ep.address << std::endl;
            close(sock);
            return -3;
        }
        
        int result = send(sock, message, strlen(message), 0);
        close(sock);
        return result;
        
    } else if (ep.type == CONNECTION_UDP) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            std::cerr << "Ошибка создания UDP сокета" << std::endl;
            return -4;
        }
        
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ep.port);
        inet_pton(AF_INET, ep.address.c_str(), &addr.sin_addr);
        
        int result = sendto(sock, message, strlen(message), 0, 
                           (struct sockaddr*)&addr, sizeof(addr));
        close(sock);
        return result;
    }
    
    return -5;
}

}