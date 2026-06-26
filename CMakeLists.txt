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

include(FetchContent)

set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_FIXED_POINT ON CACHE BOOL "" FORCE)
FetchContent_Declare(opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG v1.5.2
)
FetchContent_MakeAvailable(opus)

add_library(ds5onswitch_core
    src/dualsense_output.cpp
    src/dualsense_report.cpp
    src/haptics.cpp
    src/mapper.cpp
    src/switch_pro.cpp
    src/switch_usb.cpp
)

target_include_directories(ds5onswitch_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(ds5onswitch_core PRIVATE
    -Wall
    -Wextra
)

add_executable(ds5onswitch
    src/bootsel_button.cpp
    src/dualsense_audio.cpp
    src/dualsense_bt.cpp
    src/main.cpp
    src/usb_descriptors.cpp
)

target_link_libraries(ds5onswitch PRIVATE
    ds5onswitch_core
    pico_stdlib
    pico_cyw43_arch_poll
    pico_btstack_classic
    pico_btstack_cyw43
    tinyusb_device
    tinyusb_board
    opus
)

target_compile_definitions(ds5onswitch PRIVATE
    PICO_FLASH_ASSUME_CORE1_SAFE=1
    CYW43_LWIP=0
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
)

target_include_directories(ds5onswitch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

pico_set_program_name(ds5onswitch "ds5onswitch")
pico_set_program_version(ds5onswitch "0.1.0-dev")
pico_enable_stdio_uart(ds5onswitch 1)
pico_enable_stdio_usb(ds5onswitch 0)
pico_add_extra_outputs(ds5onswitch)
