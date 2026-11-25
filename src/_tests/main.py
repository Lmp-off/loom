# test_loom.py
import loom
import time
import signal
import sys

keep_running = True

def signal_handler(sig, frame):
    global keep_running
    print("\n🛑 Stopping...")
    loom.stop()
    keep_running = False

# Тестовые функции
def add():
    return 5 + 3

def hello():
    return "Hello from Python!"

def get_time():
    return str(time.time())

def main():
    if len(sys.argv) < 2:
        print("Usage: python test_loom.py <module_name>")
        sys.exit(1)
    
    module_name = sys.argv[1]
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    print(f"🚀 Starting module: {module_name}")
    
    if loom.start(module_name) != 0:
        print("❌ Start failed")
        sys.exit(1)
    
    # Регистрация методов
    print("\n========== REGISTER METHODS ==========")
    loom.register_method(module_name, "add", "->int", add)
    loom.register_method(module_name, "hello", "->string", hello)
    loom.register_method(module_name, "get_time", "->string", get_time)
    
    print(f"\n✅ Module {module_name} ready")
    print("📌 Press Ctrl+C to stop\n")
    
    # Отправка тестовых сообщений
    counter = 0
    while keep_running:
        time.sleep(2)
        counter += 1
        msg = f"Test message #{counter}"
        print(f"📤 Sending: {msg}")
        loom.module_send(module_name, msg)
    
    loom.stop()
    print(f"👋 Module {module_name} finished")

if __name__ == "__main__":
    main()