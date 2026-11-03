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
    src/transport/link.cpp
    src/transport/local/uds_transport.cpp
    src/transport/connection_cache.cpp
    src/transport/transport_manager.cpp
    src/core/jit_generator.cpp
    src/core/manager.cpp
    src/core/rpc_codec.cpp
    src/core/rpc_state.cpp
    src/protocol/gwp.cpp
    src/gateway/TDB/buffer/buffer.cpp
    src/gateway/TDB/strategy/strategy.cpp
    src/gateway/TDB/worker/worker.cpp
    src/gateway/TDB/uplink/uplink.cpp
    src/gateway/TDB/outlink/outlink.cpp
    src/gateway/TDB/tdb.cpp
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
# ПРИМЕРЫ: ДВА МОДУЛЯ
# ============================================================
add_executable(loom_calc_server examples/calc_server.cpp)
target_link_libraries(loom_calc_server loom pthread dl rt)

add_executable(loom_calc_client examples/calc_client.cpp)
target_link_libraries(loom_calc_client loom pthread dl rt)

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
