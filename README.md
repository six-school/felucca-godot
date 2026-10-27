# felucca-godot

The is [Felucca](https://github.com/hugelton/Felucca) (the wonderful multi-engine synth firmware for the M-VAVE FM-1) running inside
Godot 4.7+ as a native extension. You can load projects you made on your real hardware FM-1 into Godot and control them from GDScript! You can sync game events to the music, or sequence notes and effect changes for any instrument.  You can even run multiple Felucca engines in parallel with everything's synced to a global beat so it all lines up. All of this can run in the browser, too. Pretty cool.

> [!NOTE]
> Note that the desktop version only runs on Linux right now, so if you're on Mac or Windows you'll have to stick to web builds.

## Install

1. Download the addon zip from the Releases page and unzip it into your project's folder. You should end up with an `addons` folder with `felucca` inside it.
2. Go to `Project > Project Settings > Globals > Autoload > Select Script/Scene` and choose `addons/felucca/felucca.tscn`.
3. You might need to restart the editor.

### Exporting for the web

- In `Project > Export...`, add the Web preset if it's not already there, then check Extensions Support and Thread Support. You might see a warning that the export template isn't found or whatever. If there is, it should have a link to "Manage Export Templates" that lets you download the template for the web build.
- Threads support for web exports causes builds not to work [unless the server provides specific headers](https://docs.godotengine.org/en/stable/tutorials/export/exporting_for_web.html#thread-and-extension-support). For testing locally, Godot can handle that for you. Make sure "Runnable" is enabled for the Web preset in the Export dialog and a new little button should appear next to the play buttons in the top-right of the main Godot screen.

## Projects

Make a project on the FM-1, then export it using the [Felucca web editor](https://hugelton.github.io/Felucca/webapp/editor/) over USB. It should give you a `.json` file, and you'll have to change that to `.felucca` to bring it into your project (annoying, I know).

## Quick start

Add a `FeluccaSource2D`, `FeluccaSource3D`, or non-spatial `FeluccaSource` to a scene and point it at your Felucca project.

Then you can start scripting!

```gdscript
extends Node2D

@onready var song: FeluccaSource2D = $CoolSong

func _ready() -> void:
	FeluccaConductor.tempo = song.project.bpm
	FeluccaConductor.play()
	song.play()
```

## API overview

### `FeluccaConductor`

This controls the global beat for all the synth instances. You probably won't need to touch this much after setting it off, other than to change tempo.

- Get or set `FeluccaConductor.tempo` (you can use Godot's [tweens](https://docs.godotengine.org/en/stable/classes/class_tween.html) to change it over time too!)
- Subscribe to global beats with the `on_beat` signal.
- Some other stuff: `play()`, `stop()`, `running`, `playhead` (beats since playback started), `bar`, `beat`

### `FeluccaSource`, `FeluccaSource2D`, `FeluccaSource3D`

- `project`, `play()`, `stop()` do what you'd expect. `stream_paused` is kind of a weird name, but that's how you already pause/resume audio streams in Godot so that's how you do it for these ones too.
- `quantize`: `QUANTIZE_BEAT` (start on the next beat) `QUANTIZE_BAR` (start on the next 4-note bar), or `QUANTIZE_NOW` (free jazz).
- `join`: if this source gets paused and then unpaused later, should it `JOIN_FOLLOW` (jump back in step with the current time) or `JOIN_HOLD` (continue from where it stopped)? It'll stay on beat either way.
- `muted_parts`: you can mute one of the four instruments with this.
- `overrides`: you can override an engine or global synth parameter with this (for example, crank up the delay send on the drums).
- `get_snapshot()`: the state of Felucca instance at that point in time
- `pinned`: there's a limit to how many instances can be running at once, and if there's too many normally the quietest/furthest get the boot. You shouldn't need to touch this, but if you need to keep a source audible you can set this and it'll never give up its spot. Incidentally, `is_bound()` and the signals `bound` and `unbound` are for whether a source has a spot and to listen for when a source gets or loses a spot, respectively.

---

Beyond here be slop docs

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
