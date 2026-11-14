// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "player/player_inbox.hpp"

#include <algorithm>
#include <utility>

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

void PlayerInbox::add_drained() {
    queue_.with([](Queue& queue) { queue.events.emplace_back(Drained{}); });
}

std::size_t PlayerInbox::deliver(FrameListener& listener, FrameListener::Clock::time_point now) {
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
            listener.on_frame(*frame, now);
        } else {
            listener.on_drained(now);
        }
    }
    return dropped;
}

}  // namespace diskos::player
