# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Host builds (presets host and host-tsan): the C++ modules and their Catch2 tests, run under
# sanitizers. Nothing device-specific is configured here.

set(DISKOS_SANITIZERS "" CACHE STRING
    "Sanitizers for every host target, e.g. address,undefined or thread (empty: none)")
if(DISKOS_SANITIZERS)
    # Stop at the first report: a sanitizer finding fails the test that caused it.
    add_compile_options(
        -fsanitize=${DISKOS_SANITIZERS} -fno-sanitize-recover=all -fno-omit-frame-pointer
    )
    add_link_options(-fsanitize=${DISKOS_SANITIZERS})
endif()

find_package(Catch2 3 REQUIRED)
include(Catch)
enable_testing()
