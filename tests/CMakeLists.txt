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

add_executable(test_decode_state test_decode_state.cpp)
target_link_libraries(test_decode_state PRIVATE arctic_macon)
add_test(NAME decode_state COMMAND test_decode_state)

add_executable(test_tuya_codec test_tuya_codec.cpp)
target_link_libraries(test_tuya_codec PRIVATE arctic_macon)
add_test(NAME tuya_codec COMMAND test_tuya_codec)

add_executable(test_macon_link test_macon_link.cpp)
target_link_libraries(test_macon_link PRIVATE arctic_macon)
add_test(NAME macon_link COMMAND test_macon_link)

add_executable(test_macon_master test_macon_master.cpp)
target_link_libraries(test_macon_master PRIVATE arctic_macon)
add_test(NAME macon_master COMMAND test_macon_master)

add_executable(test_macon_listener test_macon_listener.cpp)
target_link_libraries(test_macon_listener PRIVATE arctic_macon)
add_test(NAME macon_listener COMMAND test_macon_listener)

add_executable(test_faults test_faults.cpp)
target_link_libraries(test_faults PRIVATE arctic_macon)
add_test(NAME faults COMMAND test_faults)

add_executable(test_fault_identity test_fault_identity.cpp)
target_link_libraries(test_fault_identity PRIVATE arctic_macon)
add_test(NAME fault_identity COMMAND test_fault_identity)

add_executable(test_image test_image.cpp)
target_link_libraries(test_image PRIVATE arctic_macon)
add_test(NAME image COMMAND test_image)

add_executable(test_bus_params test_bus_params.cpp)
target_link_libraries(test_bus_params PRIVATE arctic_macon)
add_test(NAME bus_params COMMAND test_bus_params)
