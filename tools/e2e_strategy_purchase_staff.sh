#!/bin/sh
# Run a paid New Game Engineer and Rifle delivery without a victory-save input.
# Usage: sh tools/e2e_strategy_purchase_staff.sh OUTPUT_DIR
# A relative output path starts at the repo root. The path must be new.
# Existing output is never removed or reused.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")

if [ "$#" -ne 1 ]; then
    echo "USAGE: sh tools/e2e_strategy_purchase_staff.sh OUTPUT_DIR"
    exit 2
fi
out_arg=$1
case "$out_arg" in
    /*) out=$out_arg ;;
    *) out=$root/$out_arg ;;
esac

if [ ! -f "$source_exe" ]; then
    echo "BUILD MISSING: $source_exe"
    echo "Build the pocketufo Release target, then rerun this script."
    exit 1
fi
if [ ! -f "$root/main.dat" ] || [ ! -f "$root/lang.dat" ]; then
    echo "DATA MISSING: main.dat and lang.dat must sit in the project root."
    exit 1
fi
if ! command -v cygpath >/dev/null 2>&1; then
    echo "TOOL MISSING: cygpath is needed to pass flow paths to the Win64 executable."
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "TOOL MISSING: python3 is needed for the state checks."
    exit 1
fi
for dll in libopenmpt.dll openmpt-mpg123.dll openmpt-vorbis.dll openmpt-zlib.dll openmpt-ogg.dll; do
    if [ ! -f "$source_dir/$dll" ]; then
        echo "RUNTIME DLL MISSING: $source_dir/$dll"
        exit 1
    fi
done
if [ ! -d "$source_dir/libopenmpt-licenses" ]; then
    echo "LICENSE DIRECTORY MISSING: $source_dir/libopenmpt-licenses"
    exit 1
fi

out_parent=$(dirname "$out")
mkdir -p "$out_parent"
if ! mkdir "$out"; then
    echo "OUTPUT EXISTS: choose a new output directory: $out"
    exit 1
fi
app=$out/app
mkdir "$app"

cp "$source_exe" "$app/pocketufo.exe"
cp "$root/main.dat" "$app/main.dat"
cp "$root/lang.dat" "$app/lang.dat"
for dll in libopenmpt.dll openmpt-mpg123.dll openmpt-vorbis.dll openmpt-zlib.dll openmpt-ogg.dll; do
    cp "$source_dir/$dll" "$app/$dll"
done
mkdir "$app/libopenmpt-licenses"
cp -R "$source_dir/libopenmpt-licenses/." "$app/libopenmpt-licenses/"

# Load the pending order in a new process, then use the real timer path.
delivery_flow=$out/e2e_flow_strategy_purchase_staff_delivery.txt
cat > "$delivery_flow" <<'FLOW'
load 0
state pending_load
strategystate pending_load
click 60 308
tick 25
tick 2
strategystate delivered_before_ack
lines
click 29 186
tick 2
lines
strategystate delivered
campaign cargo delivered
save 1
state delivered_save
FLOW

# A second new process checks that delivered staff and stock persist.
final_flow=$out/e2e_flow_strategy_purchase_staff_final_load.txt
cat > "$final_flow" <<'FLOW'
load 1
state final_load
strategystate final_load
campaign cargo final_load
FLOW

run_flow() {
    flow_path=$1
    log_path=$2
    if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow_path")") >"$log_path" 2>&1; then
        echo "RUN FAILED (see $log_path)"
        tail -160 "$log_path"
        exit 1
    fi
}

run_flow "$dir/e2e_flow_strategy_purchase_staff.txt" "$out/new_game_purchase.log"
if [ ! -s "$app/sav0.sav" ]; then
    echo "SAVE MISSING: pending order slot 0 was not written"
    tail -160 "$out/new_game_purchase.log"
    exit 1
fi

run_flow "$delivery_flow" "$out/load_delivery.log"
if [ ! -s "$app/sav1.sav" ]; then
    echo "SAVE MISSING: delivered state slot 1 was not written"
    tail -160 "$out/load_delivery.log"
    exit 1
fi

run_flow "$final_flow" "$out/final_load.log"
if ! python3 "$dir/check_strategy_purchase_staff.py" \
    "$out/new_game_purchase.log" "$out/load_delivery.log" "$out/final_load.log"; then
    echo "STATE CHECK FAILED (see logs in $out)"
    tail -160 "$out/new_game_purchase.log"
    tail -160 "$out/load_delivery.log"
    tail -160 "$out/final_load.log"
    exit 1
fi

echo "new game and purchase log: $out/new_game_purchase.log"
echo "fresh-load delivery log: $out/load_delivery.log"
echo "delivered-state fresh-load log: $out/final_load.log"
echo "run artifacts: $out"
