# Felucca in Godot: design

Working notes for implementing (and possibly re-implementing differently) a GDExtension that runs Felucca headless in
a Godot game. Written from the research conversation of 2026-10-24. Claims marked *measured* have a test in
`prototypes/` (C, against the firmware: `tools/setup_felucca.sh && prototypes/run_all.sh`) or `spikes/` (Godot).

How to use this doc: §3 is what was decided and why; §12 lists the alternatives and which layer each one replaces, so
switching approach means swapping one layer behind an interface that stays put (§4.1, §5.1). §13 is what is still
open; §14 the plan.

---

## 1. Requirements (from the user)

| | Requirement |
|---|---|
| R1 | Synthesize sound in a Godot game with Felucca, headless. An optional debug view of the device screen only if cheap. |
| R2 | Sounds and sequences are designed on the user's FM-1 (Felucca **1.6.1**) and exported with the Felucca web app (project `.json`); the game loads them, each sound source with its project and per-source overrides (e.g. a part muted initially). |
| R3 | Every parameter controllable from code **directly** (e.g. ARP RATE), never by driving the emulated UI. |
| R4 | Several instances running concurrently, managed as a **pool** smaller than the number of in-world sources (§7). |
| R5 | Type safety against typos in scripts: no free-string parameter names. |
| R6 | All instances in sync, driven from the Godot side, for minutes at a time, with no drift. |
| R7 | Tempo tweened from Godot like any property (int tweens; float too, e.g. from the player's distance), at any tempo, without song mode. |
| R8 | Scheduling in musical time: "switch track 1 to another pattern on the next loop", "when the sequencer reaches step 14". |
| R9 | Scripts react to scheduled events and to sequencer steps/beats, in time with what is heard (particles on each knock). |
| R10 | GDExtension via godot-cpp (or godot-rust). |
| R11 | **Godot 4.7.2** (latest stable); moving to a later stable minor must be easy when GDExtension and audio have not broken. |
| R12 | **Pause stops all sound and all musical time**, so audio-synced events stay in sync; respect `stream_paused` of the players unless there is a good reason not to. |
| R13 | Nothing persists between sessions: instance memory (flash image) is thrown away. |
| R14 | Licence: the game is released as **GPL-3.0** (decided by the user). |
| R15 | Web export if possible; otherwise desktop only. |
| R16 | Development continues in Claude Code CLI on the user's machine (this folder, a local git repo). |

Motivating scenario (acceptance test, M6): the player triggers a "knock": four kicks on steps 1, 5, 9, 13 of the drum
track's **next** loop, each emitting particles on the door in time with the sound; one loop later the door opens and
a cutoff sweep starts. Pool scenario (M5): cellar (5 sources), hallway (1 + 2 cellar sources still audible), bedroom
(hallway + 4), with a pool of 8.

## 2. Facts about Felucca the design depends on

Felucca main `63f88ec` (5 commits after 1.6.1: README/licensing, #282 a BPM-0 guard in `fx.c beat_samples()`); the
user's device runs 1.6.1, whose project format is the same (FUN10). The maintainer has stepped back.

- **One translation unit.** `firmware/src/felucca.c` `#include`s every source. `web/emu/felucca_web.c` (the browser
  emulator's *board file*) builds the same firmware against a simulated FM-1: it includes `tests/hostsim.c` (libc,
  engines, voices, FX, sequencer), then audio, UI, storage, projects. Nearly all state is `static`; the only global data
  names are `panel settings proj_slot bootguard felucca_dbg` plus linker markers (`_bss_start`…) and the functions
  (`web_*`, `felucca_mem*`, `isr_*`, `fm1_*_irq`, `hostsim_main`). *Measured* with `nm`.
- **Board-file ABI** (`web_*`): `web_boot`, `web_render(n)` → `web_out_l/r`, `web_midi(status,d1,d2)` (channel messages
  only), `web_keys/buttons/enc/master`, `web_screen()` (240×240 RGB565, high byte first), LEDs, `web_nor()` (1 MiB
  flash image). Fixed **44.1 kHz**. Built `-sSTANDALONE_WASM`, the wasm has no imports.
- **Time.** `web_ms()` = one device millisecond: the audio ISR for every 128-frame half due, the main loop (`web_frame`:
  UI, settings/autosave polls, LEDs, drawing) every 15 ms. The ISR renders a half as four `CTL` = 32-sample blocks;
  `events_block(n)` (seq.c) runs once per block: MIDI in, `seq_tick`, arps, metronome, beat counter. **Step timing
  resolution is one 32-sample block (0.73 ms)**, on the hardware too.
- **Parameters.** `trk[4].p[P_COUNT]` (per part), `song.g[G_COUNT]` (global), `int16_t`; the audio ISR reads them as they
  are, so a direct write takes effect live (*measured*). Reference write path = the editor protocol's ED_SET /
  ED_TRACK_PARAM: clamp to the descriptor, `enum_orig()`, `motion_capture()`, `ui.force = 1`. Engines: `set_engine_of()`.
- **Descriptors.** `params.c`: `TP[]`, `GP[]` (label, fmt, min, max, def, enum names); engine parameters from
  `ENGINES[e]->edit[8]` via `track_desc()`; pages in `PAGES[]` (title, scope, 4 ids). Dump: `prototypes/params.json`.
- **Projects.** The web app's "Felucca-project-*.json" = `{format:"felucca-project", version:1, firmware, slot,
  object:{size, crc, data}}`: `crc` is CRC-32 (zlib) of the decoded bytes, `data` base64 of the 3840-byte FUN10 store
  object (`proj_pack()`; editor_backup.c packs exports the same way; accepted sizes 3840 3648 3584 3388 for older
  formats). Load: `proj_import(&q, bytes, n)` (migrates) + `project_restore_runtime(&q)`. A project holds the four parts'
  sounds, 64-step patterns, LEN/DIV/SWING/GATE, the song chain, automation/locks, FM6 patches, its name and BPM. **Not**
  user sample audio, the user-preset library or settings. *Measured with the user's own export* (§10 T11).
- **Patterns** are per track: `trk[t].step[64]` + P_SLEN/SDIV/SSWING/SGATE (+ P_SQNT) + that track's records in the shared
  `motion` store. Song mode (`song_chain.c`) plays copies of up to four saved projects' patterns per section;
  `seq_steps(t)` returns the steps that play.
- **Transport/clock.** `seq_start()` / `seq_stop()`; no Song Position Pointer and no "locate" (§6.4 adds one).
  Internal clock: integer `G_BPM` 40..240, quarter = `floor(44100·60/BPM)`, step = `floor(quarter / divisor)`; a tempo
  change does not rescale the step in progress. External clock: 24-ppqn pulses timestamped in whole device ms,
  tempo re-measured every 6 pulses as whole ms.
- **beat_samples()** feeds the delay time (read point jumps on change), synced LFOs, ENV SYNC, gates, ratchets.
- **Not in the board file:** USB (audio, MIDI out, editor SysEx), TRS MIDI, user sample slots, battery, crash/UBOOT.
- **Host-only imports** from `tests/hostsim.c` (`fork pipe waitpid fopen fwrite …`) are never called but must link:
  stub them on Windows/web (felucca-vst has Windows stubs in `core/win/`).

## 3. Decisions

| # | Decision | Why / evidence | Alternatives (→ §12) |
|---|---|---|---|
| D1 | Run Felucca's own C via a board file, not a chip emulation. | Direct parameter access needs the firmware's structures (R3). 1.2 % of a core for the heavy test song (*measured*). | A5 |
| D2 | Own the **board file**: include `felucca_web.c` (fork it only if needed) and own the render loop around `web_ms()`. | Host clock, scheduler, events and locate hook between halves/blocks. | — |
| D3 | Instances = a **pool of N copies of the core compiled into one binary**, each exposing one entry point `fel_core_<n>()` (a function table). Copies are separated with **generated preprocessor renames** (`-Dpanel=fel3_panel …`, list from `nm` of a reference build, checked by a 2-copy link test); `objcopy` localization only where it is the simpler option. | Globals ⇒ a copy per instance. Renames work with every toolchain incl. Emscripten (llvm-objcopy's wasm support doesn't localize). Two copies isolated, bit-identical over 10 min (*measured*, ELF/objcopy). Cost ≈ 0.95 MB code+data per copy (shared read-only tables could fold with linker ICF), ≈ 1.6 MB RAM per instance. | A1, A2 |
| D4 | **godot-cpp 10.0.0-stable**, `api_version=4.7`, `.gdextension` `compatibility_minimum = "4.7"`, SCons. | R11: godot-cpp is versioned independently since 10.x and ships API files per Godot minor (10.0.0: 4.3–4.7; master adds 4.8); extensions are forward-compatible, so the 4.7 build loads in later minors unchanged. Moving on = bump the pin and `api_version` only to use newer API. One variable holds the version. | A3 |
| D5 | Audio out: `FeluccaStream : AudioStream`, playback `: AudioStreamPlaybackResampled` (`_get_stream_sampling_rate` = 44100, `_mix_resampled`). | Godot resamples to its mix rate (44100 on desktop by default; the browser's rate on the web). | — |
| D6 | **One musical timeline** `B` (beats) owned by the conductor; every instance's host clock is fed so its musical position follows `B` (§6). `B` advances once per **mix step** in a clock `AudioEffect` on a muted bus (`_process_silence() → true`), by `buffer frames · tempo / (60 · mix rate)`, unless paused. Commands (tempo, transport, pause) are published to the audio thread under `AudioServer.lock()` and take effect on a step boundary. | Instances stay in sync even when Godot mixes them unevenly. *Measured* in Godot 4.7.2: the effect runs on every mix step on an empty muted bus, paused too (T12). | per-player rendering (only sync'd starts) |
| D7 | **Pause** (R12): the conductor autoload (first child of the root, hears `NOTIFICATION_PAUSED` first) stops `B` and sets `stream_paused` on every bound source's player **in one `AudioServer.lock()`**; Godot's own per-node calls that follow are no-ops. Unpause likewise, except sources the user had paused themselves. A user's `stream_paused` on one source pauses just that source; on resume it relocates to `B` (`follow`, default) or carries on where it stopped (`hold`). | *Measured* (T13): with Godot's per-node pause, 8 players drifted 21 504 frames (0.49 s) apart over 300 pause/unpause cycles; atomic under the lock: 0. | — |
| D8 | Clock: **host clock** = PR 269's grid fed per 32-sample block by the board file from `B` (patch 0002), `midi_clock_retempo()` on tempo changes. Tempo is a float; `G_BPM` only shows it. The project's own BPM is information (the game may copy it to the conductor). | R6/R7. 5 min of tempo tweens: step error ≤ 0.85 ms (block size), no growth; beat length steady at constant tempo (T5). In the core (M2): 0 to 0.725 ms early (one block), never late, float tempos and pauses too. | A4, A6 |
| D9 | **Delay glide** (patch 0003). | Every tempo change otherwise clicks in the echoes (T6). | crossfade |
| D10 | Scripting API: **generated typed classes** from the firmware's descriptor tables (§8). | R5. | A7 |
| D11 | Scheduling on the audio thread in the board file, keyed to (track, loop, step) or timeline positions, applied before the half in which the step starts; relative targets resolved on the audio thread. | Knock prototype exact (T9). | — |
| D12 | Events: audio thread → lock-free queue (sample + `B` stamps) → signals **when heard**; plus a polled, heard-time snapshot for continuous visuals. | Polling alone misses steps, can't tag, reports rendered time. | A8 |
| D13 | Projects: import plugin turns the web app's `.json` into a `FeluccaProject` resource (validated: format, version, size, CRC-32); loaded with `proj_import` + `project_restore_runtime`. | R2; *measured* with the user's export (T11). | backup → flash image (user samples, later) |
| D14 | **Sources and pool** (R4): `FeluccaSource`, `FeluccaSource2D`, `FeluccaSource3D` (extend the matching `AudioStreamPlayer*`) hold project, overrides, script-set state and schedules; the conductor binds the N most audible to instances, joining them at the timeline position (`locate`, §6.4). | §7. | restart-on-bar join |
| D15 | Persistence: none (R13). Flash writes stay in the instance's RAM image and vanish with it; autosave may be switched off to save CPU. | — | — |
| D16 | **Web: design for it, ship desktop first.** No hard dependency on threads, portable renames (D3), lock-based publication works there too. A web milestone (M8) with an early spike; threaded web builds are the target (audio on its own thread). | §9. | desktop only |
| D17 | Felucca base: main `63f88ec` + PR 269 (firmware/tests) + patches 0002–0004 (`patches/`). 0002 asks the board file's `host_clock_units()` on **every** block, playing or not (M1): the board file starts the transport inside the block where `B` reaches `play()`'s beat, and stopped blocks move the arps and the beat with `B`, as the internal clock does. | Series verified with `git am` on a clean checkout; all prototypes reproduce on it; Felucca's host suite passes on it (2026-10-24: every host test; the device-image tests need the AC79 toolchain). | rebase if PR 269 changes |

## 4. Architecture

```
GDScript ────────────────────────────────────────────────────────────────────────────── main thread
  FeluccaSource / 2D / 3D (extend AudioStreamPlayer*)   FeluccaPart ×4   FeluccaAnalog… (engine views)
  FeluccaConductor (autoload: timeline, tempo, pause, pool)   FeluccaProject (+ importer)   FeluccaOverrides
  FeluccaWhen / FeluccaAction   signals
        │ property sets, schedule(), play() ── published under AudioServer.lock() ──┐
        ▼                                                                          │      ▲ signals when heard
L2  C++ engine: timeline B, pool + binding, per-instance SPSC command/event queues, │      │ snapshots
    shadows, heard-time map, worker for boot/import/locate                         │
        │ ClockEffect::_process (once per mix step) ◄──────────────────────────────┘      audio thread
        │ FeluccaPlayback::_mix_resampled (per bound source) → fel_core_t.render
        ▼
L1  C core, one compiled copy per pool slot: board file (render loop, host clock from B, locate, scheduler,
    events; API: params/engines/projects/transport/notes/screen) ── ABI: fel_core_t (§5.1)
        │ #include
        ▼
L0  Felucca firmware (63f88ec + patches), one translation unit
```

### 4.1 Seams (what stays fixed when an approach is swapped)

- **Core seam: `fel_core_t`.** L2 never sees a firmware symbol. Renamed copies (D3), dlopen copies (A1) and wasm
  instances (A2) are providers of the same table.
- **Clock seam:** the core's clock mode (host / pulses / internal) is the core's business; L2 always speaks timeline
  (`B` at block boundaries, tempo, play, stop, locate).
- **Binding seam:** the generator's registry (`params.json` → codegen) feeds the typed API (D10) or any other front end.
- **Version seam:** the Godot API version is one build variable (D4).

### 4.2 Threads

- **Audio thread** (Godot's mix): `ClockEffect::_process` advances `B` and applies published commands at each step's
  end; `FeluccaPlayback::_mix_resampled` renders its instance (host clock fed from `B` for that step), runs the
  scheduler, pushes events. All `fel_core_t` calls for a bound instance happen here.
- **Worker thread** (desktop; synchronous fallback on the web without threads): boot, project import, overrides and
  `locate` for an instance about to be bound; then hand-off to the audio thread under the lock.
- **Main thread:** property sets push commands; getters return a **shadow** (last value set, refreshed from snapshots
  each frame; values moved by recorded automation show up a frame late). Publication of anything that must take
  effect for several instances on the same step (tempo, transport, pause, binding) happens inside
  `AudioServer.lock()`/`unlock()` (bound to scripts and extensions; the driver holds it for each mix).
- Nothing in the audio path allocates or locks (firmware is static-memory code; queues are SPSC rings).

## 5. Interfaces (drafts)

### 5.1 `fel_core_t` (L1 → L2), plain C, one table per pool slot

```c
typedef struct {
    uint32_t abi;                                       /* bump on any change */
    /* life */
    void (*boot)(void);                                 /* erased flash; splash run through (device time) */
    /* audio: n frames; the host clock advances from b0 to b1 beats over them (linear); scheduler + events inside */
    void (*render)(uint32_t n, double b0, double b1, float *l, float *r);
    void (*tempo)(double bpm);                          /* beat length for delay / LFO / gates (midi_clock_retempo) */
    /* parameters (descriptor-checked writes, the ED_TRACK_PARAM path) */
    int  (*param_get)(int track, int id);   int (*param_set)(int track, int id, int v);
    int  (*global_get)(int id);             int (*global_set)(int id, int v);
    void (*engine_set)(int track, int engine);
    /* projects */
    int  (*project_import)(const uint8_t *b, uint32_t n);   int (*project_export)(uint8_t out[3840]);
    /* transport */
    void (*play)(double at_beats); void (*stop)(void);
    int  (*locate)(double beats_since_start);           /* §6.4: jump without playing what lies between */
    /* notes (live, now) */
    void (*note)(int track, int note, int vel);         /* vel 0 = off */
    /* scheduling (relative targets resolved here) */
    int  (*schedule)(const fel_when_t *w, const fel_action_t *a, uint32_t tag);
    void (*cancel)(uint32_t tag);
    /* events out */
    uint32_t (*events)(fel_event_t *out, uint32_t max);
    void (*snapshot)(fel_snapshot_t *s);                /* positions, loops, beat phase, playing */
    /* debug view (optional) */
    const uint16_t *(*screen)(uint32_t *draws);  void (*input)(uint32_t buttons, uint32_t keys, int enc_role, int steps);
} fel_core_t;
```

Prototype equivalents: `felucca_api.c` (params, engines, projects, transport), `fel_core.h`/`fel_copy.c` (table + one
symbol), `sched.c` (render loop, scheduler, events), `tempo.c` MODE 3 (host clock), `project_load.c` (JSON → core).

**As built (M1, `core/fel_core.h`, ABI 1):** the draft above without `locate` (M2), `schedule`/`cancel`/`events` (M6)
and `screen`/`input` (M7); those come with their milestones and an ABI bump. Differences from the draft: parameter
calls return `int32_t` with `FEL_INVALID` for a track/id that does not exist; `engine_get` added, `engine_set` returns
the engine taken; `global_set` takes only the sound's globals (swing, tune, delay, reverb, chorus): BPM is `tempo()`'s,
CLK and ROUT are the core's (forced to USB clock and CH1-4 after boot and after every import), slots and actions are
the UI's; `tempo()` clamps to 40..240 (the beat length only: steps follow `B` at any tempo); `boot()` is once per copy;
`render()` takes null buffers (rendered, dropped); `snapshot` has playing, beats since `play()`, frames, each track's
step and position (as rendered: up to one half ahead). MASTER is at its top (unity); output is the device's
24-bit scale (−6 dBFS ceiling). The project JSON check is `core/project_json.c`, firmware-independent (one copy for the
importer and the tests). The pool: `core/build.sh` → `build/core/libfelucca_core.a`, `fel_pool_core(n)`.

**M2 (ABI 2):** `locate(origin_beats)` — by the origin on `B` (where step 1 was or will be), not "beats since start":
an instance renders up to a half ahead of what it has returned, so a position "now" is ambiguous, while an origin is
exact at whichever block it lands in (§6.4); 1 while a SONG chain plays. The snapshot's tracks add `loop` (passes
through LEN since step 1). Delay glide (patch 0003) on.

### 5.2 GDScript surface (sketch)

```gdscript
# scene: one FeluccaSource3D per in-world source (more sources than pool instances is fine)
#   inspector: project = res://music/cellar_drone.felucca, overrides = <FeluccaOverrides>, priority, join = FOLLOW
FeluccaConductor.tempo = preload("res://music/cellar_drone.felucca").bpm
FeluccaConductor.play()
create_tween().tween_property(FeluccaConductor, "tempo", 140.0, 2.0)

@onready var cellar: FeluccaSource3D = $Cellar/Drone
var bass: FeluccaPart = cellar.part_1                    # part_1..part_4, also parts: Array[FeluccaPart]
bass.arp_rate = FeluccaPart.ArpRate.R_1_16               # stored on the source; applied now if bound, on binding if not
var synth := bass.analog                                 # FeluccaAnalog, null unless the engine is ANALOG
if synth: create_tween().tween_method(func(v: int) -> void: synth.cutoff = v, 30, 127, 0.8)

var drums := cellar.part_4
for step in [1, 5, 9, 13]:
    cellar.schedule(FeluccaWhen.next_loop(drums, step), FeluccaAction.hit(drums, FeluccaPart.Lane.KICK), &"knock")
cellar.schedule(FeluccaWhen.loops_ahead(drums, 2, 1), FeluccaAction.mark(), &"door")   # pins the source until done
cellar.scheduled.connect(func(tag: StringName, e: FeluccaEvent) -> void: …)          # emitted when heard
cellar.bound.connect(…); cellar.unbound.connect(…)
```

Conventions: steps **1-based** in the API (as on the device), 0-based inside; raw firmware integers with range hints;
optional normalized floats. Actions: `hit`, `note`, `set(part, Param, value)`, `pattern(part, FeluccaPattern)`, `mark`.
Targets: `next_loop(part, step)`, `loops_ahead(part, n, step)`, `next_step(part, step)`, `bar_beat(bar, beat)`. Late
targets: policy flag (next occurrence / now). `FeluccaOverrides`: a resource of typed `(part, Param, value)` entries
(inspector dropdowns from the generated enums) plus shortcuts such as `mute`.

## 6. Timing

### 6.1 Timeline

`B` (double, beats) is advanced by `ClockEffect` at the end of each mix step: `B += frames · tempo / (60 · mix_rate)`
unless paused; tempo/transport/pause published under the lock apply from the next step. During step k+1 every bound
instance renders its frames with its host clock moving linearly from `B_k` to `B_k + Δ` (the plan made at the end of
step k), mapped onto its own 32-sample blocks (`host_clock_units`). An instance mixed late or early by a step (a race
outside our control) snaps back on its next render: musical position always derives from `B`.

As built (M2): a step is entered in the 32-sample block its time falls in, so it sounds from that block's start: 0 to
0.73 ms early, never late, with no drift (T5 on the core). `play(at)` starts step 1 in the block containing `at` and
gives that block no time: the firmware drops a start block's own (its PLAY entry), so the next block's units, counted
from `at`, put the sequencer's zero exactly there. Frames rendered ahead (at most 127, the rest of a half) were clocked
with the previous call's `B` extrapolated; the next call's `B` takes over from its first new block. At a pause those
frames play on: a step due in them sounds up to 2.9 ms into the pause (Godot fades the playback then anyway).

### 6.2 Pause

As D7. Details: Godot fades a pausing playback over one more mix step (state FADE_OUT_TO_PAUSE, then PAUSED: the
playback's `mix` is no longer called) — `B` is already stopped, so the sequencer is frozen while voices fade; on
resume our playback fades in over ~5 ms. Players must not get desynchronised by Godot's per-node notification order:
the conductor acts before them (autoload order) inside the lock.

### 6.3 Events and heard time

Events carry the instance sample and `B`. The main thread keeps (step index, `B`) pairs and maps them to wall time with
`AudioServer.get_time_since_last_mix()` and `get_output_latency()`; events are held until heard, then emitted with
`late_by`. Pause holds them.

### 6.4 Locate (new, board file)

`locate(beats)` puts every track where continuous play from `play()` would have it after `beats`: `seq_idx`, `seq_pos`
(scaled to the current beat length), loop counters, arp position/state reset, ratchet/nudge cleared, held notes
released, motion back to base values, metronome/beat phase; song mode: section and repeat. No missed step sounds.
Used for: binding a source mid-timeline (D14), resuming a `follow` source after its own pause (D7). Verify by
comparing step events of a located instance with one that played continuously (M2). First version may support pattern
projects only (no song chain), with chain projects joining at their next section start.

As built (M2, `core/board.c` `fel_locate`): `locate(origin)` takes effect at the start of the next block, from `B`
there: (if playing, stop) PLAY's start state, then for each track the samples since `origin` as the clock's grid would
have summed them (with its carried remainder), walked through the swung step lengths of its LEN: step, position, passes
(`cond_loop` for the conditions), the nudge latches (a late play of the landed step still to come is latched again; a
next step's early play is latched or marked played), the automation of the pass up to that step (not the locks: the
landed step does not play). The metronome and the beat counter from the same grid; the FX layer's 1/16 grid, SLICER
and a FREE synced LFO 2 from the elapsed time at the current tempo (they run on rendered samples, §13.12). A position
on a step's end belongs to that step, so a step starting exactly there plays. The arps' held notes stay. An origin
ahead of `B` is `play(origin)`. **Verified** (`core/tests/test_locate.c`, a pattern with odd LENs, swing, 1/16 1/8T
1/4, nudges late and early, conditions 2:2 and 1:3, PAN automation): at constant tempo, instances located at 3.10 s and
6.77 s, one relocated while playing from another origin, and one located to an origin ahead, equal the continuous ones
in every 32-frame chunk (step, position to the sample, passes, conditions' loop, latches, automation); after a tempo
tween within 2 samples (continuous play's phases were rescaled at each change). The joining instance is silent until
its next step (or nudged play), then plays.

## 7. Sources and the pool (D14)

- **Sources** are scene nodes; **instances** (pool slots, N fixed at build time, default 8) are cores. A source holds:
  project, overrides, priority, join mode (`FOLLOW` = locate to the timeline; `RESTART_ON_BAR`), its musical origin
  (`B` at its `play()`, optionally quantized to a bar), script-set values (shadows, replayed on binding), schedules.
- **Binding loop** (main thread, ~10 Hz): score every playing source by estimated level at the listener: 3D from
  `volume_db`, `unit_size`, `attenuation_model`, `max_distance` and distance to the current listener/camera; 2D from its
  attenuation exponent and `max_distance`; non-positional from `volume_db`. Pluggable `score_func` (Callable) for
  occlusion etc. Pinned sources first (`pinned = true`, or pending schedules), then by score + priority. Bind the top N
  with hysteresis (a challenger must beat the weakest bound source by ≥ 3 dB for ≥ 250 ms), prefetching sources that
  are approaching audibility (≥ −60 dB) because binding takes time.
- **Bind:** worker: boot (or take a pre-booted spare) → import project → overrides → replay shadows → `locate(B −
  origin)` → hand-off under the lock with a short fade-in. **Unbind:** ~30 ms fade-out on the audio thread, release,
  re-boot as a spare in the background. (A copy boots once — its statics cannot be reset in place — so "re-boot"
  means reuse by import, or a reset of the copy's data: §13.9.)
- Unbound sources keep their state; their scripts keep working (shadows); they emit no events. More pinned sources than
  N: the lowest-priority pinned ones lose, with a warning.

## 8. Code generation (D10)

`dump.c` (compiled against the firmware) → `params.json` → generator → C++ binding sources + Godot class-reference XML.
Rules (from the real tables, `gen_preview.py`):

- One property per **parameter id**, not per page slot (ids 0–3 appear on several pages).
- Names page-qualified: `RATE` ×3, `ATK/DEC/SUS` ×8, `FLT`, `PIT`, `SHP`, `MIX`, `TYPE`, `LVL`, `SWG` repeat; never look a
  parameter up by bare label (§11).
- Engine parameters from each engine's `edit[]` (FILTER reuses engine slots, blank labels in `TP`); skip the retired
  engine slot `-` (index 1): 13 engine views.
- Enum labels → identifiers: `1/16` → `R_1_16`, `S&H` → `S_AND_H`, `UP+8` → `UP_PLUS_8`, `8T` → `R_8T`.
- A hand-kept alias file for readability (`arp_2_swg` → `arp_swing`, `cut` → `cutoff`); generated names stay as
  fallbacks so a Felucca update never silently renames.
- Mode-dependent engine descriptors (`e->desc`) exist at runtime; the generator uses `edit[]`, the core clamps anyway.
- Counts (1.6.1): FeluccaPart 88 properties, song 17, 31 enums, 13 engine views.

## 9. Web export (D16)

What Godot 4.7.2's source shows (`platform/web/audio_driver_web.cpp`, `scene/audio/audio_stream_player_internal.cpp`):
- **Threaded** web builds mix on a dedicated audio thread under the driver mutex (as desktop). They need cross-origin
  isolation (COOP/COEP) from the host. **Non-threaded** builds mix from worklet callbacks on the main thread: all
  instances would render on the game's frame thread (dropouts when a frame hitches).
- The web's default "sample" playback applies only to streams that `can_be_sampled()`; ours is mixed as a stream. Set
  `playback_type = STREAM` on our players explicitly.
- GDExtension on the web needs the export option *Extension Support* (dlink templates) and an extension built with
  Emscripten as a side module (godot-cpp has `platform=web`); toolchain/version pairing has broken before (godot
  #105717, 4.4). Not verified for 4.7.
- Our parts: renames (D3) instead of objcopy; worker → synchronous fallback; the browser's mix rate (often 48 kHz)
  goes through the resampler; download size grows ≈ 1 MB per pool copy (fold read-only data); wasm speed: Felucca's
  own wasm ran ~60× real time in V8 on a light patch (T10), the heavy song is unmeasured there.

Plan: desktop first; a web spike after M2 (build the extension for the web, one instance, threads on) decides whether
M8 happens.

## 10. Evidence

C tests reproduced on 63f88ec + series by `prototypes/run_all.sh`; Godot tests by `spikes/godot_clock_pause/run.sh`.
The core itself (M1) asserts T1–T4, T8 and T11 through `fel_core_t` in `core/tests/run.sh`, on the host clock: T2 for
all ten ARP RATE values (exact step counts, stopped and playing, 120 and 90 BPM); T8 on renamed pool copies (0 of
51 680 mix steps differ in 10 min; the user's project identical in two copies); T4 0.7–0.8 % of a core on this machine.
M2 adds T5 and T6 on the core (`test_timing.c`, `test_delay.c`, test copies with white-box accessors): T5, 5 min of
int tweens and 3 min of a float tempo with pauses, every step 0 to 0.725 ms early, mean −0.36 ms, never late; T6, 0
jumps constant / int tween / float tween (the same analysis finds 52 and 1140 without the glide); and locate (§6.4).

| | Test | Result |
|---|---|---|
| T1 | Direct ANALOG CUT while a note is held (`test_api.c`) | spectral centroid 277 → 411 → 1166 → 2145 Hz for CUT 31 → 127; CUT 0 near-silent |
| T2 | Direct ARP RATE (`test_api.c`) | 1/4 → 1 onset/s, 1/2 → 1/s, 1/32 → ~12.5/s (detector-limited), 120 BPM |
| T3 | Project round trip (`test_api.c`) | 3840 bytes, magic bytes `ANUF`; import restores values |
| T4 | CPU, heavy song (`heavy.c`) | 1.2 % of one core (Xeon 2.1 GHz) |
| T5 | Tempo tweens, 5 min, 1/16 + 1/4 + 1/8T (`tempo.c`) | A internal: 17–60 ms error, tracks diverge. B pulses: ≤ 0.75 ms, 1148 beat-length changes at constant tempo. C host clock: ≤ 0.85 ms, none |
| T6 | Delay clicks (`clicks.c` + `wet.py`) | B constant: 178 jumps; C constant: 0; C tween: 62; with 0003: 0 in all |
| T7 | Internal-clock division drift, 10 min (`drift.c`) | 120 BPM 54 ms apart, 128: 87 ms, 140: 0 |
| T8 | Two copies in one binary (`multi.c`, `lockstep.c`) | isolated; 10 min: identical positions, 0 of 206 718 blocks differ |
| T9 | Knock then door (`sched.c`) | 4 kicks within 12–14 samples of their KNOCK events; DOOR at the next loop's step 1 |
| T10 | Felucca's wasm in Node (`probe.mjs`, spinkham/felucca-try's build) | 0 imports; two instances isolated; ~60× real time |
| T11 | **The user's export** `tests/fixtures/Felucca-project-D-2026-10-24.json` (`project_load.c`) | firmware 1.6.1, 3840 bytes, CRC-32 642C0757 ok; "SOLAR SIGNAL", 68 BPM: PHASE (LEN 32), PHASE, LOFI (empty), DRUM; plays (rms ≈ 0.05, peak 0.27); two renders identical |
| T12 | Godot 4.7.2: clock effect, `_process_silence() → true`, muted empty bus (`spikes/`) | 168 calls in 2 s (172 expected at 512 frames/44.1 kHz with the dummy driver's sleep timing); runs while paused; autoload hears PAUSED first |
| T13 | Godot 4.7.2: 8 players, 4000 nodes between each, 300 pause/unpause cycles (`spikes/`) | per-node (Godot default): 21 504 frames apart, 287/300 checks out of step; atomic under `AudioServer.lock()`: 0 |

## 11. Pitfalls already hit

- Label lookup is ambiguous: `"WAVE"` found the LFO wave, not ANALOG's oscillator; `"RATE"` finds the LFO rate first.
- ARP RATE enum order is not by speed: 1/4 1/8 1/16 1/32 8T 16T 1/2 1/1 2BAR 4BAR.
- Cutoff is engine-specific (ANALOG E4 `CUT`, id 107); FM6 has none; FM6 operator values are patch bytes, not `p[]`.
- Recorded automation / locks override code-set values at their steps; `motion_capture` only moves the base.
- `proj_pack()` returns 1 on success. `web_test_heavy()` programs steps but does not start the sequencer.
- A step overlay's undo must name its target step. Mode B's pulses must be delivered per 32-sample block.
- The web app compares the magic as the string `"FUNA"` while the bytes are `ANUF`; irrelevant to loading.
- Godot's tree pause pauses players one node at a time without the audio lock: players drift apart (T13).
- Godot's unpause sets `stream_paused = false` on every player (a user's own pause is lost; godot #65743): track it.
- The FM-1 mixes its four parts to one stereo pair: one source = one stereo output (accepted, R4).
- objcopy symbol localization is ELF-only in practice; use renames (D3).

## 12. Alternatives catalogue (what each replaces)

| Id | Approach | Replaces | Notes |
|---|---|---|---|
| A1 | **dlopen private copies** (felucca-vst: memfd / temp file / unique DLL), unlimited instances | L1 provider | desktop only |
| A2 | **wasm runtime** (wasmtime JIT; wasm3/WAMR where JIT is barred) running `felucca.wasm` + our exports | L1 provider | slower, runtime size; natural with godot-rust; on the web, the browser's own wasm instances |
| A3 | **godot-rust** over the same C core (`cc` + FFI to `fel_core_t`) | L3 (L2 too if rewritten) | gdext #1131/#1133 (audio-thread `mix`); Rust game code gets compile-time-checked enums |
| A4 | Internal clock (`G_BPM`), exact tempos or song mode only | clock mode | rejected (R7; T5, T7) |
| A5 | **fm1-emu** (Rust, MIT; emulates the AC791N, runs `.fwsc`; browser, native, CLAP/VST3/AU) | L0+L1 | parameters = RAM addresses: brittle; 3–6× real time |
| A6 | **MIDI pulses** into PR 269's external clock | clock mode | beat-length flicker → delay clicks / wobble (T5, T6) |
| A7 | String-keyed API | L3 | rejected (R5) |
| A8 | Polling only | events | misses steps, no tags, per-script latency maths |
| A9 | Felucca-Engines only (MIT) | L0 | loses R2/R8; moot now the game is GPL |
| A10 | Iframe/web page (fm1-emu `?embed=1`, `webapp/try/`) | everything | no parameter API |
| A11 | Editor SysEx in the core so the web editor edits a running instance (FM1VST's "Show to editors") | L1 extra | needs the OTA plumbing in the board file |
| A12 | Pause via Godot's per-node handling | D7 | desyncs players (T13) |
| A13 | Join a newly bound source by restarting its pattern on the next bar | D14 locate | simpler; loses phase continuity |

## 13. Open questions / unverified

1. `locate()` (§6.4): exact at constant tempo, within 2 samples after tempo changes (M2); song-mode projects not yet
   (refused while a chain plays; the core never arms one).
2. Felucca's own suite (`tests/run_tests.sh`) on the patched tree; arps, ratchets, nudge, swing, song mode under the
   host clock fed from `B` (PR 269's `midi_drift_test.c` covers the external-clock paths).
3. Clock effect with a real driver (PulseAudio/WASAPI/CoreAudio) and with mix rates ≠ 44100; buffer size per step
   (512 here) is driver-dependent: read it, don't assume.
4. Web: dlink templates and Emscripten pairing for 4.7.2, threaded audio, CPU of N instances in wasm (§9).
5. Symbol renames on Windows (MSVC vs clang-cl), macOS, Emscripten; hostsim imports stubbed.
6. Pattern switching with automation (shared motion store); v1 may swap steps and timing only.
7. Pool size N and binary-size budget (default 8).
8. User samples (SAMPLE engine user slots): unsupported; a backup's sample objects could go into the flash image later.
9. Spares (§7): a copy cannot boot twice. Reuse a bound-then-released instance by importing the next project (what the
   device does on a project load: transport stopped, notes released; FX tails and noise state carry over), or put each
   copy's data in its own section (`#pragma clang section` / linker `__start_`) and restore it. Decide in M5.
10. The core does not detect a jump in `B` itself: L2 calls `locate` on a discontinuity it makes (binding, a
    `follow` source resumed). Otherwise a jump larger than half a beat per 32-sample block plays as a fast-forward
    (capped at half a beat a block).
11. Felucca's ASan builds (its `drumfuzz`, `midi_drift_test`) crash at random here with `AddressSanitizer:DEADLYSIGNAL`
    loops, on the unpatched base too (gcc 12's runtime on Linux 6.8; 2 of 6 identical runs of a deterministic test):
    the environment, not the code. Their non-ASan builds pass. Cap the output when running the suite (one loop wrote
    15 GB).
12. What runs on rendered samples, not on the clock's units: the FX layer's 1/16 grid (perform.c `pf.ph`), SLICER
    (slicer.c, from the integer `G_BPM`), a FREE synced LFO 2 (voice.c `lfo_sync_inc`). Under the host clock they keep
    moving through a pause of `B` and drift from it at fractional tempos (SLICER: `G_BPM` is rounded). `locate` puts
    them in phase. A patch could feed them units (as PR 269 does the sequencer).

## 14. Implementation plan

| M | Deliverable | Done when |
|---|---|---|
| M1 | `core/`: board file + `fel_core_t` (params, engines, projects incl. JSON validation, transport, render with `B`), rename-based pool build (N copies), C tests from the prototypes | T1–T4, T8, T11 as C tests, on Linux; 2-copy link test detects an unrenamed global. **Done** (2026-10-24): `core/tests/run.sh` |
| M2 | `locate()` + host clock from `B` + delay glide wired in the core | located instance's steps = continuous instance's; T5/T6 equivalents. **Done** (2026-10-24): `core/tests/run.sh` |
| M3 | GDExtension skeleton (godot-cpp 10.0.0, api 4.7): `FeluccaStream`/playback, `ClockEffect`, conductor autoload (timeline, tempo, lock-published commands, atomic pause), `FeluccaSource*`, `FeluccaProject` importer; demo scene with the user's project | two sources in sync; T12/T13 equivalents pass with Felucca sources |
| M4 | Codegen: registry dump, generator, typed `FeluccaPart`/engine views/enums, `FeluccaOverrides`, docs XML, alias file | a typo in a demo script is a parse error |
| M5 | Pool: scoring, hysteresis, prefetch, worker binding, spares, fades | the cellar/hallway/bedroom scenario with N = 8 |
| M6 | Scheduler + events + heard-time signals; `FeluccaWhen`/`FeluccaAction`; `FeluccaPattern` | the knock-and-door scene |
| M7 | Optional: debug screen view (RGB565 → `Image.FORMAT_RGB565` after a byte swap; input), Windows/macOS builds | — |
| M8 | Web (if the spike after M2/M3 says yes) | demo scene in a browser, threads on |

## 15. References

- Felucca https://github.com/hugelton/Felucca (main `63f88ec18b71`, 1.6.1 `a53ecf428c29`, engines `c46e0086f69b`);
  PR 269 https://github.com/hugelton/Felucca/pull/269 (head `ebdf867d631b`); Felucca-WebApp
  https://github.com/hugelton/Felucca-WebApp (`b7947ad1da20`); editor protocol: Felucca `web/EDITOR_PROTOCOL.md`
- Godot 4.7.2-stable (`ed1daf0bf`): `servers/audio/audio_server.cpp` (`_mix_step`, `set_playback_paused`, effects and
  `process_silence`, bound `lock`/`unlock`), `scene/audio/audio_stream_player_internal.cpp` (pause notifications),
  `platform/web/audio_driver_web.cpp`; release https://godotengine.org/article/maintenance-release-godot-4-7-2/
- godot-cpp https://github.com/godotengine/godot-cpp (10.0.0-stable `507ed9d`, 2026-09-15; `api_version` 4.3–4.7)
- Prior art: felucca-vst https://github.com/cdodd/felucca-vst, FM1VST https://github.com/Quixotic7/FM1VST, fm1-emu
  https://github.com/joakimeriksson/fm1-emu, FM-1 Mobile https://github.com/hb3p8/fm1-mobile-emu, FM1 DECK
  https://github.com/TonalPAK/FM1-DECK, awesome list https://github.com/cicloid/awesome-fm-1
- gdext issues https://github.com/godot-rust/gdext/issues/1131, /1133; Godot web GDExtension issue
  https://github.com/godotengine/godot/issues/105717; `stream_paused` reset https://github.com/godotengine/godot/issues/65743
