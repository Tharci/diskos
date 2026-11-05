/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The playback state (services/playback/player_state.hpp) for the legacy C code. It holds two
 * PlayerStates, as ipc.c keeps local playback and the AirPlay receiver apart: it applies the a103
 * and a202 frames to the one they belong to and copies its track into track_state_t or
 * ipc_receiver_meta. */
#ifndef DISKOS_LEGACY_PLAYER_STATE_H
#define DISKOS_LEGACY_PLAYER_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest title or path that fits the legacy buffers (track_state_t), without their NUL. */
#define DISKOS_PLAYER_TEXT_MAX 255

/* Which PlayerState a call is about. Each is used under one lock that the caller holds for every
 * call about it (ipc.c: g_mu for LOCAL, g_recov_mu for RECEIVER). */
typedef enum {
    DISKOS_PLAYER_LOCAL,
    DISKOS_PLAYER_RECEIVER,
} diskos_player_slot;

/* What one frame did. */
typedef struct {
    int accepted;       /* 1: an a103 or a202, applied whole */
    int track;          /* 1: a different track, or none any more */
    int details;        /* 1: the same track's other fields changed */
    int position;       /* 1: the position changed */
    int reported_state; /* a202: the state it reported (0 playing, 1 paused, 2 stopped); else -1 */
    int full;           /* 1: a202 in its full form, with a track */
} diskos_player_update;

/* The current track. The texts stay valid until the next diskos_player_apply() or reset. */
typedef struct {
    const char *title, *artist, *album, *path;
    int64_t song_id, pos_id; /* SONG.ID and the queue row (LIST_SONG_0.ID) */
    int64_t duration_ms, sample_rate_hz;
    int favourite, is_dsd, is_cue, is_sacd;
    int64_t queue_position, queue_count; /* "3/12" is 3 and 12; both 0 when not reported */
} diskos_player_track;

/* Apply one frame received on /ui to `slot`: `size` bytes at `bytes`.
 *  - A malformed frame, or any report but a103 and a202, changes nothing and is not accepted.
 *  - LOCAL: a track whose title or path does not fit DISKOS_PLAYER_TEXT_MAX (or holds a NUL)
 *    clears the track and is not accepted: a cut path can name another file, and a cut title is
 *    no evidence. The receiver's texts have no limit: they are only shown. */
diskos_player_update diskos_player_apply(diskos_player_slot slot, const char* bytes, size_t size);

/* Get the current track of `slot` into *track: 1, or 0 when there is none. */
int diskos_player_get_track(diskos_player_slot slot, diskos_player_track* track);

/* Advance `slot` to now (PlayerState::advance): its activity expires if its reports stopped
 * coming. Returns 1 if the activity changed, else 0. Call it regularly. */
int diskos_player_advance(diskos_player_slot slot);

/* Check whether `slot` is playing a track or loading one to play: 1 or 0. */
int diskos_player_is_playing(diskos_player_slot slot);

/* Get the position of `slot` in ms, or -1 when none was reported since the track changed. */
int64_t diskos_player_position(diskos_player_slot slot);

/* Forget everything in `slot`: the player restarted, or the receiver session ended. */
void diskos_player_reset(diskos_player_slot slot);

#ifdef __cplusplus
}
#endif

#endif
