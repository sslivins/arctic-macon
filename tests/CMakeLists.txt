# Native (host) unit tests for the arctic-macon shared library.
#
# Build & run:
#   cmake -S tests -B build-tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
#
# Pulls in the library via the parent CMakeLists' non-ESP (native) branch,
# which compiles every source in MACON_SRCS as a plain C++17 static lib.

cmake_minimum_required(VERSION 3.16)
project(arctic_macon_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the library (native branch -> add_library(arctic_macon ...)).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/arctic_macon)

enable_testing()

add_executable(test_advanced_params test_advanced_params.cpp)
target_link_libraries(test_advanced_params PRIVATE arctic_macon)
add_test(NAME advanced_params COMMAND test_advanced_params)
