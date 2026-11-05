// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/commands.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "player/frame.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

// The expected frames are as sent on the Disc (capture of 2026-11-01), with the play-list names
// replaced. The two that were not captured (marked) are as the legacy code sends them.

VolumeLevel volume(std::uint32_t level) {
    const std::optional<VolumeLevel> volume = VolumeLevel::from(level);
    REQUIRE(volume.has_value());
    return *volume;
}

ListName list_name(std::string name) {
    std::optional<ListName> list_name = ListName::from(std::move(name));
    REQUIRE(list_name.has_value());
    return *std::move(list_name);
}

TEST_CASE("commands build their frames for playback control", "[player][commands]") {
    const SeekCommand seek{.position_ms = 0x00C98DA8};
    const SetVolumeCommand set_volume{.level = volume(0x3F)};
    const SetPlayModeCommand set_mode{.mode = PlayMode::Single};
    const SetFavouriteCommand set_favourite{.favourite = true};

    CHECK(PlayPauseCommand{}.frame() == "0201000C0000");
    CHECK(NextTrackCommand{}.frame() == "0201000C0001");
    CHECK(PreviousTrackCommand{}.frame() == "0201000C0002");
    CHECK(RequestStateCommand{}.frame() == "02020008");
    CHECK(seek.frame() == "0103001000C98DA8");
    CHECK(set_volume.frame() == "0715000C003F");
    CHECK(set_mode.frame() == "0102000C0004");
    CHECK(set_favourite.frame() == "0104000C0001");
}

TEST_CASE("commands build their frames for play lists", "[player][commands]") {
    using enum ListType;
    const PlayListCommand all{.type = All, .position = 0x61A};
    const PlayListCommand slot{.type = ReservedSlot, .position = 0};
    const PlayListCommand favourites{.type = Favourites, .position = 8};
    const PlayListCommand album{.type = Album, .position = 1, .name = list_name("Some Album")};
    const PlayListCommand artist{.type = Artist, .position = 0, .name = list_name("Some Artist")};
    // A jump within the current queue (legacy ui_queue_jump).
    const PlayListCommand jump{.type = CurrentQueue, .position = 3};

    CHECK(all.frame() == "01000010061A0001");
    CHECK(slot.frame() == "0100001000000005");
    CHECK(favourites.frame() == "0100001000080006");
    CHECK(album.frame() == "0100001A00010003Some Album");
    CHECK(artist.frame() == "0100001B00000002Some Artist");
    CHECK(jump.frame() == "0100001000030000");
}

TEST_CASE("commands build their frames for the source and route sequence", "[player][commands]") {
    const SwitchSourceCommand local{.source = Source::Local};
    const SwitchSourceCommand airplay{.source = Source::AirPlay};
    const RouteOutputCommand dac{.route = OutputRoute::InternalDac};
    const RouteOutputCommand bluetooth{.route = OutputRoute::BluetoothSource};
    const RouteOutputCommand spdif{.route = OutputRoute::Spdif};
    const SelectUsbGadgetCommand usb_dac{.gadget = UsbGadget::UsbDac};
    const SelectUsbGadgetCommand usb_audio{.gadget = UsbGadget::UsbHostAudio};

    CHECK(local.frame() == "0657000C0008");
    CHECK(airplay.frame() == "0657000C000A");
    CHECK(dac.frame() == "0666000C0006");
    CHECK(bluetooth.frame() == "0666000C0002");
    CHECK(usb_dac.frame() == "0642000C0002");
    CHECK(PrepareBluetoothSinkCommand{}.frame() == "0818000C0000");
    // Not captured: as the legacy code sends them.
    CHECK(spdif.frame() == "0666000C0004");
    CHECK(usb_audio.frame() == "0642000C0005");
}

TEST_CASE("frame() builds whichever command a Command holds", "[player][commands]") {
    const Command command = SeekCommand{.position_ms = 1000};
    CHECK(frame(command) == "01030010000003E8");
}

TEST_CASE("VolumeLevel and ListName accept only what can be sent", "[player][commands]") {
    const std::string longest(kMaxListNameSize, 'x');

    CHECK(VolumeLevel::from(kMaxVolume).has_value());
    CHECK_FALSE(VolumeLevel::from(kMaxVolume + 1).has_value());
    CHECK(ListName::from(longest).has_value());
    CHECK_FALSE(ListName::from(longest + "x").has_value());
    CHECK(ListName{}.value().empty());
}

TEST_CASE("every command frame is well formed", "[player][commands]") {
    using enum ListType;
    const ListName longest_name = list_name(std::string(kMaxListNameSize, 'x'));
    const ListName quoted_name = list_name(R"(Say "Hi" / 1)");
    const PlayListCommand longest{.type = Album, .position = 0, .name = longest_name};
    const PlayListCommand quoted{.type = Album, .position = 1, .name = quoted_name};

    CHECK(parse_frame(longest.frame()).has_value());

    const std::string quoted_frame = quoted.frame();
    const auto parsed = parse_frame(quoted_frame);
    REQUIRE(parsed.has_value());
    CHECK(parsed->tag == "0100");
    CHECK(parsed->body == R"(00010003Say "Hi" / 1)");
}

}  // namespace
}  // namespace diskos::player
