#include "manager.h"
#include "global.h"
#include <thread>
#include <chrono>
#include <metalogger.h>

namespace loom_event_manager{

static void process_pending_registrations() {
    // TODO: обработать отложенные регистрации методов
    METALOG_DEBUG("Processing pending registrations...");
}

static void send_heartbeat() {
    METALOG_DEBUG("Sending heartbeat...");
}

static void manager_loop() {
    while (g_running) {
        if (g_flags & FLAG_METHOD_REGISTRATION) {
            process_pending_registrations();
            g_flags &= ~FLAG_METHOD_REGISTRATION;
        }
        
        if (g_flags & FLAG_HEARTBEAT) {
            send_heartbeat();
            g_flags &= ~FLAG_HEARTBEAT;
        }
        
        g_last_heartbeat = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void start_manager() {
    g_running = true;

    std::thread g_manager_thread = std::thread(manager_loop);
    g_manager_thread.detach();
    
    METALOG_INFO("Manager thread started");
}

void stop_manager(){
    g_running = false;
    METALOG_INFO("Manager thread stopped");
}
}