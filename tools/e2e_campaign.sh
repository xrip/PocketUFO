#!/bin/sh
# Run the real-executable campaign path against an existing Release build.
# Usage: sh tools/e2e_campaign.sh [OUTPUT_DIR]

set -eu

dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
source_exe=$root/build/src/pufo/Release/pocketufo.exe
source_dir=$(dirname "$source_exe")
out=${1:-$root/build/e2e_campaign}
app=$out/app
log=$out/e2e_flow_campaign.txt.log
load_log=$out/e2e_flow_campaign_load.txt.log
flow=$dir/e2e_flow_campaign.txt
load_flow=$dir/e2e_flow_campaign_load.txt

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
    echo "TOOL MISSING: cygpath is needed to pass the script path to the Win64 executable."
    exit 1
fi
if command -v python3 >/dev/null 2>&1; then
    campaign_python=python3
elif command -v python >/dev/null 2>&1; then
    campaign_python=python
else
    echo "TOOL MISSING: python is needed to check the private PPM snapshots."
    exit 1
fi

# Saves are relative to the executable directory. Copy the built app and its two
# input archives to this private run folder so the cycle cannot overwrite user saves.
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
rm -f "$app"/sav*.sav "$app"/campaign_*.ppm "$log" "$load_log"

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

must 'campaign gear: craft [0-9]+ crew [1-9][0-9]* cargo rifle 6 clip 10 grenade 4' \
    'the original rifle, clip and grenade kit is on the crew craft'
must 'campaign contact created: id 3 class 2 race 0 hp 50 at .* intercept [0-9]+' \
    'a damaged race-0 type-4 contact and a crew craft intercept order were created'
must 'campaign contact creation: PASS' \
    'the contact factory and real craft intercept update accepted the contact'
must '^  \[0\] flag 23 slots [0-9]+ [0-9]+' \
    'the strategic event made the real flag-23 descent offer'
must 'campaign arrival: PASS (inventory screen|native screen0 path)' \
    'a real flag-23 offer opened the populated tactical mission'
must 'campaign state launch1:.*tact 1/1 mission [0-8] .*damageBand 70 sprites 9/18/23/23 .*xcom [1-9][0-9]* enemy [1-9][0-9]* .*enemyCells [1-9][0-9]* primary9 [1-9][0-9]*' \
    'the selected-craft UI built live race-0 type-9 enemy cells'
must 'campaign deploy: (committed [1-9][0-9]* XCOM against [1-9][0-9]* enemies|already active screen 0, xcom [1-9][0-9]* enemies [1-9][0-9]* selected type [4-9][0-9]* input 1)' \
    'the native inventory route reached an active crew mission'
must 'campaign grenade: object 17 (moved .*to right hand|is already in the right hand)' \
    'the real grenade item moved through tactical inventory input'
must 'campaign grenade source: key 0x[0-9a-f]+ ownerSlot [0-9]+ kind [1-3] slot -?[0-9]+ index [0-9]+ ammo [0-9]+ armed 0 fuse 0' \
    'pickup chose one clean grenade linked to a live owner and supported source'
must 'campaign move: [0-9]+/[0-9]+/[0-9]+ -> [0-9]+/[0-9]+/[0-9]+, tu [0-9]+ -> [0-9]+, queue drained' \
    'the live pointer queued and completed a unit move'
move_line=$(grep -a 'pocketufo: campaign move: [0-9][0-9]*/[0-9][0-9]*/[0-9][0-9]* -> ' "$log" | tail -1 || true)
move_tuple=$(printf '%s\n' "$move_line" | sed -nE 's/.*move: ([0-9]+)\/([0-9]+)\/([0-9]+) -> ([0-9]+)\/([0-9]+)\/([0-9]+), tu ([0-9]+) -> ([0-9]+), queue drained/\1 \2 \3 \4 \5 \6 \7 \8/p')
if [ -z "$move_tuple" ]; then
    echo "STATE FAILED: the live move has no cell and TU tuple"
    tail -120 "$log"
    exit 1
fi
set -- $move_tuple
if [ "$1" = "$4" ] && [ "$2" = "$5" ] && [ "$3" = "$6" ]; then
    echo "STATE FAILED: the live move did not change the selected unit's cell ($move_tuple)"
    tail -120 "$log"
    exit 1
fi
if [ "$8" -ge "$7" ]; then
    echo "STATE FAILED: the live move did not spend TU ($move_tuple)"
    tail -120 "$log"
    exit 1
fi
echo "ok: pointer movement changed the cell and spent TU ($move_tuple)"

fire_line=$(grep -a 'pocketufo: campaign fire: rifle 4 ammo' "$log" | tail -1 || true)
fire_pair=$(printf '%s\n' "$fire_line" | sed -nE 's/.*ammo ([0-9]+) -> ([0-9]+), tu ([0-9]+) -> ([0-9]+).*/\1 \2 \3 \4/p')
if [ -z "$fire_pair" ]; then
    echo "STATE FAILED: the campaign shot did not report ammo and TU"
    tail -120 "$log"
    exit 1
fi
set -- $fire_pair
if [ "$2" -ge "$1" ] || [ "$4" -ge "$3" ]; then
    echo "STATE FAILED: firing did not spend ammunition and time units ($fire_pair)"
    tail -120 "$log"
    exit 1
fi
echo "ok: a real rifle shot spent ammo and TU ($fire_pair)"

must 'campaign throw: command 17 grenade 17 at [0-9]+/[0-9]+/[0-9]+, armed 1 fuse 0, handR (-1|0), prime [0-9]+->[0-9]+ throw [0-9]+->[0-9]+ cost [1-9][0-9]* landed cell [0-9]+/[0-9]+/[0-9]+' \
    'the armed grenade left the hand, paid throw TU and landed on a map cell'
must 'campaign throw source: key 0x[0-9a-f]+ pool [0-9]+ ownerSlot [0-9]+ index [0-9]+ ammo [0-9]+ armed 0 fuse 0' \
    'throw began with the exact picked-up clean grenade'
must 'campaign throw identity: key 0x[0-9a-f]+ pool [0-9]+ index [0-9]+ ammo [0-9]+->[0-9]+ armed 1 fuse 0 cell [0-9]+/[0-9]+/[0-9]+' \
    'the same armed pool item landed on a live map cell'
throw_line=$(grep -a 'pocketufo: campaign throw: command 17 grenade 17' "$log" | tail -1 || true)
throw_tuple=$(printf '%s\n' "$throw_line" | sed -nE 's/.*prime ([0-9]+)->([0-9]+) throw ([0-9]+)->([0-9]+) cost ([0-9]+) landed cell.*/\1 \2 \3 \4 \5/p')
if [ -z "$throw_tuple" ]; then
    echo "STATE FAILED: the throw report has no prime/throw TU tuple"
    tail -120 "$log"
    exit 1
fi
set -- $throw_tuple
if [ "$2" -ge "$1" ] || [ "$3" -lt "$4" ] || [ "$(( $3 - $4 ))" -lt "$5" ]; then
    echo "STATE FAILED: grenade prime/throw did not spend the reported TU cost ($throw_tuple)"
    tail -120 "$log"
    exit 1
fi
echo "ok: grenade prime and throw spent TU ($throw_tuple)"
must 'campaign detonation: result PASS pointerGone 1 armedRemaining 0 .*after enemy tick [1-9][0-9]*' \
    'the unique armed fuse-0 grenade detonated during the real enemy turn'
enemyturns=$(grep -aEc 'pocketufo: campaign enemyturn: started enemy [1-9][0-9]* input 0' "$log" || true)
if [ "${enemyturns:-0}" -lt 2 ]; then
    echo "STATE FAILED: two real enemy turns did not start"
    tail -120 "$log"
    exit 1
fi
must 'campaign enemyturn: ticks [1-9][0-9]* enemy 0 input 1 busy 0 queue [0-9]+/0 changed [0-9]+' \
    'an enemy turn ended and player input returned'
must 'campaign abort: (yes at [0-9]+/[0-9]+, ended 1 screen 13|battle already ended on screen 13)' \
    'the real abort or finish path opened the debrief'
must 'campaign abort: returned geoscape 1 screen 0 tact 0' \
    'the debrief control returned to the geoscape'
must 'campaign returnack: (PASS acked [0-9]+|NONE pending) lines 0 modal 0 geo 1 screen 0 tact 0 pause [01]' \
    'the returned geoscape has empty status and modal queues'
if [ ! -s "$app/sav0.sav" ]; then
    echo "SAVE FAILED: $app/sav0.sav was not written"
    tail -120 "$log"
    exit 1
fi

# The first sortie is saved on the geoscape after debrief. Raw SaveGame skips
# TGameData unless Map_bTactFlag is set, so mission type and Map+0x1511E are
# intentionally outside this geoscape persistence tuple.
campaign_assignment_tuple() {
    grep -a "pocketufo: campaign roster $2:" "$1" |
        sed -nE 's/^pocketufo: campaign roster .*: mode ([0-9]+) craftIdx (-?[0-9]+) selectedBase (-?[0-9]+) soldiers ([0-9]+) craftMatch ([0-9]+) craftOther ([0-9]+) craftOut ([0-9]+) noCraft ([0-9]+)/\1 \2 \3 \4 \5 \6 \7 \8/p' |
        head -n 1
}

# Load slot 0 in a new process under the same private executable directory.
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

must_load 'pocketufo: state after_load:' 'a fresh process loaded save slot 0'
must_load 'campaign trace after_load:.*spawnFns 1/1 user 1 zoom [0-9]+/[0-9]+' \
    'the fresh load wired the native spawn and strategic event callbacks'
must_load 'campaign navcycle: PASS root10 0->1 nav2 1->4 close 4->1->0' \
    'the loaded world opened and closed the base and soldier screens through live controls'
if ! "$campaign_python" "$dir/check_campaign_save_load.py" "$log" "$load_log"; then
    echo "STATE FAILED: saved geoscape records did not match the fresh-process load"
    tail -120 "$load_log"
    exit 1
fi

load_assign=$(campaign_assignment_tuple "$load_log" after_load)
nav_assign=$(campaign_assignment_tuple "$load_log" after_navcycle)
contact_assign=$(campaign_assignment_tuple "$load_log" after_contact)
pulses_assign=$(campaign_assignment_tuple "$load_log" after_contact_pulses)
if [ -z "$load_assign" ] || [ "$load_assign" != "$nav_assign" ] \
     || [ "$load_assign" != "$contact_assign" ] || [ "$load_assign" != "$pulses_assign" ]; then
    echo "STATE FAILED: the fresh-load/nav/contact steps changed the saved soldier craft assignments"
    echo "  after load:    $load_assign"
    echo "  after nav:     $nav_assign"
    echo "  after contact: $contact_assign"
    echo "  after pulses:  $pulses_assign"
    tail -120 "$load_log"
    exit 1
fi
set -- $load_assign
expected_second_xcom=$5
if [ "$1" != 0 ] || [ "$2" != 0 ] || [ "$3" != 0 ] || [ "$expected_second_xcom" -le 0 ]; then
    echo "STATE FAILED: the second contact is not linked to the loaded craft's assigned crew ($load_assign)"
    tail -120 "$load_log"
    exit 1
fi
echo "ok: loaded crew assignments stayed stable and $expected_second_xcom soldiers are assigned to craft 0"

must_load 'campaign arrival: PASS (inventory screen|native screen0 path)' \
    'the fresh process reached a real flag-23 intercept and opened the mission'

before_launch_assign=$(campaign_assignment_tuple "$load_log" before_launch)
if [ -z "$before_launch_assign" ]; then
    echo "STATE FAILED: no pre-launch soldier-assignment trace"
    tail -120 "$load_log"
    exit 1
fi
set -- $before_launch_assign
second_launch_line=$(grep -a 'pocketufo: campaign state launch2:' "$load_log" | tail -1 || true)
second_launch_xcom=$(printf '%s\n' "$second_launch_line" | sed -nE 's/.*xcom ([0-9]+) enemy.*/\1/p')
if [ "$1" != 0 ] || [ "$2" != 0 ] || [ "$3" != 0 ] \
     || [ "$5" -ne "$expected_second_xcom" ] \
     || [ "$second_launch_xcom" != "$expected_second_xcom" ]; then
    echo "STATE FAILED: tactical XCOM does not match every saved soldier assigned to craft 0"
    echo "  saved assignment: $load_assign"
    echo "  before launch:   $before_launch_assign"
    echo "  launch state:    $second_launch_line"
    tail -120 "$load_log"
    exit 1
fi
echo "ok: the tactical mission selected all $expected_second_xcom soldiers assigned to craft 0"

second_line=$(grep -a 'pocketufo: campaign state deploy2:' "$load_log" | tail -1 || true)
deployed_xcom=$(printf '%s\n' "$(grep -a 'pocketufo: campaign deploy: committed' "$load_log" | tail -1)" |
    sed -nE 's/.*committed ([0-9]+) XCOM.*/\1/p')
if ! printf '%s\n' "$second_line" | grep -aEq 'xcom [1-9][0-9]* enemy [1-9][0-9]*' \
     || [ "$deployed_xcom" != "$expected_second_xcom" ]; then
    echo "STATE FAILED: the second real deployment is not populated"
    echo "  expected XCOM: $expected_second_xcom"
    echo "  deployed XCOM: $deployed_xcom"
    echo "  state: $second_line"
    tail -120 "$load_log"
    exit 1
fi
echo "ok: the loaded campaign launched a second populated mission"

"$campaign_python" "$dir/check_campaign_ppm.py" "$app" \
    campaign_after_deploy.ppm campaign_after_move.ppm campaign_after_fire.ppm \
    campaign_after_throw.ppm campaign_after_detonation.ppm campaign_debrief.ppm \
    campaign_return_card.ppm campaign_returned.ppm campaign_after_load.ppm campaign_second_deploy.ppm

echo "campaign log: $log"
echo "fresh-load log: $load_log"
