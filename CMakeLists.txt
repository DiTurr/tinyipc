# --------------------------------------------------
# CMake configuration
# --------------------------------------------------
cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME tinyipc)
project(${PROJECT_NAME} LANGUAGES CXX)

# --------------------------------------------------
# Sources
# --------------------------------------------------
add_executable(publisher
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyipc/tests/test_publisher.cpp)

add_executable(subscriber_00
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyipc/tests/test_subscriber_00.cpp)

add_executable(subscriber_01
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyipc/tests/test_subscriber_01.cpp)

# --------------------------------------------------
# Include
# --------------------------------------------------
target_include_directories(publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyipc)

target_include_directories(subscriber_00 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyipc)

target_include_directories(subscriber_01 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyipc)

# --------------------------------------------------
# C++ configuration
# --------------------------------------------------
set_target_properties(publisher subscriber_00 subscriber_01
    PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build")

# --------------------------------------------------
# Compiler flags
# --------------------------------------------------
target_compile_options(publisher PRIVATE -O3)
target_compile_options(subscriber_00 PRIVATE -O3)
target_compile_options(subscriber_01 PRIVATE -O3)
