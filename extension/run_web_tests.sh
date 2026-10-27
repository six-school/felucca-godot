#!/bin/sh
# The extension's checks in a browser (docs/DESIGN.md §9, M7): built for the web (SCons platform=web: tools/setup_web.sh
# first), the "Web tests" export served with COOP/COEP (tools/serve_web.py), each test run in headless Chromium
# (tools/web_run.mjs; demo/tests/web_tests.gd runs the one the page's ?scene= names): m3_test, m5_test, m5_scene_test
# and m6_test at the browser's 44.1 kHz and at 48 kHz (res://override.cfg in the export: audio/driver/mix_rate.web),
# then m4_test and project_test, with extension/run_tests.sh's counts of warnings and refusals. (The typo scripts are
# the editor's analyzer's: run_tests.sh only.)
#   extension/run_web_tests.sh [GODOT] [PORT]     (default port 8063)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
G=${1:-$ROOT/third_party/godot/Godot_v4.7.2-stable_linux.x86_64}
PORT=${2:-8063}
cd "$ROOT"
# the desktop build too: the editor's import and export load the extension
third_party/.felucca-venv/bin/python -m SCons -j"$(nproc)" use_llvm=no > build/scons.log 2>&1 || { tail -30 build/scons.log; exit 1; }
# (emsdk_env.sh is bash's)
bash -c '. third_party/emsdk/emsdk_env.sh > /dev/null 2>&1 &&
    third_party/.felucca-venv/bin/python -m SCons -j"$(nproc)" platform=web target=template_debug' > build/scons_web.log 2>&1 ||
    { tail -30 build/scons_web.log; exit 1; }
# twice: the first scan registers the extension, whose importer the second one runs
"$G" --headless --path demo --import > build/import.log 2>&1 || true
"$G" --headless --path demo --import > build/import.log 2>&1
fail=0
server=""
trap '[ -n "$server" ] && kill $server 2>/dev/null; rm -f demo/override.cfg' EXIT
REFUSED="^ERROR: (FeluccaWhen: |FeluccaAction\.|Refused: schedule\(\))"

# run NAME RATE [EXPECTED]: the test until its summary line, which must say 0 failed; any error but EXPECTED printed
run() {
    echo "== demo/tests/$1.tscn at $2 Hz in Chromium"
    node tools/web_run.mjs "http://127.0.0.1:$PORT/?scene=$1" --until "^$1: [0-9]+ failed" --timeout 300 \
        > "build/web_$1_$2.log" 2>&1 || true
    grep -E "^(ok|FAIL|mix rate|$1: |web_run: )" "build/web_$1_$2.log" || true
    grep -E "^(ERROR|SCRIPT ERROR|exception)" "build/web_$1_$2.log" | grep -v -E "${3:-^$}" || true
    grep -q "^$1: 0 failed" "build/web_$1_$2.log" || fail=1
}
# count LOG N WHAT PATTERN: N lines match
count() {
    n=$(grep -c -E "$4" "$1" || true)
    [ "$n" = "$2" ] && echo "ok   $3" || { echo "FAIL $3 ($n)"; fail=1; }
}

for rate in 44100 48000; do
    if [ $rate = 44100 ]; then rm -f demo/override.cfg; else printf '[audio]\ndriver/mix_rate.web=%s\n' $rate > demo/override.cfg; fi
    rm -rf build/web-tests
    mkdir -p build/web-tests
    "$G" --headless --path demo --export-debug "Web tests" ../build/web-tests/index.html > build/export_web_tests.log 2>&1 ||
        { tail -20 build/export_web_tests.log; exit 1; }
    python3 tools/serve_web.py build/web-tests "$PORT" > build/serve_web.log 2>&1 &
    server=$!
    sleep 1
    run m3_test $rate "FeluccaProject: \"format\"|not a Felucca project"
    run m5_test $rate
    L=build/web_m5_test_$rate.log
    count $L 1 "play() with pitch_scale 2: one warning" "^WARNING: .*pitch_scale has no effect on a Felucca source"
    count $L 1 "more sources in range than copies: one warning" "^WARNING: FeluccaConductor: more Felucca sources in range"
    run m5_scene_test $rate
    count build/web_m5_scene_test_$rate.log 1 "9 in range: one warning" "^WARNING: FeluccaConductor: more Felucca sources in range"
    run m6_test $rate "$REFUSED"
    L=build/web_m6_test_$rate.log
    count $L 13 "13 refusals, each an error" "$REFUSED"
    count $L 1 "one warning: more sources in range" "^WARNING: FeluccaConductor: more Felucca sources in range"
    count $L 1 "one warning: more pinned sources" "^WARNING: FeluccaConductor: more pinned Felucca sources"
    count $L 1 "one warning: on_hit on a part not DRUM" "^WARNING: Notes: on_hit is connected on part 1, which is not a DRUM part"
    if [ $rate = 44100 ]; then
        run m4_test $rate
        run project_test $rate
    fi
    kill $server
    server=""
done
[ $fail = 0 ] && echo "web: all passed" || echo "web: FAILED"
exit $fail
