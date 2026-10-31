# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Cross toolchain for the Disc: the pinned mipsel-linux-musl GCC (MIPS32r2, FP64, NaN2008) from the
# diskos-ui-builder image. The prefix comes from $CROSS (set in the image) or -DDISKOS_CROSS=...

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)

if(NOT DISKOS_CROSS)
    if(DEFINED ENV{CROSS})
        set(DISKOS_CROSS "$ENV{CROSS}")
    else()
        set(DISKOS_CROSS "mipsel-linux-musl-")
    endif()
endif()
set(DISKOS_CROSS "${DISKOS_CROSS}" CACHE STRING
    "Cross toolchain prefix, e.g. /opt/x/bin/mipsel-linux-musl-")

set(CMAKE_C_COMPILER "${DISKOS_CROSS}gcc")
set(CMAKE_CXX_COMPILER "${DISKOS_CROSS}g++")
set(CMAKE_ASM_COMPILER "${DISKOS_CROSS}gcc")

# Everything is linked statically: checks must not try to run or link target programs.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
