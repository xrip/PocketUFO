#!/bin/sh
# PLAN 122a measurement (Part 153): how long a new-game world takes to detect a contact on its own.
#
# The gate is Strateg_CalcDetect @ 0x81378: the base's site state (the byte Base_ClearFacilityGridRow writes)
# must be 1, 2, 11 or 12; the contact must be inside that state's radius (24 or 31 tiles, wrapped dx over the
# 800-wide map) with nTimer == 0; then Game_RandU16(1000) < pct, where pct is the base's EMPTY-facility-cell
# score (2 per free type-4 cell + 3 per free type-5 cell, Base_CountEmptyFacilityCells @ 0x6aee0, stored by
# Game_StartNew at 0x9461C) plus 7 while apLines[117] is 0. A placed new-game base measures state 1 and pct 9
# (docs/parity/game-core.md, Part 153), so the clock has to run for many game-hours.
#
# Geoscape time advances with the 0xC353 pulse (App_ScriptTicks), NOT with gametick: the original's 0xC350 arm
# in the geoscape is the four-byte stub Geoscape_GameTickStub @ 0x94550 (measured at 0x27508).
#
# Run from the repo root:  sh docs/parity/measure_122a_sweep.sh [pulses]   (default 60000, about 10 min)
# Prints the strateg list and the message lines every 1500 pulses. Look for "detected 1" and a
# "flag 10 slots <strateg index>" line -- that is 0x818F4's contact message.

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
pulses=${1:-60000}
out=$root/build/measure122a
mkdir -p "$out"

flow=$out/sweep.txt
: > "$flow"
printf 'newgame\ntick 20\npick\nclose\nclose\n' >> "$flow"
i=0
while [ "$i" -lt "$pulses" ]; do
    printf 'tick 1500\nstrateg\nlines\n' >> "$flow"
    i=$((i + 1500))
done

(cd "$out" && "$root/build/src/pufo/Release/pocketufo.exe" --script "$(cygpath -w "$flow")") \
    > "$out/sweep.log" 2>&1
grep -E "detected 1|lines: [1-9] of|flag 10 " "$out/sweep.log" || echo "no contact in $pulses pulses"
