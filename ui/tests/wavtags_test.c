/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Off-device host test for the scanner's WAV tag reader (wav_read, through tags_with_provenance too). scanner.c is
 * compiled into this test so its static readers are reachable; everything they don't use is dropped at link time
 * (tests/Makefile), so no SQLite or UI is linked. Every WAV is built here byte by byte. Built under ASan/UBSan. */
/* TODO: move the tag readers (text decoding, the format readers, tags_with_provenance) out of scanner.c into a
 * tags module, so this test links it like config_test and playstate_test instead of including a .c file. */
#include "../scanner.c"

/* ---- checks: a failure names the field and prints what came back ---- */
static int fails = 0;
static void check_str(const char *what, const char *got, const char *want){
    if(strcmp(got, want) != 0){ printf("  FAIL: %s: got \"%s\", want \"%s\"\n", what, got, want); fails++; }
}
static void check_int(const char *what, int got, int want){
    if(got != want){ printf("  FAIL: %s: got %d, want %d\n", what, got, want); fails++; }
}

/* ---- building WAV files ---- */
typedef struct { unsigned char b[1 << 16]; size_t n; } buf;
typedef struct { const char *id, *value; } tag;   /* an INFO item or ID3 frame: 4-char id and its text */
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void put(buf *w, const void *p, size_t n){
    if(w->n + n > sizeof w->b){ fprintf(stderr, "test WAV too big\n"); exit(2); }
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
static void put_le32(buf *w, uint32_t v){
    unsigned char c[4] = { v, v >> 8, v >> 16, v >> 24 };
    put(w, c, 4);
}
/* A RIFF chunk: id, size (LE), body (len bytes; NULL = zeros), then a pad byte when len is odd. */
static void chunk(buf *w, const char *id, const void *body, uint32_t len){
    static const unsigned char zeros[1024];
    put(w, id, 4);
    put_le32(w, len);
    put(w, body ? body : zeros, len);
    if(len & 1) put(w, "", 1);
}
/* "RIFF" <size> "WAVE" and a fmt chunk (PCM 44.1 kHz stereo 16-bit). The size is set by wav_finish. */
static void wav_begin(buf *w){
    static const unsigned char fmt[16] = { 1,0, 2,0, 0x44,0xAC,0,0, 0x10,0xB1,2,0, 4,0, 16,0 };
    w->n = 0;
    put(w, "RIFF", 4);
    put_le32(w, 0);
    put(w, "WAVE", 4);
    chunk(w, "fmt ", fmt, sizeof fmt);
}
static void wav_finish(buf *w){
    uint32_t size = (uint32_t)w->n - 8;
    unsigned char c[4] = { size, size >> 8, size >> 16, size >> 24 };
    memcpy(w->b + 4, c, 4);
}
static void data_chunk(buf *w){ chunk(w, "data", NULL, 64); }   /* 64 bytes of silence */

/* A LIST/INFO chunk, each value NUL-terminated the way rippers write them. */
#define info_chunk(w, tags) info_chunk_n(w, tags, COUNT(tags))
static void info_chunk_n(buf *w, const tag *tags, size_t n){
    buf list = { .n = 0 };
    put(&list, "INFO", 4);
    for(size_t i = 0; i < n; i++)
        chunk(&list, tags[i].id, tags[i].value, (uint32_t)strlen(tags[i].value) + 1);
    chunk(w, "LIST", list.b, (uint32_t)list.n);
}
/* An "id3 " chunk holding an ID3v2.3 tag of UTF-16 (BOM) text frames, as WAV taggers write it. ASCII values only. */
#define id3_chunk(w, tags) id3_chunk_n(w, tags, COUNT(tags))
static void id3_chunk_n(buf *w, const tag *tags, size_t n){
    buf id3 = { .n = 0 };
    put(&id3, "ID3\3\0\0", 6);                                 /* ID3v2.3.0, no flags */
    put(&id3, "\0\0\0\0", 4);                                  /* tag size, set below */
    for(size_t i = 0; i < n; i++){
        size_t len = strlen(tags[i].value);
        uint32_t size = 3 + 2 * (uint32_t)len;                 /* encoding byte, BOM, UTF-16LE text */
        unsigned char size_be[4] = { size >> 24, size >> 16, size >> 8, size };
        put(&id3, tags[i].id, 4);
        put(&id3, size_be, 4);
        put(&id3, "\0\0", 2);                                  /* frame flags */
        put(&id3, "\1\xFF\xFE", 3);                            /* encoding 1 = UTF-16, little-endian BOM */
        for(size_t k = 0; k < len; k++){
            unsigned char u16[2] = { (unsigned char)tags[i].value[k], 0 };
            put(&id3, u16, 2);
        }
    }
    uint32_t size = (uint32_t)id3.n - 10;                      /* syncsafe: 7 bits per byte */
    unsigned char size_ss[4] = { (size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F };
    memcpy(id3.b + 6, size_ss, 4);
    chunk(w, "id3 ", id3.b, (uint32_t)id3.n);
}

/* ---- running the reader ---- */
static char g_dir[64];
static const char *save(const buf *w, const char *name){
    static char path[128];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    FILE *f = fopen(path, "wb");
    if(!f || fwrite(w->b, 1, w->n, f) != w->n){ perror(path); exit(2); }
    fclose(f);
    return path;
}
typedef struct {
    char title[TAGLEN], artist[TAGLEN], album[TAGLEN], genre[TAGLEN], album_artist[TAGLEN];
    int track, disc, read_err;
} wav_tags;
static wav_tags read_wav(const buf *w){
    wav_tags t;
    memset(&t, 0, sizeof t);
    FILE *f = fopen(save(w, "t.wav"), "rb");
    if(!f){ perror("t.wav"); exit(2); }
    wav_read(f, t.title, t.artist, t.album, t.genre, t.album_artist, &t.track, &t.disc, &t.read_err);
    fclose(f);
    return t;
}

/* The tags most tests use: the same song described by INFO and by ID3. */
static const tag INFO_TAGS[] = {
    { "ISFT", "Some Ripper 1.0" }, { "ITRK", "1" }, { "INAM", "Song Title" }, { "IPRD", "Album Name" },
    { "IART", "Artist Name" }, { "IGNR", "Genre Name" },
};
static const tag ID3_TAGS[] = {
    { "TIT2", "Song Title" }, { "TPE1", "Artist Name" }, { "TRCK", "1/7" }, { "TALB", "Album Name" },
    { "TPOS", "1/1" }, { "TCON", "Genre Name" }, { "TPE2", "Album Artist" },
};

static void test_tag_sources(void){
    buf w; wav_tags t;

    wav_begin(&w); info_chunk(&w, INFO_TAGS); data_chunk(&w); wav_finish(&w);
    t = read_wav(&w);
    check_int("INFO only: read error",   t.read_err, 0);
    check_str("INFO only: title",        t.title, "Song Title");
    check_str("INFO only: artist",       t.artist, "Artist Name");
    check_str("INFO only: album",        t.album, "Album Name");
    check_str("INFO only: genre",        t.genre, "Genre Name");
    check_int("INFO only: track",        t.track, 1);
    check_str("INFO only: album artist", t.album_artist, "");   /* INFO has no such item */
    check_int("INFO only: disc",         t.disc, 0);

    /* taggers usually put the ID3 chunk after the audio */
    wav_begin(&w); data_chunk(&w); id3_chunk(&w, ID3_TAGS); wav_finish(&w);
    t = read_wav(&w);
    check_int("ID3 after data: read error",   t.read_err, 0);
    check_str("ID3 after data: title",        t.title, "Song Title");
    check_str("ID3 after data: artist",       t.artist, "Artist Name");
    check_str("ID3 after data: album",        t.album, "Album Name");
    check_str("ID3 after data: genre",        t.genre, "Genre Name");
    check_int("ID3 after data: track",        t.track, 1);
    check_str("ID3 after data: album artist", t.album_artist, "Album Artist");
    check_int("ID3 after data: disc",         t.disc, 1);
}

/* ID3 wins over INFO whichever chunk comes first, and INFO fills the fields ID3 leaves empty. */
static void test_precedence(void){
    static const tag info[] = { { "INAM", "info title" }, { "IART", "info artist" }, { "IGNR", "Info Genre" },
                                { "ITRK", "9" } };
    static const tag id3[]  = { { "TIT2", "id3 title" }, { "TPE1", "id3 artist" }, { "TRCK", "2" } };
    buf w; wav_tags t;

    wav_begin(&w); info_chunk(&w, info); data_chunk(&w); id3_chunk(&w, id3); wav_finish(&w);
    t = read_wav(&w);
    check_int("INFO then ID3: read error", t.read_err, 0);
    check_str("INFO then ID3: title",      t.title, "id3 title");
    check_str("INFO then ID3: artist",     t.artist, "id3 artist");
    check_int("INFO then ID3: track",      t.track, 2);
    check_str("INFO then ID3: genre",      t.genre, "Info Genre");

    wav_begin(&w); id3_chunk(&w, id3); data_chunk(&w); info_chunk(&w, info); wav_finish(&w);
    t = read_wav(&w);
    check_int("ID3 then INFO: read error", t.read_err, 0);
    check_str("ID3 then INFO: title",      t.title, "id3 title");
    check_str("ID3 then INFO: artist",     t.artist, "id3 artist");
    check_int("ID3 then INFO: track",      t.track, 2);
    check_str("ID3 then INFO: genre",      t.genre, "Info Genre");
}

static void test_info_items(void){
    buf w; wav_tags t;

    /* track number: ITRK, else IPRT */
    { static const tag tags[] = { { "IPRT", "3" } };
      wav_begin(&w); info_chunk(&w, tags); wav_finish(&w);
      check_int("IPRT is the track when there is no ITRK", read_wav(&w).track, 3); }
    { static const tag tags[] = { { "IPRT", "3" }, { "ITRK", "4" } };
      wav_begin(&w); info_chunk(&w, tags); wav_finish(&w);
      check_int("ITRK after IPRT wins", read_wav(&w).track, 4); }
    { static const tag tags[] = { { "ITRK", "4" }, { "IPRT", "3" } };
      wav_begin(&w); info_chunk(&w, tags); wav_finish(&w);
      check_int("ITRK before IPRT wins", read_wav(&w).track, 4); }
    { static const tag tags[] = { { "ITRK", "abc" }, { "INAM", "Song Title" } };
      wav_begin(&w); info_chunk(&w, tags); wav_finish(&w);
      t = read_wav(&w);
      check_int("junk track number: read error",       t.read_err, 0);
      check_int("junk track number: track",            t.track, 0);
      check_str("junk track number: title still read", t.title, "Song Title"); }

    /* an overlong value is cut, not overflowed */
    { char longv[400];
      memset(longv, 'a', sizeof longv - 1);
      longv[sizeof longv - 1] = 0;
      const tag tags[] = { { "INAM", longv } };
      wav_begin(&w); info_chunk(&w, tags); wav_finish(&w);
      t = read_wav(&w);
      check_int("overlong value: read error",            t.read_err, 0);
      check_int("overlong value: cut at TAGLEN-1 bytes", (int)strlen(t.title), TAGLEN - 1); }

    /* text encoding: UTF-8 when valid, else the Tag Encoding setting */
    { static const tag tags[] = { { "INAM", "Zen\xc3\xa9k" } };
      wav_begin(&w); info_chunk(&w, tags); wav_finish(&w);
      check_str("UTF-8 text kept as is", read_wav(&w).title, "Zen\xc3\xa9k"); }
    { static const tag tags[] = { { "INAM", "\xd2\xe5\xf1\xf2" } };   /* "Тест" (test) in CP1251 */
      wav_begin(&w); info_chunk(&w, tags); wav_finish(&w);
      scanner_set_old_tags(OLD_TAGS_AUTO);
      check_str("CP1251 text, Tag Encoding Auto: Cyrillic", read_wav(&w).title,
                "\xd0\xa2\xd0\xb5\xd1\x81\xd1\x82");                  /* "Тест" in UTF-8 */
      scanner_set_old_tags(OLD_TAGS_WESTERN);
      check_str("same bytes, Tag Encoding Western: Latin-1", read_wav(&w).title,
                "\xc3\x92\xc3\xa5\xc3\xb1\xc3\xb2");                  /* "Òåñò" in UTF-8 */
      scanner_set_old_tags(OLD_TAGS_AUTO); }

    /* other LIST types and an empty LIST are skipped */
    { static const tag tags[] = { { "INAM", "Song Title" } };
      buf adtl = { .n = 0 };                                             /* cue labels, no tags */
      put(&adtl, "adtl", 4);
      chunk(&adtl, "labl", "zz", 2);
      wav_begin(&w);
      chunk(&w, "LIST", adtl.b, (uint32_t)adtl.n);
      chunk(&w, "LIST", NULL, 0);
      info_chunk(&w, tags);
      wav_finish(&w);
      t = read_wav(&w);
      check_int("adtl and empty LIST skipped: read error", t.read_err, 0);
      check_str("adtl and empty LIST skipped: title",      t.title, "Song Title"); }
}

/* Chunk layouts real files have that are still valid. */
static void test_layout(void){
    buf w; wav_tags t;

    wav_begin(&w); chunk(&w, "junk", "abc", 3); info_chunk(&w, INFO_TAGS); wav_finish(&w);
    t = read_wav(&w);
    check_int("odd-sized chunk's pad byte skipped: read error", t.read_err, 0);
    check_str("odd-sized chunk's pad byte skipped: album",      t.album, "Album Name");

    wav_begin(&w); info_chunk(&w, INFO_TAGS); data_chunk(&w);              /* no wav_finish: RIFF size left 0 */
    t = read_wav(&w);
    check_int("RIFF size 0 reads to the end: read error", t.read_err, 0);
    check_str("RIFF size 0 reads to the end: album",      t.album, "Album Name");

    wav_begin(&w); info_chunk(&w, INFO_TAGS);
    memset(w.b + 4, 0xFF, 4);                                              /* RIFF size far past the file */
    t = read_wav(&w);
    check_int("RIFF size past the file clamped: read error", t.read_err, 0);
    check_str("RIFF size past the file clamped: album",      t.album, "Album Name");

    wav_begin(&w); info_chunk(&w, INFO_TAGS);
    put(&w, "data", 4); put_le32(&w, 0x7FFFFFF0); put(&w, "\0\0\0\0", 4);  /* recording cut short */
    wav_finish(&w);
    t = read_wav(&w);
    check_int("cut-short data chunk ends the walk: read error", t.read_err, 0);
    check_str("cut-short data chunk ends the walk: album",      t.album, "Album Name");
}

static void test_malformed(void){
    buf w; wav_tags t;

    wav_begin(&w); put(&w, "junk", 4); put_le32(&w, 0x7FFFFFF0);
    check_int("non-data chunk past the end: read error", read_wav(&w).read_err, 1);

    { buf list = { .n = 0 };
      put(&list, "INFO", 4); put(&list, "INAM", 4); put_le32(&list, 500); put(&list, "x", 2);
      wav_begin(&w); chunk(&w, "LIST", list.b, (uint32_t)list.n); wav_finish(&w);
      t = read_wav(&w);
      check_int("INFO item past its LIST: read error", t.read_err, 1);
      check_str("INFO item past its LIST: no title",   t.title, ""); }

    w.n = 0; put(&w, "RIFF", 4); put_le32(&w, 4); put(&w, "AVI ", 4);
    t = read_wav(&w);
    check_int("RIFF that is not WAVE: read error", t.read_err, 1);
    check_str("RIFF that is not WAVE: no title",   t.title, "");

    w.n = 0; put(&w, "RIFF", 4);
    check_int("truncated header: read error", read_wav(&w).read_err, 1);

    wav_begin(&w);
    for(int i = 0; i < 300; i++) chunk(&w, "junk", NULL, 0);
    info_chunk(&w, INFO_TAGS);
    wav_finish(&w);
    check_int("more than 256 chunks: read error", read_wav(&w).read_err, 1);
}

/* The return code tells the scanner whether to write the row (see tags_with_provenance). */
static void test_provenance(void){
    char title[TAGLEN], artist[TAGLEN], album[TAGLEN], genre[TAGLEN], album_artist[TAGLEN];
    int track, disc, title_tag, rc;
    long long dur_ms;
    buf w;
#define READ(path, name) tags_with_provenance(path, name, title, artist, album, genre, album_artist, \
                                              &track, &disc, &dur_ms, &title_tag)

    wav_begin(&w); info_chunk(&w, INFO_TAGS); data_chunk(&w); id3_chunk(&w, ID3_TAGS); wav_finish(&w);
    rc = READ(save(&w, "01 - Tagged.wav"), "01 - Tagged.wav");
    check_int("tagged WAV: rc",           rc, 1);
    check_str("tagged WAV: album",        album, "Album Name");
    check_str("tagged WAV: album artist", album_artist, "Album Artist");
    check_int("tagged WAV: track",        track, 1);
    check_int("tagged WAV: title is a tag", title_tag, 1);

    wav_begin(&w); data_chunk(&w); wav_finish(&w);
    rc = READ(save(&w, "02 - Plain.wav"), "02 - Plain.wav");
    check_int("untagged WAV: rc",                 rc, 1);
    check_str("untagged WAV: title is file name", title, "02 - Plain");
    check_str("untagged WAV: artist",             artist, "Unknown artist");
    check_int("untagged WAV: title is not a tag", title_tag, 0);

    w.n = 0; put(&w, "RIFF", 4);
    rc = READ(save(&w, "03 - Bad.wav"), "03 - Bad.wav");
    check_int("malformed WAV: rc -1 (existing tags kept)", rc, -1);
    check_str("malformed WAV: fallback title",             title, "03 - Bad");

    char missing[128];
    snprintf(missing, sizeof missing, "%s/none.wav", g_dir);
    check_int("unopenable WAV: rc 0 (existing row kept, no new row)", READ(missing, "none.wav"), 0);
#undef READ
}

int main(void){
    snprintf(g_dir, sizeof g_dir, "/tmp/wavtags.XXXXXX");
    if(!mkdtemp(g_dir)) return 2;

    printf("tag sources\n");                  test_tag_sources();
    printf("precedence\n");                   test_precedence();
    printf("INFO items\n");                   test_info_items();
    printf("layout\n");                       test_layout();
    printf("malformed\n");                    test_malformed();
    printf("through tags_with_provenance\n"); test_provenance();

    static const char *const made[] = { "t.wav", "01 - Tagged.wav", "02 - Plain.wav", "03 - Bad.wav" };
    for(size_t i = 0; i < COUNT(made); i++){
        char path[128];
        snprintf(path, sizeof path, "%s/%s", g_dir, made[i]);
        unlink(path);
    }
    rmdir(g_dir);
    printf(fails ? "FAILED: %d\n" : "all wavtags checks passed\n", fails);
    return fails ? 1 : 0;
}
