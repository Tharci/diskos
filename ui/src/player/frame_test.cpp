// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/frame.hpp"

#include <string>

#include "utils/numbers.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::player {
namespace {

TEST_CASE("encode_frame fills in the total length", "[player][frame]") {
    // Play/pause, the state request (no body) and a seek to 1 s, as docs/COMMAND_MAP.md has them.
    CHECK(encode_frame("0201", "0000") == "0201000C0000");
    CHECK(encode_frame("0202", "") == "02020008");
    CHECK(encode_frame("0103", utils::format_hex(1000, 8)) == "01030010000003E8");
}

TEST_CASE("encode_frame refuses a tag that is not 4 hex digits", "[player][frame]") {
    CHECK(encode_frame("201", "") == std::unexpected(FrameError::BadTag));
    CHECK(encode_frame("02g1", "") == std::unexpected(FrameError::BadTag));
}

TEST_CASE("encode_frame refuses a body the length field cannot count", "[player][frame]") {
    const std::string longest(kMaxFrameSize - kHeaderSize, 'x');
    CHECK(encode_frame("0810", longest).has_value());
    CHECK(encode_frame("0810", longest + "x") == std::unexpected(FrameError::TooLong));
}

TEST_CASE("parse_frame splits a report into tag and body", "[player][frame]") {
    const auto frame = parse_frame("a1030010000003E8");
    REQUIRE(frame.has_value());
    CHECK(frame->tag == "a103");
    CHECK(frame->body == "000003E8");
    CHECK(utils::parse_hex(frame->body) == 1000);
}

TEST_CASE("parse_frame accepts a header-only frame", "[player][frame]") {
    const auto frame = parse_frame("02020008");
    REQUIRE(frame.has_value());
    CHECK(frame->body.empty());
}

TEST_CASE("parse_frame rejects malformed frames", "[player][frame]") {
    CHECK(parse_frame("a103") == std::unexpected(FrameError::TooShort));
    CHECK(parse_frame("zz030010000003E8") == std::unexpected(FrameError::BadTag));
    CHECK(parse_frame("a103zz10000003E8") == std::unexpected(FrameError::BadLength));
    // Truncated, and with bytes past the advertised length.
    CHECK(parse_frame("a1030010000003") == std::unexpected(FrameError::LengthMismatch));
    CHECK(parse_frame("a1030010000003E800") == std::unexpected(FrameError::LengthMismatch));
}

TEST_CASE("parse_frame reads what encode_frame writes", "[player][frame]") {
    const auto encoded = encode_frame("0657", "0008");
    REQUIRE(encoded.has_value());
    const auto frame = parse_frame(*encoded);
    REQUIRE(frame.has_value());
    CHECK(frame->tag == "0657");
    CHECK(frame->body == "0008");
}

}  // namespace
}  // namespace diskos::player
