// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_controller.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "player/command_sender.hpp"
#include "player/commands.hpp"
#include "player/modes.hpp"
#include "player/player_instance.hpp"
#include "player/reports.hpp"
#include "services/playback/player_state.hpp"
#include "utils/synchronized.hpp"
#include <catch2/catch_test_macros.hpp>

namespace diskos::playback {
namespace {

using player::ReportedPlayState;
using namespace std::chrono_literals;

constexpr PlayerState::Clock::time_point kNow{};

constexpr player::PlayerInstance kInstance{1};

/**
 * Records the frames it sends. It fails every send while `failing`, and refuses one meant for
 * another instance than the connected one.
 */
class FakeSender : public player::CommandSender {
public:
    std::expected<void, player::SendError> send(
            const player::Command& command,
            std::optional<player::PlayerInstance> instance = std::nullopt
    ) override {
        if (instance && instance != connected_) {
            return std::unexpected{player::SendError::WrongInstance};
        }
        if (failing_) {
            return std::unexpected{player::SendError::Failed};
        }
        frames_.push_back(player::frame(command));
        return {};
    }

    void set_failing(bool failing) noexcept {
        failing_ = failing;
    }

    void set_connected(player::PlayerInstance instance) noexcept {
        connected_ = instance;
    }

    [[nodiscard]] const std::vector<std::string>& frames() const noexcept {
        return frames_;
    }

private:
    std::vector<std::string> frames_{};
    bool failing_ = false;
    player::PlayerInstance connected_ = kInstance;
};

/** Readies the player as the output router does on the internal DAC: it sends a 0666 first. */
class FakePreparer final : public PlayPreparer {
public:
    explicit FakePreparer(FakeSender& sender) : sender_(&sender) {}

    SendResult prepare_play() override {
        if (failing_) {
            return std::unexpected{player::SendError::Failed};
        }
        return sender_->send(player::RouteOutputCommand{.route = player::OutputRoute::InternalDac});
    }

    void set_failing(bool failing) noexcept {
        failing_ = failing;
    }

private:
    FakeSender* sender_;
    bool failing_ = false;
};

const std::string kPrepared = "0666000C0006";

const player::Song kSong{
        .id = 1415,
        .pos_id = 1,
        .title = "Some Song",
        .path = "/tmp/sdcard/Some Album/01 - Some Song.flac",
};

/**
 * A controller over a fake sender and the state of kInstance, which last reported `reported`.
 */
class Fixture {
public:
    explicit Fixture(std::optional<ReportedPlayState> reported) {
        controller_.set_preparer(preparer_);
        restart(kInstance);
        if (reported) {
            const player::Report report = player::TrackReport{.song = kSong, .state = *reported};
            apply(report);
        }
    }

    /** Connect the player `instance`, with a new state. */
    void restart(player::PlayerInstance instance) {
        sender_.set_connected(instance);
        state_.with([instance](PlayerState& state) { state = PlayerState{instance}; });
    }

    void apply(const player::Report& report, PlayerState::Clock::time_point now = kNow) {
        state_.with([&](PlayerState& state) { state.apply_report(report, now); });
    }

    [[nodiscard]] PlayState play_state() {
        return state_.with([](const PlayerState& state) { return state.play_state(); });
    }

    FakeSender& sender() noexcept {
        return sender_;
    }

    FakePreparer& preparer() noexcept {
        return preparer_;
    }

    PlayerController& controller() noexcept {
        return controller_;
    }

private:
    FakeSender sender_;
    FakePreparer preparer_{sender_};
    utils::Synchronized<PlayerState> state_;
    PlayerController controller_{sender_, state_};
};

TEST_CASE(
        "PlayerController loads a resumed track until it plays", "[playback][player_controller]"
) {
    using enum PlayState;
    Fixture fixture{ReportedPlayState::Paused};
    const bool sent = fixture.controller().toggle(kNow).has_value();
    const PlayState requested = fixture.play_state();
    // Over Bluetooth: the track reads Stopped, then the stream starts.
    fixture.apply(player::TrackReport{.song = kSong, .state = ReportedPlayState::Stopped});
    const PlayState reported_stopped = fixture.play_state();
    fixture.apply(player::PlayStateReport{.state = ReportedPlayState::Playing});

    CHECK(sent);
    CHECK(fixture.sender().frames() == std::vector<std::string>{"0201000C0000"});
    CHECK(requested == Loading);
    CHECK(reported_stopped == Loading);
    CHECK(fixture.play_state() == Playing);
}

TEST_CASE(
        "PlayerController expects a resume only from Paused or Stopped",
        "[playback][player_controller]"
) {
    using enum PlayState;
    Fixture playing{ReportedPlayState::Playing};
    Fixture stopped{ReportedPlayState::Stopped};
    Fixture unknown{std::nullopt};
    const bool all_sent = playing.controller().toggle(kNow).has_value()
                          && stopped.controller().toggle(kNow).has_value()
                          && unknown.controller().toggle(kNow).has_value();

    CHECK(all_sent);
    CHECK(playing.play_state() == Playing);
    CHECK(stopped.play_state() == Loading);
    CHECK(unknown.play_state() == Unknown);
}

TEST_CASE(
        "PlayerController leaves the state alone when a toggle is not sent",
        "[playback][player_controller]"
) {
    Fixture fixture{ReportedPlayState::Paused};
    fixture.sender().set_failing(true);
    const PlayerController::SendResult result = fixture.controller().toggle(kNow);

    CHECK(result == std::unexpected{player::SendError::Failed});
    CHECK(fixture.sender().frames().empty());
    CHECK(fixture.play_state() == PlayState::Paused);
}

TEST_CASE("PlayerController sends each command's frame", "[playback][player_controller]") {
    Fixture fixture{ReportedPlayState::Playing};
    PlayerController& controller = fixture.controller();
    const std::optional<player::VolumeLevel> volume = player::VolumeLevel::from(63);
    REQUIRE(volume.has_value());
    const bool all_sent = controller.next().has_value() && controller.previous().has_value()
                          && controller.seek(0x00C98DA8).has_value()
                          && controller.set_volume(*volume).has_value()
                          && controller.set_play_mode(player::PlayMode::Single).has_value()
                          && controller.set_favourite(true).has_value();
    const std::vector<std::string> expected{
            "0201000C0001",
            "0201000C0002",
            "0103001000C98DA8",
            "0715000C003F",
            "0102000C0004",
            "0104000C0001",
    };

    CHECK(all_sent);
    CHECK(fixture.sender().frames() == expected);
    CHECK(fixture.play_state() == PlayState::Playing);
}

// Restoring playback around an output change. The player's own reports are as captured on
// 2026-11-07, routing a track at 3:03 to a Bluetooth speaker.

const player::Song kOtherSong{
        .id = 1416,
        .pos_id = 2,
        .title = "Other Song",
        .path = "/tmp/sdcard/Some Album/02 - Other Song.flac",
};
constexpr std::uint32_t kAt = 183000;  // 3:03
const std::string kPlayPause = "0201000C0000";
const std::string kSeekBack = "010300100002CAD8";  // To 3:03

player::Report position(std::uint32_t position_ms) {
    return player::PositionReport{.position_ms = position_ms};
}

player::Report reported(ReportedPlayState state) {
    return player::PlayStateReport{.state = state};
}

/**
 * A fixture playing kSong at kAt, then routed and restored with `resume`: paused before the route
 * for Resume::AsBefore, kept playing into a lost sink for Resume::OnNextPlay.
 */
class RestoreFixture : public Fixture {
public:
    explicit RestoreFixture(Resume resume, std::uint32_t at = kAt)
        : Fixture{ReportedPlayState::Playing} {
        apply(position(at), time(0));
        const std::optional<PlaybackSnapshot> taken = controller().snapshot();
        REQUIRE(taken.has_value());
        snapshot_ = *taken;
        if (resume == Resume::AsBefore) {
            const bool paused = controller().pause(time(1)).has_value();
            REQUIRE(paused);
            apply(reported(ReportedPlayState::Paused), time(1));
        }
        controller().note_output_reset(time(2));
        restored_ = controller().restore(snapshot_, resume, time(2));
    }

    /** A time `seconds` after the snapshot. */
    static PlayerState::Clock::time_point time(int seconds) {
        return kNow + std::chrono::seconds{100 + seconds};
    }

    /** Apply `report` at time(seconds), then advance there. */
    void step(const player::Report& report, int seconds) {
        apply(report, time(seconds));
        controller().advance(time(seconds));
    }

    [[nodiscard]] const PlaybackSnapshot& snapshot() const noexcept {
        return snapshot_;
    }

    [[nodiscard]] const PlayerController::SendResult& restored() const noexcept {
        return restored_;
    }

private:
    PlaybackSnapshot snapshot_{};
    PlayerController::SendResult restored_{};
};

TEST_CASE(
        "PlayerController restores a track's position once it plays",
        "[playback][player_controller]"
) {
    RestoreFixture fixture{Resume::AsBefore};
    // The pause, the re-init's Stopped, then Playing; audio and positions follow ~2 s later.
    fixture.step(reported(ReportedPlayState::Paused), 2);
    fixture.step(player::TrackReport{.song = kSong, .state = ReportedPlayState::Stopped}, 4);
    const std::size_t before_playing = fixture.sender().frames().size();
    fixture.step(reported(ReportedPlayState::Playing), 5);
    fixture.step(position(kAt + 1000), 7);
    fixture.step(position(kAt + 2000), 8);
    const std::vector<std::string> expected{kPlayPause, kPlayPause, kSeekBack};

    CHECK(fixture.restored().has_value());
    CHECK(before_playing == 2);
    CHECK(fixture.sender().frames() == expected);
    CHECK(fixture.play_state() == PlayState::Playing);
}

TEST_CASE(
        "PlayerController seeks once more when a restore's seek did not land",
        "[playback][player_controller]"
) {
    RestoreFixture fixture{Resume::AsBefore};
    fixture.step(reported(ReportedPlayState::Playing), 5);
    fixture.step(position(1000), 7);  // From the beginning: the seek was ignored
    fixture.step(position(2000), 8);  // Again: give up
    fixture.step(position(3000), 9);
    const std::vector<std::string> expected{kPlayPause, kPlayPause, kSeekBack, kSeekBack};

    CHECK(fixture.sender().frames() == expected);
}

TEST_CASE(
        "PlayerController restores on the next play after a lost sink",
        "[playback][player_controller]"
) {
    RestoreFixture fixture{Resume::OnNextPlay};
    // The player kept playing into the lost sink: its last position arrives after the reset.
    fixture.step(position(kAt + 1000), 3);
    const std::size_t waiting = fixture.sender().frames().size();
    const PlayState after_reset = fixture.play_state();
    const bool played = fixture.controller().toggle(RestoreFixture::time(60)).has_value();
    fixture.step(reported(ReportedPlayState::Playing), 62);
    fixture.step(position(kAt + 1000), 64);
    const std::vector<std::string> expected{kPlayPause, kSeekBack};

    CHECK(fixture.restored().has_value());
    CHECK(waiting == 0);
    CHECK(after_reset == PlayState::Stopped);
    CHECK(played);
    CHECK(fixture.sender().frames() == expected);
}

TEST_CASE(
        "PlayerController keeps a paused track paused when restoring it as before",
        "[playback][player_controller]"
) {
    Fixture fixture{ReportedPlayState::Paused};
    fixture.apply(position(kAt));
    const std::optional<PlaybackSnapshot> snapshot = fixture.controller().snapshot();
    REQUIRE(snapshot.has_value());
    fixture.controller().note_output_reset(kNow);
    const bool restored =
            fixture.controller().restore(*snapshot, Resume::AsBefore, kNow).has_value();
    const std::size_t sent_by_restore = fixture.sender().frames().size();
    const bool played = fixture.controller().toggle(kNow).has_value();
    fixture.apply(reported(ReportedPlayState::Playing));
    fixture.controller().advance(kNow);

    CHECK(restored);
    CHECK(sent_by_restore == 0);
    CHECK(played);
    CHECK(fixture.sender().frames() == std::vector{kPlayPause, kSeekBack});
}

TEST_CASE(
        "PlayerController cancels a restore for another track or a user's skip or seek",
        "[playback][player_controller]"
) {
    RestoreFixture other_track{Resume::AsBefore};
    other_track.step(
            player::TrackReport{.song = kOtherSong, .state = ReportedPlayState::Playing}, 5
    );
    other_track.step(player::TrackReport{.song = kSong, .state = ReportedPlayState::Playing}, 6);
    RestoreFixture skipped{Resume::AsBefore};
    const bool skip_sent = skipped.controller().next().has_value();
    skipped.step(reported(ReportedPlayState::Playing), 5);
    RestoreFixture sought{Resume::AsBefore};
    const bool seek_sent = sought.controller().seek(5000).has_value();
    sought.step(reported(ReportedPlayState::Playing), 5);

    CHECK(skip_sent);
    CHECK(seek_sent);
    CHECK(other_track.sender().frames() == std::vector{kPlayPause, kPlayPause});
    CHECK(skipped.sender().frames()
          == std::vector<std::string>{kPlayPause, kPlayPause, "0201000C0001"});
    CHECK(sought.sender().frames()
          == std::vector<std::string>{kPlayPause, kPlayPause, "0103001000001388"});
}

TEST_CASE(
        "PlayerController sends nothing decided on a player that has restarted",
        "[playback][player_controller]"
) {
    // The new player is connected, but the state is still the old one's.
    Fixture fixture{ReportedPlayState::Paused};
    fixture.sender().set_connected(kInstance.next());
    const PlayerController::SendResult toggled = fixture.controller().toggle(kNow);
    const PlayerController::SendResult sought = fixture.controller().seek(5000);

    CHECK(toggled == std::unexpected{player::SendError::WrongInstance});
    CHECK(sought == std::unexpected{player::SendError::WrongInstance});
    CHECK(fixture.sender().frames().empty());
    CHECK(fixture.play_state() == PlayState::Paused);
}

TEST_CASE(
        "PlayerController drops a restore when the player restarts", "[playback][player_controller]"
) {
    RestoreFixture fixture{Resume::AsBefore};
    fixture.restart(kInstance.next());
    // The new player plays the same track: the restore was about the old one.
    fixture.step(player::TrackReport{.song = kSong, .state = ReportedPlayState::Playing}, 5);
    fixture.step(position(1000), 7);

    CHECK(fixture.sender().frames() == std::vector{kPlayPause, kPlayPause});
}

TEST_CASE(
        "PlayerController does not restore a position near the start",
        "[playback][player_controller]"
) {
    RestoreFixture fixture{Resume::AsBefore, 1500};
    fixture.step(reported(ReportedPlayState::Playing), 5);
    fixture.step(position(1000), 7);

    CHECK(fixture.sender().frames() == std::vector{kPlayPause, kPlayPause});
}

// The album "Some Album" from its first song.
const player::PlayListCommand kAlbum{
        .type = player::ListType::Album,
        .position = 0,
        .name = player::ListName::from("Some Album").value(),
};
const std::string kPlayAlbum = "0100001A00000003Some Album";

TEST_CASE(
        "PlayerController readies an idle player before a list play",
        "[playback][player_controller]"
) {
    Fixture paused{ReportedPlayState::Paused};
    Fixture no_track{std::nullopt};
    const bool from_paused = paused.controller().play_list(kAlbum, kNow).has_value();
    const bool from_nothing = no_track.controller().play_list(kAlbum, kNow).has_value();
    const std::vector<std::string> expected{kPrepared, kPlayAlbum};

    CHECK(from_paused);
    CHECK(from_nothing);
    CHECK(paused.sender().frames() == expected);
    CHECK(no_track.sender().frames() == expected);
    CHECK(paused.play_state() == PlayState::Loading);
    CHECK(no_track.play_state() == PlayState::Loading);
}

TEST_CASE(
        "PlayerController plays a list at once while one plays", "[playback][player_controller]"
) {
    Fixture playing{ReportedPlayState::Playing};
    const bool sent = playing.controller().play_list(kAlbum, kNow).has_value();

    CHECK(sent);
    CHECK(playing.sender().frames() == std::vector{kPlayAlbum});
    CHECK(playing.play_state() == PlayState::Loading);
}

TEST_CASE(
        "PlayerController sends no play the player could not be readied for",
        "[playback][player_controller]"
) {
    Fixture fixture{ReportedPlayState::Paused};
    fixture.preparer().set_failing(true);
    const PlayerController::SendResult played = fixture.controller().play_list(kAlbum, kNow);

    CHECK(played == std::unexpected{player::SendError::Failed});
    CHECK(fixture.sender().frames().empty());
    CHECK(fixture.play_state() == PlayState::Paused);
}

TEST_CASE(
        "PlayerController leaves the state alone when a play is not sent",
        "[playback][player_controller]"
) {
    Fixture fixture{ReportedPlayState::Playing};
    fixture.sender().set_failing(true);
    const PlayerController::SendResult played = fixture.controller().play_list(kAlbum, kNow);

    CHECK(played == std::unexpected{player::SendError::Failed});
    CHECK(fixture.play_state() == PlayState::Playing);
}

TEST_CASE(
        "PlayerController continues a list play's track from a start position",
        "[playback][player_controller]"
) {
    // A Beam return: the album from the track and position the TV played.
    Fixture fixture{ReportedPlayState::Paused};
    const bool played = fixture.controller().play_list(kAlbum, kNow, kAt).has_value();
    const auto step = [&fixture](const player::Report& report, std::chrono::seconds at) {
        fixture.apply(report, kNow + at);
        fixture.controller().advance(kNow + at);
    };
    step(reported(ReportedPlayState::Paused), 0s);  // The track it leaves
    step(player::TrackReport{.song = kOtherSong, .state = ReportedPlayState::Stopped}, 1s);
    const std::size_t before_playing = fixture.sender().frames().size();
    step(reported(ReportedPlayState::Playing), 2s);
    step(position(kAt + 1000), 4s);
    step(position(kAt + 2000), 5s);
    const std::vector<std::string> expected{kPrepared, kPlayAlbum, kSeekBack};

    CHECK(played);
    CHECK(before_playing == 2);
    CHECK(fixture.sender().frames() == expected);
}

TEST_CASE(
        "PlayerController drops a start position when the play does not start",
        "[playback][player_controller]"
) {
    Fixture fixture{ReportedPlayState::Paused};
    REQUIRE(fixture.controller().play_list(kAlbum, kNow, kAt).has_value());
    fixture.apply(reported(ReportedPlayState::Stopped));  // A file it could not open
    fixture.controller().advance(kNow);
    const bool resumed = fixture.controller().toggle(kNow).has_value();
    fixture.apply(reported(ReportedPlayState::Playing));
    fixture.controller().advance(kNow);
    const std::vector<std::string> expected{kPrepared, kPlayAlbum, kPlayPause};

    CHECK(resumed);
    CHECK(fixture.sender().frames() == expected);
}

TEST_CASE("PlayerController pauses only what plays", "[playback][player_controller]") {
    Fixture playing{ReportedPlayState::Playing};
    Fixture paused{ReportedPlayState::Paused};
    Fixture unknown{std::nullopt};
    const bool all_done = playing.controller().pause(kNow).has_value()
                          && paused.controller().pause(kNow).has_value()
                          && unknown.controller().pause(kNow).has_value();

    CHECK(all_done);
    CHECK(playing.sender().frames() == std::vector{kPlayPause});
    CHECK(paused.sender().frames().empty());
    CHECK(unknown.sender().frames().empty());
}

TEST_CASE("PlayerController snapshots what plays", "[playback][player_controller]") {
    Fixture playing{ReportedPlayState::Playing};
    playing.apply(position(kAt));
    const std::optional<PlaybackSnapshot> snapshot = playing.controller().snapshot();
    Fixture no_track{std::nullopt};

    REQUIRE(snapshot.has_value());
    CHECK(snapshot->track.song == kSong);
    CHECK(snapshot->position_ms == kAt);
    CHECK(snapshot->playing);
    CHECK_FALSE(no_track.controller().snapshot().has_value());
}

TEST_CASE(
        "PlayerController drops a pending restore for a list play", "[playback][player_controller]"
) {
    RestoreFixture fixture{Resume::OnNextPlay};
    const bool played =
            fixture.controller().play_list(kAlbum, RestoreFixture::time(10)).has_value();
    fixture.step(reported(ReportedPlayState::Playing), 12);
    fixture.step(position(1000), 13);

    CHECK(played);
    CHECK(fixture.sender().frames() == std::vector{kPrepared, kPlayAlbum});
}

TEST_CASE(
        "PlayerController keeps a pending restore's position in a snapshot",
        "[playback][player_controller]"
) {
    // A lost sink, then the earphones play by themselves and the router moves back to them
    // before the restore lands (capture of 2026-11-13): the next snapshot still holds kAt.
    RestoreFixture fixture{Resume::OnNextPlay};
    fixture.step(reported(ReportedPlayState::Playing), 10);
    const std::optional<PlaybackSnapshot> during = fixture.controller().snapshot();
    fixture.step(position(kAt + 1000), 12);
    fixture.step(position(kAt + 5000), 16);
    const std::optional<PlaybackSnapshot> after = fixture.controller().snapshot();

    REQUIRE(during.has_value());
    REQUIRE(after.has_value());
    CHECK(during->position_ms == kAt);
    CHECK(during->playing);
    CHECK(after->position_ms == kAt + 5000);
}

}  // namespace
}  // namespace diskos::playback
