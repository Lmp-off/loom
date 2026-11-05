#include "../../include/loom.h"

#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../../metalogger/metalogger.h"

#include "server_core_api.h"
#include "../transport/transport_manager.hpp"
#include "../transport/link.hpp"
#include "../transport/connection_cache.hpp"
#include "../transport/local/uds_transport.hpp"
#include "../discovery/capabilities.h"
#include "../discovery/cache.h"
#include "manager.h"
#include "rpc_codec.h"
#include "rpc_state.h"
#include "../gateway/TDB/tdb.hpp"
#include "../global.h"
#include "../protocol/gwp.h"

static std::string g_name;
static std::unique_ptr<TransportManager> g_transport_manager;

namespace {

struct PendingMethod {
    std::string name;
    std::string signature;
    void* impl = nullptr;
};

// Регистрация методов и чтение реестра воркерами идут из разных потоков.
// Держим снимок method под этим mutex, а пользовательскую функцию
// вызываем уже без блокировки.
std::mutex g_registration_mutex;
std::vector<PendingMethod> g_pending_methods;

void flush_pending_methods(const std::string& module) {
    std::lock_guard<std::mutex> lock(g_registration_mutex);
    for (const auto& pending : g_pending_methods) {
        register_method_impl(module.c_str(), pending.name.c_str(),
                             pending.signature.c_str(), pending.impl);
    }
    g_pending_methods.clear();
}

void send_response(const IncomingPacket& incoming,
                   const std::string& method_name,
                   loom_rpc::ReplyTo reply_to,
                   const OutgoingSink& sink,
                   gwp::StatusCode status,
                   const std::vector<uint8_t>& data) {
    if (incoming.client_address.empty()) {
        METALOG_WARNING("loom: cannot reply, sender module is unknown");
        return;
    }

    gwp::GWPPacket response;
    response.set_type(gwp::PacketType::RESPONSE);
    response.set_id(gwp::next_packet_id());
    response.set_method(method_name);
    response.set_status(static_cast<uint8_t>(status));
    const std::string body = loom_rpc::encode_reply_envelope(reply_to, data);
    response.set_data(body.data(), body.size());

    OutgoingPacket packet;
    packet.module = incoming.client_address;
    packet.transport_type = incoming.transport_type;
    packet.priority = incoming.priority;
    packet.data = response.make_package();

    // Если ответ придётся отправлять через LinkManager, handshake нового
    // соединения должен нести тот же priority-режим. Обратный случай
    // (priority=false) не должен сбрасывать явную настройку use_module_direct.
    if (incoming.priority) {
        get_connection_cache().set_priority(incoming.client_address, true);
    }

    if (!sink(std::move(packet))) {
        METALOG_WARNING("loom: failed to enqueue response for " +
                        incoming.client_address);
    }
}

// Вызывает зарегистрированный метод и при respond == true отправляет
// RESPONSE обратно отправителю. ASYNC_DATA вызывает метод без ответа.
void invoke_method(const IncomingPacket& incoming,
                   gwp::GWPPacket& request,
                   const OutgoingSink& sink,
                   bool respond) {
    const std::string method_name = request.get_method();
    const uint8_t* args_data = request.get_data();
    size_t args_size = request.get_data_size();
    loom_rpc::ReplyTo reply_to;
    if (respond) {
        loom_rpc::ReplyEnvelope envelope;
        if (!loom_rpc::decode_reply_envelope(args_data, args_size, envelope)) {
            METALOG_WARNING("loom: invalid RPC request envelope");
            return;
        }
        reply_to = envelope.reply_to;
        args_data = envelope.data;
        args_size = envelope.size;
    }

    method target;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_registration_mutex);
        Module& module = ModuleRegistry::get(g_name);
        const method* registered = module.get_method(method_name);
        if (registered != nullptr) {
            target = *registered;
            found = true;
        }
    }

    if (!found || target.adapter == nullptr) {
        METALOG_WARNING("loom: method not found: " + method_name);
        if (respond) {
            send_response(incoming, method_name, reply_to, sink,
                          gwp::StatusCode::METHOD_NOT_FOUND, {});
        }
        return;
    }

    std::vector<loom_rpc::Value> values;
    if (!loom_rpc::decode_args(target.param_types,
                               args_data, args_size, values)) {
        METALOG_WARNING("loom: invalid arguments for method " + method_name);
        if (respond) {
            send_response(incoming, method_name, reply_to, sink,
                          gwp::StatusCode::INVALID_PARAMS, {});
        }
        return;
    }

    std::vector<const void*> args;
    args.reserve(values.size());
    for (const auto& value : values) {
        args.push_back(value.data());
    }

    loom_rpc::Value result;
    result.type = loom_rpc::normalize_type(target.return_type);
    void* result_ptr = (result.type == "void") ? nullptr : result.data();

    using AdapterFunc = void (*)(const void**, void*);
    auto adapter = reinterpret_cast<AdapterFunc>(target.adapter);

    try {
        adapter(args.empty() ? nullptr : args.data(), result_ptr);
    } catch (const std::exception& e) {
        METALOG_ERROR("loom: method " + method_name + " failed: " + e.what());
        if (respond) {
            send_response(incoming, method_name, reply_to, sink,
                          gwp::StatusCode::ERROR, {});
        }
        return;
    } catch (...) {
        METALOG_ERROR("loom: method " + method_name + " failed");
        if (respond) {
            send_response(incoming, method_name, reply_to, sink,
                          gwp::StatusCode::ERROR, {});
        }
        return;
    }

    if (!respond) return;

    std::vector<uint8_t> payload;
    if ((result.type != "void" &&
         !loom_rpc::encode_value(result.type, result.data(), payload)) ||
        payload.size() > gwp::MAX_DATA_LEN - loom_rpc::REPLY_ENVELOPE_HEADER_SIZE) {
        send_response(incoming, method_name, reply_to, sink,
                      gwp::StatusCode::ERROR, {});
        return;
    }

    send_response(incoming, method_name, reply_to, sink,
                  gwp::StatusCode::OK, payload);
}

void loom_packet_handler(const IncomingPacket& incoming,
                         gwp::GWPPacket& packet,
                         const OutgoingSink& sink) {
    switch (packet.get_type()) {
        case gwp::PacketType::REQUEST:
            invoke_method(incoming, packet, sink, true);
            break;
        case gwp::PacketType::ASYNC_DATA:
            invoke_method(incoming, packet, sink, false);
            break;
        default:
            break;
    }
}

} // namespace

//TODO:
uint8_t start(const char* module_name) {
    if (module_name == nullptr || *module_name == '\0') {
        METALOG_ERROR("start: module name is empty");
        return 1;
    }

    if (g_tdb || g_transport_manager) {
        stop();
    }

    g_name = module_name;
    g_running = true;
    get_connection_cache().clear();

    // Имя модуля нужно и для handshake, и для ответа через Cache.
    get_link_manager().set_local_module(g_name);

    // Имитация ядра: публикуем module -> endpoint, чтобы другие процессы
    // могли найти нас через server_api::locate().
    server_api::register_module(g_name, "/tmp/loom_" + g_name + ".sock");

    // Методы можно регистрировать до start(); здесь они попадают в реестр.
    flush_pending_methods(g_name);

    loom_event_manager::start_manager();

    g_tdb = std::make_unique<TDB>(loom_rpc::receive_response);
    if (!g_tdb->init(9, 4)) {
        METALOG_ERROR("не удалось инициализировать TDB");
        g_tdb.reset();
        return 1;
    }

    // Вся входящая бизнес-логика идёт через этот handler.
    g_tdb->set_handler(loom_packet_handler);

    g_transport_manager = std::make_unique<TransportManager>();

    auto uds_transport = std::make_unique<UdsTransport>(
        "/tmp/loom_" + g_name + ".sock");
    if (!g_transport_manager->register_transport(std::move(uds_transport))) {
        METALOG_ERROR("не удалось зарегистрировать UDS-транспорт");
        g_tdb->shutdown();
        g_tdb.reset();
        g_transport_manager.reset();
        return 1;
    }

    g_transport_manager->set_tdb(g_tdb.get());
    // OUTLINK отправляет готовые кадры через TransportManager.
    g_tdb->set_sender([manager = g_transport_manager.get()](
                          OutgoingPacket&& packet) {
        return manager->send_outgoing(std::move(packet));
    });

    loom_rpc::pending_calls().start();
    if (!g_transport_manager->start_all()) {
        METALOG_ERROR("не удалось запустить транспорт");
        g_transport_manager->stop_all();
        loom_rpc::pending_calls().shutdown();
        g_tdb->shutdown();
        g_tdb.reset();
        g_transport_manager.reset();
        return 1;
    }

    METALOG_INFO(std::string("module ") + g_name + " has started");

    return 0;
}

uint8_t stop() {
    METALOG_INFO(std::string("остановка модуля ") + g_name);

    // 1. Перестаём принимать новые пакеты, но транспорт ещё может отправлять.
    if (g_transport_manager) {
        g_transport_manager->stop_all();
    }

    // 2. Будим все ожидающие RPC: ответов уже не будет.
    loom_rpc::pending_calls().shutdown();

    // 3. TDB дочитывает воркеров и очередь OUTLINK.
    if (g_tdb) {
        g_tdb->shutdown();
        g_tdb.reset();
    }

    g_transport_manager.reset();
    get_link_manager().close_all();
    get_link_manager().set_local_module("");
    get_connection_cache().clear();
    server_api::unregister_module(g_name);
    g_running = false;
    return 0;
}

uint8_t use_module_direct(const char* module_name) {
    return use_module_direct_priority(module_name, 0);
}

uint8_t use_module_direct_priority(const char* module_name, int priority) {
    if (module_name == nullptr || *module_name == '\0') {
        METALOG_ERROR("use_module_direct: module name is empty");
        return 1;
    }

    const std::string name(module_name);
    METALOG_INFO(std::string("идет коннект к модулю ") + name +
                 (priority ? " (priority)" : ""));

    std::string socket_path = server_api::locate(name);
    if (socket_path.empty()) {
        METALOG_ERROR(std::string("не удалось найти модуль ") + name);
        return 1;
    }

    save_endpoint_to_cache(name, socket_path);

    const bool direct = priority != 0;
    server_api::set_priority(name, direct);
    get_connection_cache().set_priority(name, direct);

    // Если Link уже открыт, смена режима заставит LinkManager переоткрыть
    // его с новым handshake.
    endpoint ep;
    if (get_cache().get(name, ep)) {
        get_link_manager().set_priority(ep, direct);
    }

    return 0;
}

uint8_t use_module(const char* description, const char* as_name) {
    METALOG_INFO(std::string("поиск по описанию: ") + description + "\nсохранить как: " + as_name);

    std::string socket_path = server_api::locate_by_description(description);
    if (socket_path.empty()) return 1;

    save_endpoint_to_cache(as_name, socket_path);
    return 0;
}

void module_send(const char* module_name, const char* message){
    if (!g_transport_manager) {
        METALOG_ERROR("module_send: транспорт не запущен");
        return;
    }
    if (module_name == nullptr || message == nullptr) return;

    // module_send — это односторонний вызов метода "message" с одним
    // строковым аргументом. Кодируем его тем же кодеком, что и loom_call.
    const std::string text(message);
    const std::vector<std::string> params{"string"};
    const void* args[] = {&text};

    std::vector<uint8_t> payload;
    if (!loom_rpc::encode_args(params, args, payload)) {
        METALOG_ERROR("module_send: failed to encode message");
        return;
    }

    std::string body;
    if (!payload.empty()) {
        body.assign(reinterpret_cast<const char*>(payload.data()),
                    payload.size());
    }

    g_transport_manager->send_async(module_name, "message", body);
}

void register_method(const char* method_name, const char* signature, void* impl){
    if (method_name == nullptr || signature == nullptr || impl == nullptr) {
        METALOG_ERROR("register_method: invalid arguments");
        return;
    }

    std::lock_guard<std::mutex> lock(g_registration_mutex);

    // До start() имя модуля ещё неизвестно — откладываем до запуска.
    if (g_name.empty()) {
        g_pending_methods.push_back({method_name, signature, impl});
        return;
    }

    register_method_impl(g_name.c_str(), method_name, signature, impl);
}

int loom_call(const char* module, const char* method, const char* signature,
              const void* const* args, void* result, int timeout_ms) {
    if (module == nullptr || method == nullptr || signature == nullptr) {
        return -1;
    }
    if (!g_transport_manager) {
        METALOG_ERROR("loom_call: транспорт не запущен");
        return -2;
    }

    std::vector<std::string> params;
    std::string ret_type;
    if (!loom_rpc::parse_signature(signature, params, ret_type)) {
        METALOG_ERROR(std::string("loom_call: invalid signature: ") + signature);
        return -3;
    }

    std::vector<uint8_t> payload;
    if (!loom_rpc::encode_args(params, args, payload) ||
        payload.size() > gwp::MAX_DATA_LEN - loom_rpc::REPLY_ENVELOPE_HEADER_SIZE) {
        METALOG_ERROR("loom_call: failed to encode arguments");
        return -4;
    }

    loom_rpc::PendingCall pending(loom_rpc::pending_calls());
    const loom_rpc::ReplyTo reply_to = pending.reply_to();
    if (!reply_to.valid()) return -2;

    const std::string body = loom_rpc::encode_reply_envelope(reply_to, payload);
    const int rc = g_transport_manager->send_request(module, method, body);
    if (rc < 0) {
        return rc;
    }

    loom_rpc::Result response;
    if (!pending.wait(response, timeout_ms)) {
        return -5;
    }

    if (response.status != static_cast<uint8_t>(gwp::StatusCode::OK)) {
        return -6;
    }

    if (loom_rpc::normalize_type(ret_type) != "void") {
        if (result == nullptr) return -7;
        if (!loom_rpc::decode_value(ret_type, response.data.data(),
                                    response.data.size(), result)) {
            return -8;
        }
    }

    return 0;
}

int loom_listen(const char* module_name, const char* method_name, int interval_ms){
    (void)module_name;
    (void)method_name;
    (void)interval_ms;
    return 0;
}

void unlisten(int listener_id){
    (void)listener_id;
}
