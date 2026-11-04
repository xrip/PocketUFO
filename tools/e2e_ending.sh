#!/bin/sh
# Run the already built Win64 app through the ending fixtures; this script does not build.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
exe=${1:-$root/build/src/pufo/Release/pocketufo.exe}
out=${2:-$root/build/e2e-ending}

if [ ! -f "$exe" ]; then
    echo "RUN FAILED: executable not found: $exe"
    exit 1
fi

mkdir -p "$out"

check_picture() {
    python3 -I - "$1" <<'PY'
import pathlib
import sys

name = sys.argv[1]
data = pathlib.Path(name).read_bytes()
header_end = data.find(b"\n", data.find(b"255\n"))
if not data.startswith(b"P6\n") or header_end < 0:
    raise SystemExit(f"PICTURE FAILED: invalid PPM: {name}")
pixels = data[header_end + 1:]
if len(pixels) != 240 * 320 * 3:
    raise SystemExit(f"PICTURE FAILED: wrong pixel size: {name}")
colors = {pixels[i:i + 3] for i in range(0, len(pixels), 3)}
visible = sum(pixels[i:i + 3] != b"\xff\xff\xff" for i in range(0, len(pixels), 3))
if len(colors) < 4 or visible < 100:
    raise SystemExit(f"PICTURE FAILED: ending is blank or white: {name}")
print(f"ending picture: {name}: {len(colors)} colors, {visible} non-white pixels")
PY
}

flow=$(cygpath -w "$dir/e2e_flow_ending.txt")
if ! (cd "$out" && "$exe" --script "$flow") > "$out/ending.log" 2>&1; then
    echo "RUN FAILED: ending flow"
    cat "$out/ending.log"
    exit 1
fi
cat "$out/ending.log"

log=$out/ending.log
if ! grep -Fq 'pocketufo: ending fixture end: ended 1, screen 18/18, rows 16, backdrop 1, tact live 0' "$log"; then
    echo "STATE FAILED: live last-base check did not start screen 18"
    exit 1
fi
if ! grep -Fq 'pocketufo: ending fixture tick: scroll 0 -> 1' "$log"; then
    echo "STATE FAILED: screen-18 tick did not advance the ending scroll"
    exit 1
fi
if ! grep -Fq 'pocketufo: ending state: screen 18/18, rows 16, backdrop 1, scroll 161,' "$log"; then
    echo "STATE FAILED: ending frame did not pass the white fade"
    exit 1
fi
if ! grep -Eq 'pocketufo: state ending_release: start screen 17, .*geo ui byte 0, tact flag 0,' "$log"; then
    echo "STATE FAILED: a pointer release did not return to startup screen 17"
    exit 1
fi
if ! grep -Fq 'pocketufo: new game:' "$log"; then
    echo "STATE FAILED: the startup new-game click did not run"
    exit 1
fi
if ! grep -Eq 'pocketufo: state ending_new_game: start screen 22, .*geo ui byte 1,' "$log"; then
    echo "STATE FAILED: the new-game click did not enter the live game"
    exit 1
fi
for picture in 20_ending.ppm 21_newgame.ppm; do
    if [ ! -s "$out/$picture" ]; then
        echo "PICTURE FAILED: $out/$picture was not written"
        exit 1
    fi
done
check_picture "$out/20_ending.ppm"
echo "ending fixture: end check, screen 18 draw/tick, input return, and new-game click passed"

flow=$(cygpath -w "$dir/e2e_flow_good_ending.txt")
if ! (cd "$out" && "$exe" --script "$flow") > "$out/good-ending.log" 2>&1; then
    echo "RUN FAILED: good-ending flow"
    cat "$out/good-ending.log"
    exit 1
fi
cat "$out/good-ending.log"

glog=$out/good-ending.log
if ! grep -Fq 'pocketufo: good-ending fixture: ended 1, screen 18/18, rows 10, backdrop 1, tact live 0,' "$glog"; then
    echo "STATE FAILED: live victory-tile scan did not start the good ending"
    exit 1
fi
if ! grep -Fq 'pocketufo: ending state: screen 18/18, rows 10, backdrop 1, scroll 161,' "$glog"; then
    echo "STATE FAILED: good-ending frame did not pass the white fade"
    exit 1
fi
if ! grep -Fq 'pocketufo: ending state: screen 17/17,' "$glog"; then
    echo "STATE FAILED: good-ending release did not return to startup"
    exit 1
fi
if [ ! -s "$out/22_good_ending.ppm" ]; then
    echo "PICTURE FAILED: $out/22_good_ending.ppm was not written"
    exit 1
fi
check_picture "$out/22_good_ending.ppm"
echo "good-ending fixture: live victory-tile scan, screen 18, draw, and release passed"

flow=$(cygpath -w "$dir/e2e_flow_bankruptcy.txt")
if ! (cd "$out" && "$exe" --script "$flow") > "$out/bankruptcy.log" 2>&1; then
    echo "RUN FAILED: bankruptcy flow"
    cat "$out/bankruptcy.log"
    exit 1
fi
cat "$out/bankruptcy.log"

blog=$out/bankruptcy.log
if ! grep -Fq 'pocketufo: bankruptcy fixture report: open 1, funds -13000000, screen 19, pulse 1, seconds 2678394->2678400, tick 5, month 1->2, paused 0, action-fail 0' "$blog"; then
    echo "STATE FAILED: one unpaused month-boundary pulse did not open the report below the funds limit"
    exit 1
fi
if ! grep -Fq 'pocketufo: ending state: screen 18/18, rows 16, backdrop 1,' "$blog"; then
    echo "STATE FAILED: closing screen 19 did not open the bankruptcy ending"
    exit 1
fi
if ! grep -Fq 'pocketufo: ending state: screen 18/18, rows 16, backdrop 1, scroll 160,' "$blog"; then
    echo "STATE FAILED: bankruptcy frame did not pass the white fade"
    exit 1
fi
if ! grep -Fq 'pocketufo: ending state: screen 17/17,' "$blog"; then
    echo "STATE FAILED: the bankruptcy ending release did not return to startup"
    exit 1
fi
for picture in 30_month_report.ppm 31_bankruptcy_end.ppm; do
    if [ ! -s "$out/$picture" ]; then
        echo "PICTURE FAILED: $out/$picture was not written"
        exit 1
    fi
done
check_picture "$out/31_bankruptcy_end.ppm"
echo "bankruptcy fixture: live month report, case-19 close, screen 18, and input return passed"
