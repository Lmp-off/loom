/*
g++ -std=c++17 \
src/core/loom.cpp \
src/discovery/cache.cpp \
src/discovery/capabilities.cpp \
src/core/server_core_api.cpp \
src/connection/local/uds.cpp \
src/core/jit_generator.cpp \
metalogger/metalogger.cpp \
src/tests/main.cpp src/global.cpp \
-o test -pthread -ldl

*/

/*
---------------------
!AUTO GENERATED TEST!
---------------------
*/

#include "../../include/loom.h"
#include "../discovery/capabilities.h"
#include "../core/jit_generator.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <string>
#include <thread>
#include "../../metalogger/metalogger.h"

bool keep_running = true;

void signal_handler(int sig) {
    std::cout << "\n🛑 Получен сигнал " << sig << ", остановка..." << std::endl;
    stop();
    keep_running = false;
}

// ============================================
// ТЕСТОВЫЕ ФУНКЦИИ ДЛЯ JIT
// ============================================

// 1. Простая функция сложения
int test_add(int a, int b) {
    std::cout << "[JIT] test_add(" << a << ", " << b << ") = " << (a + b) << std::endl;
    return a + b;
}

// 2. Функция умножения
int test_multiply(int a, int b) {
    std::cout << "[JIT] test_multiply(" << a << ", " << b << ") = " << (a * b) << std::endl;
    return a * b;
}

// 3. Функция с плавающей точкой
float test_divide(float a, float b) {
    if (b == 0) return 0;
    std::cout << "[JIT] test_divide(" << a << ", " << b << ") = " << (a / b) << std::endl;
    return a / b;
}

// 4. Функция со строкой
std::string test_echo(const std::string& message) {
    std::cout << "[JIT] test_echo: " << message << std::endl;
    return message;
}

// 5. Функция без возврата
void test_print(const std::string& message) {
    std::cout << "[JIT] test_print: " << message << std::endl;
}

// ============================================
// ФУНКЦИЯ ДЛЯ ПРОВЕРКИ JIT
// ============================================

void test_jit_functions(const std::string& module_name) {
    std::cout << "\n========== JIT ТЕСТ ==========" << std::endl;
    
    // Тест 1: int,int -> int (add)
    std::cout << "\n[TEST 1] Регистрация test_add (int,int->int)" << std::endl;
    register_method_impl(module_name.c_str(), "add", "int,int->int", (void*)test_add);
    
    // Тест 2: int,int -> int (multiply)
    std::cout << "\n[TEST 2] Регистрация test_multiply (int,int->int)" << std::endl;
    register_method_impl(module_name.c_str(), "multiply", "int,int->int", (void*)test_multiply);
    
    // Тест 3: float,float -> float (divide)
    std::cout << "\n[TEST 3] Регистрация test_divide (float,float->float)" << std::endl;
    register_method_impl(module_name.c_str(), "divide", "float,float->float", (void*)test_divide);
    
    // Тест 4: string -> string (echo)
    std::cout << "\n[TEST 4] Регистрация test_echo (string->string)" << std::endl;
    register_method_impl(module_name.c_str(), "echo", "string->string", (void*)test_echo);
    
    // Тест 5: string -> void (print)
    std::cout << "\n[TEST 5] Регистрация test_print (string->void)" << std::endl;
    register_method_impl(module_name.c_str(), "print", "string->void", (void*)test_print);
    
    // Проверяем, что методы зарегистрировались
    std::cout << "\n========== ПРОВЕРКА РЕГИСТРАЦИИ ==========" << std::endl;
    Module& mod = ModuleRegistry::get(module_name);
    
    const char* test_methods[] = {"add", "multiply", "divide", "echo", "print"};
    for (const char* name : test_methods) {
        const method* m = mod.get_method(name);
        if (m) {
            std::cout << "✅ Метод '" << name << "' зарегистрирован" << std::endl;
            if (m->adapter) {
                std::cout << "   ✅ Адаптер создан: " << m->adapter << std::endl;
            } else {
                std::cout << "   ❌ Адаптер НЕ создан!" << std::endl;
            }
        } else {
            std::cout << "❌ Метод '" << name << "' НЕ зарегистрирован!" << std::endl;
        }
    }
    
    std::cout << "===============================\n" << std::endl;
}

// ============================================
// ТЕСТ ВЫЗОВОВ ЧЕРЕЗ АДАПТЕРЫ
// ============================================

void test_call_methods(const std::string& module_name) {
    std::cout << "\n========== ВЫЗОВ МЕТОДОВ ЧЕРЕЗ АДАПТЕРЫ ==========" << std::endl;
    Module& mod = ModuleRegistry::get(module_name);

    // Тест 1: add (int,int -> int)
    {
        int a = 5, b = 3;
        int result = 0;
        const void* args[2] = { &a, &b };
        mod.call_method("add", args, &result);
        std::cout << "📞 add(5, 3) = " << result << " (ожидалось 8)" << std::endl;
    }

    // Тест 2: multiply (int,int -> int)
    {
        int a = 4, b = 7;
        int result = 0;
        const void* args[2] = { &a, &b };
        mod.call_method("multiply", args, &result);
        std::cout << "📞 multiply(4, 7) = " << result << " (ожидалось 28)" << std::endl;
    }

    // Тест 3: divide (float,float -> float)
    {
        float a = 10.0f, b = 4.0f;
        float result = 0.0f;
        const void* args[2] = { &a, &b };
        mod.call_method("divide", args, &result);
        std::cout << "📞 divide(10.0, 4.0) = " << result << " (ожидалось 2.5)" << std::endl;
    }

    // Тест 4: echo (string -> string)
    {
        std::string input = "Hello Loom!";
        std::string output;
        const void* args[1] = { &input };
        mod.call_method("echo", args, &output);
        std::cout << "📞 echo(\"Hello Loom!\") = \"" << output << "\"" << std::endl;
    }

    // Тест 5: print (string -> void)
    {
        std::string msg = "Test print message";
        const void* args[1] = { &msg };
        mod.call_method("print", args, nullptr);
        std::cout << "📞 print(\"Test print message\") - вывод выше" << std::endl;
    }

    std::cout << "================================================\n" << std::endl;
}

// ============================================
// MAIN
// ============================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Использование: ./test <имя_модуля>" << std::endl;
        std::cerr << "Пример: ./test t1" << std::endl;
        return 1;
    }
    //metalog::set_logger_active(false);
    std::string module_name = argv[1];
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    std::cout << "🚀 Запуск модуля: " << module_name << std::endl;
    start(module_name.c_str());
    
    // ========== ЗАПУСК JIT ТЕСТОВ ==========
    test_jit_functions(module_name);
    
    // Проверяем, что адаптеры работают
    test_call_methods(module_name);
    
    // ========== ПОИСК ДРУГИХ МОДУЛЕЙ ==========
    if (module_name == "t1") {
        std::cout << "🔍 Поиск модуля t2..." << std::endl;
        use_module_direct("t2");
    } else if (module_name == "t2") {
        std::cout << "🔍 Поиск модуля t1..." << std::endl;
        use_module_direct("t1");
    }
    
    // ========== ПОТОК ДЛЯ ОТПРАВКИ СООБЩЕНИЙ ==========
    std::thread sender_thread([&module_name]() {
        int counter = 0;
        while (keep_running) {
            sleep(3);
            
            if (module_name == "t1") {
                std::string msg = "Привет от t1 #" + std::to_string(++counter);
                std::cout << "📤 Отправка t1 -> t2: " << msg << std::endl;
                module_send("t2", msg.c_str());
            } else if (module_name == "t2") {
                std::string msg = "Привет от t2 #" + std::to_string(++counter);
                std::cout << "📤 Отправка t2 -> t1: " << msg << std::endl;
                module_send("t1", msg.c_str());
            } else {
                // Для JIT-тестов можно отправлять самому себе
                std::string msg = "Тестовое сообщение #" + std::to_string(++counter);
                std::cout << "📤 Отправка " << module_name << " -> " << module_name << ": " << msg << std::endl;
                module_send(module_name.c_str(), msg.c_str());
            }
        }
    });
    
    sender_thread.detach();
    
    std::cout << "✅ Модуль " << module_name << " готов. Ожидание сообщений..." << std::endl;
    std::cout << "📌 Нажмите Ctrl+C для остановки" << std::endl;
    
    while (keep_running) {
        sleep(1);
    }
    
    std::cout << "👋 Завершение модуля " << module_name << std::endl;
    return 0;
}