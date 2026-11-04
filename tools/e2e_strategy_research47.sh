#!/bin/sh
# Run raw New Game topic 47 through Research UI, same-process status input, clock, and fresh-process loads.
# Usage: sh tools/e2e_strategy_research47.sh OUTPUT_DIR
# OUTPUT_DIR must be new; this runner never removes or reuses it.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
checker=$dir/check_strategy_research47.py

if [ "$#" -ne 1 ]; then
    echo "USAGE: sh tools/e2e_strategy_research47.sh OUTPUT_DIR"
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

run_flow() {
    flow_path=$1
    log_path=$2
    if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow_path")") >"$log_path" 2>&1; then
        echo "RUN FAILED (see $log_path)"
        tail -160 "$log_path"
        exit 1
    fi
}

probe_log=$out/research_probe.log
run_flow "$dir/e2e_flow_strategy_research47_probe.txt" "$probe_log"
start_flow=$out/research_start.flow.txt
if ! python3 "$checker" probe "$probe_log" "$dir/e2e_flow_strategy_research47.txt" \
    "$start_flow" "$root/main.dat" "$root/lang.dat"; then
    echo "RESEARCH PROBE FAILED (see $probe_log)"
    tail -160 "$probe_log"
    exit 1
fi

start_log=$out/research_start.log
run_flow "$start_flow" "$start_log"
if [ ! -s "$app/sav0.sav" ]; then
    echo "SAVE MISSING: ongoing Research slot 0 was not written"
    tail -160 "$start_log"
    exit 1
fi
if ! python3 "$checker" start "$start_log" "$app/sav0.sav" "$root/main.dat" "$root/lang.dat"; then
    echo "RESEARCH START FAILED (see $start_log)"
    tail -160 "$start_log"
    exit 1
fi

first_clock_log=
last_clock_log=
stage=0
while [ "$stage" -lt 6 ]; do
    stage_name=$(printf '%02d' "$stage")
    clock_flow=$out/clock-$stage_name.txt
    clock_log=$out/clock-$stage_name.log

    if [ "$stage" -eq 0 ]; then
        if ! python3 "$checker" clock-start "$start_log" "$clock_flow" "$root/main.dat" "$root/lang.dat"; then
            echo "CLOCK START FLOW FAILED (see $start_log)"
            tail -160 "$start_log"
            exit 1
        fi
        previous_log=$start_log
    else
        previous_log=$last_clock_log
        if [ -z "$previous_log" ]; then
            echo "CLOCK CHECKPOINT MISSING before stage $stage"
            exit 1
        fi
        if ! python3 "$checker" clock-next "$previous_log" "$clock_flow"; then
            echo "CLOCK CONTINUE CHECK FAILED (see $previous_log)"
            tail -160 "$previous_log"
            exit 1
        fi
        if [ ! -s "$clock_flow" ]; then
            last_clock_log=$previous_log
            break
        fi
    fi

    run_flow "$clock_flow" "$clock_log"
    if [ ! -s "$app/sav1.sav" ]; then
        echo "SAVE MISSING: clock stage $stage did not write slot 1"
        tail -160 "$clock_log"
        exit 1
    fi
    if ! python3 "$checker" clock-stage "$previous_log" "$clock_log" \
        "$clock_flow" "$app/sav1.sav" "$root/main.dat" "$root/lang.dat"; then
        echo "CLOCK STAGE $stage FAILED (see $clock_log)"
        tail -160 "$clock_log"
        exit 1
    fi

    if [ "$stage" -eq 0 ]; then
        first_clock_log=$clock_log
    fi
    last_clock_log=$clock_log
    stage=$((stage + 1))
done

if [ "$stage" -ge 6 ]; then
    final_check_flow=$out/clock-06.txt
    if ! python3 "$checker" clock-next "$last_clock_log" "$final_check_flow"; then
        echo "CLOCK STAGE LIMIT: topic 47 did not finish within 24 saved stages"
        tail -160 "$last_clock_log"
        exit 1
    fi
    if [ -s "$final_check_flow" ]; then
        echo "CLOCK STAGE LIMIT: topic 47 still needs more Research work"
        tail -160 "$last_clock_log"
        exit 1
    fi
fi
if [ -z "$first_clock_log" ] || [ -z "$last_clock_log" ] || [ ! -s "$app/sav1.sav" ]; then
    echo "CLOCK SAVE MISSING: no completed Research checkpoint was retained"
    exit 1
fi

final_log=$out/research_final_load.log
run_flow "$dir/e2e_flow_strategy_research47_final.txt" "$final_log"
if ! python3 "$checker" complete "$start_log" "$first_clock_log" "$last_clock_log" \
    "$final_log" "$app/sav1.sav" "$root/main.dat" "$root/lang.dat"; then
    echo "RESEARCH COMPLETION FAILED (see $final_log)"
    tail -160 "$start_log"
    tail -160 "$first_clock_log"
    tail -160 "$last_clock_log"
    tail -160 "$final_log"
    exit 1
fi
if ! python3 "$checker" final "$final_log" "$dir/e2e_flow_strategy_research47_final.txt" \
    "$root/main.dat" "$root/lang.dat"; then
    echo "FINAL RESEARCH UI FAILED (see $final_log)"
    tail -160 "$final_log"
    exit 1
fi

echo "New Game probe: $probe_log"
echo "Research start: $start_log"
echo "First fresh-load clock stage: $first_clock_log"
echo "Final clock stage: $last_clock_log"
echo "Completed fresh-load Research UI: $final_log"
echo "All retained files: $out"
