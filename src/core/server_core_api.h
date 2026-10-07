#ifndef SERVER_CORE_API_H
#define SERVER_CORE_API_H

#include <string>
namespace server_api{
    std::string locate(std::string desc);
    std::string locate_by_description(std::string desc);
}
#endif