// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include "player/command_sender.hpp"
#include "player/player_instance.hpp"
#include "player/report_listener.hpp"
#include "player/reports.hpp"
#include "services/output/interfaces.hpp"
#include "services/output/output.hpp"
#include "services/output/player_readiness.hpp"
#include "services/output/transition.hpp"
#include "services/playback/play_preparer.hpp"
#include "services/playback/player_controller.hpp"
#include "utils/bluetooth_address.hpp"

namespace diskos::output {

/** Potential outcomes of advance() */
struct Outcome {
    enum class Kind : std::uint8_t {
        Switched,      // The audio now goes to `output`
        Retrying,      // A move to `output` failed with nothing changed; it is tried again
        Failed,        // A move to `output` failed kMaxAttempts times: the choice went back
        NoUsbDac,      // A move to `output` found no USB DAC: the choice went back
        Recovering,    // A move to `output` failed halfway: retried until one lands
        ResumeFailed,  // After the move to `output`, playback could not be started again
    };

    Kind kind{};
    Output output{};

    friend bool operator==(const Outcome&, const Outcome&) = default;
};

/** Why select() refused a choice: each needs the user to act first. */
enum class Refusal : std::uint8_t {
    CardNotReady,
    OtherSource,
    Audiobook,
    NoUsbDac,
};

/**
 * Where the player sends its audio. It keeps the output the user and the Bluetooth layer want
 * (desired()) and the one the player has (applied()), and moves the player from one to the other
 * whenever it may, one transition at a time.
 *
 * A transition starts once the player is ready (PlayerReadiness), the retry wait has passed and
 * nothing blocks it (Admission). It is one of three kinds:
 *  - Change: desired() changed. It waits while a play starts (Loading), then pauses playback if
 *    it plays, and resumes it after.
 *  - Correction: desired() did not change, but applied() was lost (a player restart, a source
 *    change, a move that failed halfway). It waits until playback stops: it never interrupts it.
 *  - Departure: applied() is a Bluetooth sink that is gone. It does not pause (the player plays
 *    into the dead sink), waits for nothing but the source and the card, and resumes on the
 *    user's next play.
 * As the PlayPreparer, it readies the player before each play from idle (prepare_play()).
 * A move that fails before any 0666 changed nothing: it resumes playback and is retried after
 * kRetryWait, a Change up to kMaxAttempts times. One that fails after a 0666 leaves the output
 * unknown: it is retried every kRecoveryRetry until one lands, and playback stays paused till then.
 *
 * It keeps pointers to everything it is given, which must outlive it. It is not thread-safe: call
 * it from one thread, the main loop.
 */
class OutputRouter final : public player::ReportListener, public playback::PlayPreparer {
public:
    // After a move that failed with nothing changed.
    static constexpr Clock::duration kRetryWait = std::chrono::seconds{3};
    // After a move that failed halfway: the player may be left with no work mode.
    static constexpr Clock::duration kRecoveryRetry = std::chrono::milliseconds{500};
    // The most attempts at a Change.
    static constexpr int kMaxAttempts = 3;

    OutputRouter(
            player::CommandSender& sender,
            PlaybackControl& playback,
            UsbDac& usb_dac,
            Admission& admission,
            RouteStore& store
    );

    // Properties

    /** Get where the audio should go: the internal DAC until something else is wanted. */
    [[nodiscard]] const Output& desired() const noexcept {
        return desired_;
    }

    /** Get where the player sends its audio, or nullopt if that is unknown. */
    [[nodiscard]] const std::optional<Output>& applied() const noexcept {
        return applied_;
    }

    /** Check whether the player may be given a route (PlayerReadiness). */
    [[nodiscard]] bool ready() const noexcept {
        return readiness_.ready();
    }

    /** Check whether a transition runs, or a move that failed halfway is being retried. */
    [[nodiscard]] bool busy() const noexcept {
        return transition_.has_value() || recovering_;
    }

    // Intents

    /**
     * Choose `target`, as the user does. Choosing desired() again changes nothing.
     */
    [[nodiscard]] std::expected<void, Refusal> select(const Output& target);

    /**
     * Note that the Bluetooth sink `device` is connected: it becomes desired(), unless the user
     * chose S/PDIF or USB audio.
     */
    void bluetooth_connected(const utils::BluetoothAddress& device);

    /**
     * Note that the Bluetooth sink `device` is gone, or about to be: the internal DAC replaces it
     * as desired(), and leaving it is a Departure. Call advance() right after: a Departure sends
     * its first 0666 at once, so the audio leaves the sink before it is dropped.
     */
    void bluetooth_lost(const utils::BluetoothAddress& device);

    // The player: PlayerLink::deliver() calls these

    /**
     * Note that the player `instance` connected: a running transition is dropped and applied()
     * becomes unknown. On the first connection, applied() and whether the player had settled come
     * from the store instead (a UI restart), and a stored applied() becomes desired() too: the
     * route the player has is kept.
     */
    void on_connected(player::PlayerInstance instance, Clock::time_point now) override;

    /**
     * Hear one report from the connected player. A track, play-state or position report shows
     * that it takes commands, as the answer to a state request does: PlayerReadiness waits for one.
     * A list of connected Bluetooth sinks without the sink that is applied() counts as
     * bluetooth_lost(): the player reports a dropped link first, while it still plays into it.
     * A switch to a source other than Local makes applied() unknown and drops a running
     * transition: the player re-routes for that source.
     */
    void on_report(const player::Report& report, Clock::time_point now) override;

    // Playing

    /**
     * Prepare a local play from idle: send the play preamble (plan_play_preamble()) for applied(),
     * right before the play command. While applied() is unknown it goes to desired(), or to the
     * internal DAC if that is a Bluetooth sink; if the player then reports Playing, the route
     * evidently took, and becomes applied(). Does nothing while a transition runs or before a
     * player connected. PlayerController calls it only when nothing plays: a 0666 gaps a playing
     * track.
     * Returns a SendError if a frame was not sent.
     */
    [[nodiscard]] SendResult prepare_play() override;

    // Time

    /**
     * Advance to `now`: ask a starting player for its state, start a transition when one may
     * start, and run it as far as it goes. Call it every tick of the main loop.
     * Returns what happened, oldest first.
     */
    [[nodiscard]] std::vector<Outcome> advance(Clock::time_point now);

private:
    enum class Kind : std::uint8_t {
        Change,
        Correction,
        Departure,
    };

    [[nodiscard]] Kind next_kind() const;
    [[nodiscard]] bool departing() const;
    [[nodiscard]] bool may_start(Clock::time_point now);
    /** Lose the applied Bluetooth sink if the player no longer lists it. */
    void note_sinks(const player::BluetoothSinksReport& report);
    /**
     * Forget applied() if the player left Local. A switch back to Local changes nothing: applied()
     * has been unknown since it left, and every 0657 8, the router's own included, is reported
     * again ~1 s later (captures of 2026-11-01), when a transition may already have run.
     */
    void note_source(const player::SourceReport& report);
    /** Note that the player's output is not known: applied() becomes unknown, a transition ends. */
    void forget_applied();
    /** Take a prepared output as applied once the player plays on it. */
    void confirm_prepared();
    void start(player::PlayerInstance instance);
    void run(Clock::time_point now, std::vector<Outcome>& outcomes);
    void on_done(const Transition& done, Clock::time_point now, std::vector<Outcome>& outcomes);
    void on_failed(const Transition& failed, Clock::time_point now, std::vector<Outcome>& outcomes);
    /** Resume playback after a move that paused it and changed nothing. */
    void resume(const Output& target, Clock::time_point now, std::vector<Outcome>& outcomes);
    /** Restore the snapshot, playing it again `when` said, and report if its play was not sent. */
    void restore(
            playback::Resume when,
            const Output& target,
            Clock::time_point now,
            std::vector<Outcome>& outcomes
    );
    /** Give up on desired(): back to applied(), or to the internal DAC if that is unknown. */
    void revert_desired();
    void save();

    player::CommandSender* sender_;
    PlaybackControl* playback_;
    UsbDac* usb_dac_;
    Admission* admission_;
    RouteStore* store_;

    std::optional<player::PlayerInstance> instance_{};
    PlayerReadiness readiness_;
    bool loaded_ = false;  // The store was read, on the first connection
    Output desired_{};
    std::optional<Output> applied_{};
    bool intent_ = false;  // desired_ changed since a transition last started
    std::optional<utils::BluetoothAddress> lost_{};  // The sink that went away
    std::optional<Transition> transition_{};
    std::optional<Output> prepared_{};  // Its preamble was sent while applied_ was unknown
    Kind kind_ = Kind::Correction;      // Of the running or last transition
    std::optional<playback::PlaybackSnapshot>
            snapshot_{};       // Taken at its start, kept till restored
    bool recovering_ = false;  // A move failed halfway
    int failures_ = 0;         // Changes in a row that failed with nothing changed
    Clock::time_point retry_at_{};
};

}  // namespace diskos::output
