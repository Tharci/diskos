/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Player traffic capture. Compiled in ONLY with -DDISKOS_FRAME_LOG (a capture build, never shipped). */
#ifndef DISKOS_FRAMELOG_H
#define DISKOS_FRAMELOG_H
#ifdef DISKOS_FRAME_LOG
#include <stddef.h>
/* A frame received on /ui. */
void framelog_rx(const char *frame, size_t n);
/* A send to /player: err 0 = sent, an errno = failed, -1 = /player could not be opened. */
void framelog_tx(const char *frame, size_t n, int err, const char *policy);
#endif
#endif
