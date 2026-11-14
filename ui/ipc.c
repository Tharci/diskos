/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "ipc.h"
#include "receiver.h"
#include "path_nosymlink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include "framelog.h"
#include "legacy/player_link.h"
#include "legacy/player_reports.h"
#include "legacy/player_state.h"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static track_state_t g_state;

/* ---- the connection to the player ---------------------
 * legacy/player_link.h owns the queues and the receive thread. On the main loop (diskos_output_tick) it calls
 * link_connected() when a new player instance connects (also after a player restart, which recreates the queues),
 * parse_frame() for each frame and link_drained() after a requested drain, each before the output router hears the
 * same event. g_mu and g_recov_mu still guard the state other threads read; g_tx_mu serializes sends with the BT
 * autonext experiment and guards the send error. */
static pthread_mutex_t g_tx_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_recov_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_rx_ready    = 0;   /* [g_recov_mu] 1 once a player instance connected */
static int g_reconnected = 0;   /* [g_recov_mu] a player restart was seen: the main loop resyncs */
static unsigned g_rx_frames = 0; /* [g_recov_mu] count of /ui frames received FROM the player. /ui is the
                                  * player->UI queue (we O_RDONLY it), so any frame proves the player is up
                                  * and responding - a positive-readiness signal that does NOT depend on the
                                  * cold-boot /ui reopen firing (see ipc_rx_frames / main.c initial apply). */
static unsigned g_instance = 0; /* [g_recov_mu] the player instance (legacy/player_link.h) last delivered: 0 before
                                 * the first, one more after each player restart. See ipc_instance(). */
static int g_confirmed;         /* [g_recov_mu] accepted a2 state report from this player instance */
static uint64_t g_mode_change_sequence; /* [g_recov_mu] durable departures */
static uint64_t g_arm_next, g_arm_request, g_arm_ack, g_arm_sequence; /* [g_recov_mu] */
static uint64_t g_source_sequence; /* [g_recov_mu] accepted source reports from this player instance */
static uint64_t g_obs_id, g_adopt_id; /* [g_recov_mu] observation arm, and the one that adopted */
static int g_player_mode = -1;   /* [g_recov_mu] last a607 external-mode announced by the player (8=LOCALPLAYER).
                                  * -1 = none received THIS player instance. NOTE (RE-verified vs v2.57): a607 is
                                  * NOT a reliable *solicited* readiness oracle - a 0607 query's reply does not come
                                  * back on /ui (design trace: a separate socket transport); the player only announces a607 to /ui after a 0657 mode
                                  * CHANGE. Readiness is established by the a2 probe + settle (main.c); when an a607
                                  * does arrive here, mode==8 is a valid "LOCALPLAYER set" confirmation.
                                  * Reset to -1 on /ui reopen (new player instance). See ipc_player_mode(). */

/* ---- helpers ---- */
static char g_stock_title[IPC_TITLE_CAPACITY];
static int g_title_frozen, g_title_stock;
static uint64_t g_title_revision;
static int title_pending(const char *path, const char *title){
    const char *base=strrchr(path,'/'); base=base ? base+1 : path;
    const char *ext=strrchr(base,'.');
    return path[0]=='/' && ext && (!strcasecmp(ext,".opus") ||
        !strcasecmp(ext,".oga") || !strcasecmp(ext,".ogg")) &&
        (!title[0] || !strcmp(base,title));
}
/* Zero the current-track fields. Caller must hold g_mu. */
static void clear_track(void){
    if(g_state.player_confirmed) g_state.playback_token++;
    g_state.player_confirmed=0; g_state.provisional=0; g_state.title_ready=0;
    g_state.song_id_valid=g_state.pos_id_valid=0;
    g_state.is_cue_valid=g_state.is_sacd_valid=0;
    g_state.song_id_raw=g_state.pos_id_raw=0;
    g_title_frozen=g_title_stock=0; g_stock_title[0]=0;
    g_state.have_track=0;
    g_state.title[0]=0; g_state.artist[0]=0; g_state.album[0]=0; g_state.path[0]=0;
    g_state.duration_ms=0; g_state.position_ms=0; g_state.pos_id=0;
    g_state.sample_rate=0; g_state.is_dsd=0;
}

/* Receiver publication is serialized with source/instance evidence, never g_mu. */
static ipc_receiver_meta g_receiver_meta;
static receiver_art_key g_receiver_art;
static uint64_t g_receiver_art_revision;
static uint64_t g_receiver_arm, g_receiver_session;
static unsigned g_receiver_instance;
static int g_receiver_selected;
/* Local publication has one owner, under g_recov_mu, with no new lock order.
 * RESERVED blocks entry frames but rolls back on cancellation before dispatch.
 * BLOCKED covers sent entry, selection and recovery; RETURN_ARM/READY cover exit.
 * No a1/a2 reaches local publication in these states, including old-queue frames.
 * Fresh Local after the return boundary, an observed AirPlay->Local departure,
 * unsent cancellation, or a new instance opens publication again. READY survives
 * cancellation/retry: stock suppresses duplicate a607 (MP 0x4f1174-0x4f119c), so
 * neither already observed departure nor late Local may require another report.
 * Outside these sessions local publication (publish_track_locked) is unchanged. */
static struct {
    enum { LOCAL_OPEN, LOCAL_RESERVED, LOCAL_BLOCKED,
           LOCAL_RETURN_ARM, LOCAL_RETURN_READY } state;
    uint64_t return_arm;
} g_local_publication;
static int receiver_owned_locked(void){
    return g_receiver_arm && g_receiver_instance==g_instance &&
        g_arm_ack==g_receiver_arm;
}
static void receiver_clear_locked(void){
    g_receiver_arm=0; g_receiver_selected=0;
    memset(&g_receiver_meta,0,sizeof g_receiver_meta);
    diskos_player_reset(DISKOS_PLAYER_RECEIVER, g_instance);
    g_receiver_meta.instance=g_instance;
}
void ipc_receiver_begin(unsigned instance, uint64_t arm){
    pthread_mutex_lock(&g_recov_mu);
    receiver_clear_locked();
    g_receiver_meta.instance=instance;
    g_receiver_meta.session_revision=++g_receiver_session;
    g_receiver_instance=instance; g_receiver_arm=arm;
    if(g_local_publication.state==LOCAL_OPEN)
        g_local_publication.state=g_player_mode==10 ? LOCAL_BLOCKED : LOCAL_RESERVED;
    pthread_mutex_unlock(&g_recov_mu);
}
void ipc_receiver_end(void){
    pthread_mutex_lock(&g_recov_mu);
    receiver_clear_locked();
    if(g_local_publication.state==LOCAL_RESERVED)
        g_local_publication.state=LOCAL_OPEN;
    /* An unacknowledged return cannot supply freshness after cancellation. */
    if(g_local_publication.state==LOCAL_RETURN_ARM){
        g_local_publication.state=LOCAL_BLOCKED;
        g_local_publication.return_arm=0;
    }
    pthread_mutex_unlock(&g_recov_mu);
}
void ipc_receiver_return(unsigned instance, uint64_t arm){
    pthread_mutex_lock(&g_recov_mu);
    receiver_clear_locked();
    if(instance==g_instance){
        if(g_local_publication.state==LOCAL_RESERVED)
            g_local_publication.state=LOCAL_OPEN;
        if(g_local_publication.state==LOCAL_BLOCKED ||
           g_local_publication.state==LOCAL_RETURN_ARM){
            g_local_publication.state=LOCAL_RETURN_ARM;
            g_local_publication.return_arm=arm;
        }
    }
    pthread_mutex_unlock(&g_recov_mu);
}
void ipc_receiver_metadata(ipc_receiver_meta *out){
    pthread_mutex_lock(&g_recov_mu);
    if(g_receiver_meta.instance!=g_instance){
        memset(&g_receiver_meta,0,sizeof g_receiver_meta);
        diskos_player_reset(DISKOS_PLAYER_RECEIVER, g_instance);
        g_receiver_meta.instance=g_instance; g_receiver_arm=0;
    }
    *out=g_receiver_meta;
    pthread_mutex_unlock(&g_recov_mu);
}
/* Receipt-order art key shares the locked metadata snapshot, not file provenance. */
void receiver_art_get(receiver_art_key *out){
    pthread_mutex_lock(&g_recov_mu);
    ipc_receiver_meta *m=&g_receiver_meta;
    *out=g_receiver_art;
    if(m->instance!=g_instance || out->generation!=m->instance ||
       out->session!=m->session_revision || out->item!=m->item_revision)
        *out=(receiver_art_key){g_instance,m->session_revision,m->item_revision,0,0};
    if(m->instance!=g_instance) out->session=out->item=0;
    pthread_mutex_unlock(&g_recov_mu);
}

static uint64_t receiver_now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
}
/* Copy src into dst[cap], cut at a character boundary. */
static void copy_text(char *dst, size_t cap, const char *src){
    size_t n=strlen(src);
    if(n>=cap){
        n=cap-1;
        while(n>0 && ((unsigned char)src[n]&0xC0)==0x80) n--;   /* not inside a UTF-8 sequence */
    }
    memcpy(dst,src,n); dst[n]=0;
}
static int is_a202(diskos_report_kind kind){
    return kind==DISKOS_REPORT_PLAY_STATE || kind==DISKOS_REPORT_TRACK || kind==DISKOS_REPORT_AIRPLAY_TRACK;
}
/* Apply an a202 to the receiver's state (legacy/player_state.h) and publish it in g_receiver_meta.
 * Caller holds g_recov_mu. Returns 0 if the frame is refused. */
static int receiver_apply(const char *frame,int n){
    const diskos_player_update update=diskos_player_apply(DISKOS_PLAYER_RECEIVER, frame, (size_t)n);
    if(!update.accepted) return 0;
    ipc_receiver_meta *m=&g_receiver_meta;
    if(update.reported_state>=0){ m->transport_state=update.reported_state; m->transport_valid=1; }
    if(!update.full) return 1;
    uint64_t now=receiver_now_ms();
    if(update.track){
        /* a103 carries no item id: position and duration belong to the old item. */
        m->item_revision++; m->item_rx_ms=now;
        m->progress_valid=0; m->position_ms=m->duration_ms=0;
    }
    diskos_player_track t;
    if(diskos_player_get_track(DISKOS_PLAYER_RECEIVER, &t)){
        copy_text(m->title, sizeof m->title, t.title);
        copy_text(m->artist, sizeof m->artist, t.artist);
        copy_text(m->album, sizeof m->album, t.album);
        if(t.duration_ms>=0 && t.duration_ms<=INT32_MAX){ m->duration_ms=(long)t.duration_ms; m->dur_rx_ms=now; m->dur_seq++; }
    }
    return 1;
}

/* Under g_recov_mu. Fresh evidence only: the observation boundary was acknowledged, nothing owns the receiver. */
static int adopt_ready_locked(void){
    return g_obs_id && g_obs_id==g_arm_ack && !g_receiver_arm;
}
/* The adopting frame is the one that identifies the source: ownership, selection and wire mode are published with
 * its metadata. A complete frame from the current player also confirms the instance (Return to Local needs it). */
static void adopt_locked(void){
    g_receiver_instance=g_instance; g_receiver_arm=g_adopt_id=g_obs_id;
    g_receiver_selected=1; g_confirmed=1;
    if(g_local_publication.state==LOCAL_OPEN || g_local_publication.state==LOCAL_RESERVED)
        g_local_publication.state=LOCAL_BLOCKED;
}
static void adopt_begin_locked(void){
    receiver_clear_locked();
    g_receiver_meta.instance=g_instance; g_receiver_meta.session_revision=++g_receiver_session;
}

/* Copy the player state's track (legacy/player_state.h) into g_state. Caller holds g_mu.
 * A new track drops the previous one's position and title freeze; a frozen title stays otherwise. */
static void publish_track_locked(int new_track){
    diskos_player_track t;
    /* A local track needs a path: without one, new positions would bind to the previous track. */
    if(!diskos_player_get_track(DISKOS_PLAYER_LOCAL, &t) || !t.path[0]){ clear_track(); return; }
    if(new_track){
        g_state.playback_token++; g_title_frozen=g_title_stock=0;
        g_state.position_ms=0; g_state.pos_seq=0;
    }
    if(strcmp(g_stock_title,t.title) || !g_state.is_cue_valid || g_state.is_cue!=t.is_cue || g_state.is_sacd!=t.is_sacd)
        g_title_revision++;
    g_state.player_confirmed=1; g_state.provisional=0;
    g_state.song_id_raw=(uint64_t)t.song_id; g_state.pos_id_raw=(uint64_t)t.pos_id;
    g_state.song_id_valid=g_state.pos_id_valid=g_state.is_cue_valid=g_state.is_sacd_valid=1;
    g_state.is_cue=t.is_cue; g_state.is_sacd=t.is_sacd;
    snprintf(g_stock_title, sizeof g_stock_title, "%s", t.title);
    snprintf(g_state.path, sizeof g_state.path, "%s", t.path);
    if(!g_title_frozen){
        snprintf(g_state.title, sizeof g_state.title, "%s", t.title);
        if(!title_pending(t.path,t.title) || t.is_cue || t.is_sacd) g_title_stock=1;
        g_state.title_ready=g_title_stock;
    }
    copy_text(g_state.artist, sizeof g_state.artist, t.artist);
    copy_text(g_state.album, sizeof g_state.album, t.album);
    g_state.duration_ms=(long)t.duration_ms;
    g_state.sample_rate=(int)t.sample_rate_hz;
    g_state.is_dsd=t.is_dsd;
    g_state.is_favorite=t.favourite;
    g_state.pos_id = (t.pos_id>0 && t.pos_id<=0x7FFFFFFF) ? (int)t.pos_id : 0;
    if(t.queue_count>0)
        snprintf(g_state.playing_num, sizeof g_state.playing_num, "%lld/%lld", (long long)t.queue_position, (long long)t.queue_count);
    else g_state.playing_num[0]=0;
    g_state.have_track=1;
}

/* the player's screen sleep/wake ("aa23000C0000" / "...0001", echo_sys_control.c V2.57) and power-off ("aa03"):
 * counters so the main loop sees every event (the latest value wins between its polls) */
static unsigned g_scr_seq, g_aa03_seq; static int g_scr_on = -1;
/* Called under g_recov_mu: frame state belongs only to this player instance. */
static void screen_new_instance(unsigned instance){
    g_confirmed = 0;
    g_local_publication.state=LOCAL_OPEN; g_local_publication.return_arm=0;
    g_instance = instance; g_scr_on = -1; g_scr_seq = g_aa03_seq = 0;
}
/* The stock player's charging report, aa1f (V2.57 mq_player, adc_control.c loop_adc_check_thread; static RE of the
 * fixture binary). The player's charging flag (0x8990ec) is only ever stored as 0 or 1 (0x4f2390, 0x4f27ac, 0x4f27d4,
 * 0x4f2828, 0x4f286c). When it differs from the last sent value (0x8990e8) for 4 loop passes, it is sent with
 * 0x4ddaec(1, "aa1f", flag, NULL) (0x4f2458-0x4f246c), which formats "%s%04X%04X" with length 12 (0x4ddc54-0x4ddc84):
 * "aa1f000C0000" / "aa1f000C0001". While state byte 0x83a756 (read as the battery percentage) is 100 the send carries 0
 * instead (0x4f2450 -> 0x4f26d0), and one 0 is sent when it reaches 100 (0x4f2538-0x4f2558). Nothing else touches
 * 0x8990e8, so there is no resend on request: a UI restart has no report until the flag next changes. Only that exact
 * 12-character form is accepted; any other aa1f frame is counted and ignored and never changes the state. Layout pinned
 * by tests/release/test_parity_power.py; docs/COMMAND_MAP.md "aa1f". Live frames UNVERIFIED. [g_recov_mu] */
static int g_charge_state = -1; static uint64_t g_charge_at_ms; static unsigned g_charge_seq, g_charge_bad;
/* Called under g_recov_mu with the instance change: the new player's last-sent value starts over, so an older
 * report says nothing about it (it reports only a change from its own start-up value). */
static void charge_new_instance(void){ g_charge_state = -1; g_charge_at_ms = 0; }
/* diskos_boot.log line per received aa1f (raw bytes + decision), at most 8 lines a minute, the rest counted. RX only. */
static void charge_log(const char *buf, int n, int value, unsigned bad){
    static int have; static uint64_t window; static unsigned lines, dropped;
    uint64_t now = receiver_now_ms();
    if(!have || now - window >= 60000u){ have = 1; window = now; lines = 0; }
    if(lines >= 8){ dropped++; return; }
    lines++;
    char raw[17]; int k = n < 16 ? (n > 0 ? n : 0) : 16;
    for(int i = 0; i < k; i++){ unsigned char c = (unsigned char)buf[i]; raw[i] = (c >= 0x20 && c < 0x7f && c != '"') ? (char)c : '?'; }
    raw[k] = 0;
    if(value >= 0)
        fprintf(stderr, "charge: aa1f \"%s\" -> %s (stock report wins)", raw, value ? "charging" : "not charging");
    else
        fprintf(stderr, "charge: aa1f \"%s\" (%d bytes) malformed, ignored (%u so far)", raw, n, bad);
    if(dropped) fprintf(stderr, " [%u earlier lines not logged]", dropped);
    fputc('\n', stderr);
    dropped = 0;
}
/* v: 0 or 1 from a decoded report, -1 for an aa1f frame the decoder refused. */
static void charge_on_frame(const char *buf, int n, int v){
    pthread_mutex_lock(&g_recov_mu);
    unsigned bad = g_charge_bad;
    if(v >= 0){ g_charge_state = v; g_charge_at_ms = receiver_now_ms(); g_charge_seq++; }
    else bad = ++g_charge_bad;
    pthread_mutex_unlock(&g_recov_mu);
    charge_log(buf, n, v, bad);
}
/* V2.57 MP@42a720 -> 4ddc94 emits a1030010%08X with NO item identity: the
 * position is bound to the item current at receipt (Disc capture 2026-10-16:
 * ms, ~1 Hz while playing, stops on pause, restarts at 0 on a skip). Exactly
 * 8 hex digits within int32; anything else is ignored. Under g_recov_mu. */
static void receiver_position(uint32_t ms){
    if(ms>INT32_MAX) return;
    g_receiver_meta.position_ms=(long)ms; g_receiver_meta.progress_valid=1;
    g_receiver_meta.pos_rx_ms=receiver_now_ms();
    g_receiver_meta.pos_item_revision=g_receiver_meta.item_revision; g_receiver_meta.pos_seq++;
}
static void conv_on_reply(int ok, unsigned code);   /* still-image conversion (end of file) */
static void conv_new_instance(unsigned instance);
/* A frame the decoder refused changes nothing; the two reports that count malformed frames still count them. */
static void refused_frame(const char *buf, int n){
    if(n >= 4 && !memcmp(buf, "aa1f", 4)) charge_on_frame(buf, n, -1);
    else if(n >= 4 && !memcmp(buf, "a810", 4)) conv_on_reply(0, 0);
}
static void parse_frame(const char*buf,int n){
#ifdef DISKOS_FRAME_LOG
    framelog_rx(buf, n > 0 ? (size_t)n : 0);
#endif
    /* src/player decodes every report strictly
     * a202 comes back UNDECODED and is still read below. */
    const diskos_report report = diskos_decode_report(buf, n > 0 ? (size_t)n : 0);
    if(report.kind == DISKOS_REPORT_MALFORMED){ refused_frame(buf, n); return; }
    if(report.kind == DISKOS_REPORT_COVER_ART){
        /* STOCK-PARITY (owner decision 55): bind by receipt, not verified item.
         * V2.40 converter 0x45b69c -> aa05=1 at 0x45b6fc-0x45b710;
         * V2.57 0x4620c4 -> 0x462124-0x462138. Neither checks the return.
         * V2.57 can return at 0x480bd4-0x480c08 before writer 0x480d54.
         * A late notice or failed conversion can show the previous cover for
         * the current item. The key only rejects jobs superseded after receipt. */
        pthread_mutex_lock(&g_recov_mu);
        if(receiver_owned_locked())
            g_receiver_art=(receiver_art_key){g_receiver_meta.instance,g_receiver_meta.session_revision,
                g_receiver_meta.item_revision,++g_receiver_art_revision,
                g_receiver_meta.item_revision && report.value == 1};
        pthread_mutex_unlock(&g_recov_mu);
        return;
    }
    if(report.kind == DISKOS_REPORT_SCREEN){
        pthread_mutex_lock(&g_recov_mu); g_scr_on=report.value!=0; g_scr_seq++; pthread_mutex_unlock(&g_recov_mu);
        return;
    }
    if(report.kind == DISKOS_REPORT_POWER_OFF){
        pthread_mutex_lock(&g_recov_mu); g_aa03_seq++; pthread_mutex_unlock(&g_recov_mu);
        return;
    }
    if((buf[0]=='a' && buf[1]=='1') || (buf[0]=='a' && buf[1]=='2')){
        pthread_mutex_lock(&g_recov_mu);
        if(report.kind == DISKOS_REPORT_AIRPLAY_TRACK && !receiver_owned_locked()){
            /* Quarantine: AirPlay metadata never reaches the Local parser (no path, so it would clear the Local
             * track). Fresh evidence after the observation boundary adopts the source and publishes this frame. */
            if(adopt_ready_locked() && (g_player_mode<0 || g_player_mode==10)){   /* a wire report of another source conflicts */
                adopt_begin_locked();
                if(receiver_apply(buf,n)){
                    adopt_locked();
                    if(g_player_mode!=10){ g_player_mode=10; g_mode_change_sequence++; g_source_sequence++; }
                } else receiver_clear_locked();
            }
            /* The observation boundary gates ownership, not quarantine. Treat an
             * unconflicted AirPlay report like a607 mode 10 even before adoption. */
            if(g_player_mode<0 || g_player_mode==10){
                if(g_local_publication.state==LOCAL_OPEN || g_local_publication.state==LOCAL_RESERVED)
                    g_local_publication.state=LOCAL_BLOCKED;
                if(g_player_mode!=10){ g_player_mode=10; g_mode_change_sequence++; g_source_sequence++; }
            }
            g_confirmed=1;   /* a complete frame from the current player */
            pthread_mutex_unlock(&g_recov_mu); return;
        }
        if(g_local_publication.state!=LOCAL_OPEN){
            if(receiver_owned_locked() && is_a202(report.kind))
                receiver_apply(buf,n);
            else if(receiver_owned_locked() && report.kind == DISKOS_REPORT_POSITION) receiver_position(report.value);
            /* No receiver frame may reach local position/track publication. */
            pthread_mutex_unlock(&g_recov_mu); return;
        }
        pthread_mutex_unlock(&g_recov_mu);
    }
    if(report.kind == DISKOS_REPORT_CONVERSION){ conv_on_reply(1, report.value); return; }
    if(report.kind == DISKOS_REPORT_CHARGING){ charge_on_frame(buf, n, (int)report.value); return; }
    if(buf[0]==97 && buf[1]==49){ /* a1: only a103 is a position */
        /* a102 is the play-mode ack (a102000C0004 after a book's Single mode) - read as a position it put Now
         * Playing at 0:00 and made the next skip count from there. A position must fit the 32-bit long. */
        if(report.kind != DISKOS_REPORT_POSITION || report.value > INT32_MAX) return;
        /* Every report counts, also an unchanged one: pos_seq means "a position was reported" (play-start,
         * route-resume and BT-prepare checks wait for one; lyrics re-anchor on it). */
        pthread_mutex_lock(&g_mu);
        if(diskos_player_apply(DISKOS_PLAYER_LOCAL, buf, (size_t)n).accepted){
            g_state.position_ms=(long)diskos_player_position(DISKOS_PLAYER_LOCAL); g_state.seq++; g_state.pos_seq=g_state.seq;
        }
        pthread_mutex_unlock(&g_mu);
    } else if(is_a202(report.kind)){ /* the play state, or the current track */
        pthread_mutex_lock(&g_mu);
        const diskos_player_update update=diskos_player_apply(DISKOS_PLAYER_LOCAL, buf, (size_t)n);
        char oldpath[sizeof g_state.path]; snprintf(oldpath, sizeof oldpath, "%s", g_state.path);
        if(update.reported_state>=0) g_state.state=update.reported_state;
        if(update.track || update.details) publish_track_locked(update.track);
        g_state.seq++;
        if(strcmp(oldpath, g_state.path) != 0) g_state.path_seq = g_state.seq;   /* track changed -> new identity epoch */
        pthread_mutex_unlock(&g_mu);
        if(update.accepted){
            pthread_mutex_lock(&g_recov_mu); g_confirmed=1; pthread_mutex_unlock(&g_recov_mu);
        }
    } else if(report.kind == DISKOS_REPORT_VOLUME){
        pthread_mutex_lock(&g_mu); g_state.volume=(int)report.value; g_state.volume_seq++; pthread_mutex_unlock(&g_mu);
    } else if(report.kind == DISKOS_REPORT_SOURCE){
        /* "a607000C<MODE>" - the player's external/input mode, announced to /ui after a 0657 mode CHANGE.
         * (A 0607 query's reply does not arrive here (separate transport, unverified on the Disc), so we do NOT rely on soliciting it - see
         * g_player_mode.) MODE 0008 = LOCALPLAYER; when this arrives, ==8 confirms LOCALPLAYER is set. */
        int mode=(int)report.value;
        pthread_mutex_lock(&g_recov_mu);
        if(mode==10 && adopt_ready_locked()){ adopt_begin_locked(); adopt_locked(); }
        if(mode!=g_player_mode) g_mode_change_sequence++;
        if(mode==10 && (g_local_publication.state==LOCAL_OPEN ||
            g_local_publication.state==LOCAL_RESERVED))
            g_local_publication.state=LOCAL_BLOCKED;   /* wire AirPlay: no receiver frame is a Local frame */
        if(mode==8 && (g_local_publication.state==LOCAL_RETURN_READY ||
           (g_local_publication.state==LOCAL_BLOCKED && g_player_mode==10))){
            g_local_publication.state=LOCAL_OPEN;
            g_local_publication.return_arm=0;
        }
        if(receiver_owned_locked()){
            if(g_receiver_selected && mode!=10){
                g_receiver_arm=0; memset(&g_receiver_meta,0,sizeof g_receiver_meta);
                diskos_player_reset(DISKOS_PLAYER_RECEIVER, g_instance);
                g_receiver_meta.instance=g_instance;
            } else if(mode==10) g_receiver_selected=1;
        }
        g_player_mode=mode; g_source_sequence++;
        pthread_mutex_unlock(&g_recov_mu);
    }
    /* NB: for playback the player emits a1/a2/a714 to /ui (a2=state/love/work_mode/track, a1=position,
     * a714=volume) - no a622/a639/a704 completion replies. SEPARATELY, a 0657 mode CHANGE makes it announce
     * a607 (external/input mode) to /ui, parsed above (a bare 0607 query is not answered on /ui). */
}

/* Main loop: a new player instance connected (legacy/player_link.h). Everything learned from a
 * previous instance is dropped; the first one starts from the fresh state. */
static void link_connected(unsigned instance){
    pthread_mutex_lock(&g_recov_mu);
    const int restarted = g_rx_ready;
    g_rx_ready = 1;
    if(restarted) g_reconnected = 1;   /* the main thread resyncs */
    g_player_mode = -1; g_source_sequence = g_mode_change_sequence = 0;
    g_arm_request = g_arm_ack = 0; g_obs_id = g_adopt_id = 0; screen_new_instance(instance);
    charge_new_instance();
    pthread_mutex_unlock(&g_recov_mu);
    pthread_mutex_lock(&g_mu);
    diskos_player_reset(DISKOS_PLAYER_LOCAL, instance);   /* before its first frame: nothing to lose on the first */
    if(restarted){
        int had_path=g_state.path[0]!=0;
        clear_track(); g_state.playback_token++; g_state.seq++;
        if(had_path) g_state.path_seq=g_state.seq;
    }
    g_state.player_instance=instance;
    pthread_mutex_unlock(&g_mu);
    if(restarted){
        conv_new_instance(instance);   /* a conversion in flight died with the old player */
        fprintf(stderr,"ipc: player instance %u connected after a restart\n", instance);
    }
}

static void link_frame(const char *frame, size_t size){
    pthread_mutex_lock(&g_recov_mu); g_rx_frames++; pthread_mutex_unlock(&g_recov_mu);
    parse_frame(frame, (int)size);
}

/* Main loop: every frame queued when an arm was requested has been parsed (diskos_link_request_drain). No
 * request ID exists in a607: this gives receipt freshness, not command-response correlation. */
static void link_drained(void){
    pthread_mutex_lock(&g_recov_mu);
    uint64_t id=g_arm_request;
    if(id && id!=g_arm_ack){
        g_arm_sequence=g_source_sequence; g_arm_ack=id;
        if(g_local_publication.state==LOCAL_RETURN_ARM &&
           g_local_publication.return_arm==id){
            g_local_publication.state=LOCAL_RETURN_READY;
            g_local_publication.return_arm=0;
        }
    }
    pthread_mutex_unlock(&g_recov_mu);
}

int ipc_start(void){
    const diskos_link_listener listener = { link_connected, link_frame, link_drained };
    (void)diskos_link_start(listener);   /* -1 only when already started */
    return 0;
}
/* 1 (and clears) once after a recovery reattach - main thread resyncs UI-owned state. */
int ipc_take_reconnected(void){
    pthread_mutex_lock(&g_recov_mu); int r=g_reconnected; g_reconnected=0; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
/* Number of /ui frames received from the player so far (>0 => the player is up and responding). Used by
 * the main loop to guarantee the initial audio apply even if the cold-boot /ui reopen never fired. */
unsigned ipc_rx_frames(void){
    pthread_mutex_lock(&g_recov_mu); unsigned r=g_rx_frames; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
/* Last a607 external-mode reported by the player THIS instance: -1 = none yet (player not confirmed at
 * its command dispatcher), else the mode (8 = LOCALPLAYER). The v2.40 work-mode handshake oracle. */
int ipc_player_mode(void){
    pthread_mutex_lock(&g_recov_mu); int r=g_player_mode; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
/* Player instance: bumps on each /ui reattach (player restart). The v2.40 work-mode one-shot uses this
 * to re-arm after a restart (send the init once per instance). */
void ipc_screen_snapshot(int *on, unsigned *scr_seq, unsigned *poweroff_seq, unsigned *instance){
    pthread_mutex_lock(&g_recov_mu);
    if(on) *on=g_scr_on; if(scr_seq) *scr_seq=g_scr_seq; if(poweroff_seq) *poweroff_seq=g_aa03_seq;
    if(instance) *instance=g_instance;
    pthread_mutex_unlock(&g_recov_mu);
}
int ipc_charge_snapshot(ipc_charge_evidence *out){
    ipc_charge_evidence e;
    pthread_mutex_lock(&g_recov_mu);
    e.state=g_charge_state; e.at_ms=g_charge_at_ms; e.seq=g_charge_seq; e.bad=g_charge_bad; e.instance=g_instance;
    pthread_mutex_unlock(&g_recov_mu);
    if(out) *out=e;
    return e.state;
}
void ipc_source_snapshot(ipc_source_evidence *out){
    pthread_mutex_lock(&g_recov_mu);
    out->instance=g_instance; out->sequence=g_source_sequence;
    out->mode_change_sequence=g_mode_change_sequence;
    out->arm_id=g_arm_ack; out->arm_sequence=g_arm_sequence; out->adopt_id=g_adopt_id;
    out->mode=g_player_mode; out->valid=g_rx_ready;
    pthread_mutex_unlock(&g_recov_mu);
}
uint64_t ipc_source_arm(unsigned instance){
    pthread_mutex_lock(&g_recov_mu);
    uint64_t id=0;
    if(g_rx_ready && g_confirmed && instance==g_instance){
        id=++g_arm_next; g_arm_request=id; g_arm_ack=0;
    }
    pthread_mutex_unlock(&g_recov_mu);
    if(id) diskos_link_request_drain();
    return id;
}
uint64_t ipc_observe_arm(unsigned instance){
    pthread_mutex_lock(&g_recov_mu);
    uint64_t id=0;
    if(g_rx_ready && instance==g_instance && !g_arm_request && !g_receiver_arm){
        id=++g_arm_next; g_arm_request=g_obs_id=id; g_arm_ack=0;
    }
    pthread_mutex_unlock(&g_recov_mu);
    if(id) diskos_link_request_drain();
    return id;
}
void ipc_source_cancel_arm(uint64_t id){
    pthread_mutex_lock(&g_recov_mu);
    if(id && g_arm_request==id) g_arm_request=g_arm_ack=0;
    pthread_mutex_unlock(&g_recov_mu);
}
unsigned ipc_instance(void){
    pthread_mutex_lock(&g_recov_mu); unsigned r=g_instance; pthread_mutex_unlock(&g_recov_mu);
    return r;
}
void ipc_get_state(track_state_t*out){
    pthread_mutex_lock(&g_mu); *out=g_state; pthread_mutex_unlock(&g_mu);
}
int ipc_local_playing(void){
    pthread_mutex_lock(&g_mu);
    diskos_player_advance(DISKOS_PLAYER_LOCAL);   /* the main loop's tick is the clock */
    int playing=diskos_player_is_playing(DISKOS_PLAYER_LOCAL);
    pthread_mutex_unlock(&g_mu);
    return playing;
}
void ipc_get_title_event(ipc_title_event *out){
    pthread_mutex_lock(&g_mu);
    out->state=g_state; strcpy(out->player_title,g_stock_title); out->evidence_revision=g_title_revision;
    pthread_mutex_unlock(&g_mu);
}
int ipc_publish_title(ipc_title_event *event, const char *title, int freeze){
    pthread_mutex_lock(&g_mu);
    int accepted=g_state.player_confirmed && g_state.playback_token==event->state.playback_token &&
        g_title_revision==event->evidence_revision;
    if(accepted){
        /* Change presentation only: retain newer position, volume and metadata. */
        snprintf(g_state.title,sizeof g_state.title,"%s",freeze ? title : g_stock_title);
        g_title_frozen=freeze; g_title_stock=!freeze; g_state.title_ready=1; g_state.seq++;
    } else {
        event->state=g_state; strcpy(event->player_title,g_stock_title);
        event->evidence_revision=g_title_revision;
    }
    pthread_mutex_unlock(&g_mu);
    return accepted;
}
/* Seed the track fields at startup (from the DB resume state) so the UI shows
 * the current song before the player sends its first a2 frame.  Guarded: a real
 * a2 frame (have_track already set) is never overwritten. */
void ipc_seed_state(const track_state_t *s){
    if(!s) return;
    pthread_mutex_lock(&g_mu);
    if(!g_state.have_track){
        snprintf(g_state.title,  sizeof g_state.title,  "%s", s->title);
        snprintf(g_state.artist, sizeof g_state.artist, "%s", s->artist);
        snprintf(g_state.album,  sizeof g_state.album,  "%s", s->album);
        snprintf(g_state.path,   sizeof g_state.path,   "%s", s->path);
        g_state.duration_ms = s->duration_ms;
        g_state.position_ms = s->position_ms;
        g_state.state       = s->state;
        g_state.is_favorite = s->is_favorite;
        g_state.have_track  = 1;
        g_state.provisional = 1; g_state.player_confirmed = 0; g_state.title_ready = 0;
        g_state.seq++;
    }
    pthread_mutex_unlock(&g_mu);
}
#ifdef DISKOS_HOST
void ipc_host_set_state(const track_state_t *s){
    if(!s) return;
    pthread_mutex_lock(&g_mu);
    g_state = *s;
    snprintf(g_stock_title, sizeof g_stock_title, "%s", s->title);
    g_title_frozen = 0; g_title_stock = s->title_ready; g_title_revision++;
    pthread_mutex_unlock(&g_mu);
}
#endif
/* Send to /player with bounded nonblocking retries, serialized by g_tx_mu. */
static int g_send_err = 0;   /* sticky until ipc_take_send_error() reads it */

enum { SEND_USER, SEND_PROBE, SEND_AUTOMATIC };
/* Called with g_tx_mu held (g_send_err and the once-per-boot flag are guarded by it). */
static void ipc_note_send_error(int policy){
    static int automatic_reported;
    if(policy == SEND_PROBE) return;
    if(policy == SEND_AUTOMATIC){
        if(automatic_reported) return;
        automatic_reported = 1;
        int fd = open("/tmp/.diskos-player-send-notice", O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, 0600);
        if(fd >= 0) close(fd);
        else if(errno == EEXIST) return;
    }
    g_send_err = 1;
}
/* Probes stay silent (background state-sync / health probes and the still-image conversion requests must not raise
 * "Player didn't respond" during the startup connect race); automatic failures notify once per boot. User failures
 * always report. An automatic send goes only to the player instance it names: for another one (or none) it
 * stays deferred. The link retries a full queue briefly and reopens the queue after a failure. Caller holds g_tx_mu. */
static int ipc_send_wire_locked(const char*frame, int policy, unsigned instance){
    if(policy == SEND_AUTOMATIC && instance == 0) return IPC_SEND_DEFERRED;   /* no player instance to send to yet */
    const diskos_link_result sent = diskos_link_send(frame, policy == SEND_AUTOMATIC ? instance : 0);
#ifdef DISKOS_FRAME_LOG
    const char *policy_name = policy == SEND_USER ? "user" : policy == SEND_PROBE ? "probe" : "automatic";
    framelog_tx(frame, strlen(frame), sent == DISKOS_LINK_SENT ? 0 : -1, policy_name);
#endif
    if(sent == DISKOS_LINK_SENT) return 0;
    if(sent == DISKOS_LINK_WRONG_INSTANCE) return IPC_SEND_DEFERRED;
    ipc_note_send_error(policy);
    if(policy != SEND_PROBE) fprintf(stderr,"ipc_send_cmd '%s' failed\n", frame);
    return -1;
}
/* #28 experiment: stock owns EOF and queue policy. Only its gapless scalar is
 * overridden; TX serializes restoration with every route/source command.
 * An explicit diskOS preference is required so System default is never guessed.
 * Whether the audio is on a Bluetooth sink is the output router's (the poll's eligibility): it reports one only once
 * a route to it has landed and no move runs, so this only restores before a route frame the router sends between
 * two polls. */
#ifndef BT_AUTONEXT_PATH
#define BT_AUTONEXT_PATH "/usr/data/diskos_bt_autonext_exp"
#endif
static struct { unsigned gen; int active, restore; } g_bt_autonext; /* g_tx_mu */
static int bt_autonext_mode(void){
    int fd = open(BT_AUTONEXT_PATH, O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    if(fd < 0) return 0;
    struct stat st; char buf[32];
    ssize_t n = -1;
    if(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < (off_t)sizeof buf)
        n = read(fd, buf, sizeof buf);
    close(fd);
    return (n == 10 && !memcmp(buf, "gapless-on", 10)) ||
           (n == 11 && !memcmp(buf, "gapless-on\n", 11));
}
static void bt_autonext_instance(unsigned gen){
    if(g_bt_autonext.gen != gen){
        g_bt_autonext.gen = gen;
        g_bt_autonext.active = 0; /* normal reconnect scalar restoration owns the new player */
    }
}
static int bt_autonext_restore(int policy, unsigned gen){
    if(!g_bt_autonext.active) return 0;
    const char *f = g_bt_autonext.restore ? "0647000C0001" : "0647000C0000";
    if(ipc_send_wire_locked(f, policy, gen) != 0) return -1;
    g_bt_autonext.active = 0;
    fprintf(stderr, "BT autonext experiment: restored gapless=%d\n", g_bt_autonext.restore);
    return 0;
}
static int ipc_send_locked(const char *frame, int policy, unsigned instance){
    bt_autonext_instance(instance);
    /* Restore before output mux, work/source mode or USB gadget changes, even
     * between polls. A failed restoration refuses the departure frame. */
    int departure = !strncmp(frame, "0666", 4) || !strncmp(frame, "0657", 4) || !strncmp(frame, "0642", 4);
    if(departure && bt_autonext_restore(policy, instance) != 0) return -1;
    if(g_bt_autonext.active &&
       (!strcmp(frame, "0647000C0000") || !strcmp(frame, "0647000C0001"))){
        g_bt_autonext.restore = frame[11] == '1';
        frame = "0647000C0001";
    }
    return ipc_send_wire_locked(frame, policy, instance);
}
void ipc_bt_autonext_poll(uint32_t now, int eligible, int preference){
    static uint32_t last;
    static int polled, previous_eligible, pending;
    /* Main thread only. Route departures also restore synchronously above. */
    if(polled && eligible == previous_eligible && (uint32_t)(now - last) < 1000) return;
    polled = 1; last = now; previous_eligible = eligible;
    int enabled = bt_autonext_mode() && eligible && (preference == 0 || preference == 1);
    if(!enabled && !pending) return; /* only this main-thread poll can activate */
    pthread_mutex_lock(&g_recov_mu);
    pthread_mutex_lock(&g_tx_mu);
    bt_autonext_instance(g_instance);
    if(g_confirmed){
        if(g_bt_autonext.active && (preference == 0 || preference == 1))
            g_bt_autonext.restore = preference;
        if(!enabled){
            (void)bt_autonext_restore(SEND_PROBE, g_instance);
        } else if(!g_bt_autonext.active &&
                  ipc_send_wire_locked("0647000C0001", SEND_PROBE, g_instance) == 0){
            g_bt_autonext.restore = preference;
            g_bt_autonext.active = 1;
            fprintf(stderr, "BT autonext experiment: stock gapless on (saved=%d)\n", preference);
        }
    }
    pending = g_bt_autonext.active;
    pthread_mutex_unlock(&g_tx_mu);
    pthread_mutex_unlock(&g_recov_mu);
}
/* One lock covers descriptor inspection, retries, close/reopen and health inspection.
 * Lock order: conversion reservation -> TX, and g_recov_mu -> TX (ipc_send_automatic); TX never enters conversion or
 * RX state. */
static int ipc_send_internal(const char *frame, int policy, unsigned instance){
    pthread_mutex_lock(&g_tx_mu);
    int rc = ipc_send_locked(frame, policy, instance);
    pthread_mutex_unlock(&g_tx_mu);
    return rc;
}
/* user-action send: a real failure raises the toast flag. */
static unsigned g_transport_seq; /* main-thread sends and recovery only */
unsigned ipc_transport_seq(void){ return g_transport_seq; }
int ipc_send_cmd(const char*frame){
    if(!strncmp(frame,"0100",4) || !strncmp(frame,"0103",4) || !strncmp(frame,"0201",4))
        g_transport_seq++; /* cancel recovery even if the transport send fails */
    return ipc_send_internal(frame, SEND_USER, ipc_instance());
}
/* background send (state-sync / health probe): silent, never toasts. */
int ipc_send_probe(const char*frame){ return ipc_send_internal(frame, SEND_PROBE, ipc_instance()); }
/* Snapshot and admission share the recovery lock; receipt is not track ownership. */
int ipc_confirmed_snapshot(unsigned *instance){
    pthread_mutex_lock(&g_recov_mu);
    if(instance) *instance=g_instance;
    int confirmed=g_confirmed;
    pthread_mutex_unlock(&g_recov_mu);
    return confirmed;
}
int ipc_send_automatic(const char *frame, unsigned instance){
    pthread_mutex_lock(&g_recov_mu);
    int r=IPC_SEND_DEFERRED;
    if(instance==g_instance && g_confirmed){
        r=ipc_send_internal(frame, SEND_AUTOMATIC, instance);
        /* Dispatch and cancellation serialize with RX; a reservation is not a send. */
        if(r==0 && !strcmp(frame,"0657000C000A") &&
           g_receiver_instance==instance && g_receiver_arm){
            g_local_publication.state=LOCAL_BLOCKED;
            g_local_publication.return_arm=0;
        }
    }
    pthread_mutex_unlock(&g_recov_mu);
    return r;
}
/* 1 once a player instance connected. */
int ipc_is_ready(void){
    pthread_mutex_lock(&g_recov_mu); int r=g_rx_ready; pthread_mutex_unlock(&g_recov_mu); return r;
}
/* 1 (and clears) if a command send failed since the last call - UI surfaces a toast. */
int ipc_take_send_error(void){ pthread_mutex_lock(&g_tx_mu); int e=g_send_err; g_send_err=0; pthread_mutex_unlock(&g_tx_mu); return e; }

/* Separate mailbox: scanner never enters the controller or holds IPC's state lock. */
static pthread_mutex_t g_library_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_library_epoch;
void ipc_note_library_commit(void){
    pthread_mutex_lock(&g_library_mu); g_library_epoch++; pthread_mutex_unlock(&g_library_mu);
}
uint64_t ipc_library_epoch(void){
    pthread_mutex_lock(&g_library_mu); uint64_t epoch=g_library_epoch; pthread_mutex_unlock(&g_library_mu);
    return epoch;
}

/* ---- stock still-image conversion (0810 value 5 + source path -> a810 result) ----
 * Like every other frame, both put the length before the value: the request is "0810" + %04X(strlen+12) + "0005" +
 * path, the reply "a810000C" + %04X(code), 0 = done. Disc capture 2026-11-01: "081000310005" +
 * "/tmp/sdcard/CustomCover/spongebob.png" was answered "a810000C0000" in 0.5 s. (An earlier reading put the value
 * first, which the player ignores, so every conversion timed out.) mq_player@0x4f169c (V2.57) handles 0810: value 5
 * with a non-NULL string at +0x18 runs set_custom_cover_by_file(path) (0x4812c8) and replies a810 through
 * 0x4ddaec(1, "a810", code, NULL): queue index 1 is the /ui queue (0x4dc944 stores the name), and the NULL-string
 * form is snprintf(buf, 13, ...) sent as 12 bytes (0x4ddc54..0x4ddc88), so a code wider than 4 hex digits keeps its
 * first 4: the converter's -1 arrives as FFFF. The reply has no operation id, so correlation is one pending
 * operation + player instance + deadline.
 * Value 5 with a NULL path is the GIF case: no conversion, no reply (mq_player@0x4f16dc: NULL string -> straight to
 * the LOCK_THEME store at 0x4f16b4); see ipc_gif_select. */
#define CONV_PREFIX "/tmp/sdcard/CustomCover/"   /* mq_ui picker folder, same path it sends (0x6a077c) */
#define CONV_PATH_MAX 255                        /* mq_ui builds the path in a 256-byte buffer (0x47c06c) */
static pthread_mutex_t g_conv_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { ipc_conv_state state; unsigned op, code, gen; uint64_t deadline; } g_conv;
static int g_apply_reserved; /* g_conv_mu; held through database COMMIT and recovery */
static unsigned g_conv_ops, g_conv_rejected, g_conv_ignored;   /* [g_conv_mu] */
static uint64_t conv_now_ms(void){
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec*1000u + (uint64_t)t.tv_nsec/1000000u;
}
/* Only the stock picker's flat folder and the formats the converter decodes (PNG/JPEG/BMP by magic,
 * 0x482f80). GIF is refused here: stock never converts it.
 * SD sources have their own lexical rules; stored assets use allowed_path().
 * Both use the shared component symlink check. */
static int conv_path_ok(const char *p){
    size_t n = p ? strlen(p) : 0, pl = sizeof(CONV_PREFIX)-1;
    if(n <= pl || n > CONV_PATH_MAX || strncmp(p, CONV_PREFIX, pl)) return 0;
    const char *name = p + pl, *dot = NULL;
    if(!strcmp(name,".") || !strcmp(name,"..")) return 0;
    for(const char *c = name; *c; c++){
        unsigned char u = (unsigned char)*c;
        if(u < 0x20 || u == 0x7f || u == '/') return 0;
        if(u == '.') dot = c;
    }
    return dot && dot[1] && (!strcasecmp(dot,".jpg") || !strcasecmp(dot,".jpeg") ||
                             !strcasecmp(dot,".png") || !strcasecmp(dot,".bmp")) &&
           custom_saver_path_no_symlinks(p);
}
/* Caller holds g_conv_mu. PENDING past its deadline or from an older player instance is UNRESOLVED. */
static void conv_settle(void){
    if(g_conv.state != IPC_CONV_PENDING) return;
    if(g_conv.gen != ipc_instance() || conv_now_ms() >= g_conv.deadline) g_conv.state = IPC_CONV_UNRESOLVED;
}
static void conv_new_instance(unsigned instance){
    (void)instance;
    pthread_mutex_lock(&g_conv_mu); conv_settle(); pthread_mutex_unlock(&g_conv_mu);
}
/* Main loop (parse_frame). ok = 0 for an a810 the decoder refused. Malformed or unexpected replies change nothing (the deadline
 * still bounds the operation). */
static void conv_on_reply(int ok, unsigned code){
    pthread_mutex_lock(&g_conv_mu);
    conv_settle();
    if(!ok) g_conv_rejected++;
    else if((g_conv.state != IPC_CONV_PENDING &&
             g_conv.state != IPC_CONV_UNRESOLVED) ||
            g_conv.gen != ipc_instance()) g_conv_ignored++;   /* nothing outstanding / released / other instance */
    else { g_conv.code = code; g_conv.state = code ? IPC_CONV_FAILED : IPC_CONV_DONE; }
    pthread_mutex_unlock(&g_conv_mu);
}
/* Caller holds g_conv_mu and has settled: an operation is pending, holds an unreleased result, or may still be
 * writing (UNRESOLVED in this player instance). */
static int conv_blocked(unsigned gen){
    (void)gen;
    return g_conv.state == IPC_CONV_PENDING || g_conv.state == IPC_CONV_DONE || g_conv.state == IPC_CONV_FAILED ||
           g_conv.state == IPC_CONV_UNRESOLVED;
}
int ipc_conv_busy(void){
    pthread_mutex_lock(&g_conv_mu); conv_settle();
    int busy = conv_blocked(ipc_instance());
    pthread_mutex_unlock(&g_conv_mu);
    return busy;
}
int ipc_apply_reserve(void){
    pthread_mutex_lock(&g_conv_mu); conv_settle();
    int ok = !g_apply_reserved && !conv_blocked(ipc_instance());
    if(ok) g_apply_reserved = 1;
    pthread_mutex_unlock(&g_conv_mu);
    return ok ? 0 : -1;
}
void ipc_apply_release(void){
    pthread_mutex_lock(&g_conv_mu); g_apply_reserved = 0; pthread_mutex_unlock(&g_conv_mu);
}
/* Pathless 0810 values 0..4 select system themes; 5 reselects saved Custom without conversion.
 * mq_player@0x4f16ac-0x4f16e0: both routes store config id 37. Live effect unverified. */
int ipc_saver_reselect(unsigned style){
    if(style > 5 || !ipc_is_ready()) return -1;
    char frame[13]; snprintf(frame, sizeof frame, "0810000C%04X", style);
    return ipc_send_internal(frame, SEND_PROBE, ipc_instance());
}
/* Stock's GIF selection: mq_ui@0x47c628 calls 0x48b28c("0810", 5, NULL), whose NULL-string form is the 12-byte
 * "0810000C0005" (format 0x6a38e0, 0x48b3bc..0x48b3dc; length first, see the conversion above). The lock must be
 * held through the send so a conversion cannot start between the check and the frame. */
ipc_gif_select_status ipc_gif_select(void){
    if(!ipc_is_ready()) return IPC_GIF_SELECT_NOT_READY;
    pthread_mutex_lock(&g_conv_mu); conv_settle();
    ipc_gif_select_status st = conv_blocked(ipc_instance()) ? IPC_GIF_SELECT_BUSY :
        ipc_send_internal("0810000C0005", SEND_PROBE, ipc_instance()) ? IPC_GIF_SELECT_SEND : IPC_GIF_SELECT_OK;
    pthread_mutex_unlock(&g_conv_mu);
    return st;
}
ipc_conv_start_status ipc_conv_start(const char *src, unsigned *op){
    if(!conv_path_ok(src)) return IPC_CONV_START_PATH;
    if(!ipc_is_ready()) return IPC_CONV_START_NOT_READY;
    char frame[CONV_PATH_MAX + 13];
    snprintf(frame, sizeof frame, "0810%04X0005%s", (unsigned)(strlen(src) + 12), src);
    unsigned gen = ipc_instance();
    pthread_mutex_lock(&g_conv_mu);
    conv_settle();
    /* A result must be released first. UNRESOLVED blocks because neither a timeout
     * nor a queue reattachment proves that the converter stopped writing. */
    if(g_apply_reserved || conv_blocked(gen)){
        pthread_mutex_unlock(&g_conv_mu); return IPC_CONV_START_BUSY;
    }
    g_conv.state = IPC_CONV_PENDING; g_conv.code = 0; g_conv.gen = gen;   /* armed BEFORE the send: the reply can race it */
    g_conv.deadline = conv_now_ms() + IPC_CONV_TIMEOUT_MS;
    if(!++g_conv_ops) g_conv_ops = 1;
    g_conv.op = g_conv_ops;
    unsigned id = g_conv.op;
    pthread_mutex_unlock(&g_conv_mu);
    if(ipc_send_internal(frame, SEND_PROBE, ipc_instance()) != 0){
        pthread_mutex_lock(&g_conv_mu);
        if(g_conv.op == id && g_conv.state == IPC_CONV_PENDING) g_conv.state = IPC_CONV_IDLE;
        pthread_mutex_unlock(&g_conv_mu);
        return IPC_CONV_START_SEND;
    }
    if(op) *op = id;
    return IPC_CONV_START_OK;
}
ipc_conv_state ipc_conv_poll(unsigned op, unsigned *code){
    pthread_mutex_lock(&g_conv_mu);
    conv_settle();
    ipc_conv_state st = op == g_conv.op && op ? g_conv.state : IPC_CONV_STALE;
    if(code) *code = st == IPC_CONV_FAILED ? g_conv.code : 0;
    pthread_mutex_unlock(&g_conv_mu);
    return st;
}
int ipc_conv_release(unsigned op){
    pthread_mutex_lock(&g_conv_mu);
    conv_settle();
    int ok = op && op == g_conv.op &&
             (g_conv.state == IPC_CONV_DONE || g_conv.state == IPC_CONV_FAILED);
    if(ok) g_conv.state = IPC_CONV_IDLE;
    pthread_mutex_unlock(&g_conv_mu);
    return ok ? 0 : -1;
}
