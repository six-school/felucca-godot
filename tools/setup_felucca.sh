#!/bin/sh
# Felucca as this project builds it: the pinned upstream commit, its engines submodule, our patch series
# (patches/), and the generated headers (build/gen: tables, fonts, icons, CC0 samples).
#   tools/setup_felucca.sh [DIR]      (default: third_party/felucca)
# Needs git and Python 3; Pillow and fontTools go into a venv next to the checkout.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
DIR=${1:-$HERE/third_party/felucca}
BASE=63f88ec18b71c6ed4371d03d6f04dd93f4193ad3      # hugelton/Felucca main after 1.6.1 (BPM-0 guard, #282)
if [ ! -d "$DIR/.git" ]; then
    git clone -q https://github.com/hugelton/Felucca.git "$DIR"
fi
cd "$DIR"
git checkout -q --detach "$BASE"
git submodule update -q --init --depth 1                 # firmware/engines (Felucca-Engines, MIT)
git -c user.name=setup -c user.email=setup@localhost am -q "$HERE"/patches/*.patch
VENV="$DIR/../.felucca-venv"
[ -x "$VENV/bin/python" ] || { python3 -m venv "$VENV"; "$VENV/bin/pip" install -q Pillow fonttools; }
"$VENV/bin/python" -c "import sys; sys.path.insert(0, 'tools'); import build; build.generate()" > /dev/null
echo "felucca: $DIR at $(git rev-parse --short HEAD) ($(git log --oneline "$BASE"..HEAD | wc -l | tr -d ' ') patches), build/gen ready"
