# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 diskOS contributors

diskos_module(utils SOURCES numbers.cpp bluetooth_address.cpp)
diskos_test(utils SOURCES numbers_test.cpp synchronized_test.cpp bluetooth_address_test.cpp DEPENDS diskos::utils)
