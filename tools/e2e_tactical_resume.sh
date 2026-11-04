#!/bin/sh
# Cold-process tactical save/load check using an existing Release executable.
# Every run gets a new private game/runtime/licenses tree. No existing run is reused.

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
private_parent=$root/.workers/luna-runtime-victory-current
save_flow=$dir/e2e_flow_tactical_resume_save.txt
load_flow=$dir/e2e_flow_tactical_resume_load.txt

if [ ! -f "$source_exe" ]; then
    echo "RUN FAILED: executable not found: $source_exe"
    echo "Build the pocketufo Release target, then rerun this script."
    exit 1
fi
if [ ! -f "$root/main.dat" ] || [ ! -f "$root/lang.dat" ]; then
    echo "DATA MISSING: main.dat and lang.dat must sit in the project root."
    exit 1
fi
if ! command -v cygpath >/dev/null 2>&1; then
    echo "TOOL MISSING: cygpath is needed to pass the flow paths to the Win64 executable."
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "TOOL MISSING: python3 is needed to check the live logs and private save."
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

mkdir -p "$private_parent"
run_id=$(date -u +%Y%m%dT%H%M%SZ)-$$
private_root=$private_parent/tactical-resume-$run_id
if [ -e "$private_root" ]; then
    echo "RUN FAILED: refusing to reuse private output: $private_root"
    exit 1
fi
if ! mkdir "$private_root"; then
    echo "RUN FAILED: could not create unique private output: $private_root"
    exit 1
fi
game=$private_root/game
runtime=$game/runtime
licenses=$game/licenses
mkdir "$game" "$runtime" "$licenses"
app=$game/pocketufo.exe
save_log=$private_root/tactical_resume_save.log
load_log=$private_root/tactical_resume_load.log
save_path=$game/sav0.sav

cp "$source_exe" "$app"
cp "$root/main.dat" "$game/main.dat"
cp "$root/lang.dat" "$game/lang.dat"
for dll in libopenmpt.dll openmpt-mpg123.dll openmpt-vorbis.dll openmpt-zlib.dll openmpt-ogg.dll; do
    cp "$source_dir/$dll" "$runtime/$dll"
done
cp -R "$source_dir/libopenmpt-licenses/." "$licenses/"

# App_SaveSlot uses the executable directory as AppDir. Keep main.dat, lang.dat,
# and sav0.sav beside the private executable; cwd also stays at that directory.
# Process 1 creates an ordinary mission, deploys and acts, then saves at player idle.
if ! (cd "$game" && PATH="$runtime:$PATH" "$app" --script "$(cygpath -w "$save_flow")") >"$save_log" 2>&1; then
    echo "SAVE FLOW FAILED (see $save_log)"
    tail -160 "$save_log"
    exit 1
fi
if [ ! -s "$save_path" ]; then
    echo "SAVE FLOW FAILED: private slot 0 is missing or empty: $save_path"
    tail -120 "$save_log"
    exit 1
fi

# This is a second executable process. Its flow starts at the main menu and loads slot 0.
if ! (cd "$game" && PATH="$runtime:$PATH" "$app" --script "$(cygpath -w "$load_flow")") >"$load_log" 2>&1; then
    echo "LOAD FLOW FAILED (see $load_log)"
    tail -160 "$load_log"
    exit 1
fi

python3 -I "$dir/check_tactical_resume.py" \
    "$save_log" "$load_log" "$save_path" "$save_flow" "$load_flow"
echo "PRIVATE RUN ROOT: $private_root"
