// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <variant>

#include "player/modes.hpp"

/**
 * What diskOS asks the stock player to do on the /player queue, as typed values, and their
 * frames.
 *
 * Most commands carry one 4-hex-digit value after the header. The meanings and frames come from
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

/** 0201 0000: pause if playing, play if paused. */
struct PlayPauseCommand {};

/** 0201 0001: the next track in the queue. */
struct NextTrackCommand {};

/** 0201 0002: the previous track in the queue. */
struct PreviousTrackCommand {};

/** 0202: report the full state (an a202 track report). */
struct RequestStateCommand {};

/** 0103: jump to a position in the current track. */
struct SeekCommand {
    std::uint32_t position_ms;
};

/** 0715: set the volume. */
struct SetVolumeCommand {
    std::uint8_t level;  // 0..kMaxVolume
};

/** 0102: set how the player moves through its queue. */
struct SetPlayModeCommand {
    PlayMode mode;
};

/** 0104: set/unset the current track as favourite. */
struct SetFavouriteCommand {
    bool favourite;
};

/** 0100: build a queue and start playing it at `position`. */
struct PlayListCommand {
    ListType type;
    std::uint16_t position;  // 0-based, in the player's order for the list
    std::string name;        // See ListType; at most kMaxListNameSize bytes
};

/** 0657: switch the player's source. */
struct SwitchSourceCommand {
    Source source;
};

/** 0666: route the audio output. */
struct RouteOutputCommand {
    OutputRoute route;
};

/** 0642: set what the USB port offers. */
struct SelectUsbGadgetCommand {
    UsbGadget gadget;
};

/**
 * 0818 0000: sent before switching the source to the Bluetooth sink, as the stock sequence does.
 * What the player does with it is not known.
 */
struct PrepareBluetoothSinkCommand {};

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
        PrepareBluetoothSinkCommand>;

enum class CommandError : std::uint8_t {
    BadValue,  // A value outside what the command allows.
    TooLong,   // A name too long to send.
};

/**
 * Encode a command as the frame to send on /player.
 * Returns:
 *  - the frame
 *  - BadValue for a volume above kMaxVolume
 *  - TooLong for a play-list name longer than kMaxListNameSize
 */
[[nodiscard]] std::expected<std::string, CommandError> encode_command(const Command& command);

}  // namespace diskos::player
