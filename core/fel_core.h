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

#define FEL_CORE_ABI 7                  /* bump on any change to this file (2: locate, the passes in the snapshot;
                                         * 3: project_info, engine_name; 4: project_info's values; 5: cut;
                                         * 6: schedule, cancel, events; 7: a step event's length) */
#define FEL_RATE 44100                  /* the FM-1's rate: the caller resamples */
#define FEL_TRACKS 4
#define FEL_PROJECT_SIZE 3840           /* a project store object (FUN10), as the web app exports it */
#define FEL_INVALID INT32_MIN           /* a parameter call on a track / id that does not exist, or one the core owns */
#define FEL_PARAMS 128                  /* track parameter ids are below this (the formats keep P_COUNT under 128) */
#define FEL_GLOBALS 32                  /* .. global ids */
#define FEL_LANES 8                     /* the DRUM grid's lanes: KICK SNARE CLAP HAT-CL HAT-OP TOM RIM BELL */
#define FEL_SCHEDULES 64                /* actions an instance holds until their time */
#define FEL_EVENTS 256                  /* events it holds until events() takes them */

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

/* when an action applies (schedule) */
enum { FEL_AT_STEP, FEL_AT_BEATS };
enum { FEL_LATE_NEXT, FEL_LATE_DROP, FEL_LATE_NOW };    /* a target found passed: its next occurrence, dropped, now */
typedef struct {
    uint8_t kind;                       /* FEL_AT_STEP: a step of a track; FEL_AT_BEATS: a value of the conductor's
                                         * timeline */
    uint8_t track;                      /* AT_STEP: the track whose steps and passes count */
    uint8_t late;                       /* FEL_LATE_*: AT_STEP's next occurrence is the step in a later pass;
                                         * AT_BEATS' is on by whole `every` (dropped when every is 0) */
    uint16_t step;                      /* AT_STEP: 0-based */
    uint32_t loops;                     /* AT_STEP: 0 the next time the step starts (in the pass playing if it is
                                         * still to come), n in the n-th pass after the one playing (before step 1,
                                         * the first pass is the next) */
    double beats, every;                /* AT_BEATS: the conductor's timeline; the period of its occurrences */
} fel_when_t;

/* what it does */
enum { FEL_DO_MARK, FEL_DO_HIT, FEL_DO_NOTE, FEL_DO_SET, FEL_DO_GLOBAL };
typedef struct {
    uint8_t kind;                       /* MARK: nothing sounds (the event only) */
    uint8_t track;
    int32_t a, b, c;                    /* HIT: lane (0..7), velocity: the lane's GM note on the track (DRUM strikes
                                         * the lane, another engine plays the pitch) for a step; NOTE: note, velocity,
                                         * length in the track's steps (1..64), played as MIDI IN would (arps, chords,
                                         * scales); SET: param_set(track, a, b) from that step on; GLOBAL:
                                         * global_set(a, b) */
} fel_action_t;

/* what happened (events) */
enum { FEL_EV_ACTION, FEL_EV_DROPPED, FEL_EV_STEP };
typedef struct {
    uint8_t kind;                       /* ACTION: an action applied; DROPPED: one found passed (FEL_LATE_DROP), or
                                         * whose step is past its track's LEN; STEP: a track entered a step */
    uint8_t track;
    uint16_t step;                      /* 0-based: the step entered (AT_BEATS: the step playing) */
    uint32_t loop;                      /* the track's pass (as the snapshot counts them) */
    uint32_t id;                        /* ACTION, DROPPED: schedule()'s */
    uint64_t frame;                     /* the first frame of the block it sounds in (render()'s count, as the snapshot's
                                         * frames) */
    double beats;                       /* the conductor's timeline there */
    double length;                      /* STEP: the step's length in beats (its DIV's, swung), as it starts */
} fel_event_t;

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
    /* projects: the store object of a web app export (the extension decodes and checks the file: FeluccaProject). import ->
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
    /* back to silence at once, for the copy's next source (DESIGN.md §7, §13.9): the transport stopped, every note
     * forgotten and every voice stopped without its release, every line that rings zeroed (the delay, the chorus,
     * the reverb's three models, each part's DIST, INSERT and SLICER, the FX layer's flanger), the master's filters
     * and limiter at rest, and the frames already rendered ahead zeroed: render() gives exact zeros until a note or a
     * play() sounds. What is not heard stays
     * (noise seeds, phases, the engines' own memory, which a fresh note starts from rest): a cut copy is silent, not
     * fresh. The parameters and the patterns stay too (an import replaces them) */
    void (*cut)(void);
    /* a note on a track now (in the next block): vel 0 = off. Played as MIDI IN would (arps, chords, scales) */
    void (*note)(int track, int note, int vel);
    void (*snapshot)(fel_snapshot_t *s);
    /* hold an action until its target (DESIGN.md §5.1): it applies in the block in which its step starts (or the
     * conductor's timeline reaches its beats), before the step's own notes, as a programmed step's would. A step
     * target is resolved in the first block rendered while the transport plays (play() and locate() have taken
     * effect there). -> 0, 1 not valid (an id 0, a track, step, lane, note, velocity or parameter that is not one),
     * 2 the table is full (FEL_SCHEDULES). stop() and cut() forget them all */
    int32_t (*schedule)(const fel_when_t *w, const fel_action_t *a, uint32_t id);
    void (*cancel)(uint32_t id);                        /* the pending actions with that id (0: every one) */
    /* what happened since the last call, in the order it was rendered (up to a half ahead of what render() has
     * returned) -> events written. The ring holds FEL_EVENTS: past that the newest are lost. cut() empties it */
    uint32_t (*events)(fel_event_t *out, uint32_t max);
} fel_core_t;

#endif
