# felucca-godot

Felucca (FM-1 synth firmware, C) inside Godot 4.7 as a GDExtension. **Read `docs/DESIGN.md` first**: the requirements
(§1), the decisions and their evidence (§3, §10), the timing and pause design (§6), the pool (§7), the open questions
(§13) and the milestone plan (§14). When asked to try a different approach, start from §12 (each alternative names the
layer it replaces) and keep the seams of §4.1 intact.

## Setup and checks

- `tools/setup_felucca.sh`: Felucca `63f88ec` + `patches/` + generated headers into `third_party/felucca` (gitignored).
- `prototypes/run_all.sh`: rebuilds and reruns the C prototypes; expected results are DESIGN.md §10.
- `spikes/godot_clock_pause/run.sh <godot-4.7.2-binary>`: Godot-side checks (T12, T13).
- Toolchain used so far: clang, Python 3 (Pillow + fontTools in a venv made by the setup script), Godot 4.7.2.

## Rules of thumb

- Never modify Felucca's sources in place: change them only through `patches/` (`git format-patch` series on
  `63f88ec`), each behind an `#ifdef` unless it is upstream code.
- Never look a Felucca parameter up by its bare label (labels repeat across pages); use ids and page-qualified names.
- The audio path (anything reached from a playback's mix or the clock effect) must not allocate, lock or log.
- Anything that must take effect for several instances on the same mix step is published under `AudioServer.lock()`.
- Firmware state is global per compiled copy: one copy per pool instance (DESIGN.md D3).
- Felucca's own code style: C99 (gnu99), dense comments that say why, `-ffp-contract=off` as its host build.
