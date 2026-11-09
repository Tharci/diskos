// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/player_inbox.hpp"

#include <algorithm>
#include <utility>

#include "player/reports.hpp"

namespace diskos::player {

void PlayerInbox::add_connection(PlayerInstance instance) {
    queue_.with([instance](Queue& queue) { queue.events.emplace_back(Connected{instance}); });
}

void PlayerInbox::add_frame(std::string_view frame) {
    queue_.with([this, frame](Queue& queue) {
        if (queue.frames == capacity_) {
            const auto oldest = std::ranges::find_if(queue.events, [](const Event& event) {
                return std::holds_alternative<std::string>(event);
            });
            queue.events.erase(oldest);
            --queue.frames;
            ++queue.dropped;
        }
        queue.events.emplace_back(std::string{frame});
        ++queue.frames;
    });
}

std::size_t PlayerInbox::deliver(ReportListener& listener, ReportListener::Clock::time_point now) {
    // Taken out whole, so the receiving thread never waits for the listener.
    auto [events, dropped] = queue_.with([](Queue& queue) {
        auto taken = std::pair{std::exchange(queue.events, {}), std::exchange(queue.dropped, 0)};
        queue.frames = 0;
        return taken;
    });
    for (const Event& event : events) {
        if (const auto* connected = std::get_if<Connected>(&event)) {
            listener.on_connected(connected->instance, now);
        } else if (const auto* frame = std::get_if<std::string>(&event)) {
            if (const Decoded<Report> report = decode_report(*frame)) {
                listener.on_report(*report, now);
            }
        }
    }
    return dropped;
}

}  // namespace diskos::player
