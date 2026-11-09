// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <variant>

#include "player/player_instance.hpp"
#include "player/report_listener.hpp"
#include "utils/synchronized.hpp"

namespace diskos::player {

/**
 * Carries what the player sends from the thread that receives it to the one that acts on it (the
 * main loop), so the state it updates there needs no lock. PlayerLink holds one.
 *
 * The receiving thread adds each connection and a copy of each frame. deliver() hands them on in
 * order, decoding the frames there: a decoded report can hold views into its frame, which the
 * copy keeps alive until the listener returns.
 *
 * It holds at most `capacity` frames: when full, the oldest frame goes, never a connection.
 */
class PlayerInbox {
public:
    // Frames come about once a second; this covers a main loop that stalls for minutes.
    static constexpr std::size_t kCapacity = 256;

    explicit PlayerInbox(std::size_t capacity = kCapacity) : capacity_(capacity) {}

    /** Add that the player `instance` connected. Any thread. */
    void add_connection(PlayerInstance instance);

    /** Add a copy of `frame`, raw: it may be malformed. Any thread. */
    void add_frame(std::string_view frame);

    /**
     * Hand everything added so far to `listener`, oldest first, at `now`. A frame that is not a
     * report is skipped. Call it from one thread.
     * Returns how many frames were dropped since the last delivery, as the inbox was full.
     */
    std::size_t deliver(ReportListener& listener, ReportListener::Clock::time_point now);

private:
    struct Connected {
        PlayerInstance instance;
    };

    using Event = std::variant<Connected, std::string>;

    struct Queue {
        std::deque<Event> events{};
        std::size_t frames = 0;   // The events that are frames
        std::size_t dropped = 0;  // Frames dropped since the last delivery
    };

    std::size_t capacity_;
    utils::Synchronized<Queue> queue_{};
};

}  // namespace diskos::player
