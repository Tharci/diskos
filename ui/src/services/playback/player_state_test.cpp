// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_state.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "player/modes.hpp"
#include "player/reports.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::playback {
namespace {

using player::ReportedPlayState;

using namespace std::chrono_literals;

// A time for the reports whose timing does not matter.
constexpr PlayerState::Clock::time_point kNow{};

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

// A track from a phone over AirPlay: no ids and no path.
const player::Song kStreamed{.title = "Some Song", .artist = "Some Artist", .album = "Some Album"};

player::Report airplay(const player::Song& song, ReportedPlayState state) {
    return player::TrackReport{
            .song = song,
            .state = state,
            .work_mode = std::string{player::TrackReport::kAirPlay},
    };
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

/** Apply the reports in order, and get the play state after each one. */
std::vector<PlayState> play_states(PlayerState& state, const auto&... reports) {
    return {(state.apply_report(reports, kNow), state.play_state())...};
}

TEST_CASE("PlayerState follows playing, pausing and resuming", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    // Starting up paused, then play, pause, play: each change is reported at once.
    const auto started = play_states(state, track(kFirst, ReportedPlayState::Paused));
    const auto toggled = play_states(state, playing(), paused(), playing());

    CHECK(started == std::vector{Paused});
    CHECK(toggled == std::vector{Playing, Paused, Playing});
}

TEST_CASE("PlayerState loads a new track until it plays", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing), kNow);
    // Next: the track report reads Stopped, then the player starts it.
    const auto next =
            play_states(state, track(kSecond, ReportedPlayState::Stopped), playing(), position(0));

    CHECK(next == std::vector{Loading, Playing, Playing});
}

TEST_CASE("PlayerState keeps loading while the previous track pauses", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing), kNow);
    // Playing an artist: the player reports Paused for the track it leaves.
    const auto artist =
            play_states(state, track(kSecond, ReportedPlayState::Stopped), paused(), playing());

    CHECK(artist == std::vector{Loading, Loading, Playing});
}

TEST_CASE("PlayerState stops on a track that cannot be opened", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Paused), kNow);
    // Playing the empty file, then pressing play on it again.
    const auto opened = play_states(
            state, track(kEmptyFile, ReportedPlayState::Stopped), position(0), stopped()
    );
    const auto retried = play_states(
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
    const auto answered = play_states(stopped_player, track(kFirst, ReportedPlayState::Stopped));
    const auto positions = play_states(playing_player, position(5000), position(6000));

    CHECK(PlayerState{}.play_state() == Unknown);
    CHECK(answered == std::vector{Stopped});
    CHECK(positions == std::vector{Playing, Playing});
}

TEST_CASE("PlayerState says what each report changed", "[playback][player_state]") {
    PlayerState state;
    const Changes loaded = state.apply_report(track(kFirst, ReportedPlayState::Paused), kNow);
    const Changes started = state.apply_report(playing(), kNow);
    const Changes moved = state.apply_report(position(1000), kNow);
    const Changes unmoved = state.apply_report(position(1000), kNow);
    const Changes favourite =
            state.apply_report(track(kFirst, ReportedPlayState::Playing, true), kNow);
    const Changes next = state.apply_report(track(kSecond, ReportedPlayState::Stopped), kNow);
    const Changes louder = state.apply_report(player::VolumeReport{.level = volume(63)}, kNow);
    const Changes same_volume = state.apply_report(player::VolumeReport{.level = volume(63)}, kNow);
    const Changes battery = state.apply_report(player::BatteryReport{.percent = 87}, kNow);
    const Changes unknown =
            state.apply_report(player::UnknownReport{.tag = "a60a", .body = "0213"}, kNow);

    CHECK(loaded == Changes{.track = true, .play_state = true});
    CHECK(started == Changes{.play_state = true});
    CHECK(moved == Changes{.position = true});
    CHECK(unmoved == Changes{});
    CHECK(favourite == Changes{.details = true});
    CHECK(next == Changes{.track = true, .play_state = true, .position = true});
    CHECK(louder == Changes{.volume = true});
    CHECK(same_volume == Changes{});
    CHECK(battery == Changes{});
    CHECK(unknown == Changes{});
}

TEST_CASE("PlayerState keeps the track and its position", "[playback][player_state]") {
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing, true), kNow);
    state.apply_report(position(1000), kNow);
    const std::optional<Track> first = state.track();
    const std::optional<std::uint32_t> first_position = state.position_ms();
    state.apply_report(track(kSecond, ReportedPlayState::Stopped), kNow);

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
    state.apply_report(track(kFirst, ReportedPlayState::Playing), kNow);
    const Changes new_title = state.apply_report(track(retagged, ReportedPlayState::Playing), kNow);
    const Changes new_row = state.apply_report(track(requeued, ReportedPlayState::Playing), kNow);

    CHECK(new_title == Changes{.details = true});
    CHECK(new_row == Changes{.track = true});
}

TEST_CASE(
        "PlayerState clears the track for a local song without a path", "[playback][player_state]"
) {
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing), kNow);
    state.apply_report(position(1000), kNow);
    const Changes cleared = state.apply_report(
            track(player::Song{.title = "Some Song"}, ReportedPlayState::Playing), kNow
    );

    CHECK(cleared == Changes{.track = true, .position = true});
    CHECK_FALSE(state.track().has_value());
    CHECK(state.play_state() == PlayState::Playing);
}

TEST_CASE("PlayerState gets no track from a state-only report", "[playback][player_state]") {
    PlayerState state;
    state.apply_report(playing(), kNow);

    CHECK_FALSE(state.track().has_value());
    CHECK(state.play_state() == PlayState::Playing);
}

TEST_CASE("PlayerState tells AirPlay tracks apart by their texts", "[playback][player_state]") {
    player::Song next = kStreamed;
    next.title = "Other Song";
    player::Song longer = kStreamed;
    longer.duration_ms = 200000;
    PlayerState state;
    const Changes first = state.apply_report(airplay(kStreamed, ReportedPlayState::Playing), kNow);
    const Changes same = state.apply_report(airplay(longer, ReportedPlayState::Playing), kNow);
    const Changes other = state.apply_report(airplay(next, ReportedPlayState::Playing), kNow);

    CHECK(first == Changes{.track = true, .play_state = true});
    CHECK(same == Changes{.details = true});
    CHECK(other == Changes{.track = true});
    REQUIRE(state.track().has_value());
    CHECK(state.track()->song.title == "Other Song");
}

TEST_CASE(
        "PlayerState expires Playing when the position stops moving", "[playback][player_state]"
) {
    using enum PlayState;
    const auto start = kNow + 100s;
    const auto moved = start + 4s;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Playing), start);
    const auto first_deadline = state.deadline();
    state.apply_report(position(1000), moved);
    const auto deadline = state.deadline();
    const Changes early = state.advance(moved + PlayerState::kStale - 1ms);
    const Changes expired = state.advance(moved + PlayerState::kStale);

    CHECK(first_deadline == start + PlayerState::kStale);
    CHECK(deadline == moved + PlayerState::kStale);
    CHECK(early == Changes{});
    CHECK(expired == Changes{.play_state = true});
    CHECK(state.play_state() == Unknown);
    CHECK_FALSE(state.is_playing());
    CHECK_FALSE(state.deadline().has_value());
}

TEST_CASE("PlayerState expires Loading after a while", "[playback][player_state]") {
    using enum PlayState;
    const auto start = kNow + 100s;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Paused), start);
    state.apply_report(track(kSecond, ReportedPlayState::Stopped), start + 1s);
    const bool loading = state.is_playing();
    const Changes expired = state.advance(start + 1s + PlayerState::kLoadTimeout);

    CHECK(loading);
    CHECK(expired == Changes{.play_state = true});
    CHECK(state.play_state() == Unknown);
}

TEST_CASE("PlayerState lets no other state expire", "[playback][player_state]") {
    const auto start = kNow + 100s;
    PlayerState unknown;
    PlayerState paused_player;
    PlayerState stopped_player;
    paused_player.apply_report(track(kFirst, ReportedPlayState::Paused), start);
    stopped_player.apply_report(track(kFirst, ReportedPlayState::Stopped), start);

    CHECK_FALSE(unknown.deadline().has_value());
    CHECK_FALSE(paused_player.deadline().has_value());
    CHECK_FALSE(stopped_player.deadline().has_value());
    CHECK_FALSE(paused_player.is_playing());
    CHECK_FALSE(stopped_player.is_playing());
}

TEST_CASE("PlayerState times Playing from the report that started it", "[playback][player_state]") {
    const auto start = kNow + 100s;
    const auto resumed = start + 60s;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Paused), start);
    // Resumed long after pausing: playing from the report on, before the first position.
    state.apply_report(playing(), resumed);

    CHECK(state.is_playing());
    CHECK(state.deadline() == resumed + PlayerState::kStale);
}

TEST_CASE(
        "PlayerState expires a missed deadline before the next report", "[playback][player_state]"
) {
    using enum PlayState;
    const auto start = kNow + 100s;
    const auto late = start + 1min;
    PlayerState loading;
    PlayerState playing_player;
    loading.apply_report(track(kFirst, ReportedPlayState::Paused), start);
    loading.apply_report(track(kSecond, ReportedPlayState::Stopped), start);
    playing_player.apply_report(track(kFirst, ReportedPlayState::Playing), start);
    // No advance() call in between: the reports apply to the expired (Unknown) state.
    const Changes reloaded = loading.apply_report(track(kSecond, ReportedPlayState::Stopped), late);
    const Changes replayed = playing_player.apply_report(playing(), late);

    CHECK(reloaded == Changes{.play_state = true});
    CHECK(loading.play_state() == Stopped);
    CHECK(replayed == Changes{});
    CHECK(playing_player.deadline() == late + PlayerState::kStale);
}

TEST_CASE("PlayerState loads a resumed track until it plays", "[playback][player_state]") {
    using enum PlayState;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Paused), kNow);
    // Resuming over Bluetooth: the same track reads Stopped until the stream starts.
    const Changes requested = state.request_play(kNow);
    const auto resumed =
            play_states(state, track(kFirst, ReportedPlayState::Stopped), playing(), position(0));

    CHECK(requested == Changes{.play_state = true});
    CHECK(resumed == std::vector{Loading, Playing, Playing});
}

TEST_CASE("PlayerState expects a resume only from Paused or Stopped", "[playback][player_state]") {
    using enum PlayState;
    PlayerState unknown;
    PlayerState playing_player;
    PlayerState stopped_player;
    PlayerState no_track;
    playing_player.apply_report(track(kFirst, ReportedPlayState::Playing), kNow);
    stopped_player.apply_report(track(kFirst, ReportedPlayState::Stopped), kNow);
    no_track.apply_report(paused(), kNow);
    // A toggle while Unknown may pause; with no track there is nothing to play.
    const Changes from_unknown = unknown.request_play(kNow);
    const Changes from_playing = playing_player.request_play(kNow);
    const Changes from_stopped = stopped_player.request_play(kNow);
    const Changes without_track = no_track.request_play(kNow);

    CHECK(from_unknown == Changes{});
    CHECK(from_playing == Changes{});
    CHECK(from_stopped == Changes{.play_state = true});
    CHECK(without_track == Changes{});
    CHECK(unknown.play_state() == Unknown);
    CHECK(playing_player.play_state() == Playing);
    CHECK(stopped_player.play_state() == Loading);
    CHECK(no_track.play_state() == Paused);
}

TEST_CASE("PlayerState expires a resume that never comes", "[playback][player_state]") {
    using enum PlayState;
    const auto start = kNow + 100s;
    PlayerState state;
    state.apply_report(track(kFirst, ReportedPlayState::Paused), start);
    state.request_play(start + 1s);
    const Changes expired = state.advance(start + 1s + PlayerState::kLoadTimeout);

    CHECK(expired == Changes{.play_state = true});
    CHECK(state.play_state() == Unknown);
}

}  // namespace
}  // namespace diskos::playback
