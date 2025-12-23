cmake_minimum_required(VERSION 3.13)

# Initialize the Pico SDK
set(PICO_BOARD pico2_w)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

project(chronos_rb C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(chronos_rb
    src/main.c
    src/cli.c
    src/config.c
    src/pulse_output.c
    src/ac_freq_monitor.c
    src/ntp_server.c
    src/ptp_server.c
    src/rubidium_sync.c
    src/pps_capture.c
    src/freq_counter.c
    src/wifi_manager.c
    src/time_discipline.c
    src/web_interface.c
)

target_include_directories(chronos_rb PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Flash safety: we only use core 0, so core 1 is always safe
target_compile_definitions(chronos_rb PRIVATE
    PICO_FLASH_ASSUME_CORE1_SAFE=1
)

# Enable PIO for precise timing
pico_generate_pio_header(chronos_rb ${CMAKE_CURRENT_LIST_DIR}/src/pps_capture.pio)
pico_generate_pio_header(chronos_rb ${CMAKE_CURRENT_LIST_DIR}/src/freq_counter.pio)

target_link_libraries(chronos_rb
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_sntp
    pico_flash
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_timer
    hardware_clocks
    hardware_pwm
    hardware_adc
    hardware_flash
    hardware_sync
)

# Enable USB and UART output
pico_enable_stdio_usb(chronos_rb 1)
pico_enable_stdio_uart(chronos_rb 1)

# Generate UF2 file for flashing
pico_add_extra_outputs(chronos_rb)

# Flash target: reboot into bootloader, wait for mount, copy UF2
add_custom_target(flash
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/flash.sh ${CMAKE_BINARY_DIR}/chronos_rb.uf2
    DEPENDS chronos_rb
)
