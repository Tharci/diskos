// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_link.h"

#include <atomic>
#include <optional>
#include <string_view>

#include "legacy/player_link.hpp"
#include "platform/player_link.hpp"
#include "player/frame_listener.hpp"
#include "player/player_instance.hpp"
#include "player/report_decoder.hpp"

namespace diskos::legacy {
namespace {

using player::FrameListener;

/** Hands each event to the legacy C first, then to the services, decoded. */
class Fanout final : public FrameListener {
public:
    Fanout(const diskos_link_listener& callbacks, player::ReportListener& services)
        : callbacks_(&callbacks), services_(services) {}

    void on_connected(player::PlayerInstance instance, Clock::time_point now) override {
        callbacks_->on_connected(instance.number());
        services_.on_connected(instance, now);
    }

    void on_frame(std::string_view frame, Clock::time_point now) override {
        callbacks_->on_frame(frame.data(), frame.size());
        services_.on_frame(frame, now);
    }

    void on_drained(Clock::time_point now) override {
        callbacks_->on_drained();
        services_.on_drained(now);
    }

private:
    const diskos_link_listener* callbacks_;
    player::ReportDecoder services_;
};

struct Link {
    diskos_link_listener callbacks;
    platform::PlayerLink link{};
};

/** The started link, or nullptr before diskos_link_start(). */
std::atomic<Link*>& current_link() {
    static std::atomic<Link*> link = nullptr;
    return link;
}

}  // namespace

std::size_t deliver_player(
        player::ReportListener& services, player::ReportListener::Clock::time_point now
) {
    Link* const started = current_link().load();
    if (started == nullptr) {
        return 0;
    }
    Fanout fanout{started->callbacks, services};
    return started->link.deliver(fanout, now);
}

}  // namespace diskos::legacy

extern "C" int diskos_link_start(diskos_link_listener listener) {
    using diskos::legacy::Link;
    static std::atomic<bool> started = false;
    if (started.exchange(true)) {
        return -1;
    }
    // Never destroyed: senders on any thread may use it until the process ends, past the
    // destruction of other statics.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): deliberately kept for the whole run
    auto* const link = new Link{.callbacks = listener};
    diskos::legacy::current_link().store(link);
    return 0;
}

extern "C" diskos_link_result diskos_link_send(const char* frame, unsigned instance) {
    diskos::legacy::Link* const link = diskos::legacy::current_link().load();
    if (link == nullptr) {
        return DISKOS_LINK_FAILED;
    }
    const auto to =
            instance != 0 ? std::optional{diskos::player::PlayerInstance{instance}} : std::nullopt;
    const auto sent = link->link.send_frame(frame, to);
    if (sent) {
        return DISKOS_LINK_SENT;
    }
    return sent.error() == diskos::player::SendError::WrongInstance ? DISKOS_LINK_WRONG_INSTANCE
                                                                    : DISKOS_LINK_FAILED;
}

extern "C" void diskos_link_request_drain(void) {
    diskos::legacy::Link* const link = diskos::legacy::current_link().load();
    if (link != nullptr) {
        link->link.request_drain();
    }
}
