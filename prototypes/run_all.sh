#!/bin/sh
# Builds and runs every prototype against a Felucca tree made by tools/setup_felucca.sh; the expected results are in
# docs/DESIGN.md ("Evidence"). Linux/ELF (objcopy) for the two-copy tests.
#   prototypes/run_all.sh [FELUCCA_DIR]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
F=${1:-$HERE/../third_party/felucca}
OUT=${OUT:-$HERE/../build/prototypes}
mkdir -p "$OUT"
V=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' "$F/firmware/src/felucca.c" | head -n 1)
CF="-O2 -ffp-contract=off -std=gnu99 -w -I$HERE -I$F -I$F/build/gen -I$F/firmware/src"
# shellcheck disable=SC2086
cc() { clang $CF "-DFELUCCA_VERSION=\"$V\"" "$@" -lm; }
cd "$OUT"
echo "== direct parameters, projects (test_api.c)";            cc -o test_api "$HERE/test_api.c" && ./test_api
echo "== the user's project export (project_load.c)";        cc -o project_load "$HERE/project_load.c" && ./project_load "$HERE/../tests/fixtures/Felucca-project-D-2026-10-24.json" project-D.wav
echo "== CPU, heavy song (heavy.c)";                           cc -o heavy "$HERE/heavy.c" && ./heavy
echo "== parameter tables (dump.c -> params.json)";            cc -O1 -o dump "$HERE/dump.c" && ./dump > params.json && python3 -I "$HERE/gen_preview.py" params.json | head -n 3
echo "== scheduler and events: knock, then door (sched.c)";    cc -o sched "$HERE/sched.c" && ./sched
echo "== internal-clock division drift (drift.c)";             cc -o drift "$HERE/drift.c" && for b in 120 128 140; do ./drift $b; done
echo "== two copies in one binary (multi.c, lockstep.c)"
for n in 0 1; do
    clang $CF "-DFELUCCA_VERSION=\"$V\"" -DFEL_COPY=$n -c -o copy$n.o "$HERE/fel_copy.c"
    ld -r -o core$n.o copy$n.o
    objcopy --keep-global-symbol=fel_core_$n --keep-global-symbol=fel_heavy_$n --keep-global-symbol=fel_pos_$n core$n.o
done
clang -O2 -I"$HERE" -o multi "$HERE/multi.c" core0.o core1.o -lm && ./multi
clang -O2 -I"$HERE" -o lockstep "$HERE/lockstep.c" core0.o core1.o -lm && ./lockstep
echo "== tempo tweens: A internal, B pulses, C host clock (tempo.c)"
for m in 1 2 3; do cc -DMODE=$m -o tempo$m "$HERE/tempo.c" && ./tempo$m; done
echo "== delay clicks (clicks.c, wet.py): B and C, constant and tweened; then with FELUCCA_DELAY_GLIDE"
for g in "" "-DFELUCCA_DELAY_GLIDE"; do
    for m in 2 3; do for tw in 0 1; do
        cc $g -DMODE=$m -DTWEEN=$tw -o clicks$m$tw "$HERE/clicks.c"
        ./clicks$m$tw 127 c$m${tw}_on.raw > /dev/null && ./clicks$m$tw 0 c$m${tw}_off.raw > /dev/null
    done; done
    echo "-- ${g:-no glide}"; python3 -I "$HERE/wet.py" c20 c21 c30 c31
done
