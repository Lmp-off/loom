#include <loom.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

int add(int a, int b) {
    std::printf("[calc_server] add(%d, %d)\n", a, b);
    return a + b;
}

std::string greet(const std::string& name) {
    std::printf("[calc_server] greet(%s)\n", name.c_str());
    return "Hello, " + name + "!";
}

void log_message(const std::string& message) {
    std::printf("[calc_server] message: %s\n", message.c_str());
}
} // namespace

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    if (start("calc_server") != 0) {
        std::fprintf(stderr, "failed to start calc_server\n");
        return 1;
    }

    register_method("add", "int,int->int", (void*)add);
    register_method("greet", "string->string", (void*)greet);
    register_method("message", "string->void", (void*)log_message);

    std::printf("[calc_server] ready on /tmp/loom_calc_server.sock\n");
    std::printf("[calc_server] press Ctrl+C to stop\n");

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stop();
    std::printf("[calc_server] stopped\n");
    return 0;
}
