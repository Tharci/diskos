// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_state.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "legacy/test_frames.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::legacy {
namespace {

constexpr std::string_view kPath = "/tmp/sdcard/Some Album/01 - First Song.flac";

diskos_player_update apply(std::string_view frame) {
    return diskos_player_apply(DISKOS_PLAYER_LOCAL, frame.data(), frame.size());
}

diskos_player_update receive(std::string_view frame) {
    return diskos_player_apply(DISKOS_PLAYER_RECEIVER, frame.data(), frame.size());
}

/** Whether the bridge holds a track. */
bool has_track() {
    diskos_player_track track{};
    return diskos_player_get_track(DISKOS_PLAYER_LOCAL, &track) == 1;
}

TEST_CASE("diskos_player_apply reports what each frame did", "[legacy]") {
    diskos_player_reset(DISKOS_PLAYER_LOCAL);
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
    diskos_player_reset(DISKOS_PLAYER_LOCAL);
    apply(track_frame("First Song / Café Two", kPath, 1));
    diskos_player_track track{};

    REQUIRE(diskos_player_get_track(DISKOS_PLAYER_LOCAL, &track) == 1);
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
    diskos_player_reset(DISKOS_PLAYER_LOCAL);
    apply(track_frame("First Song", kPath, 0));
    apply("a103001000C9174D");
    const std::int64_t first = diskos_player_position(DISKOS_PLAYER_LOCAL);
    apply(track_frame("Second Song", "/tmp/sdcard/Some Album/02 - Second Song.flac", 2));

    CHECK(first == 0x00C9174D);
    CHECK(diskos_player_position(DISKOS_PLAYER_LOCAL) == -1);
}

TEST_CASE("diskos_player_apply clears a track that does not fit", "[legacy]") {
    diskos_player_reset(DISKOS_PLAYER_LOCAL);
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
    diskos_player_reset(DISKOS_PLAYER_LOCAL);

    CHECK_FALSE(has_track());
    CHECK(diskos_player_position(DISKOS_PLAYER_LOCAL) == -1);
}

TEST_CASE("diskos_player_apply keeps the receiver apart from local playback", "[legacy]") {
    diskos_player_reset(DISKOS_PLAYER_LOCAL);
    diskos_player_reset(DISKOS_PLAYER_RECEIVER);
    const std::string long_title(DISKOS_PLAYER_TEXT_MAX + 1, 'x');
    apply(track_frame("First Song", kPath, 0));
    const diskos_player_update streamed = receive(airplay_frame("Some Song", 0));
    const diskos_player_update same = receive(airplay_frame("Some Song", 1));
    const diskos_player_update next = receive(airplay_frame(long_title, 0));
    diskos_player_track local{};
    diskos_player_track receiver{};

    CHECK(streamed.accepted == 1);
    CHECK(streamed.track == 1);
    CHECK(same.track == 0);
    CHECK(same.reported_state == 1);
    CHECK(next.accepted == 1);
    CHECK(next.track == 1);
    REQUIRE(diskos_player_get_track(DISKOS_PLAYER_LOCAL, &local) == 1);
    REQUIRE(diskos_player_get_track(DISKOS_PLAYER_RECEIVER, &receiver) == 1);
    CHECK(std::string_view{local.title} == "First Song");
    CHECK(std::string_view{receiver.title} == long_title);
    CHECK(std::string_view{receiver.path}.empty());
}

}  // namespace
}  // namespace diskos::legacy
