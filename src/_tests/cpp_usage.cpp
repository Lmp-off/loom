/*
Loom C++ модуль — минимальный пример

Сборка:
g++ -std=c++17 my_module.cpp -L. -lloom -o my_module -pthread -ldl

Запуск:
./my_module t1
*/

#include "../../include/loom.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// ============================================
// 1. ТВОЯ БИЗНЕС-ЛОГИКА
// ============================================

int add(int a, int b) {
    std::cout << "[JIT] add(" << a << ", " << b << ") = " << (a + b) << std::endl;
    return a + b;
}

int multiply(int a, int b) {
    std::cout << "[JIT] multiply(" << a << ", " << b << ") = " << (a * b) << std::endl;
    return a * b;
}

std::string greet(const std::string& name) {
    return "Hello, " + name + "!";
}

void log_message(const std::string& msg) {
    std::cout << "[LOG] " << msg << std::endl;
}

// ============================================
// 2. MAIN (запуск модуля)
// ============================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Использование: ./my_module <имя_модуля>" << std::endl;
        std::cerr << "Пример: ./my_module t1" << std::endl;
        return 1;
    }
    
    std::string module_name = argv[1];
    std::cout << "🚀 Запуск модуля: " << module_name << std::endl;
    
    // Регистрация методов (одна строка на метод)
    register_method_impl(module_name.c_str(), "add", "int,int->int", (void*)add);
    register_method_impl(module_name.c_str(), "multiply", "int,int->int", (void*)multiply);
    register_method_impl(module_name.c_str(), "greet", "string->string", (void*)greet);
    register_method_impl(module_name.c_str(), "log", "string->void", (void*)log_message);
    
    // Запуск модуля
    start(module_name.c_str());
    std::cout << "✅ Модуль " << module_name << " готов. Нажмите Enter для выхода." << std::endl;
    
    // Отправка сообщения другому модулю (пример)
    // use_module_direct("target_module");
    // module_send("target_module", R"({"method":"add","args":[5,3]})");
    
    std::cin.get();
    
    // Остановка
    stop();
    std::cout << "👋 Завершение модуля " << module_name << std::endl;
    
    return 0;
}