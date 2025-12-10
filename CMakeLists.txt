cmake_minimum_required(VERSION 3.10)
project(yolo_cam_pi4 CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Release)
add_compile_options(-O3 -DNDEBUG -march=armv8-a+simd)
# OpenCV
find_package(OpenCV REQUIRED)

# ncnn
set(CMAKE_PREFIX_PATH "/home/pi/ncnn/build/install" ${CMAKE_PREFIX_PATH})
find_package(ncnn REQUIRED)

# OpenMP (ncnn dùng)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found")
endif()

add_executable(yolo_cam
    main.cpp
    yolo-fastestv2.cpp
    audio_player.cpp
)

target_include_directories(yolo_cam PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${NCNN_INCLUDE_DIRS}
    /home/pi/ncnn/build/install/include
)

target_link_libraries(yolo_cam PRIVATE
    ${OpenCV_LIBS}
    ncnn
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(yolo_cam PRIVATE OpenMP::OpenMP_CXX)
endif()