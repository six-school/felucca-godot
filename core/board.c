/* The board file (docs/DESIGN.md D2): Felucca's browser build, web/emu/felucca_web.c, included as it is and driven
 * headless by a game engine. On top of it this file owns the render loop around web_ms() (one device millisecond),
 * feeds PR 269's external-clock grid from the conductor's timeline (FELUCCA_HOST_CLOCK, patch 0002), and offers
 * the firmware's state as the table of core/fel_core.h.
 *
 * One compiled copy of this file is one instance. core/build.sh compiles it N times with every global the firmware
 * defines renamed (D3); the copy's own export is fel_core_<FEL_COPY>() and nothing else.
 *
 * Timing: the firmware renders I2S halves of 128 frames, each as four 32-sample blocks; events_block runs once per
 * block and asks host_clock_units() (below) how far the music moved in it. Frames rendered ahead (at most 127) wait
 * in felucca_web.c's ring for the next render() call; their blocks were clocked with that call's conductor's timeline,
 * extrapolated, and the next call's takes over from its first new block: units come from the absolute position, so
 * nothing drifts. */
#ifndef FEL_COPY
#define FEL_COPY 0
#endif
#define FELUCCA_HOST_CLOCK 1
#define FELUCCA_DELAY_GLIDE 1           /* patch 0003: a new delay TIME (the tempo moved) glides there, no click (D9) */
#include "web/emu/felucca_web.c"
#include "fel_core.h"

_Static_assert(PROJ_STORE_SIZE == FEL_PROJECT_SIZE && NTRK == FEL_TRACKS && FS == FEL_RATE && P_COUNT <= FEL_PARAMS &&
               G_COUNT <= FEL_GLOBALS, "fel_core.h's constants");

#define FEL_BOOT_MS 1500u               /* device time run at boot: past the splash (430 ms) and the LED sweep, as every
                                         * prototype measured */

/* ------------------------------------------------------------ the host clock --- */
static uint32_t hclk_f0;                /* the ring index render()'s b0 belongs to (its first frame) */
static double hclk_b0, hclk_rate;       /* the conductor's timeline there, beats per frame for this render() */
static uint32_t hclk_half, hclk_in;     /* the ring index the half being rendered starts at, its samples so far */
static double hclk_origin;              /* the conductor's timeline at step 1: play()'s at */
static uint64_t hclk_fed;               /* units of MIDI_CLOCK_GRID since hclk_origin given to the firmware */
static uint8_t hclk_armed;              /* play() waits for the conductor's timeline to reach hclk_origin */
static uint8_t hclk_locate;             /* locate() asked: applied in the next block */
static uint64_t fel_frames;             /* frames render() returned */
static uint32_t fel_loops[NTRK];        /* each track's passes through its LEN since step 1 (snapshot) */
static uint16_t fel_last_idx[NTRK];     /* its step and position when last looked at: a step back is a pass */
static uint32_t fel_last_pos[NTRK];

static void fel_locate(uint64_t units);

/* the passes: looked at before every block (a step lasts more than one) and when a snapshot is taken */
static void fel_loops_count(void)
{
    uint32_t k;
    if (!song.playing)
        return;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        if (t->seq_pos >= 0x7FFFFFFFu)          /* (PLAY: step 1 is next) */
            continue;
        if (t->seq_idx < fel_last_idx[k] ||
            (t->seq_idx == fel_last_idx[k] && t->p[P_SLEN] <= 1 && t->seq_pos < fel_last_pos[k]))   /* (LEN 1) */
            fel_loops[k]++;
        fel_last_idx[k] = t->seq_idx;
        fel_last_pos[k] = t->seq_pos;
    }
}
static void fel_loops_reset(void)
{
    memset(fel_loops, 0, sizeof fel_loops);
    memset(fel_last_idx, 0, sizeof fel_last_idx);
    memset(fel_last_pos, 0, sizeof fel_last_pos);
}

/* events_block, every block of n samples (patch 0002): the conductor's timeline at the block's end -> the units the
 * music moved in it. Playing or not: stopped, the arps and the beat still run on the conductor's timeline, as they run
 * on the internal clock. */
static uint32_t host_clock_units(uint32_t n)
{
    double b = hclk_b0 + (double)(int32_t)(hclk_half + (hclk_in += n) - hclk_f0) * hclk_rate, due;
    double start = b - n * hclk_rate;           /* the conductor's timeline at the block's start */
    uint64_t total;
    fel_loops_count();
    if (hclk_locate) {
        hclk_locate = 0;
        if (start < hclk_origin)                /* the origin is ahead: as play() */
            hclk_armed = 1;
        else if (start < hclk_origin + 1e9)     /* (the units fit; NaN: no) */
            fel_locate((uint64_t)floor((start - hclk_origin) * MIDI_CLOCK_GRID + 1e-9));
    }
    if (hclk_armed && b > hclk_origin) {        /* the conductor's timeline reaches play()'s at inside this block:
                                                 * step 1 starts in it, as a MIDI Start in its queue would (the steps
                                                 * are ticked after this) */
        if (start > hclk_origin)                /* late (at was in the past): from this block, not a fast-forward */
            hclk_origin = start;
        hclk_armed = 0;
        hclk_fed = 0;
        midi_clock_transport(0xFAu, fm1_ms);
        fel_loops_reset();
        return 0;                               /* no time in it: the firmware drops a start block's own (seq_tick's
                                                 * PLAY entry), so the next block's units, from hclk_origin, put the
                                                 * sequencer's zero exactly at it, not a block late */
    }
    due = (b - hclk_origin) * MIDI_CLOCK_GRID;
    if (!(due > (double)hclk_fed && due < 9e15))   /* behind (the conductor's timeline stood still or went back): hold
                                                    * until it is reached again. (NaN, infinite: hold) */
        return 0;
    total = (uint64_t)floor(due + 1e-9) - hclk_fed;
    n = total > MIDI_CLOCK_GRID / 2u ? MIDI_CLOCK_GRID / 2u : (uint32_t)total;   /* (midi_grid_advance's bound; a
                                                                                  * jump in the conductor's timeline:
                                                                                  * M2's locate) */
    hclk_fed += n;
    return n;
}

/* one device millisecond: web_ms(), with the half it renders (if one is due) placed in the ring for the clock */
static void fel_ms(void)
{
    if (web_samples_due + FS >= 1000u * HALF_FRAMES) {
        hclk_half = ring_w;
        hclk_in = 0;
    }
    web_ms();
}

/* ------------------------------------------------------------ locate (DESIGN.md §6.4) --- */
/* NUDGE and the matrix's TIME of the step t lands in, pos samples into it, as seq_nd_start / seq_play_on latched them
 * when continuous play entered it: a late play still to come is latched again (it sounds, as it would have); one due
 * already is gone (no missed step sounds late); the next step's early play, latched, or marked played (nd_fired) when
 * it is past */
static void fel_locate_nudge(track_t *t, uint32_t period, uint32_t len, uint32_t pos)
{
    uint32_t idx = t->seq_idx, nx = (idx + 1u) % len, late = 0;
    int32_t d;
    if (t->p[P_SQNT]) {                                 /* QUANTIZE ON: on its start, or late by TIME only */
        late = seq_time(t, idx, period);
        if (late && pos < late)
            t->nd_late = late + 1u;
        return;
    }
    d = seq_nudge(t, &seq_steps(t)[idx]);
    late = (uint32_t)(d > 0 ? d : 0) * period / NUDGE_DIV + seq_time(t, idx, period);
    if (late && pos < late)
        t->nd_late = late + 1u;
    if ((!chain.running || nx) && (d = seq_nudge(t, &seq_steps(t)[nx])) < 0) {
        uint32_t at = step_samples(t, period, idx), back = (uint32_t)-d * period / NUDGE_DIV;
        at = (at > back ? at - back : 0u) + 1u;
        at = at < t->nd_late ? t->nd_late : at;
        if (pos >= at - 1u)
            t->nd_fired = (uint8_t)(nx + 1u);
        else
            t->nd_early = at;
    }
}

/* the automation of the steps of this pass up to idx (motion_step on each since step 1; seq_start's motion_begin put
 * the base values back): each parameter at its latest record, the later in the list on one step. Not the locks:
 * they hold on the step that plays them only, and the one it lands in does not play */
static void fel_locate_motion(track_t *t, uint32_t idx)
{
    const motion_store_t *m = chain_motion(t);
    uint32_t k = trk_index(t), i;
    int8_t best[P_COUNT];
    if (!m || !((m->on >> k) & 1u))
        return;
    memset(best, -1, sizeof best);
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        uint32_t id = MOTION_ID(e), s = e->place & 63u;
        if ((e->place >> 6) != k || (e->param & MOTION_LOCK) || id >= P_COUNT || s > idx || (int32_t)s < best[id])
            continue;
        best[id] = (int8_t)s;
        t->p[id] = (int16_t)param_fit(param_desc_of(eng_idx(t->eng_req), id), e->value);
        motion_active[k][id / 32u] |= 1u << (id % 32u);
    }
}

/* the sequencer as continuous play from step 1 would have it `units` (MIDI_CLOCK_GRID a beat) later, at the current
 * tempo and patterns: what midi_grid_advance and seq_tick would have summed (exact while neither changed). The state
 * that runs on samples, not on the clock (the FX layer's grid, SLICER, a FREE synced LFO 2), is set from the same
 * time at the current tempo: in phase, though the timeline's pauses do not stop them */
static void fel_locate(uint64_t units)
{
    uint32_t q = beat_samples(), k;
    uint64_t s, el;
    if (song.playing)
        seq_stop();                                     /* what sounds goes: notes, nudges, ratchets, the arps' note */
    seq_start();                                        /* PLAY's state: motion bases, grids, conditions, slicer, .. */
    fel_loops_reset();
    hclk_fed = units;
    el = units * q / MIDI_CLOCK_GRID;                   /* samples since step 1 at the beat length */
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t div = (uint32_t)t->p[P_SDIV], len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
        uint32_t period = div_samples(div), qd = div < 6u ? q / DIV_DEN[div] * DIV_DEN[div] : q, idx = 0, i;
        uint64_t pass = 0, loops, r;
        s = units * qd;                                 /* midi_grid_advance: the samples and the carried remainder */
        midi_seq_grid[k].remainder = (uint32_t)(s % MIDI_CLOCK_GRID);
        s /= MIDI_CLOCK_GRID;
        for (i = 0; i < len; i++)
            pass += step_samples(t, period, i);
        if (!s || !pass)                                /* at step 1's start: PLAY's entry, as seq_start left it */
            continue;
        /* a position on a step's end belongs to that step (r == its length): the next one enters in this block and
         * plays, as it starts here */
        loops = (s - 1u) / pass;
        r = s - loops * pass;
        while (r > step_samples(t, period, idx))
            r -= step_samples(t, period, idx++);
        t->seq_idx = (uint16_t)idx;
        t->seq_pos = (uint32_t)r;
        t->cond_loop = (uint8_t)(loops % COND_LOOPS);
        fel_loops[k] = (uint32_t)loops;
        fel_last_idx[k] = (uint16_t)idx;
        fel_last_pos[k] = (uint32_t)r;
        fel_locate_nudge(t, period, len, (uint32_t)r);
        fel_locate_motion(t, idx);
        if (t->p[P_LTRIG] && t->p[P_LSYNC])            /* LFO 2 FREE + SYNC: in time with the bars from PLAY */
            t->lfo_ph = ((uint32_t)t->p[P_LPHASE] << 25) + (uint32_t)(el / CTL * lfo_sync_inc(t));
        {                                               /* SLICER (sl_enter): its 16 swung steps of a note value */
            sl_t *sl_k = &sl[k];
            uint32_t base = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM] / SL_DEN[(uint32_t)t->p[P_SLRATE] % 6u];
            uint32_t len0 = swing_step_len(t, base, 0), st;
            uint64_t pr = el % (2u * (uint64_t)base);
            st = (uint32_t)(el / (2u * (uint64_t)base) * 2u);
            if (pr >= len0) {
                st++;
                pr -= len0;
            }
            sl_k->idx = (uint8_t)(st & 15u);
            sl_k->base = base;
            sl_k->len = swing_step_len(t, base, sl_k->idx);
            sl_k->pos = (uint32_t)pr;
            sl_k->bit = (uint8_t)((sl_pattern(t) >> sl_k->idx) & 1u);
            sl_k->rp = sl_k->loop = sl_k->rec = sl_k->rec_on = 0;
        }
    }
    s = units * (q / 4u * 4u);                          /* the metronome (click_tick: DIV 1/16's grid) */
    midi_click_grid.remainder = (uint32_t)(s % MIDI_CLOCK_GRID);
    s /= MIDI_CLOCK_GRID;
    clk_pos = (uint32_t)(s % div_samples(2));
    clk_step = (uint32_t)(s / div_samples(2)) & 15u;
    s = units * q;                                      /* the beat (the ARP LED's, events_block) */
    midi_beat_grid.remainder = (uint32_t)(s % MIDI_CLOCK_GRID);
    s /= MIDI_CLOCK_GRID;
    beat_pos = (uint32_t)(s % q);
    beat_n = (uint32_t)(s / q) & 3u;
    pf.ph = (uint32_t)el;                               /* the FX layer's 1/16s (perform.c) */
    pf.sync = 0;
    song.tick = (uint32_t)(el / CTL);
}

/* ------------------------------------------------------------ the table --- */
static void core_boot(void)
{
    if (web_booted)
        return;
    web_nor_erase();                    /* nothing persists (R13): an erased flash, settings and slots at defaults */
    web_master(1023);                   /* MASTER at its top (unity): the level is the caller's */
    web_boot();
    song.g[G_CLOCK] = 1;                /* an external clock (USB's): PR 269's grid, which host_clock_units feeds */
    song.g[G_ROUTE] = 0;                /* MIDI IN CH1-4: note() plays track t on channel t */
    host_clock_on = 1;
    while ((int32_t)(fm1_ms - FEL_BOOT_MS) < 0)
        fel_ms();
    ring_r = ring_w;                    /* the boot's audio: dropped */
    midi_clock_retempo(beat_samples(), (uint32_t)FS * 60u / 120u);   /* (the clock change zeroed the beat length) */
    song.g[G_BPM] = 120;
}

static void core_render(uint32_t n, double b0, double b1, float *l, float *r)
{
    uint32_t k;
    if (!web_booted) {
        for (k = 0; k < n; k++) {
            if (l) l[k] = 0.0f;
            if (r) r[k] = 0.0f;
        }
        return;
    }
    hclk_f0 = ring_r;
    hclk_b0 = b0;
    hclk_rate = n && b1 > b0 ? (b1 - b0) / n : 0.0;   /* (b1 < b0 or NaN: stands still) */
    for (k = 0; k < n; k++) {
        uint32_t i;
        while (ring_w == ring_r)
            fel_ms();
        i = (ring_r++ % RING) * 2u;
        if (l) l[k] = ring[i];
        if (r) r[k] = ring[i + 1u];
    }
    fel_frames += n;
}

static void core_tempo(double bpm)
{
    uint32_t k;
    if (!(bpm >= 40.0))                 /* (NaN too) */
        bpm = 40.0;
    if (bpm > 240.0)
        bpm = 240.0;
    midi_clock_retempo(beat_samples(), (uint32_t)lround(60.0 * FS / bpm));   /* every phase keeps its musical place */
    song.g[G_BPM] = (int16_t)lround(bpm);
    for (k = 0; k < NTRK; k++)                          /* (rescaled positions are no step back) */
        fel_last_pos[k] = trk[k].seq_pos;
}

static int fel_track_ok(int track) { return track >= 0 && track < NTRK; }

static int32_t core_param_get(int track, int id)
{
    if (!fel_track_ok(track) || id < 0 || id >= P_COUNT)
        return FEL_INVALID;
    return trk[track].p[id];
}

/* editor.c ED_TRACK_PARAM: the descriptor of the engine asked for (not the one still fading out), a fixed value stays,
 * an alias lands on its original, the motion sequencer's base follows; a running SONG section owns LEN DIV SWING
 * GATE QNT */
static int32_t core_param_set(int track, int id, int32_t v)
{
    track_t *t;
    const param_desc_t *d;
    if (!fel_track_ok(track) || id < 0 || id >= P_COUNT)
        return FEL_INVALID;
    t = &trk[track];
    if (chain_busy() && chain_owns((uint32_t)id))
        return t->p[id];
    d = param_desc_of(t->eng_req % NENGINES, (uint32_t)id);
    if (d->max > d->min)
        t->p[id] = (int16_t)enum_orig(d, clamp(v, d->min, d->max));
    (void)motion_capture(t, (uint32_t)id, t->p[id]);
    ui.force = 1;
    return t->p[id];
}

static int32_t core_global_get(int id)
{
    if (id < 0 || id >= G_COUNT)
        return FEL_INVALID;
    return song.g[id];
}

/* the globals that are the sound's. Not: BPM (tempo()), CLK and ROUT (the core's: a clock change stops everything),
 * SLOT NAME LOAD SAVE CLRSQ INIT ENG SET (the UI's actions and slots), and the ones on no page (MIDI SYNC CPU, the
 * inert drum part's) */
static int fel_global_writable(int id)
{
    switch (id) {
    case G_SWING: case G_TUNE: case G_DTIME: case G_DFDBK: case G_DCOLOR: case G_DMIX:
    case G_RSIZE: case G_RDAMP: case G_CRATE: case G_CDEPTH: case G_RTYPE:
        return 1;
    default:
        return 0;
    }
}

static int32_t core_global_set(int id, int32_t v)          /* editor.c ED_SET of a global */
{
    const param_desc_t *d;
    if (id < 0 || id >= G_COUNT || !fel_global_writable(id))
        return FEL_INVALID;
    d = &GP[id];
    if (d->max > d->min)
        song.g[id] = (int16_t)enum_orig(d, clamp(v, d->min, d->max));
    ui.force = 1;
    return song.g[id];
}

static int32_t core_engine_get(int track) { return fel_track_ok(track) ? trk[track].eng_req : FEL_INVALID; }

static int32_t core_engine_set(int track, int engine)
{
    if (!fel_track_ok(track) || engine < 0 || engine >= NENGINES)
        return FEL_INVALID;
    set_engine_of(&trk[track], (uint32_t)engine);       /* (DIGITAL, retired: FM6's first preset) */
    return trk[track].eng_req;
}

static project_t fel_proj;
static project_store_t fel_store;       /* word-aligned: proj_import reads the header as uint32_t */

static int32_t core_project_import(const uint8_t *b, uint32_t n)
{
    if (!b || n > sizeof fel_store)
        return 1;
    memcpy(fel_store.raw, b, n);
    if (!proj_import(&fel_proj, fel_store.raw, (int)n) || project_restore_runtime(&fel_proj))
        return 1;
    /* a project brings the device's CLK and MIDI IN: the core's own again before events_block sees them (a clock
     * change would stop and reset the clock) */
    song.g[G_CLOCK] = 1;
    song.g[G_ROUTE] = 0;
    return 0;
}

static uint32_t core_project_export(uint8_t out[FEL_PROJECT_SIZE])
{
    if (!out)
        return 0;
    project_capture(&fel_proj);
    if (!proj_pack(&fel_store, &fel_proj))
        return 0;
    memcpy(out, fel_store.raw, PROJ_STORE_SIZE);
    return PROJ_STORE_SIZE;
}

static project_t fel_info_proj;          /* project_info's own (import may run while the instance plays) */
static project_store_t fel_info_store;

static int32_t core_project_info(const uint8_t *b, uint32_t n, fel_project_info_t *out)
{
    uint32_t k;
    if (!b || !out || n > sizeof fel_info_store)
        return 1;
    memcpy(fel_info_store.raw, b, n);
    if (!proj_import(&fel_info_proj, fel_info_store.raw, (int)n) || !proj_ok(&fel_info_proj))
        return 1;
    memset(out, 0, sizeof *out);
    (void)proj_name_get(out->name, (const uint8_t *)fel_info_proj.name);
    out->bpm = clamp(fel_info_proj.g[G_BPM], GP[G_BPM].min, GP[G_BPM].max);
    for (k = 0; k < NTRK; k++) {
        uint32_t e = fel_info_proj.t[k].engine, i;
        out->engine[k] = (int32_t)e;
        for (i = 0; i < P_COUNT; i++)                   /* (project_restore_runtime: param_fit to the engine's) */
            out->p[k][i] = (int16_t)param_fit(param_desc_of(e, i), fel_info_proj.t[k].p[i]);
    }
    for (k = 0; k < G_COUNT; k++)
        out->g[k] = (int16_t)clamp(fel_info_proj.g[k], GP[k].min, GP[k].max);
    return 0;
}

static const char *core_engine_name(int engine) { return engine >= 0 && engine < NENGINES ? ENGINES[engine]->name : 0; }

static void core_play(double at_beats)
{
    if (!(at_beats > -1e15 && at_beats < 1e15))        /* (NaN, infinite: ignored) */
        return;
    hclk_origin = at_beats;
    hclk_armed = 1;
    hclk_locate = 0;
}

static int32_t core_locate(double origin_beats)
{
    if (!(origin_beats > -1e15 && origin_beats < 1e15) || chain.running || chain.armed)
        return 1;                                       /* (a SONG chain: not yet, DESIGN.md §6.4) */
    hclk_origin = origin_beats;
    hclk_armed = 0;
    hclk_locate = 1;
    return 0;
}

static void core_stop(void)
{
    hclk_armed = hclk_locate = 0;
    if (song.playing)
        midi_clock_transport(0xFCu, fm1_ms);
}

/* fel_core.h cut: what an import leaves ringing (it only releases the notes, as the device's project load does),
 * silenced. One translation unit with the firmware: its statics are ours to reach, as locate's are */
static void core_cut(void)
{
    uint32_t k, i;
    core_stop();
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        midi_forget_track(k);                           /* (seq.c's panic, at once: notes held, the arp's, chords) */
        trk_all_off(t);
        t->nheld = 0;
        t->arp_phys = 0;
        t->latched = 0;
        arp_forget(t);
        for (i = 0; i < NVOICE; i++) {                  /* no release: off now, and a fresh voice for the next note
                                                         * (a PHYS string restarts from rest on env == env_out == 0) */
            voice_t *v = &t->v[i];
            v->gate = v->active = v->stage = 0;
            v->env = v->env_out = 0;
        }
        t->nmono = 0;
        t->mono_note = 0;
        t->xf_on = t->xf = 0;                           /* (an engine switch's fade: nothing left to fade) */
        t->xp_n = 0;
        t->tail = 0;                                    /* (blocks of DIST / INSERT to run out: none) */
        t->dist_hp = t->dist_lp1 = t->dist_lp2 = 0;
        ins_rest(t);
        memset(&sl[k], 0, sizeof sl[k]);                /* SLICER: no repeat playing (STUT replays its recording
                                                         * with no note held), no gate closing */
        sl[k].idx = 15;                                 /* (as slicer_start: the next block enters step 0) */
    }
    memset(sl_buf, 0, sizeof sl_buf);                   /* (STUT's recordings; the FX layer's buffer effects too) */
    memset(dly_buf, 0, sizeof dly_buf);
    fx.dly_lp = 0;
    memset(cho_buf, 0, sizeof cho_buf);
    rev_clear();                                        /* ROOM, SPRING; HALL's lines with them, */
    memset(&hl, 0, sizeof hl);                          /* .. its line ends and half-rate blocks (sz 0: its gains are
                                                         * made again) */
    memset(fl_buf, 0, sizeof fl_buf);
    dc_l = dc_r = dce_l = dce_r = 0;                    /* the master: its DC blockers (~2 Hz: a DC-heavy sound's
                                                         * state lets go over a few hundred ms), */
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = 0;                  /* .. the speaker's LOWCUT, BASS+ */
    memset(lce, 0, sizeof lce);
    sb_lp1 = sb_lp2 = sb_lp3 = sb_lp4 = sb_env = sb_h1 = sb_h2 = sb_hl = 0;
    lim_env = LIM_T;                                    /* .. and its limiter, at rest */
    for (i = ring_r; i != ring_w; i++)                  /* rendered ahead (up to 127 frames): the old sound */
        ring[(i % RING) * 2u] = ring[(i % RING) * 2u + 1u] = 0.0f;
}

static void core_note(int track, int note, int vel)
{
    if (!fel_track_ok(track) || note < 0 || note > 127)
        return;
    vel = vel < 0 ? 0 : vel > 127 ? 127 : vel;
    (void)web_midi((vel ? 0x90u : 0x80u) | (uint32_t)track, (uint32_t)note, (uint32_t)vel);   /* (a full queue:
                                                                                               * dropped, as MIDI) */
}

static void core_snapshot(fel_snapshot_t *s)
{
    uint32_t t;
    fel_loops_count();                                  /* (blocks rendered since the last look) */
    s->playing = song.playing;
    s->beats = (double)hclk_fed / MIDI_CLOCK_GRID;
    s->frames = fel_frames;
    for (t = 0; t < NTRK; t++) {
        int fresh = trk[t].seq_pos >= 0x7FFFFFFFu;     /* (PLAY: step 1 is next) */
        s->track[t].step = fresh ? 0u : trk[t].seq_idx;
        s->track[t].pos = fresh ? 0u : trk[t].seq_pos;
        s->track[t].loop = fel_loops[t];
    }
}

static const fel_core_t FEL_CORE = {
    FEL_CORE_ABI, core_boot, core_render, core_tempo, core_param_get, core_param_set, core_global_get,
    core_global_set, core_engine_get, core_engine_set, core_project_import, core_project_export, core_project_info,
    core_engine_name, core_play, core_locate, core_stop, core_cut, core_note, core_snapshot,
};
#define FEL_CAT2(a, b) a##b
#define FEL_CAT(a, b) FEL_CAT2(a, b)
const fel_core_t *FEL_CAT(fel_core_, FEL_COPY)(void) { return &FEL_CORE; }
