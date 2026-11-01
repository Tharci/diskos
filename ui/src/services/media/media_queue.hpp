// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "media_item.hpp"

namespace diskos::services::media {

enum class PlaybackMode : std::uint8_t {
    Single,
    Sequential,
    Shuffle,
};

/**
 * MediaQueue owns all MusicItem objects.
 *
 * References, spans and ranges returned by this class are
 *  non-owning and may be invalidated by any operation that
 *  modifies the queue.
 *
 * Consumers must not retain references or views across
 *  queue modifications. Check MutationResult value to see whether
 *  the queue has been mutated.
 */
class MediaQueue {
public:
    explicit MediaQueue(std::vector<MusicItem> items) : queue_(std::move(items)) {}

    using MusicItemRef = std::reference_wrapper<const MusicItem>;
    using OptionalMusicItemRef = std::optional<MusicItemRef>;
    using MutationResult = bool;

    /**
     * Step to the next song in the queue.
     * After the last song in the queue:
     *  - Returns nullopt, if loop is false or the queue is empty.
     *  - Steps to the first one otherwise.
     */
    OptionalMusicItemRef next();

    /**
     * Step to the next song in the queue, disregarding loop.
     * After the last song in the queue:
     *  - Returns nullopt, if the queue is empty.
     *  - Steps to the first one otherwise.
     */
    OptionalMusicItemRef next_wrapped();

    /**
     * Step to the previous song in the queue. Returns nullopt only if
     *  the queue is empty.
     */
    OptionalMusicItemRef prev();

    /** Returns the current song, or nullopt if the queue is empty */
    OptionalMusicItemRef current() const;

    /**
     * Delete a music from the queue. Has no effect if the music is not in the queue.
     * Delete semantics:
     *  - Delete all o
     *  - If an item before the current item is deleted, decrement the current index.
     *  - If the current item is deleted, select the item that takes its place.
     *  - If the last item is deleted, select the new last item.
     *  - If the queue becomes empty, reset the index to nullopt.
     */
    MutationResult delete_music(const std::string_view music_id);

    /**
     * Delete a batch of music from the queue. Has no effect for music that are not in the queue.
     * Deletion is applied for each music, and semantics are described in `delete_music`.
     */
    MutationResult delete_music_batch(std::span<const std::string_view> music_ids);

    // TODO: Extend with "add to queue start/end". Keep these additions as
    //  separate throwaway vectors so that the user doesn't return to the
    //  temporary songs.

    // Properties
    std::size_t size() const noexcept;
    bool empty() const noexcept;
    // nullptr if queue is empty
    std::optional<std::size_t> current_idx() const noexcept;
    PlaybackMode playback_mode() const noexcept;
    bool loop() const noexcept;

    // Configuration
    void set_playback_mode(PlaybackMode mode) noexcept;
    void set_loop(bool loop) noexcept;

    // Queue access
    std::ranges::transform_view<const MusicItem> queue_full() const noexcept;
    std::ranges::transform_view<const MusicItem> queue_remaining(
            bool include_current = true
    ) const noexcept;

private:
    std::vector<MusicItem> queue_;             // The original queue, unsorted
    std::vector<std::size_t> playback_order_;  // The sorting of the queue when playing it
    std::optional<std::size_t> current_idx_ = std : nullopt;
    bool loop_ = false;
    PlaybackMode playback_mode_ = PlaybackMode::Sequential;
};

/** Builds different media queues from the database. */
class MediaQueueFactory {
    static MediaQueue from_artist(SqlDatabase& db, std::string_view artist);
    static MediaQueue from_album(SqlDatabase& db, std::string_view artist, std::string_view album);
    static MediaQueue from_music(SqlDatabase& db, std::string_view music_id, bool within_folder);
};

}  // namespace diskos::services::media
