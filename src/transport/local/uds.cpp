#include "uds.h"
#include "../../global.h"
#include "../../../metalogger/metalogger.h"
#include "../../discovery/cache.h"
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>

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

int create_uds_socket(const std::string& path) {
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
    METALOG_INFO(std::string("[UDS] Слушаем на ") + socket_path);
    
    int sock = create_uds_socket(socket_path);
    if (sock < 0) {
        METALOG_ERROR(std::string("[UDS] Не удалось создать сокет на ") + socket_path);
        return;
    }
    
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (g_running) {
        int client_fd = accept(sock, nullptr, nullptr);
        if (client_fd >= 0) {
            char buffer[4096];
            int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                std::string msg = buffer;
                
                g_last_message = msg;
                g_last_receiver = socket_path;
                g_message_received = true;
                
                METALOG_INFO(std::string("[UDS] Принято от ") + socket_path);
                METALOG_INFO(std::string("[UDS] Сообщение: ") + msg);
                METALOG_INFO(std::string("[UDS] Размер: ") + std::to_string(n) + " байт");
            } else if (n == 0) {
                METALOG_DEBUG("[UDS] Соединение закрыто клиентом");
            }
            close(client_fd);
        }
    }
    
    close(sock);
    unlink(socket_path.c_str());
    METALOG_INFO(std::string("[UDS] Сокет ") + socket_path + " закрыт");
}

uint8_t send_to_module(const char* module_name, const char* message){
    std::string module_name_str = std::string(module_name);
    METALOG_INFO(std::string("[UDS] Отправка модулю ") + module_name_str);
    METALOG_INFO(std::string("[UDS] Сообщение: ") + message);
    
    Cache& cache = get_cache();
    endpoint ep;
    
    if (!cache.get(module_name_str, ep)) {
        METALOG_ERROR(std::string("[UDS] Модуль не найден в кеше: ") + module_name);
        return -1;
    }
    
    if (ep.type == CONNECTION_UDS) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            METALOG_ERROR("[UDS] Ошибка создания сокета");
            return -2;
        }
        
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        ep.address.copy(addr.sun_path, sizeof(addr.sun_path) - 1);
        
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            METALOG_ERROR(std::string("[UDS] Ошибка подключения к ") + ep.address);
            close(sock);
            return -3;
        }
        
        int result = send(sock, message, strlen(message), 0);
        METALOG_INFO(std::string("[UDS] Отправлено ") + std::to_string(result) + " байт");
        close(sock);
        return result;
        
    } else if (ep.type == CONNECTION_UDP) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            METALOG_ERROR("[UDS] Ошибка создания UDP сокета");
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
