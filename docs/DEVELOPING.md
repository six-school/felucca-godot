# Developing felucca-godot

The README's "Building from source" section gets you a build. This page covers the rest: what the setup scripts install, how to run the tests and the demos, and the rules to follow when you change the code. To learn how the extension is designed and why, read [DESIGN.md](DESIGN.md).

## Setup

- `tools/setup_felucca.sh` checks out Felucca at `63f88ec`, applies `patches/` and generates Felucca's headers, all in `third_party/felucca` (gitignored).
- `tools/setup_godot.sh` downloads Godot 4.7.2 (the release zip, SHA-512 pinned) and godot-cpp `507ed9d` (10.0.0-stable), and installs SCons 4.9.1 into the venv. Run SCons as `third_party/.felucca-venv/bin/python -m SCons` rather than through the venv's `scons` script, which may point at a different Python. Builds go to `demo/addons/felucca/bin/`. If you only build, pass `--no-godot` to skip the Godot binary, which only the tests and the editor need.
- For web builds, run `tools/setup_web.sh` after `setup_godot.sh`. It installs Emscripten 4.0.20 (the version Godot 4.7.2's web templates are built with) into `third_party/emsdk`, and puts Godot 4.7.2's `web_dlink_*` export templates (SHA-512 checked) into Godot's templates folder. `emsdk_env.sh` only works in bash, so from another shell, run:

  ```sh
  bash -c '. third_party/emsdk/emsdk_env.sh && third_party/.felucca-venv/bin/python -m SCons platform=web target=template_debug'
  ```

  You need both the debug and the release web builds to export. If you only build, pass `--no-templates` to skip the export templates (a 1.3 GB download).

So far everything has been built with clang (the core), g++ (the extension: godot-cpp's default), Python 3 (with Pillow, fontTools and SCons in the venv) and Godot 4.7.2, plus Emscripten 4.0.20, headless Chromium 156 and Node 24 for the web.

## Tests

- `core/tests/run.sh` builds the core and runs its C tests. The core is built as the pool: `core/build.sh [N]` compiles N copies of `core/board.c`, each with its globals renamed, into `build/core/libfelucca_core.a`. The tests cover parameters, projects, CPU use, the pool's copies, step timing, the delay's glide, locate, the cut and the scheduler, and a link test checks that a global missing from the renames gets caught. The script also rewrites `demo/tests/heavy.felucca` (the heavy test song as a project, used by `m7_bench`) when it changes.
- `extension/run_tests.sh` builds the extension, imports the demo project and runs the Godot tests headless: `m3_test` (the clock, the pause and sync), `m5_test` and `m5_scene_test` (the pool) and `m6_test` (scheduling) at 44.1 and 48 kHz, then `m4_test` (the typed API) and `project_test` (the project file). Last, it checks that each script in `demo/tests/typo/` fails to parse.
- `extension/run_web_tests.sh` runs the same Godot tests in headless Chromium at 44.1 and 48 kHz. It exports the "Web tests" preset, whose main scene (`tests/web_tests.tscn`) runs the test named by the page's `?scene=`; a `res://override.cfg` in the pack sets the mix rate.
- `extension/run_web_bench.sh` runs the `demo/tests/m7_bench` benchmark (boot, CPU, latency) from release builds, in Chromium and on the desktop, and reports the export's download sizes.
- To serve an export yourself, use `tools/serve_web.py DIR [PORT]`: threaded web builds need the COOP/COEP headers it sends. `tools/web_run.mjs URL --until REGEX` prints a page's console (its workers' too) from headless Chromium.
- Felucca's own suite (`third_party/felucca/tests/run_tests.sh`) needs the device image, built with the AC79 toolchain, for a few of its tests. Its ASan builds have crashed at random on Linux 6.8 with gcc 12's runtime (`AddressSanitizer:DEADLYSIGNAL` loops, on unpatched Felucca too), and one loop wrote 15 GB of output, so run it with a timeout and cap the output (`| head -c`).

The C prototypes and the Godot spike that first measured the design were removed in 6d13c9a. To get them back, run `git checkout 6d13c9a^ -- prototypes spikes tests`.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main` and every pull request. It runs the core's C tests and builds the extension for Linux and the web, in debug and release. The Linux library is built on Ubuntu 22.04 so that it runs on any Linux with glibc 2.35 (22.04's version) or later. A build step checks this: it fails if the library requires a glibc version above 2.35. The Godot tests don't run there: they run in real time and would be flaky on shared runners, so run `extension/run_tests.sh` yourself before a release.

To publish a release, push a tag named `v` followed by the version, for example `v0.1.0`. CI then also runs `tools/package_release.sh`, which zips `addons/felucca/` with the four libraries, the licences and `SOURCE.md`, and publishes the zip as a GitHub release. `SOURCE.md` says where the source of those libraries is, as the GPL requires next to the binaries, and also becomes the release's notes. You can run the script yourself once all four libraries are built.

## Demos

Open `demo/picker.tscn` (the main scene, on the web too) to pick a demo: the main one, the pool, the knock or the 3D scene. Press P or Escape to go back.

When you export:

- With the "Selected scenes" export mode, add `*.gdextension` to the include filter, or the extension is left out.
- With the PWA option on, the service worker keeps serving the old build after a re-export until you unregister it.

## Rules of thumb

- Never modify Felucca's sources in place. Change them only through `patches/` (a `git format-patch` series on `63f88ec`), and put each change behind an `#ifdef` unless it is upstream code.
- Never look up a Felucca parameter by its bare label: labels repeat across pages (`"WAVE"` finds the LFO's wave before ANALOG's oscillator). Use ids and page-qualified names instead. The typed API is generated by `extension/codegen/gen_api.py`; put readable names in `extension/codegen/names.json`, keyed by id and the label the parameter must have.
- Don't allocate, lock or log anywhere on the audio path (anything reached from a playback's mix or the clock effect).
- Publish anything that must take effect for several instances on the same mix step under `AudioServer.lock()`.
- Remember that the firmware's state is global: each pool instance needs its own compiled copy of the core.
- Follow Felucca's code style: C99 (gnu99), dense comments that say why, and `-ffp-contract=off`, as in its host build.
