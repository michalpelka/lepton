cmake_minimum_required(VERSION 3.13)
# --- Enforce 32-bit enums globally ---
# Note: this must NOT be done via set(CMAKE_C_FLAGS ...) before project(),
# since that creates a normal variable that shadows the CACHE variable
# pico-sdk's toolchain file initializes with -mcpu/-mthumb, breaking the
# build. add_compile_options() applies cleanly without clobbering it.
add_compile_options(-fno-short-enums)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type" FORCE)
endif()

include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
# Pull in FreeRTOS
include($ENV{FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

project(lepton_cdc C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()


include_directories(leptonSDKEmb32PUB/include)
set (LEPTON_SDK_FILES
        leptonSDKEmb32PUB/src/crc16fast.c
        leptonSDKEmb32PUB/src/LEPTON_AGC.c
        leptonSDKEmb32PUB/src/LEPTON_I2C_Protocol.c
        leptonSDKEmb32PUB/src/LEPTON_I2C_Service.c
        leptonSDKEmb32PUB/src/LEPTON_OEM.c
        leptonSDKEmb32PUB/src/LEPTON_RAD.c
        leptonSDKEmb32PUB/src/LEPTON_SDK.c
        leptonSDKEmb32PUB/src/LEPTON_SYS.c
        leptonSDKEmb32PUB/src/LEPTON_VID.c
        leptonSDKEmb32PUB/src/raspi_I2C.c
)

add_executable(lepton_cdc
        main.cpp voisp.cpp ${LEPTON_SDK_FILES} base64.cpp
)

target_include_directories(lepton_cdc PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(lepton_cdc
        pico_stdlib
        hardware_i2c
        hardware_spi
        hardware_dma
        hardware_watchdog
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4
)

pico_add_extra_outputs(lepton_cdc)

# enable UART output
pico_enable_stdio_uart(lepton_cdc 0)
pico_enable_stdio_usb(lepton_cdc 1)


add_executable(test_cdc_speed test_cdc_speed.cpp)
target_link_libraries(test_cdc_speed
        pico_stdlib
)
pico_add_extra_outputs(test_cdc_speed)

pico_enable_stdio_uart(test_cdc_speed 0)
pico_enable_stdio_usb(test_cdc_speed 1)
