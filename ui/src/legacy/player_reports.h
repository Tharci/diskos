/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The player's reports (player/reports.hpp) for the legacy C code: decode_report() behind a C
 * interface, with each report reduced to its kind and one number. Reports the legacy code still
 * reads itself (a202, Bluetooth, unknown tags) come back UNDECODED. A frame the decoder refuses
 * comes back MALFORMED, and must change nothing. */
#ifndef DISKOS_LEGACY_PLAYER_REPORTS_H
#define DISKOS_LEGACY_PLAYER_REPORTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISKOS_REPORT_MALFORMED,
    DISKOS_REPORT_UNDECODED,
    DISKOS_REPORT_POSITION,   /* value: the position in ms */
    DISKOS_REPORT_PLAY_MODE,  /* value: 0..4, see PlayMode */
    DISKOS_REPORT_VOLUME,     /* value: 0..120 */
    DISKOS_REPORT_SOURCE,     /* value: the 0657 source, see Source */
    DISKOS_REPORT_CHARGING,   /* value: 1 charging, 0 not */
    DISKOS_REPORT_SCREEN,     /* value: 1 on, 0 off */
    DISKOS_REPORT_POWER_OFF,  /* value: 0 */
    DISKOS_REPORT_COVER_ART,  /* value: 1 ready, 0 not */
    DISKOS_REPORT_FAVOURITE,  /* value: as sent, meaning not known yet */
    DISKOS_REPORT_BATTERY,    /* value: 0..100 percent */
    DISKOS_REPORT_CONVERSION, /* value: the result code, 0 = done */
} diskos_report_kind;

typedef struct {
    diskos_report_kind kind;
    uint32_t value;
} diskos_report;

/* Decode one frame received on /ui: `size` bytes at `bytes`. */
diskos_report diskos_decode_report(const char* bytes, size_t size);

#ifdef __cplusplus
}
#endif

#endif
