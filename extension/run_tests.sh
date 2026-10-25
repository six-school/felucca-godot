#!/bin/sh
# The extension's Godot-side checks: built (SCons), the demo imported, demo/tests/m3_test.tscn run headless on the Dummy
# audio driver at 44.1 kHz (frame for frame) and at 48 kHz (the playback resamples), m5_test (the pool) at both, then
# m4_test (the typed API), project_test (the project file) and the typo scripts.
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
for rate in 44100 48000; do
    echo "== demo/tests/m5_test.tscn at $rate Hz: the pool"
    if [ $rate = 44100 ]; then rm -f demo/override.cfg; else printf '[audio]\ndriver/mix_rate=%s\n' $rate > demo/override.cfg; fi
    timeout 180 "$G" --headless --audio-driver Dummy --path demo res://tests/m5_test.tscn > build/m5_test_$rate.log 2>&1 || fail=1
    grep -E "^(ok|FAIL|mix rate|m5_test)" build/m5_test_$rate.log || true
    grep -E "^(ERROR|SCRIPT ERROR)" build/m5_test_$rate.log || true
done
rm -f demo/override.cfg
echo "== demo/tests/m4_test.tscn"
timeout 180 "$G" --headless --audio-driver Dummy --path demo res://tests/m4_test.tscn > build/m4_test.log 2>&1 || fail=1
grep -E "^(ok|FAIL|m4_test)" build/m4_test.log || true
grep -E "^(ERROR|SCRIPT ERROR)" build/m4_test.log || true
echo "== demo/tests/project_test.tscn: the project file, read with Godot's JSON"
timeout 60 "$G" --headless --path demo res://tests/project_test.tscn > build/project_test.log 2>&1 || fail=1
grep -E "^(ok|FAIL|project_test)" build/project_test.log || true
grep -E "^(ERROR|SCRIPT ERROR)" build/project_test.log || true
# M4 done when a typo is a parse error: the typed API spelled right parses, each script with one typo does not
echo "== demo/tests/typo: the typed API checked by GDScript's analyzer"
for f in demo/tests/typo/*.gd; do
    n=$(basename "$f")
    if timeout 60 "$G" --headless --path demo --check-only --script "res://tests/typo/$n" > "build/typo_$n.log" 2>&1; then r=0; else r=1; fi
    e=$(grep -m1 -E "Parse Error|SCRIPT ERROR" "build/typo_$n.log" | sed 's/^ *//' | cut -c1-110)
    case $n in
        ok.gd) [ $r = 0 ] && [ -z "$e" ] && echo "ok   $n parses" || { echo "FAIL $n: $e"; fail=1; } ;;
        *) [ $r != 0 ] && [ -n "$e" ] && echo "ok   $n: $e" || { echo "FAIL $n parsed"; fail=1; } ;;
    esac
done
[ $fail = 0 ] && echo "extension: all passed" || echo "extension: FAILED"
exit $fail
