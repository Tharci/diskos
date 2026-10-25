/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Off-device host test for volkeys_hold.c, the volume keys' press/hold state machine. Samples arrive every 10 ms, as
 * volkeys.c feeds them on the device; bit 0 is Vol+, bit 1 Vol-. */
#include <stdio.h>
#include "volkeys_hold.h"

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("  FAIL: %s\n", m); fails++; } }while(0)

#define TICK 10
static vkh_t c;
static uint32_t now;
static int escapes;
/* a fixed 400 ms hold / 80 ms repeat, so the step counts below stay easy to follow; the defaults have their own case */
static void reset(uint32_t start){ vkh_init(&c, 0, 400, 80); now = start; escapes = 0; }
/* `held` for `ms`, one sample per TICK; returns the steps over that time */
static int run(unsigned held, unsigned ms){
    int steps = 0, esc;
    for(unsigned t = 0; t < ms; t += TICK){ steps += vkh_feed(&c, now, held, &esc); escapes += esc; now += TICK; }
    return steps;
}

int main(void){
    /* 1. a tap is one step, in its direction, at the second pressed sample */
    { reset(1000); int esc;
      CHECK(vkh_feed(&c, now, 1, &esc) == 0, "first pressed sample: not confirmed yet"); now += TICK;
      CHECK(vkh_feed(&c, now, 1, &esc) == 1, "second pressed sample: Vol+ steps at once"); now += TICK;
      CHECK(run(1, 60) == 0, "a short hold adds nothing");
      CHECK(run(0, 100) == 0, "release adds nothing");
      CHECK(run(2, 50) == -1, "a Vol- tap steps down"); run(0, 50); }

    /* 2. fast taps all count: 10 taps of 30 ms down / 30 ms up */
    { reset(0); int steps = 0;
      for(int i = 0; i < 10; i++){ steps += run(1, 30); steps += run(0, 30); }
      CHECK(steps == 10, "10 fast taps = 10 steps"); }

    /* 3. a one-sample glitch is not a press, and a one-sample dropout is not a release */
    { reset(0);
      CHECK(run(1, TICK) + run(0, 50) == 0, "a single pressed sample is noise");
      int steps = run(1, 50); steps += run(0, TICK); steps += run(1, 50); steps += run(0, 50);
      CHECK(steps == 1, "a single released sample inside a press doesn't make a second press"); }

    /* 4. a hold repeats: confirmed at 10 ms, then 400 ms, then every 80 ms -> 1 + 8 steps by 1000 ms */
    { reset(5000);
      CHECK(run(1, 1000) == 9, "1 s hold = 9 steps");
      CHECK(run(0, 50) == 0, "release stops the ramp"); }

    /* 5. a stall (the thread not scheduled) doesn't burst out the missed repeats */
    { reset(0); int esc;
      run(1, 20);                                   /* confirmed: 1 step */
      now += 2000;                                  /* 2 s with no samples */
      CHECK(vkh_feed(&c, now, 1, &esc) == 1, "one repeat after a stall, not 20"); now += TICK;
      CHECK(run(1, 70) == 0, "then the normal repeat interval again"); }

    /* 6. both keys: no steps while both are down, one escape at 3 s, nothing more until both are up */
    { reset(0);
      int steps = run(3, 2900);
      CHECK(steps == 0 && escapes == 0, "both down: no steps, no escape before 3 s");
      steps = run(3, 200);
      CHECK(steps == 0 && escapes == 1, "escape fires once at 3 s");
      steps = run(3, 2000);
      CHECK(escapes == 1, "it doesn't fire again while still held");
      steps += run(1, 1000);
      CHECK(steps == 0, "letting go of Vol- first doesn't start a Vol+ ramp");
      steps = run(0, 50) + run(1, 30);
      CHECK(steps == 1, "after both are up, a tap steps again"); run(0, 50); }

    /* 7. the key pressed first steps once before the second lands (accepted); a shorter chord doesn't escape */
    { reset(0);
      int steps = run(1, 30) + run(3, 1000) + run(0, 50);
      CHECK(steps == 1 && escapes == 0, "chord after one key: one step, no escape under 3 s"); }

    /* 8. the escape timer restarts when the chord is broken */
    { reset(0);
      run(3, 2000); run(1, 100); run(3, 2000);
      CHECK(escapes == 0, "2 s + 2 s with a break is not 3 s");
      run(3, 1100);
      CHECK(escapes == 1, "3 s held in one go escapes"); }

    /* 9. the tick wraps */
    { reset(0xFFFFFF00u);
      CHECK(run(1, 1000) == 9, "a hold across the 32-bit wrap repeats normally"); run(0, 50);
      reset(0xFFFFF000u); run(3, 3200);
      CHECK(escapes == 1, "the escape across the wrap"); }

    /* 10. Vol+ still held from boot (picking the other interface) doesn't ramp; after a release it works */
    { reset(0); vkh_init(&c, 1, 400, 80);
      CHECK(run(1, 2000) == 0, "a key held at start never steps");
      CHECK(run(0, 50) + run(1, 30) == 1, "after its release a tap steps"); run(0, 50); }

    /* 11. the volume keys' default timing (400 / 40): confirmed at 10 ms, repeats from 410 ms every 40 ms -> 1 + 15 steps */
    { vkh_init(&c, 0, 400, 40); now = 0; escapes = 0;
      CHECK(run(1, 1000) == 16, "1 s hold at 400 / 40 = 16 steps"); run(0, 50); }

    /* 12. timing changed by the user mid-hold applies from the next scheduled step */
    { reset(0);
      run(1, 420);                                  /* confirmed at 10, first repeat at 410: next due at 490 */
      c.repeat_ms = 20;
      CHECK(run(1, 80) == 1, "the step already scheduled keeps its time");
      CHECK(run(1, 100) == 5, "then the new 20 ms repeat"); run(0, 50); }

    if(fails){ printf("volkeys_hold: %d FAILED\n", fails); return 1; }
    printf("volkeys_hold: all passed\n");
    return 0;
}
