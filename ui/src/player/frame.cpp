// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/frame.hpp"

#include "utils/numbers.hpp"

namespace diskos::player {

std::expected<Frame, FrameError> parse_frame(std::string_view bytes) {
    if (bytes.size() < kHeaderSize) {
        return std::unexpected(FrameError::TooShort);
    }
    const std::string_view tag = bytes.substr(0, kTagSize);
    if (!utils::parse_hex(tag)) {
        return std::unexpected(FrameError::BadTag);
    }
    const std::optional<std::uint32_t> length =
            utils::parse_hex(bytes.substr(kTagSize, kLengthSize));
    if (!length) {
        return std::unexpected(FrameError::BadLength);
    }
    if (*length != bytes.size()) {
        return std::unexpected(FrameError::LengthMismatch);
    }
    return Frame{.tag = tag, .body = bytes.substr(kHeaderSize)};
}

std::expected<std::string, FrameError> encode_frame(std::string_view tag, std::string_view body) {
    if (tag.size() != kTagSize || !utils::parse_hex(tag)) {
        return std::unexpected(FrameError::BadTag);
    }
    const std::size_t size = kHeaderSize + body.size();
    if (size > kMaxFrameSize) {
        return std::unexpected(FrameError::TooLong);
    }
    std::string frame{tag};
    frame += utils::format_hex(size, kLengthSize);
    frame += body;
    return frame;
}

}  // namespace diskos::player
