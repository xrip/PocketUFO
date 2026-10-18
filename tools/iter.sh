#!/bin/sh
# The inner loop: one target, one config, then that test only.
#
# A full Release build here costs about 5 s and a full serial ctest about 6 s, but neither belongs in an
# iteration: build the one target the edit touched, run the one test that reads it, and save the whole-tree
# pass for tools/check.sh before a commit. The build's exit code is checked before anything is run, because a
# target that fails to compile leaves its previous .exe in place and a test run on that stale binary passes.
#
# Usage: sh tools/iter.sh [target]   (default test_geoscapeframe)

set -e
target=${1:-test_geoscapeframe}
log=/tmp/pufo_iter.log

if ! cmake --build build --config Release --target "$target" > "$log" 2>&1; then
    echo "BUILD FAILED for $target"
    grep -iE "error|warning" "$log" | head -30
    exit 1
fi
grep -iE "error|warning" "$log" | head -10

case "$target" in
    test_*)
        if [ -x "build/tests/Release/$target.exe" ]; then
            "./build/tests/Release/$target.exe"
        else
            echo "no executable for $target (built the target only)"
        fi
        ;;
    *)
        # The app target opens a window and waits for the window message loop, so it is never run here.
        # Run it by hand with its debug switches, e.g.
        #   ./build/src/pufo/Release/pocketufo.exe --new-game --dump-frame run_newgame.ppm
        echo "built $target (not run -- only test_* targets are run)"
        ;;
esac
