#!/usr/bin/env python3
"""audit_bugs.py - keep docs/parity/bugs.tsv in step with the port.

Two checks.

1. Every `ORIGINAL BUG` marker in src/pufo must have a row in the ledger. A marker without a row
   means a bug was found while porting and never made it onto the QoL list.
2. Every ledger row that names a port location must point at a file that exists, and at a function
   that is actually defined there. A row that no longer resolves means the code moved or the
   reproduction was dropped.

Run:  python docs/parity/audit_bugs.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LEDGER = os.path.join(ROOT, "docs", "parity", "bugs.tsv")
SRC = os.path.join(ROOT, "src", "pufo")

ADDR_RE = re.compile(r"0x[0-9a-fA-F]{4,6}")
MARKER_RE = re.compile(r"ORIGINAL BUG")
LEDGER_COLUMNS = (
    "addr", "original_name", "kind", "what_the_original_does", "evidence",
    "port_location", "reproduced", "qol",
)
REPRODUCED_VALUES = {"yes", "no", "n/a", "unverified"}
QOL_VALUES = {"yes", "no", "n/a", "unassessed"}


def read_ledger(path=LEDGER):
    rows = []
    errors = []
    header_seen = False
    with open(path, encoding="utf-8") as fh:
        for line_number, raw_line in enumerate(fh, 1):
            line = raw_line.rstrip("\r\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.split("\t")
            if cols[0] == "addr":
                if cols != list(LEDGER_COLUMNS):
                    errors.append(
                        f"{path}:{line_number}: malformed header; expected columns "
                        f"{', '.join(LEDGER_COLUMNS)}"
                    )
                elif header_seen:
                    errors.append(f"{path}:{line_number}: duplicate ledger header")
                header_seen = True
                continue
            if len(cols) != len(LEDGER_COLUMNS):
                errors.append(
                    f"{path}:{line_number}: expected exactly {len(LEDGER_COLUMNS)} "
                    f"tab-separated fields, found {len(cols)}"
                )
                continue
            empty = [LEDGER_COLUMNS[i] for i, value in enumerate(cols) if not value.strip()]
            if empty:
                errors.append(
                    f"{path}:{line_number}: empty required field(s): {', '.join(empty)}"
                )
                continue
            reproduced = cols[6].strip()
            if reproduced not in REPRODUCED_VALUES:
                errors.append(
                    f"{path}:{line_number}: invalid reproduced value {reproduced!r}; "
                    f"expected one of {', '.join(sorted(REPRODUCED_VALUES))}"
                )
                continue
            qol = cols[7].split(" -- ", 1)[0].strip()
            if qol not in QOL_VALUES:
                errors.append(
                    f"{path}:{line_number}: invalid qol value {qol!r}; "
                    f"expected one of {', '.join(sorted(QOL_VALUES))}"
                )
                continue
            rows.append(
                {
                    "addr": cols[0].lower(),
                    "name": cols[1],
                    "kind": cols[2],
                    "port": cols[5].strip(),
                    "reproduced": reproduced,
                }
            )
    if not header_seen:
        errors.append(f"{path}: missing {len(LEDGER_COLUMNS)}-column header")
    return rows, errors


def source_markers():
    """One entry per ORIGINAL BUG marker: where it is, and every address named near it.

    A marker often names a data address or a store address rather than the canonical address for
    the finding, so a marker counts as listed when ANY address in its window is in the ledger.
    """
    found = []
    for dirpath, _dirs, files in os.walk(SRC):
        for name in files:
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8", errors="replace") as fh:
                lines = fh.readlines()
            for i, line in enumerate(lines):
                if not MARKER_RE.search(line):
                    continue
                window = "".join(lines[max(0, i - 2): i + 4])
                rel = os.path.relpath(path, ROOT).replace("\\", "/")
                found.append(
                    {
                        "where": f"{rel}:{i + 1}",
                        "addrs": {a.lower() for a in ADDR_RE.findall(window)},
                    }
                )
    return found


def main():
    if not os.path.exists(LEDGER):
        print(f"missing ledger: {LEDGER}")
        return 1

    rows, schema_errors = read_ledger()
    if schema_errors:
        print(f"\nLEDGER SCHEMA ERRORS: {len(schema_errors)}")
        for error in schema_errors:
            print(f"  {error}")
        return 1
    ledger_addrs = {r["addr"] for r in rows}
    markers = source_markers()
    problems = 0

    print(f"ledger rows: {len(rows)}")
    print(f"ORIGINAL BUG markers in src/pufo: {len(markers)}")

    unlisted = [m for m in markers if not (m["addrs"] & ledger_addrs)]
    if unlisted:
        problems += len(unlisted)
        print(f"\nBUG MARKERS WITH NO LEDGER ROW: {len(unlisted)}")
        for m in unlisted:
            named = ", ".join(sorted(m["addrs"])) or "no address named"
            print(f"  {m['where']}  names {named}")

    broken = []
    for row in rows:
        port = row["port"]
        if port in ("not ported", "n/a", ""):
            continue
        path = port.split(":")[0]
        func = port.split(":", 1)[1] if ":" in port else None
        full = os.path.join(ROOT, path)
        if not os.path.exists(full):
            broken.append((row["addr"], port, "file missing"))
            continue
        if func:
            with open(full, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            if not re.search(r"\b" + re.escape(func) + r"\s*\(", text):
                broken.append((row["addr"], port, "function not defined there"))
    if broken:
        problems += len(broken)
        print(f"\nLEDGER ROWS THAT DO NOT RESOLVE: {len(broken)}")
        for addr, port, why in broken:
            print(f"  {addr}  {port}  ({why})")

    if not problems:
        print("\nledger and port agree: every marker is listed and every row resolves")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
