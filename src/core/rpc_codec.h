#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Минимальный кодек аргументов и результата для RPC поверх GWP.
//
// Значения числовых типов копируются как есть, строка кодируется как
// [uint32 length][bytes]. Метаданные ответа имеют отдельный формат.
namespace loom_rpc {

struct ReplyTo {
    uint64_t worker_handle = 0;
    uint64_t call_seq = 0;

    bool valid() const { return worker_handle != 0 && call_seq != 0; }
};

constexpr size_t REPLY_ENVELOPE_HEADER_SIZE =
    sizeof(uint32_t) + 2 * sizeof(uint64_t);

struct ReplyEnvelope {
    ReplyTo reply_to;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

// REQUEST и RESPONSE используют этот конверт в поле data GWP.
// ASYNC_DATA содержит только аргументы.
std::string encode_reply_envelope(ReplyTo reply_to,
                                  const std::vector<uint8_t>& payload);
bool decode_reply_envelope(const uint8_t* data, size_t size,
                           ReplyEnvelope& envelope);

// Приводит имена типов к каноническому виду (как в JIT-адаптерах):
// string/str -> string, остальные — как есть.
std::string normalize_type(const std::string& type);

// Разбирает "int,string->bool" на параметры и возвращаемый тип.
bool parse_signature(const std::string& signature,
                     std::vector<std::string>& params,
                     std::string& ret);

// Хранилище одного декодированного значения.
struct Value {
    std::string type;
    int i = 0;
    double d = 0.0;
    float f = 0.0f;
    bool b = false;
    std::string s;

    void* data();
    const void* data() const;
};

// Кодирует массив аргументов. args[i] — указатель на значение типа
// params[i]. Для string ожидается const std::string*.
bool encode_args(const std::vector<std::string>& params,
                 const void* const* args,
                 std::vector<uint8_t>& out);

// Декодирует аргументы в Values. Количество и типы должны совпадать.
bool decode_args(const std::vector<std::string>& params,
                 const uint8_t* data, size_t size,
                 std::vector<Value>& out);

// Кодирует/декодирует одно значение (результат метода).
bool encode_value(const std::string& type, const void* value,
                  std::vector<uint8_t>& out);
bool decode_value(const std::string& type,
                  const uint8_t* data, size_t size, void* out);

} // namespace loom_rpc
