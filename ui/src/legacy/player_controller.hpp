// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include "services/playback/player_controller.hpp"

namespace diskos::legacy {

/**
 * Get the PlayerController that the diskos_playback_* functions (player_controller.h) and the
 * output router use, over the local PlayerState. It lives as long as the program; main thread
 * only.
 */
playback::PlayerController& player_controller();

}  // namespace diskos::legacy
