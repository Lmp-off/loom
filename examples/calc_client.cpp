#include <loom.h>

#include <cstdio>
#include <exception>
#include <string>

int main() {
    if (start("calc_client") != 0) {
        std::fprintf(stderr, "failed to start calc_client\n");
        return 1;
    }

    // Необязательно: loom_call сам спросит server_api::locate при промахе.
    use_module_direct("calc_server");

    try {
        const int sum = loom::call<int>("calc_server", "add", 2, 3);
        std::printf("[calc_client] add(2, 3) = %d\n", sum);

        const std::string greeting =
            loom::call<std::string>("calc_server", "greet", "loom");
        std::printf("[calc_client] greet(\"loom\") = %s\n", greeting.c_str());

        // Односторонняя отправка: ответ не ожидается.
        module_send("calc_server", "hello from calc_client");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[calc_client] rpc failed: %s\n", e.what());
        stop();
        return 1;
    }

    stop();
    std::printf("[calc_client] stopped\n");
    return 0;
}
