#!/bin/sh
# Benchmarks (boot, CPU, latency, download size) from release builds: demo/tests/m7_bench in headless Chromium (the "Web
# tests" export, as extension/run_web_tests.sh serves and runs it) at the browser's 44.1 kHz and at 48 kHz, then on the
# desktop (Dummy driver) at both; then the "Web" export's files, as downloaded (each, and gzip -9: what a server
# compressing them sends).
#   extension/run_web_bench.sh [GODOT] [PORT]     (default port 8063)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
G=${1:-$ROOT/third_party/godot/Godot_v4.7.2-stable_linux.x86_64}
PORT=${2:-8063}
cd "$ROOT"
# the desktop build too: the editor's import and export load the extension
third_party/.felucca-venv/bin/python -m SCons -j"$(nproc)" use_llvm=no > build/scons.log 2>&1 || { tail -30 build/scons.log; exit 1; }
# (emsdk_env.sh is bash's)
bash -c '. third_party/emsdk/emsdk_env.sh > /dev/null 2>&1 &&
    third_party/.felucca-venv/bin/python -m SCons -j"$(nproc)" platform=web target=template_release' > build/scons_web.log 2>&1 ||
    { tail -30 build/scons_web.log; exit 1; }
# twice: the first scan registers the extension, whose importer the second one runs
"$G" --headless --path demo --import > build/import.log 2>&1 || true
"$G" --headless --path demo --import > build/import.log 2>&1
fail=0
server=""
trap '[ -n "$server" ] && kill $server 2>/dev/null; rm -f demo/override.cfg' EXIT
show() { grep -E "^(ok|FAIL|bench|m7_bench: |web_run: |ERROR|SCRIPT ERROR|exception)" "$1" || true; grep -q "^m7_bench: 0 failed" "$1" || fail=1; }

for rate in 44100 48000; do
    if [ $rate = 44100 ]; then rm -f demo/override.cfg; else printf '[audio]\ndriver/mix_rate.web=%s\n' $rate > demo/override.cfg; fi
    rm -rf build/web-bench
    mkdir -p build/web-bench
    "$G" --headless --path demo --export-release "Web tests" ../build/web-bench/index.html > build/export_web_bench.log 2>&1 ||
        { tail -20 build/export_web_bench.log; exit 1; }
    python3 tools/serve_web.py build/web-bench "$PORT" > build/serve_web.log 2>&1 &
    server=$!
    sleep 1
    echo "== demo/tests/m7_bench.tscn at $rate Hz in Chromium (release)"
    node tools/web_run.mjs "http://127.0.0.1:$PORT/?scene=m7_bench" --until "^m7_bench: [0-9]+ failed" --timeout 120 \
        > "build/web_m7_bench_$rate.log" 2>&1 || true
    show "build/web_m7_bench_$rate.log"
    kill $server
    server=""
done
for rate in 44100 48000; do
    echo "== demo/tests/m7_bench.tscn at $rate Hz on the desktop (Dummy driver)"
    if [ $rate = 44100 ]; then rm -f demo/override.cfg; else printf '[audio]\ndriver/mix_rate=%s\n' $rate > demo/override.cfg; fi
    timeout 120 "$G" --headless --audio-driver Dummy --path demo res://tests/m7_bench.tscn > "build/m7_bench_$rate.log" 2>&1 || true
    show "build/m7_bench_$rate.log"
done
rm -f demo/override.cfg

echo "== the download: the \"Web\" export (release)"
rm -rf build/web
mkdir -p build/web
"$G" --headless --path demo --export-release "Web" ../build/web/index.html > build/export_web.log 2>&1 ||
    { tail -20 build/export_web.log; exit 1; }
total=0
gz=0
for f in build/web/*; do
    a=$(wc -c < "$f")
    b=$(gzip -9 -c "$f" | wc -c)
    total=$((total + a))
    gz=$((gz + b))
    printf '%10d %10d  %s\n' "$a" "$b" "${f#build/web/}"
done
printf '%10d %10d  (bytes, gzip -9)\n' $total $gz
[ $fail = 0 ] && echo "bench: all ran" || echo "bench: FAILED"
exit $fail
