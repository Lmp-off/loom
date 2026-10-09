#include "gwp.h"
#include <arpa/inet.h>
#include <cstring>

namespace gwp {

void GWPPacket::set_method(const std::string& method) {
    method_name_ = method;
    header_.method_name_len = method.length();
}

void GWPPacket::set_data(const void* data, size_t size) {
    data_.resize(size);
    if (size > 0 && data) {
        memcpy(data_.data(), data, size);
    }
    header_.data_len = size;
}

std::string GWPPacket::get_method() {
    return method_name_;
}

std::vector<uint8_t> GWPPacket::make_package() const {
    size_t total_size = sizeof(GWPHeader) + method_name_.size() + data_.size();
    std::vector<uint8_t> package(total_size);
    
    GWPHeader h = header_;
    h.magic = htonl(h.magic);
    h.packet_id = htonl(h.packet_id);
    h.method_name_len = htonl(h.method_name_len);
    h.data_len = htonl(h.data_len);
    
    memcpy(package.data(), &h, sizeof(GWPHeader));
    
    size_t offset = sizeof(GWPHeader);
    if (!method_name_.empty()) {
        memcpy(package.data() + offset, method_name_.data(), method_name_.size());
        offset += method_name_.size();
    }
    
    if (!data_.empty()) {
        memcpy(package.data() + offset, data_.data(), data_.size());
    }
    
    return package;
}

bool GWPPacket::deserialize(const uint8_t* buffer, size_t len) {
    if (len < sizeof(GWPHeader)) return false;
    
    memcpy(&header_, buffer, sizeof(GWPHeader));
    
    header_.magic = ntohl(header_.magic);
    if (header_.magic != 0x4757505F) return false;
    
    header_.packet_id = ntohl(header_.packet_id);
    header_.method_name_len = ntohl(header_.method_name_len);
    header_.data_len = ntohl(header_.data_len);
    
    size_t expected_size = sizeof(GWPHeader) + header_.method_name_len + header_.data_len;
    if (len < expected_size) return false;
    
    size_t offset = sizeof(GWPHeader);
    
    if (header_.method_name_len > 0) {
        method_name_.assign(reinterpret_cast<const char*>(buffer + offset), header_.method_name_len);
        offset += header_.method_name_len;
    } else {
        method_name_.clear();
    }
    
    if (header_.data_len > 0) {
        data_.resize(header_.data_len);
        memcpy(data_.data(), buffer + offset, header_.data_len);
    } else {
        data_.clear();
    }
    
    return true;
}

}