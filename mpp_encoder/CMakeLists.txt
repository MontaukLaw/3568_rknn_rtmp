cmake_minimum_required(VERSION 3.5.1)

# This file is used for cross compilation on hisi chip platform

set(CMAKE_SYSTEM_NAME Linux)

set(TOOLCHAIN_DIR /opt/atk-dlrk356x-toolchain)
set(CMAKE_C_COMPILER ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-c++)
# set(SYSROOT ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu//sysroot/usr/include)
# set(CMAKE_SYSROOT ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu/sysroot)
include_directories(${SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

project(MPPEncode)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 14)

# set(CMAKE_CXX_FLAGS "-O0 -g")

include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/dev/include
        ${CMAKE_CURRENT_LIST_DIR}/dev/include/rockchip
        /home/alientek/mpp/inc
        /home/alientek/mpp/osal/inc
        /home/alientek/mpp/utils/
        /home/alientek/mpp/mpp/base/inc/
)

link_directories(
        /home/alientek/mpp/build/linux/aarch64/mpp/
        ${CMAKE_CURRENT_LIST_DIR}/dev/lib
)

AUX_SOURCE_DIRECTORY(. SRCS)
# add_executable(MPPEncode ${SRCS} MyEncoder.cpp MyEncoder.h main.cpp mpi_enc_utils.c utils.c iniparser.c mpp_opt.c dictionary.c)
# target_link_libraries(MPPEncode
#         rockchip_mpp
#         # utils
#         pthread
#         c
#         # opencv_imgcodecs
#         # opencv_core
#         # opencv_imgproc
#         # opencv_highgui
#         # opencv_videoio
#         )

add_library(encoder_api SHARED encoder_api.cpp MyEncoder.cpp MyEncoder.h mpi_enc_utils.c utils.c iniparser.c mpp_opt.c dictionary.c) 
target_link_libraries(encoder_api
        rockchip_mpp
        pthread
        c) 

add_executable(encoder_api_test test.cpp) 
target_link_libraries(encoder_api_test encoder_api
                rockchip_mpp
                pthread
                c) 
        