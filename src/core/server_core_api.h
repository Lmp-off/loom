#ifndef SERVER_CORE_API_H
#define SERVER_CORE_API_H

#include <string>

// server_core_api — имитация ядра для тестов и примеров.
//
// Архитектурные инварианты:
// - ядро хранит directory module -> endpoint/priority в общем файле
//   /tmp/loom_core.registry, поэтому разные процессы видят регистрацию друг друга;
// - формат строки: name<TAB>address<TAB>priority;
// - доступ сериализуется flock(), запись идёт под LOCK_EX;
// - start() регистрирует модуль, stop() снимает;
// - при отсутствии записи используется fallback /tmp/loom_<name>.sock;
// - это не production-ядро: нет heartbeat, TTL и авторизации.

namespace server_api {

struct ModuleInfo {
    std::string name;
    std::string address;
    bool priority = false;
};

void register_module(const std::string& name, const std::string& address);
void unregister_module(const std::string& name);

bool locate_module(const std::string& name, ModuleInfo& out);

void set_priority(const std::string& name, bool priority);
bool is_priority(const std::string& name);

std::string locate(std::string desc);
std::string locate_by_description(std::string desc);

void clear_registry();

} // namespace server_api

#endif
