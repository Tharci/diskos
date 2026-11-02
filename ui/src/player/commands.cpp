// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/commands.hpp"

#include <string_view>

#include "player/frame.hpp"
#include "utils/numbers.hpp"

namespace diskos::player {
namespace {

using Encoded = std::expected<std::string, CommandError>;

constexpr std::size_t kValueDigits = 4;
constexpr std::size_t kPositionDigits = 8;

/** A frame with `body`; the tags are fixed, so only the length can fail. */
Encoded framed(std::string_view tag, std::string_view body) {
    return encode_frame(tag, body).transform_error([](FrameError /*error*/) {
        return CommandError::TooLong;
    });
}

/** The usual body: one 4-digit value. */
Encoded with_value(std::string_view tag, std::uint32_t value) {
    return framed(tag, utils::format_hex(value, kValueDigits));
}

Encoded encode(const PlayPauseCommand& /*command*/) {
    return with_value("0201", 0);
}

Encoded encode(const NextTrackCommand& /*command*/) {
    return with_value("0201", 1);
}

Encoded encode(const PreviousTrackCommand& /*command*/) {
    return with_value("0201", 2);
}

Encoded encode(const RequestStateCommand& /*command*/) {
    return framed("0202", "");
}

Encoded encode(const SeekCommand& command) {
    return framed("0103", utils::format_hex(command.position_ms, kPositionDigits));
}

Encoded encode(const SetVolumeCommand& command) {
    if (command.level > kMaxVolume) {
        return std::unexpected(CommandError::BadValue);
    }
    return with_value("0715", command.level);
}

Encoded encode(const SetPlayModeCommand& command) {
    return with_value("0102", static_cast<std::uint32_t>(command.mode));
}

Encoded encode(const SetFavouriteCommand& command) {
    return with_value("0104", command.favourite ? 1 : 0);
}

Encoded encode(const PlayListCommand& command) {
    if (command.name.size() > kMaxListNameSize) {
        return std::unexpected(CommandError::TooLong);
    }
    std::string body = utils::format_hex(command.position, kValueDigits);
    body += utils::format_hex(static_cast<std::uint32_t>(command.type), kValueDigits);
    body += command.name;
    return framed("0100", body);
}

Encoded encode(const SwitchSourceCommand& command) {
    return with_value("0657", static_cast<std::uint32_t>(command.source));
}

Encoded encode(const RouteOutputCommand& command) {
    return with_value("0666", static_cast<std::uint32_t>(command.route));
}

Encoded encode(const SelectUsbGadgetCommand& command) {
    return with_value("0642", static_cast<std::uint32_t>(command.gadget));
}

Encoded encode(const PrepareBluetoothSinkCommand& /*command*/) {
    return with_value("0818", 0);
}

}  // namespace

std::expected<std::string, CommandError> encode_command(const Command& command) {
    return std::visit([](const auto& typed) { return encode(typed); }, command);
}

}  // namespace diskos::player
