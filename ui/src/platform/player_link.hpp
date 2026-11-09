// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "platform/message_queue.hpp"
#include "player/frame_listener.hpp"
#include "player/frame_sender.hpp"
#include "player/player_inbox.hpp"
#include "player/player_instance.hpp"
#include "player/report_listener.hpp"
#include "utils/synchronized.hpp"

namespace diskos::platform {

/** The stock player's two queues. */
struct QueueNames {
    std::string reports = "/ui";       // Player to UI
    std::string commands = "/player";  // UI to player
};

/** How often the receive thread wakes, checks and retries. */
struct LinkTiming {
    std::chrono::milliseconds receive_wait{1000};   // The longest wait for a frame
    std::chrono::milliseconds restart_check{5000};  // How often to look for a player restart
    std::chrono::milliseconds retry_wait{50};       // After a failed open or receive
};

/**
 * The connection to the stock player, which moves frames both ways: it receives them on a thread
 * of its own, and sends them from any thread. player::FrameCommandSender sends Commands through
 * it. What it receives reaches:
 *  - services, decoded, on the thread that calls deliver() (the main loop): it queues what it
 *    receives until then, in a player::PlayerInbox;
 *  - the legacy IPC, as raw frames, on the receiving thread (player::FrameListener).
 *
 * When the player restarts, it creates its queues anew, and a descriptor on an old one keeps
 * working without ever receiving again. So the receive thread compares the queues' identities
 * with their names' every LinkTiming::restart_check: a new reports queue is a new player instance,
 * and a new commands queue is reopened on the next send.
 *
 * The listener must outlive the link; destroying the link stops and joins its thread.
 */
class PlayerLink final : public player::FrameSender {
public:
    explicit PlayerLink(
            player::FrameListener& listener, QueueNames names = {}, LinkTiming timing = {}
    );

    PlayerLink(const PlayerLink&) = delete;
    PlayerLink(PlayerLink&&) = delete;
    PlayerLink& operator=(const PlayerLink&) = delete;
    PlayerLink& operator=(PlayerLink&&) = delete;
    ~PlayerLink() override = default;

    /**
     * Send `frame`; with `instance`, only if that player instance is still the connected one. A
     * full commands queue is retried for a few milliseconds.
     * Returns:
     *  - SendError::WrongInstance if `instance` is given and is not the connected one
     *  - SendError::Failed if it was not queued
     */
    [[nodiscard]] std::expected<void, player::SendError> send_frame(
            std::string_view frame, std::optional<player::PlayerInstance> instance = std::nullopt
    ) override;

    /**
     * Ask the receive thread to deliver every frame queued now, then call on_drained(): once it is
     * called, every frame the player sent before this request has reached on_frame(). A player
     * restart before then cancels it.
     */
    void request_drain() noexcept;

    /**
     * Hand everything received since the last call to `listener`, oldest first and decoded, at
     * `now`: each new player instance, then its reports. Call it from one thread, regularly.
     * Returns how many frames were dropped since the last call, as too many were waiting.
     */
    std::size_t deliver(
            player::ReportListener& listener, player::ReportListener::Clock::time_point now
    ) {
        return inbox_.deliver(listener, now);
    }

    /** Get the connected player instance, or nullopt before the first connects. */
    [[nodiscard]] std::optional<player::PlayerInstance> instance();

private:
    /** What the receive thread shares with senders. */
    struct Connection {
        std::optional<player::PlayerInstance> instance{};
        std::optional<MessageSender> commands{};  // Opened on the first send
        std::optional<QueueIdentity> commands_identity{};
        bool commands_stale = false;  // The player recreated it: reopen on the next send
    };

    void run(const std::stop_token& stop);
    /** Open the reports queue anew. Returns false if it cannot be opened. */
    [[nodiscard]] bool connect();
    /** Receive one frame and hand it to the listener; after Closed, connect() again. */
    std::expected<std::size_t, MessageReceiver::Error> receive(std::chrono::milliseconds timeout);
    /** Deliver every queued frame, then call on_drained(). */
    void drain();
    void check_restart();

    player::FrameListener* listener_;
    QueueNames names_;
    LinkTiming timing_;

    // The receive thread's own
    std::optional<MessageReceiver> reports_{};
    std::optional<QueueIdentity> reports_identity_{};
    std::vector<char> buffer_{};

    player::PlayerInbox inbox_{};
    utils::Synchronized<Connection> connection_{};
    std::atomic<bool> drain_requested_ = false;
    std::jthread thread_;  // Last: starts once everything else is ready
};

}  // namespace diskos::platform
