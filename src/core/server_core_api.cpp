#include "server_core_api.h"
namespace server_api{
    std::string locate(std::string desc){
        return "/tmp/loom_"+desc+".sock";
    }
    std::string locate_by_description(std::string desc){
        return "/tmp/loom_"+desc+".sock";
    }
}