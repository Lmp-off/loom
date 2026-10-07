#ifndef UDS_H
#define UDS_H

#include <string>
#include <vector>
#include <cstdint>

namespace uds{
    int connect_uds(const std::string& path);
    int create_uds_server(const std::string& path);
    void listener_thread(const std::string& socket_path);
    void stop_uds_listener();
    uint8_t send_to_module(const char* module_name, const char* message);
}
#endif