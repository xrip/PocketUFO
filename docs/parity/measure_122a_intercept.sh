#!/bin/sh
# PLAN 122a measurement (Part 156): drive the intercept from a detected contact to the flag-23 descent line.
#
# The choreography, straight from the IDB:
#  1. fasttick runs geoscape time until Strateg_CalcDetect @ 0x81378 reveals a contact (line flag 10, pause at 0x818FC).
#     A placed new-game base detects the first contact at 357300 s (Part 153), i.e. in block 20 of 3000-pulse blocks.
#  2. `tick 1` paints once -- the flag-10 draw arm installs its two controls (0x8EC74 confirm = Smap_OnPointerUp,
#     0x8EC8C clear = Smap_ClearSelection); without a paint the rects do not exist and a press hits nothing (Part 155).
#  3. `click 120 136` presses the confirm control. Its handler IS Smap_OnPointerUp @ 0x84688, whose no-selection branch
#     recentres the camera on line 0 slot 0's item -- nScrollX = nX-(nMapW/2-120)-120, nScrollY = nY-(nMapH/2-160)-160
#     (0x84838/0x8484C) -- so the contact lands at screen (120,160) with the crosshair there -- then UiPanel_RemoveLine(0)
#     and GameTime_Unpause when nothing is left (0x84944/0x84954).
#  4. `click 120 160` now presses the *map* at its centre, exactly where the contact is: the select arm writes
#     nSelectedShipIdx @ 0x187054 and bMapSelectionFlag @ 0x159E84 (0x8372C, right after GameTime_Pause at 0x83720). The
#     release of the same click takes Smap_OnPointerUp's first branch (0x84698) -> Ship_LaunchIntercept(0x187054, line 0
#     slot 0) @ 0x5a64c, and its tail unpauses again (0x84944/0x84954). One press: select and launch.
#  5. StrategList_ProcessEvents @ 0x7fcb0 then walks the launched craft's marker toward its target (0x7fe90..0x80524) and at
#     the arrival gate 0x80368 (|dx|<2 and |dy|<2) appends flag 23 (arm B at 0x80f08 for a type-1 target with nTimer > 0)
#     with slot 0 = nInterceptShipIdx @ 0x15967C and slot 1 = nInterceptStrategIdx @ 0x159680.
#
# Run from the repo root: sh docs/parity/measure_122a_intercept.sh [answer block] [pulses] [chase blocks]
# (default 20 x 3000 x 40). Log: build/measure122e/chain.log. Look for "flag 23" and the other arrival flags 14/21/22.

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
answerat=${1:-20}
step=${2:-3000}
chase=${3:-40}
out=$root/build/measure122e
mkdir -p "$out"

flow=$out/chain.txt
: > "$flow"
printf 'newgame\ntick 20\npick\nclose\nclose\n' >> "$flow"
i=0
while [ "$i" -lt "$answerat" ]; do
    printf 'fasttick %d\ntick 1\nclick 120 136\nlines\n' "$step" >> "$flow"
    i=$((i + 1))
done
printf 'tick 1\nclick 120 160\nstate\nstrateg\nlines\ndump after-launch.ppm\n' >> "$flow"
i=0
while [ "$i" -lt "$chase" ]; do
    printf 'fasttick %d\nstrateg\nlines\n' "$step" >> "$flow"
    i=$((i + 1))
done
printf 'tick 1\nlines\nstate\ndump before-descent.ppm\n' >> "$flow"

(cd "$out" && "$root/build/src/pufo/Release/pocketufo.exe" --script "$(cygpath -w "$flow")") \
    > "$out/chain.log" 2>&1

echo "exit $? ; answer at block $answerat, then $chase chase blocks of $step pulses"
echo "--- clocks at the launch and at the last three chase blocks:"
grep -E "clock [0-9]+ s" "$out/chain.log" | sed -n '1p;20p;21p;22p;60,64p'
echo "--- arrival flags (14 cell gone, 21 site, 22 base, 23 descent):"
grep -oE "flag (14|21|22|23) slots" "$out/chain.log" | sort | uniq -c || echo "none"
echo "--- the first 6 non-empty line lists:"
grep -A3 -E "^pocketufo: lines: [1-9]" "$out/chain.log" | head -30
echo "--- strateg right after the launch press:"
grep -A8 "pocketufo: state" "$out/chain.log" | head -12
