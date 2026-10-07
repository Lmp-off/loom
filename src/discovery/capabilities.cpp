#include "capabilities.h"
#include "../core/jit_generator.h"
#include "../../metalogger/metalogger.h"
#include <dlfcn.h>
#include <iostream>

std::unordered_map<std::string, Module> ModuleRegistry::modules_;

Module& ModuleRegistry::get(const std::string& name) {
    auto it = modules_.find(name);
    if (it == modules_.end()) {
        modules_.emplace(name, Module(name));
        return modules_[name];
    }
    return it->second;
}

void Module::parse_signature(const std::string& sig,
                             std::vector<std::string>& params,
                             std::string& ret) const {
    params.clear();
    ret.clear();

    size_t arrow = sig.find("->");
    if (arrow == std::string::npos) {
        METALOG_WARNING("parse_signature: no '->' in signature: " + sig);
        return;
    }

    std::string params_str = sig.substr(0, arrow);
    ret = sig.substr(arrow + 2);

    ret.erase(0, ret.find_first_not_of(" \t"));
    ret.erase(ret.find_last_not_of(" \t") + 1);

    if (!params_str.empty()) {
        size_t start = 0, end;
        while ((end = params_str.find(',', start)) != std::string::npos) {
            std::string p = params_str.substr(start, end - start);
            p.erase(0, p.find_first_not_of(" \t"));
            p.erase(p.find_last_not_of(" \t") + 1);
            if (!p.empty()) params.push_back(p);
            start = end + 1;
        }
        std::string last = params_str.substr(start);
        last.erase(0, last.find_first_not_of(" \t"));
        last.erase(last.find_last_not_of(" \t") + 1);
        if (!last.empty()) params.push_back(last);
    }
}

void Module::add_method(const method& method_data) {
    methods_[method_data.name] = method_data;
    METALOG_INFO("✅ Метод '" + method_data.name + "' добавлен в реестр");
}

const method* Module::get_method(const std::string& name) const {
    auto it = methods_.find(name);
    return it != methods_.end() ? &it->second : nullptr;
}

void Module::call_method(const std::string& name, const void** args, void* result) const {
    auto it = methods_.find(name);
    if (it == methods_.end() || !it->second.adapter) {
        METALOG_ERROR("Method not found or no adapter: " + name);
        return;
    }

    METALOG_DEBUG("Calling method '" + name + "' via JIT adapter");

    using AdapterFunc = void(*)(const void**, void*);
    AdapterFunc adapter = reinterpret_cast<AdapterFunc>(it->second.adapter);
    
    adapter(args, result);
}

Module::~Module() {
    for (auto& pair : methods_) {
        if (pair.second.lib_handle) {
            dlclose(pair.second.lib_handle);
        }
    }
}

extern "C" void register_method_impl(const char* module_name,
                                     const char* method_name,
                                     const char* signature,
                                     void* impl) {
    if (!module_name || !method_name || !signature || !impl) {
        METALOG_ERROR("register_method_impl: invalid arguments");
        return;
    }

    method m;
    m.name = method_name;
    m.implementation = impl;

    Module& mod = ModuleRegistry::get(module_name);
    mod.parse_signature(signature, m.param_types, m.return_type);

    std::string arrow_sig;
    for (size_t i = 0; i < m.param_types.size(); ++i) {
        if (i > 0) arrow_sig += ",";
        arrow_sig += m.param_types[i];
    }
    arrow_sig += "->" + (m.return_type.empty() ? "void" : m.return_type);

    m.adapter = jit_get_adapter(arrow_sig, impl);

    if (m.adapter) {
        mod.add_method(m);
    } else {
        METALOG_ERROR("Failed to create adapter for method: " + std::string(method_name));
    }
}