#!/bin/sh
# Run the bounded natural-victory flow against an existing Release app build.
# This runner does not build and keeps saves in its own private app directory.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
out=$root/build/e2e_victory
app=$out/app
flow=$dir/e2e_flow_victory.txt
log=$out/victory.log

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
    echo "TOOL MISSING: python3 is needed for the log and cargo checks."
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

# Only this runner's private app and output files are cleared.
rm -f "$app"/sav*.sav "$out"/victory.log "$out"/campaign_victory_debrief.ppm \
    "$out"/campaign_return_card.ppm

if ! (cd "$out" && "$app/pocketufo.exe" --script "$(cygpath -w "$flow")") >"$log" 2>&1; then
    echo "RUN FAILED (see $log)"
    tail -160 "$log"
    exit 1
fi

python3 "$dir/check_e2e_victory.py" "$log" "$app/sav0.sav"
