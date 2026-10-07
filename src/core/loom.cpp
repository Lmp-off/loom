#include "../../include/loom.h"
#include "../discovery/cache.h"

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_map>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "server_core_api.h"
#include "../connection/local/uds.h"


static std::atomic<bool> g_running(true);
static std::string g_name;
/*
TODO
Добавить буферизацию при отправке сообщений чтобы формировать большой пакет вместо множества мелких
автоскейлинг количества процессов обработки у модуля чтобы рагилировать пропускную способность (ограничить количество экземпляров от греха подальше)
Добавить буферизацию на вход чтобы раскидывать задачи по исполняющим процессам
трассировка с последующей отправкой Server Core
подумать как при отправке данных по сокетам использовать SHARED MEMORY вместо КОПИРОВАНИЯ (для больших файлов можно записывать в файл и передавать fd через сокет и блокировать пока не получим что процесс Б прочитал)

что делать с версиями

*/

uint8_t start(const char* module_name) {
    g_name = module_name;
    std::cout << "модуль " << g_name << " запущен" << std::endl;
    
    std::string socket_path = "/tmp/loom_" + g_name + ".sock";
    std::thread open_uds(uds::listener_thread, socket_path);
    open_uds.detach();
    
    return 0;
}

uint8_t stop() {
    std::cout << "остановка модуля " << g_name << std::endl;
    g_running = false;
    return 0;
}

uint8_t use_module_direct(const char* module_name) {
    std::cout << "идет коннект к модулю " << module_name << "\n";
    
    std::string socket_path = server_api::locate(std::string(module_name));
    if (socket_path.empty()) {
        std::cerr << "не удалось найти модуль " << module_name << std::endl;
        return 1;
    }
    
    save_endpoint_to_cache(module_name, socket_path);
    return 0;
}

uint8_t use_module(const char* description, const char* as_name) {
    std::cout << "поиск по описанию: " << description << "\nсохранить как: " << as_name << "\n";
    
    std::string socket_path = server_api::locate_by_description(description);
    if (socket_path.empty()) return 1;
    
    save_endpoint_to_cache(as_name, socket_path);
    return 0;
}

void module_send(const char* module_name, const char* message){
    uds::send_to_module(module_name,message);
}