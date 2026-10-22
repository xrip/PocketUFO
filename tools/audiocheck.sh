#!/bin/sh
# Manual audio evidence: the device gate inside test_audio opens the real waveOut device,
# loads the 35 shipped TACT*.WAV through the winmm backend, queues TACT2 and waits for
# WOM_DONE on the completion event. Run this by hand at the machine to hear the sample;
# the one line it ends with is what a report cites.
#
# A host with no wave device prints the skip line and stays green: the control-flow parity
# the rest of test_audio asserts never depends on a device existing.
#
# Usage: sh tools/audiocheck.sh

set -e
dir=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$dir/.." && pwd)
exe=$root/build/tests/Release/test_audio.exe

if ! cmake --build "$root/build" --config Release --target test_audio > /tmp/pufo_audio_build.log 2>&1; then
    echo "BUILD FAILED (see /tmp/pufo_audio_build.log)"
    grep -iE "error|warning" /tmp/pufo_audio_build.log | head -20
    exit 1
fi

rc=0
out=$("$exe" 2>&1) || rc=$?

echo "$out" | grep -E "^winmm device gate|device gate skipped|WOM_DONE|FAIL:" || true

if [ "$rc" -ne 0 ]; then
    echo "audiocheck: test_audio reported a failure"
    exit 1
fi

if echo "$out" | grep -q "WOM_DONE"; then
    echo "audiocheck: TACT2 played through winmm and the device reported it done"
elif echo "$out" | grep -q "device gate skipped"; then
    echo "audiocheck: no wave device on this host; control-flow gates green, nothing audible"
else
    echo "audiocheck: the device gate ran but reported no played sample"
    exit 1
fi
