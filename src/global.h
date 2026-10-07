#ifndef GLOBAL_H
#define GLOBAL_H

#include <atomic>

extern std::atomic<bool> g_running;
extern std::atomic<bool> g_initialized;

void init_globals();

#endif