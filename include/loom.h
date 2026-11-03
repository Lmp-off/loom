#ifndef LOOM_H
#define LOOM_H

#include <stdint.h>
#include <stddef.h>

// Loom — публичный C-ABI для модулей-процессов.
//
// Архитектура:
// - start() поднимает TDB и UDS-транспорт, регистрирует модуль в имитации ядра;
// - register_method() добавляет метод в ModuleRegistry;
// - loom_call()/module_send() уходят в TransportManager: ConnectionCache ->
//   Cache -> ядро -> LinkManager;
// - входящие кадры проходят Transport -> TDB -> Worker -> PacketHandler;
// - use_module_direct_priority() включает direct/priority путь;
// - C++-обёртка loom::call<R>() над loom_call() находится ниже.
//
// Публичный контракт остаётся C-совместимым; C++-удобства не меняют ABI.

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*generic_callback_t)(void* args, void* userdata);

uint8_t start(const char* module_name);
uint8_t stop(void);

uint8_t use_module_direct(const char* module_name);

// То же, но с явным режимом direct/priority.
// priority != 0: пакеты модуля идут в приоритетную очередь TDB, а на
// принимающей стороне для соединения создаётся выделенный Worker
// (если лимит ресурсов позволяет).
uint8_t use_module_direct_priority(const char* module_name, int priority);

uint8_t use_module(const char* description, const char* as_name);
void module_send(const char* module_name, const char* message);

void register_method(const char* method_name,
                     const char* signature,
                     void* impl);

// Синхронный вызов метода другого модуля через TDB.
//
// signature: "int,int->int" / "string->string" / "void->void".
// args:      массив указателей на аргументы в порядке signature.
//            Для string ожидается const std::string* (C++).
// result:    указатель на память результата; для string — std::string*.
//            Для void можно передать nullptr.
// timeout_ms: <= 0 — значение по умолчанию (5000 мс).
//
// Возвращает 0 при успехе, отрицательный код при ошибке.
int loom_call(const char* module, const char* method, const char* signature,
              const void* const* args, void* result, int timeout_ms);

int loom_listen(const char* module_name, const char* method_name, int interval_ms);
void unlisten(int listener_id);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

// C++-удобство: use_module_direct("calc", true).
inline uint8_t use_module_direct(const char* module_name, bool priority) {
    return use_module_direct_priority(module_name, priority ? 1 : 0);
}

#include <array>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

// Удобная C++-обёртка над loom_call:
//   int sum = loom::call<int>("calc", "add", 2, 3);
//   std::string s = loom::call<std::string>("calc", "greet", "world");
//   loom::call<void>("logger", "log", "hello");
namespace loom {
namespace detail {

template <typename T>
struct wire_type {
    static_assert(sizeof(T) == 0, "loom::call: unsupported type");
};

template <> struct wire_type<int> {
    static const char* name() { return "int"; }
};
template <> struct wire_type<double> {
    static const char* name() { return "double"; }
};
template <> struct wire_type<float> {
    static const char* name() { return "float"; }
};
template <> struct wire_type<bool> {
    static const char* name() { return "bool"; }
};
template <> struct wire_type<std::string> {
    static const char* name() { return "string"; }
};
template <> struct wire_type<void> {
    static const char* name() { return "void"; }
};

template <typename T>
struct store_type {
    using type = T;
};
template <> struct store_type<const char*> {
    using type = std::string;
};
template <> struct store_type<char*> {
    using type = std::string;
};

template <typename T>
using stored_t = typename store_type<std::decay_t<T>>::type;

template <typename T>
stored_t<T> store(T&& value) {
    using Stored = stored_t<T>;
    if constexpr (std::is_same_v<Stored, std::string>) {
        return std::string(std::forward<T>(value));
    } else {
        return static_cast<Stored>(std::forward<T>(value));
    }
}

template <typename T>
const char* wire_name() {
    using Plain = std::decay_t<T>;
    if constexpr (std::is_same_v<Plain, std::string> ||
                  std::is_same_v<Plain, const char*> ||
                  std::is_same_v<Plain, char*>) {
        return "string";
    } else {
        return wire_type<Plain>::name();
    }
}

template <typename... Args>
std::string make_signature(const char* ret) {
    std::string signature;
    size_t index = 0;
    ((signature += (index++ == 0 ? "" : ","),
      signature += wire_name<Args>()),
     ...);
    signature += "->";
    signature += ret;
    return signature;
}

} // namespace detail

template <typename R, typename... Args>
R call(const char* module, const char* method, Args&&... args) {
    using PlainR = std::decay_t<R>;
    static_assert(!std::is_same_v<PlainR, const char*> &&
                      !std::is_same_v<PlainR, char*>,
                  "loom::call: use std::string for string results");

    auto stored = std::make_tuple(
        detail::store<Args>(std::forward<Args>(args))...);

    std::array<const void*, sizeof...(Args)> pointers{};
    std::apply(
        [&pointers](auto&... values) {
            size_t index = 0;
            ((pointers[index++] = static_cast<const void*>(&values)), ...);
        },
        stored);

    const std::string signature =
        detail::make_signature<Args...>(detail::wire_name<R>());

    if constexpr (std::is_void_v<R>) {
        const int rc = loom_call(module, method, signature.c_str(),
                                 pointers.data(), nullptr, 5000);
        if (rc != 0) {
            throw std::runtime_error("loom::call failed: " +
                                     std::to_string(rc));
        }
        return;
    } else {
        R result{};
        const int rc = loom_call(module, method, signature.c_str(),
                                 pointers.data(), &result, 5000);
        if (rc != 0) {
            throw std::runtime_error("loom::call failed: " +
                                     std::to_string(rc));
        }
        return result;
    }
}

} // namespace loom

#endif // __cplusplus

#endif // LOOM_H
