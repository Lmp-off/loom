#include "../../src/transport/transport_manager.hpp"
#include "../../src/transport/local/uds_transport.hpp"
#include "../../src/transport/link.hpp"
#include "../../src/transport/connection_cache.hpp"
#include "../../src/discovery/cache.h"
#include "../../src/gateway/TDB/tdb.hpp"
#include "../../src/protocol/gwp.h"

#include <atomic>
#include <chrono>
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

class FakeTransport final : public Transport {
public:
    bool start() override {
        started = true;
        return true;
    }

    void stop() override {
        stopped = true;
    }

    int send(const endpoint& ep, const std::vector<uint8_t>& frame) override {
        last_endpoint = ep;
        frames.push_back(frame);
        return static_cast<int>(frame.size());
    }

    connection_type type() const override { return type_; }
    int get_listen_fd() const override { return -1; }

    connection_type type_ = CONNECTION_UDS;
    bool started = false;
    bool stopped = false;
    endpoint last_endpoint;
    std::vector<std::vector<uint8_t>> frames;
};

int test_manager_routing() {
    get_cache().clear();

    TransportManager manager;
    auto fake = std::make_unique<FakeTransport>();
    FakeTransport* transport = fake.get();

    if (!manager.register_transport(std::move(fake))) return 1;
    if (manager.count() != 1) return 2;
    if (!manager.start_all()) return 3;
    if (!transport->started) return 4;

    save_endpoint_to_cache("target", "/tmp/loom_test_target.sock");

    if (manager.send_request("target", "echo", "hello") <= 0) return 5;
    if (transport->frames.size() != 1) return 6;

    gwp::GWPPacket request;
    if (!request.deserialize(transport->frames[0].data(),
                             transport->frames[0].size())) {
        return 7;
    }
    if (request.get_type() != gwp::PacketType::REQUEST) return 8;
    if (request.get_method() != "echo") return 9;
    if (request.get_data_size() != 5) return 10;

    const std::string body(
        reinterpret_cast<const char*>(request.get_data()),
        request.get_data_size());
    if (body != "hello") return 11;

    const std::vector<uint8_t> raw = {1, 2, 3, 4};
    OutgoingPacket outgoing;
    outgoing.module = "target";
    outgoing.data = raw;

    if (manager.send_outgoing(std::move(outgoing)) !=
        static_cast<int>(raw.size())) {
        return 12;
    }
    if (transport->frames.size() != 2) return 13;
    if (transport->frames[1] != raw) return 14;

    // Cache miss должен уйти в ядро (server_api::locate) и осесть в Cache.
    get_cache().remove("missing");
    if (manager.send_request("missing", "m", "d") <= 0) return 15;
    if (transport->frames.size() != 3) return 16;

    endpoint resolved;
    if (!get_cache().get("missing", resolved)) return 17;
    if (resolved.address != "/tmp/loom_missing.sock") return 18;

    endpoint udp_endpoint;
    udp_endpoint.type = CONNECTION_UDP;
    udp_endpoint.address = "127.0.0.1";
    udp_endpoint.port = 1;
    get_cache().set("udp_target", udp_endpoint);
    if (manager.send_request("udp_target", "m", "d") >= 0) return 19;

    manager.stop_all();
    if (!transport->stopped) return 20;

    get_cache().clear();
    get_link_manager().close_all();
    return 0;
}

int test_manager_uds_roundtrip() {
    get_cache().clear();
    get_link_manager().close_all();

    const std::string module = "transport_manager_self";
    const std::string path = "/tmp/loom_" + module + ".sock";

    TDB tdb;
    if (!tdb.init(1, 1, 16)) return 1;

    std::atomic<int> handled{0};
    std::mutex handled_mutex;
    std::string handled_method;

    tdb.set_handler([&handled, &handled_mutex, &handled_method](
                        const IncomingPacket&,
                        gwp::GWPPacket& packet,
                        const OutgoingSink&) {
        {
            std::lock_guard<std::mutex> lock(handled_mutex);
            handled_method = packet.get_method();
        }
        handled.fetch_add(1, std::memory_order_relaxed);
    });

    TransportManager manager;
    if (!manager.register_transport(std::make_unique<UdsTransport>(path))) {
        return 2;
    }
    manager.set_tdb(&tdb);
    if (!manager.start_all()) return 3;

    // Cache пуст: endpoint должен прийти из ядра и сохраниться в Cache.
    if (manager.send_request(module, "ping", "payload") <= 0) return 4;

    endpoint cached;
    if (!get_cache().get(module, cached)) return 5;
    if (cached.address != path) return 6;

    if (!wait_until([&handled]() {
            return handled.load(std::memory_order_relaxed) == 1;
        })) {
        return 7;
    }
    {
        std::lock_guard<std::mutex> lock(handled_mutex);
        if (handled_method != "ping") return 8;
    }

    // Второй кадр должен уйти по тому же Link, а не новым соединением.
    OutgoingPacket outgoing;
    outgoing.module = module;
    outgoing.data = make_frame(8, gwp::PacketType::ASYNC_DATA, "raw", "x");
    if (manager.send_outgoing(std::move(outgoing)) <= 0) return 9;
    if (get_link_manager().size() != 1) return 10;

    if (!wait_until([&handled]() {
            return handled.load(std::memory_order_relaxed) == 2;
        })) {
        return 11;
    }
    {
        std::lock_guard<std::mutex> lock(handled_mutex);
        if (handled_method != "raw") return 12;
    }

    manager.stop_all();
    tdb.shutdown();
    get_cache().clear();
    get_link_manager().close_all();
    return 0;
}

int test_link_concurrent_reuse() {
    get_cache().clear();
    get_link_manager().close_all();

    const std::string module = "link_concurrent";
    const std::string path = "/tmp/loom_" + module + ".sock";

    TDB tdb;
    if (!tdb.init(1, 1, 256)) return 1;

    std::atomic<int> handled{0};
    tdb.set_handler([&handled](const IncomingPacket&,
                               gwp::GWPPacket&,
                               const OutgoingSink&) {
        handled.fetch_add(1, std::memory_order_relaxed);
    });

    TransportManager manager;
    if (!manager.register_transport(std::make_unique<UdsTransport>(path))) {
        return 2;
    }
    manager.set_tdb(&tdb);
    if (!manager.start_all()) return 3;

    save_endpoint_to_cache(module, path);

    constexpr int senders = 4;
    constexpr int per_sender = 25;
    constexpr int total = senders * per_sender;

    std::atomic<int> failed{0};
    std::vector<std::thread> threads;
    threads.reserve(senders);
    for (int i = 0; i < senders; ++i) {
        threads.emplace_back([&manager, &failed, module, i]() {
            for (int j = 0; j < per_sender; ++j) {
                if (manager.send_request(module,
                                         "m",
                                         std::to_string(i) + ":" +
                                             std::to_string(j)) <= 0) {
                    failed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();

    if (failed.load(std::memory_order_relaxed) != 0) return 4;
    if (!wait_until([&handled, total]() {
            return handled.load(std::memory_order_relaxed) == total;
        })) {
        return 5;
    }
    if (get_link_manager().size() != 1) return 6;

    manager.stop_all();
    tdb.shutdown();
    get_cache().clear();
    get_link_manager().close_all();
    return 0;
}

int test_inbound_connection_reuse() {
    get_cache().clear();
    get_link_manager().close_all();
    get_connection_cache().clear();

    const std::string module = "inbound_reuse";
    const std::string path = "/tmp/loom_" + module + ".sock";

    TDB tdb;
    if (!tdb.init(1, 1, 32)) return 1;

    std::atomic<int> responses{0};
    std::mutex method_mutex;
    std::string response_method;

    tdb.set_handler([&responses, &method_mutex, &response_method](
                        const IncomingPacket& incoming,
                        gwp::GWPPacket& packet,
                        const OutgoingSink& sink) {
        if (packet.get_type() == gwp::PacketType::REQUEST &&
            packet.get_method() == "ping") {
            // Отвечаем на тот же модуль, от которого пришёл запрос.
            // Транспорт обязан найти уже принятое входящее соединение
            // и записать ответ в него, а не открывать новое.
            OutgoingPacket response;
            response.module = incoming.client_address;
            response.transport_type = incoming.transport_type;
            response.data = make_frame(gwp::next_packet_id(),
                                       gwp::PacketType::RESPONSE,
                                       "pong",
                                       "ok");
            sink(std::move(response));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(method_mutex);
            response_method = packet.get_method();
        }
        responses.fetch_add(1, std::memory_order_relaxed);
    });

    TransportManager manager;
    if (!manager.register_transport(std::make_unique<UdsTransport>(path))) {
        return 2;
    }
    manager.set_tdb(&tdb);
    tdb.set_sender([&manager](OutgoingPacket&& packet) {
        return manager.send_outgoing(std::move(packet));
    });
    if (!manager.start_all()) return 3;

    // Имя модуля уйдёт в handshake, поэтому сервер зарегистрирует
    // входящее соединение под этим именем.
    get_link_manager().set_local_module(module);
    save_endpoint_to_cache(module, path);

    if (manager.send_request(module, "ping", "payload") <= 0) return 4;

    if (!wait_until([&responses]() {
            return responses.load(std::memory_order_relaxed) == 1;
        })) {
        return 5;
    }
    {
        std::lock_guard<std::mutex> lock(method_mutex);
        if (response_method != "pong") return 6;
    }

    // Ответ пришёл по тому же fd: ни второго Link, ни второго
    // входящего соединения не появилось.
    if (get_link_manager().size() != 1) return 7;
    if (get_connection_cache().size() != 1) return 8;

    manager.stop_all();
    tdb.shutdown();
    get_link_manager().set_local_module("");
    get_connection_cache().clear();
    get_link_manager().close_all();
    get_cache().clear();
    return 0;
}

} // namespace

int test_transport_all() {
    int result = test_manager_routing();
    if (result != 0) return 100 + result;

    result = test_manager_uds_roundtrip();
    if (result != 0) return 200 + result;

    result = test_link_concurrent_reuse();
    if (result != 0) return 300 + result;

    result = test_inbound_connection_reuse();
    if (result != 0) return 400 + result;

    return 0;
}
