// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

// The framing of the stock player's message queues.
//
// A frame is ASCII text: a 4-hex-digit tag, the total frame length as 4 hex digits (the header
// included), then the body. "0201000C0000" is tag 0201 with body "0000". Most bodies start with
// a 4-digit hex value (docs/COMMAND_MAP.md lists them). One family breaks the layout: 0810
// requests and a810 replies put a value before the length, so they do not go through here.
namespace diskos::player {

inline constexpr std::size_t kTagSize = 4;
inline constexpr std::size_t kLengthSize = 4;
inline constexpr std::size_t kHeaderSize = kTagSize + kLengthSize;
inline constexpr std::size_t kMaxFrameSize = 0xFFFF;

struct Frame {
    std::string_view tag;
    std::string_view body;
};

enum class FrameError : std::uint8_t {
    TooShort,        // Fewer bytes than a header.
    BadTag,          // The tag is not 4 hex digits.
    BadLength,       // The length field is not 4 hex digits.
    LengthMismatch,  // The length field disagrees with the number of bytes.
    TooLong,         // The frame would not fit the length field.
};

// Splits a received frame into tag and body, checking the length field against its size.
std::expected<Frame, FrameError> parse_frame(std::string_view bytes);

// Builds a frame from a tag and a body, filling in the length field.
std::expected<std::string, FrameError> encode_frame(std::string_view tag, std::string_view body);

// Reads 1 to 8 hex digits (case insensitive).
// Returns:
//  uint32_t - for hex digits
//  nullopt - for anything else
std::optional<std::uint32_t> parse_hex(std::string_view digits);

// Writes `value` in uppercase hex, zero-padded to at least `width` digits, like printf's %0*X:
// a value too wide for `width` keeps all its digits.
std::string format_hex(std::uint64_t value, std::size_t width);

}  // namespace diskos::player
