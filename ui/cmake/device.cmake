# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# The device build (preset device): its inputs and options, then mq_ui and its helpers, one file
# per part. The preset reads STOCK_SYSROOT and STOCK_AV_HEADERS from the environment (README.md).

set(STOCK_SYSROOT "" CACHE PATH
    "Your extracted stock V2.57 root filesystem (lib/, usr/lib/) for diskos-audiodec")
set(STOCK_AV_HEADERS "" CACHE FILEPATH "ffmpeg-4.2.2.tar.xz: only its public headers are used")
set(DISKOS_DEP_OUT "" CACHE PATH
    "Where tools/build-native-deps.py puts the third-party libraries (shareable)")
if(NOT DISKOS_DEP_OUT)
    set(DISKOS_DEP_OUT "${CMAKE_BINARY_DIR}/deps")
endif()
if(NOT STOCK_SYSROOT OR NOT STOCK_AV_HEADERS)
    message(FATAL_ERROR "set STOCK_SYSROOT and STOCK_AV_HEADERS (environment or -D): see README.md")
endif()

option(DISKOS_PROFILE "Measurement build: compile in prof.c (never shipped)" OFF)
option(DISKOS_FRAME_LOG "Capture build: log all player traffic to /tmp (never shipped)" OFF)
option(DISKOS_TEST_OUTPUTS "Device-test build: show SPDIF Output and USB Audio" OFF)
option(DISKOS_TEST_BT_PREPARE "Bluetooth preparation experiment (use its own build directory)" OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/third_party.cmake)     # third_party::* libraries
include(cmake/audiodec.cmake)        # the stock-library helper embedded in mq_ui
include(cmake/legacy_sources.cmake)  # the C sources, in link order
include(cmake/mq_ui.cmake)           # the UI binary
include(cmake/helpers.cmake)         # diskos-bootprobe, diskos-artdec (built on request)
include(cmake/analysis.cmake)        # clang-tidy and cppcheck over the C++ modules
