/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef FOLDERBROWSER_H
#define FOLDERBROWSER_H
#include "lvgl/lvgl.h"
/* File/folder browser (SCR_FOLDER): a browsable directory tree of the SD card
 * (mounted at /tmp/sdcard). Tap a folder to descend; the header back-chevron
 * ascends one level (and leaves the screen at the SD root). Tap an audio file
 * to play that track with its folder as the queue, or hold a folder to play it
 * from its first song; see folderbrowser.c. */
/* Folder Playback (config key "folder_queue", Settings > Library): what a tapped audio file queues. */
#define FQ_FOLDER     0   /* the tapped file's folder (default) */
#define FQ_SUBFOLDERS 1   /* the folder and every subfolder under it */
#define FQ_ALL        2   /* the whole library (the all-songs queue) */
void folderbrowser_create(lv_obj_t *root);   /* build the screen once (from screens_init) */
void folderbrowser_open(void);               /* reset to the SD root + show SCR_FOLDER */
#endif
