#!/usr/bin/env python3
"""Part 127's mutation proofs: the 35 `save` rows row 121a had uncited.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part127.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part127.py ReadTime

Rows: Save_ReadObjBefore 0x3f0e8, Save_WriteObjBefore 0x3f158, Save_ReadStrategVer 0x3f1c8,
Save_WriteBases 0x3f1fc, Save_WriteProduct 0x3f4f4, Save_WriteResearch 0x3f558, Save_WriteShips 0x3f6dc,
Save_WriteSmap 0x3fa28, Save_WriteStrateg 0x3fbb4, Save_WriteTime 0x40204, Save_ReadBases 0x402c0,
Save_ReadProduct 0x4062c, Save_ReadResearch 0x406e8, Save_ReadShips 0x4086c, Save_ReadSmap 0x40c4c,
Save_ReadStrateg 0x40f4c, Save_ReadTime 0x41600, Save_ReadSoldiers 0x416bc, Save_WriteSoldiers 0x42350,
Save_AssignRosterIndices 0x42ea0, Save_WriteFlobjs 0x42f48, Save_AllocFlobjs 0x431ac, Save_ReadFlobjs 0x43258,
Save_ReadMap 0x433d8, Save_WriteMap 0x438e0, Save_WriteTeamUnits 0x43c84, Save_ReadRmp 0x43ee0,
Save_ReadTeamUnits 0x44020, Save_ReadTGameData 0x44248, Save_WriteTGameData 0x446a8,
Save_WriteGameState 0x44928, Save_ReadGameState 0x44958, SaveGame 0x44988, LoadGame 0x44bbc,
SaveSlot_FormatList 0x45904.

Each case attacks the ONE thing the row's own checks pin: a field order the chunk's byte positions pin, a
width the original chose (1 vs 2 vs 4), a version gate's boundary, or a rule the original applies on read
only. `src/` is CRLF, so `case()` translates a "\n" in the anchor into the file's own line ending --
otherwise a multi-line anchor silently matches nothing and the runner prints SKIP.

Restore is by recorded offset (`mutate_run.run_case`), not by a second text search, which matters here:
several replacement texts -- `W(pDat, &pItem->bUnk8, 1)`, `W(pDat, &pShip->bHangarY, 1)`,
`DatFile_Read(pDat, &pTime->nSlot101, 4)` -- are also another line's text in the same file.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

C = "src/pufo/game/pufo_save.c"
T = "test_save"

CASES = []


def case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    eol = b"\r\n" if data.count(b"\r\n") > data.count(b"\n") - data.count(b"\r\n") else b"\n"
    CASES.append((name, target, path, old.replace("\n", "\r\n" if eol == b"\r\n" else "\n").encode(),
                  new.replace("\n", "\r\n" if eol == b"\r\n" else "\n").encode(), show))


# gamestate: the one payload byte, and the tag each half looks for.
case("WriteGameState-0x44928-count-width", T, C,
     "return W(pDat, &bTactFlag, 1);",
     "return W(pDat, &bTactFlag, 2);")
case("ReadGameState-0x44958-tag", T, C,
     'if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_GAMESTATE)) {',
     'if (!DatFile_FindChunk(pDat, "gamestat")) {')

# objbefore: one u16 per object at record +0xCA, in both directions.
case("ReadObjBefore-0x3f0e8-width", T, C,
     "if (!DatFile_Read(pDat, &apObjects[i]->uRecovScore, 2)) {",
     "if (!DatFile_Read(pDat, &apObjects[i]->uRecovScore, 1)) {")
case("WriteObjBefore-0x3f158-width", T, C,
     "if (!W(pDat, &apObjects[i]->uRecovScore, 2)) {",
     "if (!W(pDat, &apObjects[i]->uRecovScore, 1)) {")

# time: the write order is not the slot order, and the pause byte is 1 wide while its neighbours are 4.
case("WriteTime-0x40204-pause-width", T, C,
     "if (!W(pDat, &pTime->nSlot106, 1)) { return false; }",
     "if (!W(pDat, &pTime->nSlot106, 4)) { return false; }")
case("ReadTime-0x41600-slot-order", T, C,
     "if (!DatFile_Read(pDat, &pTime->nSlot102, 4)) { return false; }",
     "if (!DatFile_Read(pDat, &pTime->nSlot101, 4)) { return false; }")

# product: 0x1900 / 0x1900 / 0xC80 written, and the version gate that chooses the old 0x1200/0x1200/0x900/0x480.
case("WriteProduct-0x3f4f4-tail-size", T, C,
     "return DatFile_Write(pDat, pBlock + 12800, 3200); /* 0x3F52C */",
     "return DatFile_Write(pDat, pBlock + 12800, 3072); /* 0x3F52C */")
case("ReadProduct-0x4062c-signed-version", T, C,
     "    if (nSaveFmtVer >= 3) {\n        if (!DatFile_Read(pDat, pBlock, 6400)) {",
     "    if ((u32)nSaveFmtVer >= 3u) {\n        if (!DatFile_Read(pDat, pBlock, 6400)) {")

# research: 14 per-topic writes in the original's order, and the four head blocks before them.
case("WriteResearch-0x3f558-topic-order", T, C,
     "    return DatFile_Write(pDat, &pTopic->bComplete, 1)\n        && DatFile_Write(pDat, &pTopic->bUnk31, 1)",
     "    return DatFile_Write(pDat, &pTopic->bUnk31, 1)\n        && DatFile_Write(pDat, &pTopic->bComplete, 1)")
case("ReadResearch-0x406e8-topic-order", T, C,
     "    return DatFile_Read(pDat, &pTopic->bComplete, 1)\n        && DatFile_Read(pDat, &pTopic->bUnk31, 1)",
     "    return DatFile_Read(pDat, &pTopic->bUnk31, 1)\n        && DatFile_Read(pDat, &pTopic->bComplete, 1)")

# smap: 740-byte site records -- bFlag1 then bHasLoot, ahead of the 0x200 item-quantity block.
case("WriteSmap-0x3fa28-site-order", T, C,
     "        && DatFile_Write(pDat, &pSite->bFlag1, 1)\n        && DatFile_Write(pDat, &pSite->bHasLoot, 1)",
     "        && DatFile_Write(pDat, &pSite->bHasLoot, 1)\n        && DatFile_Write(pDat, &pSite->bFlag1, 1)")
case("ReadSmap-0x40c4c-site-order", T, C,
     "        && DatFile_Read(pDat, &pSite->bFlag1, 1)\n        && DatFile_Read(pDat, &pSite->bHasLoot, 1)",
     "        && DatFile_Read(pDat, &pSite->bHasLoot, 1)\n        && DatFile_Read(pDat, &pSite->bFlag1, 1)")

# flobjs: 17 bytes per item, and the count width that Map+0x118 chooses at both 0x431E0 and 0x43274.
case("WriteFlobjs-0x42f48-item-order", T, C,
     "if (!W(pDat, &pItem->bUnk7, 1))",
     "if (!W(pDat, &pItem->bUnk8, 1))")
case("ReadFlobjs-0x43258-count-width", T, C,
     "    if (nWideCountFlag != 0) {\n        if (!DatFile_Read(pDat, pnTactItemCount, 4)) {",
     "    if (nWideCountFlag >= 0) {\n        if (!DatFile_Read(pDat, pnTactItemCount, 4)) {")
case("AllocFlobjs-0x431ac-count-width", T, C,
     "if (nWideCountFlag != 0) { /* 0x431E0",
     "if (nWideCountFlag >= 0) { /* 0x431E0")

# teamunits: four chunks, counts 1/1/4/4 wide, and a reader that consumes all 30 slots whatever the count says.
case("WriteTeamUnits-0x43c84-alien-count-width", T, C,
     "if (!W(pDat, &pState->nEnemyCount, 1)) {",
     "if (!W(pDat, &pState->nEnemyCount, 4)) {")
case("ReadTeamUnits-0x44020-count-ignored", T, C,
     "if (!ReadRosterIds(pDat, pState->apXcomUnits, PUFO_SAVE_ROSTER_MAX,",
     "if (!ReadRosterIds(pDat, pState->apXcomUnits, pState->nXcomCount,")

# rmp: six blocks, and the level block is the LAST of them (0x43F7C), after the 0x4B0 names.
case("ReadRmp-0x43ee0-block-order", T, C,
     "    if (!DatFile_Read(pDat, aNames, sizeof(aNames))) { return false; }\n    if (!DatFile_Read(pDat, aLevel, sizeof(aLevel))) { return false; }",
     "    if (!DatFile_Read(pDat, aLevel, sizeof(aLevel))) { return false; }\n    if (!DatFile_Read(pDat, aNames, sizeof(aNames))) { return false; }")

# bases: 20-byte facility names, and two version gates on the read side (0x403A4 and 0x403E0).
case("WriteBases-0x3f1fc-facility-name", T, C,
     "return W(pDat, pFac->szName, 20)",
     "return W(pDat, pFac->szName, 19)")
case("ReadBases-0x402c0-version-gate", T, C,
     "if (nSaveFmtVer >= 3) {\n        if (!DatFile_Read(pDat, &pState->bUnk158848, 1)) {",
     "if (nSaveFmtVer >= 2) {\n        if (!DatFile_Read(pDat, &pState->bUnk158848, 1)) {")

# ships: 33 writes per 61-byte record, then a read-side rule no writer has (craft type 0 forces uRangeMax).
case("WriteShips-0x3f6dc-hangar-order", T, C,
     "if (!W(pDat, &pShip->bHangarX, 1))",
     "if (!W(pDat, &pShip->bHangarY, 1))")
case("ReadShips-0x4086c-craft-type0-range", T, C,
     "pNew->uRangeMax = 10000;",
     "pNew->uRangeMax = 10001;")

# strateg: 33 per-item writes with nType 21st, and a read gate on the BUILD's version byte.
case("WriteStrateg-0x3fbb4-item-order", T, C,
     "        if (!W(pDat, &p->bHyperDetect, 1)) { return false; }\n        if (!W(pDat, &p->nType, 1)) { return false; }",
     "        if (!W(pDat, &p->nType, 1)) { return false; }\n        if (!W(pDat, &p->bHyperDetect, 1)) { return false; }")
case("ReadStrateg-0x40f4c-version-gate", T, C,
     "        if (pSt->bSaveVersion > 2) {",
     "        if (pSt->bSaveVersion >= 2) {")

# map: one record per walk slot; bUnk56 is a byte, bFlags57[0] is two bits, and the read masks 0x8F.
case("WriteMap-0x438e0-unk56-vs-flags57", T, C,
     "if (!W(pDat, &pCell->bUnk56, 1)) { /* 0x43B3C */",
     "if (!W(pDat, &pCell->bFlags57[1], 1)) { /* 0x43B3C */")
case("ReadMap-0x433d8-mask-0x8F", T, C,
     "pCell->bFlags29 = (u8)(((u32)pCell->bFlags29 & 0x8Fu) |",
     "pCell->bFlags29 = (u8)(((u32)pCell->bFlags29 & 0xDFu) |")

# soldiers: a 2-byte count the writer emits once and the reader consumes TWICE (0x416D4 and 0x417D4).
case("WriteSoldiers-0x42350-count-width", T, C,
     "if (!W(pDat, &uCount, 2)) {",
     "if (!W(pDat, &uCount, 4)) {")
case("ReadSoldiers-0x416bc-second-count-read", T, C,
     "    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_SOLDIERS)) {\n        return false;\n    }\n    if (!DatFile_Read(pDat, &uCount, 2)) {\n        return false;\n    }\n    if ((i32)uCount > nCapacity) {\n        return false;\n    }\n    /* The three bInTact arms resolve",
     "    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_SOLDIERS)) {\n        return false;\n    }\n    /* The three bInTact arms resolve")

# the version byte of its own chunk, and the two id spaces that deliberately restart at 0.
case("ReadStrategVer-0x3f1c8-find-chunk", T, C,
     "    (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_STRATEG);\n    return DatFile_Read(pDat, pbVersion, 1);",
     "    return DatFile_Read(pDat, pbVersion, 1);")
case("AssignRosterIndices-0x42ea0-item-counter", T, C,
     "apTactItems[i]->nRefId = i;",
     "apTactItems[i]->nRefId = nSoldierCount + i;")

# t_gamedata: header fields written in order, the three craft tiles as ONE byte each of a dword, and the
# reset block the load applies after them.
case("WriteTGameData-0x446a8-craft-tile-width", T, C,
     "if (!W(pDat, &pSt->anCraftTile[i], 1)) {",
     "if (!W(pDat, &pSt->anCraftTile[i], 4)) {")
case("ReadTGameData-0x44248-uislot-200", T, C,
     "pReset->aUiSlot[4] = 200;",
     "pReset->aUiSlot[4] = 201;")

# the two whole-file drivers and the slot list.
case("SaveGame-0x44988-mission-count-width", T, C,
     "if (bOk) { bOk = W(pDat, pIn->pnMissionCount, 1); }",
     "if (bOk) { bOk = W(pDat, pIn->pnMissionCount, 4); }")
case("LoadGame-0x44bbc-tactver-width", T, C,
     "(void)DatFile_Read(pDat, &nTactVer, 2);",
     "(void)DatFile_Read(pDat, &nTactVer, 4);")
case("SaveSlot_FormatList-0x45904-day-pad", T, C,
     "if (pCtx->pTime->nDay < 10) {",
     "if (pCtx->pTime->nDay < 11) {")


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 else None
    counts = {"fail": 0, "inert": 0, "skip": 0, "not-restored": 0}
    for name, target, path, old, new, show in CASES:
        if only and only not in name:
            continue
        lines = run_case(name, target, path, old, new, show)
        if lines is None:
            counts["skip"] += 1
        elif len(lines) == 0:
            counts["inert"] += 1
        else:
            counts["fail"] += 1
    print("cases:", len(CASES), counts)
