cmake_minimum_required(VERSION 3.16)
project(loom VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================
# ИСХОДНЫЕ ФАЙЛЫ БИБЛИОТЕКИ
# ============================================================
set(SOURCES
    src/core/loom.cpp
    src/discovery/cache.cpp
    src/discovery/capabilities.cpp
    src/core/server_core_api.cpp
    src/transport/local/uds.cpp
    src/core/jit_generator.cpp
    src/core/manager.cpp
    metalogger/metalogger.cpp
    src/global.cpp
)

# ============================================================
# ПУБЛИЧНЫЕ ЗАГОЛОВКИ
# ============================================================
set(PUBLIC_HEADERS
    include/loom.h
)

# ============================================================
# БИБЛИОТЕКА
# ============================================================
add_library(loom SHARED ${SOURCES})

target_include_directories(loom 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/metalogger>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(loom
    pthread
    dl
    rt
)

# ============================================================
# BRUTAL TESTER (loom/brutal_tester/)
# ============================================================
add_subdirectory(brutal_tester)

# ============================================================
# УСТАНОВКА
# ============================================================
install(TARGETS loom
    LIBRARY DESTINATION lib
)

install(FILES ${PUBLIC_HEADERS}
    DESTINATION include/loom
)

# ============================================================
# ИНФОРМАЦИЯ
# ============================================================
message(STATUS "Loom version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

message(STATUS "")
message(STATUS "Targets:")
message(STATUS "  make loom         - Build shared library (libloom.so)")
message(STATUS "  make loom_test    - Build old test (src/_tests/main.cpp)")
message(STATUS "  make brutal_tester - Build new test runner")
message(STATUS "  make install      - Install library and headers")
message(STATUS "")
