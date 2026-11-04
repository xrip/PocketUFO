#!/bin/sh
# Transfer one raw New Game ammo item between two player-built bases.
# Usage: sh tools/e2e_strategy_newgame_transfer.sh OUTPUT_DIR
# A relative output path starts at the repo root and must not already exist.
# No old output or save is removed or reused.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")

if [ "$#" -ne 1 ]; then
    echo "USAGE: sh tools/e2e_strategy_newgame_transfer.sh OUTPUT_DIR"
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

stores_clock_flow=$out/stores-clock-00.txt
probe_flow=$out/e2e_flow_strategy_newgame_transfer_probe.txt
commit_flow=$out/e2e_flow_strategy_newgame_transfer_commit.txt
arrival_clock_flow=$out/transfer_arrival_clock.txt
final_flow=$out/e2e_flow_strategy_newgame_transfer_final.txt

cat > "$probe_flow" <<'FLOW'
load 1
state transfer_probe_loaded
strategystate transfer_probe_loaded
click 180 266
tick 2
click 220 15
tick 2
strategystate transfer_source_selected
click 180 274
tick 20
strategystate transfer_open
press 220 43
tick 1
release
tick 1
strategystate transfer_destination_selected
FLOW

cat > "$final_flow" <<'FLOW'
load 1
state final_loaded
strategystate final_base1
grid 1
click 180 266
tick 2
click 220 15
tick 2
click 201 11
tick 2
strategystate final_base0
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

run_check() {
    if ! python3 "$dir/check_strategy_newgame_transfer.py" "$@"; then
        echo "STATE CHECK FAILED (artifacts in $out)"
        exit 1
    fi
}

run_flow "$dir/e2e_flow_strategy_newgame_transfer.txt" "$out/setup.log"
if [ ! -s "$app/sav0.sav" ]; then
    echo "SAVE MISSING: pending Stores state slot 0 was not written"
    tail -160 "$out/setup.log"
    exit 1
fi
run_check setup "$out/setup.log" "$app/sav0.sav" "$stores_clock_flow"

stores_first_log=
stores_last_log=
stage=0
while [ "$stage" -lt 6 ]; do
    stage_name=$(printf '%02d' "$stage")
    stores_clock_flow=$out/stores-clock-$stage_name.txt
    stores_clock_log=$out/stores-clock-$stage_name.log

    if [ "$stage" -eq 0 ]; then
        previous_store_log=$out/setup.log
    else
        previous_store_log=$stores_last_log
        if [ -z "$previous_store_log" ]; then
            echo "STORES CHECKPOINT MISSING before stage $stage"
            exit 1
        fi
        if ! python3 "$dir/check_strategy_newgame_transfer.py" stores-next \
            "$previous_store_log" "$stores_clock_flow"; then
            echo "STORES CLOCK CONTINUE FAILED (see $previous_store_log)"
            tail -160 "$previous_store_log"
            exit 1
        fi
        if [ ! -s "$stores_clock_flow" ]; then
            stores_last_log=$previous_store_log
            break
        fi
    fi

    run_flow "$stores_clock_flow" "$stores_clock_log"
    if [ ! -s "$app/sav1.sav" ]; then
        echo "SAVE MISSING: Stores clock stage $stage did not write slot 1"
        tail -160 "$stores_clock_log"
        exit 1
    fi
    if ! python3 "$dir/check_strategy_newgame_transfer.py" stores-stage \
        "$previous_store_log" "$stores_clock_log" "$stores_clock_flow" \
        "$app/sav1.sav"; then
        echo "STORES CLOCK STAGE $stage FAILED (see $stores_clock_log)"
        tail -160 "$stores_clock_log"
        exit 1
    fi

    if [ "$stage" -eq 0 ]; then
        stores_first_log=$stores_clock_log
    fi
    stores_last_log=$stores_clock_log
    stage=$((stage + 1))
done

if [ "$stage" -ge 6 ]; then
    final_store_flow=$out/stores-clock-06.txt
    if ! python3 "$dir/check_strategy_newgame_transfer.py" stores-next \
        "$stores_last_log" "$final_store_flow"; then
        echo "STORES STAGE LIMIT: Stores did not finish in six measured stages"
        tail -160 "$stores_last_log"
        exit 1
    fi
    if [ -s "$final_store_flow" ]; then
        echo "STORES STAGE LIMIT: remaining build days were measured after stage six"
        tail -160 "$stores_last_log"
        exit 1
    fi
fi
if [ -z "$stores_first_log" ] || [ -z "$stores_last_log" ] || [ ! -s "$app/sav1.sav" ]; then
    echo "STORES CHECKPOINT MISSING: no finished Stores save was retained"
    exit 1
fi
run_check stores "$out/setup.log" "$stores_first_log" "$stores_last_log" "$app/sav1.sav"

run_flow "$probe_flow" "$out/transfer_probe.log"
run_check probe "$out/setup.log" "$out/transfer_probe.log" "$commit_flow"

run_flow "$commit_flow" "$out/transfer_commit.log"
if [ ! -s "$app/sav0.sav" ]; then
    echo "SAVE MISSING: pending transfer state slot 0 was not written"
    tail -160 "$out/transfer_commit.log"
    exit 1
fi
run_check commit "$stores_last_log" "$out/transfer_probe.log" "$out/transfer_commit.log" "$app/sav0.sav"

run_check arrival-flow "$out/transfer_commit.log" "$arrival_clock_flow"
run_flow "$arrival_clock_flow" "$out/transfer_arrival_clock.log"
if [ ! -s "$app/sav1.sav" ]; then
    echo "SAVE MISSING: transfer arrival clock checkpoint slot 1 was not written"
    tail -160 "$out/transfer_arrival_clock.log"
    exit 1
fi
run_check arrival "$out/transfer_commit.log" "$out/transfer_arrival_clock.log" "$app/sav1.sav" "$arrival_clock_flow"

run_flow "$final_flow" "$out/final_load.log"
run_check final "$out/setup.log" "$out/transfer_probe.log" "$out/transfer_arrival_clock.log" \
    "$out/final_load.log" "$final_flow"

echo "New Game / base / Stores log: $out/setup.log"
echo "last Stores build clock log: $stores_last_log"
echo "live transfer probe log: $out/transfer_probe.log"
echo "transfer commit log: $out/transfer_commit.log"
echo "24-hour transfer arrival clock log: $out/transfer_arrival_clock.log"
echo "same-process flag41 acknowledgement log: $out/transfer_arrival_clock.log"
echo "final fresh-load log: $out/final_load.log"
echo "run artifacts: $out"
