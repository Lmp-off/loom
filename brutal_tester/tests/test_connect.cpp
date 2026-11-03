#include "../../src/discovery/cache.h"
#include "../../src/core/server_core_api.h"
#include "../../src/transport/handshake.hpp"

int test_connect_all() {
    server_api::clear_registry();

    std::string module_name = "test_target";

    // 1. locate возвращает путь (fallback старой заглушки)
    std::string found = server_api::locate(module_name);
    if (found != "/tmp/loom_test_target.sock") return 1;

    // 2. Сохранение в кеш
    save_endpoint_to_cache(module_name, "/tmp/loom_test_target.sock");
    
    // 3. Получение из кеша
    Cache& cache = get_cache();
    endpoint ep;
    if (!cache.get(module_name, ep)) return 2;
    if (ep.address != "/tmp/loom_test_target.sock") return 3;

    // 4. Имитация ядра: регистрация, priority, поиск, снятие.
    server_api::register_module("core_mod", "/tmp/core_mod.sock");
    server_api::ModuleInfo info;
    if (!server_api::locate_module("core_mod", info)) return 4;
    if (info.address != "/tmp/core_mod.sock") return 5;
    if (info.priority) return 6;

    server_api::set_priority("core_mod", true);
    if (!server_api::is_priority("core_mod")) return 7;
    if (!server_api::locate_module("core_mod", info)) return 8;
    if (!info.priority) return 9;

    server_api::unregister_module("core_mod");
    if (!server_api::locate_module("core_mod", info)) return 10;
    if (info.address != "/tmp/loom_core_mod.sock") return 11;

    // 5. Handshake несёт priority-флаг.
    const auto handshake = loom_transport::make_handshake("core_mod", true);
    std::string name;
    bool priority = false;
    if (!loom_transport::parse_handshake(handshake.data(), handshake.size(),
                                         name, &priority)) {
        return 12;
    }
    if (name != "core_mod") return 13;
    if (!priority) return 14;

    const auto plain = loom_transport::make_handshake("core_mod", false);
    priority = true;
    if (!loom_transport::parse_handshake(plain.data(), plain.size(), name,
                                         &priority)) {
        return 15;
    }
    if (priority) return 16;

    server_api::clear_registry();
    return 0;
}
