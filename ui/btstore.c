/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "btstore.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int mac_name(const char *s){   /* a storage entry named AA:BB:CC:DD:EE:FF (adapter or device) */
    if(strlen(s) != 17) return 0;
    for(int i = 0; i < 17; i++)
        if(i % 3 == 2 ? s[i] != ':' : !isxdigit((unsigned char)s[i])) return 0;
    return 1;
}

/* One device's info file, a GKeyFile. Lines are read whole (getline): Services lists every UUID on one line. */
typedef struct { char name[249], alias[249]; long cls; int has_class, has_services, sink, paired, trusted; } info_t;
static void info_read(FILE *f, info_t *in){
    char *line = NULL, section[32] = "";
    size_t cap = 0;
    ssize_t len;
    memset(in, 0, sizeof *in);
    while((len = getline(&line, &cap, f)) >= 0){
        while(len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if(line[0] == '['){
            char *end = strchr(line, ']');
            if(end){ *end = 0; snprintf(section, sizeof section, "%s", line + 1); }
            /* a link key (BR/EDR) or long-term key (LE) is what being paired means to BlueZ */
            if(!strcmp(section, "LinkKey") || !strcmp(section, "LongTermKey")) in->paired = 1;
            continue;
        }
        if(strcmp(section, "General")) continue;
        if(!strncmp(line, "Name=", 5))          snprintf(in->name, sizeof in->name, "%s", line + 5);
        else if(!strncmp(line, "Alias=", 6))    snprintf(in->alias, sizeof in->alias, "%s", line + 6);
        else if(!strncmp(line, "Trusted=", 8))  in->trusted = !strcmp(line + 8, "true");
        else if(!strncmp(line, "Class=", 6)){   in->cls = strtol(line + 6, NULL, 0); in->has_class = 1; }
        else if(!strncmp(line, "Services=", 9)){
            in->has_services = 1;
            in->sink = strstr(line + 9, "0000110b-") != NULL;   /* A2DP Audio Sink (BlueZ writes UUIDs lower-case) */
        }
    }
    free(line);
}
/* An A2DP sink by its service list; a device whose services were never resolved by its class (major Audio/Video). */
static int info_audio(const info_t *in){
    return in->has_services ? in->sink : in->has_class && ((in->cls >> 8) & 0x1f) == 0x04;
}

static int by_name(const void *a, const void *b){
    return strcasecmp(((const btstore_dev_t *)a)->name, ((const btstore_dev_t *)b)->name);
}

int btstore_paired_audio(const char *root, btstore_dev_t *out, int cap){
    int n = 0;
    DIR *top = opendir(root);
    if(!top) return 0;
    struct dirent *a;
    while((a = readdir(top))){
        if(!mac_name(a->d_name)) continue;                  /* adapters; skips settings, cache and friends */
        char dir[512];
        snprintf(dir, sizeof dir, "%s/%.17s", root, a->d_name);   /* mac_name(): 17 characters */
        DIR *ad = opendir(dir);
        if(!ad) continue;
        struct dirent *d;
        while((d = readdir(ad)) && n < cap){
            if(!mac_name(d->d_name)) continue;
            int seen = 0;                                   /* one device under two adapters lists once */
            for(int i = 0; i < n; i++) if(!strcasecmp(out[i].mac, d->d_name)) seen = 1;
            if(seen) continue;
            char path[600];
            snprintf(path, sizeof path, "%s/%.17s/info", dir, d->d_name);
            FILE *f = fopen(path, "r");
            if(!f) continue;
            info_t in;
            info_read(f, &in);
            fclose(f);
            if(!in.paired || !info_audio(&in)) continue;
            snprintf(out[n].mac, sizeof out[n].mac, "%.17s", d->d_name);
            snprintf(out[n].name, sizeof out[n].name, "%s", in.alias[0] ? in.alias : in.name[0] ? in.name : out[n].mac);
            out[n].trusted = in.trusted;
            n++;
        }
        closedir(ad);
    }
    closedir(top);
    qsort(out, (size_t)n, sizeof *out, by_name);
    return n;
}
