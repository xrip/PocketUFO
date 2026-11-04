#!/bin/sh
# Run the natural normal-air-to-ground play flow or one bounded move probe from Git Bash.
# PowerShell: & 'C:\Program Files\Git\bin\bash.exe' tools/e2e_normal_ground_play.sh [play|moveprobe]

set -eu

case "${MSYSTEM:-}" in
    MINGW*|UCRT*|MSYS*) ;;
    *)
        echo "SHELL MISSING: run this script with Git Bash, not WSL bash."
        exit 1
        ;;
esac

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Debug/pocketufo.exe
source_dir=$(dirname "$source_exe")
arrival_checker=$dir/check_normal_air.py
combat_checker=$dir/check_normal_air_combat.py
ground_checker=$dir/check_normal_ground.py
mode=${1:-play}
case "$mode" in
    play)
        out=$root/build/e2e_normal_ground_play
        flow=$dir/e2e_flow_normal_ground_play.txt
        play_checker=$dir/check_normal_ground_play.py
        log_name=normal_ground_play.log
        ;;
    moveprobe)
        out=$root/build/e2e_normal_ground_move_probe
        flow=$dir/e2e_flow_normal_ground_move_probe.txt
        play_checker=$dir/check_normal_ground_move_probe.py
        log_name=normal_ground_move_probe.log
        ;;
    *)
        echo "USAGE: $0 [play|moveprobe]"
        exit 1
        ;;
esac

if [ ! -f "$source_exe" ]; then
    echo "DEBUG BUILD MISSING: $source_exe"
    echo "Build the pocketufo Debug target, then rerun this script."
    exit 1
fi
if [ ! -f "$root/main.dat" ] || [ ! -f "$root/lang.dat" ]; then
    echo "DATA MISSING: main.dat and lang.dat must sit in the project root."
    exit 1
fi
if ! command -v cygpath >/dev/null 2>&1; then
    echo "TOOL MISSING: Git Bash cygpath is needed to pass the flow path to Win64."
    exit 1
fi
if command -v python3 >/dev/null 2>&1; then
    check_python=python3
elif command -v python >/dev/null 2>&1; then
    check_python=python
else
    echo "TOOL MISSING: Python is needed to check the captured output."
    exit 1
fi

# Keep every private app copy, log, frame, and status file for review.
run_id=$(date -u +%Y%m%dT%H%M%SZ)_$$
run_dir=$out/run_$run_id
mkdir -p "$out"
while ! mkdir "$run_dir" 2>/dev/null; do
    run_id=${run_id}_x
    run_dir=$out/run_$run_id
done
app=$run_dir/app
log=$run_dir/$log_name
flow_copy=$run_dir/$(basename "$flow")
play_checker_log=$run_dir/check_${mode}.log
mkdir "$app"
cp "$source_exe" "$app/pocketufo.exe"
cp "$root/main.dat" "$app/main.dat"
cp "$root/lang.dat" "$app/lang.dat"
cp "$flow" "$flow_copy"
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

if (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow_copy")") >"$log" 2>&1; then
    app_status=0
else
    app_status=$?
fi

if "$check_python" "$arrival_checker" "$log"; then
    arrival_status=0
else
    arrival_status=$?
fi
if "$check_python" "$combat_checker" "$log"; then
    combat_status=0
else
    combat_status=$?
fi
if "$check_python" "$ground_checker" "$log"; then
    ground_status=0
else
    ground_status=$?
fi
if [ "$mode" = play ]; then
    if "$check_python" "$play_checker" "$log" "$app" >"$play_checker_log" 2>&1; then
        play_raw_status=0
        play_kind=returned_after_abort
    else
        play_raw_status=$?
    fi
else
    if "$check_python" "$play_checker" "$log" "$app" >"$play_checker_log" 2>&1; then
        play_raw_status=0
        play_kind=moveprobe
    else
        play_raw_status=$?
        play_kind=failed
    fi
fi
if [ "$mode" = play ] && [ "$play_raw_status" -ne 0 ]; then
    if [ "$play_raw_status" -eq 2 ]; then
        play_kind=natural_terminal
    else
        play_kind=failed
    fi
fi
cat "$play_checker_log"

printf 'mode=%s\napp_exit=%s\narrival_checker_exit=%s\ncombat_checker_exit=%s\nground_checker_exit=%s\nplay_checker_exit=%s\nplay_result=%s\n' \
    "$mode" "$app_status" "$arrival_status" "$combat_status" "$ground_status" "$play_raw_status" "$play_kind" >"$run_dir/status.txt"

if [ "$app_status" -ne 0 ] || [ "$arrival_status" -ne 0 ] \
    || [ "$combat_status" -ne 0 ] || [ "$ground_status" -ne 0 ] \
    || { [ "$play_raw_status" -ne 0 ] && { [ "$mode" != play ] || [ "$play_raw_status" -ne 2 ]; }; }; then
    echo "RUN FAILED: app $app_status, arrival $arrival_status, combat $combat_status, ground $ground_status, play $play_raw_status"
    echo "Captured output: $log"
    tail -260 "$log"
    exit 1
fi

if [ "$mode" = moveprobe ]; then
    echo "RUN MOVE PROBE COMPLETE: one real candidate tap pair was captured with no queue drain: $run_dir"
elif [ "$play_kind" = natural_terminal ]; then
    echo "RUN NATURAL RESULT: see the play checker record for the terminal stage and observed later steps: $run_dir"
else
    echo "RUN PASS: live actions reached a persisted debrief exit; home arrival remains separate: $run_dir"
fi
