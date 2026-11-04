#!/bin/sh
# The whole-path gate: run the real executable through the menu -> new game -> placement -> geoscape ->
# save -> load flow, take a picture at every step, check each picture, and compare them against the
# read-only reference images in tests/ref/e2e; missing or changed images fail as unapproved.
#
# Two runs because the load is entered the way the original enters it: a fresh process at the main menu,
# row 1, a slot row. The save the first run wrote is the file the second one loads, so the pair also
# proves the save/load round trip across processes.
#
# The window is never shown on this path (App_InitInstance's bHidden arm): the pictures are read off the
# logical surface, and a window that grabs the foreground on every run gets in the way of the person at
# the machine.
#
# Usage: sh tools/e2e.sh

set -e
dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
out=$root/build/e2e
refs=$root/tests/ref/e2e
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
app=$out/app

if ! cmake --build "$root/build" --config Release --target pocketufo > /tmp/pufo_e2e_build.log 2>&1; then
    echo "BUILD FAILED (see /tmp/pufo_e2e_build.log)"
    grep -iE "error|warning" /tmp/pufo_e2e_build.log | head -20
    exit 1
fi

# Save slots and data lookup use the private exe directory, not the source exe directory.
# The flow saves live only in the private app directory.
# PPM dump paths are relative to the process working directory, so they stay in $out.
rm -f "$out"/*.ppm "$out"/*.png "$out"/flow_*.log "$out"/*.rgb
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
rm -f "$app"/sav*.sav

for flow in e2e_flow_new.txt e2e_flow_load.txt e2e_flow_quit.txt e2e_flow_tact.txt; do
    if ! (cd "$out" && "$app/pocketufo.exe" --script "$(cygpath -w "$dir/$flow")") > "$out/flow_$flow.log" 2>&1; then
        echo "RUN FAILED for $flow"
        cat "$out/flow_$flow.log"
        exit 1
    fi
    cat "$out/flow_$flow.log"
done

# State assertion for the startup picks: InitInstance @ 0x9FD48 spends nine draws (0xA0134..0xA024C) and the
# two live results are Map_nMissionType (stored at 0xA0228) and Map+0x1511E (raised at 0xA01C8, cleared for
# mission types 3 and 4 at 0xA0244/0xA024C). With the shell's fixed clock (30 s 0 ms) those draws land on
# mission 6 with the byte left 0 -- the same values tests/test_rand.c pins from the record. The unit tests
# cannot see the install itself (pufo_app.c is the executable's own file), so this gate is what proves the
# values reach the live world.
if ! grep -q 'mission type 6, map 0x1511E 0' "$out/flow_e2e_flow_new.txt.log"; then
    echo "STATE FAILED: the startup picks did not reach the world (want mission 6, Map+0x1511E 0)"
    grep -m2 'pocketufo: state' "$out/flow_e2e_flow_new.txt.log" || true
    exit 1
fi
echo "startup picks: mission type 6, Map+0x1511E 0 (InitInstance 0xA0228 / 0xA01C8)"

# Chain assertion (Part 148, PLAN 120b): the load flow's world is the save the new-game process wrote, and
# the file it reads is the one the previous flow wrote -- the slot files are cleared at the top of this
# script and only these two flows touch them. So the cells the save carries must come out identical in the
# second process: the base count, the placed base's own coordinates, the soldier count and the funds.
# The cells deliberately NOT compared are the per-process UI state -- list
# mode, globe sweep, animation progress, pending region -- which the save does not carry through to the
# loaded moment, and the base coordinates are compared only because both processes have the base placed by
# then (an unplaced base reads -6000/-6000, which is the placement screen's state, not the save's).
# Mission type and Map+0x1511E belong to the tactical-only TGameData chunk. Their identical startup
# values in these two processes do not prove persistence, so they are excluded from this tuple.
save_tuple() {
    grep 'pocketufo: state after' "$1" | tail -1 |
        sed -E 's/.*bases ([0-9]+), base 0 at (-?[0-9]+)\/(-?[0-9]+), soldiers (-?[0-9]+), funds (-?[0-9]+).*/\1 \2\/\3 \4 \5/'
}
t_new=$(save_tuple "$out/flow_e2e_flow_new.txt.log")
t_load=$(save_tuple "$out/flow_e2e_flow_load.txt.log")
if [ -z "$t_new" ] || [ "$t_new" != "$t_load" ]; then
    echo "STATE FAILED: the save->load round trip changed the save-carried cells"
    echo "  wrote (process 1): $t_new"
    echo "  read  (process 2): $t_load"
    exit 1
fi
echo "save->load across processes: bases/base-0/soldiers/funds identical ($t_new -> $t_load)"

# Quit seam on a loaded world (Part 148, PLAN 120b): the load flow now runs App_QuitFromTact @ 0x18e18 over
# the world a save produced, not over a new game. The list the load built must be live before the seam
# (the original's LoadGame fix-ups at 0x44f64/0x45008 add items on top of what the save carried, so a
# loaded count larger than the stamp is the original's own behaviour), must come out empty through the
# seam's PtrArray_DeleteAll arm (0x3E14C..0x3E154), and the 0x186530 count mirror must keep the value the
# save restamped -- the residue App_QuitFromTact's 0x18E20 arm leaves, which the quit flow already shows
# over a new game. Exit code 0 of the whole run is checked by the loop above.
strat_pair() { sed -E 's/.*strateg: ([0-9]+) items, count mirror 0x186530 ([0-9]+).*/\1 \2/'; }
s_first=$(grep -m1 'pocketufo: strateg:' "$out/flow_e2e_flow_load.txt.log" | strat_pair)
s_last=$(grep 'pocketufo: strateg:' "$out/flow_e2e_flow_load.txt.log" | tail -1 | strat_pair)
n_first=${s_first% *}; m_first=${s_first#* }
n_last=${s_last% *}; m_last=${s_last#* }
if [ "${n_first:-0}" -le 0 ] || [ "${n_last:-1}" -ne 0 ] || [ "$m_first" != "$m_last" ]; then
    echo "STATE FAILED: quit-YES over a loaded world"
    echo "  before the seam: items $n_first, mirror $m_first"
    echo "  after  the seam: items $n_last, mirror $m_last"
    exit 1
fi
echo "quit-YES on a loaded world: $n_first live items -> 0, mirror 0x186530 unchanged ($m_first)"

# Live mission (Part 150, PLAN 122): the tact flow runs the launch tail -- GeoscapeUi_FreeAll mode 0 at
# 0x83ABC, whose 0x86D34 store clears Map_bGeoscapeUi and opens GameTick_OnTimer's tactical branch at
# 0x27508, then Tact_InitMission mode 0 at 0x83AC4 -- over the world the new-game flow built. What the
# gate requires is what the original's own bring-up produces: the 0x83AC4 thunk returns 1, the eight
# soldiers the world already had are deployed on the tactical map, the tactical branch owns the ticks
# (player input live), and the quit seam at App_QuitFromTact @ 0x18e18 closes a LIVE mission rather than
# standing in for one -- the last tact line must read "no live mission". What the gate does NOT claim:
# this mission has no enemy roster (the tier/variant cells come from Smap_BeginCraftSelect @ 0x838dc's
# decision tree, which a scripted run does not walk), so the queue stays empty -- stated in
# tools/e2e_flow_tact.txt and in PLAN, not hidden by a weaker assertion.
tlog=$out/flow_e2e_flow_tact.txt.log
if ! LC_ALL=C grep -Eq '^pocketufo: equiparmor: base .* type 2, suit stock 0,' "$tlog"; then
    echo "ARMOUR EQUIP FAILED: the scripted soldier did not wear the stocked Power suit"
    exit 1
fi
if ! grep -q 'step launch (0x83ABC then 0x83AC4): 1' "$tlog"; then
    echo "STATE FAILED: the launch tail did not bring the mission up (0x83AC4 must hand back 1)"
    grep -m2 'step launch' "$tlog" || true
    exit 1
fi
t_first=$(grep -m1 'pocketufo: tact:' "$tlog")
n_xcom=$(sed -n '/step launch/,/step gametick/p' "$tlog" | grep -c '^  xcom ')
if ! printf '%s\n' "$t_first" | grep -q 'player input 1' || [ "$n_xcom" -lt 4 ]; then
    echo "STATE FAILED: the mission came up without the world's own soldiers or without player input"
    echo "  first tact line: $t_first"
    echo "  deployed soldiers reported: $n_xcom"
    exit 1
fi
t_last=$(grep 'pocketufo: tact:' "$tlog" | tail -1)
if [ "$t_last" != "pocketufo: tact: no live mission" ]; then
    echo "STATE FAILED: the quit seam did not close the live mission"
    echo "  last tact line: $t_last"
    exit 1
fi
echo "live mission in the exe: 0x83AC4 returned 1, $n_xcom deployed soldiers, tactical ticks live; quit-YES closed the live mission"

# Gate scaffolding: a --script run does NOTHING and still exits 0 while another pocketufo.exe owns the
# POCKET_UFO window -- App_InitInstance's single-instance check (the original's FindWindowW @ 0x9FD64)
# returns false and WinMain returns 0 before the script is read. Measured: one leftover hidden window from
# a killed run made six later runs write no pictures at all. Count the pictures against the flow files
# before believing the run.
dump_files=$(grep -h '^[[:space:]]*dump ' "$dir"/e2e_flow_new.txt "$dir"/e2e_flow_load.txt "$dir"/e2e_flow_tact.txt | awk '{ print $2 }')
want=$(printf '%s\n' "$dump_files" | sed '/^$/d' | wc -l)
got=$(ls "$out"/*.ppm 2>/dev/null | wc -l)
if [ "$got" -ne "$want" ]; then
    echo "RUN FAILED: $got pictures, the flows ask for exactly $want"
    echo "(a pocketufo.exe that is still running holds the single-instance window; close it -- e.g."
    echo " taskkill //F //IM pocketufo.exe -- and re-run)"
    exit 1
fi
for name in $dump_files; do
    if [ ! -s "$out/$name" ]; then
        echo "PICTURE MISSING (unapproved): $name"
        exit 1
    fi
done

for ppm in "$out"/*.ppm; do
    if ! ffmpeg -y -loglevel error -i "$ppm" "${ppm%.ppm}.png"; then
        echo "PPM -> PNG FAILED for $ppm"
        exit 1
    fi
done

python3 -I "$dir/e2e_report.py" "$out"

rm -f "$out"/current.rgb "$out"/reference.rgb
for png in "$out"/*.png; do
    name=$(basename "$png")
    ref=$refs/$name
    if [ ! -f "$ref" ]; then
        echo "REFERENCE MISSING (unapproved): $name"
        exit 1
    fi
    if ! ffmpeg -y -loglevel error -i "$png" -f rawvideo -pix_fmt rgb24 "$out/current.rgb"; then
        echo "IMAGE DECODE FAILED: generated $name"
        exit 1
    fi
    if ! ffmpeg -y -loglevel error -i "$ref" -f rawvideo -pix_fmt rgb24 "$out/reference.rgb"; then
        echo "REFERENCE DECODE FAILED: $name"
        exit 1
    fi
    if ! cmp -s "$out/current.rgb" "$out/reference.rgb"; then
        echo "IMAGE CHANGED (unapproved): $name"
        exit 1
    fi
    echo "e2e reference: unchanged $name"
done
rm -f "$out"/current.rgb "$out"/reference.rgb
echo "e2e pictures: unchanged against read-only references"
