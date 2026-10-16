#!/usr/bin/env python3
"""Generate docs/PARITY.md: the master function -> recreated-code map plus gaps.

Sources
  docs/idb/inventory-grouped.tsv   addr, name, size, subsystem (from the IDB)
  src/**/*.{c,h}                  the "@ 0xADDR" evidence citations
  build/tests/Release/*.exe       run to capture live pass/fail and check counts

The report is generated rather than hand-written so the gap list cannot drift out
of date: every uncovered original function appears with its name and size.

Usage (from the repo root):
    python docs/parity/gen_report.py
"""
import os
import re
import subprocess
import sys
from collections import defaultdict

# The citation rule and the inventory loader come from coverage.py, so the two
# scripts cannot drift apart and PARITY.md quotes the same numbers the summary
# prints. See docs/parity/coverage.py for the definition of "cited".
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import ADDR_RE, CITE_RE, INVENTORY, load_inventory  # noqa: E402

OUT = "PARITY.md"
TESTS_DIR = os.path.join("build", "tests", "Release")
IMAGE_BASE = 0x10000  # the base IDA chose for pufobeta.exe

# Uniform suite summary, printed by TestSummary() in tests/pufo_test.h.
SUMMARY_RE = re.compile(r"^(\w+): (\d+) checks, (\d+) failures$", re.M)

# Subsystem presentation order and a one-line description. The descriptions state
# what is recreated, so a reader can tell coverage apart from intent.
SUBSYSTEMS = [
    ("dat", "tagged archive container (DatFile) and AssetReader"),
    ("boot-assets", "startup asset load, text-table extraction, shutdown"),
    ("render", "software surface, blitters, palette loading"),
    ("audio", "FMOD-backed sound, behind a backend seam"),
    ("util", "growable pointer array, RNG, string helpers"),
    ("app-shell", "WinMain, window class, display bring-up, registry options"),
    ("platform", "WinCE -> Win32 seams (GDI display, registry)"),
    ("save", "save/load chunk read/write"),
    ("tact", "tactical: map cells, entities, pathing, LOS, turns, action queue"),
    ("tact-ui", "tactical HUD, inventory, command pads, input dispatch"),
    ("ui-core", "shared UI panel, text engine, menus"),
    ("strat", "geoscape: globe, radar, ships, bases, strategy list"),
    ("strat-ui", "base/crew/manuf/research/buysell/transfer/options screens"),
    ("titles", "intro/outro animated scenes"),
]

# A reason for the notable gaps, so "no unexplained gaps" is literally true.
GAP_REASONS = {
    "Map_InitAndLoadRegistry": "the tact registry bring-up; scheduled with the last tact item",
    "Map_BuildMissionTerrain": "needs the .ma1 templates (ported) plus Font_LoadTactSprites "
                               "(759 B) and the palette LUTs (Tact_BuildPaletteLuts, 1184 B)",
    "Combat_ResolveShot": "combat resolution; needs arms/ammo and the LOS trace (both ported)",
    "Tact_ProcessFireAction": "the largest fire action; depends on Combat_ResolveShot",
    "Tact_ProcessMoveAction": "the queue consumer for movement; depends on ProcessFallAction",
    "Unit_UpdateVisibility": "3728 B of visibility/spotting; needed by Tact_RefreshTeamVisibility",
    "BeforeEnemyStep": "2792 B of pre-AI-turn bookkeeping; injected as a hook in pufo_turn",
    "AfterEnemyStep": "1368 B of post-AI-turn bookkeeping; injected as a hook in pufo_turn",
    "Tact_EnemyAiTick": "3440 B enemy AI driver; injected as a hook in pufo_turn",
    "TactState_EnqueueFireBurst": "628 B; injected as pfnRequestFire in pufo_turn",
    "Map_LoadRmp": "",  # covered
}

# The UiPanel struct conflict: these belong to a second structure.
UI_PANEL_CONFLICT = {
    "UiPanel_InsertLine", "UiPanel_RemoveLine", "UiPanel_GetLineFlag",
    "UiPanel_GetLineColor", "UiPanel_AppendLineWithSound",
}


# The original's import-stub and CRT-startup block. IDA placed every Win32 API,
# C runtime, MFC operator and fmodce.dll entry point the game calls through as a
# small stub here, followed by the CRT's own start/_cinit/atexit/_onexit chain.
# The port reaches the same behaviour through Win64 Win32 calls, the C standard
# library and the audio backend seam, so these stubs are not recreated as
# functions -- see the constraint in CLAUDE.md about not re-linking original DLLs.
IMPORT_BLOCK_START = 0xA115C
IMPORT_BLOCK_END = 0xA1760

# Names that really are glue: IDA's nullsubs, `j_` jump thunks, MFC mangled import
# names, and the atexit registrar pairs the runtime helpers install. A small
# function with any other name is real game behaviour that simply is not ported
# yet, and the gap note has to say so rather than call it behaviour-free.
GLUE_NAME_RE = re.compile(r"^(nullsub_|j_|\?)|(RegisterAtexit|AtexitThunk|Atexit_nullsub\w*)$")


def thunk_target(name):
    """The callee a `j_`-prefixed jump thunk calls, or None if it is not a thunk.

    IDA names a one-instruction `B target` thunk `j_<target>`, and a thunk to a thunk
    `j_j_<target>`. Stripping the prefixes gives the real callee."""
    if not name.startswith("j_"):
        return None
    target = name[2:]
    while target.startswith("j_"):
        target = target[2:]
    return target or None


def load_citations():
    """addr -> sorted list of source files citing it.

    One citation may name a family of addresses (`@ 0x17790/0x177fc/0x17868`),
    so the whole run is split and every address in it counts.
    """
    cites = defaultdict(set)
    for root, _dirs, files in os.walk("src"):
        for fn in files:
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(root, fn).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as f:
                for m in CITE_RE.finditer(f.read()):
                    for a in ADDR_RE.finditer(m.group(1)):
                        cites["0x" + a.group(1).lower()].add(path)
    return {k: sorted(v) for k, v in cites.items()}


def count_checks(out):
    """Checks a suite ran, from its summary line, or from its per-check lines.

    The summary line is the contract (tests/pufo_test.h). The line count is the
    fallback for a suite that has not been moved to the shared harness yet.
    """
    m = SUMMARY_RE.search(out)
    if m:
        return int(m.group(2))
    return sum(1 for ln in out.splitlines()
               if ln.startswith("  ok   ") or ln.startswith("  FAIL "))


def run_tests():
    results = []
    if not os.path.isdir(TESTS_DIR):
        return results
    for fn in sorted(os.listdir(TESTS_DIR)):
        if not fn.endswith(".exe"):
            continue
        path = os.path.join(TESTS_DIR, fn)
        try:
            p = subprocess.run([path], capture_output=True, text=True, timeout=120)
            out = p.stdout
            checks = count_checks(out)
        except Exception as exc:  # noqa: BLE001
            results.append((fn[:-4], False, 0, str(exc)))
            continue
        name = fn[:-4]
        # Retry from the repo root: some tests need the archives.
        if p.returncode != 0:
            try:
                p = subprocess.run([os.path.abspath(path)], capture_output=True,
                                   text=True, timeout=120, cwd=".")
                out = p.stdout
                checks = count_checks(out)
            except Exception:  # noqa: BLE001
                pass
        results.append((name, p.returncode == 0, checks, ""))
    return results


def main():
    if not os.path.exists(INVENTORY):
        sys.exit("missing %s; run docs/idb/group_inventory.py first" % INVENTORY)
    inv = load_inventory()
    cites = load_citations()
    tests = run_tests()

    covered = {a for a in cites if a in inv}
    unknown = sorted(a for a in cites if a not in inv)

    by_sub = defaultdict(lambda: {"cov": [], "gap": []})
    for addr, (name, size, sub) in inv.items():
        rec = (addr, name, size, cites.get(addr, []))
        if addr in covered:
            by_sub[sub]["cov"].append(rec)
        else:
            by_sub[sub]["gap"].append(rec)

    total_bytes = sum(v[1] for v in inv.values())
    cov_bytes = sum(inv[a][1] for a in covered)

    L = []
    A = L.append
    A("# Parity report — PocketUFO Win64 recreation")
    A("")
    A("Generated by `docs/parity/gen_report.py`. Do not edit by hand.")
    A("")
    A("Source of truth: `pufobeta.exe.i64` (WinCE/ARM32, 1103 functions) via IDA Pro MCP.")
    A("")
    A("## How to verify this report")
    A("")
    A("```bash")
    A("cmake -S . -B build -G \"Visual Studio 17 2022\" -A x64 -T ClangCL")
    A("cmake --build build --config Release     # must be 0 errors, 0 warnings")
    A("python docs/parity/gen_report.py          # regenerates this file")
    A("python docs/parity/coverage.py            # coverage numbers only")
    A("```")
    A("")
    A("## Coverage")
    A("")
    A("| Measure | Value |")
    A("|---|---|")
    A("| Functions cited as evidence | %d / %d (%.1f%%) |"
      % (len(covered), len(inv), 100.0 * len(covered) / len(inv)))
    A("| Bytes of original code cited | %d / %d (%.1f%%) |"
      % (cov_bytes, total_bytes, 100.0 * cov_bytes / total_bytes))
    A("| Subsystems in the IDB map | %d |" % len(SUBSYSTEMS))
    A("| Test suites | %d |" % len(tests))
    A("| Test checks run | %d |" % sum(c for _n, _ok, c, _e in tests))
    A("")
    A("A citation means the port names the original function it answers to in a")
    A("source comment of the form `@ 0xADDR`. It answers \"is this original function")
    A("accounted for?\", not \"is the port correct?\" — correctness evidence lives in")
    A("the per-subsystem notes and the tests listed at the end.")
    A("")

    A("## Per-subsystem status")
    A("")
    A("| Subsystem | Recreated | Total | Bytes covered | What it is |")
    A("|---|---|---|---|---|")
    for sub, desc in SUBSYSTEMS:
        d = by_sub.get(sub)
        if d is None:
            continue
        n_cov = len(d["cov"])
        n_tot = n_cov + len(d["gap"])
        b_cov = sum(r[2] for r in d["cov"])
        b_tot = b_cov + sum(r[2] for r in d["gap"])
        A("| `%s` | %d | %d | %d / %d | %s |" % (sub, n_cov, n_tot, b_cov, b_tot, desc))
    A("")

    A("## Recreated functions -> source file")
    A("")
    A("Every original function the port cites, grouped by subsystem. The file column")
    A("is the module that answers to it.")
    A("")
    for sub, _desc in SUBSYSTEMS:
        d = by_sub.get(sub)
        if d is None or not d["cov"]:
            continue
        A("### `%s` (%d functions)" % (sub, len(d["cov"])))
        A("")
        A("| Address | Original | Size | Recreated in |")
        A("|---|---|---|---|")
        for addr, name, size, files in sorted(d["cov"], key=lambda r: -r[2]):
            shown = ", ".join("`%s`" % f for f in files[:2])
            if len(files) > 2:
                shown += " (+%d)" % (len(files) - 2)
            A("| `%s` | `%s` | %d | %s |" % (addr, name, size, shown))
        A("")

    A("## Remaining gaps")
    A("")
    A("Every original function not yet cited, with its size and an explanation,")
    A("so there are no unexplained omissions. Small thunks and atexit")
    A("registrations are named as such; the rest are attributed to the subsystem")
    A("whose port work they belong to.")
    A("")
    # Names the port already cites, so a `j_` thunk can be attributed to its target
    # instead of being called a thunk that carries no behaviour.
    ported_names = {inv[a][0] for a in covered}
    all_names = {v[0] for v in inv.values()}

    for sub, _desc in SUBSYSTEMS:
        d = by_sub.get(sub)
        if d is None or not d["gap"]:
            continue
        gap_bytes = sum(r[2] for r in d["gap"])
        A("### `%s` — %d functions, %d bytes remaining"
          % (sub, len(d["gap"]), gap_bytes))
        A("")
        A("| Address | Original | Size | Note |")
        A("|---|---|---|---|")
        for addr, name, size, _files in sorted(d["gap"], key=lambda r: -r[2]):
            target = thunk_target(name)
            if name in UI_PANEL_CONFLICT:
                note = ("belongs to a **second struct** merged under the name "
                        "`UiPanel`; see docs/parity/ui-core.md")
            elif name in GAP_REASONS and GAP_REASONS[name]:
                note = GAP_REASONS[name]
            elif target is not None and target in ported_names:
                note = ("jump thunk to `%s`, which is ported; the port calls the target "
                        "directly, so the thunk wrapper has no behaviour of its own" % target)
            elif target is not None and target in all_names:
                note = ("jump thunk to `%s`; the thunk itself is a call-site wrapper and "
                        "follows that function's port work" % target)
            elif IMPORT_BLOCK_START <= int(addr, 16) <= IMPORT_BLOCK_END:
                note = ("import stub in the original's import/CRT-startup block "
                        "(`0x%X..0x%X`): the Win32 API, C runtime, MFC operator or fmodce.dll "
                        "entry the game calls through. The port reaches the same behaviour "
                        "through Win64 Win32 calls, the C standard library and the audio "
                        "backend seam, so the stub is not recreated as a function"
                        % (IMPORT_BLOCK_START, IMPORT_BLOCK_END))
            elif size <= 20 and GLUE_NAME_RE.search(name):
                note = ("glue only: a nullsub, jump thunk, MFC import stub or atexit "
                        "registrar; carries no game behaviour to recreate")
            else:
                note = ("not yet ported; scheduled with the `%s` subsystem work" % sub)
            A("| `%s` | `%s` | %d | %s |" % (addr, name, size, note))
        A("")

    A("## Test suites")
    A("")
    A("Run from the repository root (the data tests read `main.dat` / `lang.dat`).")
    A("")
    A("| Suite | Result | Checks |")
    A("|---|---|---|")
    n_pass = 0
    for name, ok, checks, err in tests:
        A("| `%s` | %s | %d |" % (name, "PASS" if ok else "FAIL: " + err, checks))
        if ok:
            n_pass += 1
    A("")
    A("**%d / %d suites passing.**" % (n_pass, len(tests)))
    A("")

    A("## Per-subsystem parity notes")
    A("")
    A("Each note records the original function addresses, the verified behaviour,")
    A("every deviation with its reason, and the residual uncertainty.")
    A("")
    for note in ("scaffold", "data-layer", "render-audio", "game-core", "ui-core"):
        path = os.path.join("docs", "parity", note + ".md")
        if os.path.exists(path):
            A("- `%s`" % path)
    A("")

    if unknown:
        # IDA loaded this image at 0x10000, so a citation below that cannot be an
        # address in the database at all: it is an offset inside a named block.
        below = [a for a in unknown if int(a, 16) < IMAGE_BASE]
        at_or_above = [a for a in unknown if int(a, 16) >= IMAGE_BASE]
        A("## Cited addresses that are not function starts")
        A("")
        A("`@ 0xADDR` is used in this port for two different things: an address in the")
        A("IDB, and an offset inside a named block (`nLosX0 @ 0x14004` is `Map+0x14004`).")
        A("Only the first kind can be matched against the function inventory, so the")
        A("rest are listed here instead of counted. Splitting them is part of the final")
        A("parity report. Total: %d citations." % len(unknown))
        A("")
        A("### Offsets below the image base (%d)" % len(below))
        A("")
        A("IDA loaded `pufobeta.exe` at `0x10000`, so no original code or data lives")
        A("below it. Every one of these is a field offset inside a struct.")
        A("")
        A(", ".join("`%s`" % a for a in below))
        A("")
        A("### At or above the image base (%d)" % len(at_or_above))
        A("")
        A("Two kinds are mixed here and the report cannot tell them apart on its own:")
        A("global data symbols the port cites as blocks (the `Map` block at `0x1139b8`,")
        A("`aTextRecs` at `0xb38d0`, the muzzle table at `0xa9768`), and block-relative")
        A("offsets that happen to be numerically large (`Map+0x14004`). Triage them")
        A("against the IDB when the final report is assembled.")
        A("")
        for a in at_or_above:
            A("- `%s`" % a)
        A("")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    print("wrote %s (%d lines)" % (OUT, len(L)))
    print("coverage: %d/%d functions (%.1f%%), %d/%d bytes (%.1f%%)"
          % (len(covered), len(inv), 100.0 * len(covered) / len(inv),
             cov_bytes, total_bytes, 100.0 * cov_bytes / total_bytes))
    print("tests: %d/%d passing" % (n_pass, len(tests)))


if __name__ == "__main__":
    main()
