# loom/CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(loom VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============ Исходные файлы ============
set(SOURCES
    src/core/loom.cpp
    src/connection/local/uds.cpp
    src/connection/network/udp.cpp
    #src/discovery/cache.cpp
)

# ============ Публичные заголовки ============
set(PUBLIC_HEADERS
    include/loom.h
)

# ============ Библиотека ============
add_library(loom SHARED ${SOURCES})

target_include_directories(loom 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Linux библиотеки
target_link_libraries(loom
    pthread
    dl
    rt
)

# ============ Установка ============
install(TARGETS loom
    LIBRARY DESTINATION lib
)

install(FILES ${PUBLIC_HEADERS}
    DESTINATION include/loom
)

# ============ Сборка ============
# mkdir build && cd build && cmake .. && make