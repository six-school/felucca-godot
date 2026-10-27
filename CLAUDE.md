# felucca-godot

Felucca (FM-1 synth firmware, C) inside Godot 4.7 as a GDExtension. **Read `docs/DESIGN.md` first**: the requirements
(§1), the decisions and their evidence (§3, §10), the timing and pause design (§6), the pool (§7), the open questions
(§13) and the milestone plan (§14). When asked to try a different approach, start from §12 (each alternative names the
layer it replaces) and keep the seams of §4.1 intact.

## Setup and checks

- `tools/setup_felucca.sh`: Felucca `63f88ec` + `patches/` + generated headers into `third_party/felucca` (gitignored).
- `core/build.sh [N]`: the pool (N renamed copies of `core/board.c`) → `build/core/libfelucca_core.a`;
  `core/tests/run.sh` builds it and runs the checks: M1's (T1–T4, T8, T11, the 2-copy link test),
  M2's (T5, T6, locate) and M5's (the cut) on test copies (`core/tests/test_hooks.c`: white-box accessors, renamed as
  the pool), M6's scheduler; it also writes `demo/tests/heavy.felucca` (the heavy test song as a project, M7's
  measures) when that changes.
- Felucca's own suite (`tests/run_tests.sh`) needs the device image (AC79 toolchain) for a few tests; its ASan builds
  crash at random in this environment (DESIGN.md §11): cap the output (`| head -c`) and use a timeout.
- `tools/setup_godot.sh`: Godot 4.7.2 (release zip, SHA-512 pinned), godot-cpp `507ed9d` (10.0.0-stable), SCons
  4.9.1 in the venv. Build: `third_party/.felucca-venv/bin/python -m SCons` (the venv's own scripts may name another
  path) → `demo/addons/felucca/bin/libfelucca.*.so`; `extension/run_tests.sh`: build, import, then headless
  `demo/tests/m3_test`, `m5_test` and `m5_scene_test` (the pool), `m6_test` (scheduling) at 44.1 and 48 kHz, `m4_test`
  (the typed API), `project_test` (the project file), the typo scripts.
- The C prototypes, the Godot spike and `tests/fixtures/` (§10's first evidence) were removed after M7; DESIGN.md §10
  says how to get them back from git.
- Web (M7, DESIGN.md §9: threads only, Chromium only): `tools/setup_web.sh` (after `setup_godot.sh`): Emscripten
  4.0.20 (the version Godot 4.7.2's web templates are built with) into `third_party/emsdk`, and the 4.7.2 export
  templates' `web_dlink_*` (SHA-512 checked) into Godot's templates folder (Linux's). Build (emsdk_env.sh is bash's):
  `bash -c '. third_party/emsdk/emsdk_env.sh && third_party/.felucca-venv/bin/python -m SCons platform=web
  target=template_debug'` (and `template_release`) → `demo/addons/felucca/bin/libfelucca.web.*.wasm32.wasm`, both needed
  to export. `extension/run_web_tests.sh`: the "Web tests" export (its feature's main scene, `tests/web_tests.tscn`,
  runs the test `?scene=` names) in headless Chromium at 44.1 and 48 kHz (`res://override.cfg` in the pack); m3, m5,
  m5_scene, m6, m4, project_test. `extension/run_web_bench.sh`: `tests/m7_bench` from release builds, in Chromium and on
  the desktop, and the download's sizes. `tools/serve_web.py DIR [PORT]` serves an export with COOP/COEP;
  `tools/web_run.mjs URL --until REGEX` prints a page's console (and its workers') from headless Chromium. Kill a server
  with `pgrep -f '^python3 tools/serve_web.py'` (`pkill -f` matches its own shell).
- The demos start from `demo/picker.tscn` (the main scene, the web's too): main, pool, knock, the 3D scene; P or
  Escape back. The user exports "Web" from the editor themselves: leave the export presets alone. "Selected scenes"
  export mode drops the extension (`*.gdextension` in the include filter keeps it); the PWA option's service worker
  serves an old build after a re-export until it is unregistered.
- Toolchain used so far: clang (the core), g++ (the extension: godot-cpp's default), Python 3 (Pillow + fontTools +
  SCons in the venv), Godot 4.7.2; for the web, Emscripten 4.0.20, Chromium 156 (headless) and Node 24.

## Rules of thumb

- Never modify Felucca's sources in place: change them only through `patches/` (`git format-patch` series on
  `63f88ec`), each behind an `#ifdef` unless it is upstream code.
- Never look a Felucca parameter up by its bare label (labels repeat across pages); use ids and page-qualified names.
  The typed API is generated (`extension/codegen/gen_api.py`); readable names go in `extension/codegen/names.json`,
  keyed by id and the label it must have.
- The audio path (anything reached from a playback's mix or the clock effect) must not allocate, lock or log.
- Anything that must take effect for several instances on the same mix step is published under `AudioServer.lock()`.
- Firmware state is global per compiled copy: one copy per pool instance (DESIGN.md D3).
- Felucca's own code style: C99 (gnu99), dense comments that say why, `-ffp-contract=off` as its host build.
