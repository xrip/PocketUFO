#!/usr/bin/env python3
"""Part 124's mutation proofs: the 12 `tact-ui` rows row 121a still had uncited.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part124.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part124.py BELT

Rows: the seven inventory filter buttons (0x17410 All, 0x17490/0x17610/0x17690/0x17510/0x17590/0x17710 for
filters 0,1,2,3,4,5), the cargo pair (0x45e8c, 0x45ec8), the inventory open gate (0x16ef4), the confirm dialog
(0x1eb68) and the drop path (0x12260).
"""
import os
import sys

# `python -I` keeps the script's own directory out of sys.path, so name it explicitly.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

TUI  = "src/pufo/game/pufo_tactui.c"
SHIP = "src/pufo/game/pufo_ship.c"
HUD  = "src/pufo/game/pufo_tacthud.c"
DRAG = "src/pufo/game/pufo_invdrag.h"

FILTERS_OLD = (b"static const FilterPad s_aFilterPad[6] = {\n"
               b"    FILTER_PAD_0, FILTER_PAD_3, FILTER_PAD_4, FILTER_PAD_1, FILTER_PAD_2, FILTER_PAD_X\n};")

def filters_new(at: int, pad: str) -> bytes:
    pads = ["FILTER_PAD_0", "FILTER_PAD_3", "FILTER_PAD_4", "FILTER_PAD_1", "FILTER_PAD_2", "FILTER_PAD_X"]
    pads[at] = pad
    return (b"static const FilterPad s_aFilterPad[6] = {\n    "
            + ", ".join(pads).encode() + b"\n}; /* MUTATION */")

CASES = [
    # The filter buttons: each of the seven is a separate ARM function whose only difference is which pad
    # global it lights and which index it stores. The port carries the mapping in one table, so a wrong entry
    # has to fail the check of exactly that filter.
    ("0x17490 filter 0 lights pad 1 instead of pad 0", "test_tactui", TUI, FILTERS_OLD, filters_new(0, "FILTER_PAD_1")),
    ("0x17610 filter 1 lights pad 4 instead of pad 3", "test_tactui", TUI, FILTERS_OLD, filters_new(1, "FILTER_PAD_4")),
    ("0x17690 filter 2 lights pad 3 instead of pad 4", "test_tactui", TUI, FILTERS_OLD, filters_new(2, "FILTER_PAD_3")),
    ("0x17510 filter 3 lights pad 2 instead of pad 1", "test_tactui", TUI, FILTERS_OLD, filters_new(3, "FILTER_PAD_2")),
    ("0x17590 filter 4 lights pad 1 instead of pad 2", "test_tactui", TUI, FILTERS_OLD, filters_new(4, "FILTER_PAD_1")),
    # 0x17710 is the only one that lights pBank+0x2AA, the X pad; the port names that with a separate flag.
    ("0x17710 filter 5 lights a numbered pad instead of the X pad", "test_tactui", TUI, FILTERS_OLD, filters_new(5, "FILTER_PAD_0")),
    # 0x17410 stores 0xFF in unk_1183DE where every numbered filter stores its own number.
    ("0x17410 All selection index 0xFF -> 0", "test_tactui", TUI,
     b"    pUi->nInvListSel = (nFilter == PUFO_INV_FILTER_ALL) ? (u8)-1 : (u8)nFilter;",
     b"    pUi->nInvListSel = (nFilter == PUFO_INV_FILTER_ALL) ? (u8)0 : (u8)nFilter; /* MUTATION */"),

    # The cargo pair: 0x45E8C adds into the object's column for this craft AND the craft's own total,
    # 0x45EC8 subtracts from both. Swapping the flag on either half breaks the pair.
    ("0x45e8c InvSel_AddItemQty subtracts instead", "test_ship", SHIP,
     b"    return ShipCargoApply(pSel, pItem, nDelta, true);\r\n}\r\n\r\n/* InvSel_SubItemQty @ 0x45ec8. */",
     b"    return ShipCargoApply(pSel, pItem, nDelta, false); /* MUTATION */\r\n}\r\n\r\n/* InvSel_SubItemQty @ 0x45ec8. */"),
    ("0x45ec8 InvSel_SubItemQty adds instead", "test_ship", SHIP,
     b"    return ShipCargoApply(pSel, pItem, nDelta, false);\r\n}\r\n\r\n/* BaseRec_SubItemQty @ 0x5571c. */",
     b"    return ShipCargoApply(pSel, pItem, nDelta, true); /* MUTATION */\r\n}\r\n\r\n/* BaseRec_SubItemQty @ 0x5571c. */"),

    # 0x16ef4: the unit-type gate (<= 3) and the "every category" selection byte it stores first.
    ("0x16ef4 unit type gate > 3u -> > 2u", "test_tacthud", HUD,
     b"    if (pCtx->pSelUnit == NULL || pCtx->pSelUnit->nUnitType > 3u) {",
     b"    if (pCtx->pSelUnit == NULL || pCtx->pSelUnit->nUnitType > 2u) { /* MUTATION */"),
    ("0x16ef4 selection byte 0xFF -> 0", "test_tacthud", HUD,
     b"    pCtx->pUi->nInvListSel = (u8)-1;   /* 0xFF at 0x4A26: every category */",
     b"    pCtx->pUi->nInvListSel = (u8)0;   /* MUTATION */"),

    # 0x1eb68: the title is centred on y 10 and the two side labels are at fixed x 10 and x 170.
    ("0x1eb68 title y 10 -> 12", "test_tacthud", HUD,
     b"    Text_DrawString((240 - nWidth) / 2, 10, szTitle);",
     b"    Text_DrawString((240 - nWidth) / 2, 12, szTitle); /* MUTATION */"),
    ("0x1eb68 right label x 170 -> 160", "test_tacthud", HUD,
     b"    Text_DrawString(170, 30, szRight);",
     b"    Text_DrawString(160, 30, szRight); /* MUTATION */"),

    # 0x12260: the per-region time costs. The test's expectations are literals, so these move them.
    ("0x12260 pocket 0/1 cost 4 -> 5", "test_invdrag", DRAG,
     b"#define PUFO_INV_TU_POCKET_A 4",
     b"#define PUFO_INV_TU_POCKET_A 5 /* MUTATION */"),
    ("0x12260 hand cost 6 -> 5", "test_invdrag", DRAG,
     b"#define PUFO_INV_TU_HAND     6",
     b"#define PUFO_INV_TU_HAND     5 /* MUTATION */"),
    ("0x12260 belt cost 8 -> 7", "test_invdrag", DRAG,
     b"#define PUFO_INV_TU_BELT     8",
     b"#define PUFO_INV_TU_BELT     7 /* MUTATION */"),
]

if __name__ == "__main__":
    wanted = sys.argv[1] if len(sys.argv) > 1 else None
    for name, target, path, old, new in CASES:
        if wanted and wanted not in name:
            continue
        run_case(name, target, path, old, new)
