// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/frame.hpp"

#include <algorithm>
#include <charconv>
#include <memory>
#include <system_error>

namespace diskos::player {

std::expected<Frame, FrameError> parse_frame(std::string_view bytes) {
    if (bytes.size() < kHeaderSize) {
        return std::unexpected(FrameError::TooShort);
    }
    const std::string_view tag = bytes.substr(0, kTagSize);
    if (!parse_hex(tag)) {
        return std::unexpected(FrameError::BadTag);
    }
    const std::optional<std::uint32_t> length = parse_hex(bytes.substr(kTagSize, kLengthSize));
    if (!length) {
        return std::unexpected(FrameError::BadLength);
    }
    if (*length != bytes.size()) {
        return std::unexpected(FrameError::LengthMismatch);
    }
    return Frame{.tag = tag, .body = bytes.substr(kHeaderSize)};
}

std::expected<std::string, FrameError> encode_frame(std::string_view tag, std::string_view body) {
    if (tag.size() != kTagSize || !parse_hex(tag)) {
        return std::unexpected(FrameError::BadTag);
    }
    const std::size_t size = kHeaderSize + body.size();
    if (size > kMaxFrameSize) {
        return std::unexpected(FrameError::TooLong);
    }
    std::string frame{tag};
    frame += format_hex(size, kLengthSize);
    frame += body;
    return frame;
}

std::optional<std::uint32_t> parse_hex(std::string_view digits) {
    constexpr std::size_t kMaxDigits = 8;
    constexpr int kBase = 16;
    if (digits.empty() || digits.size() > kMaxDigits) {
        return std::nullopt;
    }
    const char* const begin = std::to_address(digits.cbegin());
    const char* const end = std::to_address(digits.cend());
    std::uint32_t value = 0;
    const auto [stop, error] = std::from_chars(begin, end, value, kBase);
    if (error != std::errc{} || stop != end) {
        return std::nullopt;
    }
    return value;
}

std::string format_hex(std::uint64_t value, std::size_t width) {
    constexpr std::string_view kDigits = "0123456789ABCDEF";
    constexpr std::uint64_t kBase = 16;
    std::string text;
    for (; value != 0; value /= kBase) {
        const auto digit = static_cast<unsigned>(value % kBase);
        text.push_back(kDigits[digit]);
    }
    text.resize(std::max({text.size(), width, std::size_t{1}}), '0');
    std::ranges::reverse(text);
    return text;
}

}  // namespace diskos::player
