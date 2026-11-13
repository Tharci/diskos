// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>

#include "player/command_sender.hpp"
#include "player/modes.hpp"
#include "player/player_instance.hpp"
#include "services/playback/player_state.hpp"
#include "utils/synchronized.hpp"

namespace diskos::playback {

/** What was playing, to restore after the output changed. */
struct PlaybackSnapshot {
    Track track{};
    std::uint32_t position_ms = 0;
    bool playing = false;
};

/** When a restore plays the track again. */
enum class Resume : std::uint8_t {
    AsBefore,    // At once if it was playing: a normal output change
    OnNextPlay,  // On the user's next play: the sink was lost
};

/**
 * Sends the commands that drive local playback to the stock player, and tells its PlayerState
 * what a command will do before the player reports it.
 * It keeps pointers to the sender and the state, which must outlive it. It holds the state's
 * lock only to read or update it, never while sending. It is not thread-safe: call it from one
 * thread, the main loop.
 */
class PlayerController {
public:
    using Clock = PlayerState::Clock;
    using SendResult = std::expected<void, player::SendError>;

    // A position counts as restored within this tolerance window
    static constexpr std::chrono::milliseconds kRestoreTolerance{2000};
    static constexpr int kMaxRestoreSeeks = 2;

    PlayerController(player::CommandSender& sender, utils::Synchronized<PlayerState>& state)
        : sender_(&sender), state_(&state) {}

    /**
     * Play if paused, pause if playing: the player only has a toggle.
     * Returns SendError::Failed or WrongInstance if it was not sent; the state is then left as it
     * was.
     */
    [[nodiscard]] SendResult toggle(Clock::time_point now);

    /**
     * Skip to the next track in the queue, cancelling a pending restore.
     * Returns SendError::Failed or WrongInstance if it was not sent.
     */
    [[nodiscard]] SendResult next();

    /**
     * Go back to the previous track in the queue, cancelling a pending restore.
     * Returns SendError::Failed or WrongInstance if it was not sent.
     */
    [[nodiscard]] SendResult previous();

    /**
     * Jump to `position_ms` in the current track, cancelling a pending restore.
     * Returns SendError::Failed or WrongInstance if it was not sent.
     */
    [[nodiscard]] SendResult seek(std::uint32_t position_ms);

    /** Set the volume. Returns SendError::Failed or WrongInstance if it was not sent. */
    [[nodiscard]] SendResult set_volume(player::VolumeLevel level);

    /**
     * Set how the player moves through its queue.
     * Returns SendError::Failed or WrongInstance if it was not sent.
     */
    [[nodiscard]] SendResult set_play_mode(player::PlayMode mode);

    /**
     * Mark or unmark the current track as a favourite.
     * Returns SendError::Failed or WrongInstance if it was not sent.
     */
    [[nodiscard]] SendResult set_favourite(bool favourite);

    // Restoring playback around an output change

    /**
     * Get what is playing now, or nullopt if there is no track. While a restore of this track is
     * pending, its position is the restore's: an output change during a restore keeps it.
     */
    [[nodiscard]] std::optional<PlaybackSnapshot> snapshot() const;

    /**
     * Pause, if playing or loading; send nothing otherwise.
     * Returns SendError::Failed or WrongInstance if it was not sent.
     */
    [[nodiscard]] SendResult pause(Clock::time_point now);

    /** Note that the player re-initialised its output (PlayerState::note_output_reset()). */
    void note_output_reset(Clock::time_point now);

    /**
     * Restore `snapshot` after an output change, replacing a pending restore. With
     * Resume::AsBefore it plays the track again at once if it was playing; otherwise it waits for
     * the next play. Either way, once the player reports the track Playing, advance() seeks back
     * to the position, unless it is shorter than kRestoreTolerance.
     *
     * A seek before the play does not work: the player ignores it and starts from the beginning
     * (capture of 2026-11-07). Audio starts ~2 s after the Playing report, so a seek at the report
     * can land before anything is heard.
     *
     * A next(), previous() or seek(), another track, or a restart cancels the restore. A
     * toggle() does not: it is the play a restore that does not play at once waits for.
     * Returns, for a play at once:
     *  - SendError::WrongInstance if the player has restarted
     *  - SendError::Failed if it was not sent; the restore still waits for the next play
     */
    [[nodiscard]] SendResult restore(
            const PlaybackSnapshot& snapshot, Resume resume, Clock::time_point now
    );

    /**
     * Advance a pending restore to `now`: once the track plays, seek to its position, and finish
     * when a position reported after the seek is within kRestoreTolerance of it. A position that
     * is not gets one more seek; after kMaxRestoreSeeks it gives up. Call it regularly.
     */
    void advance(Clock::time_point now);

private:
    /** A restore waiting for the track to play, or for its seek to land. */
    struct PendingRestore {
        Track track{};
        std::uint32_t position_ms = 0;
        std::optional<player::PlayerInstance> instance{};
        int seek_attempts = 0;
        std::optional<Clock::time_point> last_seek_time{};  // When the last seek was sent
    };

    /** Get the player instance the state is about. */
    [[nodiscard]] std::optional<player::PlayerInstance> state_instance() const;

    /** Send a toggle to `instance`, and expect a resume if it plays a paused or stopped track. */
    SendResult send_toggle(std::optional<player::PlayerInstance> instance, Clock::time_point now);

    /**
     * Send `pending`'s seek, at `now`. Returns false if the restore should end: its player is gone,
     * or it has used its seeks.
     */
    [[nodiscard]] bool send_restore_seek(PendingRestore& pending, Clock::time_point now);

    player::CommandSender* sender_;
    utils::Synchronized<PlayerState>* state_;
    std::optional<PendingRestore> restore_{};
};

}  // namespace diskos::playback
