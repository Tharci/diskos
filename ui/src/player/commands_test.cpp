// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/commands.hpp"

#include <expected>
#include <string>

#include "player/frame.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

// The expected frames are as sent on the Disc (capture of 2026-11-01), with the play-list names
// replaced. The two that were not captured (marked) are as the legacy code sends them.

TEST_CASE("encode_command encodes playback control", "[player][commands]") {
    CHECK(encode_command(PlayPauseCommand{}) == "0201000C0000");
    CHECK(encode_command(NextTrackCommand{}) == "0201000C0001");
    CHECK(encode_command(PreviousTrackCommand{}) == "0201000C0002");
    CHECK(encode_command(RequestStateCommand{}) == "02020008");
    CHECK(encode_command(SeekCommand{.position_ms = 0x00C98DA8}) == "0103001000C98DA8");
    CHECK(encode_command(SetVolumeCommand{.level = 0x3F}) == "0715000C003F");
    CHECK(encode_command(SetPlayModeCommand{.mode = PlayMode::Single}) == "0102000C0004");
    CHECK(encode_command(SetFavouriteCommand{.favourite = true}) == "0104000C0001");
}

TEST_CASE("encode_command encodes play lists", "[player][commands]") {
    CHECK(encode_command(PlayListCommand{.type = ListType::All, .position = 0x61A, .name = ""})
          == "01000010061A0001");
    CHECK(encode_command(PlayListCommand{.type = ListType::ReservedSlot, .position = 0, .name = ""})
          == "0100001000000005");
    CHECK(encode_command(PlayListCommand{.type = ListType::Favourites, .position = 8, .name = ""})
          == "0100001000080006");
    CHECK(encode_command(
                  PlayListCommand{.type = ListType::Album, .position = 1, .name = "Some Album"}
          )
          == "0100001A00010003Some Album");
    CHECK(encode_command(
                  PlayListCommand{.type = ListType::Artist, .position = 0, .name = "Some Artist"}
          )
          == "0100001B00000002Some Artist");
    // A jump within the current queue (legacy ui_queue_jump).
    CHECK(encode_command(PlayListCommand{.type = ListType::CurrentQueue, .position = 3, .name = ""})
          == "0100001000030000");
}

TEST_CASE("encode_command encodes the source and route sequence", "[player][commands]") {
    CHECK(encode_command(SwitchSourceCommand{.source = Source::Local}) == "0657000C0008");
    CHECK(encode_command(SwitchSourceCommand{.source = Source::AirPlay}) == "0657000C000A");
    CHECK(encode_command(RouteOutputCommand{.route = OutputRoute::InternalDac}) == "0666000C0006");
    CHECK(encode_command(RouteOutputCommand{.route = OutputRoute::BluetoothSource})
          == "0666000C0002");
    CHECK(encode_command(RouteOutputCommand{.route = OutputRoute::Spdif})
          == "0666000C0004");  // legacy
    CHECK(encode_command(SelectUsbGadgetCommand{.gadget = UsbGadget::UsbDac}) == "0642000C0002");
    CHECK(encode_command(SelectUsbGadgetCommand{.gadget = UsbGadget::UsbHostAudio})
          == "0642000C0005");  // legacy
    CHECK(encode_command(PrepareBluetoothSinkCommand{}) == "0818000C0000");
}

TEST_CASE("encode_command refuses what cannot be sent", "[player][commands]") {
    CHECK(encode_command(SetVolumeCommand{.level = kMaxVolume}).has_value());
    CHECK(encode_command(SetVolumeCommand{.level = kMaxVolume + 1})
          == std::unexpected(CommandError::BadValue));

    const std::string longest(kMaxListNameSize, 'x');
    CHECK(encode_command(PlayListCommand{.type = ListType::Album, .position = 0, .name = longest})
                  .has_value());
    CHECK(encode_command(
                  PlayListCommand{.type = ListType::Album, .position = 0, .name = longest + "x"}
          )
          == std::unexpected(CommandError::TooLong));
}

TEST_CASE("every encoded command is a well-formed frame", "[player][commands]") {
    const auto frame = encode_command(
            PlayListCommand{.type = ListType::Album, .position = 1, .name = R"(Say "Hi" / 1)"}
    );
    REQUIRE(frame.has_value());
    const auto parsed = parse_frame(*frame);
    REQUIRE(parsed.has_value());
    CHECK(parsed->tag == "0100");
    CHECK(parsed->body == R"(00010003Say "Hi" / 1)");
}

}  // namespace
}  // namespace diskos::player
