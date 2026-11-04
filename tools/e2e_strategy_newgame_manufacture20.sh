#!/bin/sh
# Run raw New Game topic 14 -> one Motion Scanner through real UI and clock input.
# Usage: sh tools/e2e_strategy_newgame_manufacture20.sh OUTPUT_DIR
# OUTPUT_DIR must be new; this runner never removes or reuses it.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
checker=$dir/check_strategy_newgame_manufacture20.py

if [ "$#" -ne 1 ]; then
    echo "USAGE: sh tools/e2e_strategy_newgame_manufacture20.sh OUTPUT_DIR"
    exit 2
fi
out_arg=$1
case "$out_arg" in
    /*) out=$out_arg ;;
    *) out=$root/$out_arg ;;
esac

if [ ! -f "$source_exe" ]; then
    echo "BUILD MISSING: $source_exe"
    echo "Build the pocketufo Release target after the parent passes its App gates, then rerun this script."
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

run_flow() {
    flow_path=$1
    log_path=$2
    if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow_path")") >"$log_path" 2>&1; then
        echo "RUN FAILED (see $log_path)"
        tail -180 "$log_path"
        exit 1
    fi
}

probe_log=$out/newgame_research_probe.log
run_flow "$dir/e2e_flow_strategy_newgame_manufacture20_probe.txt" "$probe_log"
research_start_flow=$out/research_start.flow.txt
if ! python3 "$checker" newgame-probe "$probe_log" \
    "$dir/e2e_flow_strategy_newgame_manufacture20_research_start.txt" \
    "$research_start_flow" "$root/main.dat" "$root/lang.dat"; then
    echo "NEW GAME / RESEARCH PROBE FAILED (see $probe_log)"
    tail -180 "$probe_log"
    exit 1
fi

research_start_log=$out/research_start.log
run_flow "$research_start_flow" "$research_start_log"
if [ ! -s "$app/sav0.sav" ]; then
    echo "SAVE MISSING: ongoing topic-14 Research save was not written"
    tail -180 "$research_start_log"
    exit 1
fi
if ! python3 "$checker" research-start "$research_start_log" "$app/sav0.sav" \
    "$root/main.dat" "$root/lang.dat"; then
    echo "RESEARCH START FAILED (see $research_start_log)"
    tail -180 "$research_start_log"
    exit 1
fi

first_clock_log=
last_clock_log=
stage=0
previous_log=$research_start_log
while [ "$stage" -lt 6 ]; do
    stage_name=$(printf '%02d' "$stage")
    clock_flow=$out/research-clock-$stage_name.flow.txt
    clock_log=$out/research-clock-$stage_name.log

    if [ "$stage" -eq 0 ]; then
        if ! python3 "$checker" clock-start "$research_start_log" "$clock_flow" \
            "$root/main.dat" "$root/lang.dat"; then
            echo "RESEARCH CLOCK START FAILED (see $research_start_log)"
            tail -180 "$research_start_log"
            exit 1
        fi
    else
        if ! python3 "$checker" clock-next "$previous_log" "$clock_flow" \
            "$root/main.dat" "$root/lang.dat"; then
            echo "RESEARCH CLOCK CONTINUE FAILED (see $previous_log)"
            tail -180 "$previous_log"
            exit 1
        fi
        if [ ! -s "$clock_flow" ]; then
            break
        fi
    fi

    run_flow "$clock_flow" "$clock_log"
    if [ ! -s "$app/sav1.sav" ]; then
        echo "SAVE MISSING: Research clock stage $stage did not write slot 1"
        tail -180 "$clock_log"
        exit 1
    fi
    if ! python3 "$checker" clock-stage "$previous_log" "$clock_log" \
        "$clock_flow" "$app/sav1.sav" "$root/main.dat" "$root/lang.dat"; then
        echo "RESEARCH CLOCK STAGE $stage FAILED (see $clock_log)"
        tail -180 "$clock_log"
        exit 1
    fi
    if [ "$stage" -eq 0 ]; then
        first_clock_log=$clock_log
    fi
    last_clock_log=$clock_log
    previous_log=$clock_log
    stage=$((stage + 1))
done
if [ "$stage" -ge 6 ] || [ -z "$first_clock_log" ] || [ -z "$last_clock_log" ] \
        || [ ! -s "$app/sav1.sav" ]; then
    echo "RESEARCH CLOCK LIMIT OR SAVE MISSING: topic 14 did not finish in bounded stages"
    exit 1
fi

manuf_load_log=$out/manufacture_map_loaded.log
run_flow "$dir/e2e_flow_strategy_newgame_manufacture20_manuf_load.txt" "$manuf_load_log"
manuf_open_flow=$out/manufacture_open_probe.flow.txt
if ! python3 "$checker" manuf-load "$last_clock_log" "$manuf_load_log" \
    "$manuf_open_flow" "$root/main.dat" "$root/lang.dat"; then
    echo "MANUFACTURE MAP PROBE FAILED (see $manuf_load_log)"
    tail -180 "$manuf_load_log"
    exit 1
fi

manuf_probe_log=$out/manufacture_probe.log
run_flow "$manuf_open_flow" "$manuf_probe_log"
manuf_order_flow=$out/manufacture_order.flow.txt
if ! python3 "$checker" manuf-probe "$manuf_probe_log" \
    "$dir/e2e_flow_strategy_newgame_manufacture20_manuf_order.txt" \
    "$manuf_order_flow" "$root/main.dat" "$root/lang.dat"; then
    echo "MANUFACTURE ROW PROBE FAILED (see $manuf_probe_log)"
    tail -180 "$manuf_probe_log"
    exit 1
fi

manuf_order_log=$out/manufacture_order.log
run_flow "$manuf_order_flow" "$manuf_order_log"
if [ ! -s "$app/sav2.sav" ]; then
    echo "SAVE MISSING: pending slot-20 manufacture order was not written"
    tail -180 "$manuf_order_log"
    exit 1
fi
if ! python3 "$checker" manuf-order "$manuf_order_log" "$app/sav2.sav"; then
    echo "MANUFACTURE ORDER FAILED (see $manuf_order_log)"
    tail -180 "$manuf_order_log"
    exit 1
fi

pending_log=$out/manufacture_pending_loaded.log
run_flow "$dir/e2e_flow_strategy_newgame_manufacture20_pending_probe.txt" "$pending_log"
manuf_clock_flow=$out/manufacture_clock.flow.txt
if ! python3 "$checker" pending-probe "$pending_log" "$manuf_order_log" "$manuf_clock_flow"; then
    echo "PENDING ORDER LOAD FAILED (see $pending_log)"
    tail -180 "$pending_log"
    exit 1
fi

manuf_clock_log=$out/manufacture_clock.log
run_flow "$manuf_clock_flow" "$manuf_clock_log"
if [ ! -s "$app/sav3.sav" ]; then
    echo "SAVE MISSING: completed slot-20 manufacture save was not written"
    tail -180 "$manuf_clock_log"
    exit 1
fi
if ! python3 "$checker" manuf-clock "$pending_log" "$manuf_clock_log" \
    "$manuf_clock_flow" "$app/sav3.sav" "$root/main.dat" "$root/lang.dat"; then
    echo "MANUFACTURE CLOCK FAILED (see $manuf_clock_log)"
    tail -180 "$manuf_clock_log"
    exit 1
fi

final_log=$out/manufacture_final_load.log
run_flow "$dir/e2e_flow_strategy_newgame_manufacture20_final_load.txt" "$final_log"
if ! python3 "$checker" manuf-final "$manuf_clock_log" "$final_log" "$app/sav3.sav" \
    "$root/main.dat" "$root/lang.dat"; then
    echo "MANUFACTURE SAVE/LOAD FAILED (see $final_log)"
    tail -180 "$manuf_clock_log"
    tail -180 "$final_log"
    exit 1
fi

echo "New Game and Research probe: $probe_log"
echo "Topic-14 Research start: $research_start_log"
echo "Research clock range: $first_clock_log -> $last_clock_log"
echo "Manufacture UI probe: $manuf_probe_log"
echo "Pending order and saved load: $manuf_order_log / $pending_log"
echo "Manufacture output and fresh load: $manuf_clock_log / $final_log"
echo "All retained files: $out"
