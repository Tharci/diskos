// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include "legacy/player_state.h"
#include "services/playback/player_state.hpp"
#include "utils/synchronized.hpp"

namespace diskos::legacy {

/**
 * Get the PlayerState of `slot`, which the diskos_player_* functions (player_state.h) apply the
 * legacy C's frames to. It lives as long as the program.
 */
utils::Synchronized<playback::PlayerState>& player_state(diskos_player_slot slot);

}  // namespace diskos::legacy
