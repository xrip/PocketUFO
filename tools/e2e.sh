#!/bin/sh
# The whole-path gate: run the real executable through the menu -> new game -> placement -> geoscape ->
# save -> load flow, take a picture at every step, check each picture, and compare them against the
# committed references in tests/ref/e2e.
#
# Two runs because the load is entered the way the original enters it: a fresh process at the main menu,
# row 1, a slot row. The save the first run wrote is the file the second one loads, so the pair also
# proves the save/load round trip across processes.
#
# The window is never shown on this path (App_InitInstance's bHidden arm): the pictures are read off the
# logical surface, and a window that grabs the foreground on every run gets in the way of the person at
# the machine.
#
# Usage: sh tools/e2e.sh

set -e
dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
out=$root/build/e2e
refs=$root/tests/ref/e2e
exe=$root/build/src/pufo/Release/pocketufo.exe

if ! cmake --build "$root/build" --config Release --target pocketufo > /tmp/pufo_e2e_build.log 2>&1; then
    echo "BUILD FAILED (see /tmp/pufo_e2e_build.log)"
    grep -iE "error|warning" /tmp/pufo_e2e_build.log | head -20
    exit 1
fi

# The slot files are szAppDir-relative -- the exe's own directory, which is where App_BuildAssetPaths
# points szAppDir @ 0x1883b0 -- so that is where they are cleared, and the one the new-game flow wrote
# is what the load flow reads.
rm -f "$out"/*.ppm "$out"/*.png "$out"/sav*.sav "$(dirname "$exe")"/sav*.sav
mkdir -p "$out" "$refs"

for flow in e2e_flow_new.txt e2e_flow_load.txt; do
    if ! (cd "$out" && "$exe" --script "$(cygpath -w "$dir/$flow")"); then
        echo "RUN FAILED for $flow"
        exit 1
    fi
done

mv "$(dirname "$exe")"/sav*.sav "$out"/ 2>/dev/null || true

# Gate scaffolding: a --script run does NOTHING and still exits 0 while another pocketufo.exe owns the
# POCKET_UFO window -- App_InitInstance's single-instance check (the original's FindWindowW @ 0x9FD64)
# returns false and WinMain returns 0 before the script is read. Measured: one leftover hidden window from
# a killed run made six later runs write no pictures at all. Count the pictures against the flow files
# before believing the run.
want=$(grep -h '^[[:space:]]*dump ' "$dir"/e2e_flow_new.txt "$dir"/e2e_flow_load.txt | wc -l)
got=$(ls "$out"/*.ppm 2>/dev/null | wc -l)
if [ "$got" -lt "$want" ]; then
    echo "RUN FAILED: $got pictures, the flows ask for $want"
    echo "(a pocketufo.exe that is still running holds the single-instance window; close it -- e.g."
    echo " taskkill //F //IM pocketufo.exe -- and re-run)"
    exit 1
fi

for ppm in "$out"/*.ppm; do
    if ! ffmpeg -y -loglevel error -i "$ppm" "${ppm%.ppm}.png"; then
        echo "PPM -> PNG FAILED for $ppm"
        exit 1
    fi
done

python3 -I "$dir/e2e_report.py" "$out"

for png in "$out"/*.png; do
    cp "$png" "$refs/$(basename "$png")"
done

if git -C "$root" status --porcelain -- tests/ref/e2e | grep -q .; then
    echo "e2e pictures: CHANGED or NEW -- $(git -C "$root" status --porcelain -- tests/ref/e2e | tr '\n' ' ')"
    echo "(a new reference is expected when a step first gets its picture; commit the pictures you have looked at)"
    exit 1
fi
echo "e2e pictures: unchanged against the committed references ($(ls "$refs"/*.png | wc -l) pictures)"
