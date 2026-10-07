#ifndef JIT_GENERATOR_H
#define JIT_GENERATOR_H

#include <string>
#include <vector>

void* jit_get_adapter(const std::string& signature, void* user_func);

#endif