// ============================================================================
//  АРХИТЕКТУРА ОБМЕНА ПАКЕТАМИ
// ============================================================================
//
//  ПОТОК ПАКЕТА (A → B → ответ)
//  ----------------------------
//
//   A: бизнес-логика
//        │ manager.send_request("B", "process", data)
//        ▼
//      Transport (UDS/UDP/TCP)
//        │ get_cache("B") → endpoint{type, address, port}
//        │ GWPPacket{method, data}.make_package() → байты
//        │ send/sendto(endpoint)
//        │
//        │  [сеть / UDS]
//        ▼
//   B: Transport (UDS/UDP/TCP)
//        │ accept/recvfrom → fd, addr
//        │ recv → frame
//        │ IncomingPacket{ data=move(frame), client_fd, address, type }
//        ▼
//      TDB
//        │ on_packet(move(ip))
//        │ разбор пакета и вызов бизнес-логики
//        │
//        ├─ client_fd >= 0: ответ отправляется обратно через транспорт
//        └─ client_fd == -1: ответ не требуется
//
//
//  СОСТАВ
//  ------
//   GWPPacket        — сериализуемый пакет: method + data + header
//   IncomingPacket   — принятый пакет: байты кадра + fd + адрес отправителя
//   Transport        — приём/отправка через сокет, знает только TDB
//   TDB              — приём пакетов от транспорта, разбор, вызов логики
//   Cache            — соответствие module_name → endpoint
//
//
//  МОДЕЛИ ОБМЕНА
//  -------------
//                                 | Прямой сокет      | Общий пул
//                                 | (A и B знают др.) | (случайные модули)
//   ------------------------------|-------------------|------------------
//   client_fd в IncomingPacket    | fd                | -1
//   Ответ                         | обратно по fd     | не требуется
//   Janitor (закрытие fd)         | нужен             | не нужен
//
// ============================================================================

#pragma once
#include "uplink/uplink.hpp"
#include "buffer/buffer.hpp"

class TDB {
public:
    TDB();
    ~TDB();
    
    bool init(size_t workers, size_t senders);
    void shutdown();

    void pass_packet(IncomingPacket&& ip);
    
    UPLINK* uplink() { return uplink_.get(); }
    UPLINK* outlink() { return outlink_.get(); }
    
private:
    std::unique_ptr<UPLINK> uplink_;
    std::unique_ptr<UPLINK> outlink_;
};