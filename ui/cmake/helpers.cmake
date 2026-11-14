# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors
# Small static helpers the image builder installs in /opt/diskos/bin. Built on request:
#   cmake --build --preset device -t diskos-bootprobe diskos-artdec

# diskos_helper(<name> <source>): a stripped, statically linked helper outside the default build.
function(diskos_helper name source)
    add_executable(${name} EXCLUDE_FROM_ALL ${source})
    target_link_libraries(${name} PRIVATE diskos_reproducible)
    target_link_options(${name} PRIVATE -static)
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

# The boot selector's probe.
diskos_helper(diskos-bootprobe tools/diskos_bootprobe.c)
target_compile_options(diskos-bootprobe PRIVATE -Os -Wall -Wextra -Werror)

# Extracts and decodes covers in its own process, so no stock FFmpeg is needed (stock V2.57's has
# no image decoders). stb_image.h is vendored.
diskos_helper(diskos-artdec tools/diskos_artdec.c)
target_compile_options(diskos-artdec PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(diskos-artdec PRIVATE m)
