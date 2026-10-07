#include "jit_generator.h"
#include "../../metalogger/metalogger.h"
#include <sstream>
#include <unordered_map>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#include <algorithm>

static std::unordered_map<std::string, void*> g_adapter_cache;

std::string normalize_type(const std::string& t) {
    std::string lower = t;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "string" || lower == "str") return "std::string";
    if (lower == "int")     return "int";
    if (lower == "double")  return "double";
    if (lower == "float")   return "float";
    if (lower == "bool")    return "bool";
    if (lower == "void")    return "void";
    return t;
}

std::string generate_adapter_code(const std::string& signature,
                                  const std::vector<std::string>& param_types,
                                  const std::string& return_type,
                                  int unique_id) {
    std::stringstream code;

    bool uses_string = false;
    for (const auto& p : param_types) if (normalize_type(p) == "std::string") uses_string = true;
    if (normalize_type(return_type) == "std::string") uses_string = true;

    code << "#include <stdint.h>\n";
    if (uses_string) code << "#include <string>\n";
    code << "\n";

    std::string ret_type = normalize_type(return_type);
    code << "typedef " << ret_type << " (*user_func_t)(";
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) code << ", ";
        code << normalize_type(param_types[i]);
    }
    if (param_types.empty()) code << "void";
    code << ");\n\n";

    code << "static user_func_t g_user_func_" << unique_id << " = nullptr;\n\n";

    code << "extern \"C\" void set_user_func_" << unique_id << "(void* f) {\n";
    code << "    g_user_func_" << unique_id << " = (user_func_t)f;\n";
    code << "}\n\n";

    code << "extern \"C\" void adapter(const void** args, void* result) {\n";
    code << "    if (!g_user_func_" << unique_id << ") return;\n\n";

    if (ret_type != "void") {
        code << "    " << ret_type << " ret = g_user_func_" << unique_id << "(";
    } else {
        code << "    g_user_func_" << unique_id << "(";
    }

    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) code << ", ";
        std::string t = normalize_type(param_types[i]);
        code << "*(" << t << "*)args[" << i << "]";
    }
    code << ");\n";

    if (ret_type != "void") {
        code << "    if (result) *((" << ret_type << "*)result) = ret;\n";
    }

    code << "}\n";

    METALOG_INFO("\n=== JIT Adapter Generated for " + signature + " ===\n" + code.str() + "===========================\n");
    return code.str();
}

void* jit_get_adapter(const std::string& raw_signature, void* user_func) {
    std::string signature = raw_signature;
    size_t arrow = signature.find("->");
    if (arrow == std::string::npos) {
        size_t open = signature.find('(');
        if (open != std::string::npos) {
            std::string params = signature.substr(open + 1, signature.find(')') - open - 1);
            std::string ret = signature.substr(0, open);
            signature = params + "->" + ret;
            METALOG_DEBUG("Fixed signature: " + raw_signature + " → " + signature);
        } else {
            METALOG_ERROR("Invalid signature: " + raw_signature);
            return nullptr;
        }
    }

    auto it = g_adapter_cache.find(signature);
    if (it != g_adapter_cache.end()) {
        return it->second;
    }

    std::string params_str = signature.substr(0, arrow);
    std::string return_type = signature.substr(arrow + 2);

    return_type.erase(0, return_type.find_first_not_of(" \t"));
    return_type.erase(return_type.find_last_not_of(" \t") + 1);

    std::vector<std::string> param_types;
    if (!params_str.empty()) {
        size_t start = 0, end;
        while ((end = params_str.find(',', start)) != std::string::npos) {
            std::string p = params_str.substr(start, end - start);
            p.erase(0, p.find_first_not_of(" \t"));
            p.erase(p.find_last_not_of(" \t") + 1);
            if (!p.empty()) param_types.push_back(p);
            start = end + 1;
        }
        std::string last = params_str.substr(start);
        last.erase(0, last.find_first_not_of(" \t"));
        last.erase(last.find_last_not_of(" \t") + 1);
        if (!last.empty()) param_types.push_back(last);
    }

    static int counter = 0;
    int id = counter++;

    std::string code = generate_adapter_code(signature, param_types, return_type, id);

    std::string cpp_file = "/tmp/jit_" + std::to_string(id) + ".cpp";
    std::string so_file  = "/tmp/jit_" + std::to_string(id) + ".so";

    FILE* f = fopen(cpp_file.c_str(), "w");
    if (!f) {
        METALOG_ERROR("Cannot create cpp file");
        return nullptr;
    }
    fwrite(code.c_str(), 1, code.length(), f);
    fclose(f);

    std::string cmd = "g++ -shared -fPIC -std=c++17 " + cpp_file + " -o " + so_file + " 2>&1";
    METALOG_DEBUG("Compiling: " + cmd);

    if (system(cmd.c_str()) != 0) {
        METALOG_ERROR("JIT compilation failed for: " + signature);
        return nullptr;
    }

    void* handle = dlopen(so_file.c_str(), RTLD_NOW);
    if (!handle) {
        METALOG_ERROR("dlopen failed: " + std::string(dlerror()));
        return nullptr;
    }

    std::string setter_name = "set_user_func_" + std::to_string(id);
    void (*setter)(void*) = (void(*)(void*))dlsym(handle, setter_name.c_str());
    void* adapter_ptr = dlsym(handle, "adapter");

    if (setter && adapter_ptr) {
        setter(user_func);
        g_adapter_cache[signature] = adapter_ptr;
        METALOG_INFO("✅ JIT adapter created for: " + signature);
        return adapter_ptr;
    }

    dlclose(handle);
    return nullptr;
}