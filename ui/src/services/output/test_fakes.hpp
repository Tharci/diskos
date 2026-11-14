// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "player/command_sender.hpp"
#include "player/commands.hpp"
#include "player/player_instance.hpp"
#include "services/output/interfaces.hpp"
#include "services/playback/player_controller.hpp"
#include "services/playback/player_state.hpp"

/** Fakes for the output router's tests. */
namespace diskos::output::testing {

/** What the fakes were asked to do, in order: frames, and the playback's calls. */
using Log = std::vector<std::string>;

inline constexpr player::PlayerInstance kInstance{1};

/**
 * Logs the frames it sends. It refuses a send meant for another instance than the connected one,
 * and fails every send after fail_after() of them.
 */
class FakeSender : public player::CommandSender {
public:
    explicit FakeSender(Log& log) : log_(&log) {}

    SendResult send(
            const player::Command& command,
            std::optional<player::PlayerInstance> instance = std::nullopt
    ) override {
        if (instance && instance != connected_) {
            return std::unexpected{player::SendError::WrongInstance};
        }
        if (succeeding_ && *succeeding_ == 0) {
            return std::unexpected{player::SendError::Failed};
        }
        if (succeeding_) {
            --*succeeding_;
        }
        log_->push_back(player::frame(command));
        return {};
    }

    /** Let `sends` more sends succeed, then fail every one; nullopt: never fail. */
    void fail_after(std::optional<int> sends) noexcept {
        succeeding_ = sends;
    }

    void set_connected(player::PlayerInstance instance) noexcept {
        connected_ = instance;
    }

private:
    Log* log_;
    std::optional<int> succeeding_{};
    player::PlayerInstance connected_ = kInstance;
};

/** Logs its calls; its play state is whatever the test sets, as the player's reports would. */
class FakePlayback : public PlaybackControl {
public:
    explicit FakePlayback(Log& log) : log_(&log) {}

    playback::PlayState play_state() override {
        return play_state_;
    }

    std::optional<playback::PlaybackSnapshot> snapshot() override {
        return playback::PlaybackSnapshot{
                .track = {.song = {.id = 957, .pos_id = 1, .path = "/tmp/sdcard/Some Song.flac"}},
                .position_ms = 183000,
                .playing = play_state_ == playback::PlayState::Playing,
        };
    }

    SendResult pause(Clock::time_point /*now*/) override {
        log_->emplace_back("pause");
        return {};
    }

    void note_output_reset(Clock::time_point /*now*/) override {
        log_->emplace_back("reset");
    }

    SendResult restore(
            const playback::PlaybackSnapshot& /*snapshot*/,
            playback::Resume resume,
            Clock::time_point /*now*/
    ) override {
        log_->emplace_back(
                resume == playback::Resume::AsBefore ? "restore as before" : "restore on next play"
        );
        return {};
    }

    void set_play_state(playback::PlayState state) noexcept {
        play_state_ = state;
    }

private:
    Log* log_;
    playback::PlayState play_state_ = playback::PlayState::Stopped;
};

class FakeUsbDac : public UsbDac {
public:
    bool connected() override {
        return connected_;
    }

    void set_connected(bool connected) noexcept {
        connected_ = connected;
    }

private:
    bool connected_ = true;
};

class FakeAdmission : public Admission {
public:
    std::optional<Blocker> blocker() override {
        return blocker_;
    }

    void set_blocker(std::optional<Blocker> blocker) noexcept {
        blocker_ = blocker;
    }

private:
    std::optional<Blocker> blocker_{};
};

/** Hands out what the test stored, and keeps what was saved last. */
class FakeStore : public RouteStore {
public:
    std::optional<StoredRoute> load() override {
        return stored_;
    }

    void save(const StoredRoute& route) override {
        saved_ = route;
    }

    void set_stored(std::optional<StoredRoute> route) {
        stored_ = std::move(route);
    }

    [[nodiscard]] const std::optional<StoredRoute>& saved() const noexcept {
        return saved_;
    }

private:
    std::optional<StoredRoute> stored_{};
    std::optional<StoredRoute> saved_{};
};

}  // namespace diskos::output::testing
