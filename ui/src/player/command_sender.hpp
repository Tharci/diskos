// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <expected>

#include "player/commands.hpp"

namespace diskos::player {

enum class SendError : std::uint8_t {
    Failed,         // It was not queued
    WrongInstance,  // It was meant for a player instance that is no longer connected
};

/**
 * Interface for communicating with the stock player.
 */
class CommandSender {
public:
    CommandSender() = default;
    CommandSender(const CommandSender&) = delete;
    CommandSender(CommandSender&&) = delete;
    CommandSender& operator=(const CommandSender&) = delete;
    CommandSender& operator=(CommandSender&&) = delete;
    virtual ~CommandSender() = default;

    /** Send `command`. Returns SendError::Failed if it was not queued. */
    [[nodiscard]] virtual std::expected<void, SendError> send(const Command& command) = 0;
};

}  // namespace diskos::player
