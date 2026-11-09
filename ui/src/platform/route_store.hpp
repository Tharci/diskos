// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <optional>
#include <string>

#include "platform/message_queue.hpp"
#include "services/output/interfaces.hpp"
#include <sys/types.h>

namespace diskos::platform {

/** Where a FileRouteStore keeps its file, and which queue tells the running player apart. */
struct RouteStoreLocation {
    std::string directory = "/tmp/.diskos-ui";  // tmpfs: gone at a reboot, like the player
    std::string file = "output-route";
    std::string reports_queue = "/ui";  // Recreated by each new player: its identity names one
};

/**
 * Keeps the output router's StoredRoute in a one-line file, for the player that is running: the
 * file names the player by its reports queue's identity, which only a new player changes (see
 * PlayerLink). A file about another player is dropped.
 *
 * It trusts nothing it did not write: the directory must be its user's with mode 0700, the file a
 * regular one of its user's with mode 0600, and its line well formed; otherwise it is ignored and
 * removed (never through a directory that is not its own). A save writes a temporary file and
 * renames it, so a reader never sees half a file.
 */
class FileRouteStore final : public output::RouteStore {
public:
    explicit FileRouteStore(RouteStoreLocation location = {});

    /** Get the route saved for the running player, or nullopt if there is none or it is not. */
    [[nodiscard]] std::optional<output::StoredRoute> load() override;

    /** Save `route` for the running player; if that fails, remove what was there. */
    void save(const output::StoredRoute& route) override;

private:
    /** Create the directory if it is missing. Returns whether it is then safe to use. */
    [[nodiscard]] bool make_directory() const;
    /** Check that the directory is its user's alone, and not a link. */
    [[nodiscard]] bool directory_safe() const;
    /** Read the file, if it is safe to trust. */
    [[nodiscard]] std::optional<std::string> read() const;
    [[nodiscard]] bool write(const std::string& line) const;
    /** Get the identity of the running player's reports queue, or nullopt if there is none. */
    [[nodiscard]] std::optional<QueueIdentity> running_player() const;
    void remove() const;

    RouteStoreLocation location_;
    std::string path_;
    uid_t owner_;  // The user it runs as: the only one whose files it trusts
};

}  // namespace diskos::platform
