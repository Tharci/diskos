// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <expected>
#include <optional>

#include "player/command_sender.hpp"
#include "player/commands.hpp"
#include "player/player_instance.hpp"

namespace diskos::legacy {

/**
 * Sends commands through the legacy IPC (ipc.c):
 *  - Without an instance, as user actions (ipc_send_cmd): a failed send raises its error flag, and
 *    a transport command cancels its position recovery.
 *  - With an instance, as automatic sends (ipc_send_automatic): silent, and only to that instance
 *    once it has answered. The legacy generation is the instance number minus one.
 */
class IpcCommandSender : public player::CommandSender {
public:
    /**
     * Send `command`'s frame.
     * Returns:
     *  - SendError::WrongInstance if `instance` is given and is not the connected one, or has not
     *    answered yet
     *  - SendError::Failed if it was not queued
     */
    [[nodiscard]] std::expected<void, player::SendError> send(
            const player::Command& command,
            std::optional<player::PlayerInstance> instance = std::nullopt
    ) override;
};

}  // namespace diskos::legacy
