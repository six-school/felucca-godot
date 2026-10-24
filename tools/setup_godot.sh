#!/bin/sh
# What the extension builds and runs with (docs/DESIGN.md D4, M3): Godot 4.7.2's Linux binary (its release zip, the
# SHA-512 below as its SHA512-SUMS.txt gives it), godot-cpp at 10.0.0-stable, SCons in the venv setup_felucca.sh made.
#   tools/setup_godot.sh                (into third_party/godot, third_party/godot-cpp; both gitignored)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
T=$HERE/third_party
GODOT=4.7.2
ZIP=Godot_v$GODOT-stable_linux.x86_64.zip
SHA=9aa00f7a605200940bce3027a567b782f49bd8e940dd06ae9e987bd65aee1b1467edd56ed84fcdcbdd44354bf613bdbb4e5d2913e925850368e150c59ed54c65
CPP=507ed9d840c01a3c5b2a39af8bb4000bfac30bf5      # godot-cpp 10.0.0-stable (2026-09-15): API files 4.3 .. 4.7
if [ ! -x "$T/godot/Godot_v$GODOT-stable_linux.x86_64" ]; then
    mkdir -p "$T/godot/dl"
    curl -sSfL -o "$T/godot/dl/$ZIP" "https://github.com/godotengine/godot/releases/download/$GODOT-stable/$ZIP"
    echo "$SHA  $T/godot/dl/$ZIP" | sha512sum -c - > /dev/null
    unzip -q -o "$T/godot/dl/$ZIP" -d "$T/godot"
fi
if [ ! -d "$T/godot-cpp/.git" ]; then
    git clone -q --depth 1 --branch 10.0.0-stable https://github.com/godotengine/godot-cpp.git "$T/godot-cpp"
fi
[ "$(git -C "$T/godot-cpp" rev-parse HEAD)" = "$CPP" ] || { echo "godot-cpp is not at $CPP"; exit 1; }
"$T/.felucca-venv/bin/python" -m pip install -q "scons==4.9.1"   # (python -m: the venv's own scripts may name
                                                                  # another path if it was made elsewhere)
echo "godot: $("$T/godot/Godot_v$GODOT-stable_linux.x86_64" --version | tail -n 1); godot-cpp $(git -C "$T/godot-cpp" rev-parse --short HEAD); SCons $("$T/.felucca-venv/bin/python" -m SCons --version | sed -n 's/.*SCons: v\([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p')"
