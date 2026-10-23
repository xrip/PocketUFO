#!/bin/sh
# PLAN 122a measurement (Part 155): run a new-game world for many game-days and watch the mission-kind line appear.
#
# Why a press+release after every block: a contact announcement pauses the geoscape clock (GameTime_Pause @ 0x5d6ac
# from 0x818FC) and only the Smap_* family clears the latch (GameTime_Unpause @ 0x5d6c0 has nine code xrefs, all in
# that family). Smap_OnPointerUp @ 0x84688 -- the release the scripted `click` performs -- removes line 0 and calls
# GameTime_Unpause when the line list is empty (0x84944/0x84954), so one press+release per block keeps the clock
# moving for as long as the game would let it: the same release a player makes on the map.
#
# Why the answer press needs one painted step: the two controls the flag-10 press arm hit-tests (0x186BA4, 0x186BB4)
# are *installed* by the flag-10 draw arm (StatusPanel_DrawSelectedContact writes the handler and the rect at
# 0x8EC84..0x8ECF4), and the draw arm runs on the paint. A block of `fasttick` leaves the panel logically open (state
# prints `status flag 10`, the line is there) with the rects never written, so a click then hits nothing -- measured:
# 80 blocks of `fasttick 3000` + `click 120 160` froze the clock at 357300 s with `status flag 10, lines: 1` on every
# block after the detection (build/measure122d, Part 155). One `tick 1` paints once, installs the rects, and the press
# answers the line. $3 picks which control: 136 = the confirm (PUFO_STATUS_SELCTL_Y_STD_CONF 0x88, pfnSelectTarget),
# 160 = the clear (_STD_CLEAR 0xA0, pfnClearSelection). Both call GameTime_Unpause (0x84668, 0x84954).
#
# What the sweep is looking for: nAlienPoolAddend @ 0x1472C8 (+1 per day at Geoscape_TickBases 0x5D3C8) gates
# StrategList_RollCraftStats @ 0x81B7C, which clamps the craft class to 2 while it is < 15 -- so classes 3..7 and the
# mission SITES they carry need 15+ game-days. A day is 86400 s and a pulse is 5.955 s (measured: 60000 pulses =
# 357300 s), so 15 days is ~218000 pulses. The mission line itself is flag 23, produced only by
# Strateg_ConfirmInterceptTarget @ 0x7fb98 through StrategList_ProcessEvents @ 0x7fcb0 (the type-4 arrival arm at
# 0x80798..0x807D8 pauses the clock and offers the descent).
#
# Run from the repo root:  sh docs/parity/measure_122a_days.sh [blocks] [block pulses] [answer y]   (default
# 80 x 3000 x 136 = 240000 pulses ~ 16.5 game-days). Prints state + the strateg list + the line list after every
# block; the log is build/measure122d/days.log.

set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
blocks=${1:-80}
step=${2:-3000}
answery=${3:-136}
out=$root/build/measure122d
mkdir -p "$out"

flow=$out/days.txt
: > "$flow"
printf 'newgame\ntick 20\npick\nclose\nclose\n' >> "$flow"
i=0
while [ "$i" -lt "$blocks" ]; do
    printf 'fasttick %d\ntick 1\nclick 120 %d\nstate\nstrateg\nlines\n' "$step" "$answery" >> "$flow"
    i=$((i + 1))
done

(cd "$out" && "$root/build/src/pufo/Release/pocketufo.exe" --script "$(cygpath -w "$flow")") \
    > "$out/days.log" 2>&1

echo "exit $? ; blocks $blocks x $step pulses, answer click at 120/$answery"
grep -c "pocketufo: strateg:" "$out/days.log" || true
grep -E "clock [0-9]+ s" "$out/days.log" | sed -n '1~10p'
echo "--- items with class >= 3 or a site:"
grep -E "class [3-9]|site [0-9]" "$out/days.log" | sort -u | head -20
echo "--- detected contacts:"
grep -c "detected 1" "$out/days.log" || true
echo "--- message line flags seen:"
grep -oE "flag [0-9]+ slots" "$out/days.log" | sort | uniq -c
echo "--- flag 23 (the mission/descent line):"
grep -n "flag 23" "$out/days.log" | head -5 || echo "none"
