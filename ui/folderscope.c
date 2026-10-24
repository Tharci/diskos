/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "folderscope.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* PATH is BINARY-collated TEXT, so "everything under dir" is the byte range dir+"/" .. dir+"0" ('0' is the byte after
 * '/'): an exact prefix match that the diskos_song_path index serves, with no LIKE escaping for '%' or '_' in a name.
 * The bounds are built from the bound dir in SQL, so no path length limit applies here. */
static const char *const FS_SQL =
    "SELECT ID, PATH FROM SONG WHERE PATH > ?1 || '/' AND PATH < ?1 || '0' AND lower(PATH) NOT LIKE '%.m4b' "
    "ORDER BY PATH, TRACK, ID;";

int folderscope_ids(sqlite3 *d, const char *dir, int recursive, const char *tapped, int **ids, int *start_id){
    if(ids) *ids = NULL;
    if(start_id) *start_id = 0;
    size_t dl = dir ? strlen(dir) : 0;
    if(!d || !ids || !start_id || !dl || dir[dl - 1] == '/' || dl > INT_MAX - 1) return -1;
    sqlite3_stmt *st;
    if(sqlite3_prepare_v2(d, FS_SQL, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, dir, (int)dl, SQLITE_STATIC);
    int *v = NULL, n = 0, cap = 0, start = 0, rc;
    while((rc = sqlite3_step(st)) == SQLITE_ROW){
        const char *path = (const char *)sqlite3_column_text(st, 1);
        if(!path) continue;
        /* direct children only: no further '/' after dir + "/". Checked here on bytes - SQL substr/instr count
         * characters, which a byte offset would misplace in a non-ASCII folder name. */
        if(!recursive && strchr(path + dl + 1, '/')) continue;
        if(n == cap){
            if(cap > INT_MAX / 2){ rc = SQLITE_NOMEM; break; }
            int nc = cap ? cap * 2 : 64;
            int *nv = realloc(v, (size_t)nc * sizeof *v);
            if(!nv){ rc = SQLITE_NOMEM; break; }
            v = nv; cap = nc;
        }
        int id = sqlite3_column_int(st, 0);
        if(!start && tapped && !strcmp(path, tapped)) start = id;   /* rows come in TRACK order: the file's first */
        v[n++] = id;
    }
    sqlite3_finalize(st);
    if(rc != SQLITE_DONE || !n){ free(v); return rc == SQLITE_DONE ? 0 : -1; }
    *ids = v; *start_id = start;
    return n;
}
