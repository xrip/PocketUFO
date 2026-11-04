#!/bin/sh
# Manual audio evidence: the device gate inside test_audio opens the real waveOut device,
# loads BACK0.IT and the 35 shipped TACT*.WAV files, then waits for a nonzero music mix
# buffer and the TACT2 final mix buffer to complete. This proves PCM reached waveOut;
# it does not prove the machine's speakers produced audible sound.
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

echo "$out" | grep -E "^winmm device gate|device gate skipped|BACK0.IT|final waveOut buffer|FAIL:" || true

if [ "$rc" -ne 0 ]; then
    echo "audiocheck: test_audio reported a failure"
    exit 1
fi

if echo "$out" | grep -q "BACK0.IT produced nonzero PCM in a completed waveOut buffer" \
    && echo "$out" | grep -q "final waveOut buffer"; then
    echo "audiocheck: BACK0.IT and TACT2 PCM reached completed WinMM output buffers"
elif echo "$out" | grep -q "device gate skipped"; then
    echo "audiocheck: no wave device on this host; control-flow gates green, nothing audible"
else
    echo "audiocheck: the device gate ran but reported no played sample"
    exit 1
fi
