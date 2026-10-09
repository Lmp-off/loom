#include "../../src/discovery/cache.h"
#include "../../src/core/server_core_api.h"

int test_connect_all() {
    std::string module_name = "test_target";
    
    // 1. locate возвращает путь
    std::string found = server_api::locate(module_name);
    if (found != "/tmp/loom_test_target.sock") return 1;

    // 2. Сохранение в кеш
    save_endpoint_to_cache(module_name, "/tmp/loom_test_target.sock");
    
    // 3. Получение из кеша
    Cache& cache = get_cache();
    endpoint ep;
    if (!cache.get(module_name, ep)) return 2;
    if (ep.address != "/tmp/loom_test_target.sock") return 3;

    return 0;
}