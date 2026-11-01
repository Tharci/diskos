// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>

namespace diskos::player {

/**
 * How the player moves through its queue: the value of the 0102 command and its a102 report.
 * The stock UI cycles through them in this order.
 */
enum class PlayMode : std::uint8_t {
    Sequential = 0,
    Shuffle = 1,
    SingleRepeat = 2,
    SequentialRepeat = 3,
    Single = 4,
};

/**
 * The player's audio source: the value of the 0657 command and its a607 report.
 */
enum class Source : std::uint8_t {
    UsbDac = 1,
    Optical = 2,
    BluetoothSink = 6,
    BluetoothStreaming = 7,
    Local = 8,
    Roon = 9,
    AirPlay = 10,
    AirPlayAndDlna = 11,
    Dlna = 12,
};

/** The player's play state, as its a202 reports give it. */
enum class PlayState : std::uint8_t {
    Playing = 0,
    Paused = 1,
    Stopped = 2,  // Also a track that could not be opened.
};

}  // namespace diskos::player
