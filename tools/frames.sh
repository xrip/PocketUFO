#!/bin/sh
# The picture gate: regenerate the PPM dumps the draw tests write into tests/ref, convert them to PNG for
# looking at, and report any change against the committed reference pictures. A clean diff means the draws
# still paint exactly the frames recorded in tests/ref -- parity read off the rendered picture, not only off
# lit-pixel probes inside the test.
#
# The .ppm files are generated (gitignored); the .png conversions are the committed references.
#
# Usage: sh tools/frames.sh [target]   (default test_geoscapeframe)

set -e
target=${1:-test_geoscapeframe}
log=/tmp/pufo_frames.log

# The status comes from the run, not from a pipe -- the same rule tools/check.sh spells out.
if ! sh tools/iter.sh "$target" > "$log" 2>&1; then
    echo "BUILD OR TEST FAILED (see $log)"
    tail -20 "$log"
    exit 1
fi
if ! grep -q "checks passed" "$log"; then
    echo "TEST DID NOT REPORT A PASS (see $log)"
    tail -20 "$log"
    exit 1
fi

for ppm in tests/ref/*.ppm; do
    [ -f "$ppm" ] || continue
    if ! ffmpeg -y -loglevel error -i "$ppm" "${ppm%.ppm}.png"; then
        echo "PPM -> PNG FAILED for $ppm"
        exit 1
    fi
done

# A new reference is as much a gate event as a changed one, and `git diff` is blind to untracked files: Part 76's
# seven new tact_hud_*.png passed this check silently until they were looked at. Both forms are reported now.
untracked=$(git ls-files --others --exclude-standard -- tests/ref | grep -c '\.png$')
if git diff --quiet -- tests/ref && [ "$untracked" -eq 0 ]; then
    echo "frames: unchanged ($(ls tests/ref/*.png 2>/dev/null | wc -l) reference pictures)"
else
    echo "frames: CHANGED -- $(git diff --name-only -- tests/ref | tr '\n' ' ')untracked: $(git ls-files --others --exclude-standard -- tests/ref | grep '\.png$' | tr '\n' ' ')"
    exit 1
fi
