#pragma once
#include <string>
#include <vector>
#include "../gateway/TDB/buffer/buffer.hpp"

class TDB;

//TODO: replace TDB by receiver interface so we can easily change block 

class Transport {
public:
    virtual ~Transport() = default;

    virtual bool start() = 0;
    virtual void stop()  = 0;

    void set_tdb(TDB* tdb) { tdb_ = tdb; }

    virtual int send_request(const std::string& module,
                             const std::string& method,
                             const std::string& data) = 0;

    virtual int send_response(int fd, const std::vector<uint8_t>& bytes) = 0;

    virtual int get_listen_fd() const = 0;
    virtual int type() const = 0;

protected:
    TDB* tdb_ = nullptr;
};