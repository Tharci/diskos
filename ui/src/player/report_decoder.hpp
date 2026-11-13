// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <string_view>

#include "player/frame_listener.hpp"
#include "player/player_instance.hpp"
#include "player/report_listener.hpp"
#include "player/reports.hpp"

namespace diskos::player {

/**
 * Hands what the player sends to a ReportListener, decoded: each connection, and each frame that
 * is a report (decode_report()). Malformed frames and finished drains are dropped.
 * It keeps a pointer to the listener, which must outlive it.
 */
class ReportDecoder final : public FrameListener {
public:
    explicit ReportDecoder(ReportListener& listener) : listener_(&listener) {}

    void on_connected(PlayerInstance instance, Clock::time_point now) override {
        listener_->on_connected(instance, now);
    }

    void on_frame(std::string_view frame, Clock::time_point now) override {
        if (const Decoded<Report> report = decode_report(frame)) {
            listener_->on_report(*report, now);
        }
    }

    void on_drained(Clock::time_point /*now*/) override {}

private:
    ReportListener* listener_;
};

}  // namespace diskos::player
