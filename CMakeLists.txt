cmake_minimum_required(VERSION 3.20)
project(lepton)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED)
message(STATUS "Found OpenCV ${OpenCV_VERSION}")

# Direct SPI/I2C/GPIO access to the Lepton (spi_viewer, leptonLib) is
# Raspberry Pi/Linux-only.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(GPIOD_LIBRARY NAMES libgpiod.so)
    if(NOT GPIOD_LIBRARY)
        message(FATAL_ERROR "gpiod library not found. Install: apt install libgpiod-dev")
    endif()
endif()

find_path(LIBSERIAL_INCLUDE_DIR SerialPort.h
    PATH_SUFFIXES include
    PATHS /usr /usr/local /usr/include/libserial)
find_library(LIBSERIAL_LIBRARY
    NAMES libserial.so
    PATH_SUFFIXES lib
    PATHS /usr/lib)

add_subdirectory(lib)

# cdc_viewer (readCDC_gui) is portable — builds without gpiod/LibSerial on
# non-Linux platforms (e.g. macOS), using a built-in POSIX/termios reader.
add_subdirectory(cdc_viewer)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(spi_viewer)
    if(LIBSERIAL_LIBRARY)
        message(STATUS "LibSerial found — building mandeye")
        add_subdirectory(mandeye)
    else()
        message(STATUS "LibSerial not found — skipping mandeye")
    endif()
endif()