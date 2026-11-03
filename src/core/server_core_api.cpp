#include "server_core_api.h"

#include "../metalogger/metalogger.h"

#include <cerrno>
#include <fcntl.h>
#include <map>
#include <sys/file.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace server_api {

namespace {

constexpr const char* kRegistryPath = "/tmp/loom_core.registry";

std::string fallback_address(const std::string& name) {
    return "/tmp/loom_" + name + ".sock";
}

bool lock_fd(int fd, int operation) {
    while (::flock(fd, operation) != 0) {
        if (errno != EINTR) return false;
    }
    return true;
}

std::string read_fd(int fd) {
    std::string content;
    char buffer[4096];
    off_t offset = 0;

    for (;;) {
        const ssize_t count = ::pread(fd, buffer, sizeof(buffer), offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (count == 0) break;
        content.append(buffer, static_cast<size_t>(count));
        offset += count;
    }
    return content;
}

std::map<std::string, ModuleInfo> parse(const std::string& content) {
    std::map<std::string, ModuleInfo> modules;

    size_t start = 0;
    while (start <= content.size()) {
        const size_t end = content.find('\n', start);
        const std::string line =
            content.substr(start, end == std::string::npos
                                      ? std::string::npos
                                      : end - start);
        if (!line.empty()) {
            const size_t first = line.find('\t');
            const size_t second =
                first == std::string::npos
                    ? std::string::npos
                    : line.find('\t', first + 1);

            if (first != std::string::npos && second != std::string::npos) {
                ModuleInfo info;
                info.name = line.substr(0, first);
                info.address = line.substr(first + 1, second - first - 1);
                info.priority = line.substr(second + 1) == "1";
                if (!info.name.empty()) {
                    modules[info.name] = std::move(info);
                }
            }
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }

    return modules;
}

std::string serialize(const std::map<std::string, ModuleInfo>& modules) {
    std::string content;
    for (const auto& entry : modules) {
        content += entry.second.name;
        content += '\t';
        content += entry.second.address;
        content += '\t';
        content += entry.second.priority ? '1' : '0';
        content += '\n';
    }
    return content;
}

int open_registry(int flags) {
    return ::open(kRegistryPath, flags | O_CLOEXEC, 0644);
}

bool write_registry(int fd,
                    const std::map<std::string, ModuleInfo>& modules) {
    const std::string content = serialize(modules);

    if (::ftruncate(fd, 0) != 0) return false;

    size_t offset = 0;
    while (offset < content.size()) {
        const ssize_t count =
            ::pwrite(fd, content.data() + offset, content.size() - offset,
                     static_cast<off_t>(offset));
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (count == 0) return false;
        offset += static_cast<size_t>(count);
    }
    return true;
}

// Выполняет read-modify-write под эксклюзивной блокировкой.
template <typename Fn>
bool update_registry(Fn&& fn) {
    const int fd = open_registry(O_RDWR | O_CREAT);
    if (fd < 0) return false;

    if (!lock_fd(fd, LOCK_EX)) {
        ::close(fd);
        return false;
    }

    auto modules = parse(read_fd(fd));
    fn(modules);
    const bool ok = write_registry(fd, modules);

    lock_fd(fd, LOCK_UN);
    ::close(fd);
    return ok;
}

} // namespace

void register_module(const std::string& name, const std::string& address) {
    if (name.empty()) return;

    const std::string resolved =
        address.empty() ? fallback_address(name) : address;

    update_registry([&](std::map<std::string, ModuleInfo>& modules) {
        auto it = modules.find(name);
        const bool priority =
            it == modules.end() ? false : it->second.priority;
        modules[name] = ModuleInfo{name, resolved, priority};
    });

    METALOG_DEBUG("core: registered module " + name + " -> " + resolved);
}

void unregister_module(const std::string& name) {
    if (name.empty()) return;

    update_registry([&](std::map<std::string, ModuleInfo>& modules) {
        modules.erase(name);
    });

    METALOG_DEBUG("core: unregistered module " + name);
}

bool locate_module(const std::string& name, ModuleInfo& out) {
    if (name.empty()) return false;

    bool found = false;
    ModuleInfo info;

    const int fd = open_registry(O_RDONLY);
    if (fd >= 0) {
        if (lock_fd(fd, LOCK_SH)) {
            auto modules = parse(read_fd(fd));
            auto it = modules.find(name);
            if (it != modules.end()) {
                info = it->second;
                found = true;
            }
            lock_fd(fd, LOCK_UN);
        }
        ::close(fd);
    }

    if (!found) {
        // Совместимость со старой заглушкой: если ядро ничего не знает,
        // используем детерминированный локальный путь.
        info = ModuleInfo{name, fallback_address(name), false};
    }

    out = std::move(info);
    return true;
}

void set_priority(const std::string& name, bool priority) {
    if (name.empty()) return;

    update_registry([&](std::map<std::string, ModuleInfo>& modules) {
        auto it = modules.find(name);
        if (it == modules.end()) {
            modules[name] = ModuleInfo{name, fallback_address(name), priority};
        } else {
            it->second.priority = priority;
        }
    });
}

bool is_priority(const std::string& name) {
    ModuleInfo info;
    if (!locate_module(name, info)) return false;
    return info.priority;
}

std::string locate(std::string desc) {
    ModuleInfo info;
    if (!locate_module(desc, info)) return std::string();
    return info.address;
}

std::string locate_by_description(std::string desc) {
    return locate(std::move(desc));
}

void clear_registry() {
    const int fd = open_registry(O_RDWR | O_CREAT);
    if (fd < 0) return;

    if (lock_fd(fd, LOCK_EX)) {
        ::ftruncate(fd, 0);
        lock_fd(fd, LOCK_UN);
    }
    ::close(fd);
}

} // namespace server_api
