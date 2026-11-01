// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/report.hpp"

#include <expected>
#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

// The report decoded from `frame` if it is a T, else nullptr.
template <typename T>
const T* decode_as(std::string_view frame, std::expected<Report, ReportError>& storage) {
    storage = decode_report(frame);
    return storage ? std::get_if<T>(&*storage) : nullptr;
}

// Every frame below was received from the player on the Disc (capture of 2026-11-01).

TEST_CASE("decode_report reads positions", "[player][report]") {
    std::expected<Report, ReportError> report;
    const auto* position = decode_as<Position>("a103001000C9174D", report);
    REQUIRE(position != nullptr);
    CHECK(position->position_ms == 0x00C9174D);
}

TEST_CASE("decode_report reads play modes, volume, source and battery", "[player][report]") {
    std::expected<Report, ReportError> report;
    const auto* mode = decode_as<PlayModeReport>("a102000C0004", report);
    REQUIRE(mode != nullptr);
    CHECK(mode->mode == PlayMode::Single);

    const auto* volume = decode_as<Volume>("a714000C003F", report);
    REQUIRE(volume != nullptr);
    CHECK(volume->level == 63);

    const auto* source = decode_as<SourceReport>("a607000C000A", report);
    REQUIRE(source != nullptr);
    CHECK(source->source == Source::AirPlay);

    const auto* battery = decode_as<Battery>("aa1d000C0057", report);
    REQUIRE(battery != nullptr);
    CHECK(battery->percent == 87);
}

TEST_CASE("decode_report reads the on/off reports", "[player][report]") {
    std::expected<Report, ReportError> report;
    const auto* charging = decode_as<Charging>("aa1f000C0001", report);
    REQUIRE(charging != nullptr);
    CHECK(charging->charging);

    const auto* screen = decode_as<Screen>("aa23000C0000", report);
    REQUIRE(screen != nullptr);
    CHECK_FALSE(screen->on);

    const auto* cover = decode_as<CoverArt>("aa05000C0001", report);
    REQUIRE(cover != nullptr);
    CHECK(cover->ready);
}

TEST_CASE("decode_report reads acknowledgements and results", "[player][report]") {
    std::expected<Report, ReportError> report;
    const auto* favourite = decode_as<FavouriteReport>("aa01000C0001", report);
    REQUIRE(favourite != nullptr);
    CHECK(favourite->value == 1);

    const auto* conversion = decode_as<ConversionResult>("a810000C0000", report);
    REQUIRE(conversion != nullptr);
    CHECK(conversion->code == 0);
}

TEST_CASE("decode_report ignores the power-off body", "[player][report]") {
    // Never captured, so the body is unknown: any well-formed frame counts.
    std::expected<Report, ReportError> report;
    CHECK(decode_as<PowerOff>("aa030008", report) != nullptr);
    CHECK(decode_as<PowerOff>("aa03000C0001", report) != nullptr);
}

TEST_CASE("decode_report passes other reports through as Unknown", "[player][report]") {
    std::expected<Report, ReportError> report;
    const auto* unknown = decode_as<Unknown>("a60a000C0213", report);
    REQUIRE(unknown != nullptr);
    CHECK(unknown->tag == "a60a");
    CHECK(unknown->body == "0213");
}

TEST_CASE("decode_report rejects malformed frames and bodies", "[player][report]") {
    // A wrong length field: the frame itself is broken.
    CHECK(decode_report("a103000F000003E8") == std::unexpected(ReportError::BadFrame));
    // A well-formed frame whose body has the wrong size or is not hex.
    CHECK(decode_report("a103000C03E8") == std::unexpected(ReportError::BadBody));
    CHECK(decode_report("a714000E000023") == std::unexpected(ReportError::BadBody));
    CHECK(decode_report("aa1f000C000x") == std::unexpected(ReportError::BadBody));
}

TEST_CASE("decode_report rejects values outside the report's range", "[player][report]") {
    CHECK(decode_report("a102000C0005") == std::unexpected(ReportError::BadValue));
    CHECK(decode_report("a714000C0079") == std::unexpected(ReportError::BadValue));
    CHECK(decode_report("a607000C0003") == std::unexpected(ReportError::BadValue));
    CHECK(decode_report("aa1f000C0002") == std::unexpected(ReportError::BadValue));
    CHECK(decode_report("aa1d000C0065") == std::unexpected(ReportError::BadValue));
}

}  // namespace
}  // namespace diskos::player
