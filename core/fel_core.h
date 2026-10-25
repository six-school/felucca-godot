/* The core seam (docs/DESIGN.md §4.1, §5.1): one Felucca instance as a table of entry points. Every compiled copy of
 * the firmware (core/board.c, renamed per copy: D3) exports exactly one symbol, fel_core_<n>(), so N copies link into
 * one binary as N independent instruments. Nothing above this table sees a firmware symbol.
 *
 * Threads: every call for one instance comes from one thread at a time (the audio thread once it is bound; a worker
 * before that). Nothing here allocates, locks or logs.
 * Time: the instance runs at FEL_RATE; render() advances its host clock along the conductor's timeline (in
 * beats). */
#ifndef FEL_CORE_H
#define FEL_CORE_H
#include <stdint.h>

#define FEL_CORE_ABI 4                  /* bump on any change to this file (2: locate, the passes in the snapshot;
                                         * 3: project_info, engine_name; 4: project_info's values) */
#define FEL_RATE 44100                  /* the FM-1's rate: the caller resamples */
#define FEL_TRACKS 4
#define FEL_PROJECT_SIZE 3840           /* a project store object (FUN10), as the web app exports it */
#define FEL_INVALID INT32_MIN           /* a parameter call on a track / id that does not exist, or one the core owns */
#define FEL_PARAMS 128                  /* track parameter ids are below this (the formats keep P_COUNT under 128) */
#define FEL_GLOBALS 32                  /* .. global ids */

typedef struct {
    uint32_t playing;
    double beats;                       /* musical position: beats since the play() that started it (as rendered) */
    uint64_t frames;                    /* frames render() has returned since boot */
    struct { uint32_t step, pos, loop; } track[FEL_TRACKS];   /* the step playing (0-based), samples into it, passes
                                                               * through its LEN since step 1; as rendered: up to one
                                                               * half (128 frames) ahead of what render returned */
} fel_snapshot_t;

typedef struct {
    char name[16];                      /* as the device shows it ("" for none) */
    int32_t bpm;                        /* the project's own tempo (information: the conductor's is the game's) */
    int32_t engine[FEL_TRACKS];         /* each track's engine (engine_name) */
    int16_t p[FEL_TRACKS][FEL_PARAMS];  /* each track's parameters as loading puts them (inside the engine's ranges) */
    int16_t g[FEL_GLOBALS];             /* the globals, as stored (the core keeps CLK and ROUT its own when it loads) */
} fel_project_info_t;

typedef struct {
    uint32_t abi;                                       /* FEL_CORE_ABI */
    /* life: erased flash, MASTER at its top, the splash run through in device time. Once per copy (the firmware's
     * statics cannot be reset in place); later calls do nothing. */
    void (*boot)(void);
    /* audio: n frames into l, r (either may be 0: rendered and dropped). The host clock moves linearly from b0 to b1
     * beats over them; the sequencer, the arps and the beat follow it (32-sample blocks). b1 == b0: musical time
     * stands still (pause), voices ring on. */
    void (*render)(uint32_t n, double b0, double b1, float *l, float *r);
    /* the beat length the delay, synced LFOs, gates and ratchets use (40..240 BPM, the firmware's range); the step
     * timing comes from the conductor's timeline whatever it is. Shown as BPM (global G_BPM) */
    void (*tempo)(double bpm);
    /* parameters, as the editor protocol writes them (ED_TRACK_PARAM, ED_SET): clamped to the descriptor of the
     * engine asked for, the motion sequencer's base follows. -> the value taken, or FEL_INVALID */
    int32_t (*param_get)(int track, int id);
    int32_t (*param_set)(int track, int id, int32_t v);
    int32_t (*global_get)(int id);
    int32_t (*global_set)(int id, int32_t v);           /* the sound's globals only (swing, tune, delay, reverb,
                                                         * chorus): the clock, tempo, MIDI routing, slots and actions
                                                         * are the core's or the caller's */
    int32_t (*engine_get)(int track);                   /* the engine asked for (it sounds after a short fade) */
    int32_t (*engine_set)(int track, int engine);       /* its preset 0, as the device's ENGINE page does */
    /* projects: the store object of a web app export (core/project_json.h decodes and checks the file). import ->
     * 0 loaded (the transport stopped; global G_BPM is the project's until tempo() is called), else not a project */
    int32_t (*project_import)(const uint8_t *b, uint32_t n);
    uint32_t (*project_export)(uint8_t out[FEL_PROJECT_SIZE]);   /* -> bytes written, 0 on failure */
    /* what a project's bytes hold, as import would read them (older formats converted), without loading it: the copy's
     * instance is left alone, booted or not (an importer may use any copy). Not concurrently with itself on one copy.
     * -> 0, or 1 when the bytes are not a project */
    int32_t (*project_info)(const uint8_t *b, uint32_t n, fel_project_info_t *out);
    const char *(*engine_name)(int engine);             /* "ANALOG" .. ("-": the retired slot), 0 past the last */
    /* transport: step 1 of every track starts in the block in which the conductor's timeline reaches at_beats (now if
     * it has); the steps after it from at_beats exactly (each in the block its time falls in) */
    void (*play)(double at_beats);
    /* join a timeline in progress (DESIGN.md §6.4): from the next block, every track where continuous play since step
     * 1 at origin_beats would have it at the current tempo and patterns (step, position, its passes and conditions,
     * automation, nudges; the metronome, the FX layer, SLICER, a synced LFO 2 in phase), without what it would have
     * played on the way: the step it lands in does not sound (a nudged play of it still to come does). origin ahead of
     * the conductor's timeline: as play(). -> 0, or 1 while a SONG chain plays (not yet) */
    int32_t (*locate)(double origin_beats);
    void (*stop)(void);
    /* a note on a track now (in the next block): vel 0 = off. Played as MIDI IN would (arps, chords, scales) */
    void (*note)(int track, int note, int vel);
    void (*snapshot)(fel_snapshot_t *s);
} fel_core_t;

#endif
