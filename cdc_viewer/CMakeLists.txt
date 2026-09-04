# readCDC is a single-device Linux-only CLI tool (hardcoded /dev path,
# direct LibSerial + leptonLib use). readCDC_gui is the portable multi-device
# GUI viewer and only needs leptonCDCLib/leptonFrameLib, so it builds
# everywhere (including macOS).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND LIBSERIAL_LIBRARY)
    add_executable(readCDC readCDC.cpp)
    target_include_directories(readCDC PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LIBSERIAL_INCLUDE_DIR})
    target_link_libraries(readCDC PRIVATE leptonLib ${LIBSERIAL_LIBRARY})
endif()

add_executable(readCDC_gui readCDC_gui.cpp)
target_link_libraries(readCDC_gui PRIVATE leptonCDCLib leptonFrameLib)