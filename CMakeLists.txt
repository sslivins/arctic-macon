# arctic-macon — shared ESP-IDF component
#
# Consumed as a git submodule under each project's components/ directory:
#   arctic-controller, arctic-simulator, arctic-sniffer.
# ESP-IDF auto-discovers it there; list "arctic-macon" in the consuming
# component's REQUIRES.
#
# Also builds natively (host unit tests) — the sources are pure C++ with no
# ESP-IDF / FreeRTOS dependency.

set(MACON_SRCS
    "src/tuya_codec.cpp"
    "src/macon_registers.cpp"
    "src/macon_advanced_params.cpp"
    "src/macon_state.cpp"
    "src/macon_faults.cpp")

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${MACON_SRCS}
        INCLUDE_DIRS "include")
else()
    add_library(arctic_macon STATIC ${MACON_SRCS})
    target_include_directories(arctic_macon PUBLIC include)
    target_compile_features(arctic_macon PUBLIC cxx_std_17)
endif()
