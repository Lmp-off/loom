# brutal_tester/CMakeLists.txt

set(TESTER_SOURCES
    tester.cpp
    tests/test_cache.cpp
    tests/test_module.cpp
    tests/test_uds.cpp
    tests/test_jit.cpp
    tests/test_connect.cpp
    tests/test_send_recv.cpp
)

add_executable(brutal_tester ${TESTER_SOURCES})

target_include_directories(brutal_tester
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../metalogger
)

target_link_libraries(brutal_tester
    loom
    pthread
    dl
    rt
)
