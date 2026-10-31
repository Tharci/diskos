/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "volkeys_hold.h"
#include <string.h>

void vkh_init(vkh_t *k, unsigned held, uint32_t hold_ms, uint32_t repeat_ms){
    memset(k, 0, sizeof *k);
    k->hold_ms = hold_ms; k->repeat_ms = repeat_ms;
    k->down = held & 3u;
    k->chord = k->down != 0;   /* the chord rule already means "no steps until both are up" */
}
int vkh_feed(vkh_t *k, uint32_t now, unsigned held, int *chord){
    unsigned pressed = 0;
    *chord = 0;
    for(int i = 0; i < 2; i++){
        unsigned bit = 1u << i;
        if((held & bit) == (k->down & bit)){ k->run[i] = 0; continue; }
        if(++k->run[i] < VKH_DEBOUNCE) continue;
        k->run[i] = 0; k->down ^= bit;
        if(held & bit) pressed |= bit;
    }
    if(k->down == 3u){
        if(!k->both){ k->both = 1; k->since = now; }   /* the chord counts from when BOTH are down, every time */
        k->chord = 1;
    } else k->both = 0;
    if(k->chord){
        /* Letting go of one button first must not start a ramp on the other, so steps wait until both are up. */
        if(!k->down){ k->chord = 0; k->reported = 0; }
        else if(k->both && !k->reported && now - k->since >= VKH_CHORD_MS){ k->reported = 1; *chord = 1; }
        return 0;
    }
    int steps = 0;
    for(int i = 0; i < 2; i++){
        unsigned bit = 1u << i;
        int dir = i ? -1 : 1;
        if(!(k->down & bit)) continue;
        if(pressed & bit){ steps += dir; k->next[i] = now + k->hold_ms; }
        else if((int32_t)(now - k->next[i]) >= 0){ steps += dir; k->next[i] = now + k->repeat_ms; }   /* no catch-up after a stall */
    }
    return steps;
}
