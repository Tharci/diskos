// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/ipc_command_sender.hpp"

#include <string>

extern "C" {
#include "ipc.h"
}

namespace diskos::legacy {
namespace {

/** The number of `instance`, or of the one the legacy IPC delivered last without. */
unsigned number_of(std::optional<player::PlayerInstance> instance) {
    return instance ? instance->number() : ipc_instance();
}

}  // namespace

std::expected<void, player::SendError> IpcCommandSender::send(
        const player::Command& command, std::optional<player::PlayerInstance> instance
) {
    const std::string frame = player::frame(command);
    int sent = 0;
    if (number_of(instance) != ipc_instance()) {
        // Checked just before the send, not with it: a restart in between still lets it through,
        // as every command did before instances.
        sent = IPC_SEND_DEFERRED;
    } else if (policy_ == Policy::Background) {
        sent = ipc_send_probe(frame.c_str());
    } else {
        sent = ipc_send_cmd(frame.c_str());
    }
    if (sent == IPC_SEND_DEFERRED) {
        return std::unexpected{player::SendError::WrongInstance};
    }
    if (sent != 0) {
        return std::unexpected{player::SendError::Failed};
    }
    return {};
}

}  // namespace diskos::legacy
