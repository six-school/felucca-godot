# felucca-godot

[Felucca](https://github.com/hugelton/Felucca) (multi-engine synth firmware for the M-VAVE FM-1) running headless inside
Godot 4.7 as a GDExtension: projects made on the device, parameters and scheduling from GDScript, a pool of instances
kept in sync with the game, pause-safe.

Status: M1 done, the C core (`core/`); the Godot extension itself (M3) is not written yet.

- `docs/DESIGN.md`: requirements, decisions with their evidence, architecture, the alternatives and how to switch to each
- `core/`: the board file and the `fel_core_t` table (L1), the project JSON reader, the pool build of N renamed copies
  (`core/build.sh`) and its tests (`core/tests/run.sh`)
- `patches/`: the Felucca patch series (PR 269 and three of ours), on Felucca `63f88ec`
- `tools/setup_felucca.sh`: clones Felucca at that commit, applies the series, generates its headers
- `prototypes/`: the C experiments the decisions rest on; `prototypes/run_all.sh` rebuilds and reruns them
- `spikes/godot_clock_pause/`: Godot 4.7.2 headless tests of the clock effect and of pause handling
- `tests/fixtures/`: a real project exported from an FM-1 running Felucca 1.6.1

Licence: GPL-3.0 (Felucca is GPL-3.0-only, and so is this project).
