/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The output router (services/output/output_router.hpp) for the legacy C code: where the player
 * sends its audio, and every move from one output to another. Main thread only: call these from
 * the LVGL loop. */
#ifndef DISKOS_LEGACY_OUTPUT_H
#define DISKOS_LEGACY_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* "AA:BB:CC:DD:EE:FF" and its NUL */
#define DISKOS_OUTPUT_MAC_SIZE 18

typedef enum {
    DISKOS_OUTPUT_UNKNOWN = -1,
    DISKOS_OUTPUT_INTERNAL_DAC = 0,
    DISKOS_OUTPUT_SPDIF,
    DISKOS_OUTPUT_USB_AUDIO,
    DISKOS_OUTPUT_BLUETOOTH,
} diskos_output_kind;

/* What keeps the output from changing now (services/output/interfaces.hpp, Blocker). */
typedef enum {
    DISKOS_OUTPUT_UNBLOCKED = 0,
    DISKOS_OUTPUT_CARD_NOT_READY,
    DISKOS_OUTPUT_OTHER_SOURCE,
    DISKOS_OUTPUT_AUDIOBOOK,
    DISKOS_OUTPUT_PLAY_STARTING,
    DISKOS_OUTPUT_CASTING,
} diskos_output_blocker;

/* What diskos_output_select() did with a choice. */
typedef enum {
    DISKOS_OUTPUT_SELECTED = 0,
    DISKOS_OUTPUT_REFUSED_CARD_NOT_READY,
    DISKOS_OUTPUT_REFUSED_OTHER_SOURCE,
    DISKOS_OUTPUT_REFUSED_AUDIOBOOK,
    DISKOS_OUTPUT_REFUSED_NO_USB_DAC,
    DISKOS_OUTPUT_REFUSED_INVALID, /* Not an output, or a malformed address */
} diskos_output_selection;

/* What the router reports from its ticks (OutputRouter's Outcome). */
typedef enum {
    DISKOS_OUTPUT_SWITCHED = 0,
    DISKOS_OUTPUT_RETRYING,
    DISKOS_OUTPUT_FAILED,
    DISKOS_OUTPUT_NO_USB_DAC,
    DISKOS_OUTPUT_RECOVERING,
    DISKOS_OUTPUT_RESUME_FAILED,
} diskos_output_outcome;

/* What the router asks of the legacy C. */
typedef struct {
    diskos_output_blocker (*blocker)(void);
    void (*on_outcome)(diskos_output_outcome outcome, diskos_output_kind output);
} diskos_output_hooks;

/* Start the router with `hooks`. Later calls change nothing. Until it starts, the other
 * functions do nothing and report an unknown, idle, unready router. */
void diskos_output_start(diskos_output_hooks hooks);

/* Run the router for one main-loop tick: hand what the player sent to the legacy IPC
 * (player_link.h) and to the router, advance a pending playback restore, and move the output where
 * it may. Outcomes go to hooks.on_outcome. Nothing reaches the legacy IPC before
 * diskos_output_start(), so start the router before the link. */
void diskos_output_tick(void);

/* The user chooses `output`; `mac` names the sink for DISKOS_OUTPUT_BLUETOOTH, else is ignored. */
diskos_output_selection diskos_output_select(diskos_output_kind output, const char* mac);

/* The Bluetooth layer saw the sink `mac` connect. */
void diskos_output_bluetooth_connected(const char* mac);

/* The sink `mac` is gone, or about to be: if it has the audio, the audio leaves it before this
 * returns (its first 0666 is sent), so the sink can be dropped right after. */
void diskos_output_bluetooth_lost(const char* mac);

/* As diskos_output_bluetooth_lost(), for whatever sink the audio has or should go to (turning
 * Bluetooth off). */
void diskos_output_leave_bluetooth(void);

/* Before a local play from idle: re-assert the output and the work mode (OutputRouter::
 * prepare_play). Returns 0, or -1 if a frame was not sent. */
int diskos_output_prepare_play(void);

/* Get where the audio should go, and the sink's address into `mac` (DISKOS_OUTPUT_MAC_SIZE bytes,
 * may be NULL) for a Bluetooth one, else "". */
diskos_output_kind diskos_output_desired(char* mac);

/* Get where the player sends its audio, as diskos_output_desired(); DISKOS_OUTPUT_UNKNOWN when it
 * is not known. */
diskos_output_kind diskos_output_applied(char* mac);

/* 1 while a move runs or a half-applied one is retried: playback must not start then. */
int diskos_output_busy(void);

/* 1 once the player may be given a route: it answered and its late start-up is over. */
int diskos_output_ready(void);

#ifdef __cplusplus
}
#endif

#endif
