// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 diskOS contributors
#pragma once

#include <string>

#include "media_metadata.hpp"

namespace diskos::services::media {

enum class MediaType : std::uint8_t {
    Music,
    Book,
}

/**
 * Represents a music or book with all their relevant attributes.
 */
struct MediaItem {
    std::string id;
    std::string title;
    // Path to the media on the device.
    std::string path;
    int duration_ms;  // length in seconds
    MediaType type;
    MediaMetadata metadata;
};

struct MusicItem : MediaItem {};

// TODO: Extend BookItem to support handling multiple files as one book
//  based on some heuristics or metadata.
struct BookItem : MediaItem {};

}  // namespace diskos::services::media
