#!/usr/bin/env python3
"""One mutation, one test, one restored file.

Row 121a's proof is "the check fails when the port is wrong", so the mutation has to be applied exactly where it
is meant to land. A line-numbered `sed` moved the wrong line once in Part 122 (it replaced a play call with a
second optimize call and failed an unrelated check), and a shell parse error mid-batch left a source file
half-mutated. This runner is the fixed version of that: the anchor is a byte string that must occur exactly once,
the file is written in binary (CRLF-safe), the test that reads it is run, and the bytes are put back with the
result reported as `restored exactly: True`. A run that prints 0 fail lines means the check has no teeth, which is
a finding -- that is how StartUi_Shutdown's pad-range claim was found to be unprovable.

Run from the repo root:  python -I docs/parity/mutations_part122.py
"""
import subprocess


def run_case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    if data.count(old) != 1:
        print("SKIP", name, "- anchor occurs", data.count(old), "times in", path)
        return None

    # The splice is by POSITION, not by a second text search. A replace-all restore is wrong whenever the
    # mutated text also occurs somewhere else in the file: Part 126's KillTimers_D case shortened
    # "KillOne(UIPULSE); KillOne(EXTRA); }" to "KillOne(UIPULSE); }", which is also KillTimers_BySpeed's
    # tail, and the restore put the dropped line back in BOTH functions -- a real edit left in the source,
    # reported only as `restored exactly: False`. Splicing at the recorded offset cannot do that.
    off = data.find(old)
    mutated = data[:off] + new + data[off + len(old):]
    open(path, "wb").write(mutated)
    try:
        # The codec has to be explicit. text=True alone picks the host locale codec, which is cp1251 here, and a
        # test that prints the game's own captions writes bytes cp1251 cannot decode: the reader thread raised
        # UnicodeDecodeError (byte 0x98, a Cyrillic caption in test_geoscapeframe's output), the exception was
        # swallowed by threading, and r.stdout came back None -- a TypeError on `r.stdout + r.stderr`, with the
        # mutation already restored, so the case looked like a crash of the suite rather than of the decode.
        r = subprocess.run(["sh", "tools/iter.sh", target], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=".")
        lines = [l.strip() for l in (r.stdout + r.stderr).splitlines()
                 if "FAIL" in l or "BUILD FAILED" in l]
        print("###", name)
        print("   fail lines:", len(lines), "(0 means the check has no teeth)")
        for l in lines[:show]:
            print("   ", l)
        return lines
    finally:
        open(path, "wb").write(mutated[:off] + old + mutated[off + len(new):])
        back = open(path, "rb").read()
        print("   restored exactly:", back == data,
              "| MUTATION left:", back.count(b"MUTATION"))
