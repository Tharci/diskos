// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_controller.h"

#include <cstdint>
#include <optional>

#include "legacy/ipc_command_sender.hpp"
#include "legacy/player_state.h"
#include "legacy/player_state.hpp"
#include "player/modes.hpp"
#include "services/playback/player_controller.hpp"

namespace diskos::legacy {
namespace {

playback::PlayerController& controller() {
    static IpcCommandSender sender{IpcCommandSender::Policy::User};
    static playback::PlayerController controller{sender, player_state(DISKOS_PLAYER_LOCAL)};
    return controller;
}

int status(const playback::PlayerController::SendResult& result) {
    return result ? 0 : -1;
}

}  // namespace
}  // namespace diskos::legacy

extern "C" int diskos_playback_toggle(void) {
    using namespace diskos::legacy;
    return status(controller().toggle(diskos::playback::PlayerState::Clock::now()));
}

extern "C" int diskos_playback_next(void) {
    using namespace diskos::legacy;
    return status(controller().next());
}

extern "C" int diskos_playback_previous(void) {
    using namespace diskos::legacy;
    return status(controller().previous());
}

extern "C" int diskos_playback_seek(uint32_t position_ms) {
    using namespace diskos::legacy;
    return status(controller().seek(position_ms));
}

extern "C" int diskos_playback_set_volume(int level) {
    using namespace diskos::legacy;
    const std::optional<diskos::player::VolumeLevel> volume =
            level < 0 ? std::nullopt
                      : diskos::player::VolumeLevel::from(static_cast<std::uint32_t>(level));
    return volume ? status(controller().set_volume(*volume)) : -1;
}

extern "C" int diskos_playback_set_play_mode(int mode) {
    using namespace diskos::legacy;
    using diskos::player::PlayMode;
    if (mode < 0 || mode > static_cast<int>(PlayMode::Single)) {
        return -1;
    }
    return status(controller().set_play_mode(static_cast<PlayMode>(mode)));
}

extern "C" int diskos_playback_set_favourite(int favourite) {
    using namespace diskos::legacy;
    return status(controller().set_favourite(favourite != 0));
}
