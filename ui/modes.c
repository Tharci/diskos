/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "modes.h"
#include "legacy/output.h"
#include "receiver.h"
#include "i18n.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <glob.h>

/* Audio Source picker (stock: Working mode) - mirrors stock's "Working mode" list. Tapping a mode replays
 * the captured V2.28 switch sequence via ui_set_source_mode() and marks it selected.
 * 0=Local 1=USB-DAC 2=BT-Receiving 3=USB-Storage. USB Audio (stock's "USB AUDIO"), is an OUTPUT
 * route of the Local source (the Disc feeds an external USB DAC), not a source mode: see the output section. */

typedef struct { int action; const char *name, *sub; } modeinfo_t;
static const modeinfo_t MODES[] = {
    { 0, "Local Playback",      "Play from the microSD card" },
    { 2, "Bluetooth Receiving", "Play audio sent from a phone" },
    { SOURCE_AIRPLAY, "AirPlay Receiving", "Play audio sent from an iPhone or Mac" },
    { 1, "USB DAC",             "Be a USB sound card for a PC" },
    { 3, "USB Storage",         "Open the card on a computer" },
#ifdef DISKOS_TEST_OUTPUTS
    { SOURCE_USB_AUDIO, "USB Audio",           "Play through an external USB DAC" },   /* device-unverified: test builds only */
#endif
};
#define N_MODES ((int)(sizeof(MODES)/sizeof(MODES[0])))
static int mode_row(int action){
    for(int i=0;i<N_MODES;i++) if(MODES[i].action == action) return i;
    return -1;
}

static lv_obj_t *g_check[N_MODES];   /* per-row checkmark label */
static lv_obj_t *g_row[N_MODES];     /* per-row button (for the selected highlight) */

/* The output (Internal DAC / SPDIF / USB Audio / a Bluetooth sink) is the output router's (src/legacy/output.h): the picker
 * only shows its choice and passes the user's on. What stays here are the PCM probes Cast and the receiver need. */
#ifndef OUT_PCM_GLOB
#define OUT_PCM_GLOB "/proc/asound/card*/pcm*p/sub*/status"
#endif
static void modes_ui_refresh(void);
/* 1 only when every playback PCM node is readable and none is RUNNING or DRAINING (and, with require_closed, every one is
 * closed); unreadable / no nodes = not proven */
static int out_pcm_probe(int require_closed){
    glob_t g; int quiet = 1;
    if(glob(OUT_PCM_GLOB, 0, NULL, &g) != 0) return 0;
    for(size_t i = 0; i < g.gl_pathc && quiet; i++){
        FILE *f = fopen(g.gl_pathv[i], "r"); char l[96] = "";
        if(!f){ quiet = 0; break; }
        if(!fgets(l, sizeof l, f)) l[0] = 0;
        fclose(f);
        if(!l[0] || strstr(l, "RUNNING") || strstr(l, "DRAINING")) quiet = 0;
        if(require_closed && strcmp(l, "closed\n") != 0 && strcmp(l, "closed") != 0) quiet = 0;
    }
    globfree(&g);
    return quiet;
}
int modes_receiver_quiet(void){ return out_pcm_probe(1); }
int modes_local_teardown(unsigned generation){ return ipc_send_automatic("0657000C0008", generation); }
int modes_pcm_quiet(void){ return out_pcm_probe(1); }   /* Cast requires every playback PCM explicitly closed */
void modes_output_changed(void){ modes_ui_refresh(); }

/* Actions are independent of row order in production and test layouts. */
static int shown_mode(int src){ return mode_row(src == 0 && diskos_output_desired(NULL) == DISKOS_OUTPUT_USB_AUDIO ? SOURCE_USB_AUDIO : src); }

static void mark_selected_mode(int cur){
    for(int i=0;i<N_MODES;i++){
        if(g_check[i]){ lv_label_set_text(g_check[i], i==cur ? LV_SYMBOL_OK : "");
                        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0); }  /* track accent changes */
        if(g_row[i]){   /* selected row gets an accent ring + slightly lifted fill */
            lv_obj_set_style_border_width(g_row[i], i==cur ? 2 : 0, 0);
            lv_obj_set_style_border_color(g_row[i], ui_current_accent(), 0);
            lv_obj_set_style_bg_color(g_row[i], (i==cur ? TC(SURFACE_SELECTED) : TC(SURFACE)), 0);
        }
    }
}
static void mark_selected(void){ mark_selected_mode(ui_source_switch_failed() ? -1 : shown_mode(ui_get_source_mode())); }
/* an output switch finished, failed or recovered (or the player restarted): show the real route in the picker and Settings */
static void modes_ui_refresh(void){
    mark_selected();
    setting_detail_refresh();
    setlist_refresh();
}


static uint32_t g_last_switch = 0;   /* debounce: a switch takes a few seconds to apply in the player */

/* Pending state: the tapped row shows a "switching" glyph (not the confirmed checkmark) while the
 * gadget switch is in flight - there is no source-mode completion readback, so after the switch
 * window we settle to the selection best-effort (matches the honest "Switching..." toast). */
static void mark_pending(int m){
    for(int i=0;i<N_MODES;i++){
        if(g_check[i]){ lv_label_set_text(g_check[i], i==m ? LV_SYMBOL_REFRESH : "");
                        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0); }
        if(g_row[i]){   /* highlight the row being switched to */
            lv_obj_set_style_border_width(g_row[i], i==m ? 2 : 0, 0);
            lv_obj_set_style_border_color(g_row[i], ui_current_accent(), 0);
            lv_obj_set_style_bg_color(g_row[i], (i==m ? TC(SURFACE_SELECTED) : TC(SURFACE)), 0);
        }
    }
}

static lv_timer_t *g_settle = NULL;
static int g_pending_mode = -1;   /* the mode a switch is settling to (so reopening the screen keeps showing "switching") */
/* M17: after the switch window, settle to the CONFIRMED mode read from the real USB gadget state,
 * not a blind assumption. If the gadget shows the switch didn't take, reflect reality + say so. */
static void settle_cb(lv_timer_t *t){
    (void)t;
    if(diskos_output_busy()) return;   /* an output move is still running */
    if(ui_source_switch_pending()) return;
    if(lv_tick_elaps(g_last_switch) < 3200) return;
    lv_timer_del(g_settle); g_settle = NULL;
    int intended = g_pending_mode; g_pending_mode = -1;
    if(ui_source_switch_failed()){ mark_selected_mode(-1); return; }
    /* M17: DISPLAY the ACTUAL gadget state (read-only) instead of a blind timer assumption. Do NOT
     * mutate the intent mirror (g_source_mode) - it also guards coldplug, and a transient mid-transition
     * sample must not flip that guard. */
    int show = (intended >= 0) ? intended : ui_get_source_mode();
    if(intended == 0 || intended == 1 || intended == 3){   /* USB gadget modes are readback-confirmable */
        int actual = ui_detect_source_mode();
        show = actual;
        if(actual != intended) ui_toast("Mode didn't switch");
    }
    /* intended == 2 (BT receiving) is gadget-invisible and needs a phone to connect - no reliable
     * readback here, so show the intent without asserting a false confirmation. */
    if(intended == SOURCE_USB_AUDIO || intended == SOURCE_AIRPLAY) show = ui_get_source_mode();   /* USB Audio: not visible in the gadget state; the route we last set decides */
    mark_selected_mode(shown_mode(show));
}

static void row_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int m = (int)(uintptr_t)lv_event_get_user_data(e);
    /* Serialise: ignore taps while the previous switch is still applying (the player's gadget
     * state-machine is asynchronous). NB we do NOT early-return on "same mode" - re-issuing must
     * always be allowed so Local works as a recover even if our cached mode is stale. */
    if(g_last_switch && lv_tick_elaps(g_last_switch) < 3000){ ui_toast("Switching..."); return; }
    if(m == SOURCE_USB_AUDIO){   /* USB Audio: an output route of the Local source, not a source mode */
        if(ui_output_select(DISKOS_OUTPUT_USB_AUDIO, NULL) == 0){
            g_last_switch = lv_tick_get();
            g_pending_mode = m; mark_pending(mode_row(m));
            if(g_settle) lv_timer_del(g_settle);
            g_settle = lv_timer_create(settle_cb, 500, NULL);
            ui_toast("Switching to USB audio output");
        }
        return;
    }
    if(m == 0 && (ui_receiver_active() || ui_receiver_exit_needs_confirm())){
        ui_receiver_return();
        return;
    }
    if(ui_set_source_mode(m) == 0){
        g_last_switch = lv_tick_get();
        g_pending_mode = m;
        mark_pending(mode_row(m));      /* async switch in flight: show "switching", not a confirmed selection */
        if(g_settle) lv_timer_del(g_settle);
        g_settle = lv_timer_create(settle_cb, 500, NULL);   /* settle to the checkmark after the switch window */
        /* honest wording: the frames are queued; the async switch completes a moment later. */
        if(ui_get_source_mode() != -1){
            switch(m){
                case 0: ui_toast("Switching to local playback"); break;
                case 1: ui_toast("Switching to USB DAC"); break;
                case 2: ui_toast("Switching to Bluetooth receiving"); break;
                case 3: ui_toast("Switching to USB storage"); break;
            }
        }
    }
}

void modes_open(void){
    if(g_settle && g_pending_mode >= 0) mark_pending(mode_row(g_pending_mode));  /* a switch is still settling - keep showing it */
    else mark_selected();
    screen_show(SCR_WORKMODE);
}

void modes_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* back button (kept out of the clipped top-left corner) */
    lv_obj_t *title = ui_header(root, tr("Source"));
    if(theme_preset() == 0) lv_obj_set_y(title, 54);

    /* vertical list of mode rows */
    lv_obj_t *col = lv_obj_create(root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 260, 176); lv_obj_set_pos(col, 50, 100);
    lv_obj_set_style_pad_bottom(col, 44, 0);   /* last mode row scrolls clear of the round bezel */
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);

    for(int i=0;i<N_MODES;i++){
        if(MODES[i].action == SOURCE_AIRPLAY && !receiver_available()) continue;
        lv_obj_t *row = lv_button_create(col);
        g_row[i] = row;
        lv_obj_remove_style_all(row);
        int subtitle = MODES[i].action != 0 && MODES[i].action != 2;
        lv_obj_set_size(row, 260, subtitle ? 60 : 52);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_bg_color(row, TC(SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, TC(SURFACE_PRESSED), LV_STATE_PRESSED);
        ui_on(row, row_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)MODES[i].action, "modes.row", UI_CORE);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, tr(MODES[i].name));
        lv_obj_set_pos(nm, 16, subtitle ? 7 : 16);
        lv_obj_set_width(nm, 218);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(nm, TF(UI_16), 0);
        lv_obj_set_style_text_color(nm, TC(TEXT_PRIMARY), 0);

        lv_obj_t *sb = lv_label_create(row);
        lv_label_set_text(sb, tr(MODES[i].sub));
        if(!subtitle) lv_obj_add_flag(sb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(sb, 16, 25);
        lv_obj_set_height(sb, 30);                      /* two lines keep the full receiver hint in the row */
        lv_obj_set_width(sb, 228);
        lv_label_set_long_mode(sb, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(sb, TF(UI_12), 0);
        lv_obj_set_style_text_color(sb, TC(TEXT_MUTED), 0);

        g_check[i] = lv_label_create(row);
        lv_label_set_text(g_check[i], "");
        lv_obj_align(g_check[i], LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_set_style_text_color(g_check[i], ui_current_accent(), 0);
        lv_obj_set_style_text_font(g_check[i], TF(UI_18), 0);
    }
    mark_selected();
}
