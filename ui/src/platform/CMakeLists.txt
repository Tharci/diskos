# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

find_package(Threads REQUIRED)

diskos_module(
    platform
    SOURCES message_queue.cpp player_link.cpp
    DEPENDS diskos::player diskos::utils Threads::Threads rt
)
diskos_test(platform SOURCES player_link_test.cpp DEPENDS diskos::platform)
