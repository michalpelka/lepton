cmake_minimum_required(VERSION 3.20)
project(lepton)

set(CMAKE_CXX_STANDARD 20)

# Find gpiod using pkg-config (recommended for Debian/RPi OS)

find_library(GPIOD_LIBRARY NAMES libgpiod.so)
if(NOT GPIOD_LIBRARY)
    message(FATAL_ERROR "gpiod library not found. Install apt install libgpiod-dev")
endif()

find_package(OpenCV REQUIRED)
message(STATUS "Found OpenCV ${OpenCV_VERSION}")

add_subdirectory(leptonSDKEmb32PUB)
include_directories(leptonSDKEmb32PUB/include)

add_executable(readRawLepton leptonRawRead.cpp)
add_executable(readRawLeptonEx leptonRawReadEx.cpp )
target_link_libraries(readRawLeptonEx PRIVATE gpiod)
add_executable(testLepton testLepton.cpp cci.cpp cci.h)
# link OpenCV here
target_link_libraries(testLepton PRIVATE gpiod leptonSDK ${OpenCV_LIBS})
add_executable(captureLepton capture.c)

enable_testing()


# Download and link GoogleTest
include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(test_simple test.cpp)
target_link_libraries(test_simple gtest_main)

add_test(NAME simple_tests COMMAND test_simple)
