/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef DISKOS_MODES_H
#define DISKOS_MODES_H

/* The Audio Source picker (stock: Working mode). Its USB Audio row, like Settings' SPDIF row, exists only in builds
 * made with -DDISKOS_TEST_OUTPUTS (device-unverified); the output router (src/legacy/output.h) moves the output. */
enum { SOURCE_AIRPLAY = 0x101, SOURCE_USB_AUDIO = 0x200 };
int modes_receiver_quiet(void); /* strict closed-PCM proof, no mutation */
int modes_local_teardown(unsigned generation); /* one guarded Local reselect, no start */
int  modes_pcm_quiet(void);             /* 1 only when every playback PCM node is readable and explicitly closed */
void modes_output_changed(void);        /* the output changed, or its move did: refresh the picker and Settings */

#endif
