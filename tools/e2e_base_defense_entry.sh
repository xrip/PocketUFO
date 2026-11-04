#!/bin/sh
# Run the real subtype-6 base-defense entry and first enemy turn against a Release build.
# This is a private early-flow proof; it does not test tactical defeat.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
out=$root/build/e2e_base_defense_entry
app=$out/app
flow=$dir/e2e_flow_base_defense_entry.txt
log=$out/base_defense_entry.log

if [ ! -f "$source_exe" ]; then
    echo "RUN FAILED: executable not found: $source_exe"
    exit 1
fi
if [ ! -f "$root/main.dat" ] || [ ! -f "$root/lang.dat" ]; then
    echo "DATA MISSING: main.dat and lang.dat must sit in the project root."
    exit 1
fi
if ! command -v cygpath >/dev/null 2>&1; then
    echo "TOOL MISSING: cygpath is needed to pass the flow path to the Win64 executable."
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "TOOL MISSING: python3 is needed for the log and image checks."
    exit 1
fi

mkdir -p "$out" "$app"
cp "$source_exe" "$app/pocketufo.exe"
cp "$root/main.dat" "$app/main.dat"
cp "$root/lang.dat" "$app/lang.dat"
for dll in libopenmpt.dll openmpt-mpg123.dll openmpt-vorbis.dll openmpt-zlib.dll openmpt-ogg.dll; do
    if [ ! -f "$source_dir/$dll" ]; then
        echo "RUNTIME DLL MISSING: $source_dir/$dll"
        exit 1
    fi
    cp "$source_dir/$dll" "$app/$dll"
done
if [ ! -d "$source_dir/libopenmpt-licenses" ]; then
    echo "LICENSE DIRECTORY MISSING: $source_dir/libopenmpt-licenses"
    exit 1
fi
mkdir -p "$app/libopenmpt-licenses"
cp -R "$source_dir/libopenmpt-licenses/." "$app/libopenmpt-licenses/"

# Only this runner's private saves, log, and pictures are cleared.
rm -f "$app"/sav*.sav "$log" \
    "$out"/base_defense_new_base.ppm \
    "$out"/base_defense_base_at.ppm \
    "$out"/base_defense_initial_loadout.ppm \
    "$out"/base_defense_deployed.ppm \
    "$out"/base_defense_after_first_enemyturn.ppm \
    "$out"/base_defense_type3_mission.ppm \
    "$out"/base_defense_after_confirm_key.ppm

if ! (cd "$out" && "$app/pocketufo.exe" --script "$(cygpath -w "$flow")") >"$log" 2>&1; then
    echo "RUN FAILED (see $log)"
    tail -160 "$log"
    exit 1
fi

python3 "$dir/check_e2e_base_defense_entry.py" "$log" "$out"
# This checks PPM size and visible content. It does not check visual parity.
python3 "$dir/e2e_report.py" "$out"
