// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <expected>

#include "player/command_sender.hpp"
#include "player/commands.hpp"

namespace diskos::legacy {

/**
 * Sends commands through the legacy IPC (ipc.c's ipc_send_cmd), as user actions: a failed send
 * raises its error flag, and a transport command cancels its position recovery.
 */
class IpcCommandSender : public player::CommandSender {
public:
    /** Send `command`'s frame. Returns SendError::Failed if ipc_send_cmd did not queue it. */
    [[nodiscard]] std::expected<void, player::SendError> send(
            const player::Command& command
    ) override;
};

}  // namespace diskos::legacy
