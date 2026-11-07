// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "services/playback/player_controller.hpp"

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

constexpr PlayerState::Clock::time_point kNow{};

/** Records the frames it is asked to send; fails every send while `failing`. */
class FakeSender : public player::CommandSender {
public:
    std::expected<void, player::SendError> send(
            const player::Command& command,
            std::optional<player::PlayerInstance> /*instance*/ = std::nullopt
    ) override {
        if (failing_) {
            return std::unexpected{player::SendError::Failed};
        }
        frames_.push_back(player::frame(command));
        return {};
    }

    void set_failing(bool failing) noexcept {
        failing_ = failing;
    }

    [[nodiscard]] const std::vector<std::string>& frames() const noexcept {
        return frames_;
    }

private:
    std::vector<std::string> frames_{};
    bool failing_ = false;
};

const player::Song kSong{
        .id = 1415,
        .pos_id = 1,
        .title = "Some Song",
        .path = "/tmp/sdcard/Some Album/01 - Some Song.flac",
};

/** A controller over a fake sender and a state the player last reported as `reported`. */
class Fixture {
public:
    explicit Fixture(std::optional<ReportedPlayState> reported) {
        if (reported) {
            const player::Report report = player::TrackReport{.song = kSong, .state = *reported};
            apply(report);
        }
    }

    void apply(const player::Report& report) {
        state_.with([&report](PlayerState& state) { state.apply_report(report, kNow); });
    }

    [[nodiscard]] PlayState play_state() {
        return state_.with([](const PlayerState& state) { return state.play_state(); });
    }

    FakeSender& sender() noexcept {
        return sender_;
    }

    PlayerController& controller() noexcept {
        return controller_;
    }

private:
    FakeSender sender_;
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

}  // namespace
}  // namespace diskos::playback
