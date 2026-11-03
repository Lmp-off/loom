#include "../../src/gateway/TDB/tdb.hpp"
#include "../../src/gateway/TDB/strategy/strategy.hpp"
#include "../../src/gateway/TDB/worker/worker.hpp"
#include "../../src/discovery/cache.h"
#include "../../src/protocol/gwp.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

template <typename Predicate>
bool wait_until(Predicate predicate, int timeout_ms = 3000) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

std::vector<uint8_t> make_frame(uint32_t id,
                                gwp::PacketType type,
                                const std::string& method,
                                const std::string& payload) {
    gwp::GWPPacket packet;
    packet.set_id(id);
    packet.set_type(type);
    packet.set_method(method);
    packet.set_data(payload.empty() ? nullptr : payload.data(), payload.size());
    return packet.make_package();
}

class CountingStrategy final : public DistributionStrategy {
public:
    size_t dispatch(
        const IncomingPacket&,
        const std::vector<std::unique_ptr<Worker>>& workers) override {
        calls.fetch_add(1, std::memory_order_relaxed);

        const size_t count = workers.size();
        if (count == 0) return npos;

        const size_t start = next.fetch_add(1, std::memory_order_relaxed);
        for (size_t offset = 0; offset < count; ++offset) {
            const size_t index = (start + offset) % count;
            if (workers[index] && workers[index]->is_free()) {
                return index;
            }
        }
        return npos;
    }

    std::atomic<size_t> calls{0};
    std::atomic<size_t> next{0};
};

int test_buffer_basic() {
    Buffer buffer;
    if (!buffer.init(64)) return 1;
    if (!buffer.empty() || buffer.size() != 0) return 2;
    if (buffer.capacity() != 64) return 3;

    IncomingPacket packet;
    packet.data = {1, 2, 3};
    packet.client_fd = -1;
    if (!buffer.push(std::move(packet))) return 4;
    if (buffer.empty() || buffer.size() != 1) return 5;

    IncomingPacket out;
    if (!buffer.pop(out)) return 6;
    if (out.data != std::vector<uint8_t>({1, 2, 3})) return 7;
    if (!buffer.empty()) return 8;
    if (buffer.pop(out)) return 9;

    return 0;
}

int test_buffer_mpmc() {
    Buffer buffer;
    if (!buffer.init(1024)) return 1;

    constexpr int producers = 4;
    constexpr int consumers = 4;
    constexpr int per_producer = 2000;
    constexpr int total = producers * per_producer;

    std::atomic<bool> abort{false};
    std::atomic<int> received{0};
    auto seen = std::make_unique<std::atomic<int>[]>(total);
    for (int i = 0; i < total; ++i) {
        seen[i].store(0, std::memory_order_relaxed);
    }

    std::vector<std::thread> threads;
    threads.reserve(producers + consumers);

    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&buffer, &abort, p]() {
            for (int i = 0; i < per_producer; ++i) {
                IncomingPacket packet;
                packet.data = {
                    static_cast<uint8_t>(p),
                    static_cast<uint8_t>(i & 0xFF),
                    static_cast<uint8_t>((i >> 8) & 0xFF),
                };
                packet.client_fd = -1;

                while (!buffer.push(std::move(packet))) {
                    if (abort.load(std::memory_order_relaxed)) return;
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int c = 0; c < consumers; ++c) {
        threads.emplace_back([&buffer, &abort, &received, &seen]() {
            while (received.load(std::memory_order_relaxed) < total &&
                   !abort.load(std::memory_order_relaxed)) {
                IncomingPacket out;
                if (!buffer.pop(out)) {
                    std::this_thread::yield();
                    continue;
                }

                if (out.data.size() != 3) {
                    abort.store(true, std::memory_order_relaxed);
                    return;
                }

                const int producer = out.data[0];
                const int sequence = out.data[1] | (out.data[2] << 8);
                if (producer < 0 || producer >= producers ||
                    sequence < 0 || sequence >= per_producer) {
                    abort.store(true, std::memory_order_relaxed);
                    return;
                }

                const int index = producer * per_producer + sequence;
                if (seen[index].fetch_add(1, std::memory_order_relaxed) != 0) {
                    abort.store(true, std::memory_order_relaxed);
                    return;
                }
                received.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (received.load(std::memory_order_relaxed) < total &&
           Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (received.load(std::memory_order_relaxed) != total) {
        abort.store(true, std::memory_order_relaxed);
    }

    for (auto& thread : threads) thread.join();

    if (abort.load(std::memory_order_relaxed)) return 2;
    if (received.load(std::memory_order_relaxed) != total) return 3;

    for (int i = 0; i < total; ++i) {
        if (seen[i].load(std::memory_order_relaxed) != 1) return 4;
    }

    return 0;
}

int test_uplink_dispatch() {
    TDB tdb;
    if (!tdb.init(4, 1, 1024)) return 1;

    constexpr int total = 400;
    for (int i = 0; i < total; ++i) {
        IncomingPacket packet;
        packet.data = make_frame(static_cast<uint32_t>(i),
                                 gwp::PacketType::REQUEST,
                                 "process",
                                 "payload");
        packet.client_fd = -1;
        tdb.pass_packet(std::move(packet));
    }

    if (!wait_until([&tdb, total]() {
            return tdb.processed_count() == static_cast<size_t>(total);
        })) {
        return 2;
    }

    if (tdb.invalid_count() != 0) return 3;
    if (tdb.dropped_count() != 0) return 4;

    UPLINK* uplink = tdb.uplink();
    if (uplink == nullptr) return 5;
    if (uplink->workers_count() != 4) return 6;
    if (uplink->queue_size() != 0) return 7;

    size_t per_worker_total = 0;
    for (size_t i = 0; i < uplink->workers_count(); ++i) {
        per_worker_total += uplink->worker_processed(i);
    }
    if (per_worker_total != total) return 8;

    return 0;
}

int test_uplink_invalid_frame() {
    TDB tdb;
    if (!tdb.init(1, 1, 8)) return 1;

    IncomingPacket packet;
    packet.data = {0x00, 0x01, 0x02, 0x03};
    packet.client_fd = -1;
    tdb.pass_packet(std::move(packet));

    if (!wait_until([&tdb]() { return tdb.invalid_count() == 1; })) return 2;
    if (tdb.processed_count() != 0) return 3;

    return 0;
}

int test_uplink_strategy_dispatch() {
    TDB tdb;
    auto strategy = std::make_unique<CountingStrategy>();
    CountingStrategy* counting = strategy.get();

    if (!tdb.init(3, 1, 1024, std::move(strategy))) return 1;

    constexpr int total = 300;
    for (int i = 0; i < total; ++i) {
        IncomingPacket packet;
        packet.data = make_frame(static_cast<uint32_t>(i),
                                 gwp::PacketType::REQUEST,
                                 "process",
                                 "payload");
        packet.client_fd = -1;
        tdb.pass_packet(std::move(packet));
    }

    if (!wait_until([&tdb, total]() {
            return tdb.processed_count() == static_cast<size_t>(total);
        })) {
        return 2;
    }

    if (tdb.invalid_count() != 0) return 3;
    if (tdb.dropped_count() != 0) return 4;

    UPLINK* uplink = tdb.uplink();
    if (uplink == nullptr) return 5;
    if (uplink->dispatched_count() != static_cast<size_t>(total)) return 6;
    if (counting->calls.load(std::memory_order_relaxed) <
        static_cast<size_t>(total)) {
        return 7;
    }

    return 0;
}

int test_worker_single_slot() {
    WakeChannel ready;
    Worker worker(0, &ready);

    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;

    worker.set_outgoing_sink([](OutgoingPacket&&) { return 0; });
    worker.set_handler([&mutex, &cv, &entered, &release](
                           const IncomingPacket&,
                           gwp::GWPPacket&,
                           const OutgoingSink&) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&release] { return release; });
    });

    if (worker.start() != 0) return 1;

    IncomingPacket first;
    first.data = make_frame(1, gwp::PacketType::REQUEST, "process", "first");
    first.client_fd = -1;
    if (!worker.try_submit(std::move(first))) return 2;

    if (!wait_until([&mutex, &entered]() {
            std::lock_guard<std::mutex> lock(mutex);
            return entered;
        })) {
        return 3;
    }

    IncomingPacket second;
    second.data = make_frame(2, gwp::PacketType::REQUEST, "process", "second");
    second.client_fd = -1;
    if (worker.try_submit(std::move(second))) return 4;

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();
    worker.stop();

    return 0;
}

int test_outlink_delivery() {
    TDB tdb;
    if (!tdb.init(1, 2, 512)) return 1;

    constexpr int total = 200;

    std::atomic<int> delivered{0};
    std::atomic<uint64_t> checksum{0};

    tdb.set_sender([&delivered, &checksum](OutgoingPacket&& packet) {
        if (!packet.data.empty()) {
            checksum.fetch_add(packet.data[0], std::memory_order_relaxed);
        }
        delivered.fetch_add(1, std::memory_order_relaxed);
        return 0;
    });

    uint64_t expected_checksum = 0;
    for (int i = 0; i < total; ++i) {
        const uint8_t value = static_cast<uint8_t>(i & 0xFF);
        expected_checksum += value;

        OutgoingPacket packet;
        packet.module = "target";
        packet.data = {value};
        packet.transport_type = 0;

        if (!tdb.submit_outgoing(std::move(packet))) return 2;
    }

    if (!wait_until([&delivered, &tdb, total]() {
            return delivered.load(std::memory_order_relaxed) == total &&
                   tdb.outlink()->sent_count() == static_cast<size_t>(total);
        })) {
        return 3;
    }

    if (tdb.outlink() == nullptr) return 4;
    if (tdb.outlink()->sent_count() != total) return 5;
    if (tdb.outlink()->failed_count() != 0) return 6;
    if (checksum.load(std::memory_order_relaxed) != expected_checksum) return 7;

    return 0;
}

int test_outlink_failure() {
    TDB tdb;
    if (!tdb.init(1, 1, 16)) return 1;

    tdb.set_sender([](OutgoingPacket&&) { return -1; });

    OutgoingPacket packet;
    packet.module = "target";
    packet.data = {1};
    if (!tdb.submit_outgoing(std::move(packet))) return 2;

    if (!wait_until([&tdb]() {
            return tdb.outlink()->failed_count() == 1;
        })) {
        return 3;
    }

    return 0;
}

int test_worker_outgoing_chain() {
    TDB tdb;
    if (!tdb.init(1, 1, 16)) return 1;

    std::mutex outgoing_mutex;
    std::vector<OutgoingPacket> outgoing;

    tdb.set_sender([&outgoing_mutex, &outgoing](OutgoingPacket&& packet) {
        std::lock_guard<std::mutex> lock(outgoing_mutex);
        outgoing.push_back(std::move(packet));
        return 0;
    });

    tdb.set_handler([](const IncomingPacket& incoming,
                       gwp::GWPPacket& request,
                       const OutgoingSink& sink) {
        gwp::GWPPacket response;
        response.set_type(gwp::PacketType::RESPONSE);
        response.set_id(request.get_id());
        response.set_method(request.get_method());
        const std::string body = "ok";
        response.set_data(body.data(), body.size());

        OutgoingPacket packet;
        packet.module = incoming.client_address;
        packet.transport_type = incoming.transport_type;
        packet.data = response.make_package();
        sink(std::move(packet));
    });

    IncomingPacket packet;
    packet.data = make_frame(42, gwp::PacketType::REQUEST, "process", "payload");
    packet.client_address = "target";
    packet.transport_type = static_cast<int>(CONNECTION_UDS);
    tdb.pass_packet(std::move(packet));

    if (!wait_until([&outgoing_mutex, &outgoing]() {
            std::lock_guard<std::mutex> lock(outgoing_mutex);
            return !outgoing.empty();
        })) {
        return 2;
    }

    OutgoingPacket captured;
    {
        std::lock_guard<std::mutex> lock(outgoing_mutex);
        captured = std::move(outgoing.front());
    }

    if (captured.module != "target") return 3;

    gwp::GWPPacket response;
    if (!response.deserialize(captured.data.data(), captured.data.size())) {
        return 4;
    }
    if (response.get_type() != gwp::PacketType::RESPONSE) return 5;
    if (response.get_id() != 42) return 6;
    if (response.get_method() != "process") return 7;
    if (response.get_data_size() != 2) return 8;

    const std::string body(
        reinterpret_cast<const char*>(response.get_data()),
        response.get_data_size());
    if (body != "ok") return 9;

    return 0;
}

int test_uplink_priority_dedicated_worker() {
    TDB tdb;
    if (!tdb.init(1, 1, 64)) return 1;

    std::mutex mutex;
    std::condition_variable cv;
    bool slow_entered = false;
    bool release = false;
    std::vector<std::string> order;

    tdb.set_handler([&mutex, &cv, &slow_entered, &release, &order](
                        const IncomingPacket& incoming, gwp::GWPPacket&,
                        const OutgoingSink&) {
        if (incoming.client_address == "slow") {
            std::unique_lock<std::mutex> lock(mutex);
            slow_entered = true;
            cv.notify_all();
            cv.wait(lock, [&release] { return release; });
        }

        std::lock_guard<std::mutex> lock(mutex);
        order.push_back(incoming.client_address);
    });

    auto make = [](uint32_t id, const std::string& module, bool priority) {
        IncomingPacket packet;
        packet.data = make_frame(id, gwp::PacketType::REQUEST, "m", "x");
        packet.client_address = module;
        packet.priority = priority;
        return packet;
    };

    tdb.pass_packet(make(1, "slow", false));
    if (!wait_until([&mutex, &slow_entered]() {
            std::lock_guard<std::mutex> lock(mutex);
            return slow_entered;
        })) {
        return 2;
    }

    // Пока единственный общий воркер занят, priority-пакет должен уйти
    // на выделенный Worker и обработаться сразу.
    tdb.pass_packet(make(2, "fast", true));
    if (!wait_until([&mutex, &order]() {
            std::lock_guard<std::mutex> lock(mutex);
            return !order.empty();
        })) {
        return 3;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (order.size() != 1 || order[0] != "fast") return 4;
    }

    if (tdb.uplink() == nullptr) return 5;
    if (tdb.uplink()->direct_workers_count() != 1) return 6;

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();

    // processed_count() увеличивается до вызова handler, поэтому ждём
    // фактического завершения обработчиков, а не счётчик воркера.
    if (!wait_until([&mutex, &order]() {
            std::lock_guard<std::mutex> lock(mutex);
            return order.size() == 2;
        })) {
        return 7;
    }
    return 0;
}

int test_uplink_priority_shared_fallback() {
    TDB tdb;
    // max_direct_workers = 0: выделенных воркеров нет, priority-пакет
    // должен обслуживаться общим пулом, но раньше обычных пакетов.
    if (!tdb.init(1, 1, 64, nullptr, 0)) return 1;

    std::mutex mutex;
    std::condition_variable cv;
    bool slow_entered = false;
    bool release = false;
    std::vector<std::string> order;

    tdb.set_handler([&mutex, &cv, &slow_entered, &release, &order](
                        const IncomingPacket& incoming, gwp::GWPPacket&,
                        const OutgoingSink&) {
        if (incoming.client_address == "slow") {
            std::unique_lock<std::mutex> lock(mutex);
            slow_entered = true;
            cv.notify_all();
            cv.wait(lock, [&release] { return release; });
        }

        std::lock_guard<std::mutex> lock(mutex);
        order.push_back(incoming.client_address);
    });

    auto make = [](uint32_t id, const std::string& module, bool priority) {
        IncomingPacket packet;
        packet.data = make_frame(id, gwp::PacketType::REQUEST, "m", "x");
        packet.client_address = module;
        packet.priority = priority;
        return packet;
    };

    tdb.pass_packet(make(1, "slow", false));
    if (!wait_until([&mutex, &slow_entered]() {
            std::lock_guard<std::mutex> lock(mutex);
            return slow_entered;
        })) {
        return 2;
    }

    tdb.pass_packet(make(2, "fast", true));
    tdb.pass_packet(make(3, "normal", false));

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();

    if (!wait_until([&mutex, &order]() {
            std::lock_guard<std::mutex> lock(mutex);
            return order.size() == 3;
        })) {
        return 3;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        if (order.size() != 3) return 4;
        if (order[0] != "slow") return 5;
        if (order[1] != "fast") return 6;
        if (order[2] != "normal") return 7;
    }

    if (tdb.uplink() == nullptr) return 8;
    if (tdb.uplink()->direct_workers_count() != 0) return 9;

    return 0;
}

} // namespace

int test_tdb_all() {
    int result = test_buffer_basic();
    if (result != 0) return 100 + result;

    result = test_buffer_mpmc();
    if (result != 0) return 200 + result;

    result = test_uplink_dispatch();
    if (result != 0) return 300 + result;

    result = test_uplink_invalid_frame();
    if (result != 0) return 400 + result;

    result = test_uplink_strategy_dispatch();
    if (result != 0) return 450 + result;

    result = test_worker_single_slot();
    if (result != 0) return 475 + result;

    result = test_outlink_delivery();
    if (result != 0) return 500 + result;

    result = test_outlink_failure();
    if (result != 0) return 600 + result;

    result = test_worker_outgoing_chain();
    if (result != 0) return 700 + result;

    result = test_uplink_priority_dedicated_worker();
    if (result != 0) return 800 + result;

    result = test_uplink_priority_shared_fallback();
    if (result != 0) return 900 + result;

    return 0;
}
