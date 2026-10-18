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
ARTIFACTS = os.path.join("docs", "idb", "image-artifacts.tsv")

ARTIFACT_CLASSES = ("import-thunk", "code-thunk", "empty-stub", "init-term-entry",
                    "crt-runtime", "debug-print-stub", "decode-artifact")


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


def load_artifacts():
    """addr -> (class, evidence) for the original's no-behaviour functions.

    `docs/idb/image-artifacts.tsv` is read straight out of the IDB, instruction by
    instruction, not inferred from names: an import thunk is `LDR R12, =__imp_X;
    LDR PC, [R12]`, a code thunk ends in `B target`, an empty stub does nothing, a
    CRT init-term entry is `PUSH {LR}; BL ctor; POP {LR}; B registrar`, and the rest
    are the image's own C runtime, the compiled-out debug print and one decode
    artifact in a data segment. See the header of that file for the recipe.
    """
    out = {}
    if not os.path.exists(ARTIFACTS):
        return out
    with open(ARTIFACTS, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 5:
                continue
            # Round-trip through int so a padded row ("0x09e0b0") and the inventory's
            # "0x9e0b0" are the same key.
            out["0x%x" % int(parts[0], 16)] = (parts[3], parts[4])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true", help="list covered function names")
    ap.add_argument("--gaps", action="store_true", help="list uncovered functions")
    ap.add_argument("--check", action="store_true",
                    help="verify every uncovered function is accounted for")
    args = ap.parse_args()

    by_addr = load_inventory()
    cited = load_citations()
    hit = {a for a in cited if a in by_addr}
    unknown = sorted(cited - set(by_addr))
    art = load_artifacts()

    gaps = set(by_addr) - hit
    art_gaps = sorted(a for a in gaps if a in art)
    real_gaps = sorted(a for a in gaps if a not in art)
    cited_art = sorted(a for a in art if a in hit)
    art_bytes = sum(by_addr[a][1] for a in art_gaps)
    behaviour_total = len(by_addr) - len(art_gaps)
    behaviour_cited = len(hit) - len(cited_art)

    total_bytes = sum(v[1] for v in by_addr.values())
    hit_bytes = sum(by_addr[a][1] for a in hit)

    print("functions cited: %d / %d (%.1f%%)" % (len(hit), len(by_addr),
                                                 100.0 * len(hit) / len(by_addr)))
    print("bytes cited:     %d / %d (%.1f%%)" % (hit_bytes, total_bytes,
                                                 100.0 * hit_bytes / total_bytes))
    print("no-behaviour artifacts uncited: %d functions / %d bytes (%s)"
          % (len(art_gaps), art_bytes, ARTIFACTS))
    print("behaviour-bearing functions: %d total, %d cited, %d uncited"
          % (behaviour_total, behaviour_cited, len(real_gaps)))
    print()
    print("%-12s %8s %9s %9s %7s" % ("subsystem", "cited", "behaviour", "artifacts", "pct"))
    counts = Counter(by_addr[a][2] for a in hit)
    totals = Counter(v[2] for v in by_addr.values())
    art_counts = Counter(by_addr[a][2] for a in art_gaps)
    for sub, tot in sorted(totals.items(), key=lambda kv: -kv[1]):
        n = counts.get(sub, 0)
        na = art_counts.get(sub, 0)
        beh = tot - na
        print("%-12s %8d %9d %9d %6s"
              % (sub, n, beh, na, "%.0f%%" % (100.0 * n / beh) if beh else "-"))

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
        print("uncited functions that carry game behaviour:")
        if not real_gaps:
            print("  (none)")
        for a in real_gaps:
            name, sz, sub = by_addr[a]
            print("  %-12s %-8s %7d  %s" % (sub, a, sz, name))
        print()
        print("uncited functions that carry no behaviour, by class:")
        for cls in ARTIFACT_CLASSES:
            members = [a for a in art_gaps if art[a][0] == cls]
            if not members:
                continue
            print("  %s: %d" % (cls, len(members)))
            for a in members:
                name, sz, sub = by_addr[a]
                print("    %-8s %5d  %-46s  %s" % (a, sz, name, art[a][1]))

    if args.check:
        bad = []
        for a in sorted(art):
            cls, ev = art[a]
            if a not in by_addr:
                bad.append("artifact row %s is not a function in the inventory" % a)
            if cls not in ARTIFACT_CLASSES:
                bad.append("artifact row %s has an unknown class %r" % (a, cls))
            if not ev:
                bad.append("artifact row %s carries no evidence" % a)
            for m in ADDR_RE.finditer(ev):
                t = "0x" + m.group(1).lower()
                if t not in by_addr:
                    continue          # an import name or a table slot, not a function
                if t not in hit and t not in art:
                    bad.append("artifact %s points at %s (%s): neither cited nor an artifact"
                               % (a, t, by_addr[t][0]))
        for a in gaps:
            if a not in art:
                bad.append("uncited function %s (%s) is neither an artifact nor ported"
                           % (a, by_addr[a][0]))
        for a in cited_art:
            print("note: %s (%s) is listed as no-behaviour but the port cites it"
                  % (a, by_addr[a][0]))
        for b in bad:
            print("CHECK FAIL:", b)
        if bad:
            return 1
        print("check: OK -- %d artifact rows, %d uncited functions, %d of them real gaps"
              % (len(art), len(gaps), len(real_gaps)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
