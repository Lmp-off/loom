#ifndef CACHE_H
#define CACHE_H


#include <string>
#include <chrono>
#include "map"

enum connection_type{
    CONNECTION_UDP,
    CONNECTION_UDS,
};

struct endpoint {
    connection_type type;
    std::string address;      // "/tmp/module.sock" или "192.168.1.2"
    int port;
    std::chrono::steady_clock::time_point timestamp;
};

class Cache {
public:
    void set_TTL(int TTL);
    void set(const std::string& module, const endpoint& ep);
    bool get(const std::string& module, endpoint& ep);
    int  get_ttl() const;
    bool is_expired(const endpoint& ep);
    void remove(const std::string& module);
    void clear();
    
private:
    std::map<std::string, endpoint> data;
    int TTL_SECONDS = 60;
};

Cache& get_cache();
void save_endpoint_to_cache(const std::string& name, const std::string& socket_path);


#endif