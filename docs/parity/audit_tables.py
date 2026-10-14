#!/usr/bin/env python3
"""audit_tables.py - compare the port's static data tables against the shipped image.

Every table in the port that claims to be "dumped from the binary" should be checkable here rather than
read by eye. Two mistakes this session came from transcribing long byte runs by hand, so the rule is:
compute the comparison.

Address mapping was established by locating aDirDeltaXY (its ten signed step triples appear verbatim in the
file): IDA_VA - 0x13000 == file offset into pufobeta.exe.

Usage:  python docs/parity/audit_tables.py [pufobeta.exe]
Exit 0 if every registered table matches, 1 otherwise.
"""
import re
import struct
import sys

EXE = "pufobeta.exe"
IDA_VA_TO_FILE = 0x13000

# name, IDA VA, port symbol, port file, entry count, struct format, byte stride (0 = packed)
TABLES = [
    ("new base cost by kind",    0xAB488, "g_anNewBaseCostByKind", "src/pufo/game/pufo_baseui.c",  10, "i", 0),
    ("shot sound by sort key",   0xAA040, "g_aShotSound",          "src/pufo/game/pufo_combat.c", 148, "H", 0),
    ("fire sound by sort key",   0xAA168, "g_aFireSoundBySortKey", "src/pufo/game/pufo_combat.c", 148, "H", 0),
    ("step deltas",              0xA7AE0, "g_aStepDir",            "src/pufo/game/pufo_path.c",    30, "b", 0),
    ("unit type flag byte[4i]",  0xA7B80, "g_aUnitTypeFlag0",      "src/pufo/game/pufo_unit.c",    24, "b", 4),
    ("unit type flag byte[4i+1]", 0xA7B81, "g_aUnitTypeIsLarge",   "src/pufo/game/pufo_unit.c",    24, "b", 4),
    ("unit type flag byte[4i+2]", 0xA7B82, "g_aUnitTypeFlag2",     "src/pufo/game/pufo_unit.c",    24, "b", 4),
    # 23 blocks of 96 bytes at unk_A9768: 23 types x 2 selections x 8 facings, each an i16 triple.
    ("unit muzzle offsets",      0xA9768, "g_aUnitMuzzleOffset",   "src/pufo/game/pufo_unit.c",  1104, "h", 0),
    # aInvHitRects: 25 records of 14 bytes, seven i16 each.
    ("inventory hit rects",      0xA8DD8, "s_aHitRect",            "src/pufo/game/pufo_invdrag.c", 175, "h", 0),
    # The two Tact_EquipTeamFromCraft candidate tables, plain dword object indices.
    ("equip left-hand order",    0xA62F0, "s_aHandObjects",        "src/pufo/game/pufo_invdrag.c",  13, "i", 0),
    ("equip third-pickup order", 0xA6328, "s_aPackObjects",        "src/pufo/game/pufo_invdrag.c",   6, "i", 0),
    # The nine MapView_DrawUnit tables, dumped by tools/gen_isodraw_tables.py. Extents come from the
    # image: each one is closed by the next symbol, and the two short ones are recorded as short.
    ("unit walk kind",           0xA6B00, "g_aUnitWalkKind",       "src/pufo/game/pufo_isodraw.c", 1840, "B", 0),
    ("unit anim frame",          0xA7230, "g_aUnitAnimFrame",      "src/pufo/game/pufo_isodraw.c",  234, "H", 0),
    ("unit walk glyph",          0xA7408, "g_aUnitWalkGlyph",      "src/pufo/game/pufo_isodraw.c",  594, "B", 0),
    ("pose frame table",         0xA7660, "g_aPoseFrameTable",     "src/pufo/game/pufo_isodraw.c",  760, "B", 0),
    ("unit type pose base",      0xA7958, "g_aUnitTypePoseBase",   "src/pufo/game/pufo_isodraw.c",   23, "B", 0),
    ("unit height offset",       0xA7A28, "g_aUnitHeightOff",      "src/pufo/game/pufo_isodraw.c",  184, "b", 0),
    ("bob phase Y",              0xA7BE0, "g_aBobPhaseY",          "src/pufo/game/pufo_isodraw.c",    8, "b", 0),
    ("unit weapon Y offset",     0xA9310, "g_aUnitWeaponYOff",     "src/pufo/game/pufo_isodraw.c",   23, "b", 0),
    ("unit facing draw offset",  0xA93A8, "g_aUnitFacingDrawOff",  "src/pufo/game/pufo_isodraw.c",  224, "b", 0),
    # The nine Tact_SpawnAliens tables, dumped by tools/gen_alien_tables.py. Every extent is closed
    # by the next symbol in the image; see tools/gen_alien_tables.py --check for the boundary bytes.
    ("sprite set names",         0xA7BE8, "s_aSpriteSetName",         "src/pufo/game/pufo_tactctl.c", 207, "B", 0),
    ("alien race counts",        0xA6618, "s_aAlienRaceCounts",       "src/pufo/game/pufo_tactctl.c",  16, "B", 0),
    ("alien secondary counts",   0xA6658, "s_aAlienSecondaryCounts",  "src/pufo/game/pufo_tactctl.c",  16, "B", 0),
    ("alien race composition",   0xA6628, "s_aAlienRaceComposition",  "src/pufo/game/pufo_tactctl.c",  40, "B", 0),
    ("alien rank types",         0xA6650, "s_aAlienRankTypes",        "src/pufo/game/pufo_tactctl.c",   5, "B", 0),
    ("alien mission totals",     0xAA020, "s_aAlienMissionTotals",    "src/pufo/game/pufo_tactctl.c",   8, "H", 0),
    ("alien composition divisor", 0xAA030, "s_aAlienCompositionDivisor", "src/pufo/game/pufo_tactctl.c", 8, "H", 0),
    ("alien belt objects",       0xAA008, "s_aAlienBeltObjects",      "src/pufo/game/pufo_tactctl.c",   8, "h", 0),
    ("alien unit stats",         0xA8F38, "s_aAlienUnitStats",        "src/pufo/game/pufo_tactctl.c", 224, "h", 0),
    # The two shipped tables Map_BuildMissionTerrain reads. aTileSprIndex's real extent is 128 bytes:
    # IDA declares aTileSprIndex[128] (256 bytes) and runs past the end into the module-name strings
    # at 0xA6590, and the 40 bytes from 0xA6568 belong to Smap_InitCraftSelectState (0x61b94).
    ("tile sprite slots",        0xA64E8, "g_aTileSprIndex",          "src/pufo/game/pufo_terrain.c",  64, "H", 0),
    ("terrain module count",     0xA6AF0, "g_aTerrainModuleCount",    "src/pufo/game/pufo_terrain.c",  10, "B", 0),
    # The four module-name tables, all nine-byte stride. Extents are the gap to the next named symbol:
    # aUfo1a ends at 0xA6617 (aAlienRaceCounts starts at 0xA6618) and aPlane at 0xA6687
    # (aHwpsDeployOffsets starts at 0xA6688). IDA declares char aUfo1a[216], 81 bytes too long.
    ("ufo module names",         0xA6590, "g_aUfoModuleName",         "src/pufo/game/pufo_terrain.c", 135, "B", 0),
    ("craft pad names",          0xA6668, "g_aPlaneName",             "src/pufo/game/pufo_terrain.c",  27, "B", 0),
    ("terrain name by mission",  0xA67F8, "g_aTerrainNameByMission",  "src/pufo/game/pufo_terrain.c",  90, "B", 0),
    ("terrain name suffix",      0xA7B20, "g_aTerrainNameSuffix",     "src/pufo/game/pufo_terrain.c",  90, "B", 0),
    # aFacBuildSpec: 18 records of three bytes (tileA byte, tileB byte, signed build days). IDA
    # declares it as 56 bytes; the real extent is 54 -- the last two bytes are zero padding, and the
    # highest index the readers reach is 17.
    ("facility build spec",      0xAA8F8, "g_aFacBuildSpec",          "src/pufo/game/pufo_geoscape.c", 54, "B", 0),
    # MapView_DrawUnitFacingArrow's glyph nudge table, indexed 10 * nRowId + n. The image holds the
    # same 64 bytes twice -- byte_A9290 for x and byte_A92D0 for y -- and both are registered against
    # the port's single copy, which is what proves they are identical.
    ("arrow glyph x nudge",      0xA9290, "g_abArrowGlyphOff",        "src/pufo/game/pufo_isodraw.c", 64, "b", 0),
    ("arrow glyph y nudge",      0xA92D0, "g_abArrowGlyphOff",        "src/pufo/game/pufo_isodraw.c", 64, "b", 0),
]


def read_image(data, va, n):
    off = va - IDA_VA_TO_FILE
    if off < 0 or off + n > len(data):
        raise SystemExit("VA %08X is outside the image" % va)
    return data[off:off + n]


def port_table(path, sym):
    """Pull the initialiser list for a table out of the port source.

    Comments are stripped first: several of these tables carry per-row comments whose numbers would
    otherwise be read as entries.
    """
    src = open(path, encoding="utf-8").read()
    m = re.search(r"\b" + sym + r"(?:\[[^\]]*\])+\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("no table named %s in %s" % (sym, path))
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|-?\d+", body)]


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else EXE
    data = open(exe, "rb").read()
    failures = 0

    for label, va, sym, path, n, fmt, stride in TABLES:
        if stride:
            raw = bytes(read_image(data, va + i * stride, 1)[0] for i in range(n))
        else:
            raw = read_image(data, va, n * struct.calcsize(fmt))
        image = list(struct.unpack("<" + str(n) + fmt, raw))
        port = port_table(path, sym)

        if len(port) != n:
            print("%-26s %08X  port has %d entries, expected %d" % (sym, va, len(port), n))
            failures += 1
            continue

        bad = [(i, p, q) for i, (p, q) in enumerate(zip(port, image)) if p != q]
        if bad:
            failures += 1
        print("%-26s %08X  %3d entries  %s"
              % (sym, va, n, "exact match" if not bad else "MISMATCH"))
        for i, p, q in bad[:25]:
            print("    [%3d] port %6d  image %6d" % (i, p, q))

    print("\n%d tables checked, %d mismatched" % (len(TABLES), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
