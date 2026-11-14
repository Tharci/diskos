# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Compile flags shared by the device targets, as INTERFACE targets: a target takes them on by
# linking one.

# Every target: the same sources give the same binary, bit for bit.
add_library(diskos_reproducible INTERFACE)
target_compile_options(diskos_reproducible INTERFACE
    # Strip the source folder from __FILE__ (asserts) and debug info, so the binary does not change
    # when the tree is moved or built elsewhere.
    -ffile-prefix-map=${CMAKE_SOURCE_DIR}/=
    -ffile-prefix-map=${CMAKE_SOURCE_DIR}/lvgl/=lvgl/
    # No fused multiply-add contraction: floating-point results stay exactly those of the separate
    # multiply and add the code is written with (MIPS32r2 otherwise uses madd.s/madd.d).
    -ffp-contract=off
)

# mq_ui's code: small, with every function and object in its own section so the link's
# --gc-sections can drop the unused ones.
add_library(diskos_size_optimized INTERFACE)
target_compile_options(diskos_size_optimized INTERFACE
    -Os -pipe -ffunction-sections -fdata-sections
)
target_link_libraries(diskos_size_optimized INTERFACE diskos_reproducible)
