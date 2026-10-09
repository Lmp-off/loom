#include "../../src/transport/local/uds.h"
#include "../../src/discovery/cache.h"
#include "../../src/global.h"
#include <thread>
#include <chrono>
#include <unistd.h>

int test_send_recv_all() {
    std::string module_name = "test_module";
    std::string socket_path = "/tmp/test_send_recv.sock";

    // 1. Сохраняем в кеш
    save_endpoint_to_cache(module_name, socket_path);

    // 2. Запускаем listener_thread
    std::thread listener(uds::listener_thread, socket_path);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 3. Отправляем сообщение
    const char* test_msg = "Hello from tester!";
    int result = uds::send_to_module(module_name.c_str(), test_msg);
    
    if (result < 0) {
        g_running = false;
        listener.join();
        return 1;
    }

    // 4. Ждем обработки
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 5. Останавливаем listener_thread
    g_running = false;
    listener.join();

    return 0;
}
