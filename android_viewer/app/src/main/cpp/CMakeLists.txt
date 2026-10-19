cmake_minimum_required(VERSION 3.22.1)
project(lepton_viewer CXX)

set(CMAKE_CXX_STANDARD 20)

# Reuse the portable decoding code from the main repo directly (lib/'s own
# CMakeLists pulls in googletest and desktop-only targets).
set(LEPTON_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../lib)

# Prebuilt OpenCV Android SDK, see fetch_opencv.sh. Static, so only our .so
# (plus libc++_shared) ends up in the APK.
set(OpenCV_STATIC ON)
set(OpenCV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/OpenCV-android-sdk/sdk/native/jni)
find_package(OpenCV REQUIRED core imgproc imgcodecs videoio)

add_library(lepton_viewer SHARED
    native_viewer.cpp
    ${LEPTON_LIB_DIR}/leptonCDCParser.cpp
    ${LEPTON_LIB_DIR}/leptonFrame.cpp
    ${LEPTON_LIB_DIR}/voisp.cpp)
target_include_directories(lepton_viewer PRIVATE ${LEPTON_LIB_DIR})
target_link_libraries(lepton_viewer ${OpenCV_LIBS} android log)
