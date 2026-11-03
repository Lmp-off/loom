#ifndef GLOBAL_H
#define GLOBAL_H

#include <atomic>
#include <chrono>
#include <string>
#include "gateway/TDB/tdb.hpp"

// Глобальное состояние процесса Loom.
//
// Архитектурные инварианты:
// - g_running и g_flags — атомарные переключатели жизненного цикла;
// - g_tdb — единственный TDB процесса, создаётся в start() и сбрасывается в stop();
// - g_last_heartbeat обновляет manager-поток;
// - поля g_last_* используются только для диагностики и не влияют на маршрутизацию;
// - глобальное состояние не заменяет владение компонентами: TDB, TransportManager
//   и LinkManager живут в своих модулях.

extern std::atomic<bool> g_running;
extern std::unique_ptr<TDB> g_tdb;
extern std::atomic<uint32_t> g_flags;
extern std::chrono::steady_clock::time_point g_last_heartbeat;

extern std::string g_last_sender;
extern std::string g_last_receiver;
extern std::string g_last_message;
extern std::atomic<bool> g_message_received;

constexpr uint32_t FLAG_RUNNING = 1 << 0;
constexpr uint32_t FLAG_METHOD_REGISTRATION = 1 << 1;
constexpr uint32_t FLAG_HEARTBEAT = 1 << 2;

#endif
