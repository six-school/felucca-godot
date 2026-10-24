/* The board file (docs/DESIGN.md D2): Felucca's browser build, web/emu/felucca_web.c, included as it is and driven
 * headless by a game engine. On top of it this file owns the render loop around web_ms() (one device millisecond),
 * feeds PR 269's external-clock grid from the caller's timeline B (FELUCCA_HOST_CLOCK, patch 0002), and offers the
 * firmware's state as the table of core/fel_core.h.
 *
 * One compiled copy of this file is one instance. core/build.sh compiles it N times with every global the firmware
 * defines renamed (D3); the copy's own export is fel_core_<FEL_COPY>() and nothing else.
 *
 * Timing: the firmware renders I2S halves of 128 frames, each as four 32-sample blocks; events_block runs once per
 * block and asks host_clock_units() (below) how far the music moved in it. Frames rendered ahead (at most 127) wait
 * in felucca_web.c's ring for the next render() call; their blocks were clocked with that call's B, extrapolated, and
 * the next call's B takes over from its first new block: units come from the absolute position, so nothing drifts. */
#ifndef FEL_COPY
#define FEL_COPY 0
#endif
#define FELUCCA_HOST_CLOCK 1
#include "web/emu/felucca_web.c"
#include "fel_core.h"

_Static_assert(PROJ_STORE_SIZE == FEL_PROJECT_SIZE && NTRK == FEL_TRACKS && FS == FEL_RATE, "fel_core.h's constants");

#define FEL_BOOT_MS 1500u               /* device time run at boot: past the splash (430 ms) and the LED sweep, as every
                                         * prototype measured */

/* ------------------------------------------------------------ the host clock --- */
static uint32_t hclk_f0;                /* the ring index render()'s b0 belongs to (its first frame) */
static double hclk_b0, hclk_rate;       /* B there, beats per frame for this render() */
static uint32_t hclk_half, hclk_in;     /* the ring index the half being rendered starts at, its samples so far */
static double hclk_origin;              /* B of step 1: play()'s at */
static uint64_t hclk_fed;               /* units of MIDI_CLOCK_GRID since hclk_origin given to the firmware */
static uint8_t hclk_armed;              /* play() waits for B to reach hclk_origin */
static uint64_t fel_frames;             /* frames render() returned */

/* events_block, every block of n samples (patch 0002): B at the block's end -> the units the music moved in it.
 * Playing or not: stopped, the arps and the beat still run on B, as they run on the internal clock. */
static uint32_t host_clock_units(uint32_t n)
{
    double b = hclk_b0 + (double)(int32_t)(hclk_half + (hclk_in += n) - hclk_f0) * hclk_rate, due;
    uint64_t total;
    if (hclk_armed && b > hclk_origin) {        /* B reaches play()'s at inside this block: step 1 starts in it, as a
                                                 * MIDI Start in its queue would (the steps are ticked after this) */
        double start = b - n * hclk_rate;
        if (start > hclk_origin)                /* late (at was in the past): from this block, not a fast-forward */
            hclk_origin = start;
        hclk_armed = 0;
        hclk_fed = 0;
        midi_clock_transport(0xFAu, fm1_ms);
    }
    due = (b - hclk_origin) * MIDI_CLOCK_GRID;
    if (!(due > (double)hclk_fed && due < 9e15))   /* behind (B stood still or went back): hold until it is reached
                                                    * again. (NaN, infinite: hold) */
        return 0;
    total = (uint64_t)floor(due + 1e-9) - hclk_fed;
    n = total > MIDI_CLOCK_GRID / 2u ? MIDI_CLOCK_GRID / 2u : (uint32_t)total;   /* (midi_grid_advance's bound; a
                                                                                  * jump in B: M2's locate) */
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
    if (!(bpm >= 40.0))                 /* (NaN too) */
        bpm = 40.0;
    if (bpm > 240.0)
        bpm = 240.0;
    midi_clock_retempo(beat_samples(), (uint32_t)lround(60.0 * FS / bpm));   /* every phase keeps its musical place */
    song.g[G_BPM] = (int16_t)lround(bpm);
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

static void core_play(double at_beats)
{
    if (!(at_beats > -1e15 && at_beats < 1e15))        /* (NaN, infinite: ignored) */
        return;
    hclk_origin = at_beats;
    hclk_armed = 1;
}

static void core_stop(void)
{
    hclk_armed = 0;
    if (song.playing)
        midi_clock_transport(0xFCu, fm1_ms);
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
    s->playing = song.playing;
    s->beats = (double)hclk_fed / MIDI_CLOCK_GRID;
    s->frames = fel_frames;
    for (t = 0; t < NTRK; t++) {
        int fresh = trk[t].seq_pos >= 0x7FFFFFFFu;     /* (PLAY: step 1 is next) */
        s->track[t].step = fresh ? 0u : trk[t].seq_idx;
        s->track[t].pos = fresh ? 0u : trk[t].seq_pos;
    }
}

static const fel_core_t FEL_CORE = {
    FEL_CORE_ABI, core_boot, core_render, core_tempo, core_param_get, core_param_set, core_global_get,
    core_global_set, core_engine_get, core_engine_set, core_project_import, core_project_export, core_play, core_stop,
    core_note, core_snapshot,
};
#define FEL_CAT2(a, b) a##b
#define FEL_CAT(a, b) FEL_CAT2(a, b)
const fel_core_t *FEL_CAT(fel_core_, FEL_COPY)(void) { return &FEL_CORE; }
