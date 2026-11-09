// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "platform/player_link.hpp"

#include <utility>

namespace diskos::platform {
namespace {

using player::PlayerInstance;
using player::SendError;

// The reports queue as the player creates it, for when the UI starts first.
constexpr QueueCapacity kReportsCapacity{.max_messages = 10, .message_size = 8192};
// A full commands queue is retried this often, this long apart, to let the player drain it.
constexpr int kSendAttempts = 5;
constexpr std::chrono::milliseconds kSendRetryWait{3};

/** The identity of the queue `name` names now, or nullopt if it cannot be opened. */
template <typename Queue>
std::optional<QueueIdentity> identity_of(const std::string& name) {
    const auto queue = Queue::open(name);
    return queue ? queue->identity() : std::nullopt;
}

}  // namespace

PlayerLink::PlayerLink(player::FrameListener& listener, QueueNames names, LinkTiming timing)
    : listener_(&listener),
      names_(std::move(names)),
      timing_(timing),
      thread_([this](const std::stop_token& stop) { run(stop); }) {}

std::expected<void, SendError> PlayerLink::send_frame(
        std::string_view frame, std::optional<PlayerInstance> instance
) {
    return connection_.with([&](Connection& connection) -> std::expected<void, SendError> {
        if (instance && instance != connection.instance) {
            return std::unexpected(SendError::WrongInstance);
        }
        if (std::exchange(connection.commands_stale, false)) {
            connection.commands.reset();
        }
        if (!connection.commands) {
            auto opened = MessageSender::open(names_.commands);
            if (!opened) {
                return std::unexpected(SendError::Failed);
            }
            connection.commands_identity = opened->identity();
            connection.commands = std::move(*opened);
        }
        for (int attempt = 0; attempt < kSendAttempts; ++attempt) {
            const auto sent = connection.commands->send(frame);
            if (sent) {
                return {};
            }
            if (sent.error() != MessageSender::Error::Full) {
                break;
            }
            std::this_thread::sleep_for(kSendRetryWait);
        }
        // Reopen it next time: a queue that stays full can be one a restarted player left behind.
        connection.commands.reset();
        return std::unexpected(SendError::Failed);
    });
}

void PlayerLink::request_drain() noexcept {
    drain_requested_.store(true);
}

std::optional<PlayerInstance> PlayerLink::instance() {
    return connection_.with([](const Connection& connection) { return connection.instance; });
}

void PlayerLink::run(const std::stop_token& stop) {
    using Clock = std::chrono::steady_clock;
    auto next_check = Clock::now() + timing_.restart_check;
    while (!stop.stop_requested()) {
        if (!reports_ && !connect()) {
            std::this_thread::sleep_for(timing_.retry_wait);
            continue;
        }
        if (drain_requested_.exchange(false)) {
            drain();
        }
        if (Clock::now() >= next_check) {
            check_restart();
            next_check = Clock::now() + timing_.restart_check;
        }
        (void)receive(timing_.receive_wait);
    }
}

bool PlayerLink::connect() {
    // The first time, the UI may start before the player: create the queue for it.
    auto opened = reports_identity_
                          ? MessageReceiver::open(names_.reports)
                          : MessageReceiver::open_or_create(names_.reports, kReportsCapacity);
    if (!opened) {
        return false;
    }
    const auto size = opened->message_size();
    const auto identity = opened->identity();
    if (!size || !identity) {
        return false;
    }
    buffer_.resize(*size);
    reports_ = std::move(*opened);  // Opened before the old one closes: no gap
    if (*identity == reports_identity_) {
        return true;  // The same queue, so the same player
    }
    reports_identity_ = *identity;
    const PlayerInstance connected = connection_.with([](Connection& connection) {
        connection.instance = connection.instance ? connection.instance->next() : PlayerInstance{1};
        connection.commands_stale = true;
        return *connection.instance;
    });
    drain_requested_.store(false);  // A drain was about the old player
    inbox_.add_connection(connected);
    listener_->on_connected(connected);
    return true;
}

std::expected<std::size_t, MessageReceiver::Error> PlayerLink::receive(
        std::chrono::milliseconds timeout
) {
    using Error = MessageReceiver::Error;
    if (!reports_) {
        return std::unexpected(Error::Closed);
    }
    const auto received = reports_->receive(buffer_, timeout);
    if (received) {
        const std::string_view frame{buffer_.data(), *received};
        inbox_.add_frame(frame);
        listener_->on_frame(frame);
        return received;
    }
    if (received.error() == Error::Closed) {
        reports_->abandon();  // Its number may already belong to another descriptor
        reports_.reset();
    } else if (received.error() == Error::Failed) {
        std::this_thread::sleep_for(timing_.retry_wait);
    }
    return received;
}

void PlayerLink::drain() {
    using Error = MessageReceiver::Error;
    for (;;) {
        const auto received = receive(std::chrono::milliseconds{0});
        if (!received && received.error() == Error::Empty) {
            listener_->on_drained();
            return;
        }
        if (!received && received.error() != Error::Interrupted) {
            return;
        }
    }
}

void PlayerLink::check_restart() {
    const auto reports = identity_of<MessageReceiver>(names_.reports);
    if (reports && reports != reports_identity_) {
        (void)connect();  // On failure, the next check tries again
    }
    const auto commands = identity_of<MessageSender>(names_.commands);
    connection_.with([&commands](Connection& connection) {
        if (connection.commands && commands && commands != connection.commands_identity) {
            connection.commands_stale = true;
        }
    });
}

}  // namespace diskos::platform
