// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/reports.hpp"

#include <expected>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "utils/bluetooth_address.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

// The report decoded from `frame` if it is a T, else nullopt.
template <typename T>
std::optional<T> decode_as(std::string_view frame) {
    const Decoded<Report> report = decode_report(frame);
    const T* typed = report ? std::get_if<T>(&*report) : nullptr;
    return typed != nullptr ? std::optional<T>{*typed} : std::nullopt;
}

// Every frame below was received from the player on the Disc (capture of 2026-11-01).

TEST_CASE("decode_report reads positions", "[player][report]") {
    const auto position = decode_as<PositionReport>("a103001000C9174D");

    REQUIRE(position.has_value());
    CHECK(position->position_ms == 0x00C9174D);
}

TEST_CASE("decode_report reads play modes, volume, source and battery", "[player][report]") {
    const auto mode = decode_as<PlayModeReport>("a102000C0004");
    const auto volume = decode_as<VolumeReport>("a714000C003F");
    const auto source = decode_as<SourceReport>("a607000C000A");
    const auto battery = decode_as<BatteryReport>("aa1d000C0057");

    REQUIRE(mode.has_value());
    REQUIRE(volume.has_value());
    REQUIRE(source.has_value());
    REQUIRE(battery.has_value());
    CHECK(mode->mode == PlayMode::Single);
    CHECK(volume->level.value() == 63);
    CHECK(source->source == Source::AirPlay);
    CHECK(battery->percent == 87);
}

TEST_CASE("decode_report reads the on/off reports", "[player][report]") {
    const auto charging = decode_as<ChargingReport>("aa1f000C0001");
    const auto screen = decode_as<ScreenReport>("aa23000C0000");
    const auto cover = decode_as<CoverArtReport>("aa05000C0001");

    REQUIRE(charging.has_value());
    REQUIRE(screen.has_value());
    REQUIRE(cover.has_value());
    CHECK(charging->charging);
    CHECK_FALSE(screen->on);
    CHECK(cover->ready);
}

TEST_CASE("decode_report reads acknowledgements and results", "[player][report]") {
    const auto favourite = decode_as<FavouriteReport>("aa01000C0001");
    const auto conversion = decode_as<ConversionReport>("a810000C0000");

    REQUIRE(favourite.has_value());
    REQUIRE(conversion.has_value());
    CHECK(favourite->value == 1);
    CHECK(conversion->code == 0);
}

TEST_CASE("decode_report ignores the power-off body", "[player][report]") {
    // Never captured, so the body is unknown: any well-formed frame counts.
    CHECK(decode_as<PowerOffReport>("aa030008").has_value());
    CHECK(decode_as<PowerOffReport>("aa03000C0001").has_value());
}

TEST_CASE("decode_report passes other reports through as UnknownReport", "[player][report]") {
    const auto unknown = decode_as<UnknownReport>("a60a000C0213");

    REQUIRE(unknown.has_value());
    CHECK(unknown->tag == "a60a");
    CHECK(unknown->body == "0213");
}

TEST_CASE("decode_report rejects malformed frames and bodies", "[player][report]") {
    using enum ReportError;
    // A wrong length field: the frame itself is broken.
    CHECK(decode_report("a103000F000003E8") == std::unexpected(BadFrame));
    // A well-formed frame whose body has the wrong size or is not hex.
    CHECK(decode_report("a103000C03E8") == std::unexpected(BadBody));
    CHECK(decode_report("a714000E000023") == std::unexpected(BadBody));
    CHECK(decode_report("aa1f000C000x") == std::unexpected(BadBody));
}

TEST_CASE("decode_report rejects values outside the report's range", "[player][report]") {
    using enum ReportError;
    CHECK(decode_report("a102000C0005") == std::unexpected(BadValue));
    CHECK(decode_report("a714000C0079") == std::unexpected(BadValue));
    CHECK(decode_report("a607000C0003") == std::unexpected(BadValue));
    CHECK(decode_report("aa1f000C0002") == std::unexpected(BadValue));
    CHECK(decode_report("aa1d000C0065") == std::unexpected(BadValue));
}

TEST_CASE("decode_report reads exactly the state-only a202 form", "[player][report]") {
    using enum ReportError;
    const auto playing = decode_as<PlayStateReport>(R"(a2020013{"state":0})");
    const auto paused = decode_as<PlayStateReport>(R"(a2020013{"state":1})");
    const auto stopped = decode_as<PlayStateReport>(R"(a2020013{"state":2})");

    REQUIRE(playing.has_value());
    REQUIRE(paused.has_value());
    REQUIRE(stopped.has_value());
    CHECK(playing->state == ReportedPlayState::Playing);
    CHECK(paused->state == ReportedPlayState::Paused);
    CHECK(stopped->state == ReportedPlayState::Stopped);
    CHECK(decode_report(R"(a2020013{"state":3})") == std::unexpected(BadValue));
    CHECK(decode_report(R"(a2020014{"state":01})") == std::unexpected(BadBody));
    CHECK(decode_report(R"(a2020014{"state": 1})") == std::unexpected(BadBody));
    CHECK(decode_report(R"(a2020014{"state":1,})") == std::unexpected(BadBody));
}

// As captured on 2026-11-07, with the address replaced.
TEST_CASE("decode_report reads the connected Bluetooth sinks", "[player][report]") {
    const auto speaker = utils::BluetoothAddress::from("A4:C1:38:0F:9E:2B");
    const auto headphones = utils::BluetoothAddress::from("00:1B:66:C2:7D:01");
    const auto none = decode_as<BluetoothSinksReport>("a6c400100000[{}]");
    const auto one = decode_as<BluetoothSinksReport>(
            R"(a6c400460001[{"id":1,"addr":"a4:c1:38:0f:9e:2b","name":"","status":0}])"
    );
    const auto two = decode_as<BluetoothSinksReport>(
            R"(a6c400860002[{"id":1,"addr":"A4:C1:38:0F:9E:2B","name":"Kitchen","status":0},)"
            R"({"id":2,"addr":"00:1B:66:C2:7D:01","name":"","status":1}])"
    );

    REQUIRE(speaker.has_value());
    REQUIRE(headphones.has_value());
    REQUIRE(none.has_value());
    REQUIRE(one.has_value());
    REQUIRE(two.has_value());
    CHECK(none->sinks.empty());
    CHECK(one->sinks == std::vector{*speaker});
    CHECK(two->sinks == std::vector{*speaker, *headphones});
}

TEST_CASE("decode_report rejects malformed Bluetooth sink lists", "[player][report]") {
    const auto error = [](std::string_view frame) {
        return decode_report(frame).error();
    };

    CHECK(error("a6c4000C0000") == ReportError::BadBody);
    CHECK(error("a6c400100001[{}]") == ReportError::BadBody);
    CHECK(error("a6c4000E0000[]") == ReportError::BadBody);
    CHECK(error(R"(a6c400460000[{"id":1,"addr":"A4:C1:38:0F:9E:2B","name":"","status":0}])")
          == ReportError::BadBody);
    CHECK(error(R"(a6c400470001[{"id":1,"addr":"A4:C1:38:0F:9E:2B","name":"","status":0},])")
          == ReportError::BadBody);
    CHECK(error(R"(a6c400460001[{"id":x,"addr":"A4:C1:38:0F:9E:2B","name":"","status":0}])")
          == ReportError::BadBody);
    CHECK(error(R"(a6c400460001[{"id":1,"addr":"A4-C1-38-0F-9E-2B","name":"","status":0}])")
          == ReportError::BadValue);
}

}  // namespace
}  // namespace diskos::player
