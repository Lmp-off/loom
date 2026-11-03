#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Транспортный handshake для UDS.
//
// Архитектурные инварианты:
// - GWP не содержит имени отправителя, поэтому имя модуля передаётся
//   отдельным первым кадром при открытии UDS-соединения;
// - принимающая сторона хранит имя на всё время жизни соединения и кладёт
//   его в IncomingPacket::client_address;
// - priority-флаг разрешает direct-путь: priority Buffer UPLINK и
//   выделенный Worker на принимающей стороне;
// - handshake — часть транспорта, воркер его не видит.
//
// Формат: [uint32 magic][uint32 flags][uint32 name_len][name_len bytes].
// flags: HANDSHAKE_FLAG_PRIORITY — соединение помечено как direct/priority.

namespace loom_transport {

constexpr uint32_t HANDSHAKE_MAGIC = 0x4C4D4B48u; // "LMKH"
constexpr uint32_t HANDSHAKE_FLAG_PRIORITY = 1u << 0;
constexpr uint32_t MAX_MODULE_NAME = 4096;
constexpr size_t HANDSHAKE_HEADER_SIZE = 12;

inline std::vector<uint8_t> make_handshake(const std::string& module,
                                           bool priority = false) {
    const uint32_t magic = HANDSHAKE_MAGIC;
    const uint32_t flags = priority ? HANDSHAKE_FLAG_PRIORITY : 0u;
    const uint32_t name_len = static_cast<uint32_t>(module.size());

    std::vector<uint8_t> frame(HANDSHAKE_HEADER_SIZE + module.size());
    std::memcpy(frame.data(), &magic, sizeof(magic));
    std::memcpy(frame.data() + 4, &flags, sizeof(flags));
    std::memcpy(frame.data() + 8, &name_len, sizeof(name_len));
    if (!module.empty()) {
        std::memcpy(frame.data() + HANDSHAKE_HEADER_SIZE, module.data(),
                    module.size());
    }
    return frame;
}

inline bool parse_handshake(const uint8_t* data, size_t size,
                            std::string& module,
                            bool* priority = nullptr) {
    if (data == nullptr || size < HANDSHAKE_HEADER_SIZE) return false;

    uint32_t magic = 0;
    uint32_t flags = 0;
    uint32_t name_len = 0;
    std::memcpy(&magic, data, sizeof(magic));
    std::memcpy(&flags, data + 4, sizeof(flags));
    std::memcpy(&name_len, data + 8, sizeof(name_len));

    if (magic != HANDSHAKE_MAGIC) return false;
    if (name_len > MAX_MODULE_NAME) return false;
    if (size < HANDSHAKE_HEADER_SIZE + static_cast<size_t>(name_len)) {
        return false;
    }

    module.assign(reinterpret_cast<const char*>(data + HANDSHAKE_HEADER_SIZE),
                  name_len);
    if (priority != nullptr) {
        *priority = (flags & HANDSHAKE_FLAG_PRIORITY) != 0;
    }
    return true;
}

} // namespace loom_transport
