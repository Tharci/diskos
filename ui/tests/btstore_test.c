/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Off-device host test for btstore.c: a BlueZ storage tree is built in a temp dir, shaped like the Disc's
 * /usr/data/storebluetooth/lib/bluetooth (adapter dir, a settings file, per-device dirs with info files). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "btstore.h"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

static char root[64];
static void put(const char *rel, const char *text){
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    for(char *p = path + strlen(root) + 1; *p; p++) if(*p == '/'){ *p = 0; mkdir(path, 0700); *p = '/'; }
    FILE *f = fopen(path, "w");
    if(!f){ perror(path); exit(2); }
    fputs(text, f);
    fclose(f);
}
#define SINK "00001108-0000-1000-8000-00805f9b34fb;0000110b-0000-1000-8000-00805f9b34fb;"
#define AD "40:D9:5A:E3:1B:07/"

int main(void){
    snprintf(root, sizeof root, "/tmp/diskos-btstore-XXXXXX");
    if(!mkdtemp(root)){ perror("mkdtemp"); return 2; }
    btstore_dev_t d[8];

    CHECK(btstore_paired_audio("/nonexistent/btstore", d, 8) == 0, "missing root lists nothing");

    put(AD "settings", "[General]\nDiscoverable=false\nAlias=SNOWSKY DISC\n");
    put(AD "cache/30:82:16:AB:38:A0", "[General]\nName=stale cache entry\n");
    /* paired + trusted A2DP sink with a user alias, CRLF line ends */
    put(AD "30:82:16:AB:38:A0/info", "[General]\r\nName=AirPods Pro\r\nAlias=Pods\r\nClass=0x240418\r\nTrusted=true\r\n"
                                     "Services=" SINK "\r\n[LinkKey]\r\nKey=00\r\n");
    /* paired, untrusted; its services were never resolved, so its Audio/Video class decides */
    put(AD "94:DB:56:54:B7:37/info", "[General]\nName=wh-1000xm3\nClass=0x240404\nTrusted=false\n[LinkKey]\nKey=00\n");
    /* paired phone: an audio SOURCE (110a), not a sink */
    put(AD "AA:BB:CC:DD:EE:01/info", "[General]\nName=Phone\nClass=0x5a020c\nServices=0000110a-0000-1000-8000-00805f9b34fb;\n"
                                     "[LinkKey]\nKey=00\n");
    /* discovered once, never paired */
    put(AD "AA:BB:CC:DD:EE:02/info", "[General]\nName=Shop speaker\nClass=0x240414\nServices=" SINK "\n");
    /* LE-only paired sink (long-term key), no name: falls back to its address */
    put(AD "AA:BB:CC:DD:EE:03/info", "[General]\nServices=" SINK "\n[LongTermKey]\nKey=00\n");
    /* a Services line far longer than any fixed line buffer, the sink UUID at its end */
    { char big[9000] = "[General]\nName=Big list\nServices=";
      for(int i = 0; i < 200; i++) strcat(big, "0000aaaa-0000-1000-8000-00805f9b34fb;");
      strcat(big, "0000110b-0000-1000-8000-00805f9b34fb;\n[LinkKey]\nKey=00\n");
      put(AD "AA:BB:CC:DD:EE:04/info", big); }
    put(AD "not-a-mac/info", "[General]\nName=junk\n[LinkKey]\n");

    int n = btstore_paired_audio(root, d, 8);
    CHECK(n == 4, "paired sinks only: no phone, no unpaired speaker, no junk");
    if(n == 4){
        CHECK(!strcmp(d[0].mac, "AA:BB:CC:DD:EE:03") && !strcmp(d[0].name, "AA:BB:CC:DD:EE:03"), "no name: the address");
        CHECK(!strcmp(d[1].name, "Big list"), "the sink UUID at the end of a long Services line counts");
        CHECK(!strcmp(d[2].name, "Pods") && d[2].trusted, "the alias wins over the name; trusted read");
        CHECK(!strcmp(d[3].name, "wh-1000xm3") && !d[3].trusted, "class decides without services; untrusted");
    }

    /* a second adapter recording the same device lists it once */
    put("40:D9:5A:E3:1B:08/30:82:16:AB:38:A0/info", "[General]\nName=AirPods Pro\nServices=" SINK "\n[LinkKey]\n");
    CHECK(btstore_paired_audio(root, d, 8) == 4, "one device under two adapters lists once");
    CHECK(btstore_paired_audio(root, d, 2) == 2, "the cap holds");

    char cmd[96];
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    if(system(cmd) != 0) printf("  (could not remove %s)\n", root);
    if(fails){ printf("%d btstore check(s) FAILED\n", fails); return 1; }
    puts("all btstore checks passed");
    return 0;
}
