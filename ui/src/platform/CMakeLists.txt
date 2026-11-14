# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

find_package(Threads REQUIRED)

diskos_module(
    platform
    SOURCES message_queue.cpp player_link.cpp usb_dac.cpp route_store.cpp
    DEPENDS diskos::player diskos::output diskos::utils Threads::Threads rt
)
diskos_test(
    platform
    SOURCES player_link_test.cpp usb_dac_test.cpp route_store_test.cpp
    DEPENDS diskos::platform
)
