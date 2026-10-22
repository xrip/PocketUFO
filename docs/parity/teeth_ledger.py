#!/usr/bin/env python3
"""teeth_ledger.py - which original functions a test actually reaches.

For every behaviour-bearing function in docs/idb/inventory-grouped.tsv (the inventory minus
docs/idb/image-artifacts.tsv), record two things: whether src/ cites its address (the same
CITE_RE rule docs/parity/coverage.py uses, so the two cannot disagree) and whether any tests/*.c
file reaches it -- either by citing that original address or by naming the port function.

Output: docs/parity/teeth.tsv, one row per function, plus a per-subsystem table on stdout.

Reach is measured three ways, because the port often renames: a test cites the original address, a test
names the original name, or a test names the port function that owns that address. The last one comes
from a pass over src/: a definition line and the `/* Name @ 0xaddr ... */` header directly above it,
which is the convention every module uses. Without it a renamed function reads as unreached.

Read the last column for what it is: REACH, not assertion coverage. A name in a test means the
test calls the function; it does not mean a check would fail if that function were wrong. The
point of the ledger is to size the second step -- a pinned check per function -- which is the
part of "100% coverage with proofs" that no gate measures yet.

Run: python docs/parity/teeth_ledger.py   (from the repo root)
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coverage as cv  # the gate's own matcher

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)

DEF_RE = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_ \*]*\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{]*\)\s*(?:/\*[^*]*\*/\s*)?$")


def load_port_names():
    """original address -> the port functions that own it.

    A function owns an address when the address is cited either in its `/* Name @ 0x... */` header or
    anywhere inside its body -- the way every module writes parity notes. An address cited by two
    functions maps to both, and a test naming either one counts as reach.
    """
    addr_to_port = {}

    def own(addr, name):
        key = "0x%x" % int(addr, 16)
        if name not in addr_to_port.setdefault(key, []):
            addr_to_port[key].append(name)

    for root, _dirs, files in os.walk("src/pufo"):
        for fn in files:
            if not fn.endswith(".c"):
                continue
            with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
            i = 0
            while i < len(lines):
                m = DEF_RE.match(lines[i])
                if not m:
                    i += 1
                    continue
                name = m.group(1)
                j = i - 1
                while j >= 0 and lines[j].strip()[:2] in ("/*", "*/", "* "):
                    for mm in cv.CITE_RE.finditer(lines[j]):
                        for a in cv.ADDR_RE.finditer(mm.group(1)):
                            own(a.group(1), name)
                    j -= 1
                for a in cv.ADDR_RE.finditer(lines[i]):
                    own(a.group(1), name)
                # body: stop at the first line that is just "}" in column 0
                k = i + 1
                depth = 0
                started = False
                while k < len(lines):
                    line = lines[k]
                    depth += line.count("{") - line.count("}")
                    if "{" in line:
                        started = True
                    for a in cv.ADDR_RE.finditer(line):
                        own(a.group(1), name)
                    k += 1
                    if started and depth <= 0:
                        break
                i = k if started else i + 1
    return addr_to_port


inv = cv.load_inventory()
artifacts = cv.load_artifacts()
cited = cv.load_citations()
port_names = load_port_names()

test_texts = {}
test_addrs = set()
for path in sorted(glob.glob("tests/*.c")):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    test_texts[path] = text
    for m in cv.CITE_RE.finditer(text):
        for a in cv.ADDR_RE.finditer(m.group(1)):
            test_addrs.add("0x" + a.group(1).lower())

def mentioned(name):
    if not name:
        return False
    pat = re.compile(r"\b" + re.escape(name) + r"\b")
    return any(pat.search(t) for t in test_texts.values())

rows = []
for key, (name, size, sub) in inv.items():
    if key in artifacts:
        continue  # no game behaviour of its own; the artifact ledger covers it
    ports = [p for p in port_names.get(key, []) if p != name]
    addr_hit = key in test_addrs
    reached = addr_hit or mentioned(name) or any(mentioned(p) for p in ports)
    rows.append((key, name, size, sub, key in cited, addr_hit, reached, "|".join(ports)))

rows.sort(key=lambda r: (r[3], int(r[0], 16)))

out = ["# addr\tname\tsize\tsubsystem\tcited_in_src\ttest_cites_addr\ttest_names_it\tport_function",
       "# Generated by docs/parity/teeth_ledger.py from docs/idb/inventory-grouped.tsv,",
       "# docs/idb/image-artifacts.tsv, src/ definition headers and tests/*.c.",
       "# test_names_it is the original name or the port function name in src/ that owns the address.",
       "# Reach only: a test calls it, which is not the same as a check pinning its value."]
for r in rows:
    out.append("\t".join([r[0], r[1], str(r[2]), r[3], "yes" if r[4] else "no",
                          "yes" if r[5] else "no", "yes" if r[6] else "no", r[7] or ""]))
open("docs/parity/teeth.tsv", "w", encoding="utf-8").write("\n".join(out) + "\n")

per = {}
for r in rows:
    d = per.setdefault(r[3], [0, 0, 0, 0])
    d[0] += 1
    if r[4]:
        d[1] += 1
    if r[5] or r[6]:
        d[2] += 1
    if r[4] and not (r[5] or r[6]):
        d[3] += 1

print(f"{'subsystem':12s} {'behaviour':>9s} {'cited':>6s} {'reached':>7s} {'cited-not-reached':>17s}")
tot = [0, 0, 0, 0]
for sub in sorted(per):
    d = per[sub]
    for i in range(4):
        tot[i] += d[i]
    print(f"{sub:12s} {d[0]:9d} {d[1]:6d} {d[2]:7d} {d[3]:17d}")
print(f"{'TOTAL':12s} {tot[0]:9d} {tot[1]:6d} {tot[2]:7d} {tot[3]:17d}")

deficits = [r for r in rows if r[4] and not (r[5] or r[6])]
mapped = sum(1 for r in rows if r[7])
print(f"artifact rows: {len(artifacts)}; uncited behaviour-bearing: {tot[0] - tot[1]}; "
      f"address->port-function map covers {mapped}/{len(rows)} rows")
for sub in sorted(per):
    ds = [r for r in deficits if r[3] == sub]
    if ds:
        print(f"-- {sub}: {len(ds)} cited but not reached: " +
              ", ".join(f"{r[1]} @ {r[0]}" for r in ds[:12]) +
              (" ..." if len(ds) > 12 else ""))
