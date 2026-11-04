#!/bin/sh
# Run the live strategy UI flow against an existing Release build.
# Usage: sh tools/e2e_strategy.sh

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
out=$root/build/e2e_strategy
app=$out/app
log=$out/e2e_flow_strategy.txt.log
load_log=$out/e2e_flow_strategy_load.txt.log
flow=$dir/e2e_flow_strategy.txt
load_flow=$dir/e2e_flow_strategy_load.txt

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
    echo "TOOL MISSING: cygpath is needed to pass the flow paths to the Win64 executable."
    exit 1
fi
if command -v python3 >/dev/null 2>&1; then
    strategy_python=python3
elif command -v python >/dev/null 2>&1; then
    strategy_python=python
else
    echo "TOOL MISSING: python is needed to check the private snapshots."
    exit 1
fi

# Save files and pictures stay beside this private app copy.
mkdir -p "$app"
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
rm -f "$app"/sav*.sav "$app"/strategy_*.ppm "$log" "$load_log"

if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$flow")") >"$log" 2>&1; then
    echo "RUN FAILED (see $log)"
    tail -120 "$log"
    exit 1
fi

must() {
    if ! grep -aEq "$1" "$log"; then
        echo "STATE FAILED: $2"
        tail -120 "$log"
        exit 1
    fi
    echo "ok: $2"
}

must 'pocketufo: new game: 4147000 funds, 1 bases, 8 soldiers, 3 craft' \
    'the actual menu path started the normal campaign'
must 'strategy tuple base_info:.*screen [0-9]+/3 ' \
    'the base strip opened the live base-info screen before navigation'
must 'strategy tuple base_strip_staff:.*screen [0-9]+/1 ' \
    'the base-info close returned to screen 1 before shop navigation'
must 'strategy tuple shop_staff_open:.*screen [0-9]+/11 ' \
    'the shop nav control opened the real buy screen'
must 'strategy tuple staff_pending:.*sites 1.*engineersAvailable/Total 0/0.*scientistsAvailable/Total 0/0' \
    'live shop confirmation made a pending staff shipment'
must 'strategy tuple staff_pending_map:.*screen [0-9]+/0 ' \
    'the live close control returned to the map before time advance'
must '^  \[0\] flag 41 ' \
    'the game clock delivered the staff through the normal arrival line'
must 'strategy tuple staff_ready:.*engineersAvailable/Total 1/1.*scientistsAvailable/Total 1/1' \
    'one real engineer and scientist arrived at the starting base'
must 'strategy tuple research_base_strip:.*screen [0-9]+/1 ' \
    'the base-info close returned to screen 1 before research navigation'
must 'strategy tuple research_open:.*screen [0-9]+/8 ' \
    'the research nav control opened the real research screen'
must 'strategy topic research_running: id [0-9]+ active 1 unlocked 0 progress [0-9]+/[1-9][0-9]* scientists 1' \
    'the live research row has one scientist assigned and is not complete'
must 'strategy tuple research_after_close:.*screen [0-9]+/1 ' \
    'research close returned to the base strip before the next close'
must 'strategy tuple research_on_map:.*screen [0-9]+/0 ' \
    'the normal close controls returned from research to the map'
must 'strategy tuple shop_base_strip:.*screen [0-9]+/1 ' \
    'the base-info close returned to screen 1 before the second shop navigation'
must 'strategy tuple shop_rifle_open:.*screen [0-9]+/11 ' \
    'the second shop order used the real buy screen'
must 'strategy row shop_rifle_open: shop 7 aux 4' \
    'the second normal shop order selected the listed rifle row'
must 'strategy site before_save: index 0 flag 24 ' \
    'the rifle order remains a real pending shipment at save time'
must 'strategy tuple before_save:.*screen [0-9]+/0 ' \
    'the save snapshot is on the normal geoscape screen'
must 'strategy site-item before_save: site 0 slot 4 qty 1' \
    'the pending shipment carries one rifle'
must 'pocketufo: save slot 0: written' \
    'the normal save hook wrote private slot 0'
if [ ! -s "$app/sav0.sav" ]; then
    echo "SAVE FAILED: $app/sav0.sav was not written"
    tail -120 "$log"
    exit 1
fi

if ! (cd "$app" && ./pocketufo.exe --script "$(cygpath -w "$load_flow")") >"$load_log" 2>&1; then
    echo "LOAD RUN FAILED (see $load_log)"
    tail -120 "$load_log"
    exit 1
fi

must_load() {
    if ! grep -aEq "$1" "$load_log"; then
        echo "LOAD STATE FAILED: $2"
        tail -120 "$load_log"
        exit 1
    fi
    echo "ok: $2"
}

must_load 'pocketufo: state after_load:' 'a fresh process loaded slot 0'
must_load 'strategy tuple after_load:' 'the observer saw the loaded live strategy state'
must_load 'strategy tuple after_load:.*screen [0-9]+/0 ' 'the loaded world is on the map before time advances'
must_load '^  \[0\] flag 41 ' 'the pending rifle arrived after real one-hour timer steps'
must_load 'strategy stock after_delivery: slot 4 qty 1' 'the delivered rifle entered real base stock'
must_load 'pocketufo: lines: 0 of' 'the live arrival card was cleared before the run ended'

if ! "$strategy_python" "$dir/check_strategy_save_load.py" "$log" "$load_log"; then
    echo "STATE FAILED: saved strategy rows did not survive the fresh-process load and clock run"
    tail -120 "$load_log"
    exit 1
fi

"$strategy_python" "$dir/check_campaign_ppm.py" "$app" \
    strategy_mainmenu.ppm strategy_newgame.ppm strategy_staff_queued.ppm \
    strategy_staff_ready.ppm strategy_research_open.ppm strategy_research_running.ppm \
    strategy_before_save.ppm strategy_after_load.ppm strategy_after_delivery.ppm

echo "strategy log: $log"
echo "fresh-load log: $load_log"
