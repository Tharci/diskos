/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Volume keys: diskOS reads the two buttons itself, so a press is one volume step at once and a hold repeats.
 *
 * Stock facts (V2.57):
 *   The kernel driver x2000_key (event0) classifies gestures and reports one code per gesture: Vol+ 251 single,
 *     266 double, 263 long; Vol- 252, 267, 262. A single press is only reported once the double-press window has
 *     passed, which is the delay the stock keys have. Other codes: 250 play, 259/265 power short, 264 power long,
 *     268/269 play long/double. mq_player reads event0 without a grab (key thread 0x4e28a0) and its dispatcher
 *     (0x4e72f0, tables 0x6db9b0 / 0x6dba00) ignores value 0 for every code that is not a volume code.
 *   The buttons are GPB13 (Vol+) and GPB14 (Vol-), active low, in the GPB PxPIN register at 0x10010100. The driver
 *     owns the pins (sysfs export is refused), so they are sampled.
 *
 * What diskOS does with that: a thread samples both pins every VK_POLL_MS and runs the pure state machine below. It
 * grabs event0 so the player never acts on the volume codes, and hands every other key back to the player. Local
 * volume is set by the thread itself (also while an external app owns the screen); receiver volume goes through the
 * UI loop. Without a grab (other firmware, or no access) the stock keys stay, and the thread only watches for the
 * app force-close chord. */
#ifndef DISKOS_VOLKEYS_H
#define DISKOS_VOLKEYS_H
#include <stdint.h>

#define VK_DEBOUNCE 2          /* equal samples that confirm a press or a release */
#define VK_HOLD_DEFAULT 400    /* ms a held key waits before repeating (cfg vol_hold_ms) */
#define VK_REPEAT_DEFAULT 40   /* ms between repeats (cfg vol_repeat_ms): 0..120 in ~2.5 s at the default 2 levels a step */
#define VK_STEP_DEFAULT 2      /* volume levels per step (cfg vol_step, 1..5) on the player's 0..120 scale */
#define VK_ESCAPE_MS 3000      /* both keys held this long force-close an external app */

/* ---- the press/hold state machine (pure) ---- */
typedef struct {
    uint32_t hold_ms, repeat_ms;   /* the user's timing; may change between samples */
    unsigned down;             /* confirmed state: bit 0 = Vol+, bit 1 = Vol- */
    unsigned char run[2];      /* samples in a row that disagree with `down` */
    uint32_t next[2];          /* tick of a held key's next repeat */
    int chord, both, escaped;  /* chord: both keys went down (no steps until both are up); both: both down now */
    uint32_t since;            /* tick both keys went down */
} vk_core;

/* Start from the levels sampled now, with the default timing: a key already down (Vol+ held through boot to pick the
 * other interface) never steps until both keys are up, so the boot-time hold doesn't ramp the volume. */
void vk_init(vk_core *c, unsigned held);
/* Feed one sample. held = the raw levels (bit 0 Vol+, bit 1 Vol-, 1 = pressed); now = a monotonic ms tick
 * (wrap-safe). Returns the steps to apply (+ up, - down); the device side scales them by the Volume Step. A confirmed press steps at once; a
 * held key steps again after hold_ms and then every repeat_ms. While both keys are down nothing steps, and *escape is
 * set once when they have stayed down VK_ESCAPE_MS. */
int vk_feed(vk_core *c, uint32_t now, unsigned held, int *escape);

#ifndef VK_CORE_ONLY
/* ---- the device side ---- */
void volkeys_start(void);                    /* once, after IPC starts: the thread, and the grab where supported */
int  volkeys_owned(void);                    /* 1 while diskOS owns the volume keys */
int  volkeys_escape_ready(void);             /* 1 if the force-close chord can be read */
/* UI loop, every iteration: where volume goes, the hold timing, the levels per step, and the Max Volume cap. */
void volkeys_publish(int remote, int hold_ms, int repeat_ms, int step, int max_vol);
void volkeys_set_app(int running);           /* app_run: 1 before the app starts (clears a stale chord), 0 after */
int  volkeys_take_escape(void);              /* 1 once per chord held VK_ESCAPE_MS */
int  volkeys_take_remote(void);              /* the receiver steps pressed since the last call */
int  volkeys_take_shown(int *level);         /* 1 when a local key change should show the level */
int  volkeys_echo_window(void);              /* 1 while an a714 may be the echo of a level diskOS just sent */
int  volkeys_press_power(void);              /* hand a short power press to the player; 1 = queued */
#endif
#endif
