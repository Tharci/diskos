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
    if (!is_tag(tag)) {
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
    if (!is_tag(tag)) {
        return std::unexpected(FrameError::BadTag);
    }
    if (body.size() > kMaxBodySize) {
        return std::unexpected(FrameError::TooLong);
    }
    return build_frame(tag, body);
}

std::string build_frame(std::string_view tag, std::string_view body) {
    std::string frame{tag};
    frame += utils::format_hex(kHeaderSize + body.size(), kLengthSize);
    frame += body;
    return frame;
}

}  // namespace diskos::player
