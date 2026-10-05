# CMake 交叉编译配置 (用于较大的例程 / 第三方库)
# 用法: cmake -B build/xx -S examples/xx -DCMAKE_TOOLCHAIN_FILE=examples/toolchain.cmake
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED ENV{IMX6ULL_ROOT})
  set(ENV{IMX6ULL_ROOT} "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
set(TC_ROOT "$ENV{IMX6ULL_ROOT}/tools/toolchain/armv7-unknown-linux-musleabihf/bin")

set(CMAKE_C_COMPILER   "${TC_ROOT}/armv7-unknown-linux-musleabihf-gcc")
set(CMAKE_CXX_COMPILER "${TC_ROOT}/armv7-unknown-linux-musleabihf-g++")
set(CMAKE_AR           "${TC_ROOT}/armv7-unknown-linux-musleabihf-ar"  CACHE FILEPATH "")
set(CMAKE_STRIP        "${TC_ROOT}/armv7-unknown-linux-musleabihf-strip" CACHE FILEPATH "")

set(CMAKE_SYSTEM_PROCESSOR armv7-a)
add_compile_options(-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
