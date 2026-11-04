#!/bin/sh
# Run a real two-base construction, transfer, fresh-load, and arrival flow.
# The natural-victory save is an explicit prerequisite; this runner never builds.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
victory_save=$root/build/e2e_victory/app/sav0.sav
out=$root/build/e2e_strategy_transfer
app=$out/app
newbase_flow=$dir/e2e_flow_strategy_transfer_newbase.txt
setup_flow=$dir/e2e_flow_strategy_transfer_setup.txt
probe_open_flow=$dir/e2e_flow_strategy_transfer_probe_open.txt
probe_template=$dir/e2e_flow_strategy_transfer_probe.txt
commit_template=$dir/e2e_flow_strategy_transfer_commit.txt
load_flow=$dir/e2e_flow_strategy_transfer_load.txt
setup_log=$out/setup.log
newbase_log=$out/newbase.log
probe_open_log=$out/probe_open.log
probe_log=$out/probe.log
commit_log=$out/commit.log
load_log=$out/load.log

if [ ! -f "$source_exe" ]; then
    echo "RUN FAILED: executable not found: $source_exe"
    exit 1
fi
if [ ! -f "$root/main.dat" ] || [ ! -f "$root/lang.dat" ]; then
    echo "DATA MISSING: main.dat and lang.dat must sit in the project root."
    exit 1
fi
if [ ! -s "$victory_save" ]; then
    echo "VICTORY SAVE MISSING: run tools/e2e_victory.sh first: $victory_save"
    exit 1
fi
if ! command -v cygpath >/dev/null 2>&1; then
    echo "TOOL MISSING: cygpath is needed to pass flow paths to the Win64 executable."
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

# These are all private to this flow. Preserve no state from a prior run.
rm -f "$app"/sav*.sav "$app"/strategy_transfer_*.ppm \
    "$out"/setup.log "$out"/newbase.log "$out"/probe.log "$out"/commit.log "$out"/load.log \
    "$out"/probe_open.log "$out"/e2e_flow_strategy_transfer_probe.generated.txt \
    "$out"/e2e_flow_strategy_transfer_commit.generated.txt
cp "$victory_save" "$app/sav0.sav"

run_flow() {
    flow_path=$1
    log_path=$2
    if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow_path")") >"$log_path" 2>&1; then
        echo "RUN FAILED (see $log_path)"
        tail -160 "$log_path"
        exit 1
    fi
}

run_flow "$newbase_flow" "$newbase_log"
if ! python3 "$dir/check_strategy_transfer.py" newbase "$newbase_log" "$app/sav1.sav"; then
    echo "NEW-BASE CHECK FAILED (see $newbase_log)"
    tail -160 "$newbase_log"
    exit 1
fi

run_flow "$setup_flow" "$setup_log"
if ! python3 "$dir/check_strategy_transfer.py" setup "$setup_log" "$app/sav1.sav"; then
    echo "SETUP CHECK FAILED (see $setup_log)"
    tail -160 "$setup_log"
    exit 1
fi

run_flow "$probe_open_flow" "$probe_open_log"
if ! python3 "$dir/check_strategy_transfer.py" scroll-probe "$probe_open_log" \
    "$out/e2e_flow_strategy_transfer_probe.generated.txt" "$probe_template"; then
    echo "TRANSFER SCROLL PROBE FAILED (see $probe_open_log)"
    tail -160 "$probe_open_log"
    exit 1
fi

run_flow "$out/e2e_flow_strategy_transfer_probe.generated.txt" "$probe_log"
if ! python3 "$dir/check_strategy_transfer.py" probe "$probe_log" \
    "$out/e2e_flow_strategy_transfer_commit.generated.txt" "$commit_template"; then
    echo "TRANSFER PROBE FAILED (see $probe_log)"
    tail -160 "$probe_log"
    exit 1
fi

run_flow "$out/e2e_flow_strategy_transfer_commit.generated.txt" "$commit_log"
if ! python3 "$dir/check_strategy_transfer.py" commit "$probe_log" "$commit_log" "$app/sav0.sav"; then
    echo "TRANSFER COMMIT CHECK FAILED (see $commit_log)"
    tail -160 "$commit_log"
    exit 1
fi

run_flow "$load_flow" "$load_log"
if ! python3 "$dir/check_strategy_transfer.py" load "$commit_log" "$load_log"; then
    echo "FRESH LOAD / ARRIVAL CHECK FAILED (see $load_log)"
    tail -160 "$load_log"
    exit 1
fi

python3 "$dir/check_campaign_ppm.py" "$app" \
    strategy_transfer_new_base_region.ppm strategy_transfer_second_base.ppm \
    strategy_transfer_access_lift_pending.ppm \
    strategy_transfer_stores_selected.ppm strategy_transfer_stores_placed.ppm \
    strategy_transfer_base0_stores_placed.ppm \
    strategy_transfer_setup_ready.ppm strategy_transfer_probe_open.ppm \
    strategy_transfer_probe_before_scroll.ppm strategy_transfer_probe_destination.ppm \
    strategy_transfer_probe_scrolled.ppm \
    strategy_transfer_open.ppm strategy_transfer_destination.ppm \
    strategy_transfer_item_queued.ppm \
    strategy_transfer_committed.ppm strategy_transfer_after_load.ppm \
    strategy_transfer_arrived.ppm

echo "transfer setup log: $setup_log"
echo "new-base placement log: $newbase_log"
echo "transfer open probe log: $probe_open_log"
echo "transfer probe log: $probe_log"
echo "transfer commit log: $commit_log"
echo "fresh-load and arrival log: $load_log"
