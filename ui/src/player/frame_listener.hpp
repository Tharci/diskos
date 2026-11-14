// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <string_view>

#include "player/player_instance.hpp"

namespace diskos::player {

/**
 * Hears what the stock player sends, undecoded, on the thread that asks for it
 * (PlayerLink::deliver(), the main loop): each new player instance, each frame it put on /ui (see
 * frame.hpp), and each drain that finished. The frame's bytes stay valid only during on_frame().
 *
 * Services hear decoded reports instead, through a ReportDecoder. Only the legacy IPC reads raw
 * frames: it decodes them itself, and counts the malformed ones.
 */
class FrameListener {
public:
    using Clock = std::chrono::steady_clock;

    FrameListener() = default;
    FrameListener(const FrameListener&) = delete;
    FrameListener(FrameListener&&) = delete;
    FrameListener& operator=(const FrameListener&) = delete;
    FrameListener& operator=(FrameListener&&) = delete;
    virtual ~FrameListener() = default;

    /** A new player instance connected. Comes before any of its frames, also for the first. */
    virtual void on_connected(PlayerInstance instance, Clock::time_point now) = 0;

    /** One frame from the current instance, raw: it may be malformed. */
    virtual void on_frame(std::string_view frame, Clock::time_point now) = 0;

    /**
     * A requested drain finished: every frame the player sent before the request came before
     * this call (see PlayerLink::request_drain()).
     */
    virtual void on_drained(Clock::time_point now) = 0;
};

}  // namespace diskos::player
