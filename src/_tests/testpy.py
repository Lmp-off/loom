#!/usr/bin/env python3
"""
Минимальный Python тест для Loom JIT
Регистрация функций через ctypes и вызов через C++ JIT адаптеры
"""

import ctypes
import time
import signal
import sys
import json

# Загружаем библиотеку
libloom = ctypes.CDLL("./libloom.so")

# Определяем типы для C функций
libloom.register_method_impl.argtypes = [
    ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p
]
libloom.start.argtypes = [ctypes.c_char_p]
libloom.stop.argtypes = []
libloom.module_send.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
libloom.use_module_direct.argtypes = [ctypes.c_char_p]

# ============================================
# ТЕСТОВЫЕ ФУНКЦИИ
# ============================================

def add(a: int, b: int) -> int:
    result = a + b
    print(f"[JIT] add({a}, {b}) = {result}")
    return result

def multiply(a: int, b: int) -> int:
    result = a * b
    print(f"[JIT] multiply({a}, {b}) = {result}")
    return result

# ============================================
# РЕГИСТРАЦИЯ МЕТОДОВ
# ============================================

def register(module: str, name: str, sig: str, func):
    if sig == "int,int->int":
        cb_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_int)
    else:
        raise ValueError(f"Unknown signature: {sig}")
    
    cb = cb_type(func)
    libloom.register_method_impl(
        module.encode(), name.encode(), sig.encode(),
        ctypes.cast(cb, ctypes.c_void_p)
    )

# ============================================
# ОБРАБОТЧИКИ СООБЩЕНИЙ
# ============================================

def send_computation(target: str, x: int, y: int):
    """Отправляет запрос на вычисление другому модулю"""
    msg = json.dumps({
        "method": "compute",
        "args": [x, y]
    })
    print(f"📤 Отправка {target}: compute({x}, {y})")
    libloom.module_send(target.encode(), msg.encode())

def send_result(target: str, result: int):
    """Отправляет результат обратно"""
    msg = json.dumps({
        "method": "result",
        "args": [result]
    })
    print(f"📤 Отправка результата {result} -> {target}")
    libloom.module_send(target.encode(), msg.encode())

# ============================================
# MAIN
# ============================================

def main():
    if len(sys.argv) < 2:
        print("Использование: python test.py <t1|t2>")
        sys.exit(1)
    
    module_name = sys.argv[1]
    print(f"🚀 Запуск модуля: {module_name}")
    
    # Регистрируем методы
    if module_name == "t1":
        register("t1", "add", "int,int->int", add)
    elif module_name == "t2":
        register("t2", "multiply", "int,int->int", multiply)
    else:
        print(f"❌ Неизвестный модуль: {module_name}")
        sys.exit(1)
    
    # Запуск модуля
    libloom.start(module_name.encode())
    print(f"✅ Модуль {module_name} готов")
    
    # Поиск другого модуля
    if module_name == "t1":
        print("🔍 Поиск модуля t2...")
        libloom.use_module_direct(b"t2")
        partner = "t2"
        my_compute = add
        partner_compute = multiply
    else:
        print("🔍 Поиск модуля t1...")
        libloom.use_module_direct(b"t1")
        partner = "t1"
        my_compute = multiply
        partner_compute = add
    
    # Обработка сигналов
    running = True
    def signal_handler(sig, frame):
        nonlocal running
        print(f"\n🛑 Получен сигнал {sig}, остановка...")
        libloom.stop()
        running = False
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Основной цикл: отправка и приём вычислений
    counter = 0
    
    print(f"\n🔄 Начинаем обмен вычислениями с {partner}")
    print(f"   t1 считает сумму (a+b)")
    print(f"   t2 считает произведение (a*b)\n")
    
    while running:
        time.sleep(3)
        counter += 1
        a, b = counter, counter + 1
        
        # Отправляем запрос партнёру
        send_computation(partner, a, b)
        
        # Симуляция получения ответа (в реальности ответ приходит через listener_thread)
        # Здесь мы просто вызываем локальную функцию как пример
        if module_name == "t1":
            result = my_compute(a, b)
            print(f"📞 t1: {a} + {b} = {result}")
            print(f"💡 Если бы t2 ответил, он бы посчитал {a} * {b} = {a*b}\n")
        else:
            result = my_compute(a, b)
            print(f"📞 t2: {a} * {b} = {result}")
            print(f"💡 Если бы t1 ответил, он бы посчитал {a} + {b} = {a+b}\n")
    
    print(f"👋 Завершение модуля {module_name}")

if __name__ == "__main__":
    main()