cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT PICO_BOARD)
    set(PICO_BOARD pico2_w CACHE STRING "Board type")
endif ()

include(pico_sdk_import.cmake)

project(ds5onswitch C CXX ASM)

pico_sdk_init()

add_library(ds5onswitch_core
    src/haptics.cpp
    src/mapper.cpp
    src/switch_pro.cpp
)

target_include_directories(ds5onswitch_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(ds5onswitch_core PRIVATE
    -Wall
    -Wextra
)

add_executable(ds5onswitch
    src/main.cpp
)

target_link_libraries(ds5onswitch PRIVATE
    ds5onswitch_core
    pico_stdlib
    pico_cyw43_arch_none
)

target_compile_definitions(ds5onswitch PRIVATE
    PICO_FLASH_ASSUME_CORE1_SAFE=1
)

pico_set_program_name(ds5onswitch "ds5onswitch")
pico_set_program_version(ds5onswitch "0.1.0-dev")
pico_enable_stdio_uart(ds5onswitch 1)
pico_enable_stdio_usb(ds5onswitch 0)
pico_add_extra_outputs(ds5onswitch)
