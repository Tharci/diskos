# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(player SOURCES frame.cpp reports.cpp track_report.cpp DEPENDS diskos::utils)
diskos_test(
    player
    SOURCES frame_test.cpp reports_test.cpp track_report_test.cpp
    DEPENDS diskos::player
)
