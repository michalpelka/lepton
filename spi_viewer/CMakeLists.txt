add_executable(readRawLepton leptonRawRead.cpp)

add_executable(testLepton testLeptonApp.cpp)
target_link_libraries(testLepton PRIVATE leptonLib)