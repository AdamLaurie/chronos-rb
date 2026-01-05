cmake_minimum_required(VERSION 3.13)

# Initialize the Pico SDK
set(PICO_BOARD pico2_w)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

project(chronos_rb C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# OTA Encryption Key Configuration
# The key file should contain a 32-character hex string (128-bit AES key)
set(OTA_KEY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ota_key.txt")

if(EXISTS "${OTA_KEY_FILE}")
    file(READ "${OTA_KEY_FILE}" OTA_AES_KEY)
    string(STRIP "${OTA_AES_KEY}" OTA_AES_KEY)
    message(STATUS "OTA: Using encryption key from ota_key.txt")
else()
    # Generate a random key if none exists
    string(RANDOM LENGTH 32 ALPHABET "0123456789abcdef" OTA_AES_KEY)
    file(WRITE "${OTA_KEY_FILE}" "${OTA_AES_KEY}")
    message(STATUS "OTA: Generated new encryption key in ota_key.txt")
    message(WARNING "OTA: IMPORTANT - Keep ota_key.txt secret! It's required to create valid firmware updates.")
endif()

# Configure FOTA bootloader with encryption
# Note: Encryption requires pycryptodome: sudo apt install python3-pycryptodome
set(PFB_AES_KEY "${OTA_AES_KEY}" CACHE STRING "FOTA AES encryption key")
set(PFB_WITH_IMAGE_ENCRYPTION ON CACHE BOOL "Enable AES encryption for OTA" FORCE)

# Reserve 4KB at end of flash for persistent config storage
set(PFB_RESERVED_FILESYSTEM_SIZE_KB 4 CACHE STRING "Reserved flash for config")

# Add FOTA bootloader
add_subdirectory(deps/pico_fota_bootloader)

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
    src/pps_generator.c
    src/freq_counter.c
    src/wifi_manager.c
    src/time_discipline.c
    src/web_interface.c
    src/ota_update.c
    # Additional time protocols
    src/time_protocol.c
    src/nmea_output.c
    src/radio_timecode.c
    src/irig_b.c
    src/roughtime.c
    src/gptp.c
    src/nts.c
    # GPS receiver input
    src/gps_input.c
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
pico_generate_pio_header(chronos_rb ${CMAKE_CURRENT_LIST_DIR}/src/pps_generator.pio)
pico_generate_pio_header(chronos_rb ${CMAKE_CURRENT_LIST_DIR}/src/freq_counter.pio)

target_link_libraries(chronos_rb
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_sntp
    pico_flash
    pico_multicore
    pico_fota_bootloader_lib
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_timer
    hardware_clocks
    hardware_pwm
    hardware_adc
    hardware_flash
    hardware_sync
    hardware_uart
)

# Enable USB and UART output
pico_enable_stdio_usb(chronos_rb 1)
pico_enable_stdio_uart(chronos_rb 1)

# Compile with FOTA bootloader support (provides linker script and generates FOTA image)
# Disable encryption for simplicity (can enable later with -DPFB_AES_KEY="...")
pfb_compile_with_bootloader(chronos_rb)

# Generate UF2 file for flashing
pico_add_extra_outputs(chronos_rb)

# Flash targets for UF2 files
# Usage: make flash-bootloader  (first time only)
#        make flash-app         (application updates)
#        make flash-initial     (both bootloader + app for new devices)
#        make flash             (same as flash-app, for OTA-capable devices)

add_custom_target(flash-bootloader
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/flash.sh ${CMAKE_BINARY_DIR}/deps/pico_fota_bootloader/pico_fota_bootloader.uf2
    DEPENDS pico_fota_bootloader
    COMMENT "Flashing FOTA bootloader (required once for new devices)"
)

add_custom_target(flash-app
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/flash.sh ${CMAKE_BINARY_DIR}/chronos_rb.uf2
    DEPENDS chronos_rb
    COMMENT "Flashing application firmware"
)

add_custom_target(flash
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/flash.sh ${CMAKE_BINARY_DIR}/chronos_rb.uf2
    DEPENDS chronos_rb
    COMMENT "Flashing application firmware"
)

add_custom_target(flash-initial
    COMMAND ${CMAKE_COMMAND} -E echo "Step 1: Flashing bootloader..."
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/flash.sh ${CMAKE_BINARY_DIR}/deps/pico_fota_bootloader/pico_fota_bootloader.uf2
    COMMAND ${CMAKE_COMMAND} -E echo "Step 2: Waiting for reboot..."
    COMMAND ${CMAKE_COMMAND} -E sleep 3
    COMMAND ${CMAKE_COMMAND} -E echo "Step 3: Flashing application..."
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/flash.sh ${CMAKE_BINARY_DIR}/chronos_rb.uf2
    DEPENDS pico_fota_bootloader chronos_rb
    COMMENT "Flashing bootloader + application (for new devices)"
)

# OTA update target
# Usage: make fota [FOTA_HOST=192.168.x.x]
set(FOTA_HOST "172.16.13.40" CACHE STRING "Target device IP for OTA updates")
add_custom_target(fota
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ota.sh ${CMAKE_BINARY_DIR}/chronos_rb_fota_image_encrypted.bin ${FOTA_HOST}
    DEPENDS chronos_rb
    COMMENT "OTA firmware update to ${FOTA_HOST}"
)

# Print OTA update file location after build
add_custom_command(TARGET chronos_rb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Build Complete ==="
    COMMAND ${CMAKE_COMMAND} -E echo "For OTA updates, use: ${CMAKE_BINARY_DIR}/chronos_rb_fota_image_encrypted.bin"
    COMMAND ${CMAKE_COMMAND} -E echo ""
)
