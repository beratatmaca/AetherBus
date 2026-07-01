# Portable CMake toolchain for cross-compiling AetherBus to 64-bit Windows with
# MinGW-w64.
#
# NOTE: the supported Docker flow (scripts/build-windows.sh) does NOT use this
# file — it uses Fedora's `mingw64-cmake` wrapper, which additionally sets
# QT_HOST_PATH so Qt's moc/rcc/uic run as native host tools during the cross
# build. Use this file directly only if you have your own MinGW-w64 sysroot with
# Qt 6 installed and a matching host Qt (pass -DQT_HOST_PATH=/usr or similar):
#
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw.cmake \
#         -DQT_HOST_PATH=/usr -DBUILD_TESTING=OFF
#
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

# Default to the Fedora mingw64 sysroot layout; override with
# -DCMAKE_FIND_ROOT_PATH=/path/to/sysroot for MXE / MSYS2 / custom trees.
if(NOT CMAKE_FIND_ROOT_PATH)
    set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX}/sys-root/mingw)
endif()

# Search host paths for programs, but only the target sysroot for libs/headers.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
