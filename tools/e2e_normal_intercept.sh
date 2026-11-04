#!/bin/sh
# Run the natural New Game intercept-order flow from Git Bash.
# PowerShell: & 'C:\Program Files\Git\bin\bash.exe' tools/e2e_normal_intercept.sh

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
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
out=$root/build/e2e_normal_intercept
flow=$dir/e2e_flow_normal_intercept.txt
checker=$dir/check_normal_intercept.py

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

# A new, private folder per run keeps saves and captured output separate. It is
# never cleaned here, so prior runs remain available for review.
run_id=$(date -u +%Y%m%dT%H%M%SZ)_$$
run_dir=$out/run_$run_id
mkdir -p "$out"
while ! mkdir "$run_dir" 2>/dev/null; do
    run_id=${run_id}_x
    run_dir=$out/run_$run_id
done
app=$run_dir/app
log=$run_dir/normal_intercept.log
flow_copy=$run_dir/e2e_flow_normal_intercept.txt
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

if "$check_python" "$checker" "$log"; then
    check_status=0
else
    check_status=$?
fi
printf 'app_exit=%s\nchecker_exit=%s\n' "$app_status" "$check_status" >"$run_dir/status.txt"

if [ "$app_status" -ne 0 ] || [ "$check_status" -ne 0 ]; then
    echo "RUN FAILED: app exit $app_status, checker exit $check_status"
    echo "Captured output: $log"
    tail -120 "$log"
    exit 1
fi

echo "RUN PASS: $run_dir"
