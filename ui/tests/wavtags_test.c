/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Off-device host test for the scanner's WAV tag reader (wav_read, through tags_with_provenance too). scanner.c is
 * compiled into this test so its static readers are reachable; everything they don't use is dropped at link time
 * (tests/Makefile), so no SQLite or UI is linked. Every WAV is built here byte by byte. Built under ASan/UBSan. */
#include "../scanner.c"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

/* ---- WAV builder ---- */
typedef struct { unsigned char b[1 << 16]; size_t n; } buf;
static void put(buf *w, const void *p, size_t n){ memcpy(w->b + w->n, p, n); w->n += n; }
static void le32w(buf *w, uint32_t v){ unsigned char c[4] = { v, v >> 8, v >> 16, v >> 24 }; put(w, c, 4); }
/* a chunk: id, size, body (len bytes; NULL = zeros), pad byte when len is odd */
static void chunk(buf *w, const char *id, const void *body, uint32_t len){
    put(w, id, 4); le32w(w, len);
    if(body) put(w, body, len); else { memset(w->b + w->n, 0, len); w->n += len; }
    if(len & 1) w->b[w->n++] = 0;
}
static void wav_begin(buf *w){
    w->n = 0; put(w, "RIFF", 4); le32w(w, 0); put(w, "WAVE", 4);
    static const unsigned char fmt[16] = { 1,0, 2,0, 0x44,0xAC,0,0, 0x10,0xB1,2,0, 4,0, 16,0 };   /* PCM 44.1 kHz stereo 16-bit */
    chunk(w, "fmt ", fmt, 16);
}
static void wav_size(buf *w){ uint32_t v = (uint32_t)w->n - 8; memcpy(w->b + 4, (unsigned char[4]){ v, v >> 8, v >> 16, v >> 24 }, 4); }
/* LIST/INFO from id,value pairs (values written as given, NUL-terminated like rippers write them) */
static void info(buf *w, const char *const *kv, int n){
    buf l; l.n = 0; put(&l, "INFO", 4);
    for(int i = 0; i < n; i += 2) chunk(&l, kv[i], kv[i + 1], (uint32_t)strlen(kv[i + 1]) + 1);
    chunk(w, "LIST", l.b, (uint32_t)l.n);
}
/* an ID3v2.3 tag of UTF-16 (BOM) text frames from id,value pairs (ASCII values) */
static void id3(buf *w, const char *const *kv, int n){
    buf t; t.n = 10; memcpy(t.b, "ID3\3\0\0", 6);
    for(int i = 0; i < n; i += 2){
        size_t vl = strlen(kv[i + 1]); uint32_t fs = 1 + 2 + 2 * (uint32_t)vl;
        put(&t, kv[i], 4); unsigned char be[6] = { fs >> 24, fs >> 16, fs >> 8, fs, 0, 0 }; put(&t, be, 6);
        unsigned char e[3] = { 1, 0xFF, 0xFE }; put(&t, e, 3);
        for(size_t k = 0; k < vl; k++){ unsigned char u[2] = { (unsigned char)kv[i + 1][k], 0 }; put(&t, u, 2); }
    }
    uint32_t sz = (uint32_t)t.n - 10;
    unsigned char ss[4] = { (sz >> 21) & 0x7F, (sz >> 14) & 0x7F, (sz >> 7) & 0x7F, sz & 0x7F };
    memcpy(t.b + 6, ss, 4);
    chunk(w, "id3 ", t.b, (uint32_t)t.n);
}

/* ---- running the reader ---- */
static char g_dir[64];
typedef struct { char title[TAGLEN], artist[TAGLEN], album[TAGLEN], genre[TAGLEN], aa[TAGLEN]; int track, disc, rerr; } got;
static const char *save(const buf *w, const char *name){
    static char p[128]; snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *f = fopen(p, "wb"); fwrite(w->b, 1, w->n, f); fclose(f);
    return p;
}
static got read_wav(const buf *w){
    got g; memset(&g, 0, sizeof g);
    FILE *f = fopen(save(w, "t.wav"), "rb");
    wav_read(f, g.title, g.artist, g.album, g.genre, g.aa, &g.track, &g.disc, &g.rerr);
    fclose(f);
    return g;
}

int main(void){
    snprintf(g_dir, sizeof g_dir, "/tmp/wavtags.XXXXXX");
    if(!mkdtemp(g_dir)) return 2;
    static const unsigned char pcm[64];
    static const char *const INFO_TAGS[] = { "ISFT","gvfs-cdda", "ITRK","1", "INAM","Take Me", "IPRD","Mint Jams",
                                              "IART","Casiopea", "IGNR","Jazz Fusion" };
    static const char *const ID3_TAGS[] = { "TIT2","Take Me", "TPE1","Casiopea", "TRCK","1/7", "TALB","Mint Jams",
                                             "TPOS","1/1", "TCON","Jazz Fusion", "TPE2","Casiopea" };
    buf w; got g;

    printf("tag sources\n");
    wav_begin(&w); info(&w, INFO_TAGS, 12); chunk(&w, "data", pcm, 64); wav_size(&w); g = read_wav(&w);
    CHECK(!g.rerr && !strcmp(g.title, "Take Me") && !strcmp(g.artist, "Casiopea") && !strcmp(g.album, "Mint Jams")
          && !strcmp(g.genre, "Jazz Fusion") && g.track == 1 && !g.aa[0] && !g.disc, "LIST/INFO only: every INFO field");
    wav_begin(&w); chunk(&w, "data", pcm, 64); id3(&w, ID3_TAGS, 14); wav_size(&w); g = read_wav(&w);
    CHECK(!g.rerr && !strcmp(g.title, "Take Me") && !strcmp(g.artist, "Casiopea") && !strcmp(g.album, "Mint Jams")
          && !strcmp(g.genre, "Jazz Fusion") && !strcmp(g.aa, "Casiopea") && g.track == 1 && g.disc == 1,
          "ID3 chunk after data: UTF-16 frames, album artist, track and disc");

    printf("precedence\n");
    static const char *const INFO_DIFF[] = { "INAM","info title", "IART","info artist", "IGNR","Info Genre", "ITRK","9" };
    static const char *const ID3_PART[] = { "TIT2","id3 title", "TPE1","id3 artist", "TRCK","2" };
    for(int order = 0; order < 2; order++){
        wav_begin(&w);
        if(order == 0){ info(&w, INFO_DIFF, 8); chunk(&w, "data", pcm, 64); id3(&w, ID3_PART, 6); }
        else          { id3(&w, ID3_PART, 6); chunk(&w, "data", pcm, 64); info(&w, INFO_DIFF, 8); }
        wav_size(&w); g = read_wav(&w);
        CHECK(!g.rerr && !strcmp(g.title, "id3 title") && !strcmp(g.artist, "id3 artist") && g.track == 2,
              order ? "ID3 before INFO: ID3 wins" : "INFO before ID3: ID3 wins");
        CHECK(!strcmp(g.genre, "Info Genre"), order ? "ID3 before INFO: INFO fills the gap" : "INFO before ID3: INFO fills the gap");
    }

    printf("INFO details\n");
    { static const char *const T[] = { "IPRT","3" };
      wav_begin(&w); info(&w, T, 2); wav_size(&w); g = read_wav(&w);
      CHECK(g.track == 3, "IPRT is the track when there is no ITRK"); }
    { static const char *const T1[] = { "IPRT","3", "ITRK","4" }, *const T2[] = { "ITRK","4", "IPRT","3" };
      wav_begin(&w); info(&w, T1, 4); wav_size(&w); g = read_wav(&w); CHECK(g.track == 4, "ITRK after IPRT wins");
      wav_begin(&w); info(&w, T2, 4); wav_size(&w); g = read_wav(&w); CHECK(g.track == 4, "ITRK before IPRT wins"); }
    { static const char *const T[] = { "ITRK","abc", "INAM","x" };
      wav_begin(&w); info(&w, T, 4); wav_size(&w); g = read_wav(&w);
      CHECK(!g.rerr && g.track == 0 && !strcmp(g.title, "x"), "junk track number -> 0"); }
    { char longv[400]; memset(longv, 'a', sizeof longv - 1); longv[sizeof longv - 1] = 0;
      const char *T[] = { "INAM", longv };
      wav_begin(&w); info(&w, T, 2); wav_size(&w); g = read_wav(&w);
      CHECK(!g.rerr && strlen(g.title) == TAGLEN - 1, "an overlong value is cut at TAGLEN-1"); }
    { static const char *const T[] = { "INAM","Zen\xc3\xa9k" };
      wav_begin(&w); info(&w, T, 2); wav_size(&w); g = read_wav(&w);
      CHECK(!strcmp(g.title, "Zen\xc3\xa9k"), "UTF-8 text kept as is"); }
    { static const char *const T[] = { "INAM","\xca\xe8\xed\xee" };   /* "Kino" in CP1251 */
      wav_begin(&w); info(&w, T, 2); wav_size(&w);
      scanner_set_old_tags(OLD_TAGS_AUTO);     g = read_wav(&w);
      CHECK(!strcmp(g.title, "\xd0\x9a\xd0\xb8\xd0\xbd\xd0\xbe"), "legacy CP1251 text, Tag Encoding Auto -> Cyrillic");
      scanner_set_old_tags(OLD_TAGS_WESTERN);  g = read_wav(&w);
      CHECK(!strcmp(g.title, "\xc3\x8a\xc3\xa8\xc3\xad\xc3\xae"), "same bytes, Tag Encoding Western -> Latin-1");
      scanner_set_old_tags(OLD_TAGS_AUTO); }
    { static const char *const T[] = { "INAM","x" };
      buf l; l.n = 0; put(&l, "adtl", 4); chunk(&l, "labl", "zz", 2);
      wav_begin(&w); chunk(&w, "LIST", l.b, (uint32_t)l.n); chunk(&w, "LIST", NULL, 0); info(&w, T, 2); wav_size(&w);
      g = read_wav(&w);
      CHECK(!g.rerr && !strcmp(g.title, "x"), "non-INFO and empty LIST chunks are skipped"); }

    printf("layout\n");
    wav_begin(&w); chunk(&w, "junk", "abc", 3); info(&w, INFO_TAGS, 12); wav_size(&w); g = read_wav(&w);
    CHECK(!g.rerr && !strcmp(g.album, "Mint Jams"), "an odd-sized chunk's pad byte is skipped");
    wav_begin(&w); info(&w, INFO_TAGS, 12); chunk(&w, "data", pcm, 64); g = read_wav(&w);       /* RIFF size left 0 */
    CHECK(!g.rerr && !strcmp(g.album, "Mint Jams"), "RIFF size 0 (unset) reads to the end of the file");
    wav_begin(&w); info(&w, INFO_TAGS, 12); w.b[4] = w.b[5] = w.b[6] = w.b[7] = 0xFF; g = read_wav(&w);
    CHECK(!g.rerr && !strcmp(g.album, "Mint Jams"), "RIFF size past the file is clamped to it");
    wav_begin(&w); info(&w, INFO_TAGS, 12); put(&w, "data", 4); le32w(&w, 0x7FFFFFF0); put(&w, pcm, 64); wav_size(&w);
    g = read_wav(&w);
    CHECK(!g.rerr && !strcmp(g.album, "Mint Jams"), "a cut-short data chunk ends the walk without an error");

    printf("malformed\n");
    wav_begin(&w); put(&w, "junk", 4); le32w(&w, 0x7FFFFFF0); g = read_wav(&w);
    CHECK(g.rerr, "a non-data chunk past the end is an error");
    { buf l; l.n = 0; put(&l, "INFO", 4); put(&l, "INAM", 4); le32w(&l, 500); put(&l, "x", 2);
      wav_begin(&w); chunk(&w, "LIST", l.b, (uint32_t)l.n); wav_size(&w); g = read_wav(&w);
      CHECK(g.rerr && !g.title[0], "an INFO item past its LIST is an error"); }
    w.n = 0; put(&w, "RIFF", 4); le32w(&w, 4); put(&w, "AVI ", 4); g = read_wav(&w);
    CHECK(g.rerr && !g.title[0], "a RIFF that is not WAVE is refused");
    w.n = 0; put(&w, "RIFF", 4); g = read_wav(&w);
    CHECK(g.rerr, "a truncated header is refused");
    wav_begin(&w); for(int i = 0; i < 300; i++) chunk(&w, "junk", NULL, 0); info(&w, INFO_TAGS, 12); wav_size(&w);
    g = read_wav(&w);
    CHECK(g.rerr, "more than 256 chunks stops the walk with an error");

    printf("through tags_with_provenance\n");
    { char t[TAGLEN], a[TAGLEN], al[TAGLEN], ge[TAGLEN], aa[TAGLEN]; int tr, d, tt; long long ms; int rc;
      wav_begin(&w); info(&w, INFO_TAGS, 12); chunk(&w, "data", pcm, 64); id3(&w, ID3_TAGS, 14); wav_size(&w);
      rc = tags_with_provenance(save(&w, "01 - Take Me.wav"), "01 - Take Me.wav", t, a, al, ge, aa, &tr, &d, &ms, &tt);
      CHECK(rc == 1 && !strcmp(al, "Mint Jams") && !strcmp(aa, "Casiopea") && tr == 1 && tt == 1, "tagged WAV: real tags, rc 1");
      wav_begin(&w); chunk(&w, "data", pcm, 64); wav_size(&w);
      rc = tags_with_provenance(save(&w, "02 - Plain.wav"), "02 - Plain.wav", t, a, al, ge, aa, &tr, &d, &ms, &tt);
      CHECK(rc == 1 && !strcmp(t, "02 - Plain") && !strcmp(a, "Unknown artist") && tt == 0, "untagged WAV: file name, rc 1");
      w.n = 0; put(&w, "RIFF", 4);
      rc = tags_with_provenance(save(&w, "03 - Bad.wav"), "03 - Bad.wav", t, a, al, ge, aa, &tr, &d, &ms, &tt);
      CHECK(rc == -1 && !strcmp(t, "03 - Bad"), "malformed WAV: fallback title, rc -1 (existing tags kept)");
      char missing[128]; snprintf(missing, sizeof missing, "%s/none.wav", g_dir);
      rc = tags_with_provenance(missing, "none.wav", t, a, al, ge, aa, &tr, &d, &ms, &tt);
      CHECK(rc == 0, "unopenable WAV: rc 0 (existing row kept, no new row)"); }

    static const char *const made[] = { "t.wav", "01 - Take Me.wav", "02 - Plain.wav", "03 - Bad.wav" };
    for(unsigned i = 0; i < sizeof made / sizeof made[0]; i++){
        char p[128]; snprintf(p, sizeof p, "%s/%s", g_dir, made[i]); unlink(p);
    }
    rmdir(g_dir);
    printf(fails ? "FAILED: %d\n" : "all wavtags checks passed\n", fails);
    return fails ? 1 : 0;
}
