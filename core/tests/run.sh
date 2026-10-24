#!/bin/sh
# M1's checks (docs/DESIGN.md §14): the core built as the pool (core/build.sh), then T1 T2 T3 T4 T8 T11 and the project
# JSON reader as C tests, and the 2-copy link test: a global left out of the renames must be caught.
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
V=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' "$F/firmware/src/felucca.c" | head -n 1)
CF="-O2 -ffp-contract=off -std=gnu99 -w -I$F -I$F/build/gen -I$F/firmware/src -I$CORE"
W="-O2 -std=gnu99 -Wall -Wextra -Werror -I$CORE"
# shellcheck disable=SC2086
{
    "$CC" $W -o "$OUT/test_json" "$HERE/test_json.c" "$CORE/project_json.c"
    "$CC" $CF "-DFELUCCA_VERSION=\"$V\"" -o "$OUT/test_params" "$HERE/test_params.c" -lm &
    "$CC" $CF "-DFELUCCA_VERSION=\"$V\"" -o "$OUT/test_project" "$HERE/test_project.c" "$CORE/project_json.c" -lm &
    "$CC" $CF "-DFELUCCA_VERSION=\"$V\"" -o "$OUT/test_cpu" "$HERE/test_cpu.c" -lm &
    "$CC" $W -o "$OUT/test_pool" "$HERE/test_pool.c" "$OUT/libfelucca_core.a" -lm &
    wait
}
set +e
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }
run "project JSON: the user's export, damaged copies" "$OUT/test_json" "$FIX"
run "T1 T2: CUT and ARP RATE from code" "$OUT/test_params"
run "T3 T11: projects, the user's export" "$OUT/test_project" "$FIX"
run "T4: CPU of the heavy song" "$OUT/test_cpu"
run "T8: the pool's copies isolated, in lock step" "$OUT/test_pool" "$FIX"

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
