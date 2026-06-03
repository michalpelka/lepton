pkg_check_modules(ZMQ REQUIRED libzmq)

add_executable(mandeye_lepton main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../lib/ExtrasUtils.cpp)
target_include_directories(mandeye_lepton PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${ZMQ_INCLUDE_DIRS})
target_link_libraries(mandeye_lepton PRIVATE leptonCDCLib ${ZMQ_LIBRARIES})

install(TARGETS mandeye_lepton RUNTIME DESTINATION /opt/mandeye/extras/)
install(FILES services/mandeye_lepton.service DESTINATION /usr/lib/systemd/system)