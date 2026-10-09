#ifndef GLOBAL_H
#define GLOBAL_H

#include <atomic>
#include <chrono>
#include <string>
#include <TDB/tdb.hpp>

extern std::atomic<bool> g_running;

// TDB
extern std::unique_ptr<TDB> g_tdb;
extern std::atomic<uint32_t> g_flags;
extern std::chrono::steady_clock::time_point g_last_heartbeat;

// Для логирования отправки/получения
extern std::string g_last_sender;
extern std::string g_last_receiver;
extern std::string g_last_message;
extern std::atomic<bool> g_message_received;

constexpr uint32_t FLAG_RUNNING              = 1 << 0;
constexpr uint32_t FLAG_METHOD_REGISTRATION  = 1 << 1;
constexpr uint32_t FLAG_HEARTBEAT            = 1 << 2;

#endif
