# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# diskOS UI (LVGL 9.2.2) for the FiiO Snowsky Disc. Build with a preset from CMakePresets.json:
#   cmake --preset device && cmake --build --preset device      mq_ui for the Disc
#   cmake --preset host && ctest --preset host                  the C++ tests, with sanitizers
# This file is the outline; each part of the build is a file in cmake/.
cmake_minimum_required(VERSION 3.25)
project(diskos_ui LANGUAGES C CXX ASM)

include(cmake/flags.cmake)    # diskos_reproducible, diskos_size_optimized
include(cmake/cxx.cmake)      # C++23 and its warnings, diskos_module(), diskos_test()
if(NOT CMAKE_CROSSCOMPILING)
    include(cmake/host.cmake) # sanitizers and Catch2
endif()

add_subdirectory(src)         # the C++ modules (and, on the host, their tests)

if(CMAKE_CROSSCOMPILING)
    include(cmake/device.cmake)  # mq_ui and its helpers
endif()
