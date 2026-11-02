// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/commands.hpp"

#include "utils/numbers.hpp"

namespace diskos::player {
namespace {

constexpr std::size_t kValueDigits = 4;
constexpr std::size_t kPositionDigits = 8;

/** The usual body: one 4-digit value. */
std::string value_body(std::uint32_t value) {
    return utils::format_hex(value, kValueDigits);
}

template <typename... Commands>
constexpr bool all_commands(std::type_identity<std::variant<Commands...>> /*variant*/) {
    return (IsCommand<Commands> && ...);
}

static_assert(all_commands(std::type_identity<Command>{}));
// The longest body, a play list's, fits a frame: so does every command.
static_assert(2 * kValueDigits + kMaxListNameSize <= kMaxBodySize);

}  // namespace

std::string PlayPauseCommand::body() {
    return value_body(0);
}

std::string NextTrackCommand::body() {
    return value_body(1);
}

std::string PreviousTrackCommand::body() {
    return value_body(2);
}

std::string RequestStateCommand::body() {
    return {};
}

std::string SeekCommand::body() const {
    return utils::format_hex(position_ms, kPositionDigits);
}

std::string SetVolumeCommand::body() const {
    return value_body(level.value());
}

std::string SetPlayModeCommand::body() const {
    return value_body(static_cast<std::uint32_t>(mode));
}

std::string SetFavouriteCommand::body() const {
    return value_body(favourite ? 1 : 0);
}

std::string PlayListCommand::body() const {
    return value_body(position) + value_body(static_cast<std::uint32_t>(type)) + name.value();
}

std::string SwitchSourceCommand::body() const {
    return value_body(static_cast<std::uint32_t>(source));
}

std::string RouteOutputCommand::body() const {
    return value_body(static_cast<std::uint32_t>(route));
}

std::string SelectUsbGadgetCommand::body() const {
    return value_body(static_cast<std::uint32_t>(gadget));
}

std::string PrepareBluetoothSinkCommand::body() {
    return value_body(0);
}

std::string frame(const Command& command) {
    return std::visit([](const auto& typed) { return typed.frame(); }, command);
}

}  // namespace diskos::player
