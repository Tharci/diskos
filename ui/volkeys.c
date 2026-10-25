/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Volume keys read from the pins: one step per press, repeats on a hold (see volkeys.h for the stock facts). */
#include "volkeys.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "controls.h"
#include "fwcaps.h"
#include "volkeys_hold.h"
#include "ipc.h"
#include "screens.h"

#define VK_POLL_MS 10          /* sampling while the keys are owned or an app runs */
#define VK_WATCH_MS 200        /* keys not owned and no app: the stock player has the volume, nothing acts on the
                                * samples, so the button state is only kept current for when an app starts */
#define VK_ECHO_MS 600         /* an a714 this soon after a key change may be the echo of our own 0715 */
#define VK_SEND_MS 60          /* at most one 0715 per this while a key ramps; the last level is always sent */

static volatile uint32_t *g_pin;               /* GPB PxPIN; NULL = no /dev/mem map */
static int g_fd = -1;                          /* event0, grabbed; the key thread's alone once started */
static _Atomic int g_owned, g_app, g_remote, g_step = VK_STEP_DEFAULT, g_max = VOL_MAX;
static _Atomic int g_hold = VK_HOLD_DEFAULT, g_repeat = VK_REPEAT_DEFAULT;
static _Atomic int g_escape, g_power, g_remote_steps, g_shown_level;
static _Atomic unsigned g_shown_seq;
static _Atomic uint32_t g_changed;             /* tick of the last local key change */
static _Atomic int g_changed_any;

/* The kernel's 32-bit input_event (timeval is 2 x 32 bits whatever musl's time_t is). */
typedef struct { uint32_t sec, usec; uint16_t type, code; int32_t value; } vk_event;

static uint32_t vk_now(void){
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)t.tv_sec * 1000u + (uint32_t)(t.tv_nsec / 1000000);
}
static unsigned vk_held(void){ uint32_t v = *g_pin; return (v >> 13 & 1u ? 0 : 1u) | (v >> 14 & 1u ? 0 : 2u); }
static int vk_volume_code(unsigned code){
    return code == 251 || code == 252 || code == 262 || code == 263 || code == 266 || code == 267;
}
static int vk_open_keys(void){
    for(int i = 0; i < 8; i++){
        char path[64], name[32];
        snprintf(path, sizeof path, "/sys/class/input/event%d/device/name", i);
        FILE *f = fopen(path, "r"); if(!f) continue;
        int got = fgets(name, sizeof name, f) != NULL; fclose(f);
        if(!got || strncmp(name, "x2000_key", 9) || (name[9] && name[9] != '\n')) continue;
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        return open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    }
    return -1;
}
static void vk_lose(void){
    atomic_store(&g_owned, 0);   /* the stock keys are back: stepping too would double every press */
    if(g_fd >= 0){ close(g_fd); g_fd = -1; }
    fprintf(stderr, "volume keys: grab lost, the player has the keys again\n");
}

/* Hand one key to the player. The grab must be off for the write to reach other readers, and the input core drops
 * a key event that doesn't change the key's state: the real press already set it, so a release goes first. The
 * player ignores value 0 on these keys. We read the injected press back ourselves; echo[] skips it. */
static unsigned char g_echo[KEY_CNT];
static void vk_forward(unsigned code){
    vk_event e[6]; memset(e, 0, sizeof e);
    for(int i = 0; i < 6; i += 2){
        e[i].type = EV_KEY; e[i].code = (uint16_t)code; e[i].value = i == 2;
        e[i + 1].type = EV_SYN; e[i + 1].code = SYN_REPORT;
    }
    ioctl(g_fd, EVIOCGRAB, 0);
    int ok = write(g_fd, e, sizeof e) == (ssize_t)sizeof e;
    if(ioctl(g_fd, EVIOCGRAB, 1) != 0){ vk_lose(); return; }
    if(ok && code < KEY_CNT && g_echo[code] < 255) g_echo[code]++;
    if(!ok) fprintf(stderr, "volume keys: could not hand key %u to the player\n", code);
}
static void vk_drain(void){
    vk_event ev[16];
    for(;;){
        ssize_t n = read(g_fd, ev, sizeof ev);
        if(n < 0 && (errno == EAGAIN || errno == EINTR)) return;
        if(n <= 0 || n % (ssize_t)sizeof ev[0]){ vk_lose(); return; }
        for(size_t i = 0; i < (size_t)n / sizeof ev[0]; i++){
            unsigned code = ev[i].code;
            if(ev[i].type != EV_KEY || ev[i].value != 1 || vk_volume_code(code)) continue;
            if(code < KEY_CNT && g_echo[code]){ g_echo[code]--; continue; }
            vk_forward(code);
            if(g_fd < 0) return;
        }
    }
}

/* The level the player has: its last a714, or before any a714 the level it restored at boot. -1 = unknown. */
static int vk_reported(void){
    track_state_t st; ipc_get_state(&st);
    if(st.volume_seq) return st.volume;
    return ctl_read_volume(CTL_SYSCONFIG);
}
/* Local steps are sent from here, so they work while an app has the screen and the UI loop is parked. */
static void vk_local(int steps, uint32_t now){
    static int target = -1, pending;
    static uint32_t sent;
    if(steps){
        int recent = atomic_load(&g_changed_any) && now - atomic_load(&g_changed) < VK_ECHO_MS;
        int base = recent && target >= 0 ? target : vk_reported();
        if(base < 0) return;   /* better no step than a jump from a guessed level */
        int max = atomic_load(&g_max), level = base + steps * atomic_load(&g_step);
        target = level < 0 ? 0 : level > max ? max : level;
        pending = 1;
        atomic_store(&g_changed, now); atomic_store(&g_changed_any, 1);
        atomic_store(&g_shown_level, target); atomic_fetch_add(&g_shown_seq, 1);
    }
    if(pending && now - sent >= VK_SEND_MS){ ui_set_volume(target); sent = now; pending = 0; }
}

static void *vk_thread(void *arg){
    (void)arg;
    vkh_t core; vkh_init(&core, vk_held(), VK_HOLD_DEFAULT, VK_REPEAT_DEFAULT);
    for(;;){
        int owned = atomic_load(&g_owned);
        if(owned){
            struct pollfd p = { g_fd, POLLIN, 0 };
            if(poll(&p, 1, VK_POLL_MS) > 0) vk_drain();
            if(g_fd >= 0 && atomic_exchange(&g_power, 0)) vk_forward(265);
            owned = atomic_load(&g_owned);
        } else usleep(1000 * (atomic_load(&g_app) ? VK_POLL_MS : VK_WATCH_MS));
        core.hold_ms = (uint32_t)atomic_load(&g_hold); core.repeat_ms = (uint32_t)atomic_load(&g_repeat);
        int escape, steps = vkh_feed(&core, vk_now(), vk_held(), &escape);
        if(escape) atomic_store(&g_escape, 1);
        if(!owned) continue;
        if(atomic_load(&g_remote)){
            if(steps && !atomic_load(&g_app)) atomic_fetch_add(&g_remote_steps, steps);   /* an app parks the UI loop: drop, don't queue */
        } else vk_local(steps, vk_now());
    }
    return NULL;
}

void volkeys_start(void){
    static int started;
    if(started) return;
    started = 1;
    int mem = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC);
    if(mem >= 0){
        void *map = mmap(NULL, 4096, PROT_READ, MAP_SHARED, mem, 0x10010000);
        if(map != MAP_FAILED) g_pin = (volatile uint32_t *)((char *)map + 0x100);
        close(mem);
    }
    if(!g_pin){ fprintf(stderr, "volume keys: no pin access, stock keys and no app force-close\n"); return; }
    if(fw_os_ver() == 257 && (g_fd = vk_open_keys()) >= 0){
        if(ioctl(g_fd, EVIOCGRAB, 1) == 0) atomic_store(&g_owned, 1);
        else { close(g_fd); g_fd = -1; }
    }
    fprintf(stderr, "volume keys: %s\n", atomic_load(&g_owned) ? "owned by diskOS" : "stock (no grab on this firmware)");
    pthread_t t;
    if(pthread_create(&t, NULL, vk_thread, NULL) != 0){
        if(g_fd >= 0){ close(g_fd); g_fd = -1; }   /* closing drops the grab: the player has the keys */
        atomic_store(&g_owned, 0); g_pin = NULL;
        fprintf(stderr, "volume keys: no thread, stock keys\n");
        return;
    }
    pthread_detach(t);
}
int volkeys_owned(void){ return atomic_load(&g_owned); }
int volkeys_escape_ready(void){ return g_pin != NULL; }
void volkeys_publish(int remote, int hold_ms, int repeat_ms, int step, int max_vol){
    atomic_store(&g_remote, remote);
    atomic_store(&g_step, step < 1 ? 1 : step > 5 ? 5 : step);
    atomic_store(&g_hold, hold_ms < 100 ? 100 : hold_ms > 2000 ? 2000 : hold_ms);
    atomic_store(&g_repeat, repeat_ms < VK_POLL_MS ? VK_POLL_MS : repeat_ms > 1000 ? 1000 : repeat_ms);   /* finer than a sample can't happen */
    atomic_store(&g_max, max_vol < 0 ? 0 : max_vol > VOL_MAX ? VOL_MAX : max_vol);
}
void volkeys_set_app(int running){ if(running) atomic_store(&g_escape, 0); atomic_store(&g_app, running); }
int volkeys_take_escape(void){ return atomic_exchange(&g_escape, 0); }
int volkeys_take_remote(void){ return atomic_exchange(&g_remote_steps, 0); }
int volkeys_take_shown(int *level){
    static unsigned seen;
    unsigned seq = atomic_load(&g_shown_seq);
    if(seq == seen) return 0;
    seen = seq; *level = atomic_load(&g_shown_level);
    return 1;
}
int volkeys_echo_window(void){ return atomic_load(&g_changed_any) && vk_now() - atomic_load(&g_changed) < VK_ECHO_MS; }
int volkeys_press_power(void){
    if(!atomic_load(&g_owned)) return 0;
    atomic_store(&g_power, 1);
    return 1;
}
