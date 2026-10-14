cmake_minimum_required(VERSION 3.10)
project (RTSPSERVER)

option(RTSP_FILE_SERVER "Enable RTSP file server support" OFF)
option(DEVICE_LIVE "Enable device live support" OFF)

set(TOOLCHAIN_ROOT "/home/zmq/rv1126b/rockchip/1.2.1/1126B_Linux/buildroot/output/rockchip_invision_ipc/host/bin")
set(CMAKE_C_COMPILER ${TOOLCHAIN_ROOT}/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/aarch64-buildroot-linux-gnu-g++)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(./src/rtsp)
include_directories(./src/rtsp/rtp)
include_directories(./src/rtsp/io_event)
include_directories(./src/rockit)
include_directories(./src/ipc)
include_directories(./src/uart)
include_directories(./src/encode)
include_directories(./3rdparty/rockit/include)

aux_source_directory(./src/rtsp SRC)
aux_source_directory(./src/rtsp/rtp SRC)
aux_source_directory(./src/rtsp/io_event SRC)
aux_source_directory(./src/rockit SRC)
aux_source_directory(./src/encode SRC)
aux_source_directory(./src/ipc SRC)
aux_source_directory(./src/uart SRC)
aux_source_directory(./src SRC)

link_directories(${PROJECT_SOURCE_DIR}/3rdparty/rockit/lib)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR})
add_compile_options(-O2)
set(CMAKE_BUILD_TYPE Release)

add_executable(livestream-simple-rtsp-server ${SRC})
target_link_libraries(livestream-simple-rtsp-server pthread websockets sqlite3)
target_link_libraries(livestream-simple-rtsp-server rockit)
