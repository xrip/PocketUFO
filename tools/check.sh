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
log_reltests=/tmp/pufo_check_reltests.log
ctest --test-dir build -C Release -j 8 --output-on-failure > "$log_reltests" 2>&1
rc=$?
tail -4 "$log_reltests"
# The status comes from ctest, not from the pipeline: `ctest | tail` reports tail's status, which is 0 whatever the
# tests did -- the same shape as the stale-binary trap this script exists to avoid.
if [ "$rc" -ne 0 ]; then echo "RELEASE TESTS FAILED (rc=$rc, full output in $log_reltests)"; exit 1; fi

if ! cmake --build build --config Debug > "$log_dbg" 2>&1; then
    echo "DEBUG BUILD FAILED"
    grep -iE "error|warning" "$log_dbg" | head -40
    exit 1
fi
grep -iE "error|warning" "$log_dbg" | head -20
log_dbgtests=/tmp/pufo_check_dbgtests.log
ctest --test-dir build -C Debug -j 8 --output-on-failure > "$log_dbgtests" 2>&1
rc_dbg=$?
grep -E "Failed|tests passed" "$log_dbgtests"
# Debug runs -j 8 now. It used to stay serial because test_template went red under -j 8 and green serially, which
# looked like run-order coupling; Part 37 showed it was that one check asserting the allocator's address reuse, and
# after removing it Debug is 2-failures-both-ways (test_titles, test_plane) in serial and in parallel. rc_dbg is not
# treated as fatal -- those two are red at HEAD and are reported, not hidden -- but it is printed so a third failure
# cannot read as the same old two.
echo "debug ctest exit $rc_dbg (full output: $log_dbgtests)"

python -I docs/parity/gen_report.py 2>&1 | tail -3
grep -n "Test checks run" PARITY.md
