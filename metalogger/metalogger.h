#ifndef METALOGGER_H
#define METALOGGER_H

#include <string>

#define METALOG_ERROR(msg) metalog::write_log(msg, metalog::LOG_ERROR)
#define METALOG_WARNING(msg) metalog::write_log(msg, metalog::LOG_WARNING)
#define METALOG_INFO(msg) metalog::write_log(msg, metalog::LOG_INFO)
#define METALOG_DEBUG(msg) metalog::write_log(msg, metalog::LOG_DEBUG)
#define METALOG_TRACE(msg) metalog::write_log(msg, metalog::LOG_TRACE)

namespace metalog {

enum log_level {
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE
};

void set_logger_active(bool active);
bool is_logger_active();
void write_log(const std::string& s, log_level lv);

}

#endif