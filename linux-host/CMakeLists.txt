cmake_minimum_required(VERSION 3.13)

project(linux_system_monitor_host C)

# Default to Release for single-config generators.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(system_monitor_host system_monitor_host.c)

target_compile_features(system_monitor_host PRIVATE c_std_11)

target_compile_options(system_monitor_host PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-O0;-g>
    $<$<CONFIG:Release>:-O2;-DNDEBUG>
)

# Place the output binary under: <build-dir>/<config>/system_monitor_host
set_target_properties(system_monitor_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}"
)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_uc)
    set_target_properties(system_monitor_host PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${cfg_uc}" "${CMAKE_BINARY_DIR}/${cfg}"
    )
endforeach()
