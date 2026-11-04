#!/bin/sh
# Run the live Alien Alloys -> Personal Armour strategy path.
# The run starts from the private natural-victory save and does not build the app.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
victory_save=$root/build/e2e_victory/app/sav0.sav
out=$root/build/e2e_strategy_manufacture
app=$out/app
staff_flow=$dir/e2e_flow_strategy_manufacture_research.txt
r85_probe_flow=$dir/e2e_flow_strategy_manufacture_research85_probe.txt
r85_template=$dir/e2e_flow_strategy_manufacture_research85.txt
r94_probe_flow=$dir/e2e_flow_strategy_manufacture_research94_probe.txt
r94_template=$dir/e2e_flow_strategy_manufacture_research94.txt
manuf_probe_flow=$dir/e2e_flow_strategy_manufacture_probe.txt
manuf_commit_template=$dir/e2e_flow_strategy_manufacture_commit.txt
manuf_load_template=$dir/e2e_flow_strategy_manufacture_load.txt
gear_probe_flow=$dir/e2e_flow_strategy_manufacture_gear_probe.txt
gear_template=$dir/e2e_flow_strategy_manufacture_gear.txt
gear_load_flow=$dir/e2e_flow_strategy_manufacture_gear_load.txt
staff_log=$out/staff.log
r85_probe_log=$out/research85_probe.log
r85_log=$out/research85.log
r94_probe_log=$out/research94_probe.log
r94_log=$out/research94.log
manuf_probe_log=$out/manuf_probe.log
manuf_commit_log=$out/manuf_commit.log
manuf_load_log=$out/manuf_load.log
gear_probe_log=$out/gear_probe.log
gear_log=$out/gear.log
gear_load_log=$out/gear_load.log

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

# Keep every save, generated flow, log, and picture in this private tree.
rm -f "$app"/sav*.sav "$app"/strategy_manufacture_*.ppm \
    "$out"/staff.log "$out"/research85_probe.log "$out"/research85.log \
    "$out"/research94_probe.log "$out"/research94.log "$out"/manuf_probe.log \
    "$out"/manuf_commit.log "$out"/manuf_load.log "$out"/gear_probe.log \
    "$out"/gear.log "$out"/gear_load.log \
    "$out"/e2e_flow_strategy_manufacture_research85.generated.txt \
    "$out"/e2e_flow_strategy_manufacture_research94.generated.txt \
    "$out"/e2e_flow_strategy_manufacture_commit.generated.txt \
    "$out"/e2e_flow_strategy_manufacture_load.generated.txt \
    "$out"/e2e_flow_strategy_manufacture_gear.generated.txt
cp "$victory_save" "$app/sav0.sav"

run_flow() {
    flow_path=$1
    log_path=$2
    if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow_path")") >"$log_path" 2>&1; then
        echo "RUN FAILED (see $log_path)"
        tail -180 "$log_path"
        exit 1
    fi
}

run_flow "$staff_flow" "$staff_log"
if ! python3 "$dir/check_strategy_manufacture.py" staff "$staff_log" "$app/sav1.sav"; then
    echo "STAFF CHECK FAILED (see $staff_log)"
    tail -180 "$staff_log"
    exit 1
fi

run_flow "$r85_probe_flow" "$r85_probe_log"
if ! python3 "$dir/check_strategy_manufacture.py" research-probe 85 "$staff_log" \
    "$r85_probe_log" "$out/e2e_flow_strategy_manufacture_research85.generated.txt" "$r85_template"; then
    echo "TOPIC 85 PROBE FAILED (see $r85_probe_log)"
    tail -180 "$r85_probe_log"
    exit 1
fi

run_flow "$out/e2e_flow_strategy_manufacture_research85.generated.txt" "$r85_log"
if ! python3 "$dir/check_strategy_manufacture.py" research-complete "$r85_log" "$app/sav2.sav" 85; then
    echo "TOPIC 85 CHECK FAILED (see $r85_log)"
    tail -180 "$r85_log"
    exit 1
fi

run_flow "$r94_probe_flow" "$r94_probe_log"
if ! python3 "$dir/check_strategy_manufacture.py" research-probe 94 "$r85_log" \
    "$r94_probe_log" "$out/e2e_flow_strategy_manufacture_research94.generated.txt" "$r94_template"; then
    echo "TOPIC 94 PROBE FAILED (see $r94_probe_log)"
    tail -180 "$r94_probe_log"
    exit 1
fi

run_flow "$out/e2e_flow_strategy_manufacture_research94.generated.txt" "$r94_log"
if ! python3 "$dir/check_strategy_manufacture.py" research-complete "$r94_log" "$app/sav3.sav" 94; then
    echo "TOPIC 94 CHECK FAILED (see $r94_log)"
    tail -180 "$r94_log"
    exit 1
fi

run_flow "$manuf_probe_flow" "$manuf_probe_log"
if ! python3 "$dir/check_strategy_manufacture.py" manuf-probe "$manuf_probe_log" \
    "$out/e2e_flow_strategy_manufacture_commit.generated.txt" "$manuf_commit_template"; then
    echo "PRODUCT PROBE FAILED (see $manuf_probe_log)"
    tail -180 "$manuf_probe_log"
    exit 1
fi

run_flow "$out/e2e_flow_strategy_manufacture_commit.generated.txt" "$manuf_commit_log"
if ! python3 "$dir/check_strategy_manufacture.py" manuf-commit "$manuf_probe_log" "$manuf_commit_log" "$app/sav0.sav" \
    "$out/e2e_flow_strategy_manufacture_load.generated.txt" "$manuf_load_template"; then
    echo "MANUFACTURING CHECK FAILED (see $manuf_commit_log)"
    tail -180 "$manuf_commit_log"
    exit 1
fi

run_flow "$out/e2e_flow_strategy_manufacture_load.generated.txt" "$manuf_load_log"
if ! python3 "$dir/check_strategy_manufacture.py" manuf-output "$manuf_probe_log" "$manuf_commit_log" "$manuf_load_log" "$app/sav4.sav"; then
    echo "MANUFACTURING OUTPUT CHECK FAILED (see $manuf_load_log)"
    tail -180 "$manuf_load_log"
    exit 1
fi

run_flow "$gear_probe_flow" "$gear_probe_log"
if ! python3 "$dir/check_strategy_manufacture.py" gear-probe "$gear_probe_log" \
    "$out/e2e_flow_strategy_manufacture_gear.generated.txt" "$gear_template"; then
    echo "SOLDIER GEAR PROBE FAILED (see $gear_probe_log)"
    tail -180 "$gear_probe_log"
    exit 1
fi

run_flow "$out/e2e_flow_strategy_manufacture_gear.generated.txt" "$gear_log"
if ! python3 "$dir/check_strategy_manufacture.py" gear-equip "$gear_log" "$app/sav5.sav"; then
    echo "SOLDIER GEAR CHECK FAILED (see $gear_log)"
    tail -180 "$gear_log"
    exit 1
fi

run_flow "$gear_load_flow" "$gear_load_log"
if ! python3 "$dir/check_strategy_manufacture.py" gear-load "$gear_log" "$gear_load_log"; then
    echo "SOLDIER GEAR LOAD CHECK FAILED (see $gear_load_log)"
    tail -180 "$gear_load_log"
    exit 1
fi

python3 "$dir/check_campaign_ppm.py" "$app" \
    strategy_manufacture_research85_started.ppm strategy_manufacture_research94_started.ppm \
    strategy_manufacture_probe.ppm strategy_manufacture_allocated.ppm \
    strategy_manufacture_after_load.ppm strategy_manufacture_after_work.ppm \
    strategy_manufacture_gear_nav_probe.ppm strategy_manufacture_gear_nav_ready.ppm \
    strategy_manufacture_gear_soldier_list.ppm strategy_manufacture_gear_soldier_selected.ppm \
    strategy_manufacture_gear_mode_armed.ppm strategy_manufacture_gear_equipped.ppm \
    strategy_manufacture_gear_saved.ppm strategy_manufacture_gear_after_load.ppm

echo "staff log: $staff_log"
echo "research85 log: $r85_log"
echo "research94 log: $r94_log"
echo "manufacturing probe log: $manuf_probe_log"
echo "manufacturing commit log: $manuf_commit_log"
echo "manufacturing output log: $manuf_load_log"
echo "soldier gear log: $gear_log"
echo "soldier gear load log: $gear_load_log"
