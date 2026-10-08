#!/usr/bin/env python3
"""Recreation coverage: which original functions are cited as evidence by the port.

The port's rule is that every recreated behaviour must name the original function
it answers to, in a header comment of the form "@ 0xADDR". This script scans the
source for those citations and cross-references them against the IDB inventory
(docs/idb/inventory-grouped.tsv, produced by docs/idb/group_inventory.py).

Usage (from the repo root):
    python docs/parity/coverage.py            # summary per subsystem
    python docs/parity/coverage.py --list     # also list covered names
    python docs/parity/coverage.py --gaps     # list uncovered functions per subsystem

This is a citation-based measure, not a proof of correctness: it answers "is this
original function accounted for?" not "is the port right?". Correctness evidence
lives in the per-subsystem parity notes and the tests.
"""
import argparse
import os
import re
import sys
from collections import Counter

# A citation is `@ 0xADDR` optionally followed by more slash-separated addresses, as
# in `@ 0x17790/0x177fc/0x17868`. Matching only the first address understates
# coverage: a whole function family cited in one run would count as one function.
# The pattern below captures the whole run and the code splits it.
CITE_RE = re.compile(r"@\s*(0x[0-9a-fA-F]{4,6}(?:\s*/\s*0x[0-9a-fA-F]{4,6})*)")
ADDR_RE = re.compile(r"0x([0-9a-fA-F]{4,6})")
SRC_DIRS = ("src",)
INVENTORY = os.path.join("docs", "idb", "inventory-grouped.tsv")


def load_inventory():
    """addr -> (name, size, subsystem)"""
    by_addr = {}
    if not os.path.exists(INVENTORY):
        sys.exit("missing %s; run docs/idb/group_inventory.py first" % INVENTORY)
    with open(INVENTORY, encoding="utf-8") as f:
        next(f)
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 4:
                continue
            addr, name, size, sub = parts
            by_addr["0x" + addr.lower()] = (name, int(size), sub)
    return by_addr


def load_citations():
    cited = set()
    for root, _dirs, files in os.walk(SRC_DIRS[0]):
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fn)
            with open(path, encoding="utf-8", errors="replace") as f:
                for m in CITE_RE.finditer(f.read()):
                    # One match may hold several addresses (a family citation).
                    for a in ADDR_RE.finditer(m.group(1)):
                        cited.add("0x" + a.group(1).lower())
    return cited


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true", help="list covered function names")
    ap.add_argument("--gaps", action="store_true", help="list uncovered functions")
    args = ap.parse_args()

    by_addr = load_inventory()
    cited = load_citations()
    hit = {a for a in cited if a in by_addr}
    unknown = sorted(cited - set(by_addr))

    total_bytes = sum(v[1] for v in by_addr.values())
    hit_bytes = sum(by_addr[a][1] for a in hit)

    print("functions cited: %d / %d (%.1f%%)" % (len(hit), len(by_addr),
                                                 100.0 * len(hit) / len(by_addr)))
    print("bytes cited:     %d / %d (%.1f%%)" % (hit_bytes, total_bytes,
                                                 100.0 * hit_bytes / total_bytes))
    print()
    print("%-12s %8s %8s %7s" % ("subsystem", "cited", "total", "pct"))
    counts = Counter(by_addr[a][2] for a in hit)
    totals = Counter(v[2] for v in by_addr.values())
    for sub, tot in sorted(totals.items(), key=lambda kv: -kv[1]):
        n = counts.get(sub, 0)
        print("%-12s %8d %8d %6.0f%%" % (sub, n, tot, 100.0 * n / tot))

    if unknown:
        print()
        print("cited addresses not in the inventory (thunks, data, or typos): %d"
              % len(unknown))
        for a in unknown[:20]:
            print("  %s" % a)

    if args.list:
        print()
        print("covered functions:")
        for a in sorted(hit, key=lambda x: by_addr[x][2]):
            name, _sz, sub = by_addr[a]
            print("  %-12s %-8s %s" % (sub, a, name))

    if args.gaps:
        print()
        print("uncovered functions:")
        for a in sorted(set(by_addr) - hit, key=lambda x: (by_addr[x][2], x)):
            name, sz, sub = by_addr[a]
            print("  %-12s %-8s %7d  %s" % (sub, a, sz, name))


if __name__ == "__main__":
    main()
