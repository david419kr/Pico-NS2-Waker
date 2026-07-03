cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT PICO_BOARD)
    set(PICO_BOARD pico2_w CACHE STRING "Board type")
endif ()

include(pico_sdk_import.cmake)

project(switchbridge C CXX ASM)

pico_sdk_init()

include(FetchContent)

set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_FIXED_POINT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG v1.5.2
)
FetchContent_MakeAvailable(opus)

add_library(switchbridge_core
    src/dualshock4_output.cpp
    src/dualshock4_report.cpp
    src/dualsense_output.cpp
    src/dualsense_report.cpp
    src/haptics.cpp
    src/mapper.cpp
    src/persona_combo_filter.cpp
    src/procon2_handshake.cpp
    src/procon2_report.cpp
    src/steam_controller.cpp
    src/switch2_wake.cpp
    src/switch_identity.cpp
    src/switch_pro.cpp
    src/switch_usb.cpp
    src/xbox_controller.cpp
)

target_include_directories(switchbridge_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(switchbridge_core PRIVATE
    -Wall
    -Wextra
)

target_link_libraries(switchbridge_core PUBLIC
    pico_stdlib
)

add_executable(switchbridge
    src/bootsel_button.cpp
    src/bt_debug_log.cpp
    src/controller_feedback.cpp
    src/dualsense_audio.cpp
    src/classic_hid_bt.cpp
    src/main.cpp
    src/procon2_ble.cpp
    src/ram_mem.c
    src/ble_controller_hub.cpp
    src/switch2_wake_runtime.cpp
    src/usb_descriptors.cpp
)

set_source_files_properties(src/ram_mem.c PROPERTIES COMPILE_OPTIONS
    "-fno-builtin;-fno-tree-loop-distribute-patterns")

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    COMMAND ${CMAKE_OBJCOPY}
        --rename-section .text=.time_critical.opus_text
        --rename-section .rodata=.time_critical.opus_rodata
        --rename-section .rodata.str1.4=.time_critical.opus_strings
        $<TARGET_FILE:opus>
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp
    DEPENDS $<TARGET_FILE:opus>
    COMMENT "Relocate libopus text/rodata to RAM"
    VERBATIM
)
add_custom_target(opus_ram_relocate ALL
    DEPENDS ${CMAKE_BINARY_DIR}/opus_ram_relocate.stamp)
add_dependencies(switchbridge opus_ram_relocate)

target_link_libraries(switchbridge PRIVATE
    switchbridge_core
    pico_stdlib
    pico_cyw43_arch_poll
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_cyw43
    tinyusb_device
    tinyusb_board
    opus
)

target_compile_definitions(switchbridge PRIVATE
    PICO_FLASH_ASSUME_CORE1_SAFE=1
    CYW43_LWIP=0
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
)

target_include_directories(switchbridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

function(relocate_to_ram _suffix _renames _comment)
    add_custom_command(TARGET switchbridge PRE_LINK
        COMMAND ${CMAKE_COMMAND}
            "-DOBJROOT=${CMAKE_BINARY_DIR}/CMakeFiles/switchbridge.dir"
            "-DOBJCOPY=${CMAKE_OBJCOPY}"
            "-DSUFFIX=${_suffix}"
            "-DRENAMES=${_renames}"
            -P "${CMAKE_SOURCE_DIR}/cmake/relocate_to_ram.cmake"
        COMMENT "${_comment}"
        VERBATIM)
endfunction()

relocate_to_ram(lib/btstack/src/hci.c.o
    ".text.packet_handler=.time_critical.bt_packet_handler"
    "Relocate BTstack HCI packet handler to RAM")
relocate_to_ram(lib/btstack/src/l2cap.c.o
    ".text.l2cap_acl_handler=.time_critical.l2cap_acl_handler"
    "Relocate BTstack L2CAP ACL handler to RAM")
relocate_to_ram(lib/tinyusb/src/common/tusb_fifo.c.o
    ".text.tu_fifo_read=.time_critical.tu_fifo_read|.text.tu_fifo_read_n=.time_critical.tu_fifo_read_n|.text.tu_fifo_write=.time_critical.tu_fifo_write|.text.tu_fifo_write_n=.time_critical.tu_fifo_write_n|.text.tu_fifo_count=.time_critical.tu_fifo_count"
    "Relocate TinyUSB FIFO data path to RAM")
relocate_to_ram(src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c.o
    ".text.cyw43_spi_transfer=.time_critical.cyw43_spi_transfer|.text.cyw43_read_bytes=.time_critical.cyw43_read_bytes|.text.cyw43_write_bytes=.time_critical.cyw43_write_bytes|.text.cyw43_read_reg_u32=.time_critical.cyw43_read_reg_u32|.text.cyw43_read_reg_u16=.time_critical.cyw43_read_reg_u16|.text.cyw43_read_reg_u8=.time_critical.cyw43_read_reg_u8|.text.cyw43_write_reg_u32=.time_critical.cyw43_write_reg_u32|.text.cyw43_write_reg_u16=.time_critical.cyw43_write_reg_u16|.text.cyw43_write_reg_u8=.time_critical.cyw43_write_reg_u8|.text.read_reg_u32_swap=.time_critical.read_reg_u32_swap|.text.write_reg_u32_swap=.time_critical.write_reg_u32_swap|.text.ns_delay.constprop.0=.time_critical.cyw43_ns_delay"
    "Relocate CYW43 PIO-SPI transport to RAM")
relocate_to_ram(lib/cyw43-driver/src/cyw43_ll.c.o
    ".text.cyw43_ll_sdpcm_poll_device=.time_critical.cyw43_ll_sdpcm_poll_device|.text.cyw43_ll_process_packets=.time_critical.cyw43_ll_process_packets|.text.cyw43_sdpcm_send_common=.time_critical.cyw43_sdpcm_send_common|.text.cyw43_do_ioctl.part.0=.time_critical.cyw43_do_ioctl|.text.cyw43_ll_bt_has_work=.time_critical.cyw43_ll_bt_has_work|.text.cyw43_ll_parse_async_event=.time_critical.cyw43_ll_parse_async_event"
    "Relocate CYW43 SDPCM pump to RAM")
relocate_to_ram(lib/cyw43-driver/src/cyw43_ctrl.c.o
    ".text.cyw43_poll_func=.time_critical.cyw43_poll_func|.text.cyw43_bluetooth_hci_read=.time_critical.cyw43_bluetooth_hci_read|.text.cyw43_bluetooth_hci_write=.time_critical.cyw43_bluetooth_hci_write"
    "Relocate CYW43 poll and BT HCI bus to RAM")
relocate_to_ram(lib/btstack/src/hci.c.o
    ".text.hci_run=.time_critical.hci_run|.text.hci_send_acl_packet_fragments=.time_critical.hci_send_acl_packet_fragments|.text.hci_send_acl_packet_buffer=.time_critical.hci_send_acl_packet_buffer"
    "Relocate BTstack HCI TX pump to RAM")
relocate_to_ram(lib/btstack/src/l2cap.c.o
    ".text.l2cap_run=.time_critical.l2cap_run|.text.l2cap_send=.time_critical.l2cap_send|.text.l2cap_send_prepared=.time_critical.l2cap_send_prepared|.text.l2cap_notify_channel_can_send=.time_critical.l2cap_notify_channel_can_send"
    "Relocate BTstack L2CAP TX path to RAM")
relocate_to_ram(lib/cyw43-driver/src/cyw43_ll.c.o
    ".text.cyw43_ll_write_backplane_mem=.time_critical.cyw43_ll_write_backplane_mem|.text.cyw43_ll_read_backplane_mem=.time_critical.cyw43_ll_read_backplane_mem|.text.cyw43_write_backplane=.time_critical.cyw43_write_backplane|.text.cyw43_read_backplane.constprop.0=.time_critical.cyw43_read_backplane"
    "Relocate CYW43 backplane accessors to RAM")
relocate_to_ram(lib/tinyusb/src/device/usbd.c.o
    ".text.tud_task_ext=.time_critical.tud_task_ext|.text.usbd_edpt_xfer=.time_critical.usbd_edpt_xfer|.text.usbd_edpt_xfer_fifo=.time_critical.usbd_edpt_xfer_fifo"
    "Relocate TinyUSB device task path to RAM")
relocate_to_ram(lib/tinyusb/src/portable/raspberrypi/rp2040/dcd_rp2040.c.o
    ".text.dcd_edpt_xfer=.time_critical.dcd_edpt_xfer|.text.dcd_edpt_iso_activate=.time_critical.dcd_edpt_iso_activate"
    "Relocate TinyUSB RP2040 DCD transfer path to RAM")
relocate_to_ram(lib/tinyusb/src/class/audio/audio_device.c.o
    ".text.tud_audio_n_read=.time_critical.tud_audio_n_read|.text.tud_audio_n_write=.time_critical.tud_audio_n_write|.text.audiod_calc_tx_packet_sz.isra.0=.time_critical.audiod_calc_tx_packet_sz|.text.audiod_xfer_isr=.time_critical.audiod_xfer_isr"
    "Relocate TinyUSB audio data path to RAM")
relocate_to_ram(lib/tinyusb/src/class/hid/hid_device.c.o
    ".text.tud_hid_n_report=.time_critical.tud_hid_n_report|.text.tud_hid_n_ready=.time_critical.tud_hid_n_ready|.text.hidd_xfer_cb=.time_critical.hidd_xfer_cb"
    "Relocate TinyUSB HID report path to RAM")
relocate_to_ram(src/common/pico_util/queue.c.o
    ".text.queue_try_add=.time_critical.queue_try_add|.text.queue_try_remove=.time_critical.queue_try_remove"
    "Relocate Pico queue add/remove to RAM")
relocate_to_ram(src/rp2_common/pico_cyw43_arch/cyw43_arch.c.o
    ".text.cyw43_arch_poll=.time_critical.cyw43_arch_poll"
    "Relocate cyw43_arch_poll to RAM")
relocate_to_ram(lib/btstack/src/btstack_run_loop.c.o
    ".text.btstack_run_loop_base_poll_data_sources=.time_critical.btstack_run_loop_base_poll_data_sources|.text.btstack_run_loop_poll_data_sources_from_irq=.time_critical.btstack_run_loop_poll_data_sources_from_irq"
    "Relocate BTstack run-loop poll to RAM")

pico_set_program_name(switchbridge "switchbridge")
pico_set_program_version(switchbridge "0.1.0-dev")
pico_enable_stdio_uart(switchbridge 1)
pico_enable_stdio_usb(switchbridge 0)
pico_add_extra_outputs(switchbridge)
