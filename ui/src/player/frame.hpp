// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

/**
 * The framing of the stock player's message queues.
 *
 * A frame is ASCII text: a 4-hex-digit tag, the total frame length as 4 hex digits (the header
 * included), then the body. "0201000C0000" is tag 0201 with body "0000". Most bodies start with
 * a 4-digit hex value (docs/COMMAND_MAP.md lists them). Every frame in both directions has this
 * layout, with the length counted in bytes (Disc capture, 2026-11-01).
 */
namespace diskos::player {

inline constexpr std::size_t kTagSize = 4;
inline constexpr std::size_t kLengthSize = 4;
inline constexpr std::size_t kHeaderSize = kTagSize + kLengthSize;
inline constexpr std::size_t kMaxFrameSize = 0xFFFF;
inline constexpr std::size_t kMaxBodySize = kMaxFrameSize - kHeaderSize;

[[nodiscard]] constexpr bool is_hex_digit(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/** Check that `tag` is a frame tag: exactly 4 hex digits. Usable at compile time. */
[[nodiscard]] constexpr bool is_tag(std::string_view tag) noexcept {
    return tag.size() == kTagSize && std::ranges::all_of(tag, is_hex_digit);
}

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

/**
 * Split a received frame into tag and body, checking the length field against its size.
 * The views point into `bytes`. Returns the reason as a FrameError when the frame is malformed.
 */
[[nodiscard]] std::expected<Frame, FrameError> parse_frame(std::string_view bytes);

/**
 * Build a frame from a tag and a body, filling in the length field.
 * Returns BadTag or TooLong when the tag or the body cannot be framed.
 */
[[nodiscard]] std::expected<std::string, FrameError> encode_frame(
        std::string_view tag, std::string_view body
);

/**
 * Build a frame from a tag and a body that are known to fit: `is_tag(tag)` and at most
 * kMaxBodySize bytes of body. For callers whose types guarantee both, such as the commands.
 */
[[nodiscard]] std::string build_frame(std::string_view tag, std::string_view body);

}  // namespace diskos::player
