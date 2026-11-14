// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "platform/message_queue.hpp"

#include <cerrno>
#include <ctime>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>

namespace diskos::platform {
namespace {

constexpr mqd_t kNoDescriptor = -1;

/** The CLOCK_REALTIME time `timeout` from now, as mq_timedreceive() takes it. */
timespec deadline_after(std::chrono::milliseconds timeout) {
    // In timespec's own types: time_t and long differ in width between the host and the device.
    using Seconds = std::chrono::duration<std::time_t>;
    using Nanoseconds = std::chrono::duration<long, std::nano>;
    const auto deadline = std::chrono::system_clock::now() + timeout;
    const auto seconds = std::chrono::floor<Seconds>(deadline);
    return {
            .tv_sec = seconds.time_since_epoch().count(),
            .tv_nsec = std::chrono::duration_cast<Nanoseconds>(deadline - seconds).count(),
    };
}

/** Open the queue `name` with `flags`, or get errno. */
std::expected<QueueDescriptor, int> open_queue(const std::string& name, int flags) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): mq_open is the POSIX interface
    const mqd_t descriptor = mq_open(name.c_str(), flags);
    if (descriptor == kNoDescriptor) {
        return std::unexpected(errno);
    }
    return QueueDescriptor{descriptor};
}

}  // namespace

QueueDescriptor::QueueDescriptor(QueueDescriptor&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, kNoDescriptor)) {}

QueueDescriptor& QueueDescriptor::operator=(QueueDescriptor&& other) noexcept {
    if (this != &other) {
        if (descriptor_ != kNoDescriptor) {
            mq_close(descriptor_);
        }
        descriptor_ = std::exchange(other.descriptor_, kNoDescriptor);
    }
    return *this;
}

QueueDescriptor::~QueueDescriptor() {
    if (descriptor_ != kNoDescriptor) {
        mq_close(descriptor_);
    }
}

std::optional<QueueIdentity> QueueDescriptor::identity() const {
    struct stat status{};
    if (fstat(descriptor_, &status) != 0) {
        return std::nullopt;
    }
    return QueueIdentity{.device = status.st_dev, .inode = status.st_ino};
}

void QueueDescriptor::abandon() noexcept {
    descriptor_ = kNoDescriptor;
}

std::expected<MessageReceiver, MessageReceiver::Error> MessageReceiver::open(
        const std::string& name
) {
    auto descriptor = open_queue(name, O_RDONLY);
    if (!descriptor) {
        return std::unexpected(descriptor.error() == ENOENT ? Error::NotFound : Error::Failed);
    }
    return MessageReceiver{std::move(*descriptor)};
}

std::expected<MessageReceiver, MessageReceiver::Error> MessageReceiver::open_or_create(
        const std::string& name, QueueCapacity capacity
) {
    constexpr mode_t kPermissions = 0666;  // The player opens it too
    mq_attr attributes{};
    attributes.mq_maxmsg = capacity.max_messages;
    attributes.mq_msgsize = capacity.message_size;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): mq_open is the POSIX interface
    const mqd_t descriptor = mq_open(name.c_str(), O_RDONLY | O_CREAT, kPermissions, &attributes);
    if (descriptor == kNoDescriptor) {
        return std::unexpected(Error::Failed);
    }
    return MessageReceiver{QueueDescriptor{descriptor}};
}

std::expected<std::size_t, MessageReceiver::Error> MessageReceiver::receive(
        std::span<char> buffer, std::chrono::milliseconds timeout
) {
    const timespec deadline = deadline_after(timeout);
    const ssize_t size =
            mq_timedreceive(descriptor_.get(), buffer.data(), buffer.size(), nullptr, &deadline);
    if (size >= 0) {
        return static_cast<std::size_t>(size);
    }
    switch (errno) {
        case ETIMEDOUT:
            return std::unexpected(Error::Empty);
        case EINTR:
            return std::unexpected(Error::Interrupted);
        case EBADF:
            return std::unexpected(Error::Closed);
        default:
            return std::unexpected(Error::Failed);
    }
}

std::optional<std::size_t> MessageReceiver::message_size() const {
    mq_attr attributes{};
    if (mq_getattr(descriptor_.get(), &attributes) != 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(attributes.mq_msgsize);
}

std::expected<MessageSender, MessageSender::Error> MessageSender::open(const std::string& name) {
    auto descriptor = open_queue(name, O_WRONLY | O_NONBLOCK);
    if (!descriptor) {
        return std::unexpected(descriptor.error() == ENOENT ? Error::NotFound : Error::Failed);
    }
    return MessageSender{std::move(*descriptor)};
}

std::expected<void, MessageSender::Error> MessageSender::send(std::string_view message) {
    if (mq_send(descriptor_.get(), message.data(), message.size(), 0) == 0) {
        return {};
    }
    return std::unexpected(errno == EAGAIN ? Error::Full : Error::Failed);
}

}  // namespace diskos::platform
