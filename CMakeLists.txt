cmake_minimum_required(VERSION 3.16)
project(SensorHub CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -g)

# ---- Sanitizer: cmake -S . -B build-tsan -DSANITIZE=thread ----
set(SANITIZE "" CACHE STRING "Sanitizer: thread | address | (empty)")
set_property(CACHE SANITIZE PROPERTY STRINGS "" thread address)
if(SANITIZE STREQUAL "thread")
    set(SAN_FLAGS -fsanitize=thread)
elseif(SANITIZE STREQUAL "address")
    set(SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(NOT SANITIZE STREQUAL "")
    message(FATAL_ERROR "SANITIZE must be 'thread', 'address' or empty")
endif()
if(SAN_FLAGS)
    add_compile_options(${SAN_FLAGS})
    add_link_options(${SAN_FLAGS})
endif()

find_package(Threads REQUIRED)

# ---- Core programs ----
add_executable(sensor_hub src/main.cpp)
add_executable(monitor    src/monitor.cpp)
add_executable(tcp_client src/tcp_client.cpp)
foreach(t sensor_hub monitor tcp_client)
    target_include_directories(${t} PRIVATE include)
    target_link_libraries(${t} PRIVATE Threads::Threads rt)
endforeach()

# ---- Qt dashboard (tùy chọn; bỏ qua khi chạy sanitizer vì Qt không được instrument) ----
find_package(Qt6 QUIET COMPONENTS Widgets Charts Network)
if(Qt6_FOUND AND NOT SAN_FLAGS AND EXISTS ${CMAKE_SOURCE_DIR}/dashboard/CMakeLists.txt)
    add_subdirectory(dashboard)
endif()

# ---- Tests: ctest --test-dir build --output-on-failure ----
enable_testing()

add_executable(test_processor tests/test_processor.cpp)
target_include_directories(test_processor PRIVATE include)
add_test(NAME processor COMMAND test_processor)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME integration
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test.py
                     --build-dir ${CMAKE_BINARY_DIR})
    set_tests_properties(integration PROPERTIES TIMEOUT 90)
endif()
