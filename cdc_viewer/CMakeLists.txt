add_executable(readCDC readCDC.cpp)
target_include_directories(readCDC PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LIBSERIAL_INCLUDE_DIR})
target_link_libraries(readCDC PRIVATE leptonLib ${LIBSERIAL_LIBRARY})

add_executable(readCDC_gui readCDC_gui.cpp)
target_link_libraries(readCDC_gui PRIVATE leptonCDCLib)