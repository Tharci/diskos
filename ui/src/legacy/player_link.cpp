// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "legacy/player_link.h"

#include <atomic>
#include <optional>
#include <string_view>

#include "platform/player_link.hpp"
#include "player/frame_listener.hpp"
#include "player/player_instance.hpp"

namespace diskos::legacy {
namespace {

/** Hands the link's calls to the legacy C functions. */
class CallbackListener final : public player::FrameListener {
public:
    explicit CallbackListener(const diskos_link_listener& callbacks) : callbacks_(callbacks) {}

    void on_connected(player::PlayerInstance instance) override {
        callbacks_.on_connected(instance.number());
    }

    void on_frame(std::string_view frame) override {
        callbacks_.on_frame(frame.data(), frame.size());
    }

    void on_drained() override {
        callbacks_.on_drained();
    }

private:
    diskos_link_listener callbacks_;
};

struct Link {
    CallbackListener listener;
    platform::PlayerLink link{listener};
};

/** The started link, or nullptr before diskos_link_start(). */
std::atomic<Link*>& current_link() {
    static std::atomic<Link*> link = nullptr;
    return link;
}

}  // namespace
}  // namespace diskos::legacy

extern "C" int diskos_link_start(diskos_link_listener listener) {
    using diskos::legacy::Link;
    static std::atomic<bool> started = false;
    if (started.exchange(true)) {
        return -1;
    }
    // Never destroyed: the receive thread calls into the legacy C until the process ends, past
    // the destruction of other statics.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): deliberately kept for the whole run
    auto* const link = new Link{.listener = diskos::legacy::CallbackListener{listener}};
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
