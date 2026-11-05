// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/ipc_command_sender.hpp"

#include <string>

extern "C" {
#include "ipc.h"
}

namespace diskos::legacy {

std::expected<void, player::SendError> IpcCommandSender::send(const player::Command& command) {
    const std::string frame = player::frame(command);
    if (ipc_send_cmd(frame.c_str()) != 0) {
        return std::unexpected{player::SendError::Failed};
    }
    return {};
}

}  // namespace diskos::legacy
