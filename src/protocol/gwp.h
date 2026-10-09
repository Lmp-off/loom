#ifndef GWP_H
#define GWP_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace gwp {

enum class PacketType : uint8_t {
    REQUEST = 0,
    RESPONSE = 1,
    ASYNC_DATA = 2,
    HEARTBEAT = 3,
    CLOSE = 4
};

enum class StatusCode : uint8_t {
    OK = 0,
    ERROR = 1,
    METHOD_NOT_FOUND = 2,
    TIMEOUT = 3,
    INVALID_PARAMS = 4
};

struct GWPHeader {
    uint32_t magic;          // 0x4757505F ("GWP_")
    uint32_t packet_id;      // уникальный ID пакета
    PacketType type;         // тип пакета
    uint8_t status;          // статус (для ответов)
    uint32_t method_name_len;// длина имени метода
    uint32_t data_len;       // длина данных
};

class GWPPacket {
public:
    GWPPacket() : header_{0} { 
        header_.magic = 0x4757505F;
    }
    
    void set_type(PacketType type) { header_.type = type; }
    void set_id(uint32_t id) { header_.packet_id = id; }
    void set_status(uint8_t status) { header_.status = status; }
    void set_method(const std::string& method);
    void set_data(const void* data, size_t size);
    
    uint32_t get_id() { return header_.packet_id; }
    PacketType get_type() { return header_.type; }
    uint8_t get_status() { return header_.status; }
    std::string get_method();
    const uint8_t* get_data() { return data_.data(); }
    size_t get_data_size() { return data_.size(); }
    
    std::vector<uint8_t> make_package() const;
    
    bool deserialize(const uint8_t* buffer, size_t len);
    
private:
    GWPHeader header_;
    std::string method_name_;
    std::vector<uint8_t> data_;
};

}

#endif