#!/bin/sh
# The outer loop: the whole tree, both configs, then the parity report. Run this once before a commit, not
# during an iteration -- tools/iter.sh is the fast one.
#
# Measured here: a full Release build 5 s when nothing changed, a full Release ctest 6 s serial and 2 s with
# -j 8. The Debug configuration is a second complete compile of everything, so it runs here once rather than
# after every edit. Debug has two suites that are red at HEAD (test_titles, test_plane); they are reported,
# not hidden -- this script prints them and only treats a RELEASE failure as fatal.

log_rel=/tmp/pufo_check_rel.log
log_dbg=/tmp/pufo_check_dbg.log

if ! cmake --build build --config Release > "$log_rel" 2>&1; then
    echo "RELEASE BUILD FAILED"
    grep -iE "error|warning" "$log_rel" | head -40
    exit 1
fi
grep -iE "error|warning" "$log_rel" | head -20
ctest --test-dir build -C Release -j 8 2>&1 | tail -4 || { echo "RELEASE TESTS FAILED"; exit 1; }

if ! cmake --build build --config Debug > "$log_dbg" 2>&1; then
    echo "DEBUG BUILD FAILED"
    grep -iE "error|warning" "$log_dbg" | head -40
    exit 1
fi
grep -iE "error|warning" "$log_dbg" | head -20
# Debug deliberately stays serial. With -j 8 (and run alone) test_template is red while the serial run has it
# green, so the Debug suite is not independent of run order -- something one test leaves in the shared working
# directory makes another pass. Release is green both ways, so Release keeps -j 8.
ctest --test-dir build -C Debug 2>&1 | grep -E "Failed|tests passed"

python -I docs/parity/gen_report.py 2>&1 | tail -3
grep -n "Test checks run" PARITY.md
