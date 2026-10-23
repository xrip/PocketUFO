#!/usr/bin/env python3
"""teeth_ledger.py - which original functions a test actually reaches.

For every behaviour-bearing function in docs/idb/inventory-grouped.tsv (the inventory minus
docs/idb/image-artifacts.tsv), record two things: whether src/ cites its address (the same
CITE_RE rule docs/parity/coverage.py uses, so the two cannot disagree) and whether any tests/*.c
file reaches it -- either by citing that original address or by naming the port function.

Output: docs/parity/teeth.tsv, one row per function, plus a per-subsystem table on stdout.

Reach is measured three ways, because the port often renames: a test cites the original address, a test
calls the original name, or a test calls the port function that owns that address. The last one comes
from a pass over src/: a definition or declaration line and the `/* Name @ 0xaddr ... */` comment directly
above it, which is the convention every module uses. Without it a renamed function reads as unreached.
The call test reads the tests with their comments removed: a name in a comment is a note, not a check.

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

# A definition or declaration starts at column 0 (the repo's style) with `name(`, with an optional
# type prefix. Column 0 is what keeps an indented body call out of this list. The signature may run
# over several lines -- pufo_linelist.c:22 Line_Write and pufo_render.h:164 SoftSurf_BlitFlipEx both
# do -- so only the first line is matched and the name is taken from it.
HEAD_RE = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_ \*]*\b)?([A-Za-z_][A-Za-z0-9_]*)\s*\(")
SKIP_START = ("typedef", "#", "return", "else", "case", "goto", "static_assert")

# A function-pointer table head, `static void (*const g_aTactBankClicks[N])(void) = {`, matches HEAD_RE
# with `void` captured: the `[` stops the type-prefix class, and `void` is what sits before ` (`. Every
# cite in such a table was then owned by a function named `void` (Part 151, PLAN 123a). A keyword is
# never an owner, and the true owner of a table cite is the handler named on the entry's own line.
HEAD_KEYWORDS = {"void", "int", "char", "unsigned", "signed", "short", "long", "float", "double",
                 "static", "const", "struct", "enum", "union", "bool", "sizeof", "auto", "NULL"}
TABLE_HEAD_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_ \*]*\(\*(?:const\s+)?\w+\s*\[[^\]]*\]\)\s*"
                           r"\([^)]*\)\s*=\s*\{")
TABLE_ENTRY_RE = re.compile(r"^\s+([A-Za-z_][A-Za-z0-9_]*)\s*,?\s")


def commentish(s):
    return s.startswith(("/*", "*/", "*", "//"))


STOP_CALLS = {"if", "for", "while", "switch", "sizeof", "return", "else", "do", "case",
              "defined", "offsetof", "_Static_assert", "static_assert"}


def load_port_names():
    """original address -> the port functions that own it, and the port call graph.

    A function owns an address when the address is cited either in its `/* Name @ 0x... */` header or
    anywhere inside its body -- the way every module writes parity notes. An address cited by two
    functions maps to both, and a test naming either one counts as reach.

    Declarations in .h count too, with the comment block directly above them: several modules state the
    pairing only at the declaration (pufo_linelist.h:79 for UiPanel_GetLineColor @ 0x5a33c,
    pufo_geoscape.h:182 for Base_GetItemQtyA/B @ 0x66198/0x661b8). Without that a renamed port function
    reads as unreached even when a test calls it by name. A .h file's whole-file `Evidence:` block is not
    contiguous with a declaration, so it stays unattributed.

    The second return value maps each port definition to the port functions its body calls, which is how
    a function no test names directly still counts as reached: Path_ExpandCell is static inside
    Path_ComputeReachable, DatFile_dtor runs inside DatFile_Close, Titles_ctor runs inside Titles_Init.
    """
    addr_to_port = {}
    defs = {}

    def own(addr, name):
        key = "0x%x" % int(addr, 16)
        if name not in addr_to_port.setdefault(key, []):
            addr_to_port[key].append(name)

    def owns_cites(line, name):
        for a in cv.ADDR_RE.finditer(line):
            own(a.group(1), name)

    for root, _dirs, files in os.walk("src/pufo"):
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            with open(os.path.join(root, fn), encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
            i = 0
            while i < len(lines):
                line = lines[i]
                if not line or line[0] == " " or line.startswith(SKIP_START):
                    i += 1
                    continue
                if TABLE_HEAD_RE.match(line):
                    k = i + 1
                    while k < len(lines) and not lines[k].lstrip().startswith("}"):
                        em = TABLE_ENTRY_RE.match(lines[k])
                        if em and em.group(1) not in HEAD_KEYWORDS:
                            for mm in cv.CITE_RE.finditer(lines[k]):
                                for a in cv.ADDR_RE.finditer(mm.group(1)):
                                    own(a.group(1), em.group(1))
                        k += 1
                    i = k + 1
                    continue
                m = HEAD_RE.match(line)
                if not m:
                    i += 1
                    continue
                name = m.group(1)
                if name in HEAD_KEYWORDS:
                    i += 1
                    continue
                j = i - 1
                while j >= 0:
                    s = lines[j].strip()
                    if not commentish(s):
                        # A blank line inside a doc block must not cut it off: pufo_enemyai.h has
                        # `@ 0x4b928` at line 42 and the declaration at line 59, with blank comment
                        # lines in between. Skip the blank only when the line above is still a comment.
                        if s == "" and j > 0 and commentish(lines[j - 1].strip()):
                            j -= 1
                            continue
                        break
                    for mm in cv.CITE_RE.finditer(lines[j]):
                        for a in cv.ADDR_RE.finditer(mm.group(1)):
                            own(a.group(1), name)
                    j -= 1
                owns_cites(line, name)
                # Walk the signature to its terminator: "{" means a definition, ";" a declaration.
                k = i
                body = False
                while k < len(lines):
                    if "{" in lines[k]:
                        body = True
                        break
                    if lines[k].rstrip().endswith(";"):
                        break
                    k += 1
                if not body:
                    i = k + 1 if k > i else i + 1
                    continue
                depth = 0
                started = False
                body = []
                while k < len(lines):
                    line = lines[k]
                    body.append(line)
                    depth += line.count("{") - line.count("}")
                    if "{" in line:
                        started = True
                    owns_cites(line, name)
                    k += 1
                    if started and depth <= 0:
                        break
                callees = {c for c in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", "\n".join(body))
                           if c != name and c not in STOP_CALLS}
                defs.setdefault(name, set()).update(callees)
                i = k if started else i + 1
    return addr_to_port, defs


inv = cv.load_inventory()
artifacts = cv.load_artifacts()
cited = cv.load_citations()
port_names, port_defs = load_port_names()

BLOCK_RE = re.compile(r"/\*.*?\*/", re.S)
LINE_RE = re.compile(r"//[^\n]*")


def strip_comments(text):
    """Test text with the comments removed, so a name counts only where it is called.

    Reach started as "a test names the function", which a comment satisfies. It should not: a note that
    mentions a function proves nothing about a check. After this, a name reaches only as `name(`.
    """
    return LINE_RE.sub("", BLOCK_RE.sub("", text))


test_texts = {}
test_addrs = set()
for path in sorted(glob.glob("tests/*.c")):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    for m in cv.CITE_RE.finditer(text):  # an address cite is deliberate parity evidence, comment or not
        for a in cv.ADDR_RE.finditer(m.group(1)):
            test_addrs.add("0x" + a.group(1).lower())
    test_texts[path] = strip_comments(text)

# Every name that appears in call position in any test, collected in one pass. Doing this per name
# instead (one regex scan of every test file per candidate) costs minutes on these file sizes.
TEST_CALLS = set()
for _t in test_texts.values():
    TEST_CALLS.update(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", _t))


def called_in_tests(name):
    """Does a test call this function directly? Comments do not count (test_texts is comment-free)."""
    return bool(name) and name in TEST_CALLS


# Reach through the port's own call graph: every port function a test calls directly is a root, and
# anything a root reaches through its body is reached too. This is what closes the static internals
# (Path_ExpandCell inside Path_ComputeReachable, DatFile_dtor inside DatFile_Close, Line_Write inside
# UiLineList_Insert) that no test can name directly.
# A name a test defines for itself is not the port function, even if the call looks identical.
test_local_defs = set()
for text in test_texts.values():
    for line in text.splitlines():
        m = HEAD_RE.match(line)
        if m and not line.startswith(SKIP_START):
            test_local_defs.add(m.group(1))

roots = {n for n in port_defs if called_in_tests(n) and n not in test_local_defs}
reached_defs = set(roots)
frontier = list(roots)
while frontier:
    n = frontier.pop()
    for c in port_defs.get(n, ()):
        if c in port_defs and c not in reached_defs:
            reached_defs.add(c)
            frontier.append(c)

rows = []
n_direct = n_transit = 0
for key, (name, size, sub) in inv.items():
    if key in artifacts:
        continue  # no game behaviour of its own; the artifact ledger covers it
    ports = [p for p in port_names.get(key, []) if p != name]
    addr_hit = key in test_addrs
    direct = called_in_tests(name) or any(called_in_tests(p) for p in ports)
    transit = direct or name in reached_defs or any(p in reached_defs for p in ports)
    reached = addr_hit or transit
    if reached:
        if direct or addr_hit:
            n_direct += 1
        else:
            n_transit += 1
    rows.append((key, name, size, sub, key in cited, addr_hit, reached, "|".join(ports), direct))

rows.sort(key=lambda r: (r[3], int(r[0], 16)))

out = ["# addr\tname\tsize\tsubsystem\tcited_in_src\ttest_cites_addr\ttest_names_it\tport_function\ttest_calls_directly",
       "# Generated by docs/parity/teeth_ledger.py from docs/idb/inventory-grouped.tsv,",
       "# docs/idb/image-artifacts.tsv, src/ definition headers and tests/*.c.",
       "# test_names_it: a test reaches it -- either it calls the original name or the port function that owns",
       "# the address directly, or a function a test calls reaches it through the port call graph.",
       "# test_calls_directly separates the two. A name inside a test comment does not count; an address cite does.",
       "# Reach only: the code runs under a test, which is not the same as a check pinning its value."]
for r in rows:
    out.append("\t".join([r[0], r[1], str(r[2]), r[3], "yes" if r[4] else "no",
                          "yes" if r[5] else "no", "yes" if r[6] else "no", r[7] or "",
                          "yes" if r[8] else "no"]))
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

print(f"reached split: {n_direct} by a direct test call or an address cite, "
      f"{n_transit} only through the port call graph ({len(roots)} roots)")

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
