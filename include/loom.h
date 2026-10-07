#ifndef LOOM_H
#define LOOM_H

#include <cstdint>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*generic_adapter_t)(const void** args, void* result);

uint8_t start(const char* module_name);
uint8_t stop(void);

uint8_t use_module_direct(const char* module_name);
uint8_t use_module(const char* description, const char* as_name);

void module_send(const char* module_name, const char* message);

uint8_t register_method(const char* method_name, const char* signature, void* func_ptr);

#ifdef __cplusplus
}
#endif
#endif