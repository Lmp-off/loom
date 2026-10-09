#include "../../src/discovery/capabilities.h"

static int add_impl(int a, int b) { return a + b; }

int test_module_all() {
    Module& mod = ModuleRegistry::get("test_module");
    if (mod.get_name() != "test_module") return 1;

    method m;
    m.name = "add";
    m.param_types = {"int", "int"};
    m.return_type = "int";
    m.implementation = (void*)add_impl;
    mod.add_method(m);

    const method* found = mod.get_method("add");
    if (!found) return 2;
    if (found->name != "add") return 3;
    if (found->param_types.size() != 2) return 4;

    int a = 5, b = 3;
    const void* args[2] = { &a, &b };
    int result = 0;
    auto fn = (int(*)(int,int))found->implementation;
    result = fn(a, b);
    if (result != 8) return 5;

    return 0;
}
