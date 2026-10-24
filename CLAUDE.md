# felucca-godot

Felucca (FM-1 synth firmware, C) inside Godot 4.7 as a GDExtension. **Read `docs/DESIGN.md` first**: the requirements
(§1), the decisions and their evidence (§3, §10), the timing and pause design (§6), the pool (§7), the open questions
(§13) and the milestone plan (§14). When asked to try a different approach, start from §12 (each alternative names the
layer it replaces) and keep the seams of §4.1 intact.

## Setup and checks

- `tools/setup_felucca.sh`: Felucca `63f88ec` + `patches/` + generated headers into `third_party/felucca` (gitignored).
- `prototypes/run_all.sh`: rebuilds and reruns the C prototypes; expected results are DESIGN.md §10.
- `core/build.sh [N]`: the pool (N renamed copies of `core/board.c`) → `build/core/libfelucca_core.a`;
  `core/tests/run.sh` builds it and runs the checks: M1's (T1–T4, T8, T11, the JSON reader, the 2-copy link test) and
  M2's (T5, T6, locate) on test copies (`core/tests/test_hooks.c`: white-box accessors, renamed as the pool).
- Felucca's own suite (`tests/run_tests.sh`) needs the device image (AC79 toolchain) for a few tests; its ASan builds
  crash at random in this environment (DESIGN.md §13.11): cap the output (`| head -c`) and use a timeout.
- `tools/setup_godot.sh`: Godot 4.7.2 (release zip, SHA-512 pinned), godot-cpp `507ed9d` (10.0.0-stable), SCons
  4.9.1 in the venv. Build: `third_party/.felucca-venv/bin/python -m SCons` (the venv's own scripts may name another
  path) → `demo/bin/libfelucca.*.so`; `extension/run_tests.sh`: build, import, `demo/tests/m3_test.tscn` headless at
  44.1 and 48 kHz.
- `spikes/godot_clock_pause/run.sh <godot-4.7.2-binary>`: the GDScript spike of T12, T13.
- Toolchain used so far: clang (the core), g++ (the extension: godot-cpp's default), Python 3 (Pillow + fontTools +
  SCons in the venv), Godot 4.7.2.

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
