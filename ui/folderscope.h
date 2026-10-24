/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef FOLDERSCOPE_H
#define FOLDERSCOPE_H
#include "sqlite3.h"

/* The library songs filed under one SD folder (Browse Files' folder queue). Depends on SQLite only, so it is
 * host-tested (tests/folderscope_test.c) against an in-memory SONG table.
 *
 * dir is an absolute folder path without a trailing '/', in the scanner's own PATH form (dir + "/" + name). Rows are
 * the SONG rows whose PATH lies under dir - directly in it, or (recursive) at any depth - in PATH, TRACK, ID order: the
 * order the player reads a reserved-slot queue back in, so a Cast queue built from these ids matches the local one.
 * Audiobooks (.m4b) are left out, as in every music queue. *start_id is the first row whose PATH equals tapped (a CUE
 * or SACD file's lowest TRACK), 0 when tapped is NULL or has no row.
 *
 * Returns the row count (> 0, *ids malloc'd, caller frees), 0 when the folder has no rows, or -1 on a bad argument,
 * a database error or OOM (*ids NULL: never a partial list). */
int folderscope_ids(sqlite3 *d, const char *dir, int recursive, const char *tapped, int **ids, int *start_id);

#endif
