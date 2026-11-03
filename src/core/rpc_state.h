#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

// Ожидающие ответа RPC-вызовы.
//
// Вызывающий поток регистрирует packet_id, отправляет REQUEST и ждёт
// completion. Воркер, принявший RESPONSE, находит запись по packet_id и
// будит ожидающего. Один экземпляр на процесс.
namespace loom_rpc {

struct Result {
    uint8_t status = 0;
    std::vector<uint8_t> data;
};

class PendingCalls {
public:
    uint32_t next_id();
    void add(uint32_t id);
    void cancel(uint32_t id);

    // false — запись не найдена (например, вызов уже отменён по таймауту).
    bool complete(uint32_t id, uint8_t status, std::vector<uint8_t> data);

    // false — таймаут или запись исчезла.
    bool wait(uint32_t id, Result& out, int timeout_ms);

    // Будит все ожидающие вызовы с ошибкой. Используется при stop().
    void shutdown();

private:
    struct Entry {
        bool done = false;
        Result result;
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<uint32_t, Entry> pending_;
    std::atomic<uint32_t> next_id_{1};
};

PendingCalls& pending_calls();

} // namespace loom_rpc
