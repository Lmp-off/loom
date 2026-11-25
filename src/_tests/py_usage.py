#!/usr/bin/env python3
"""
Loom Python модуль — минимальный пример
"""

import ctypes
import time
import signal
import sys
import json

# ============================================
# 1. ЗАГРУЗКА БИБЛИОТЕКИ (один раз)
# ============================================
libloom = ctypes.CDLL("./libloom.so")

# Настройка типов для C функций (обязательно)
libloom.register_method_impl.argtypes = [
    ctypes.c_char_p,  # module_name
    ctypes.c_char_p,  # method_name
    ctypes.c_char_p,  # signature (например "int,int->int")
    ctypes.c_void_p   # указатель на функцию
]
libloom.start.argtypes = [ctypes.c_char_p]
libloom.stop.argtypes = []
libloom.module_send.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
libloom.use_module_direct.argtypes = [ctypes.c_char_p]

# ============================================
# 2. ТВОЯ БИЗНЕС-ЛОГИКА (обычные Python функции)
# ============================================

def add(a: int, b: int) -> int:
    """Сложение двух чисел"""
    result = a + b
    print(f"[JIT] add({a}, {b}) = {result}")
    return result

def multiply(a: int, b: int) -> int:
    """Умножение двух чисел"""
    result = a * b
    print(f"[JIT] multiply({a}, {b}) = {result}")
    return result

def greet(name: str) -> str:
    """Приветствие"""
    return f"Hello, {name}!"

def log_message(msg: str) -> None:
    """Логирование (без возврата)"""
    print(f"[LOG] {msg}")

# ============================================
# 3. РЕГИСТРАЦИЯ МЕТОДОВ (одна строка на метод)
# ============================================

def register(module: str, name: str, sig: str, func):
    """
    Универсальная регистрация метода
    
    Поддерживаемые сигнатуры:
    - "int,int->int"
    - "float,float->float"
    - "string->string"
    - "string->void"
    - "void->void"
    """
    
    # Определяем тип callback по сигнатуре
    if sig == "int,int->int":
        cb_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_int)
    elif sig == "float,float->float":
        cb_type = ctypes.CFUNCTYPE(ctypes.c_float, ctypes.c_float, ctypes.c_float)
    elif sig == "string->string":
        cb_type = ctypes.CFUNCTYPE(ctypes.c_char_p, ctypes.c_char_p)
    elif sig == "string->void":
        cb_type = ctypes.CFUNCTYPE(None, ctypes.c_char_p)
    elif sig == "void->void":
        cb_type = ctypes.CFUNCTYPE(None)
    else:
        raise ValueError(f"Unknown signature: {sig}")
    
    # Создаём C-обёртку и регистрируем
    cb = cb_type(func)
    libloom.register_method_impl(
        module.encode(),
        name.encode(),
        sig.encode(),
        ctypes.cast(cb, ctypes.c_void_p)
    )

# ============================================
# 4. ОТПРАВКА СООБЩЕНИЙ ДРУГИМ МОДУЛЯМ
# ============================================

def send_to(module_name: str, method: str, args: list):
    """Отправить запрос другому модулю"""
    msg = json.dumps({"method": method, "args": args})
    libloom.module_send(module_name.encode(), msg.encode())

# ============================================
# 5. MAIN (запуск модуля)
# ============================================

def main():
    if len(sys.argv) < 2:
        print("Использование: python my_module.py <имя_модуля>")
        print("Пример: python my_module.py t1")
        sys.exit(1)
    
    module_name = sys.argv[1]
    print(f"🚀 Запуск модуля: {module_name}")
    
    # Регистрируем методы
    register(module_name, "add", "int,int->int", add)
    register(module_name, "multiply", "int,int->int", multiply)
    register(module_name, "greet", "string->string", greet)
    register(module_name, "log", "string->void", log_message)
    
    # Запускаем модуль
    libloom.start(module_name.encode())
    print(f"✅ Модуль {module_name} готов")
    
    # Обработка Ctrl+C
    running = True
    def signal_handler(sig, frame):
        nonlocal running
        print(f"\n🛑 Остановка...")
        libloom.stop()
        running = False
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Основной цикл
    try:
        while running:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    
    print(f"👋 Завершение модуля {module_name}")

if __name__ == "__main__":
    main()