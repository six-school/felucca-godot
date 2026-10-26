#!/bin/sh
# The core's checks (docs/DESIGN.md §14): the core built as the pool (core/build.sh); M1: T1 T2 T3 T4 T8 T11 as C
# tests, and the 2-copy link test (a global left out of the renames must be caught); M2: T5, T6 and locate, M5: the cut,
# M6: the scheduler, on test copies (core/tests/test_hooks.c: the board file with white-box accessors, renamed as the
# pool).
#   core/tests/run.sh          (env FELUCCA: the Felucca tree, OUT: the build directory, CC: the compiler)
HERE=$(cd "$(dirname "$0")" && pwd)
CORE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$CORE/.." && pwd)
F=${FELUCCA:-$ROOT/third_party/felucca}
OUT=${OUT:-$ROOT/build/core}
CC=${CC:-clang}
FIX=$ROOT/tests/fixtures/Felucca-project-D-2026-10-24.json
export FELUCCA="$F" OUT CC
set -e
"$CORE/build.sh"
# the user's export as the core takes it: its store object's bytes (the .json is the extension's to read, with Godot's
# JSON: FeluccaProject::parse_json; here only what the tests need, and that the file is whole)
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
run "T1 T2: CUT and ARP RATE from code" "$OUT/test_params"
run "T3 T11: projects, the user's export" "$OUT/test_project" "$FIXB"
run "T4: CPU of the heavy song" "$OUT/test_cpu"
run "T8: the pool's copies isolated, in lock step" "$OUT/test_pool" "$FIXB"
run "T5: step timing on the conductor's timeline, tempo tweened (int, float), paused" "$OUT/test_timing"
run "T6: no clicks in the delay when the tempo moves it" "$OUT/test_delay"
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
