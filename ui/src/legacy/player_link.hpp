// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include "platform/player_link.hpp"

namespace diskos::legacy {

/** Get the link diskos_link_start() started (player_link.h), or nullptr before it did. */
platform::PlayerLink* player_link();

}  // namespace diskos::legacy
