#include "metalogger.h"
#include <iostream>
#include <ctime>

namespace metalog {

static bool g_logger_active = true;

void set_logger_active(bool active) {
    g_logger_active = active;
}

bool is_logger_active() {
    return g_logger_active;
}

const char* get_level_name(log_level lv) {
    switch (lv) {
        case LOG_ERROR:   return "[ERROR]";
        case LOG_WARNING: return "[WARN]";
        case LOG_INFO:    return "[INFO]";
        case LOG_DEBUG:   return "[DEBUG]";
        case LOG_TRACE:   return "[TRACE]";
        default:          return "[????]";
    }
}

const char* get_color(log_level lv) {
    switch (lv) {
        case LOG_ERROR:   return "\033[31m";
        case LOG_WARNING: return "\033[33m";
        case LOG_INFO:    return "\033[32m";
        case LOG_DEBUG:   return "\033[36m";
        case LOG_TRACE:   return "\033[90m";
        default:          return "\033[0m";
    }
}

void write_log(const std::string& s, log_level lv) {
    if (!g_logger_active) return;
    
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
    
    std::cout << "[" << time_buf << "] "
              << get_color(lv) << get_level_name(lv) << "\033[0m "
              << s << std::endl;
}

}