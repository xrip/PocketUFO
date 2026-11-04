#!/bin/sh
# Run natural ground play through a real carrier return and one separate hour pulse.
# PowerShell: & 'C:\Program Files\Git\bin\bash.exe' tools/e2e_normal_ground_home.sh

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
out=$root/build/e2e_normal_ground_home
flow=$dir/e2e_flow_normal_ground_home.txt
arrival_checker=$dir/check_normal_air.py
combat_checker=$dir/check_normal_air_combat.py
ground_checker=$dir/check_normal_ground.py
play_checker=$dir/check_normal_ground_play.py
home_checker=$dir/check_normal_ground_home.py

if [ ! -f "$source_exe" ]; then
    echo "DEBUG BUILD MISSING: $source_exe"
    echo "Build the pocketufo Debug target, then run this script."
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

# Keep every unique app copy, log, frame, and status file for review.
run_id=$(date -u +%Y%m%dT%H%M%SZ)_$$
run_dir=$out/run_$run_id
mkdir -p "$out"
while ! mkdir "$run_dir" 2>/dev/null; do
    run_id=${run_id}_x
    run_dir=$out/run_$run_id
done
app=$run_dir/app
log=$run_dir/normal_ground_home.log
flow_copy=$run_dir/e2e_flow_normal_ground_home.txt
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

if "$check_python" "$arrival_checker" "$log"; then arrival_status=0; else arrival_status=$?; fi
if "$check_python" "$combat_checker" "$log"; then combat_status=0; else combat_status=$?; fi
if "$check_python" "$ground_checker" "$log"; then ground_status=0; else ground_status=$?; fi
if "$check_python" "$play_checker" "$log" "$app"; then play_status=0; else play_status=$?; fi
if "$check_python" "$home_checker" "$log" "$app"; then home_status=0; else home_status=$?; fi
printf 'app_exit=%s\narrival_checker_exit=%s\ncombat_checker_exit=%s\nground_checker_exit=%s\nplay_checker_exit=%s\nhome_checker_exit=%s\n' \
    "$app_status" "$arrival_status" "$combat_status" "$ground_status" "$play_status" "$home_status" \
    >"$run_dir/status.txt"

if [ "$app_status" -ne 0 ] || [ "$arrival_status" -ne 0 ] \
    || [ "$combat_status" -ne 0 ] || [ "$ground_status" -ne 0 ] \
    || [ "$play_status" -ne 0 ] || [ "$home_status" -ne 0 ]; then
    echo "RUN FAILED: app $app_status, arrival $arrival_status, combat $combat_status, ground $ground_status, play $play_status, home $home_status"
    echo "Captured output: $log"
    tail -300 "$log"
    exit 1
fi

echo "RUN PASS: real play, carrier home arrival, hourly pulse, and save/load checked: $run_dir"
