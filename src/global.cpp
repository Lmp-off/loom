#include "global.h"

std::atomic<bool> g_running{true};
std::atomic<uint32_t> g_flags{FLAG_RUNNING};
std::chrono::steady_clock::time_point g_last_heartbeat = std::chrono::steady_clock::now();

// Для логирования отправки/получения
std::string g_last_sender;
std::string g_last_receiver;
std::string g_last_message;
std::atomic<bool> g_message_received{false};
