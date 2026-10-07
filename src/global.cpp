#include "global.h"

std::atomic<bool> g_running{true};
std::atomic<bool> g_initialized{false};

void init_globals() {
    g_running = true;
    g_initialized = true;
}