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
RENAMES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "renames.tsv")


def renames():
    """Original name -> port name, for citations the port keeps under another name.

    A flag that is never resolved is noise, and noise hides a real gap. Every line in
    renames.tsv is a pair somebody checked in the source, so a flagged address whose
    original name appears there is explained and drops out of the report.
    """
    out = {}
    if not os.path.exists(RENAMES):
        return out
    for line in open(RENAMES, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("	")
        if len(parts) >= 2:
            out[parts[0].lower()] = (parts[1], parts[2] if len(parts) > 2 else "")
    return out
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
                        names.add(m.group(1).lower())
    return names


def main():
    defined = definitions()
    cited = load_citations()
    inventory = load_inventory()
    renamed = renames()

    flagged = []
    explained = 0
    for addr, (orig_name, size, subsystem) in sorted(inventory.items()):
        if addr not in cited:
            continue
        if orig_name.lower() in defined:
            continue
        if orig_name.lower() in renamed:
            explained += 1
            continue
        flagged.append((addr, orig_name, subsystem, size))

    print(f"renames on record: {len(renamed)}  (they explain {explained} citations)")

    # Sorted by size: a flagged 4 KB citation distorts the report far more than a flagged
    # 100-byte one, and the big ones are worth checking by hand first.
    flagged.sort(key=lambda e: -e[3])
    total = sum(e[3] for e in flagged)

    print(f"credited addresses whose original name is not defined: {len(flagged)}"
          f"  (worth {total} bytes, {100.0 * total / 598792:.1f}% of the binary)")
    for addr, name, subsystem, size in flagged:
        print(f"  {addr}  {size:6d}  {subsystem:12s}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
