#!/usr/bin/env python3
"""Part 139 rerun: the eight pufo_baseui.c cases the first run skipped.

Why they skipped: docs/parity/mutations_part139.py split the file on CRLF, but src/pufo/game/pufo_baseui.c is
LF-only. Every "line" was then the whole file, the single-line anchors survived only as substring matches (and
run_case then found 0 copies of the CRLF-ified text) and the block anchors died on the `^` anchor. This version
reads the file's own end-of-line marker instead of assuming one.

Run from the repo root:  python -I docs/parity/mutations_part139b.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

BASEUI = "src/pufo/game/pufo_baseui.c"


def read_lines(path):
    data = open(path, "rb").read()
    eol = b"\r\n" if b"\r\n" in data else b"\n"
    return [l.decode("utf-8") for l in data.split(eol)], eol


def case(label, test, path, pattern, repl):
    ls, eol = read_lines(path)
    for line in ls:
        if re.search(pattern, line):
            new = re.sub(pattern, repl, line, count=1)
            if new == line:
                print(f"SKIP {label}: substitution changed nothing")
                return
            run_case(label, test, path, line.encode("utf-8"), new.encode("utf-8"))
            return
    print(f"SKIP {label}: anchor {pattern!r} not found in {path}")


def thunk_case(label, test, path, head, repl_body):
    ls, eol = read_lines(path)
    for i, line in enumerate(ls):
        if re.search(head, line):
            body = ls[i + 2]
            if body.strip() != "Geoscape_CloseUI(pEnv);":
                print(f"SKIP {label}: body of {head!r} is {body!r}")
                return
            old = eol.join(s.encode("utf-8") for s in ls[i:i + 4])
            new = eol.join(s.encode("utf-8") for s in (ls[i], ls[i + 1], repl_body, ls[i + 3]))
            run_case(label, test, path, old, new)
            return
    print(f"SKIP {label}: head {head!r} not found in {path}")


DROP = "    (void)pEnv; /* MUTATION: the tail call is dropped */"

thunk_case("FundingScreen_CloseThunk @ 0x5abd8 skips the dispatcher", "test_baseui", BASEUI,
           r"^void FundingScreen_CloseThunk\(", DROP)
thunk_case("FundingScreen_ExitThunk @ 0x5c078 skips the dispatcher", "test_baseui", BASEUI,
           r"^void FundingScreen_ExitThunk\(", DROP)
thunk_case("Geoscape_CloseUI_ThunkA @ 0x838d0 skips the dispatcher", "test_baseui", BASEUI,
           r"^void Geoscape_CloseUI_ThunkA\(", DROP)
thunk_case("Geoscape_CloseUI_ThunkB @ 0x96020 skips the dispatcher", "test_baseui", BASEUI,
           r"^void Geoscape_CloseUI_ThunkB\(", DROP)
thunk_case("Geoscape_CloseUI_ThunkC @ 0x9a8c0 skips the dispatcher", "test_baseui", BASEUI,
           r"^void Geoscape_CloseUI_ThunkC\(", DROP)

case("Funding_ScoreToBarLevel @ 0x5aa60 prices an up-step at 30000", "test_baseui", BASEUI,
     r"case  1: nAmount =  25000; break;", "case  1: nAmount =  30000; /* MUTATION */")
case("FundingScreen_Init @ 0x5c084 puts the close control at x 223", "test_baseui", BASEUI,
     r"pEnv->pCloseCtl->x = 224;                 /\* 0x5C0E0 \*/",
     "pEnv->pCloseCtl->x = 223; /* MUTATION */")
case("FundingScreen_Toggle @ 0x5abe4 leaves the open flag clear", "test_baseui", BASEUI,
     r"\*pEnv->pbOpen = 1;                              /\* 0x5AC00 \*/",
     "*pEnv->pbOpen = 0; /* MUTATION */")
case("FundingScreen_Open @ 0x5b560 wires the close control at x 223", "test_baseui", BASEUI,
     r"pEnv->pCloseCtl->x = 224;                             /\* 0x5B574 \*/",
     "pEnv->pCloseCtl->x = 223; /* MUTATION */")
case("FundingScreen_Draw @ 0x5b1fc draws while the screen is closed", "test_baseui", BASEUI,
     r"if \(pEnv == NULL \|\| pEnv->pbOpen == NULL \|\| \*pEnv->pbOpen == 0\) \{",
     "if (pEnv == NULL || pEnv->pbOpen == NULL || *pEnv->pbOpen == 1) { /* MUTATION: 0x5B210 gate inverted */")
case("FundingScreen_DrawExtended @ 0x5b59c draws the header uncentred", "test_baseui", BASEUI,
     r"Text_DrawString\(120 - nWidth / 2, 5, szHead\);",
     "Text_DrawString(120, 5, szHead); /* MUTATION: the centring at 0x5B7D4 is gone */")
