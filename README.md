# felucca-godot

The is [Felucca](https://github.com/hugelton/Felucca) (the wonderful multi-engine synth firmware for the M-VAVE FM-1) running inside
Godot 4.7+ as a native extension. You can load projects you made on your real hardware FM-1 into Godot and control them from GDScript! You can sync game events to the music, or sequence notes and effect changes for any instrument.  You can even run multiple Felucca engines in parallel with everything synced to a global beat. All of this can run in the browser, too. Pretty cool.

> [!NOTE]
> Note that the desktop version only runs on Linux right now, so if you're on Mac or Windows you'll have to stick to web builds.

## Install

1. Download the addon zip from the [Releases page](https://github.com/six-school/felucca-godot/releases) and unzip it into your project's folder. You should end up with an `addons` folder with `felucca` inside it.
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
  song.play() # or set the source to autoplay in the editor
```

## API overview

### `FeluccaConductor`

This controls the global beat for all the synth instances. You probably won't need to touch this much after setting it off, other than to change tempo.

- Get or set `FeluccaConductor.tempo` (you can use Godot's [tweens](https://docs.godotengine.org/en/stable/classes/class_tween.html) to change it over time too!)
- `play()`, `stop()`, and `running` do what you'd expect.
- Subscribe to global beats with the `on_beat` signal.
- Some other stuff: `playhead` (beats since playback started), `bar`, `beat`.

### `FeluccaSource`, `FeluccaSource2D`, `FeluccaSource3D`

- `project`, `play()`, `stop()` do what you'd expect. `stream_paused` is kind of a weird name, but that's how you already pause/resume audio streams in Godot so that's how you do it for these ones too.
- `quantize`: `QUANTIZE_BEAT` (start on the next beat) `QUANTIZE_BAR` (start on the next 4-note bar), or `QUANTIZE_NOW` (free jazz).
- `join`: if this source gets paused and then unpaused later, should it `JOIN_FOLLOW` (jump back in step with the current time) or `JOIN_HOLD` (continue from where it stopped)? It'll stay on beat either way.
- `muted_parts`: you can mute one of the four instruments with this.
- `overrides`: you can override an engine or global synth parameter with this (for example, crank up the delay send on the drums).
- `get_snapshot()`: the state of Felucca instance at that point in time
- `pinned`: there's a limit to how many instances can be running at once, and if there's too many normally the quietest/furthest get the boot. You shouldn't need to touch this, but if you need to keep a source audible you can set this and it'll never give up its spot. Incidentally, `is_bound()` and the signals `bound` and `unbound` are for whether a source has a spot and to listen for when a source gets or loses a spot, respectively.

There's more APIs available, docs coming soon! For now, you can look at the [Godot demo project](demo) for examples.

## Building from source

If the pre-built extension on the [Releases page](https://github.com/six-school/felucca-godot/releases) doesn't work or you want to change the code, you'll need to build from source.

The build process has only been tested on Linux. You'll need git, clang, gcc/g++ and Python 3 installed. Then run:

```sh
tools/setup_felucca.sh  # pull Felucca repo, apply patches, prepare for build.
tools/setup_godot.sh    # install Godot and the GDExtension native SDK.

# then build!
third_party/.felucca-venv/bin/python -m SCons target=template_debug # or target=template_release
```

The built extension will be output to `demo/addons/felucca/bin/`.

To make a web build:

```sh
tools/setup_web.sh  # set up the web toolchain (emscripten and godot templates)
source third_party/emsdk/emsdk_env.sh
third_party/.felucca-venv/bin/python -m SCons platform=web target=template_debug # or target=template_release
```

## Licence

[GPL-3.0-only](LICENSE). Felucca was created by Hügelton Instruments (Leo Kuroshita) and is also GPL-3.0, but some parts of it retain their own licences. Check [Felucca](https://github.com/hugelton/Felucca) for details.
