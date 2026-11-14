/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The playback commands (services/playback/player_controller.hpp) for the legacy C. Each sends
 * one command to the stock player as a user action (ipc_send_cmd), and returns 0 once it is
 * queued, or -1 if it was not sent or its value is out of range. A toggle that resumes a paused
 * track makes the local PlayerState Loading until the player reports it playing. */
#ifndef DISKOS_LEGACY_PLAYER_CONTROLLER_H
#define DISKOS_LEGACY_PLAYER_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int diskos_playback_toggle(void);
int diskos_playback_next(void);
int diskos_playback_previous(void);
int diskos_playback_seek(uint32_t position_ms);
int diskos_playback_set_volume(int level);        /* 0..120 (VOL_MAX) */
int diskos_playback_set_play_mode(int mode);      /* 0..4, as the 0102 command and PlayMode */
int diskos_playback_set_favourite(int favourite); /* 1 to mark, 0 to unmark */

/* Play the list `list_type` (0 to 8, or 10: ListType, the 0100 command) named `name` (may be
 * NULL for none) from its 0-based `position` (at most 0xFFFF), and its first track from
 * `start_ms` (0 for the start; PlayerController::play_list). When nothing plays, the output
 * router readies the player first (its play preamble), and no play is sent if it could not. The
 * local PlayerState is Loading once it is sent. */
int diskos_playback_play_list(
        int list_type, unsigned position, const char* name, uint32_t start_ms
);

/* Check whether a play is starting: the local PlayerState is Loading (a list play or a resume
 * was sent, or a new track was reported, and it is not playing yet). 1 or 0. */
int diskos_playback_starting(void);

#ifdef __cplusplus
}
#endif

#endif
