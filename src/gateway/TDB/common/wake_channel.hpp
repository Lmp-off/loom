#pragma once

#include <condition_variable>
#include <mutex>

// Общий канал пробуждения для пула потребителей одной очереди.
// Очередь lock-free, поэтому mutex/cv нужны только для сна и пробуждения.
struct WakeChannel {
    std::mutex mutex;
    std::condition_variable cv;

    void notify_one() { cv.notify_one(); }
    void notify_all() { cv.notify_all(); }
};
