/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The connection to the stock player (platform/player_link.hpp) for the legacy C code: one
 * PlayerLink, which receives the player's frames on its own thread and sends from any thread.
 * What it receives reaches the legacy C on the main loop, through diskos_output_tick(). */
#ifndef DISKOS_LEGACY_PLAYER_LINK_H
#define DISKOS_LEGACY_PLAYER_LINK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the link calls on the main loop, before the services hear the same event
 * (player/frame_listener.hpp, legacy/player_link.hpp). */
typedef struct {
    void (*on_connected)(
            unsigned instance
    ); /* A new player instance: 1, then one more each restart */
    void (*on_frame)(const char* frame, size_t size);
    void (*on_drained)(void);
} diskos_link_listener;

typedef enum {
    DISKOS_LINK_SENT = 0,
    DISKOS_LINK_FAILED = -1,        /* Not queued */
    DISKOS_LINK_WRONG_INSTANCE = 1, /* Meant for a player instance that is no longer connected */
} diskos_link_result;

/* Start the link, which calls `listener` from the main loop. Returns 0, or -1 if it already
 * started. It lives until the process ends. */
int diskos_link_start(diskos_link_listener listener);

/* Send `frame` (a NUL-terminated frame) to the player; with `instance` (not 0) only if that
 * instance is still the connected one. FAILED before diskos_link_start(). */
diskos_link_result diskos_link_send(const char* frame, unsigned instance);

/* Take every frame the player queued now; on_drained follows them on the main loop
 * (PlayerLink::request_drain). */
void diskos_link_request_drain(void);

#ifdef __cplusplus
}
#endif

#endif
