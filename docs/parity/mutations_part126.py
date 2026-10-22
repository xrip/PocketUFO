#!/usr/bin/env python3
"""Part 126's mutation proofs: the 19 `app-shell` rows row 121a still had uncited.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part126.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part126.py KillBySpeed

Rows: KillTimers_A 0x9e6a4, StartTimers_A 0x9e6d4, StartTimers_B 0x9e714, KillTimers_B 0x9e754,
StartTimers_C 0x9e784, KillTimers_C 0x9e7c4, StartTimers_D 0x9e7f4, KillTimers_D 0x9e864,
KillTimers_BySpeed 0x9e8b4, StartTimers_BySpeed 0x9e904, ??0CDialogTemplate 0x54c3c,
??1CDialogTemplate 0x54c48, CDialogTemplate_GetTemplateByte0 0x54c68, GetTemplateByte1 0x54c5c,
GetLevelCount 0x54c74, GetCellByte 0x54c80, Reg_SetString 0x9ed0c, Reg_GetString 0x9edbc,
App_QuitDisplay 0xa0388.

Unlike the earlier Parts, anchors here are multi-line as often as not, and `src/` is CRLF, so `case()` below
translates a "\n" in the anchor into the file's own line ending before handing it to run_case -- otherwise a
multi-line anchor silently matches nothing and the runner prints SKIP.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

T   = "src/pufo/game/pufo_timer.c"
TH  = "src/pufo/game/pufo_timer.h"
TPL = "src/pufo/game/pufo_template.c"
REG = "src/pufo/platform/pufo_reg.c"
APP = "src/pufo/app/pufo_app.c"

CASES = []


def case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") - data.count(b"\r\n") else b"\n"
    CASES.append((name, target, path, old.replace("\n", "\r\n" if eol == b"\r\n" else "\n").encode(),
                  new.replace("\n", "\r\n" if eol == b"\r\n" else "\n").encode(), show))


# ------------------------------------------------------------------ the timer set (10 rows)

case("0x9e6d4 StartTimers_A render interval -1ms", "test_timer", T,
     "    Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_RENDER);",
     "    Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_RENDER - 1u); /* MUTATION */")

case("0x9e6d4 StartTimers_A render id swapped to the pulse", "test_timer", T,
     "    Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_RENDER);",
     "    Timer_StartOne(PUFO_TIMER_UIPULSE, PUFO_TIMER_MS_RENDER); /* MUTATION */")

case("0x9e6a4 KillTimers_A kills the pulse instead of the anim", "test_timer", T,
     "void Timer_KillA(void)\n{\n    Timer_KillOne(PUFO_TIMER_RENDER);\n    Timer_KillOne(PUFO_TIMER_ANIM);\n}",
     "void Timer_KillA(void)\n{\n    Timer_KillOne(PUFO_TIMER_RENDER);\n    Timer_KillOne(PUFO_TIMER_UIPULSE); /* MUTATION */\n}")

case("0x9e6a4 KillTimers_A also kills 0xC350", "test_timer", T,
     "void Timer_KillA(void)\n{\n    Timer_KillOne(PUFO_TIMER_RENDER);\n    Timer_KillOne(PUFO_TIMER_ANIM);\n}",
     "void Timer_KillA(void)\n{\n    Timer_KillOne(PUFO_TIMER_RENDER);\n    Timer_KillOne(PUFO_TIMER_ANIM);\n    Timer_KillOne(PUFO_TIMER_GAMETICK); /* MUTATION */\n}")

case("0x9e7f4 StartTimers_D zero interval becomes 1", "test_timer", T,
     "    Timer_StartOne(PUFO_TIMER_RENDER, 0);",
     "    Timer_StartOne(PUFO_TIMER_RENDER, 1u); /* MUTATION: 0x9E808's MOV R2,#0 */")

case("0x9e7f4 StartTimers_D pulse interval 20 -> 50", "test_timer", TH,
     "#define PUFO_TIMER_MS_D_PULSE  20u",
     "#define PUFO_TIMER_MS_D_PULSE  50u /* MUTATION */")

case("0x9e864 KillTimers_D drops the 0xC354 kill", "test_timer", T,
     "    Timer_KillOne(PUFO_TIMER_UIPULSE);\n    Timer_KillOne(PUFO_TIMER_EXTRA);\n}",
     "    Timer_KillOne(PUFO_TIMER_UIPULSE);\n}")

case("0x9e8b4 KillTimers_BySpeed kills 0xC354 first", "test_timer", T,
     "    Timer_KillOne(PUFO_TIMER_GAMETICK);\n    Timer_KillOne(PUFO_TIMER_RENDER);",
     "    Timer_KillOne(PUFO_TIMER_EXTRA); /* MUTATION: 0x9E8C8 kills 0xC354 */\n    Timer_KillOne(PUFO_TIMER_RENDER);")

case("0x9e904 speed 1 uses the slow interval", "test_timer", T,
     "            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_MID);",
     "            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_SLOW); /* MUTATION */")

case("0x9e904 the missing default arm is filled in", "test_timer", T,
     "        default:\n            /* No arm: speeds above 4 leave both game-rate timers untouched. */\n            break;",
     "        default:\n            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_FAST); /* MUTATION */\n            break;")

case("0x9e904 the always-on anim timer is dropped", "test_timer", T,
     "    Timer_StartOne(PUFO_TIMER_ANIM, PUFO_TIMER_MS_ANIM);\n    Timer_StartOne(PUFO_TIMER_UIPULSE, PUFO_TIMER_MS_PULSE);",
     "    Timer_StartOne(PUFO_TIMER_UIPULSE, PUFO_TIMER_MS_PULSE); /* MUTATION: 0x9E9D0 gone */")

case("0x9e904 speed 4 leaves the 2/3 arm", "test_timer", T,
     "        case 2:\n        case 3:\n        case 4:",
     "        case 2:\n        case 3:")

case("0x9e904 the 0xC350/0xC351 pair is swapped", "test_timer", T,
     "            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_SLOW);\n            Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_SLOW);",
     "            Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_SLOW); /* MUTATION: order */\n            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_SLOW);")

case("0x9e6d4 the plain-set render constant moves", "test_timer", TH,
     "#define PUFO_TIMER_MS_RENDER 40u  /* 0xC351 in the plain sets */",
     "#define PUFO_TIMER_MS_RENDER 39u /* MUTATION */")

case("window handle: NULL accepted", "test_timer", T,
     "    if (hWnd == NULL) {\n        return false;\n    }",
     "    if (hWnd == NULL) {\n        s_hWnd = hWnd; /* MUTATION */\n        return true;\n    }")

# ------------------------------------------------------- the CDialogTemplate family (6 rows)

case("0x54c80 row stride uses byte 0 instead of byte 1", "test_template", TPL,
     "           * ((u32)pTpl->nRows * pTpl->nCols * nZ + (u32)pTpl->nCols * nY + nX)",
     "           * ((u32)pTpl->nRows * pTpl->nCols * nZ + (u32)pTpl->nRows * nY + nX) /* MUTATION */")

case("0x54c80 level stride uses byte 1 twice", "test_template", TPL,
     "((u32)pTpl->nRows * pTpl->nCols * nZ",
     "((u32)pTpl->nCols * pTpl->nCols * nZ /* MUTATION */")

case("0x54c80 the +3 header skip becomes +2", "test_template", TPL,
     "           + nSlot + PUFO_MA1_HDR;",
     "           + nSlot + 2u; /* MUTATION: LDRB R0,[R3,#3] */")

case("0x54c80 the slot bound is dropped", "test_template", TPL,
     "        || nSlot >= PUFO_MA1_CELL_BYTES) {",
     "        ) { /* MUTATION: the slot gate gone */")

case("0x54c74 GetLevelCount returns the row count", "test_template", TPL,
     "    return pTpl->nLevels;\n}",
     "    return pTpl->nRows; /* MUTATION */\n}")

case("0x54c68 GetHeaderByte0 returns the column count", "test_template", TPL,
     "u8 MapTemplate_GetHeaderByte0(const MapTemplate *pTpl)\n{\n    return pTpl->nRows;",
     "u8 MapTemplate_GetHeaderByte0(const MapTemplate *pTpl)\n{\n    return pTpl->nCols; /* MUTATION */")

case("0x54c3c the ctor fold is gone: no free before the load", "test_template", TPL,
     "    MapTemplate_Free(pTpl);\n\n    pufo_strncpy",
     "\n    pufo_strncpy /* MUTATION: the 0x54C40 store is gone */")

case("0x54c3c Map_LoadMa1's flag-before-find order restored", "test_template", TPL,
     "    if (!AssetReader_Find(&rdr, szName)) {\n        return false;\n    }",
     "    if (!AssetReader_Find(&rdr, szName)) {\n        pTpl->nFlag = nFlag; /* MUTATION: 0x54BD0 stores first */\n        return false;\n    }")

case("0x54c48 the dtor fold leaves the pointer behind", "test_template", TPL,
     "    free(pTpl->pBlob);\n    pTpl->pBlob   = NULL;",
     "    free(pTpl->pBlob); /* MUTATION: no null store */")

# ------------------------------------------------------------- the registry pair (2 rows)

case("0x9edbc the 0xFF bound becomes 0x100", "test_boot", REG,
     "    if (nResult != ERROR_SUCCESS || dwType != REG_SZ || cbData > 0xFFu) {",
     "    if (nResult != ERROR_SUCCESS || dwType != REG_SZ || cbData > 0x100u) { /* MUTATION */")

case("0x9edbc the Type test is removed", "test_boot", REG,
     "    if (nResult != ERROR_SUCCESS || dwType != REG_SZ || cbData > 0xFFu) {",
     "    if (nResult != ERROR_SUCCESS || cbData > 0xFFu) { /* MUTATION: 0x9EE64/0x9EE68 gone */")

case("0x9ed0c the stored size drops the NUL", "test_boot", REG,
     "    cbValue = (DWORD)strlen(pszValue) + 1u;",
     "    cbValue = (DWORD)strlen(pszValue); /* MUTATION: 0x9ED8C's ADD R3,R0,#1 */")

case("0x9ed0c the value is written as REG_DWORD", "test_boot", REG,
     "    nResult = RegSetValueExW(hKey, s_wValueName, 0, REG_SZ,",
     "    nResult = RegSetValueExW(hKey, s_wValueName, 0, REG_DWORD, /* MUTATION: 0x9ED98 */")

case("0x9edbc the buffer terminator is written at the front", "test_boot", REG,
     "    s_szData[PUFO_REG_VALUE_MAX - 1] = '\\0';",
     "    s_szData[0] = '\\0'; /* MUTATION */")

# ------------------------------------------------------------------ App_QuitDisplay (1 row)

# The store at 0xA03D4. pufo_app.c belongs to the `pocketufo` target only -- no test links it -- so the
# runner can show that it still builds, and no FAIL line can appear for any target. That is the honest
# shape of this row: the quit teardown is exercised by tools/e2e.sh's quit flow (exit code 0 and the two
# strateg counts), not by a check.
case("0xa0388 the 0xA03D4 sticky store is removed", "pocketufo", APP,
     "    PostQuitMessage(0);\n    g_pointerWorld.bListRowSticky = true; /* 0xA03D4 */",
     "    PostQuitMessage(0); /* MUTATION: 0xA03D4 dropped */")


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    ran = 0
    skipped = 0
    inert = []
    teeth = []

    for name, target, path, old, new, show in CASES:
        if only and only not in name:
            continue
        ran += 1
        lines = run_case(name, target, path, old, new, show)
        if lines is None:
            skipped += 1
        elif len(lines) == 0:
            inert.append(name)
        else:
            teeth.append((name, len(lines)))

    print()
    print("cases run:", ran, "| with teeth:", len(teeth), "| without:", len(inert), "| skipped:", skipped)
    for name, n in teeth:
        print("  teeth %2d  %s" % (n, name))
    for name in inert:
        print("  INERT     %s" % name)


main()
