#ifndef GWP_H
#define GWP_H

/*
 * GWP — внутренний протокол обмена между модулями.
 *
 * Пакет транспортно-независим: один и тот же GWPPacket передаётся
 * по UDS, UDP и TCP. Транспорт только переносит байты, сериализацией
 * и десериализацией занимается сам GWPPacket.
 *
 * Состав пакета:
 *   - GWPHeader   — magic, packet_id, type, status, длины method и data
 *   - method_name — имя вызываемого метода
 *   - data        — payload, интерпретация на стороне бизнес-логики
 *
 * В пакете намеренно отсутствуют:
 *   - module_name — резолвится через Cache
 *                   (module_name -> endpoint). В пакет не кладётся,
 *                   чтобы не дублировать DNS-ответ в каждом запросе.
 *   - endpoint / address / port / transport_type — транспортные метаданные,
 *                   известны транспорту из accept/recvfrom и передаются
 *                   рядом с пакетом (PacketWithSource), не внутри.
 *   - client_fd   — эфемерное значение, живёт только в процессе,
 *                   не сериализуется. Для request/response на одном
 *                   коннекте fd передаётся отдельным параметром.
 *
 * Формат сериализации:
 *   [GWPHeader][method_name][data]
 * Порядок фиксирован. Многобайтовые поля заголовка передаются
 * в big-endian (htonl/ntohl).
 *
 *
 * deserialize() ожидает полный кадр целиком; транспорт обязан
 * дочитать его до вызова deserialize().
 */

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace gwp {

// Тип пакета. Определяет семантику обработки на приёмнике.
//   REQUEST    — запрос на вызов метода, ожидается RESPONSE
//   RESPONSE   — ответ на REQUEST, status = StatusCode
//   ASYNC_DATA — асинхронные данные, ответ не требуется
//   HEARTBEAT  — проверка живости, ответ не требуется
//   CLOSE      — корректное закрытие сессии
enum class PacketType : uint8_t {
    REQUEST = 0,
    RESPONSE = 1,
    ASYNC_DATA = 2,
    HEARTBEAT = 3,
    CLOSE = 4
};

// Статус обработки. Имеет смысл только для PacketType::RESPONSE.
enum class StatusCode : uint8_t {
    OK = 0,
    ERROR = 1,
    METHOD_NOT_FOUND = 2,
    TIMEOUT = 3,
    INVALID_PARAMS = 4
};

// Заголовок GWP, всегда идёт первым в кадре.
// В памяти поля в host-byte-order, на провод уходят big-endian.
// Порядок полей не менять без новой версии протокола.
struct GWPHeader {
    uint32_t magic;          // 0x4757505F ("GWP_"), проверяется первым
    uint32_t packet_id;      // уникальный ID пакета
    PacketType type;         // тип пакета
    uint8_t status;          // статус (для RESPONSE), иначе 0
    uint32_t method_name_len;// длина имени метода в байтах
    uint32_t data_len;       // длина payload в байтах
};

// GWPPacket — пакет протокола GWP.
//
// Инварианты:
//   - magic всегда 0x4757505F (задаётся в конструкторе)
//   - make_package() возвращает ровно sizeof(GWPHeader) + method_name_len + data_len байт
//   - deserialize() возвращает true только при совпадении magic
//     и достаточной длине буфера
//
// Использование:
//   отправка: GWPPacket{} -> set_method -> set_data -> make_package -> send
//   приём:    recv кадра -> deserialize -> get_method/get_data
//
// Не потокобезопасен. Один экземпляр — один поток; передача между
// потоками через std::move.
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
    
    // Сериализация в байты для send/sendto.
    // Возвращает полный кадр: header (big-endian) + method + data.
    std::vector<uint8_t> make_package() const;
    
    // Десериализация из байтов после recv/recvfrom.
    // buffer должен содержать полный кадр.
    // Возвращает false, если len < sizeof(GWPHeader), magic не совпал
    // или len < ожидаемой длины. При false состояние объекта не меняется.
    bool deserialize(const uint8_t* buffer, size_t len);
    
private:
    GWPHeader header_;
    std::string method_name_;
    std::vector<uint8_t> data_;
};

}

#endif