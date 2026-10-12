#!/usr/bin/env python3
"""Audit: citations that credit a function the port does not implement.

The parity report credits any `@ 0xADDR` found in src/. That is a blind spot: a
comment that says a function is NOT ported still credits it. This finds the
citing lines that read like a description of unported work, so a human can read
them and fix the ones that are wrong.

Two checks, reported separately:

1. NEGATED  - the citing line contains a negation ("not ported", "unported",
  "hook", "seam", "stub", "no implementation"). A port claim and a port
  description must agree.
2. UNDEFINED - the name written next to `@ 0xADDR` is not defined in src/.
  Renamed and merged ports make this noisy, so it is only a candidate list.

Usage: python docs/parity/audit_credits.py
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
LIST_RE = re.compile(r"^\s*(\S+)\s+(0x[0-9a-fA-F]+)\s+(\S+)\s*$")
CITE_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*@\s*(0x[0-9a-fA-F]{4,6})")
NEGATE_RE = re.compile(
    r"\bnot ported\b|\bunported\b|\bno implementation\b|\bnot recreated\b"
    r"|\bhook\b|\bseam\b|\bstub\b|\bnot implemented\b|\bnot yet\b",
    re.IGNORECASE,
)
DEF_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\s*\*)*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def credited():
    out = subprocess.run(
        [sys.executable, str(Path(__file__).with_name("coverage.py")), "--list"],
        capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        m = LIST_RE.match(line)
        if m:
            rows.append((m.group(2).lower(), m.group(3)))
    return rows


def clause(line, start, end):
    """The clause a citation sits in, and only that clause.

    A denial belongs to the citation it is attached to. On a line that lists several
    originals, "not ported" after one of them says nothing about the others, so the
    search is bounded by the commas and semicolons around this citation.
    """
    left = max(line.rfind(",", 0, start), line.rfind(";", 0, start)) + 1
    right = min(x for x in (line.find(",", end), line.find(";", end), len(line)) if x >= end)
    return line[left:right].strip()


def citations():
    """addr -> list of (file, lineno, name, line text, clause text)."""
    hits = {}
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for m in CITE_RE.finditer(line):
                hits.setdefault(m.group(2).lower(), []).append(
                    (path.relative_to(ROOT), n, m.group(1), line.strip(),
                     clause(line, m.start(), m.end())))
    return hits


def defined_names():
    names = set()
    for path in SRC.rglob("*.c"):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = DEF_RE.match(line)
            if m:
                names.add(m.group(1))
    return names


def main():
    rows = credited()
    cites = citations()
    defined = defined_names()

    negated, undefined = [], []
    for addr, name in rows:
        for where in cites.get(addr.lower(), []):
            f, n, cname, text, clausetext = where
            if NEGATE_RE.search(clausetext):
                negated.append((addr, name, f, n, text))
            elif cname not in defined:
                undefined.append((addr, name, f, n, text))

    print(f"credited functions: {len(rows)}")
    print(f"NEGATED citations (a port claim next to a port denial): {len(negated)}")
    for addr, name, f, n, text in sorted(set(negated)):
        print(f"  {addr} {name}  {f}:{n}")
        print(f"      {text[:150]}")
    print(f"\nUNDEFINED-name candidates (renames make this noisy, read before acting): {len(undefined)}")
    for addr, name, f, n, text in sorted(set(undefined))[:40]:
        print(f"  {addr} {name}  {f}:{n}  {text[:110]}")


if __name__ == "__main__":
    main()
