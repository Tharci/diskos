/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Off-device host test for folderscope.c (Browse Files folder queue rows) against an in-memory SONG table with the
 * scanner's PATH index. Built under ASan/UBSan (LeakSanitizer included) via tests/Makefile. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "folderscope.h"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)
#define R "/tmp/sdcard"

static sqlite3 *g_db;
static void exec(const char *sql){
    char *err = NULL;
    if(sqlite3_exec(g_db, sql, 0, 0, &err) != SQLITE_OK){ printf("setup failed: %s\n", err); exit(2); }
}
static void ins(int id, const char *path, int track){
    sqlite3_stmt *st;
    sqlite3_prepare_v2(g_db, "INSERT INTO SONG (ID, PATH, TRACK) VALUES (?1, ?2, ?3);", -1, &st, NULL);
    sqlite3_bind_int(st, 1, id); sqlite3_bind_text(st, 2, path, -1, SQLITE_STATIC); sqlite3_bind_int(st, 3, track);
    if(sqlite3_step(st) != SQLITE_DONE){ printf("insert failed: %s\n", path); exit(2); }
    sqlite3_finalize(st);
}
/* run the query; 1 when the count, the ids (in order) and the start id are all as expected */
static int expect(const char *dir, int recursive, const char *tapped, const int *want, int nwant, int want_start){
    int *ids = (int *)0x1, start = -1;
    int n = folderscope_ids(g_db, dir, recursive, tapped, &ids, &start);
    int ok = n == nwant && start == want_start && (n > 0 ? ids != NULL : ids == NULL);
    for(int i = 0; ok && i < n; i++) ok = ids[i] == want[i];
    if(!ok){
        printf("  got n=%d start=%d ids:", n, start);
        for(int i = 0; i < n && ids; i++) printf(" %d", ids[i]);
        printf("\n");
    }
    free(ids);
    return ok;
}

/* EXPLAIN QUERY PLAN of the statement folderscope_ids really runs (captured with the trace hook, not a copy of it) */
static char g_sql[512];
static int trace_cb(unsigned type, void *ctx, void *stmt, void *x){
    (void)ctx; (void)stmt;
    if(type == SQLITE_TRACE_STMT && !g_sql[0]) snprintf(g_sql, sizeof g_sql, "%s", (const char *)x);
    return 0;
}
static int uses_path_index(void){
    char q[600]; snprintf(q, sizeof q, "EXPLAIN QUERY PLAN %s", g_sql);
    sqlite3_stmt *st; int hit = 0;
    if(sqlite3_prepare_v2(g_db, q, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, R "/Music/A", -1, SQLITE_STATIC);
    while(sqlite3_step(st) == SQLITE_ROW){
        const char *detail = (const char *)sqlite3_column_text(st, 3);
        if(detail && strstr(detail, "USING INDEX diskos_song_path")) hit = 1;
    }
    sqlite3_finalize(st);
    return hit;
}

int main(void){
    if(sqlite3_open(":memory:", &g_db) != SQLITE_OK) return 2;
    exec("CREATE TABLE SONG (ID INTEGER PRIMARY KEY autoincrement, PATH TEXT, TRACK INT);"
         "CREATE INDEX diskos_song_path ON SONG(PATH);");
    /* inserted out of order on purpose: the result order must come from the query */
    ins(1,  R "/Music/A/02.flac", 0);
    ins(2,  R "/Music/A/01.flac", 0);
    ins(3,  R "/Music/A/CD2/01.flac", 0);
    ins(4,  R "/Music/AB/x.flac", 0);              /* sibling sharing the "A" prefix */
    ins(5,  R "/Music/A-side/y.flac", 0);          /* '-' sorts before '/' */
    ins(6,  R "/Music/A.flac", 0);                 /* a file named like the folder */
    ins(7,  R "/Music/A/live.flac", 3);            /* CUE tracks, stored out of track order */
    ins(8,  R "/Music/A/live.flac", 1);
    ins(9,  R "/Music/A/live.flac", 2);
    ins(10, R "/Music/A/book.m4b", 0);             /* audiobooks never join a music queue */
    ins(11, R "/Music/A/BOOK.M4B", 0);
    ins(12, R "/Music/A/B.flac", 0);               /* byte order: uppercase before lowercase */
    ins(13, R "/a_b/1.mp3", 0);                    /* LIKE wildcards in names */
    ins(14, R "/axb/1.mp3", 0);
    ins(15, R "/100% it's/1.mp3", 0);
    ins(16, R "/Zen\xc3\xa9k/01.mp3", 0);          /* "Zenek" with e-acute: non-ASCII folder */
    ins(17, R "/Zen\xc3\xa9k/sub/02.mp3", 0);
    ins(18, R "/\xe9\x9f\xb3\xe6\xa5\xbd/\xe6\x9b\xb2.flac", 0);   /* CJK folder and file */
    ins(19, R "/root.mp3", 0);
    ins(20, "/other/card/Music/A/01.flac", 0);     /* outside the card root entirely */

    printf("direct children vs subtree\n");
    { int w[] = {2, 1, 12, 8, 9, 7};              /* 01, 02, B, live t1..t3 */
      CHECK(expect(R "/Music/A", 0, NULL, w, 6, 0), "folder only: its files, PATH then TRACK order"); }
    { int w[] = {2, 1, 12, 3, 8, 9, 7};           /* 'C' (CD2/) sorts between 'B' and 'l' */
      CHECK(expect(R "/Music/A", 1, NULL, w, 7, 0), "recursive: subfolder rows join in path order"); }

    printf("start row\n");
    { int w[] = {2, 1, 12, 8, 9, 7};
      CHECK(expect(R "/Music/A", 0, R "/Music/A/02.flac", w, 6, 1), "start = tapped file's id");
      CHECK(expect(R "/Music/A", 0, R "/Music/A/live.flac", w, 6, 8), "CUE file starts at its lowest TRACK");
      CHECK(expect(R "/Music/A", 0, R "/Music/A/missing.flac", w, 6, 0), "unindexed tap -> start 0");
      CHECK(expect(R "/Music/A", 0, R "/Music/A/CD2/01.flac", w, 6, 0), "tap outside the queued scope -> start 0"); }

    printf("names\n");
    { int w[] = {13}; CHECK(expect(R "/a_b", 0, NULL, w, 1, 0), "'_' is literal, not a wildcard"); }
    { int w[] = {15}; CHECK(expect(R "/100% it's", 0, R "/100% it's/1.mp3", w, 1, 15), "'%', quote and space"); }
    { int w[] = {16}; CHECK(expect(R "/Zen\xc3\xa9k", 0, NULL, w, 1, 0), "non-ASCII folder: direct child only"); }
    { int w[] = {16, 17}; CHECK(expect(R "/Zen\xc3\xa9k", 1, NULL, w, 2, 0), "non-ASCII folder: recursive"); }
    { int w[] = {18}; CHECK(expect(R "/\xe9\x9f\xb3\xe6\xa5\xbd", 0, NULL, w, 1, 0), "CJK folder"); }

    printf("card root\n");
    { int w[] = {19}; CHECK(expect(R, 0, NULL, w, 1, 0), "root, folder only: just its own files"); }
    { int n = 0, *ids = NULL, s;
      n = folderscope_ids(g_db, R, 1, NULL, &ids, &s);
      CHECK(n == 17, "root, recursive: every card row except the two books");
      free(ids); }

    printf("empty and invalid\n");
    CHECK(expect(R "/Music/Empty", 0, NULL, NULL, 0, 0), "folder without rows -> 0, ids NULL");
    CHECK(expect(R "/Music/A/", 0, NULL, NULL, -1, 0), "trailing slash refused");
    CHECK(expect("", 0, NULL, NULL, -1, 0), "empty dir refused");
    CHECK(expect(NULL, 0, NULL, NULL, -1, 0), "NULL dir refused");
    { int *ids = NULL, s = 0;
      CHECK(folderscope_ids(NULL, R, 0, NULL, &ids, &s) == -1 && !ids, "NULL db refused");
      CHECK(folderscope_ids(g_db, R, 0, NULL, NULL, &s) == -1, "NULL ids refused");
      CHECK(folderscope_ids(g_db, R, 0, NULL, &ids, NULL) == -1 && !ids, "NULL start refused"); }

    printf("growth past the first allocation\n");
    exec("BEGIN;");
    for(int i = 0; i < 300; i++){
        char p[64]; snprintf(p, sizeof p, R "/Big/%03d.flac", i);
        ins(1000 + i, p, 0);
    }
    exec("COMMIT;");
    { int *ids = NULL, s = 0, ok;
      int n = folderscope_ids(g_db, R "/Big", 0, R "/Big/250.flac", &ids, &s);
      ok = n == 300 && s == 1250;
      for(int i = 0; ok && i < n; i++) ok = ids[i] == 1000 + i;
      CHECK(ok, "300 rows, all in order, start found");
      free(ids); }

    printf("index\n");
    sqlite3_trace_v2(g_db, SQLITE_TRACE_STMT, trace_cb, NULL);
    { int *ids = NULL, s; folderscope_ids(g_db, R "/Music/A", 0, NULL, &ids, &s); free(ids); }
    sqlite3_trace_v2(g_db, 0, NULL, NULL);
    CHECK(g_sql[0] && uses_path_index(), "the range is served by the diskos_song_path index");

    printf("database error\n");
    exec("DROP TABLE SONG;");
    CHECK(expect(R "/Music/A", 0, NULL, NULL, -1, 0), "missing SONG table -> -1, nothing leaked");

    sqlite3_close(g_db);
    printf(fails ? "FAILED: %d\n" : "all folderscope checks passed\n", fails);
    return fails ? 1 : 0;
}
