#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <string>
#include <vector>

namespace transport {

class Transport {
public:
    virtual ~Transport() = default;
    
    virtual void send_response(int fd, const std::string& data) = 0;
    
    virtual int send_request(const std::string& module, const std::string& method, const std::string& data) = 0;
    
    virtual void close(int fd) = 0;
    
    virtual int get_listen_fd() = 0;
};

}

#endif