//местный чан с говном
#pragma once

#include <string>
#include <functional>
#include <future>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace wkr {

class RPCCall {
public:
    struct PendingCall {
        std::promise<std::string> promise;
        std::string module;
        std::string method;
        std::string data;
        int timeout_ms;
    };

    class Manager {
    public:
        static Manager& instance();
        
        // Отправить RPC запрос и ждать ответ
        std::string call(const std::string& module, 
                        const std::string& method,
                        const std::string& data,
                        int timeout_ms = 5000);
        
        // Зарегистрировать ожидающий вызов
        uint32_t register_pending(const std::string& module,
                                  const std::string& method,
                                  const std::string& data,
                                  int timeout_ms);
        
        // Обработать ответ (вызывается из listener)
        void on_response(uint32_t id, const std::string& result);
        
        // Отмена по таймауту
        void on_timeout(uint32_t id);
        
    private:
        std::unordered_map<uint32_t, PendingCall> pending_;
        std::mutex mutex_;
        std::atomic<uint32_t> next_id_{1};
    };

};
}