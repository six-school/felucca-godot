# felucca-godot

[Felucca](https://github.com/hugelton/Felucca), the multi-engine synth firmware for the M-VAVE FM-1, running inside
Godot 4.7 as a GDExtension. Projects made on the device play in your scenes, all on one tempo and paused with the game,
their parameters and notes driven from GDScript.

- **The device's own sound**: a project made on an FM-1 running Felucca 1.6.1 plays as it does on the device: every
  engine, the sequencer and the effects, from the same firmware.
- **Many sources**: any number of sources share a pool of 8 synth instances. The ones nearest the listener get one;
  the others keep time silently and come in in step when they get one.
- **One timeline**: one tempo for the whole game (tween it). Every source stays on the same beat through tempo
  changes, and pausing the game pauses all of its music at once.
- **Typed parameters**: every parameter of the device is a typed property, its choices enums. With two project
  settings (below), a misspelled one is an error in the editor.
- **Scheduling on the music**: hits, notes, parameter changes and mutes on a part's next step, loop or bar, with
  signals when they are heard.

Platforms: Linux (x86_64) and the web (a threaded export; tested in Chromium). Windows and macOS are planned.

## Install

1. Download the addon zip from the Releases page and unzip it into your project's folder: it adds `addons/felucca/`.
2. Add the autoload: Project > Project Settings > Globals > Autoload, path `res://addons/felucca/felucca.tscn`, name
   `Felucca`. A pause stops every source at once only with it.
3. Recommended: in Project Settings > Debug > GDScript (with Advanced Settings on), set Unsafe Property Access and
   Unsafe Method Access to Error. A misspelled parameter or method is then an error as you type, not when the line
   runs.
4. Restart the editor.

The settings, as they end up in `project.godot`:

```ini
[autoload]

Felucca="*res://addons/felucca/felucca.tscn"

[debug]

gdscript/warnings/unsafe_property_access=2
gdscript/warnings/unsafe_method_access=2
```

### Exporting for the web

- In the Web preset, turn on Variant > Extensions Support and Thread Support.
- With the export mode "Export selected scenes", add `*.gdextension` to the Resources tab's filter for non-resource
  files, or the extension is left out.
- Serve the exported folder with the headers threads need: `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp`. The editor's Run in Browser sends them.

## Projects

Make a project on the FM-1, export it with Felucca's web editor (a `.json` file), rename it to `.felucca` and add it to
your project: Godot imports it as a `FeluccaProject` (`project_name`, `bpm`, `engines`).
`FeluccaProject.load_json(path)` reads an export at run time instead.

## Quick start

Add a `FeluccaSource2D` (or `FeluccaSource`, `FeluccaSource3D`) to a scene and set its `project`. The sources are
Godot's audio players, so `max_distance`, attenuation and the bus work as usual.

```gdscript
extends Node2D

@onready var cellar: FeluccaSource2D = $Cellar

func _ready() -> void:
	FeluccaConductor.tempo = cellar.project.bpm
	FeluccaConductor.play()
	cellar.play()    # on the next beat
```

`get_tree().paused = true` stops all of the music at once; unpaused, it carries on in step.

## API overview

Each parameter's range, default and place on the device are in the editor's help: `FeluccaPart`, `FeluccaSong` and
each engine's class (`FeluccaAnalog`, ..).

### `FeluccaConductor`: the timeline

- `tempo` (BPM; tween it), `play()`, `stop()`, `running`
- `playhead` (beats since `play()`), `bar`, `beat`; the signal `on_beat(bar, beat)`, as heard

### `FeluccaSource`, `FeluccaSource2D`, `FeluccaSource3D`

- `project`; Godot's own `play()`, `stop()` and `stream_paused`
- `quantize`: where `play()` starts: `QUANTIZE_BEAT` (the default), `QUANTIZE_BAR` or `QUANTIZE_NOW`
- `join`: after the source's own pause, `JOIN_FOLLOW` (back in step with the conductor, the default) or `JOIN_HOLD`
  (on from where it stopped, still on the beat)
- `pinned`: it always has a synth instance while it plays
- `muted_parts`; `overrides` (parameter values that replace the project's for this source)
- `is_bound()` and the signals `bound` and `unbound`: whether it has a synth instance (and sounds) now
- `get_snapshot()`: each part's step, loop and position, as heard

### Parameters

`part_1` .. `part_4` (also `parts`) are the project's four parts, `FeluccaPart`s; `song` (`FeluccaSong`) holds its
global settings. Each parameter of the device is a property of the part, and each engine's own are on the part's view
of that engine (`analog`, `drum`, `fm6`, ..: `null` while the part plays another engine). A value set before the source
has an instance is applied when it gets one.

```gdscript
var bass := cellar.part_1
bass.arp_rate = FeluccaPart.ArpRate.ARP_RATE_1_16
bass.send_reverb = 40
var synth := bass.analog    # FeluccaAnalog, or null
if synth:
	create_tween().tween_method(func(v: int) -> void: synth.cutoff = v, 30, 127, 0.8)
```

`engine` switches a part's engine (`FeluccaPart.Engine.ENGINE_ANALOG`, ..), loading its first preset as the device
does.

### Signals as heard

A part's signals come when the listener hears the sound, not when it is computed: `on_step(step, loop)`,
`on_loop(loop)`, `on_note(note, velocity)`, `on_hit(lane, velocity)` (a DRUM part) and `on_noteoff(note)`.

### Scheduling

`schedule(when, action, callback)` on a source holds an action until its moment in the music, and calls `callback`
when it is heard:

```gdscript
var drums := cellar.part_4
cellar.schedule(FeluccaWhen.next_loop(drums, 1), FeluccaAction.mask_loops(drums, 1))    # its own hits silent
for step in [1, 5, 9, 13]:
	cellar.schedule(FeluccaWhen.next_loop(drums, step), FeluccaAction.hit(drums, FeluccaPart.Lane.LANE_SNARE, 100))
cellar.schedule(FeluccaWhen.loops_ahead(drums, 2, 1), FeluccaAction.mark(), open_door)   # a loop later
```

- When: `FeluccaWhen.next_step(part, step)`, `next_loop(part, step)`, `loops_ahead(part, loops, step)`,
  `next_bar(beat)`. If the moment has passed by the time the source plays it: `LATE_NEXT` (the next one, the default),
  `LATE_DROP` or `LATE_NOW`.
- What: `FeluccaAction.hit(part, lane, velocity)` (in place of the step's own hit), `note(part, note, velocity, steps)`
  (in place of the step's notes), `note_layered(..)` (over them), `set_param(part, param, value)`,
  `mask_steps(part, n)`, `mask_loops(..)`, `mask_bars(..)` (the part's own steps silent), `mark()` (nothing sounds:
  only the callback).
- `schedule()` returns a `FeluccaSchedule`: its signal `triggered(event)`, `pending`, `dropped`, `cancel()`.
  `schedule_with_tag(when, action, tag)` and the source's `cancel(tag)` handle a group. A source with actions pending
  keeps its synth instance.

## Building from source

On Linux, with git, clang, g++ and Python 3:

```sh
tools/setup_felucca.sh    # Felucca at the pinned commit, with this project's patches
tools/setup_godot.sh      # Godot 4.7.2, godot-cpp, SCons
third_party/.felucca-venv/bin/python -m SCons target=template_debug     # and target=template_release
```

The libraries go to `demo/addons/felucca/bin/`; `demo/` is a Godot project with the demos. For the web:
`tools/setup_web.sh`, then the same SCons command with `platform=web`, with Emscripten's environment
(`third_party/emsdk/emsdk_env.sh`) sourced in bash.

## Licence

GPL-3.0-only (`LICENSE`), as Felucca is. Felucca is by Hügelton Instruments (Leo Kuroshita); some of its parts keep
their own licences, listed in its `LICENSING.md`. godot-cpp is under the MIT licence.
