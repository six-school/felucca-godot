/* A sketch of the audio-thread side of a Godot extension: Felucca's render loop owned by the glue, a scheduler of
 * actions keyed to musical positions (track, loop, step), and an event stream with exact sample times. No firmware
 * file is modified: web/emu/felucca_web.c's web_ms() (one device millisecond, the audio ISR when a half is due) is
 * driven from here, and the sequencer's own state (seq_idx, seq_pos, step_samples) is read between halves.
 *
 * Timing: the sequencer moves in CTL (32-sample) blocks inside each 128-frame half. An action due at a step is
 * applied before the half in which that step starts, so it is in place when the firmware reaches the step (a step
 * overlay plays exactly as if it had been programmed). A step event's time is exact: the samples rendered minus the
 * step's seq_pos after the half. */
#include "felucca_api.c"
#include <stdio.h>
#include <math.h>

/* ---------------------------------------------------------------- events (audio thread -> game thread) --- */
enum { EV_STEP = 1, EV_LOOP, EV_BEAT, EV_ACTION };
typedef struct { uint8_t kind, track; uint16_t step; uint32_t loop, tag; uint64_t sample; } fel_event_t;
#define EVN 1024u
static fel_event_t evq[EVN];
static uint32_t ev_w, ev_r;                        /* SPSC ring: the audio thread writes, the game thread drains */
static void ev_push(uint8_t kind, uint8_t track, uint16_t step, uint32_t loop, uint32_t tag, uint64_t sample)
{
    if (ev_w - ev_r < EVN) evq[ev_w++ % EVN] = (fel_event_t){kind, track, step, loop, tag, sample};
}

/* ---------------------------------------------------------------- the transport, as the glue counts it --- */
static uint64_t rendered;                          /* device samples since boot (the instance's clock) */
static uint32_t loops[NTRK];                       /* each track's completed loops since PLAY */
static uint16_t last_idx[NTRK];
static uint32_t last_beat_pos, beats;
static uint8_t was_playing;

/* ---------------------------------------------------------------- the scheduler (game thread -> audio thread) --- */
enum { A_HIT_ON = 1, A_HIT_OFF, A_PARAM, A_MARK };
typedef struct { uint8_t used, kind, track; uint16_t step; uint32_t loop, tag; int32_t a, b, c; } fel_action_t;
#define AN 64u
static fel_action_t act[AN];
static int fel_at(uint32_t track, uint32_t loop, uint32_t step, uint8_t kind, int32_t a, int32_t b, int32_t c, uint32_t tag)
{
    uint32_t i;
    for (i = 0; i < AN; i++)
        if (!act[i].used) { act[i] = (fel_action_t){1, kind, (uint8_t)track, (uint16_t)step, loop, tag, a, b, c}; return (int)i; }
    return -1;
}
static void apply(const fel_action_t *x)
{
    uint32_t target = x->kind == A_HIT_OFF ? (uint32_t)x->b : x->step;   /* OFF: due at one step, undoes another */
    step_t *s = (step_t *)&seq_steps(&trk[x->track])[target];    /* the steps that play (a SONG lane: its copy) */
    if (x->kind == A_HIT_ON) s->hit |= (uint8_t)(1u << x->a);
    else if (x->kind == A_HIT_OFF) s->hit &= (uint8_t)~(1u << x->a);
    else if (x->kind == A_PARAM) fel_param_set(x->a, x->b, x->c);
}
/* before a half: every action whose step starts inside the next HALF_FRAMES samples */
static void sched_before_half(void)
{
    uint32_t t, i;
    if (!song.playing) return;
    for (t = 0; t < NTRK; t++) {
        track_t *tr = &trk[t];
        uint32_t len = tr->p[P_SLEN] ? (uint32_t)tr->p[P_SLEN] : 1u, period = div_samples((uint32_t)tr->p[P_SDIV]);
        uint32_t idx, loop, left;
        if (tr->seq_pos >= 0x7FFFFFFFu) { idx = 0; loop = 0; left = 0; }       /* PLAY: step 1 is next */
        else {
            uint32_t cur = step_samples(tr, period, tr->seq_idx);
            left = cur > tr->seq_pos ? cur - tr->seq_pos : 0;
            if (left > HALF_FRAMES) continue;                                    /* no step starts in this half */
            idx = (tr->seq_idx + 1u) % len;
            loop = loops[t] + (idx == 0);
        }
        for (i = 0; i < AN; i++)
            if (act[i].used && act[i].track == t && act[i].loop == loop && act[i].step == idx) {
                apply(&act[i]);
                if (act[i].tag) ev_push(EV_ACTION, (uint8_t)t, (uint16_t)idx, loop, act[i].tag, rendered + left);
                act[i].used = 0;
            }
    }
}
/* after a half: what the sequencer did in it, with the sample each thing started on */
static void events_after_half(void)
{
    uint32_t t;
    if (song.playing && !was_playing) {
        memset(loops, 0, sizeof loops);
        for (t = 0; t < NTRK; t++) last_idx[t] = 0xFFFF;
        beats = 0; last_beat_pos = 0;
    }
    was_playing = song.playing;
    if (!song.playing) return;
    for (t = 0; t < NTRK; t++) {
        track_t *tr = &trk[t];
        if (tr->seq_pos >= 0x7FFFFFFFu || tr->seq_idx == last_idx[t]) continue;
        if (tr->seq_idx == 0 && last_idx[t] != 0xFFFF) { loops[t]++; ev_push(EV_LOOP, (uint8_t)t, 0, loops[t], 0, rendered - tr->seq_pos); }
        last_idx[t] = tr->seq_idx;
        ev_push(EV_STEP, (uint8_t)t, tr->seq_idx, loops[t], 0, rendered - tr->seq_pos);
    }
    if (beat_pos < last_beat_pos) ev_push(EV_BEAT, 0, 0, ++beats, 0, rendered - beat_pos);   /* seq.c's beat (the ARP LED) */
    last_beat_pos = beat_pos;
}

/* ---------------------------------------------------------------- the render loop the glue owns --- */
static void fel_render(uint32_t n, float *l, float *r)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        while (ring_w == ring_r) {
            int isr = web_samples_due + FS >= 1000u * HALF_FRAMES;          /* this millisecond runs the audio ISR */
            if (isr) sched_before_half();
            web_ms();
            if (isr) { rendered += HALF_FRAMES; events_after_half(); }
        }
        { uint32_t i = (ring_r++ % RING) * 2u; if (l) l[k] = ring[i]; if (r) r[k] = ring[i + 1]; }
    }
}

/* ================================================================ the knock, then the door ================ */
static float audio[44100 * 12];
static uint64_t audio0;                             /* rendered when audio[0] was rendered */
static void run(double sec, float *dst) { uint32_t n = (uint32_t)(sec * 44100), k; for (k = 0; k < n; k += 128) fel_render(128, dst ? dst + k : 0, 0); }

int main(void)
{
    static const uint8_t HAT[1] = {42};
    uint32_t i, k, nk = 0, t3loop;
    uint64_t knock_at[4];
    web_nor_erase(); web_master(700); web_boot();
    run(1.5, 0);
    for (i = 0; i < 3; i++) { memset(trk[i].step, 0, sizeof trk[i].step); trk[i].p[P_SLEN] = 16; }
    host_drums(&trk[3]);                                    /* track 4: DRUM, closed hats on the off-beats */
    memset(trk[3].step, 0, sizeof trk[3].step);
    for (i = 0; i < 16; i++) if (i % 2) put_step(&trk[3], i, 1, HAT, ST_NOTE, 0);
    trk[3].p[P_SLEN] = 16; trk[3].p[P_SDIV] = 2;            /* 16 steps of 1/16: a 4/4 bar */
    fel_engine_set(0, fel_engine_find("ANALOG"));
    run(0.2, 0);
    printf("tempo %d BPM, 1/16 = %u samples (exact: %.1f), a beat %u\n", song.g[G_BPM], div_samples(2),
           44100.0 * 60 / song.g[G_BPM] / 4, beat_samples());
    fel_play(1);
    run(0.3, 0);                                            /* the player presses the key somewhere in bar 1 */

    /* "knock four times on the next loop, at steps 1, 5, 9, 13; one loop later the door opens" */
    t3loop = loops[3];
    for (i = 0; i < 4; i++) {
        fel_at(3, t3loop + 1, i * 4, A_HIT_ON, 0, 0, 0, 100 + i);      /* KICK lane (0) on that step... */
        fel_at(3, t3loop + 1, i * 4 + 1, A_HIT_OFF, 0, (int32_t)(i * 4), 0, 0);   /* ...and off again right after it */
    }
    fel_at(3, t3loop + 2, 0, A_PARAM, 0, fel_param_find(0, "CUT"), 127, 200);   /* the door: CUT opens */
    printf("scheduled at sample %llu (track 4 in loop %u, step %u)\n", (unsigned long long)rendered, t3loop, trk[3].seq_idx);

    audio0 = rendered;
    run(4.5, audio);

    /* the game thread: drain the events (here: print the musical ones and the actions) */
    while (ev_r != ev_w) {
        fel_event_t e = evq[ev_r++ % EVN];
        if (e.kind == EV_ACTION) {
            printf("  ACTION %s  track %u loop %u step %2u  at sample %llu\n", e.tag == 200 ? "DOOR " : "KNOCK",
                   e.track + 1, e.loop, e.step + 1, (unsigned long long)e.sample);
            if (e.tag < 200 && nk < 4) knock_at[nk++] = e.sample;
        } else if (e.kind == EV_LOOP && e.track == 3)
            printf("  loop   track 4 -> loop %u                at sample %llu\n", e.loop, (unsigned long long)e.sample);
    }
    printf("CUT on track 1 now %d\n", trk[0].p[fel_param_find(0, "CUT")]);

    /* the audio: the kicks (low band) start where the KNOCK events say, and nowhere else */
    {
        static float lp[44100 * 12];
        double y = 0, a = 1 - exp(-2 * M_PI * 120 / 44100.0), pk = 0;
        uint32_t n = (uint32_t)(4.5 * 44100);
        for (k = 0; k < n; k++) { y += a * (audio[k] - y); lp[k] = (float)fabs(y); if (lp[k] > pk) pk = lp[k]; }
        printf("low-band onsets (above 30%% of the peak, 150 ms apart), relative to the KNOCK times:\n");
        for (k = 1, i = 0; k < n; k++)
            if (lp[k] > pk * 0.3 && lp[k - 1] <= pk * 0.3) {
                uint64_t at = audio0 + k; int64_t best = 1 << 30; uint32_t j;
                for (j = 0; j < nk; j++) if (llabs((int64_t)(at - knock_at[j])) < llabs(best)) best = (int64_t)(at - knock_at[j]);
                printf("  onset at sample %llu: %+lld samples from the nearest KNOCK\n", (unsigned long long)at, (long long)best);
                k += 6615;
            }
    }
    return 0;
}
