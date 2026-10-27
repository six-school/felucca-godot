# felucca-godot

[Felucca](https://github.com/hugelton/Felucca) (multi-engine synth firmware for the M-VAVE FM-1) running headless inside
Godot 4.7 as a GDExtension: projects made on the device, parameters and scheduling from GDScript, a pool of instances
kept in sync with the game, pause-safe.

Status: M1–M7 done: the C core (`core/`) with its host clock and `locate`, and the GDExtension (`extension/`): sources
play a project made on the device, in sync on one timeline, pause-safe, every parameter typed from the firmware's own
tables (`FeluccaPart`, its engine views, `FeluccaSong`, `FeluccaOverrides`); a pool of 8 instances shared by any
number of sources (M5); scheduling on the music's steps, beats and bars, with signals as heard (M6); a threaded web
build, its tests in headless Chromium (M7). Next: M8 (optional) and M9 (patterns), DESIGN.md §14.

- `docs/DESIGN.md`: requirements, decisions with their evidence, architecture, the alternatives and how to switch to each
- `core/`: the board file and the `fel_core_t` table (L1), the pool build of N renamed copies
  (`core/build.sh`) and its tests (`core/tests/run.sh`)
- `extension/`, `SConstruct`: the GDExtension (godot-cpp 10.0.0, API 4.7): conductor, sources, project importer,
  scheduler; its Godot-side tests (`extension/run_tests.sh`; in a browser, `extension/run_web_tests.sh`; the web's
  measures, `extension/run_web_bench.sh`)
- `demo/`: a Godot 4.7 project: the demos, picked in `picker.tscn` (P or Escape back): the user's project in two
  sources (`main.tscn`), the pool (`pool.tscn`), the knock and the door (`knock.tscn`), a 3D scene (`3d/`); the tests
  (`tests/`). Its "Web" export preset (extensions and threads on) needs the extension's web build in `demo/bin`. A project
  using the extension needs its autoload `Felucca="*res://addons/felucca/felucca.tscn"` (atomic pause: DESIGN.md D7), and
  `debug/gdscript/warnings/unsafe_property_access=2` and `unsafe_method_access=2` for a misspelled parameter or method
  to be a parse error (D10)
- `tools/setup_godot.sh`: Godot 4.7.2 (checked), godot-cpp 10.0.0-stable, SCons
- `tools/setup_web.sh`: Emscripten 4.0.20 and Godot 4.7.2's web export templates (checked);
  `tools/serve_web.py`: serves a web export with the headers its threads need (COOP/COEP); `tools/web_run.mjs`: a
  page's console from headless Chromium
- `patches/`: the Felucca patch series (PR 269 and five of ours), on Felucca `63f88ec`
- `tools/setup_felucca.sh`: clones Felucca at that commit, applies the series, generates its headers
- `prototypes/`: the C experiments the decisions rest on; `prototypes/run_all.sh` rebuilds and reruns them
- `spikes/godot_clock_pause/`: Godot 4.7.2 headless tests of the clock effect and of pause handling
- `tests/fixtures/`: a real project exported from an FM-1 running Felucca 1.6.1

Licence: GPL-3.0 (Felucca is GPL-3.0-only, and so is this project).
