# loom.py
import ctypes
from ctypes import c_uint8, c_char_p, c_void_p, CFUNCTYPE, py_object, cast

# Загрузка библиотеки
_lib = ctypes.CDLL("./build/libloom.so")

# Типизация функций
_lib.start.argtypes = [c_char_p]
_lib.start.restype = c_uint8

_lib.stop.argtypes = []
_lib.stop.restype = c_uint8

_lib.use_module_direct.argtypes = [c_char_p]
_lib.use_module_direct.restype = c_uint8

_lib.use_module.argtypes = [c_char_p, c_char_p]
_lib.use_module.restype = c_uint8

_lib.module_send.argtypes = [c_char_p, c_char_p]
_lib.module_send.restype = None

_lib.register_method_impl.argtypes = [c_char_p, c_char_p, c_char_p, c_void_p]
_lib.register_method_impl.restype = None

# Хранилище для callback-функций
_callbacks = {}

# Тип для адаптера (void* -> void*)
ADAPTER_TYPE = CFUNCTYPE(c_void_p, c_void_p)

def register_method(module_name: str, method_name: str, signature: str, func):
    """Регистрирует Python-функцию как метод"""
    
    @ADAPTER_TYPE
    def adapter(args_ptr):
        try:
            result = func()
            return cast(py_object(result), c_void_p).value
        except Exception as e:
            print(f"Error: {e}")
            return 0
    
    _callbacks[method_name] = adapter
    
    _lib.register_method_impl(
        module_name.encode(),
        method_name.encode(),
        signature.encode(),
        cast(adapter, c_void_p)
    )
    print(f"✅ Registered: {method_name}")

def start(name: str) -> int:
    return _lib.start(name.encode())

def stop() -> int:
    return _lib.stop()

def use_module_direct(name: str) -> int:
    return _lib.use_module_direct(name.encode())

def use_module(desc: str, as_name: str) -> int:
    return _lib.use_module(desc.encode(), as_name.encode())

def module_send(to: str, msg: str):
    _lib.module_send(to.encode(), msg.encode())

print("✅ Loom loaded")