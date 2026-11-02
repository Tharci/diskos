// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "player/frame.hpp"
#include "player/modes.hpp"

/**
 * What the stock player reports on the /ui queue, decoded from its frames.
 *
 * Each report is a struct with its tag (kTag) and a decode() of its body; decode_report() picks
 * the struct by the frame's tag. A fixed-layout report has a 4-hex-digit value as its whole body
 * (position: 8 digits). a202 carries the play state or the current track. The Bluetooth reports
 * (a6xx) are not decoded yet and come back as UnknownReport.
 */
namespace diskos::player {

inline constexpr std::uint8_t kMaxBattery = 100;

enum class ReportError : std::uint8_t {
    BadFrame,  // Not a frame: see parse_frame().
    BadBody,   // The body does not have the report's layout.
    BadValue,  // The value is outside what the report allows.
};

template <typename T>
using Decoded = std::expected<T, ReportError>;

/** What every report provides: its tag, checked at compile time, and a decoder for its body. */
template <typename T>
concept IsReport = is_tag(T::kTag) && requires(std::string_view body) {
    { T::decode(body) } -> std::same_as<Decoded<T>>;
};

/** a103: the playing position, about once a second while playing, never while paused. */
struct PositionReport {
    static constexpr std::string_view kTag = "a103";
    std::uint32_t position_ms;
    [[nodiscard]] static Decoded<PositionReport> decode(std::string_view body);
};

/** a102: the play mode the player switched to, after a 0102 command. */
struct PlayModeReport {
    static constexpr std::string_view kTag = "a102";
    PlayMode mode;
    [[nodiscard]] static Decoded<PlayModeReport> decode(std::string_view body);
};

/**
 * a714: the volume, after every 0715 command.
 * Over Bluetooth it arrives late, and in batches.
 */
struct VolumeReport {
    static constexpr std::string_view kTag = "a714";
    VolumeLevel level;
    [[nodiscard]] static Decoded<VolumeReport> decode(std::string_view body);
};

/** a607: the source the player switched to, after a 0657 command. */
struct SourceReport {
    static constexpr std::string_view kTag = "a607";
    Source source;
    [[nodiscard]] static Decoded<SourceReport> decode(std::string_view body);
};

/** aa1f: the charger was plugged in or out. */
struct ChargingReport {
    static constexpr std::string_view kTag = "aa1f";
    bool charging;
    [[nodiscard]] static Decoded<ChargingReport> decode(std::string_view body);
};

/** aa23: the screen went off or on. */
struct ScreenReport {
    static constexpr std::string_view kTag = "aa23";
    bool on;
    [[nodiscard]] static Decoded<ScreenReport> decode(std::string_view body);
};

/** aa03: the player is powering off. Its body is unknown (never captured) and ignored. */
struct PowerOffReport {
    static constexpr std::string_view kTag = "aa03";
    [[nodiscard]] static Decoded<PowerOffReport> decode(std::string_view body);
};

/** aa05: the cover art of the current track was converted (true) or not (false). */
struct CoverArtReport {
    static constexpr std::string_view kTag = "aa05";
    bool ready;
    [[nodiscard]] static Decoded<CoverArtReport> decode(std::string_view body);
};

/**
 * aa01: the answer to a 0104 favourite set/unset.
 * What its value means is not known yet.
 * TODO: Test it.
 */
struct FavouriteReport {
    static constexpr std::string_view kTag = "aa01";
    std::uint16_t value;
    [[nodiscard]] static Decoded<FavouriteReport> decode(std::string_view body);
};

/** aa1d: the battery level. */
struct BatteryReport {
    static constexpr std::string_view kTag = "aa1d";
    std::uint8_t percent;  // 0..kMaxBattery
    [[nodiscard]] static Decoded<BatteryReport> decode(std::string_view body);
};

/** a810: the result of a 0810 image conversion. */
struct ConversionReport {
    static constexpr std::string_view kTag = "a810";
    std::uint16_t code;  // 0 = done; the player cuts longer codes to 4 digits, so -1 is 0xFFFF
    [[nodiscard]] static Decoded<ConversionReport> decode(std::string_view body);
};

/** a202, state only: exactly {"state":N}, after every command. */
struct PlayStateReport {
    static constexpr std::string_view kTag = "a202";
    PlayState state;
    [[nodiscard]] static Decoded<PlayStateReport> decode(std::string_view body);
};

/**
 * The current track, as the player describes it.
 * Unknown numbers are 0, missing texts empty.
 */
struct Song {
    std::int64_t id;      // SONG.ID in the player's library; 0 under AirPlay
    std::int64_t pos_id;  // LIST_SONG_0.ID: the queue row; 0 under AirPlay
    std::string title;    // The file name when the file has no tags or cannot be read
    std::string artist;
    std::string album;
    std::string genre;
    std::string path;  // Empty under AirPlay
    std::int64_t duration_ms;
    std::int64_t sample_rate_hz;
    std::int64_t bits_per_sample;
    std::int64_t bit_rate_kbps;
    std::int64_t track_number;
    std::int64_t channels;
    bool is_sacd;
    bool is_cue;
    bool is_dsd;
    bool is_m3u;
    std::string m3u_path;  // Empty when not from a playlist
};

/** Where the current track is in the player's queue: "3/12" is position 3 of 12. */
struct QueuePosition {
    std::int64_t position;
    std::int64_t count;
};

/**
 * a202, full form: the current track, after a track change or a 0202 request.
 *
 * It looks like JSON but is not: the player fills a fixed template with snprintf, so a title
 * holding `"` or `\` arrives unescaped. It is read by that template (stock V2.57 only), strictly:
 * every key in order, integers and booleans exactly, and each text value up to the one place
 * where the next key follows (track_report.cpp).
 */
struct TrackReport {
    static constexpr std::string_view kTag = "a202";
    Song song;
    bool favourite;
    std::int64_t player_flag;  // Meaning unknown: 1, 2, 3, 5 and 6 locally, 255 under AirPlay
    PlayState state;           // Can lag: a track that is about to play still reads Stopped
    std::optional<QueuePosition> queue_position;  // None under AirPlay
    std::string work_mode;                        // "LOCAL" or "AIRPLAY" so far

    /** Check whether an a202 body is in this full form (else it is a PlayStateReport). */
    [[nodiscard]] static bool claims(std::string_view body);

    /**
     * Decode an a202 body in its full form.
     * Returns:
     *  - BadBody, if it does not follow the V2.57 template or a text value's end is ambiguous
     *  - BadValue, for an unknown state, a malformed queue position or an out-of-range number
     */
    [[nodiscard]] static Decoded<TrackReport> decode(std::string_view body);
};

/**
 * Any other report, with views into the decoded bytes.
 * Includes aa1c, a824 and a60a, which appear around every track
 *  and source change but whose meaning is not known.
 */
struct UnknownReport {
    std::string_view tag;
    std::string_view body;
};

/** Any report. Every one but UnknownReport is an IsReport. */
using Report = std::variant<
        UnknownReport,
        PositionReport,
        PlayModeReport,
        VolumeReport,
        SourceReport,
        ChargingReport,
        ScreenReport,
        PowerOffReport,
        CoverArtReport,
        FavouriteReport,
        BatteryReport,
        ConversionReport,
        PlayStateReport,
        TrackReport>;

/**
 * Decode one frame received on /ui.
 * Returns:
 *  - the report, for a well-formed frame (UnknownReport for a tag without a decoder)
 *  - ReportError for anything malformed
 */
[[nodiscard]] Decoded<Report> decode_report(std::string_view bytes);

}  // namespace diskos::player
