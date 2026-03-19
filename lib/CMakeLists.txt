add_subdirectory(leptonSDKEmb32PUB)

add_library(leptonLib lepton.cpp voisp.cpp)
target_include_directories(leptonLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} leptonSDKEmb32PUB/include)
target_link_libraries(leptonLib PUBLIC gpiod leptonSDK ${OpenCV_LIBS})

if(LIBSERIAL_LIBRARY)
    add_library(leptonCDCLib leptonCDC.cpp)
    target_include_directories(leptonCDCLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${LIBSERIAL_INCLUDE_DIR})
    target_link_libraries(leptonCDCLib PUBLIC leptonLib ${LIBSERIAL_LIBRARY})
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
