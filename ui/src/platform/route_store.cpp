// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#include "platform/route_store.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "services/output/bluetooth_address.hpp"
#include "services/output/output.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace diskos::platform {
namespace {

using output::Output;
using output::StoredRoute;

constexpr mode_t kDirectoryMode = 0700;
constexpr mode_t kFileMode = 0600;
constexpr mode_t kPermissions = 07777;
constexpr std::size_t kMaxSize = 128;  // The longest line is about 90 bytes
constexpr std::string_view kApplied = "applied=";
constexpr std::string_view kSettled = " settled=";
constexpr std::string_view kBluetooth = "bluetooth-";

/** A file descriptor, closed when the object goes. */
class Descriptor {
public:
    explicit Descriptor(int descriptor) noexcept : descriptor_(descriptor) {}

    Descriptor(const Descriptor&) = delete;
    Descriptor(Descriptor&&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor& operator=(Descriptor&&) = delete;

    ~Descriptor() {
        if (descriptor_ >= 0) {
            close(descriptor_);
        }
    }

    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }

    /** Close it now. Returns false if that failed: what was written may be lost. */
    [[nodiscard]] bool close_now() noexcept {
        return close(std::exchange(descriptor_, -1)) == 0;
    }

private:
    int descriptor_;
};

std::string text_of(const std::optional<Output>& applied) {
    if (!applied) {
        return "unknown";
    }
    if (const auto* bluetooth = std::get_if<output::Bluetooth>(&*applied)) {
        return std::string{kBluetooth} + std::string{bluetooth->device.value()};
    }
    if (std::holds_alternative<output::Spdif>(*applied)) {
        return "spdif";
    }
    return std::holds_alternative<output::UsbAudio>(*applied) ? "usb-audio" : "internal-dac";
}

/** Read `text` as text_of() writes a known output, or nullopt for anything else. */
std::optional<Output> output_from(std::string_view text) {
    const std::array<Output, 3> wired{output::InternalDac{}, output::Spdif{}, output::UsbAudio{}};
    const auto* const known = std::ranges::find_if(wired, [text](const Output& output) {
        return text_of(output) == text;
    });
    if (known != wired.end()) {
        return *known;
    }
    if (!text.starts_with(kBluetooth)) {
        return std::nullopt;
    }
    const auto device = output::BluetoothAddress::from(text.substr(kBluetooth.size()));
    return device ? std::optional<Output>{output::Bluetooth{.device = *device}} : std::nullopt;
}

/** The start of the line about the player whose reports queue is `player`. */
std::string prefix_for(QueueIdentity player) {
    return "player=" + std::to_string(player.device) + ":" + std::to_string(player.inode) + " ";
}

/** The rest of the line: the route. */
std::string text_of(const StoredRoute& route) {
    return std::string{kApplied} + text_of(route.applied) + std::string{kSettled}
           + (route.settled ? "1" : "0") + "\n";
}

/** Read `line` as the route of `player`: nullopt for anything but exactly what the store writes. */
std::optional<StoredRoute> route_in(std::string_view line, QueueIdentity player) {
    const std::string prefix = prefix_for(player);
    if (!line.starts_with(prefix)) {
        return std::nullopt;  // Another player's, or malformed
    }
    const std::string_view text = line.substr(prefix.size());
    const std::size_t settled = text.find(kSettled);
    if (!text.starts_with(kApplied) || settled == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view applied = text.substr(kApplied.size(), settled - kApplied.size());
    StoredRoute route{.settled = text.substr(settled + kSettled.size()) == "1\n"};
    if (applied != "unknown") {
        route.applied = output_from(applied);
        if (!route.applied) {
            return std::nullopt;
        }
    }
    // Read leniently, then kept only if it is written back the same: anything else (a lowercase
    // address, settled=yes, an extra field) was not written by the store.
    return text_of(route) == text ? std::optional{route} : std::nullopt;
}

}  // namespace

FileRouteStore::FileRouteStore(RouteStoreLocation location)
    : location_(std::move(location)),
      path_(location_.directory + "/" + location_.file),
      owner_(geteuid()) {}

std::optional<output::StoredRoute> FileRouteStore::load() {
    if (!directory_safe()) {
        return std::nullopt;
    }
    const std::optional<std::string> line = read();
    if (!line) {
        return std::nullopt;
    }
    const std::optional<QueueIdentity> player = running_player();
    std::optional<StoredRoute> route = player ? route_in(*line, *player) : std::nullopt;
    if (!route) {
        remove();  // Malformed, or about a player that is gone
    }
    return route;
}

void FileRouteStore::save(const output::StoredRoute& route) {
    const std::optional<QueueIdentity> player = running_player();
    if (!player || !make_directory() || !write(prefix_for(*player) + text_of(route))) {
        remove();
    }
}

bool FileRouteStore::make_directory() const {
    if (mkdir(location_.directory.c_str(), kDirectoryMode) != 0 && errno != EEXIST) {
        return false;
    }
    return directory_safe();
}

bool FileRouteStore::directory_safe() const {
    struct stat status{};
    return lstat(location_.directory.c_str(), &status) == 0 && S_ISDIR(status.st_mode)
           && status.st_uid == owner_ && (status.st_mode & kPermissions) == kDirectoryMode;
}

std::optional<std::string> FileRouteStore::read() const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open is the POSIX interface
    const Descriptor file{open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (file.get() < 0) {
        if (errno != ENOENT) {
            remove();  // Not a file it can read: a symlink or worse
        }
        return std::nullopt;
    }
    struct stat status{};
    const bool trusted = fstat(file.get(), &status) == 0 && S_ISREG(status.st_mode)
                         && status.st_uid == owner_ && (status.st_mode & kPermissions) == kFileMode
                         && status.st_size > 0
                         && static_cast<std::size_t>(status.st_size) < kMaxSize;
    std::array<char, kMaxSize> buffer{};
    const ssize_t size = trusted ? ::read(file.get(), buffer.data(), buffer.size()) : -1;
    if (size != status.st_size) {
        remove();
        return std::nullopt;
    }
    return std::string{buffer.data(), static_cast<std::size_t>(size)};
}

bool FileRouteStore::write(const std::string& line) const {
    std::string temporary = path_ + ".XXXXXX";
    Descriptor file{mkstemp(temporary.data())};  // Created with mode 0600
    if (file.get() < 0) {
        return false;
    }
    // One small write to a new file in tmpfs: a short one is a failure, which costs only a wait
    // for the player to settle at the next UI start.
    const bool written =
            ::write(file.get(), line.data(), line.size()) == static_cast<ssize_t>(line.size());
    const bool closed = file.close_now();
    if (!written || !closed || rename(temporary.c_str(), path_.c_str()) != 0) {
        unlink(temporary.c_str());
        return false;
    }
    return true;
}

std::optional<QueueIdentity> FileRouteStore::running_player() const {
    const auto queue = MessageReceiver::open(location_.reports_queue);
    return queue ? queue->identity() : std::nullopt;
}

void FileRouteStore::remove() const {
    if (directory_safe()) {
        unlink(path_.c_str());
    }
}

}  // namespace diskos::platform
