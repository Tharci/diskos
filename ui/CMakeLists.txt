# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# diskOS UI (LVGL 9.2.2) for the FiiO Snowsky Disc. Build with a preset from CMakePresets.json:
#   cmake --preset device && cmake --build --preset device
# The device preset reads STOCK_SYSROOT and STOCK_AV_HEADERS from the environment (see README.md).
# This file holds the inputs and options; the build itself is in cmake/, one file per part.
cmake_minimum_required(VERSION 3.25)
project(diskos_ui LANGUAGES C ASM)

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
option(DISKOS_TEST_OUTPUTS "Device-test build: show SPDIF Output and USB Audio" OFF)
option(DISKOS_TEST_BT_PREPARE "Bluetooth preparation experiment (use its own build directory)" OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/flags.cmake)           # diskos_reproducible, diskos_size_optimized
include(cmake/third_party.cmake)     # third_party::* libraries
include(cmake/audiodec.cmake)        # the stock-library helper embedded in mq_ui
include(cmake/legacy_sources.cmake)  # the C sources, in link order
include(cmake/mq_ui.cmake)           # the UI binary
include(cmake/helpers.cmake)         # diskos-bootprobe, diskos-artdec (built on request)
