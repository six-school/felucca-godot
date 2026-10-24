#!/bin/sh
# The extension's Godot-side checks (demo/tests/m3_test.tscn): built (SCons), the demo imported, the test scene run
# headless on the Dummy audio driver at 44.1 kHz (frame for frame) and at 48 kHz (the playback resamples).
#   extension/run_tests.sh [GODOT]     (default: third_party/godot/Godot_v4.7.2-stable_linux.x86_64)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
G=${1:-$ROOT/third_party/godot/Godot_v4.7.2-stable_linux.x86_64}
cd "$ROOT"
third_party/.felucca-venv/bin/python -m SCons -j"$(nproc)" use_llvm=no > build/scons.log 2>&1 || { tail -30 build/scons.log; exit 1; }
# twice: the first scan registers the extension, whose importer the second one runs
"$G" --headless --path demo --import > build/import.log 2>&1 || true
"$G" --headless --path demo --import > build/import.log 2>&1
fail=0
for rate in 44100 48000; do
    echo "== demo/tests/m3_test.tscn at $rate Hz"
    if [ $rate = 44100 ]; then rm -f demo/override.cfg; else printf '[audio]\ndriver/mix_rate=%s\n' $rate > demo/override.cfg; fi
    timeout 180 "$G" --headless --audio-driver Dummy --path demo res://tests/m3_test.tscn > build/m3_test_$rate.log 2>&1 || fail=1
    grep -E "^(ok|FAIL|mix rate|m3_test)" build/m3_test_$rate.log || true
    grep -E "^(ERROR|SCRIPT ERROR)" build/m3_test_$rate.log | grep -v "FeluccaProject: \"format\"\|not a Felucca project" || true
done
rm -f demo/override.cfg
[ $fail = 0 ] && echo "extension: all passed" || echo "extension: FAILED"
exit $fail
