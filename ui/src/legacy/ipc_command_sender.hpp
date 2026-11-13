// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include "player/command_sender.hpp"
#include "player/commands.hpp"
#include "player/player_instance.hpp"

namespace diskos::legacy {

/**
 * Sends commands through the legacy IPC (ipc.c). Its policy says how a send is treated; an
 * instance, when given, only says where it may go. The legacy generation is the instance number
 * minus one.
 */
class IpcCommandSender : public player::CommandSender {
public:
    enum class Policy : std::uint8_t {
        // ipc_send_cmd: a failed send raises the error flag, and a transport command cancels the
        // legacy position recovery.
        User,
        // ipc_send_probe: silent, also before the player has answered (the output router asks a
        // starting player for its state).
        Background,
    };

    explicit IpcCommandSender(Policy policy) : policy_(policy) {}

    /**
     * Send `command`'s frame; with `instance`, only if that player instance is still the connected
     * one.
     * Returns:
     *  - SendError::WrongInstance if `instance` is given and is not the connected one
     *  - SendError::Failed if it was not queued
     */
    [[nodiscard]] std::expected<void, player::SendError> send(
            const player::Command& command,
            std::optional<player::PlayerInstance> instance = std::nullopt
    ) override;

private:
    Policy policy_;
};

}  // namespace diskos::legacy
