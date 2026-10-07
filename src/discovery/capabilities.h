#ifndef CAPABILITIES_H
#define CAPABILITIES_H

#include <string>
#include <vector>
#include <unordered_map>

struct method {
    std::string name;
    std::vector<std::string> param_types;
    std::string return_type;
    std::string description;
    
    void* implementation = nullptr;
    void* adapter = nullptr;
    void* lib_handle = nullptr;
};

class Module {
private:
    std::string name_;
    std::string description_;
    std::unordered_map<std::string, method> methods_;

public:
    Module() = default;
    explicit Module(const std::string& name) : name_(name) {}

    void set_name(const std::string& name) { name_ = name; }
    void set_description(const std::string& desc) { description_ = desc; }

    const std::string& get_name() const { return name_; }
    const std::string& get_description() const { return description_; }

    void add_method(const method& method_data);
    const method* get_method(const std::string& name) const;
    const std::unordered_map<std::string, method>& get_methods() const { return methods_; }

    void call_method(const std::string& name, const void** args, void* result) const;

    void parse_signature(const std::string& sig,
                         std::vector<std::string>& params,
                         std::string& ret) const;

    ~Module();
};

class ModuleRegistry {
private:
    static std::unordered_map<std::string, Module> modules_;

public:
    static Module& get(const std::string& name);
    static void remove(const std::string& name);
    static void clear();
};

extern "C" {
void register_method_impl(const char* module_name,
                         const char* method_name,
                         const char* signature,
                         void* impl);
}

#endif