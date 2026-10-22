#!/usr/bin/env python3
"""Part 123's mutation proofs: the 6 `titles` rows row 121a still had uncited.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part123.py
Every case names the original address whose behaviour the mutated line reproduces.

Three cases are EXPECTED to fail 0 checks and each is a finding, not a bug in the runner: `340 -> 360` (a
window that starts earlier only adds rows that are off screen), a smaller jitter bound (RandU16(5) ->
RandU16(1) leaves the 1x1 fill inside the same probe box), and the palette slot the two titles fills take (on an
8bpp surface SoftSurf_FillRectEx writes the palette INDEX, so the slot cannot change a titles pixel). The 3x3
star size looked inert for the same reason until it was measured: SoftSurf_BlurPixels @ 0xa2000 leaves a
fingerprint two cells past the block's edge, and the cells (110,161) and (106,165) are non-zero for 3x3 and zero
for 3x1, 1x3 and 2x2.
"""
import os
import sys

# `python -I` keeps the script's own directory out of sys.path, so name it explicitly.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

TITLES_C = "src/pufo/game/pufo_titles.c"

CASES = [
    # --- Titles_PresentToSoftSurf @ 0x466dc -------------------------------------------------------
    # 0x46718: the palette argument is the pointer unk_14A2D0 = aPalette + 0x3000, i.e. slot 12.
    ("0x466dc present palette slot 12 -> 0", "test_titles", TITLES_C,
     b"                           PUFO_TITLES_PRESENT_PAL_SLOT);",
     b"                           PUFO_TITLES_FILL_PAL_SLOT); /* MUTATION */"),
    # 0x466EC: SoftSurf_GetAlloc, the raw block whose 4-byte header is u16 width then u16 height.
    # GetPixels hands the glyph parser two index bytes instead of 240 and 320.
    ("0x466dc glyph from GetPixels instead of GetAlloc", "test_titles", TITLES_C,
     b"                           (const u8 *)SoftSurf_GetAlloc(g_pTitlesSurface),",
     b"                           (const u8 *)SoftSurf_GetPixels(g_pTitlesSurface), /* MUTATION */"),

    # --- Titles_DrawCreditLines @ 0x46738 --------------------------------------------------------
    # 0x4678C: (scroll - 340) / 20. A smaller offset starts the window one row late and drops a row that IS
    # on screen (measured: 3 fails). A larger offset starts it one row EARLIER, which only adds rows that are
    # off screen -- the 36-row span already covers every row that fits -- so 340 -> 360 is inert and fails 0.
    # That asymmetry is a property of the original, not a weakness of the checks, and TestCreditWindow's
    # header comment states it.
    # The anchor carries the function header because Titles_DrawBackdrop @ 0x1e630 has the same three lines.
    ("0x46738 window offset 340 -> 320", "test_titles", TITLES_C,
     b"i32 Titles_DrawCreditLines(TitlesBackdrop *pEnv)\n{\n    i32 nFirst;\n    i32 nLast;\n\n    if (pEnv->nScroll >= 320) {\n        nFirst = (pEnv->nScroll - 340) / PUFO_TITLES_ROW_STEP;",
     b"i32 Titles_DrawCreditLines(TitlesBackdrop *pEnv)\n{\n    i32 nFirst;\n    i32 nLast;\n\n    if (pEnv->nScroll >= 320) {\n        nFirst = (pEnv->nScroll - 320) / PUFO_TITLES_ROW_STEP; /* MUTATION */"),
    ("0x46738 window offset 340 -> 360", "test_titles", TITLES_C,
     b"i32 Titles_DrawCreditLines(TitlesBackdrop *pEnv)\n{\n    i32 nFirst;\n    i32 nLast;\n\n    if (pEnv->nScroll >= 320) {\n        nFirst = (pEnv->nScroll - 340) / PUFO_TITLES_ROW_STEP;",
     b"i32 Titles_DrawCreditLines(TitlesBackdrop *pEnv)\n{\n    i32 nFirst;\n    i32 nLast;\n\n    if (pEnv->nScroll >= 320) {\n        nFirst = (pEnv->nScroll - 360) / PUFO_TITLES_ROW_STEP; /* MUTATION */"),
    # 0x46794: the span of 36 rows. It is inert as long as it covers the at-most-17 rows that can fit on
    # the screen, so this case drives it under that limit (see the TestCreditWindow comment).
    ("0x46738 row span 36 -> 16", "test_titles", TITLES_C,
     b"        nLast = nFirst + 36;\n    } else {\n        nFirst = 0;\n        nLast  = pEnv->nScroll / PUFO_TITLES_ROW_STEP;",
     b"        nLast = nFirst + 16; /* MUTATION */\n    } else {\n        nFirst = 0;\n        nLast  = pEnv->nScroll / PUFO_TITLES_ROW_STEP;"),
    # DrawCreditRow's y: PUFO_TITLES_ROW_STEP * (i + PUFO_TITLES_ROW_BASE) - scroll. Mutated here rather
    # than in the macros, so Titles_DrawBackdrop @ 0x1e630 (the twin that shares them) is not disturbed.
    ("0x46738 row base 17 -> 18 for the credits path", "test_titles", TITLES_C,
     b"                      PUFO_TITLES_ROW_STEP * (i + PUFO_TITLES_ROW_BASE) - pEnv->nScroll, 31);",
     b"                      PUFO_TITLES_ROW_STEP * (i + PUFO_TITLES_ROW_BASE + 1) - pEnv->nScroll, 31); /* MUTATION */"),

    # --- Titles_AnimateParticles @ 0x46acc -------------------------------------------------------
    # 0x46B30 / 0x46B48: _rt_sdiv by 15 in both axes.
    ("0x46acc particle step / 15 -> / 16", "test_titles", TITLES_C,
     b"    i32 nSx = nDx / 15;\n    i32 nSy = nDy / 15;",
     b"    i32 nSx = nDx / 16; /* MUTATION */\n    i32 nSy = nDy / 16;"),

    # --- Titles_UpdateStars @ 0x46dbc ------------------------------------------------------------
    # 0x46E20: the wrap test is `> 320`, so a star that lands ON 320 is not reborn in this call.
    ("0x46dbc wrap test > PUFO_TITLES_HEIGHT -> >=", "test_titles", TITLES_C,
     b"        if (pStar->nY > PUFO_TITLES_HEIGHT) {",
     b"        if (pStar->nY >= PUFO_TITLES_HEIGHT) { /* MUTATION */"),
    # 0x46E38: the reborn y is 0.
    ("0x46dbc reborn y 0 -> 1", "test_titles", TITLES_C,
     b"            pStar->nY         = 0;",
     b"            pStar->nY         = 1; /* MUTATION */"),
    # 0x46E2C / 0x46E50 / 0x46E6C: the three bounds, pinned by the seed-777 values.
    ("0x46dbc reborn x bound PUFO_TITLES_W -> PUFO_TITLES_W - 1", "test_titles", TITLES_C,
     b"            pStar->nBaseX     = (i32)Game_RandU16(PUFO_TITLES_W);",
     b"            pStar->nBaseX     = (i32)Game_RandU16(PUFO_TITLES_W - 1); /* MUTATION */"),
    ("0x46dbc reborn speed bound RandU16(3) -> RandU16(2)", "test_titles", TITLES_C,
     b"            pStar->nSpeed     = (i32)Game_RandU16(3) + 1;",
     b"            pStar->nSpeed     = (i32)Game_RandU16(2) + 1; /* MUTATION */"),
    ("0x46dbc reborn amplitude bound RandU16(100) -> RandU16(101)", "test_titles", TITLES_C,
     b"            pStar->nAmplitude = (i32)Game_RandU16(100) + 1;",
     b"            pStar->nAmplitude = (i32)Game_RandU16(101) + 1; /* MUTATION */"),

    # --- Titles_ctor @ 0x46e88 -------------------------------------------------------------------
    # 0x46EF8: state +87220 = 190.
    ("0x46e88 star count 190 -> 189", "test_titles", TITLES_C,
     b"    pEnv->nStarCount = 190; /* 0x46EF8: state +87220 */",
     b"    pEnv->nStarCount = 189; /* MUTATION */"),
    # 0x46F1C / 0x46F3C / 0x46F5C / 0x46F78: the four seed bounds, and the -320 that puts every star
    # above the top edge.
    ("0x46e88 seed x bound 0xF0 -> 0xEF", "test_titles", TITLES_C,
     b"        p->nBaseX     = (i32)Game_RandU16(0xF0u);          /* 0x46F1C */",
     b"        p->nBaseX     = (i32)Game_RandU16(0xEFu); /* MUTATION */"),
    ("0x46e88 seed y offset -320 -> -319", "test_titles", TITLES_C,
     b"        p->nY         = (i32)Game_RandU16(0x140u) - 320;   /* 0x46F3C: -320..-1, above the top edge */",
     b"        p->nY         = (i32)Game_RandU16(0x140u) - 319; /* MUTATION */"),
    ("0x46e88 seed y bound 0x140 -> 0x13F", "test_titles", TITLES_C,
     b"        p->nY         = (i32)Game_RandU16(0x140u) - 320;   /* 0x46F3C: -320..-1, above the top edge */",
     b"        p->nY         = (i32)Game_RandU16(0x13Fu) - 320; /* MUTATION */"),
    ("0x46e88 seed speed bound 3u -> 2u", "test_titles", TITLES_C,
     b"        p->nSpeed     = (i32)Game_RandU16(3u) + 1;         /* 0x46F5C: 1..4 */",
     b"        p->nSpeed     = (i32)Game_RandU16(2u) + 1; /* MUTATION */"),
    ("0x46e88 seed amplitude bound 0x64 -> 0x65", "test_titles", TITLES_C,
     b"        p->nAmplitude = (i32)Game_RandU16(0x64u) + 1;      /* 0x46F78: 1..101 */",
     b"        p->nAmplitude = (i32)Game_RandU16(0x65u) + 1; /* MUTATION */"),

    # --- Titles_DrawAnimatedFrame @ 0x1a83c ------------------------------------------------------
    # The 3x3 star fill (R11 = 3 for the width, the stack slot IDA labels nW holds 3 as the height), the
    # 0..4 jitter and the palette slot of the two fills. The jitter's lower bound and the slot are the
    # expected no-teeth cases; the sizes fail the two blur-fingerprint checks in TestAnimation.
    ("0x1a83c star fill 3x3 -> 3x1", "test_titles", TITLES_C,
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, PUFO_TITLES_STAR_COLS, PUFO_TITLES_STAR_COLS,",
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, PUFO_TITLES_STAR_COLS, 1, /* MUTATION */"),
    ("0x1a83c star fill 3x3 -> 1x3", "test_titles", TITLES_C,
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, PUFO_TITLES_STAR_COLS, PUFO_TITLES_STAR_COLS,",
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, 1, PUFO_TITLES_STAR_COLS, /* MUTATION */"),
    ("0x1a83c star fill 3x3 -> 2x2", "test_titles", TITLES_C,
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, PUFO_TITLES_STAR_COLS, PUFO_TITLES_STAR_COLS,",
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, 2, 2, /* MUTATION */"),
    ("0x1a83c jitter RandU16(5) -> RandU16(1)", "test_titles", TITLES_C,
     b"        SoftSurf_FillRectEx(pSurf, p->nX + (i32)Game_RandU16(5), p->nY + (i32)Game_RandU16(5),",
     b"        SoftSurf_FillRectEx(pSurf, p->nX + (i32)Game_RandU16(1), p->nY + (i32)Game_RandU16(1), /* MUTATION */"),
    ("0x1a83c jitter RandU16(5) -> RandU16(13)", "test_titles", TITLES_C,
     b"        SoftSurf_FillRectEx(pSurf, p->nX + (i32)Game_RandU16(5), p->nY + (i32)Game_RandU16(5),",
     b"        SoftSurf_FillRectEx(pSurf, p->nX + (i32)Game_RandU16(13), p->nY + (i32)Game_RandU16(13), /* MUTATION */"),
    # 0x1A9B0-ish: the two fills take palette slot 0 (MEMORY[0x1472D0]) with index 255.
    ("0x1a83c fill palette slot 0 -> 12", "test_titles", TITLES_C,
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, PUFO_TITLES_STAR_COLS, PUFO_TITLES_STAR_COLS,\n                            PUFO_TITLES_FILL_PAL_SLOT, 255);",
     b"        SoftSurf_FillRectEx(pSurf, nX, pStar->nY, PUFO_TITLES_STAR_COLS, PUFO_TITLES_STAR_COLS,\n                            PUFO_TITLES_PRESENT_PAL_SLOT, 255); /* MUTATION */"),
]

if __name__ == "__main__":
    # An optional substring argument runs one case: python -I docs/parity/mutations_part123.py 2x2
    wanted = sys.argv[1] if len(sys.argv) > 1 else None
    for name, target, path, old, new in CASES:
        if wanted and wanted not in name:
            continue
        run_case(name, target, path, old, new)
