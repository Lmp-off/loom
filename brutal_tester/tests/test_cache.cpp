#include "../../src/discovery/cache.h"
#include <chrono>

int test_cache_all() {
    Cache cache;
    endpoint ep;
    ep.type = CONNECTION_UDS;
    ep.address = "/tmp/test.sock";
    ep.port = 0;
    ep.timestamp = std::chrono::steady_clock::now();

    // set/get
    cache.set("test_module", ep);
    endpoint out;
    if (!cache.get("test_module", out)) return 1;
    if (out.address != "/tmp/test.sock") return 2;
    if (out.type != CONNECTION_UDS) return 3;

    // miss
    if (cache.get("non_existent", out)) return 4;

    // update
    ep.address = "/tmp/new.sock";
    cache.set("test_module", ep);
    if (!cache.get("test_module", out)) return 5;
    if (out.address != "/tmp/new.sock") return 6;

    return 0;
}
