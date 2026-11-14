// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <expected>

#include "player/command_sender.hpp"

namespace diskos::playback {

/**
 * Readies the player for a play that starts while nothing plays: what PlayerController asks
 * right before it sends one. The output router implements it with the play preamble: an idle
 * player drifts its output and drops its work mode, so a play from idle needs both re-asserted.
 */
class PlayPreparer {
public:
    using SendResult = std::expected<void, player::SendError>;

    PlayPreparer() = default;
    PlayPreparer(const PlayPreparer&) = delete;
    PlayPreparer(PlayPreparer&&) = delete;
    PlayPreparer& operator=(const PlayPreparer&) = delete;
    PlayPreparer& operator=(PlayPreparer&&) = delete;
    virtual ~PlayPreparer() = default;

    /**
     * Ready the player for a play from idle, by sending what it needs before the play command.
     * Returns a SendError if a frame was not sent: the play would then fail or hang.
     */
    [[nodiscard]] virtual SendResult prepare_play() = 0;
};

}  // namespace diskos::playback
