// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/ipc_command_sender.hpp"

#include <string>

extern "C" {
#include "ipc.h"
}

namespace diskos::legacy {

std::expected<void, player::SendError> IpcCommandSender::send(
        const player::Command& command, std::optional<player::PlayerInstance> instance
) {
    const std::string frame = player::frame(command);
    if (!instance) {
        if (ipc_send_cmd(frame.c_str()) != 0) {
            return std::unexpected{player::SendError::Failed};
        }
        return {};
    }
    const int sent = ipc_send_automatic(frame.c_str(), instance->number() - 1);
    if (sent == IPC_SEND_DEFERRED) {
        return std::unexpected{player::SendError::WrongInstance};
    }
    if (sent != 0) {
        return std::unexpected{player::SendError::Failed};
    }
    return {};
}

}  // namespace diskos::legacy
