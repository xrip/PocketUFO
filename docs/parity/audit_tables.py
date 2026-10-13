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
    ("shot sound by sort key",   0xAA040, "g_aShotSound",          "src/pufo/game/pufo_combat.c", 148, "H", 0),
    ("fire sound by sort key",   0xAA168, "g_aFireSoundBySortKey", "src/pufo/game/pufo_combat.c", 148, "H", 0),
    ("step deltas",              0xA7AE0, "g_aStepDir",            "src/pufo/game/pufo_path.c",    30, "b", 0),
    ("unit type flag byte[4i]",  0xA7B80, "g_aUnitTypeFlag0",      "src/pufo/game/pufo_unit.c",    24, "b", 4),
    ("unit type flag byte[4i+1]", 0xA7B81, "g_aUnitTypeIsLarge",   "src/pufo/game/pufo_unit.c",    24, "b", 4),
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
