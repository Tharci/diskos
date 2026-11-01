# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(legacy SOURCES player_reports.cpp DEPENDS diskos::player)
diskos_test(legacy SOURCES player_reports_test.cpp DEPENDS diskos::legacy)
