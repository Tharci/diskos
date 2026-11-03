// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstdint>
#include <optional>

namespace diskos::player {

// The device's MAX_VOL: volumes in 0715 and a714 are on a 0..kMaxVolume scale.
inline constexpr std::uint8_t kMaxVolume = 120;

/** A volume on the player's 0..kMaxVolume scale; holding one means it is in range. */
class VolumeLevel {
public:
    /** Get the volume for `level`, or nullopt above kMaxVolume. */
    [[nodiscard]] static constexpr std::optional<VolumeLevel> from(std::uint32_t level) noexcept {
        if (level > kMaxVolume) {
            return std::nullopt;
        }
        return VolumeLevel{static_cast<std::uint8_t>(level)};
    }

    [[nodiscard]] constexpr std::uint8_t value() const noexcept {
        return level_;
    }

    friend constexpr bool operator==(VolumeLevel, VolumeLevel) = default;

private:
    explicit constexpr VolumeLevel(std::uint8_t level) noexcept : level_(level) {}

    std::uint8_t level_;
};

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
enum class ReportedPlayState : std::uint8_t {
    Playing = 0,
    Paused = 1,
    Stopped = 2,  // Also a track that could not be opened.
};

}  // namespace diskos::player
