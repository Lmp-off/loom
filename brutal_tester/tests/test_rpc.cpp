#include "../../include/loom.h"

#include "../../src/core/server_core_api.h"
#include "../../src/discovery/cache.h"
#include "../../src/transport/connection_cache.hpp"
#include "../../src/transport/link.hpp"

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

namespace {

constexpr const char* kServerSocket = "/tmp/loom_rpc_server.sock";
constexpr const char* kClientSocket = "/tmp/loom_rpc_client.sock";

int rpc_add(int a, int b) {
    return a + b;
}

int rpc_sub(int a, int b) {
    return a - b;
}

std::string rpc_greet(const std::string& name) {
    return "hi " + name;
}

void rpc_message(const std::string&) {
}

int run_server(int ready_fd, int stop_fd) {
    get_cache().clear();
    get_link_manager().close_all();

    if (start("rpc_server") != 0) return 1;

    register_method("add", "int,int->int", (void*)rpc_add);
    register_method("sub", "int,int->int", (void*)rpc_sub);
    register_method("greet", "string->string", (void*)rpc_greet);
    register_method("message", "string->void", (void*)rpc_message);

    const char ready = 'r';
    if (write(ready_fd, &ready, 1) != 1) {
        stop();
        return 2;
    }

    char stop_signal = 0;
    if (read(stop_fd, &stop_signal, 1) != 1) {
        stop();
        return 3;
    }

    stop();
    return 0;
}

bool wait_child(pid_t pid, int timeout_ms, int& status) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) return true;
        if (result < 0) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return false;
}

} // namespace

int test_rpc_all() {
    unlink(kServerSocket);
    unlink(kClientSocket);
    server_api::clear_registry();
    get_cache().clear();
    get_link_manager().close_all();
    get_connection_cache().clear();

    int ready_pipe[2] = {-1, -1};
    int stop_pipe[2] = {-1, -1};
    if (pipe(ready_pipe) != 0 || pipe(stop_pipe) != 0) {
        return 1;
    }

    const pid_t pid = fork();
    if (pid < 0) return 2;

    if (pid == 0) {
        close(ready_pipe[0]);
        close(stop_pipe[1]);
        const int result = run_server(ready_pipe[1], stop_pipe[0]);
        close(ready_pipe[1]);
        close(stop_pipe[0]);
        _exit(result);
    }

    close(ready_pipe[1]);
    close(stop_pipe[0]);

    char ready = 0;
    const ssize_t ready_bytes = read(ready_pipe[0], &ready, 1);
    close(ready_pipe[0]);

    if (ready_bytes != 1) {
        int status = 0;
        wait_child(pid, 2000, status);
        unlink(kServerSocket);
        unlink(kClientSocket);
        return 3;
    }

    int result = 0;
    bool client_started = false;
    if (start("rpc_client") != 0) {
        result = 4;
    } else {
        client_started = true;
    }

    // Клиент хочет direct/priority путь к серверу: пакеты уходят в
    // приоритетную очередь, а сервер создаёт выделенный Worker.
    if (result == 0) {
        if (use_module_direct("rpc_server", true) != 0) {
            result = 13;
        } else if (!server_api::is_priority("rpc_server") ||
                   !get_connection_cache().is_priority("rpc_server")) {
            result = 14;
        }
    }

    if (result == 0) {
        int a = 2;
        int b = 3;
        int sum = 0;
        const void* args[] = {&a, &b};

        if (loom_call("rpc_server", "add", "int,int->int", args, &sum,
                      3000) != 0 ||
            sum != 5) {
            result = 5;
        }
    }

    if (result == 0) {
        int a = 10;
        int b = 3;
        int difference = 0;
        const void* args[] = {&a, &b};

        // Второй метод с той же сигнатурой: проверяем, что JIT-адаптеры
        // не переиспользуют чужой user_func.
        if (loom_call("rpc_server", "sub", "int,int->int", args, &difference,
                      3000) != 0 ||
            difference != 7) {
            result = 12;
        }
    }

    if (result == 0) {
        std::string name = "loom";
        std::string greeting;
        const void* args[] = {&name};

        if (loom_call("rpc_server", "greet", "string->string", args,
                      &greeting, 3000) != 0 ||
            greeting != "hi loom") {
            result = 6;
        }
    }

    if (result == 0) {
        try {
            const int sum = loom::call<int>("rpc_server", "add", 10, 20);
            if (sum != 30) result = 7;

            const std::string greeting =
                loom::call<std::string>("rpc_server", "greet", "helper");
            if (greeting != "hi helper") result = 8;
        } catch (...) {
            result = 9;
        }
    }

    const char stop_signal = 's';
    (void)write(stop_pipe[1], &stop_signal, 1);
    close(stop_pipe[1]);

    int status = 0;
    if (!wait_child(pid, 5000, status)) {
        result = 10;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        result = 11;
    }

    if (client_started) {
        stop();
    }
    get_link_manager().close_all();
    get_cache().clear();
    unlink(kServerSocket);
    unlink(kClientSocket);

    return result;
}
