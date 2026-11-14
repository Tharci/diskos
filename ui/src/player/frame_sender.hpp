// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <expected>
#include <optional>
#include <string_view>

#include "player/command_sender.hpp"
#include "player/commands.hpp"
#include "player/player_instance.hpp"

namespace diskos::player {

/**
 * Sends frames (see frame.hpp) to the stock player: the counterpart of FrameListener.
 */
class FrameSender {
public:
    FrameSender() = default;
    FrameSender(const FrameSender&) = delete;
    FrameSender(FrameSender&&) = delete;
    FrameSender& operator=(const FrameSender&) = delete;
    FrameSender& operator=(FrameSender&&) = delete;
    virtual ~FrameSender() = default;

    /**
     * Send `frame`; with `instance`, only if that player instance is still the connected one.
     * Returns:
     *  - SendError::WrongInstance if `instance` is given and is not the connected one
     *  - SendError::Failed if it was not queued
     */
    [[nodiscard]] virtual std::expected<void, SendError> send_frame(
            std::string_view frame, std::optional<PlayerInstance> instance = std::nullopt
    ) = 0;
};

/**
 * Sends commands as their frames, through a FrameSender. It keeps a pointer to the sender, which
 * must outlive it.
 */
class FrameCommandSender final : public CommandSender {
public:
    explicit FrameCommandSender(FrameSender& sender) : sender_(&sender) {}

    /** Send `command`'s frame, as FrameSender::send_frame() does. */
    [[nodiscard]] std::expected<void, SendError> send(
            const Command& command, std::optional<PlayerInstance> instance = std::nullopt
    ) override {
        return sender_->send_frame(frame(command), instance);
    }

private:
    FrameSender* sender_;
};

}  // namespace diskos::player
