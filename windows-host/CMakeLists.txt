cmake_minimum_required(VERSION 3.13)

project(windows_system_monitor_host C)

add_executable(system_monitor_host system_monitor_host.c)

target_compile_features(system_monitor_host PRIVATE c_std_11)

if (MSVC)
    target_compile_options(system_monitor_host PRIVATE /W4)
else()
    target_compile_options(system_monitor_host PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-O0;-g>
        $<$<CONFIG:Release>:-O2;-DNDEBUG>
    )
endif()

target_link_libraries(system_monitor_host PRIVATE psapi)

if (MINGW)
    target_link_libraries(system_monitor_host PRIVATE m)
endif()

# Place the output binary under: <build-dir>/<config>/system_monitor_host.exe
set_target_properties(system_monitor_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}"
)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_uc)
    set_target_properties(system_monitor_host PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${cfg_uc}" "${CMAKE_BINARY_DIR}/${cfg}"
    )
endforeach()
