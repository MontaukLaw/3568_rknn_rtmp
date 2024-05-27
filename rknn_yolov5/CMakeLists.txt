cmake_minimum_required(VERSION 3.4.1)

project(rknn_yolov5v7x_demo)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# rknn api
set(PLATFORM RK3566_3568)
set(LIB_ARCH aarch64)
set(RKNN_API_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/rknpu2/runtime/RK356X/Linux/librknn_api)
set(RKNN_RT_LIB ${RKNN_API_PATH}/aarch64/librknnrt.so)
include_directories(${RKNN_API_PATH}/include)

# drm
set(DRM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/common/drm)
include_directories(${DRM_DIR}/include)
include_directories(${DRM_DIR}/include/libdrm)

#rga
set(RGA_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/librga)
set(RGA_LIB ${RGA_PATH}/libs/Linux/gcc-aarch64/librga.so)
include_directories(${RGA_PATH}/include)


set(OPENCV_LIBS opencv_core opencv_flann opencv_videoio opencv_video opencv_highgui opencv_imgcodecs opencv_imgproc)
set(TOOLCHAIN_DIR /opt/atk-dlrk356x-toolchain)
set(CMAKE_C_COMPILER ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-c++)
set(SYSROOT ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu//sysroot/usr/include)
set(CMAKE_SYSROOT ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu/sysroot)
include_directories(${SYSROOT})
include_directories(${SYSROOT}/opencv4)

set(CMAKE_INSTALL_RPATH "lib")

link_directories(libs)

# rknn_yolo_demo
include_directories( ${CMAKE_SOURCE_DIR}/include)
add_executable(rknn_yolo_demo
        src/drm_func.c
        src/rga_func.c
        src/yolo.cc
        src/main.cc
        src/tcp_cmd.cc
)

target_link_libraries(rknn_yolo_demo
        ${RKNN_RT_LIB}
        ${RGA_LIB}
        ${OPENCV_LIBS}
        dl
        encoder_api
        rtmp_pusher_api
)

add_executable(get_sample src/get_sample.cc)
target_link_libraries(get_sample
  ${OPENCV_LIBS}
  dl
)

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/atk_rknn_yolo_v5_demo)
install(TARGETS rknn_yolo_demo DESTINATION ./)

install(PROGRAMS ${RKNN_RT_LIB} DESTINATION lib)
install(PROGRAMS ${RGA_LIB} DESTINATION lib)
install(DIRECTORY model DESTINATION ./)
