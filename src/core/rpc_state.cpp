#include "rpc_state.h"

#include "../gateway/TDB/buffer/buffer.hpp"
#include "../protocol/gwp.h"
#include "../../metalogger/metalogger.h"

#include <atomic>
#include <chrono>
#include <utility>

namespace loom_rpc {

namespace {

std::atomic<uint64_t> next_worker_handle{1};

} // namespace

PendingCall::PendingCall(PendingCalls& owner) : owner_(owner) {
    // Один handle на вызывающий поток, включая вызовы вне пула TDB.
    thread_local const uint64_t worker_handle =
        next_worker_handle.fetch_add(1, std::memory_order_relaxed);
    thread_local uint64_t call_seq = 0;

    std::lock_guard<std::mutex> lock(owner_.mutex_);
    if (owner_.accepting_ && owner_.pending_.emplace(worker_handle, this).second) {
        reply_to_ = {worker_handle, ++call_seq};
    }
}

PendingCall::~PendingCall() {
    if (reply_to_.valid()) {
        std::lock_guard<std::mutex> lock(owner_.mutex_);
        owner_.pending_.erase(reply_to_.worker_handle);
    }
}

bool PendingCall::wait(Result& out, int timeout_ms) {
    if (!reply_to_.valid()) return false;

    std::unique_lock<std::mutex> lock(owner_.mutex_);
    const auto timeout =
        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 5000);
    const bool ready = cv_.wait_for(lock, timeout, [this] { return done_; });
    done_ = true;
    if (ready) {
        out = std::move(result_);
    }
    return ready;
}

void PendingCalls::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = true;
}

void PendingCalls::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    for (const auto& entry : pending_) {
        PendingCall& call = *entry.second;
        if (call.done_) continue;

        call.result_.status = static_cast<uint8_t>(gwp::StatusCode::ERROR);
        call.done_ = true;
        call.cv_.notify_one();
    }
}

bool PendingCalls::try_complete(ReplyTo reply_to, uint8_t status,
                                const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = pending_.find(reply_to.worker_handle);
    if (it == pending_.end()) return false;

    PendingCall& call = *it->second;
    if (call.done_ || call.reply_to_.call_seq != reply_to.call_seq) return false;

    if (size != 0) {
        call.result_.data.assign(data, data + size);
    }
    call.result_.status = status;
    call.done_ = true;
    call.cv_.notify_one();
    return true;
}

PendingCalls& pending_calls() {
    static PendingCalls instance;
    return instance;
}

bool receive_response(const IncomingPacket& packet) {
    const auto& frame = packet.data;
    gwp::GWPHeader header;
    if (!gwp::parse_header(frame.data(), frame.size(), header) ||
        header.type != gwp::PacketType::RESPONSE) {
        return false;
    }

    const size_t data_offset = gwp::HEADER_SIZE + header.method_name_len;
    ReplyEnvelope envelope;
    if (frame.size() != data_offset + header.data_len ||
        !decode_reply_envelope(frame.data() + data_offset, header.data_len,
                               envelope)) {
        METALOG_WARNING("loom: invalid RPC response envelope");
        return true;
    }

    pending_calls().try_complete(envelope.reply_to, header.status,
                                 envelope.data, envelope.size);
    return true;
}

} // namespace loom_rpc
