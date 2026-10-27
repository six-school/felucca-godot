#!/bin/sh
# What the web build needs (docs/DESIGN.md §9, M7): Emscripten 4.0.20, the version Godot 4.7's release templates are
# built with (godotengine/build-containers, branch 4.7, Dockerfile.web), at its emsdk tag's commit; and Godot 4.7.2's
# export templates (the release's .tpz, the SHA-512 below as its SHA512-SUMS.txt gives it), of which only the threaded
# web templates with GDExtension support (web_dlink_*.zip) are kept, where Godot's template manager puts them.
#   tools/setup_web.sh        (after tools/setup_godot.sh; into third_party/emsdk, gitignored, and
#                              $XDG_DATA_HOME/godot/export_templates/4.7.2.stable; the .tpz, 1.3 GB, is deleted after)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
T=$HERE/third_party
EM=4.0.20
EMSDK=e4fe26ef59168ff44f4c23c466e497bf60b3411e   # emsdk's tag 4.0.20
GODOT=4.7.2
TPZ=Godot_v$GODOT-stable_export_templates.tpz
SHA=ca4d71c4d7b81dfc15d1a98baa07534aa95b03fdda78a0075b06672e1648d2e5f40980c9adc28d23e1b92e732ee7bf3461997aa804af74ec2fcd7a93ccb84079
TPL=${XDG_DATA_HOME:-$HOME/.local/share}/godot/export_templates/$GODOT.stable
if [ ! -d "$T/emsdk/.git" ]; then
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$EM" https://github.com/emscripten-core/emsdk.git "$T/emsdk"
fi
[ "$(git -C "$T/emsdk" rev-parse HEAD)" = "$EMSDK" ] || { echo "emsdk is not at $EMSDK"; exit 1; }
if [ ! -x "$T/emsdk/upstream/emscripten/emcc" ]; then
    "$T/emsdk/emsdk" install "$EM" > /dev/null
    "$T/emsdk/emsdk" activate "$EM" > /dev/null
fi
if [ ! -f "$TPL/web_dlink_debug.zip" ] || [ ! -f "$TPL/web_dlink_release.zip" ]; then
    mkdir -p "$T/godot/dl" "$TPL"
    curl -sSfL -o "$T/godot/dl/$TPZ" "https://github.com/godotengine/godot/releases/download/$GODOT-stable/$TPZ"
    echo "$SHA  $T/godot/dl/$TPZ" | sha512sum -c - > /dev/null
    unzip -q -o -j "$T/godot/dl/$TPZ" templates/version.txt templates/web_dlink_debug.zip templates/web_dlink_release.zip \
        -d "$TPL"
    rm -f "$T/godot/dl/$TPZ"
fi
echo "emsdk: $("$T/emsdk/upstream/emscripten/emcc" --version | head -n 1); templates $(cat "$TPL/version.txt") in $TPL: $(cd "$TPL" && ls web_dlink_*.zip | tr '\n' ' ')"
