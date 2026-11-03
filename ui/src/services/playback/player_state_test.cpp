// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_state.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include "player/modes.hpp"
#include "player/reports.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::playback {
namespace {

using player::ReportedPlayState;

const player::Song kFirst{
        .id = 1415,
        .pos_id = 1,
        .title = "First Song",
        .path = "/tmp/sdcard/Some Album/01 - First Song.flac",
        .duration_ms = 475000,
};
const player::Song kSecond{
        .id = 1416,
        .pos_id = 2,
        .title = "Second Song",
        .path = "/tmp/sdcard/Some Album/02 - Second Song.flac",
        .duration_ms = 168000,
};
// A FLAC file with no audio, which the player cannot open.
const player::Song kEmptyFile{
        .id = 1269,
        .pos_id = 1,
        .title = "01 - Some File.flac",
        .path = "/tmp/sdcard/Some Folder/01 - Some File.flac",
};

player::Report track(const player::Song& song, ReportedPlayState state, bool favourite = false) {
    return player::TrackReport{.song = song, .favourite = favourite, .state = state};
}

player::Report playing() {
    return player::PlayStateReport{.state = ReportedPlayState::Playing};
}

player::Report paused() {
    return player::PlayStateReport{.state = ReportedPlayState::Paused};
}

player::Report stopped() {
    return player::PlayStateReport{.state = ReportedPlayState::Stopped};
}

player::Report position(std::uint32_t position_ms) {
    return player::PositionReport{.position_ms = position_ms};
}

player::VolumeLevel volume(std::uint32_t level) {
    const std::optional<player::VolumeLevel> volume = player::VolumeLevel::from(level);
    REQUIRE(volume.has_value());
    return *volume;
}

/** Apply the reports in order, and get the activity after each one. */
std::vector<PlayState> activities(PlayerState& state, const auto&... reports) {
    return {(state.apply_report(reports), state.activity())...};
}

TEST_CASE("PlayerState follows playing, pausing and resuming", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    // Starting up paused, then play, pause, play: each change is reported at once.
    const auto started = activities(state, track(kFirst, ReportedPlayState::Paused));
    const auto toggled = activities(state, playing(), paused(), playing());

    CHECK(started == std::vector{Paused});
    CHECK(toggled == std::vector{Playing, Paused, Playing});
}

TEST_CASE("PlayerState loads a new track until it plays", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing));
    // Next: the track report reads Stopped, then the player starts it.
    const auto next =
            activities(state, track(kSecond, ReportedPlayState::Stopped), playing(), position(0));

    CHECK(next == std::vector{Loading, Playing, Playing});
}

TEST_CASE("PlayerState keeps loading while the previous track pauses", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing));
    // Playing an artist: the player reports Paused for the track it leaves.
    const auto artist =
            activities(state, track(kSecond, ReportedPlayState::Stopped), paused(), playing());

    CHECK(artist == std::vector{Loading, Loading, Playing});
}

TEST_CASE("PlayerState stops on a track that cannot be opened", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Paused));
    // Playing the empty file, then pressing play on it again.
    const auto opened = activities(
            state, track(kEmptyFile, ReportedPlayState::Stopped), position(0), stopped()
    );
    const auto retried = activities(
            state, track(kEmptyFile, ReportedPlayState::Stopped), position(0), stopped()
    );

    CHECK(opened == std::vector{Loading, Loading, Stopped});
    CHECK(retried == std::vector{Stopped, Stopped, Stopped});
}

TEST_CASE("PlayerState takes the first report after startup as it is", "[playback][player_state]") {
    using enum PlayState;
    PlayerState stopped_player;
    PlayerState playing_player;
    // A stopped player answers 02020008 with a track that reads Stopped; a playing one after a UI
    // restart may not answer at all, but keeps sending positions.
    const auto answered = activities(stopped_player, track(kFirst, ReportedPlayState::Stopped));
    const auto positions = activities(playing_player, position(5000), position(6000));

    CHECK(PlayerState{}.activity() == Unknown);
    CHECK(answered == std::vector{Stopped});
    CHECK(positions == std::vector{Playing, Playing});
}

TEST_CASE("PlayerState says what each report changed", "[playback][player_state]") {
    PlayerState state;
    const Changes loaded = state.apply_report(track(kFirst, ReportedPlayState::Paused));
    const Changes started = state.apply_report(playing());
    const Changes moved = state.apply_report(position(1000));
    const Changes unmoved = state.apply_report(position(1000));
    const Changes favourite = state.apply_report(track(kFirst, ReportedPlayState::Playing, true));
    const Changes next = state.apply_report(track(kSecond, ReportedPlayState::Stopped));
    const Changes louder = state.apply_report(player::VolumeReport{.level = volume(63)});
    const Changes same_volume = state.apply_report(player::VolumeReport{.level = volume(63)});
    const Changes battery = state.apply_report(player::BatteryReport{.percent = 87});
    const Changes unknown =
            state.apply_report(player::UnknownReport{.tag = "a60a", .body = "0213"});

    CHECK(loaded == Changes{.track = true, .activity = true});
    CHECK(started == Changes{.activity = true});
    CHECK(moved == Changes{.position = true});
    CHECK(unmoved == Changes{});
    CHECK(favourite == Changes{.details = true});
    CHECK(next == Changes{.track = true, .activity = true, .position = true});
    CHECK(louder == Changes{.volume = true});
    CHECK(same_volume == Changes{});
    CHECK(battery == Changes{});
    CHECK(unknown == Changes{});
}

TEST_CASE("PlayerState keeps the track and its position", "[playback][player_state]") {
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing, true));
    state.apply_report(position(1000));
    const std::optional<Track> first = state.track();
    const std::optional<std::uint32_t> first_position = state.position_ms();
    state.apply_report(track(kSecond, ReportedPlayState::Stopped));

    REQUIRE(first.has_value());
    CHECK(first->song == kFirst);
    CHECK(first->favourite);
    CHECK(first_position == 1000);
    REQUIRE(state.track().has_value());
    CHECK(state.track()->song == kSecond);
    CHECK_FALSE(state.position_ms().has_value());
}

TEST_CASE("PlayerState tells tracks apart by file and queue row", "[playback][player_state]") {
    player::Song retagged = kFirst;
    retagged.title = "First Song (Remastered)";
    player::Song requeued = kFirst;
    requeued.pos_id = 7;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing));
    const Changes new_title = state.apply_report(track(retagged, ReportedPlayState::Playing));
    const Changes new_row = state.apply_report(track(requeued, ReportedPlayState::Playing));

    CHECK(new_title == Changes{.details = true});
    CHECK(new_row == Changes{.track = true});
}

TEST_CASE("PlayerState clears the track for a song without a path", "[playback][player_state]") {
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing));
    state.apply_report(position(1000));
    const Changes cleared = state.apply_report(
            track(player::Song{.title = "Some Song"}, ReportedPlayState::Playing)
    );

    CHECK(cleared == Changes{.track = true, .position = true});
    CHECK_FALSE(state.track().has_value());
    CHECK(state.activity() == PlayState::Playing);
}

TEST_CASE("PlayerState gets no track from a state-only report", "[playback][player_state]") {
    PlayerState state;
    state.apply_report(playing());

    CHECK_FALSE(state.track().has_value());
    CHECK(state.activity() == PlayState::Playing);
}

}  // namespace
}  // namespace diskos::playback
