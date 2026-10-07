#include "cache.h"
#include <chrono>

void Cache::set(const std::string& module, const endpoint& ep) {
    data[module] = ep;
    data[module].timestamp = std::chrono::steady_clock::now();
}

bool Cache::get(const std::string& module, endpoint& ep) {
    auto it = data.find(module);
    if (it == data.end()) return false;
    
    if (is_expired(it->second)) {
        data.erase(it);
        return false;
    }
    
    ep = it->second;
    return true;
}

bool Cache::is_expired(const endpoint& ep) {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - ep.timestamp);
    return age.count() > TTL_SECONDS;
}

void Cache::set_TTL(int TTL){
    TTL_SECONDS = TTL;
}

Cache& get_cache() {
    static Cache instance;
    return instance;
}
void save_endpoint_to_cache(const std::string& name, const std::string& socket_path) {
    Cache& cache = get_cache();
    endpoint ep;
    ep.type = CONNECTION_UDS;
    ep.address = socket_path;
    ep.port = 0;
    ep.timestamp = std::chrono::steady_clock::now();
    cache.set(name, ep);
}