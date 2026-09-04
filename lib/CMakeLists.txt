# Direct SPI/I2C/GPIO access to the Lepton (leptonLib) only makes sense on
# Linux (Raspberry Pi), so it's kept out of the macOS/CDC-viewer build entirely.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(leptonSDKEmb32PUB)

    add_library(leptonLib lepton.cpp voisp.cpp)
    target_include_directories(leptonLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} leptonSDKEmb32PUB/include)
    target_link_libraries(leptonLib PUBLIC gpiod leptonSDK ${OpenCV_LIBS})
endif()

# Portable Lepton VoSPI frame decoding (no SPI/I2C/GPIO/SDK dependency) —
# used by the CDC viewers on every platform.
add_library(leptonFrameLib leptonFrame.cpp voisp.cpp)
target_include_directories(leptonFrameLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(leptonFrameLib PUBLIC ${OpenCV_LIBS})

# CDC (USB-serial-to-Pico) transport. Uses LibSerial on Linux (so still
# requires it there, same as before); on other platforms it falls back to a
# small built-in POSIX/termios reader (see PicoSerialMac.h), so no LibSerial
# dependency is required there.
if(LIBSERIAL_LIBRARY OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(leptonCDCLib leptonCDC.cpp)
    target_include_directories(leptonCDCLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    if(LIBSERIAL_LIBRARY)
        target_include_directories(leptonCDCLib PUBLIC ${LIBSERIAL_INCLUDE_DIR})
        target_link_libraries(leptonCDCLib PUBLIC ${LIBSERIAL_LIBRARY})
    endif()
endif()

# Unit tests
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)
enable_testing()
add_executable(test_simple test.cpp)
target_link_libraries(test_simple gtest_main)
add_test(NAME simple_tests COMMAND test_simple)
