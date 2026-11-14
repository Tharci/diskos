// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "player/frame.hpp"
#include "player/modes.hpp"

/**
 * What diskOS asks the stock player to do on the /player queue, as typed values, and their
 * frames.
 *
 * Each command is a struct with its tag (kTag) and its body(); CommandBase turns the two into
 * the frame. Most bodies are one 4-hex-digit value. The meanings and frames come from
 * docs/COMMAND_MAP.md, the frame capture of 2026-11-01 and the legacy code that sends them.
 */
namespace diskos::player {

/** Where the player sends its audio: the value of 0666. */
enum class OutputRoute : std::uint8_t {
    BluetoothSource = 2,
    UsbHostAudio = 3,
    Spdif = 4,
    InternalDac = 6,  // Every source change starts from here.
};

/** What the USB port offers the computer it is plugged into: the value of 0642. */
enum class UsbGadget : std::uint8_t {
    None = 0,
    Storage = 1,
    UsbDac = 2,
    UsbHostAudio = 5,
};

/** Which songs a play-list command queues up, and how its name selects them. */
enum class ListType : std::uint8_t {
    CurrentQueue = 0,  // No name: jump within the queue already playing.
    All = 1,           // No name.
    Artist = 2,        // The artist (or album artist, per the player's setting).
    Album = 3,         // The album.
    Folder = 4,        // Stock only: a file's folder.
    ReservedSlot = 5,  // No name: the playlist slot diskOS writes into the player's database.
    Favourites = 6,    // No name.
    ArtistAlbum = 7,   // {"artist":"..", "album":".."}
    GenreAlbum = 8,    // {"style":"..", "album":".."}
    Genre = 10,        // The genre.
};

// The longest play-list name the legacy sender allows (its frame buffer).
inline constexpr std::size_t kMaxListNameSize = 379;

/** A play-list name short enough to send (see ListType for what it holds); empty by default. */
class ListName {
public:
    ListName() = default;

    /** Get the name, or nullopt if it is longer than kMaxListNameSize bytes. */
    [[nodiscard]] static std::optional<ListName> from(std::string name) {
        if (name.size() > kMaxListNameSize) {
            return std::nullopt;
        }
        return ListName{std::move(name)};
    }

    [[nodiscard]] const std::string& value() const noexcept {
        return name_;
    }

private:
    explicit ListName(std::string name) : name_(std::move(name)) {}

    std::string name_;
};

/** What every command provides: its tag (checked at compile time), and its body. */
template <typename T>
concept IsCommand = is_tag(T::kTag) && requires(const T& command) {
    { command.body() } -> std::convertible_to<std::string>;
};

/** The parent of every command: builds its frame from the command's kTag and body(). */
struct CommandBase {
    /** Get the command's frame, ready to send on /player. */
    [[nodiscard]] std::string frame(this const IsCommand auto& self) {
        using Self = std::remove_cvref_t<decltype(self)>;
        return build_frame(Self::kTag, self.body());
    }
};

/** 0201 0000: pause if playing, play if paused. */
struct PlayPauseCommand : CommandBase {
    static constexpr std::string_view kTag = "0201";
    [[nodiscard]] static std::string body();
};

/** 0201 0001: the next track in the queue. */
struct NextTrackCommand : CommandBase {
    static constexpr std::string_view kTag = "0201";
    [[nodiscard]] static std::string body();
};

/** 0201 0002: the previous track in the queue. */
struct PreviousTrackCommand : CommandBase {
    static constexpr std::string_view kTag = "0201";
    [[nodiscard]] static std::string body();
};

/** 0202: report the full state (an a202 track report). */
struct RequestStateCommand : CommandBase {
    static constexpr std::string_view kTag = "0202";
    [[nodiscard]] static std::string body();
};

/** 0103: jump to a position in the current track. */
struct SeekCommand : CommandBase {
    static constexpr std::string_view kTag = "0103";
    std::uint32_t position_ms;
    [[nodiscard]] std::string body() const;
};

/** 0715: set the volume. */
struct SetVolumeCommand : CommandBase {
    static constexpr std::string_view kTag = "0715";
    VolumeLevel level;
    [[nodiscard]] std::string body() const;
};

/** 0102: set how the player moves through its queue. */
struct SetPlayModeCommand : CommandBase {
    static constexpr std::string_view kTag = "0102";
    PlayMode mode;
    [[nodiscard]] std::string body() const;
};

/** 0104: set/unset the current track as favourite. */
struct SetFavouriteCommand : CommandBase {
    static constexpr std::string_view kTag = "0104";
    bool favourite;
    [[nodiscard]] std::string body() const;
};

/** 0100: build a queue and start playing it at `position`. */
// type and position have no default, so GCC's -Wmissing-field-initializers catches a designated
// initializer that leaves one out.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init): see above
struct PlayListCommand : CommandBase {
    static constexpr std::string_view kTag = "0100";
    ListType type;
    std::uint16_t position;  // 0-based, in the player's order for the list
    ListName name{};
    [[nodiscard]] std::string body() const;
};

/** 0657: switch the player's source. */
struct SwitchSourceCommand : CommandBase {
    static constexpr std::string_view kTag = "0657";
    Source source;
    [[nodiscard]] std::string body() const;
};

/** 0666: route the audio output. */
struct RouteOutputCommand : CommandBase {
    static constexpr std::string_view kTag = "0666";
    OutputRoute route;
    [[nodiscard]] std::string body() const;
};

/** 0642: set what the USB port offers. */
struct SelectUsbGadgetCommand : CommandBase {
    static constexpr std::string_view kTag = "0642";
    UsbGadget gadget;
    [[nodiscard]] std::string body() const;
};

/**
 * 0818 0000: sent before switching the source to the Bluetooth sink, as the stock sequence does.
 * What the player does with it is not known.
 */
struct PrepareBluetoothSinkCommand : CommandBase {
    static constexpr std::string_view kTag = "0818";
    [[nodiscard]] static std::string body();
};

/**
 * 06c1 0000: turn the player's Bluetooth on, which starts its initialisation thread. The route to a
 * Bluetooth sink sends it before 0666 2, as the stock sequence does, and the codec select (06b3)
 * does nothing before it. 06c0 turns Bluetooth off.
 */
struct EnableBluetoothCommand : CommandBase {
    static constexpr std::string_view kTag = "06c1";
    [[nodiscard]] static std::string body();
};

using Command = std::variant<
        PlayPauseCommand,
        NextTrackCommand,
        PreviousTrackCommand,
        RequestStateCommand,
        SeekCommand,
        SetVolumeCommand,
        SetPlayModeCommand,
        SetFavouriteCommand,
        PlayListCommand,
        SwitchSourceCommand,
        RouteOutputCommand,
        SelectUsbGadgetCommand,
        PrepareBluetoothSinkCommand,
        EnableBluetoothCommand>;

/** Get the frame of whichever command `command` holds. */
[[nodiscard]] std::string frame(const Command& command);

}  // namespace diskos::player
