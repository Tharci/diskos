/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The volume keys' press/hold logic: the two buttons' raw levels -> debounced presses, hold-to-repeat steps, and a
 * both-held chord. The pure half of volkeys.c (which samples the pins and acts on the result): no device access, so it
 * is unit-tested off-device (tests/volkeys_hold_test.c). */
#ifndef DISKOS_VOLKEYS_HOLD_H
#define DISKOS_VOLKEYS_HOLD_H
#include <stdint.h>

#define VKH_DEBOUNCE 2          /* equal samples that confirm a press or a release */
#define VKH_CHORD_MS 3000       /* both buttons held this long report the chord once */

typedef struct {
    uint32_t hold_ms, repeat_ms;   /* the timing; may change between samples */
    unsigned down;             /* confirmed state: bit 0 = the "up" button, bit 1 = the "down" button */
    unsigned char run[2];      /* samples in a row that disagree with `down` */
    uint32_t next[2];          /* tick of a held button's next repeat */
    int chord, both, reported; /* chord: both went down (no steps until both are up); both: both down now */
    uint32_t since;            /* tick both went down */
} vkh_t;

/* Start from the levels sampled now: a button already down (Vol+ held through boot to pick the other interface)
 * never steps until both are up, so a hold that began before us doesn't ramp anything. */
void vkh_init(vkh_t *k, unsigned held, uint32_t hold_ms, uint32_t repeat_ms);
/* Feed one sample. held = the raw levels (bit 0 up, bit 1 down, 1 = pressed); now = a monotonic ms tick (wrap-safe).
 * Returns the steps (+ up, - down). A confirmed press steps at once; a held button steps again after hold_ms and then
 * every repeat_ms. While both are down nothing steps, and *chord is set once when they have stayed down VKH_CHORD_MS. */
int vkh_feed(vkh_t *k, uint32_t now, unsigned held, int *chord);
#endif
