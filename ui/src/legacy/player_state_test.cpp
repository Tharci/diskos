// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_state.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "player/frame.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::legacy {
namespace {

// Frames in the V2.57 layout (see player/track_report_test.cpp), with the title, path and state
// filled in.

/** The path as the player writes it: every '/' escaped twice. */
std::string escaped(std::string_view path) {
    std::string out;
    for (const char c : path) {
        out += c == '/' ? std::string_view{R"(\\\/)"} : std::string_view{&c, 1};
    }
    return out;
}

std::string track_frame(std::string_view title, std::string_view path, int state) {
    // clang-format off: one template field per line.
    const std::string body =
            R"({"song":"{\"id\":1415)"
            R"(,\"pos_id\":1)"
            R"(,\"song_name\":\")" + std::string{title} + R"(\")"
            R"(,\"song_duration_time\":475000)"
            R"(,\"song_file_path\":\")" + escaped(path) + R"(\")"
            R"(,\"song_sample_rate\":44100)"
            R"(,\"song_encoding_rate\":16)"
            R"(,\"song_bit_rate\":1411)"
            R"(,\"song_artist_name\":\"Some Artist\")"
            R"(,\"song_album_name\":\"Some Album\")"
            R"(,\"song_style_name\":\"Some Genre\")"
            R"(,\"song_track\":1)"
            R"(,\"song_channel\":2)"
            R"(,\"is_sacd\":false)"
            R"(,\"is_cue\":false)"
            R"(,\"is_dsd\":false)"
            R"(,\"is_m3u\":false)"
            R"(,\"m3u_file_path\":\"(null)\"}")"
            R"(,"love":true)"
            R"(,"playerflag":5)"
            R"(,"state":)" + std::to_string(state) +
            R"(,"playing_num":"3/12")"
            R"(,"work_mode":"LOCAL"})";
    // clang-format on
    return player::build_frame("a202", body);
}

constexpr std::string_view kPath = "/tmp/sdcard/Some Album/01 - First Song.flac";

diskos_player_update apply(std::string_view frame) {
    return diskos_player_apply(frame.data(), frame.size());
}

/** Whether the bridge holds a track. */
bool has_track() {
    diskos_player_track track{};
    return diskos_player_get_track(&track) == 1;
}

TEST_CASE("diskos_player_apply reports what each frame did", "[legacy]") {
    diskos_player_reset();
    const std::string first = track_frame("First Song", kPath, 2);
    const diskos_player_update loaded = apply(first);
    const diskos_player_update again = apply(first);
    const diskos_player_update playing = apply(R"(a2020013{"state":0})");
    const diskos_player_update moved = apply("a103001000C9174D");
    const diskos_player_update battery = apply("aa1d000C0057");
    const diskos_player_update malformed = apply("a103000F000003E8");

    CHECK(loaded.accepted == 1);
    CHECK(loaded.track == 1);
    CHECK(loaded.full == 1);
    CHECK(loaded.reported_state == 2);
    CHECK(again.track == 0);
    CHECK(again.details == 0);
    CHECK(playing.accepted == 1);
    CHECK(playing.full == 0);
    CHECK(playing.reported_state == 0);
    CHECK(moved.position == 1);
    CHECK(moved.reported_state == -1);
    CHECK(battery.accepted == 0);
    CHECK(malformed.accepted == 0);
}

TEST_CASE("diskos_player_get_track gives the track's fields", "[legacy]") {
    diskos_player_reset();
    apply(track_frame("First Song / Café Two", kPath, 1));
    diskos_player_track track{};

    REQUIRE(diskos_player_get_track(&track) == 1);
    CHECK(std::string_view{track.title} == "First Song / Café Two");
    CHECK(std::string_view{track.artist} == "Some Artist");
    CHECK(std::string_view{track.path} == kPath);
    CHECK(track.song_id == 1415);
    CHECK(track.pos_id == 1);
    CHECK(track.duration_ms == 475000);
    CHECK(track.sample_rate_hz == 44100);
    CHECK(track.favourite == 1);
    CHECK(track.queue_position == 3);
    CHECK(track.queue_count == 12);
}

TEST_CASE("diskos_player_position belongs to the current track", "[legacy]") {
    diskos_player_reset();
    apply(track_frame("First Song", kPath, 0));
    apply("a103001000C9174D");
    const std::int64_t first = diskos_player_position();
    apply(track_frame("Second Song", "/tmp/sdcard/Some Album/02 - Second Song.flac", 2));

    CHECK(first == 0x00C9174D);
    CHECK(diskos_player_position() == -1);
}

TEST_CASE("diskos_player_apply clears a track that does not fit", "[legacy]") {
    diskos_player_reset();
    const std::string longest_title(DISKOS_PLAYER_TEXT_MAX, 'x');
    const std::string long_title(DISKOS_PLAYER_TEXT_MAX + 1, 'x');
    const std::string long_path = "/tmp/sdcard/" + std::string(DISKOS_PLAYER_TEXT_MAX, 'x');
    const diskos_player_update fitting = apply(track_frame(longest_title, kPath, 0));
    const diskos_player_update titled = apply(track_frame(long_title, kPath, 0));
    apply(track_frame("First Song", kPath, 0));
    const diskos_player_update pathed = apply(track_frame("First Song", long_path, 0));

    CHECK(fitting.accepted == 1);
    CHECK(titled.accepted == 0);
    CHECK(titled.track == 1);
    CHECK(pathed.accepted == 0);
    CHECK(pathed.track == 1);
    CHECK_FALSE(has_track());
}

TEST_CASE("diskos_player_reset forgets the track", "[legacy]") {
    apply(track_frame("First Song", kPath, 0));
    diskos_player_reset();

    CHECK_FALSE(has_track());
    CHECK(diskos_player_position() == -1);
}

}  // namespace
}  // namespace diskos::legacy
