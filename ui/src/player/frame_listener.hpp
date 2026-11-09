// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <string_view>

#include "player/player_instance.hpp"

namespace diskos::player {

/**
 * Hears what the stock player sends: each frame it puts on /ui (see frame.hpp), and when a new
 * player instance connects. The calls come from one thread, in order; the frame's bytes stay
 * valid only during on_frame().
 *
 * Only the legacy IPC hears frames, on the receiving thread: services hear decoded reports
 * through PlayerLink::deliver() and a ReportListener.
 * TODO: Remove once the legacy IPC no longer reads frames.
 */
class FrameListener {
public:
    FrameListener() = default;
    FrameListener(const FrameListener&) = delete;
    FrameListener(FrameListener&&) = delete;
    FrameListener& operator=(const FrameListener&) = delete;
    FrameListener& operator=(FrameListener&&) = delete;
    virtual ~FrameListener() = default;

    /** A new player instance connected. Comes before any of its frames, also for the first. */
    virtual void on_connected(PlayerInstance instance) = 0;

    /** One frame from the current instance, raw: it may be malformed. */
    virtual void on_frame(std::string_view frame) = 0;

    /**
     * A requested drain finished: every frame the player sent before the request has reached
     * on_frame() (see PlayerLink::request_drain()).
     */
    virtual void on_drained() = 0;
};

}  // namespace diskos::player
