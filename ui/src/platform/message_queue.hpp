// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <mqueue.h>

/**
 * POSIX message queues, one type per direction: the kernel fixes a descriptor's direction when
 * it is opened, and so do these types.
 */
namespace diskos::platform {

/** Which queue object a descriptor or name refers to: a recreated queue has a new identity. */
struct QueueIdentity {
    std::uint64_t device = 0;
    std::uint64_t inode = 0;

    friend bool operator==(const QueueIdentity&, const QueueIdentity&) = default;
};

struct QueueCapacity {
    long max_messages = 0;
    long message_size = 0;
};

/** An open queue descriptor, closed when the object goes. */
class QueueDescriptor {
public:
    explicit QueueDescriptor(mqd_t descriptor) noexcept : descriptor_(descriptor) {}

    QueueDescriptor(const QueueDescriptor&) = delete;
    QueueDescriptor& operator=(const QueueDescriptor&) = delete;
    QueueDescriptor(QueueDescriptor&& other) noexcept;
    QueueDescriptor& operator=(QueueDescriptor&& other) noexcept;
    ~QueueDescriptor();

    [[nodiscard]] mqd_t get() const noexcept {
        return descriptor_;
    }

    /** Get the identity of the queue it refers to, or nullopt if fstat() fails. */
    [[nodiscard]] std::optional<QueueIdentity> identity() const;

    /** Forget a descriptor that is no longer valid without closing it: its number may be reused. */
    void abandon() noexcept;

private:
    mqd_t descriptor_;
};

/** The receiving end of a queue: it blocks, up to a timeout, until a message arrives. */
class MessageReceiver {
public:
    enum class Error : std::uint8_t {
        NotFound,     // No queue has that name
        Empty,        // Nothing arrived in time
        Interrupted,  // A signal interrupted the wait
        Closed,       // The descriptor is no longer valid: abandon() it
        Failed,       // Any other error
    };

    /** Open the existing queue `name` (such as "/ui"). Returns NotFound or Failed. */
    [[nodiscard]] static std::expected<MessageReceiver, Error> open(const std::string& name);

    /** Open the queue `name`, creating it with `capacity` if absent. Returns Failed. */
    [[nodiscard]] static std::expected<MessageReceiver, Error> open_or_create(
            const std::string& name, QueueCapacity capacity
    );

    /**
     * Receive one message into `buffer`, waiting up to `timeout`, and get its size. Returns Empty,
     * Interrupted, Closed or Failed (also for a buffer smaller than message_size()).
     */
    [[nodiscard]] std::expected<std::size_t, Error> receive(
            std::span<char> buffer, std::chrono::milliseconds timeout
    );

    /** Get the size of the largest message the queue holds, or nullopt if it cannot be read. */
    [[nodiscard]] std::optional<std::size_t> message_size() const;

    [[nodiscard]] std::optional<QueueIdentity> identity() const {
        return descriptor_.identity();
    }

    /** Forget a Closed descriptor without closing it: its number may already be reused. */
    void abandon() noexcept {
        descriptor_.abandon();
    }

private:
    explicit MessageReceiver(QueueDescriptor descriptor) noexcept
        : descriptor_(std::move(descriptor)) {}

    QueueDescriptor descriptor_;
};

/** The sending end of a queue: it never blocks, a full queue fails at once. */
class MessageSender {
public:
    enum class Error : std::uint8_t {
        NotFound,  // No queue has that name
        Full,      // The queue is full
        Failed,    // Any other error
    };

    /** Open the existing queue `name` (such as "/player"). Returns NotFound or Failed. */
    [[nodiscard]] static std::expected<MessageSender, Error> open(const std::string& name);

    /** Send `message`. Returns Full or Failed. */
    [[nodiscard]] std::expected<void, Error> send(std::string_view message);

    [[nodiscard]] std::optional<QueueIdentity> identity() const {
        return descriptor_.identity();
    }

private:
    explicit MessageSender(QueueDescriptor descriptor) noexcept
        : descriptor_(std::move(descriptor)) {}

    QueueDescriptor descriptor_;
};

}  // namespace diskos::platform
