#include "../../src/core/jit_generator.h"

static int add_impl(int a, int b) { return a + b; }

int test_jit_all() {
    // 1. Генерация адаптера
    void* adapter = jit_get_adapter("int,int->int", (void*)add_impl);
    if (!adapter) return 1;

    // 2. Вызов адаптера
    using AdapterFunc = void(*)(const void**, void*);
    auto func = (AdapterFunc)adapter;
    int a = 5, b = 3;
    const void* args[2] = { &a, &b };
    int result = 0;
    func(args, &result);
    if (result != 8) return 2;

    return 0;
}
