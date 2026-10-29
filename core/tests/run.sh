#!/bin/sh
# The core's checks: the core built as the pool (core/build.sh); parameters, projects, CPU and the pool's copies as C
# tests, and the 2-copy link test (a global left out of the renames must be caught); step timing, the delay's glide,
# locate, the cut and the scheduler on test copies (core/tests/test_hooks.c: the board file with white-box accessors,
# renamed as the pool); the heavy song as a project file (demo/tests/heavy.felucca, for m7_bench's measures).
#   core/tests/run.sh          (env FELUCCA: the Felucca tree, OUT: the build directory, CC: the compiler)
HERE=$(cd "$(dirname "$0")" && pwd)
CORE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$CORE/.." && pwd)
F=${FELUCCA:-$ROOT/third_party/felucca}
OUT=${OUT:-$ROOT/build/core}
CC=${CC:-clang}
FIX=$ROOT/demo/tests/project.json
export FELUCCA="$F" OUT CC
set -e
"$CORE/build.sh"
# An exported Felucca project as the core takes it: its store object's bytes (the .json is the extension's
# to read, with Godot's JSON: FeluccaProject::parse_json; here only what the tests need, and that the file is whole)
python3 - "$FIX" "$OUT/fixture.bin" <<'PY'
import base64, json, sys, zlib
f = json.load(open(sys.argv[1]))
o = f["object"]
b = base64.b64decode(o["data"], validate=True)
assert f["format"] == "felucca-project" and len(b) == o["size"] and zlib.crc32(b) == o["crc"], sys.argv[1] + ": damaged"
open(sys.argv[2], "wb").write(b)
PY
FIXB=$OUT/fixture.bin
V=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' "$F/firmware/src/felucca.c" | head -n 1)
CF="-O2 -ffp-contract=off -std=gnu99 -w -I$F -I$F/build/gen -I$F/firmware/src -I$CORE"
W="-O2 -std=gnu99 -Wall -Wextra -Werror -I$CORE"
# shellcheck disable=SC2086
{
    "$CC" $CF "-DFELUCCA_VERSION=\"$V\"" -o "$OUT/test_params" "$HERE/test_params.c" -lm &
    "$CC" $CF "-DFELUCCA_VERSION=\"$V\"" -o "$OUT/test_project" "$HERE/test_project.c" -lm &
    "$CC" $CF "-DFELUCCA_VERSION=\"$V\"" -o "$OUT/test_cpu" "$HERE/test_cpu.c" -lm &
    "$CC" $W -o "$OUT/test_pool" "$HERE/test_pool.c" "$OUT/libfelucca_core.a" -lm &
    "$CC" $W -I"$HERE" -o "$OUT/test_heavy" "$HERE/test_heavy.c" "$OUT/libfelucca_core.a" -lm &
    n=0
    while [ $n -lt 8 ]; do                              # the test copies
        "$CC" $CF -fPIC "-DFELUCCA_VERSION=\"$V\"" "-DFEL_COPY=$n" -include "$OUT/renames_$n.h" -I"$HERE" \
            -c -o "$OUT/test_copy_$n.o" "$HERE/test_hooks.c" &
        n=$((n + 1))
    done
    wait
    n=0
    while [ $n -lt 8 ]; do python3 "$CORE/renames.py" check $n "$OUT/test_copy_$n.o"; n=$((n + 1)); done
    T="$OUT/test_copy_0.o $OUT/test_copy_1.o $OUT/test_copy_2.o $OUT/test_copy_3.o $OUT/test_copy_4.o"
    T="$T $OUT/test_copy_5.o $OUT/test_copy_6.o $OUT/test_copy_7.o"
    "$CC" $W -I"$HERE" -o "$OUT/test_timing" "$HERE/test_timing.c" "$OUT/test_copy_0.o" -lm
    "$CC" $W -I"$HERE" -o "$OUT/test_delay" "$HERE/test_delay.c" $T -lm
    "$CC" $W -I"$HERE" -o "$OUT/test_locate" "$HERE/test_locate.c" $T -lm
    "$CC" $W -I"$HERE" -o "$OUT/test_cut" "$HERE/test_cut.c" $T -lm
    "$CC" $W -I"$HERE" -o "$OUT/test_sched" "$HERE/test_sched.c" $T -lm
}
set +e
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }
run "parameters: CUT and ARP RATE from code" "$OUT/test_params"
run "projects: a round trip, a test export" "$OUT/test_project" "$FIXB"
run "CPU of the heavy song" "$OUT/test_cpu"
run "the pool's copies isolated, in lock step" "$OUT/test_pool" "$FIXB"
run "the heavy song as a project file (m7_bench's measures)" "$OUT/test_heavy" "$OUT/heavy.bin"
# ... as the web app exports it, for the demo's measures (demo/tests/heavy.felucca): rewritten only when it changes
python3 - "$OUT/heavy.bin" "$ROOT/demo/tests/heavy.felucca" "FELUCCA $V" <<'PY' || fail=1
import base64, json, sys, zlib
b = open(sys.argv[1], "rb").read()
f = {"format": "felucca-project", "version": 1, "firmware": sys.argv[3],
     "object": {"size": len(b), "crc": zlib.crc32(b), "data": base64.b64encode(b).decode()}}
try:
    same = json.load(open(sys.argv[2])) == f
except (OSError, ValueError):
    same = False
if not same:
    with open(sys.argv[2], "w") as out:
        json.dump(f, out, indent=1)
        out.write("\n")
    print("  wrote " + sys.argv[2])
PY
run "step timing on the conductor's timeline, tempo tweened (int, float), paused" "$OUT/test_timing"
run "no clicks in the delay when the tempo moves it" "$OUT/test_delay"
run "locate: an instance that joins = one that played since the origin" "$OUT/test_locate"
run "cut: a reused copy carries no tail" "$OUT/test_cut" "$FIXB"
run "the scheduler: steps worked out ahead, the knock, targets and late policies" "$OUT/test_sched"

# the 2-copy link test: renames from a list that misses `panel` (a tentative definition), copies 0 and 1. The check
# must name it in both; the linker refuses the pair (a duplicate) by default, and with -fcommon merges the two panels
# without a word, which only the check sees
echo "== the 2-copy link test: a global missing from the renames is caught"
L=$OUT/linktest
mkdir -p "$L"
grep -vx panel "$OUT/renames.txt" > "$L/renames.txt"
for n in 0 1; do
    python3 "$CORE/renames.py" header "$n" "$L/renames.txt" > "$L/renames_$n.h"
    # shellcheck disable=SC2086
    "$CC" $CF -fPIC "-DFELUCCA_VERSION=\"$V\"" "-DFEL_COPY=$n" -include "$L/renames_$n.h" -c -o "$L/core_$n.o" "$CORE/board.c" &
    # shellcheck disable=SC2086
    "$CC" $CF -fPIC -fcommon "-DFELUCCA_VERSION=\"$V\"" "-DFEL_COPY=$n" -include "$L/renames_$n.h" -c -o "$L/common_$n.o" \
        "$CORE/board.c" &
done
wait
lt() { if "$@"; then echo "  ok   $what"; else echo "  FAIL $what"; fail=1; fi; }
what="the full list: copies 0 and 1 link and run"; lt "$CC" -o "$L/full" "$HERE/link_main.c" -I"$CORE" "$OUT/core_0.o" "$OUT/core_1.o" -lm
what="  ... and run"; lt "$L/full"
for n in 0 1; do
    what="panel left out: the check names it in copy $n"
    lt sh -c "! python3 '$CORE/renames.py' check $n '$L/core_$n.o' 2>'$L/check_$n.txt' && grep -q 'panel (B)' '$L/check_$n.txt'"
    what="panel left out, -fcommon: the check names it in copy $n"
    lt sh -c "! python3 '$CORE/renames.py' check $n '$L/common_$n.o' 2>'$L/common_$n.txt' && grep -q 'panel (C)' '$L/common_$n.txt'"
done
what="panel left out: the linker refuses the pair (multiple definition)"
lt sh -c "! '$CC' -o '$L/dup' '$HERE/link_main.c' -I'$CORE' '$L/core_0.o' '$L/core_1.o' -lm 2>'$L/link.txt' && grep -q panel '$L/link.txt'"
what="panel left out, -fcommon: the linker takes the pair silently (the hazard the check is for)"
lt "$CC" -o "$L/merged" "$HERE/link_main.c" -I"$CORE" "$L/common_0.o" "$L/common_1.o" -lm

[ $fail = 0 ] && echo "core: all passed" || echo "core: FAILED"
exit $fail
