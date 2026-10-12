#!/usr/bin/env python3
"""Cross-check every credited address against a real definition in src/pufo.

coverage.py credits an original function when a comment in src/pufo cites it with
the `Name @ 0xaddr` form. That is only honest when the port actually implements it.
Hook fields are where this goes wrong: a comment naming the original the host is
expected to supply gets credited as a recreation.

This walks the IDB inventory, keeps the addresses coverage.py credits, and asks
whether the original's own name appears as a definition in any .c file. A flag is a
review prompt, not a verdict: the port renames and merges families on purpose, so a
documented rename is an expected false positive. What it is good at catching is a
citation for something nobody wrote.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import load_inventory, load_citations  # noqa: E402

SRC = os.path.join("src", "pufo")
DEF_RE = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_ \*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def definitions():
    names = set()
    for root, _, files in os.walk(SRC):
        for name in files:
            if not name.endswith(".c"):
                continue
            with open(os.path.join(root, name), encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    if line.lstrip().startswith(("//", "/*", "*")):
                        continue
                    m = DEF_RE.match(line)
                    if m:
                        names.add(m.group(1))
    return names


def main():
    defined = definitions()
    cited = load_citations()
    inventory = load_inventory()

    flagged = []
    for addr, (orig_name, _size, subsystem) in sorted(inventory.items()):
        if addr not in cited:
            continue
        if orig_name in defined:
            continue
        flagged.append((addr, orig_name, subsystem))

    print(f"credited addresses whose original name is not defined: {len(flagged)}")
    for addr, name, subsystem in flagged:
        print(f"  {addr}  {subsystem:12s}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
