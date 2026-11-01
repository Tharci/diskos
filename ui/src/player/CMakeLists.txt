# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(player SOURCES frame.cpp)
diskos_test(player SOURCES frame_test.cpp DEPENDS diskos::player)
