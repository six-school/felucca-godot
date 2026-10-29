#!/bin/sh
# A release: the addon as a Godot project takes it, addons/felucca/ in a zip (the .gdextension, the autoload's scene,
# the four libraries: Linux and web, debug and release), with the licences and SOURCE.md, which says where the source
# of these libraries is. The GPL asks for those directions next to the binaries; SOURCE.md is the release's notes too.
#   tools/package_release.sh VERSION    (after the four builds, tools/setup_felucca.sh and tools/setup_godot.sh)
#   -> build/release/felucca-VERSION.zip, build/release/SOURCE.md
# REPO_URL (env): the repository the release comes from (default: origin's URL)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
V=${1:?usage: tools/package_release.sh VERSION}
REPO_URL=${REPO_URL:-$(git -C "$HERE" remote get-url origin | sed 's/\.git$//')}
COMMIT=$(git -C "$HERE" rev-parse HEAD)
FEL=$(sed -n 's/^BASE=\([0-9a-f]*\).*/\1/p' "$HERE/tools/setup_felucca.sh")    # (the pins, as the setup scripts
CPP=$(sed -n 's/^CPP=\([0-9a-f]*\).*/\1/p' "$HERE/tools/setup_godot.sh")       # have them: one place each)
ADDON=$HERE/demo/addons/felucca
LIBS="linux.template_debug.x86_64.so linux.template_release.x86_64.so web.template_debug.wasm32.wasm
web.template_release.wasm32.wasm"
for lib in $LIBS; do
    [ -f "$ADDON/bin/libfelucca.$lib" ] || { echo "missing: $ADDON/bin/libfelucca.$lib"; exit 1; }
done
[ -f "$HERE/third_party/felucca/LICENSE" ] || { echo "no Felucca tree: run tools/setup_felucca.sh"; exit 1; }
[ -f "$HERE/third_party/godot-cpp/LICENSE.md" ] || { echo "no godot-cpp: run tools/setup_godot.sh"; exit 1; }

OUT=$HERE/build/release
STAGE=$OUT/stage/addons/felucca
rm -rf "$OUT"
mkdir -p "$STAGE/bin" "$STAGE/licenses/Felucca" "$STAGE/licenses/godot-cpp"
cp "$ADDON/felucca.gdextension" "$ADDON/felucca.gdextension.uid" "$ADDON/felucca.tscn" "$STAGE/"
for lib in $LIBS; do
    cp "$ADDON/bin/libfelucca.$lib" "$STAGE/bin/"
done
cp "$HERE/LICENSE" "$STAGE/LICENSE"
cp "$HERE/third_party/felucca/LICENSE" "$HERE"/third_party/felucca/LICENSES/* "$STAGE/licenses/Felucca/"
cp "$HERE/third_party/godot-cpp/LICENSE.md" "$STAGE/licenses/godot-cpp/"
cat > "$OUT/SOURCE.md" <<EOF
# Source

felucca-godot $V is free software under the GNU General Public License, version 3 (\`LICENSE\`). Its libraries in \`bin/\` are built from:

- felucca-godot at commit $COMMIT: $REPO_URL/tree/$COMMIT (the release's source archives hold the same files). Its README and \`docs/DEVELOPING.md\` say how to build it.
- Felucca at commit $FEL, with its engines submodule, from https://github.com/hugelton/Felucca, with felucca-godot's \`patches/\` applied on top; felucca-godot's \`tools/setup_felucca.sh\` fetches it and applies them. Felucca is GPL-3.0, with some parts under their own licences (\`licenses/Felucca/\`).
- godot-cpp at commit $CPP (10.0.0-stable), from https://github.com/godotengine/godot-cpp, under the MIT licence (\`licenses/godot-cpp/\`).
EOF
cp "$OUT/SOURCE.md" "$STAGE/SOURCE.md"
(cd "$OUT/stage" && python3 -m zipfile -c "../felucca-$V.zip" addons)
rm -rf "$OUT/stage"
echo "release: $OUT/felucca-$V.zip ($(du -h "$OUT/felucca-$V.zip" | cut -f1)), $OUT/SOURCE.md"
