#pragma once

#include "buffer/buffer.hpp"
#include "outlink/outlink.hpp"
#include "uplink/uplink.hpp"

#include <cstddef>
#include <functional>
#include <memory>

// TDB — Task Distribution Block, композиция UPLINK и OUTLINK.
//
// Архитектурные инварианты:
// - входящий путь: Transport -> pass_packet() -> IngressHandler;
//   кадры, оставленные хуком, идут в UPLINK::push -> Buffer -> Worker;
// - исходящий путь: PacketHandler -> OutgoingSink -> submit_outgoing() ->
//   OUTLINK -> SendCallback -> TransportManager;
// - TDB не знает про транспорт, Cache и GWP-семантику;
// - client_fd входящего пакета всегда -1;
// - priority-пакеты идут в priority Buffer и priority-очередь OUTLINK.
//
// init(): workers — число воркеров UPLINK, senders — число потоков OUTLINK,
// max_direct_workers — лимит выделенных воркеров priority-модулей
// (UPLINK::kAutoDirectWorkers — столько же, сколько обычных).

class TDB {
public:
    // true: кадр обработан на входе. false: передать его в UPLINK.
    using IngressHandler = std::function<bool(const IncomingPacket&)>;

    explicit TDB(IngressHandler ingress_handler = {});
    ~TDB();

    TDB(const TDB&) = delete;
    TDB& operator=(const TDB&) = delete;

    bool init(size_t workers, size_t senders, size_t queue_capacity = 1024,
              std::unique_ptr<DistributionStrategy> strategy = nullptr,
              size_t max_direct_workers = UPLINK::kAutoDirectWorkers);
    void shutdown();

    void pass_packet(IncomingPacket&& ip);
    bool submit_outgoing(OutgoingPacket&& op);
    void set_sender(SendCallback callback);
    void set_handler(PacketHandler handler);

    UPLINK* uplink() { return uplink_.get(); }
    OUTLINK* outlink() { return outlink_.get(); }

    size_t processed_count() const;
    size_t invalid_count() const;
    size_t dropped_count() const;

private:
    const IngressHandler ingress_handler_;
    std::unique_ptr<UPLINK> uplink_;
    std::unique_ptr<OUTLINK> outlink_;
};
