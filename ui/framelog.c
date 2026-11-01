/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Player traffic capture (capture build only: -DDISKOS_FRAME_LOG). Every frame received on /ui and every send to
 * /player, byte-exact, one line each, in FRAMELOG_PATH:
 *   <CLOCK_MONOTONIC s> rx <bytes> <frame>
 *   <CLOCK_MONOTONIC s> tx <bytes> <ok|errno=N|no-queue> <user|probe|automatic> <frame>
 * Printable ASCII is kept, a backslash is written as \\ and every other byte as \xNN, so a NUL or a stray trailing
 * byte stays visible. The clock matches /proc/uptime on the Disc (it never suspends), so lines added from a shell
 * with that clock sort into place. The file is in RAM and stops growing at FRAMELOG_MAX bytes. */
#ifdef DISKOS_FRAME_LOG
#include "framelog.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#ifndef FRAMELOG_PATH
#define FRAMELOG_PATH "/tmp/diskos_frames.log"
#endif
#define FRAMELOG_MAX (8u * 1024u * 1024u)
#define LINE_MAX_BYTES (4u * 8200u + 128u)   /* an 8200-byte frame, every byte escaped, plus the prefix */

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_fd = -1;
static size_t g_written;
static int g_full;
static char g_line[LINE_MAX_BYTES];

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void emit(size_t len) {
    if (g_full) {
        return;
    }
    if (g_fd < 0) {
        g_fd = open(FRAMELOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (g_fd < 0) {
            g_full = 1;
            return;
        }
    }
    if (g_written + len > FRAMELOG_MAX) {
        static const char stop[] = "# frame log full: capture stopped\n";
        (void)!write(g_fd, stop, sizeof stop - 1);
        g_full = 1;
        return;
    }
    if (write(g_fd, g_line, len) == (ssize_t)len) {
        g_written += len;
    }
}

static size_t escape(size_t at, const char *frame, size_t n) {
    for (size_t i = 0; i < n && at + 5 < sizeof g_line; i++) {
        unsigned char c = (unsigned char)frame[i];
        if (c == '\\') {
            g_line[at++] = '\\';
            g_line[at++] = '\\';
        } else if (c >= 0x20 && c < 0x7f) {
            g_line[at++] = (char)c;
        } else {
            at += (size_t)snprintf(g_line + at, sizeof g_line - at, "\\x%02x", c);
        }
    }
    g_line[at++] = '\n';
    return at;
}

void framelog_rx(const char *frame, size_t n) {
    pthread_mutex_lock(&g_mu);
    int head = snprintf(g_line, sizeof g_line, "%.3f rx %zu ", now_s(), n);
    emit(escape((size_t)head, frame, n));
    pthread_mutex_unlock(&g_mu);
}

void framelog_tx(const char *frame, size_t n, int err, const char *policy) {
    pthread_mutex_lock(&g_mu);
    char result[24];
    if (err > 0) {
        snprintf(result, sizeof result, "errno=%d", err);
    } else {
        snprintf(result, sizeof result, "%s", err < 0 ? "no-queue" : "ok");
    }
    int head = snprintf(g_line, sizeof g_line, "%.3f tx %zu %s %s ", now_s(), n, result, policy);
    emit(escape((size_t)head, frame, n));
    pthread_mutex_unlock(&g_mu);
}
#endif
