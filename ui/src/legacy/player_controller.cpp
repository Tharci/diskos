// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_controller.h"

#include <cstdint>
#include <optional>

#include "legacy/ipc_command_sender.hpp"
#include "legacy/player_controller.hpp"
#include "legacy/player_state.h"
#include "legacy/player_state.hpp"
#include "player/commands.hpp"
#include "player/modes.hpp"
#include "services/playback/player_controller.hpp"
#include "services/playback/player_state.hpp"

namespace diskos::legacy {

playback::PlayerController& player_controller() {
    static IpcCommandSender sender{IpcCommandSender::Policy::User};
    static playback::PlayerController controller{sender, player_state(DISKOS_PLAYER_LOCAL)};
    return controller;
}

namespace {

int status(const playback::PlayerController::SendResult& result) {
    return result ? 0 : -1;
}

/** The list type `value` stands for in a 0100 command, or nullopt if it stands for none. */
std::optional<player::ListType> list_type_of(int value) {
    using enum player::ListType;
    for (const player::ListType type :
         {CurrentQueue,
          All,
          Artist,
          Album,
          Folder,
          ReservedSlot,
          Favourites,
          ArtistAlbum,
          GenreAlbum,
          Genre}) {
        if (static_cast<int>(type) == value) {
            return type;
        }
    }
    return std::nullopt;
}

}  // namespace
}  // namespace diskos::legacy

extern "C" int diskos_playback_toggle(void) {
    using namespace diskos::legacy;
    return status(player_controller().toggle(diskos::playback::PlayerState::Clock::now()));
}

extern "C" int diskos_playback_next(void) {
    using namespace diskos::legacy;
    return status(player_controller().next());
}

extern "C" int diskos_playback_previous(void) {
    using namespace diskos::legacy;
    return status(player_controller().previous());
}

extern "C" int diskos_playback_seek(uint32_t position_ms) {
    using namespace diskos::legacy;
    return status(player_controller().seek(position_ms));
}

extern "C" int diskos_playback_set_volume(int level) {
    using namespace diskos::legacy;
    const std::optional<diskos::player::VolumeLevel> volume =
            level < 0 ? std::nullopt
                      : diskos::player::VolumeLevel::from(static_cast<std::uint32_t>(level));
    return volume ? status(player_controller().set_volume(*volume)) : -1;
}

extern "C" int diskos_playback_set_play_mode(int mode) {
    using namespace diskos::legacy;
    using diskos::player::PlayMode;
    if (mode < 0 || mode > static_cast<int>(PlayMode::Single)) {
        return -1;
    }
    return status(player_controller().set_play_mode(static_cast<PlayMode>(mode)));
}

extern "C" int diskos_playback_play_list(int list_type, unsigned position, const char* name) {
    using namespace diskos::legacy;
    using diskos::player::ListName;
    constexpr unsigned kMaxPosition = 0xFFFF;
    const auto type = list_type_of(list_type);
    const std::optional<ListName> list_name = ListName::from(name != nullptr ? name : "");
    if (!type || position > kMaxPosition || !list_name) {
        return -1;
    }
    const diskos::player::PlayListCommand list{
            .type = *type,
            .position = static_cast<std::uint16_t>(position),
            .name = *list_name,
    };
    return status(player_controller().play_list(list, diskos::playback::PlayerState::Clock::now()));
}

extern "C" int diskos_playback_starting(void) {
    using diskos::playback::PlayState;
    return diskos::legacy::player_state(DISKOS_PLAYER_LOCAL).with([](const auto& state) {
        return state.play_state() == PlayState::Loading ? 1 : 0;
    });
}

extern "C" int diskos_playback_set_favourite(int favourite) {
    using namespace diskos::legacy;
    return status(player_controller().set_favourite(favourite != 0));
}
