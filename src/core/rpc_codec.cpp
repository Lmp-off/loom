#include "rpc_codec.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <endian.h>
#include <limits>
#include <utility>

namespace loom_rpc {

namespace {

constexpr uint32_t kReplyEnvelopeV1 = 0x4c525001; // "LRP" + версия 1

template <typename T>
void append_pod(std::vector<uint8_t>& out, const T& value) {
    const size_t offset = out.size();
    out.resize(offset + sizeof(T));
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

template <typename T>
bool read_pod(const uint8_t* data, size_t size, size_t& offset, T& value) {
    if (data == nullptr || offset > size || size - offset < sizeof(T)) {
        return false;
    }
    std::memcpy(&value, data + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

void append_string(std::vector<uint8_t>& out, const std::string& value) {
    if (value.size() > std::numeric_limits<uint32_t>::max()) {
        return;
    }
    append_pod<uint32_t>(out, static_cast<uint32_t>(value.size()));
    const size_t offset = out.size();
    out.resize(offset + value.size());
    if (!value.empty()) {
        std::memcpy(out.data() + offset, value.data(), value.size());
    }
}

bool read_string(const uint8_t* data, size_t size, size_t& offset,
                 std::string& value) {
    uint32_t length = 0;
    if (!read_pod<uint32_t>(data, size, offset, length)) return false;
    if (offset > size || size - offset < length) return false;

    value.assign(reinterpret_cast<const char*>(data + offset), length);
    offset += length;
    return true;
}

std::string trim(const std::string& value) {
    const size_t begin = value.find_first_not_of(" \t");
    if (begin == std::string::npos) return {};
    const size_t end = value.find_last_not_of(" \t");
    return value.substr(begin, end - begin + 1);
}

bool decode_value_at(const std::string& type,
                     const uint8_t* data, size_t size, size_t& offset,
                     void* out) {
    if (type == "void") return true;
    if (out == nullptr) return false;

    if (type == "int") {
        return read_pod<int>(data, size, offset, *static_cast<int*>(out));
    }
    if (type == "double") {
        return read_pod<double>(data, size, offset,
                                *static_cast<double*>(out));
    }
    if (type == "float") {
        return read_pod<float>(data, size, offset, *static_cast<float*>(out));
    }
    if (type == "bool") {
        return read_pod<bool>(data, size, offset, *static_cast<bool*>(out));
    }
    if (type == "string") {
        return read_string(data, size, offset, *static_cast<std::string*>(out));
    }

    return false;
}

} // namespace

std::string encode_reply_envelope(ReplyTo reply_to,
                                  const std::vector<uint8_t>& payload) {
    const uint32_t format = htobe32(kReplyEnvelopeV1);
    const uint64_t worker_handle = htobe64(reply_to.worker_handle);
    const uint64_t call_seq = htobe64(reply_to.call_seq);

    std::string encoded;
    encoded.reserve(REPLY_ENVELOPE_HEADER_SIZE + payload.size());
    encoded.append(reinterpret_cast<const char*>(&format), sizeof(format));
    encoded.append(reinterpret_cast<const char*>(&worker_handle),
                   sizeof(worker_handle));
    encoded.append(reinterpret_cast<const char*>(&call_seq), sizeof(call_seq));
    if (!payload.empty()) {
        encoded.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    }
    return encoded;
}

bool decode_reply_envelope(const uint8_t* data, size_t size,
                           ReplyEnvelope& envelope) {
    size_t offset = 0;
    uint32_t format = 0;
    uint64_t worker_handle = 0;
    uint64_t call_seq = 0;
    if (!read_pod(data, size, offset, format) ||
        be32toh(format) != kReplyEnvelopeV1 ||
        !read_pod(data, size, offset, worker_handle) ||
        !read_pod(data, size, offset, call_seq)) {
        return false;
    }

    const ReplyTo reply_to{be64toh(worker_handle), be64toh(call_seq)};
    if (!reply_to.valid()) return false;

    envelope = {reply_to, data + offset, size - offset};
    return true;
}

std::string normalize_type(const std::string& type) {
    std::string lower = type;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(::tolower(c)); });

    if (lower == "string" || lower == "str") return "string";
    if (lower == "int") return "int";
    if (lower == "double") return "double";
    if (lower == "float") return "float";
    if (lower == "bool") return "bool";
    if (lower == "void") return "void";
    return type;
}

bool parse_signature(const std::string& signature,
                     std::vector<std::string>& params,
                     std::string& ret) {
    params.clear();
    ret.clear();

    const size_t arrow = signature.find("->");
    if (arrow == std::string::npos) return false;

    const std::string params_str = signature.substr(0, arrow);
    ret = trim(signature.substr(arrow + 2));
    if (ret.empty()) return false;

    if (!params_str.empty()) {
        size_t start = 0;
        while (start <= params_str.size()) {
            const size_t end = params_str.find(',', start);
            const std::string part =
                trim(params_str.substr(start, end == std::string::npos
                                                  ? std::string::npos
                                                  : end - start));
            if (part.empty()) return false;
            params.push_back(part);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    return true;
}

void* Value::data() {
    const std::string type = normalize_type(this->type);
    if (type == "int") return &i;
    if (type == "double") return &d;
    if (type == "float") return &f;
    if (type == "bool") return &b;
    if (type == "string") return &s;
    return nullptr;
}

const void* Value::data() const {
    const std::string type = normalize_type(this->type);
    if (type == "int") return &i;
    if (type == "double") return &d;
    if (type == "float") return &f;
    if (type == "bool") return &b;
    if (type == "string") return &s;
    return nullptr;
}

bool encode_value(const std::string& raw_type, const void* value,
                  std::vector<uint8_t>& out) {
    const std::string type = normalize_type(raw_type);

    if (type == "void") return true;
    if (value == nullptr) return false;

    if (type == "int") {
        append_pod<int>(out, *static_cast<const int*>(value));
        return true;
    }
    if (type == "double") {
        append_pod<double>(out, *static_cast<const double*>(value));
        return true;
    }
    if (type == "float") {
        append_pod<float>(out, *static_cast<const float*>(value));
        return true;
    }
    if (type == "bool") {
        append_pod<bool>(out, *static_cast<const bool*>(value));
        return true;
    }
    if (type == "string") {
        append_string(out, *static_cast<const std::string*>(value));
        return true;
    }

    return false;
}

bool decode_value(const std::string& raw_type,
                  const uint8_t* data, size_t size, void* out) {
    size_t offset = 0;
    return decode_value_at(normalize_type(raw_type), data, size, offset, out);
}

bool encode_args(const std::vector<std::string>& params,
                 const void* const* args,
                 std::vector<uint8_t>& out) {
    if (!params.empty() && args == nullptr) return false;

    out.clear();
    for (size_t i = 0; i < params.size(); ++i) {
        if (args[i] == nullptr) return false;
        if (!encode_value(params[i], args[i], out)) return false;
    }
    return true;
}

bool decode_args(const std::vector<std::string>& params,
                 const uint8_t* data, size_t size,
                 std::vector<Value>& out) {
    out.clear();
    out.reserve(params.size());

    size_t offset = 0;
    for (const auto& param : params) {
        Value value;
        value.type = normalize_type(param);
        if (value.type == "void") return false;
        if (!decode_value_at(value.type, data, size, offset, value.data())) {
            return false;
        }
        out.push_back(std::move(value));
    }

    return offset == size;
}

} // namespace loom_rpc
