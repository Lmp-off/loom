#pragma once

#include "rpc_codec.h"

#include <condition_variable>
#include <map>
#include <mutex>

struct IncomingPacket;

namespace loom_rpc {

struct Result {
    uint8_t status = 0;
    std::vector<uint8_t> data;
};

class PendingCalls;

// Один синхронный вызов. Живёт в стеке вызывающего потока.
class PendingCall {
public:
    explicit PendingCall(PendingCalls& owner);
    ~PendingCall();

    ReplyTo reply_to() const { return reply_to_; }
    bool wait(Result& out, int timeout_ms);

private:
    friend class PendingCalls;

    PendingCalls& owner_;
    ReplyTo reply_to_;
    std::condition_variable cv_;
    bool done_ = false;
    Result result_;
};

// Один реестр на процесс. Его мьютекс защищает регистрацию, ожидание,
// тайм-аут и завершение вызова.
class PendingCalls {
public:
    void start();
    void shutdown();
    bool try_complete(ReplyTo reply_to, uint8_t status,
                      const uint8_t* data, size_t size);

private:
    friend class PendingCall;

    bool accepting_ = false;
    std::mutex mutex_;
    std::map<uint64_t, PendingCall*> pending_;
};

PendingCalls& pending_calls();

// Входной хук Core. true означает, что RESPONSE обработан до UPLINK.
bool receive_response(const IncomingPacket& packet);

} // namespace loom_rpc
