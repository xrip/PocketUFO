#!/usr/bin/env python3
"""The picture check for the end-to-end run: every PPM the app dumped must be a real 240x320 frame.

A corrupted frame shows up here as a wrong size, a near-empty colour set, or a picture that is almost
all one colour -- the three ways a blit goes wrong in this codebase (a surface that was never drawn,
a blit that landed outside the surface, a palette row that came back as one value). The numbers are
reported per picture so a change is visible, and the exit code is non-zero when a picture fails.

Usage: python3 -I tools/e2e_report.py <dir-with-ppm-files>
"""
import os
import sys

WANT_W = 240
WANT_H = 320
MIN_COLOURS = 40          # the shipped palettes give hundreds on every real screen
MIN_LIT_RATIO = 0.05      # at least 5% of the pixels are not near-black


def read_ppm(path):
    with open(path, 'rb') as f:
        if f.readline().strip() != b'P6':
            raise ValueError('not a P6 PPM')
        w, h = (int(x) for x in f.readline().split())
        f.readline()                      # max value
        data = f.read()
    return w, h, data


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    directory = sys.argv[1]
    bad = 0
    for name in sorted(os.listdir(directory)):
        if not name.endswith('.ppm'):
            continue
        path = os.path.join(directory, name)
        try:
            w, h, data = read_ppm(path)
        except Exception as exc:                      # a file that is not even a picture
            print('%-24s BROKEN  %s' % (name, exc))
            bad += 1
            continue
        colours = set()
        lit = 0
        for i in range(0, len(data) - 2, 3):
            colours.add((data[i], data[i + 1], data[i + 2]))
            if data[i] + data[i + 1] + data[i + 2] > 40:
                lit += 1
        pixels = w * h
        ratio = (lit / pixels) if pixels else 0.0
        problems = []
        if (w, h) != (WANT_W, WANT_H):
            problems.append('size %dx%d' % (w, h))
        if len(colours) < MIN_COLOURS:
            problems.append('only %d colours' % len(colours))
        if ratio < MIN_LIT_RATIO:
            problems.append('only %.1f%% lit' % (100.0 * ratio))
        if len(data) != pixels * 3:
            problems.append('%d bytes for %d pixels' % (len(data), pixels))
        if problems:
            print('%-24s BAD     %s' % (name, ', '.join(problems)))
            bad += 1
        else:
            print('%-24s ok      %dx%d, %d colours, %.1f%% lit' % (name, w, h, len(colours), 100.0 * ratio))
    print('e2e pictures: %d checked, %d bad' % (len([n for n in os.listdir(directory) if n.endswith('.ppm')]), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
