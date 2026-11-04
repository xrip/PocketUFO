#!/bin/sh
# Run natural base-defense defeat from the frozen entry flow using an existing Release build.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
out=$root/build/e2e_base_defense_defeat/out
run=$out/$(date -u +%Y%m%dT%H%M%SZ)-$$
app=$run/app
prefix=$dir/e2e_flow_base_defense_entry.txt
continuation=$dir/e2e_flow_base_defense_defeat.txt
flow=$run/e2e_flow_base_defense_defeat.txt
log=$run/base_defense_defeat.log

if [ ! -f "$source_exe" ]; then
    echo "BUILD MISSING: $source_exe"
    echo "Build the pocketufo Release target, then rerun this script."
    exit 1
fi
if [ ! -f "$root/main.dat" ] || [ ! -f "$root/lang.dat" ]; then
    echo "DATA MISSING: main.dat and lang.dat must sit in the project root."
    exit 1
fi
if [ ! -f "$prefix" ] || [ ! -f "$continuation" ]; then
    echo "FLOW MISSING: the frozen entry flow or defeat continuation is absent."
    exit 1
fi
if ! command -v cygpath >/dev/null 2>&1; then
    echo "TOOL MISSING: cygpath is needed to pass the flow path to the Win64 executable."
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "TOOL MISSING: python3 is needed for the log and PPM checks."
    exit 1
fi

# Each run gets a new private folder. Old logs, frames, and private saves stay intact.
mkdir -p "$app"
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
cat "$prefix" "$continuation" > "$flow"

if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow")") >"$log" 2>&1; then
    echo "RUN FAILED (see $log)"
    tail -160 "$log"
    exit 1
fi

if ! python3 -I "$dir/check_base_defense_defeat.py" "$log" "$flow" "$app"; then
    echo "STATE FAILED (see $log)"
    tail -160 "$log"
    exit 1
fi
if ! python3 -I "$dir/e2e_report.py" "$app"; then
    echo "PICTURE FAILED (see $log)"
    tail -160 "$log"
    exit 1
fi
echo "base-defense defeat run: $run"
