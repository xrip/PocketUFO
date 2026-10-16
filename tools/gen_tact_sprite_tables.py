"""Emit Font_LoadTactSprites' three name tables as C initialiser text.

All three are read only by Font_LoadTactSprites (0x257c4), and all three extents come from the image:
each one is closed by the next named symbol.

  aTactTerrainSetName      0xA6858  9 * (4 * nMissionType + i), i = 0..3   0x2582C
      40 slots = 10 mission types x 4 sprite sets, 360 bytes, ends exactly at aTactUfoSetName.
      IDA's own name was aJungle_0, after its first entry.
  aTactUfoSetName          0xA69C0  9 * (4 * nUfoClass + i), i = 0..3      0x258F0
      32 slots = 8 UFO classes x 4 sprite sets, 288 bytes, ends exactly at abTactUfoClassByVariant.
  abTactUfoClassByVariant  0xA6AE0  3 * (nTerrainVariant - 1) + nTerrainParam   0x258EC
      16 bytes, ends at g_aTerrainModuleCount @ 0xA6AF0. Only 15 entries are reachable, because the
      shipped variants run 1..5 and the Map+0x1510C dword the variant index adds runs 0..2.

Run with no argument to print the C block; run with --check to print the slot layout and the boundary
bytes the extents rest on.
"""
import sys

from dump_pe import rd

TABLES = [
    ("g_aTactTerrainSetName", 0xA6858, 40, "4 sprite sets per mission type, indexed 4 * nMissionType + i"),
    ("g_aTactUfoSetName",     0xA69C0, 32, "4 sprite sets per UFO class, indexed 4 * nUfoClass + i"),
]
BYTES_TABLE = ("g_abTactUfoClassByVariant", 0xA6AE0, 16,
               "the aTactUfoSetName class for 3 * (nTerrainVariant - 1) + nTerrainParam")


def slots(va, n):
    b = rd(va, 9 * n)
    if b is None:
        raise SystemExit("not file-backed: %s" % hex(va))
    out = []
    for i in range(n):
        raw = b[9 * i:9 * i + 9]
        out.append((raw.split(b"\0")[0].decode("latin1"), raw))
    return out


def main():
    check = "--check" in sys.argv
    for sym, va, n, note in TABLES:
        rows = slots(va, n)
        if check:
            print("== %s @ 0x%08X, %d slots of 9, ends 0x%08X  (%s)"
                  % (sym, va, n, va + 9 * n - 1, note))
            for i, (s, raw) in enumerate(rows):
                print("   idx %2d: %-9r %s" % (i, s, raw.hex()))
            nxt = rd(va + 9 * n, 9)
            print("   next bytes: %s" % nxt.hex())
        else:
            print("/* %s @ 0x%08X: %d names of 9 bytes. %s */" % (sym, va, n, note))
            print("const char %s[%d][9] = {" % (sym, n))
            for i, (s, raw) in enumerate(rows):
                tail = "" if i == n - 1 else ","
                body = ", ".join("%3d" % b for b in raw)
                print("    /* %2d %-8s */ { %s }%s" % (i, s, body, tail))
            print("};")
        print()

    sym, va, n, note = BYTES_TABLE
    b = rd(va, n)
    if b is None:
        raise SystemExit("not file-backed: %s" % hex(va))
    if check:
        print("== %s @ 0x%08X, %d bytes  (%s)" % (sym, va, n, note))
        print("   bytes: %s" % ", ".join(str(x) for x in b))
        print("   next bytes: %s" % rd(va + n, 9).hex())
    else:
        print("/* %s @ 0x%08X: %d bytes. %s */" % (sym, va, n, note))
        print("const u8 %s[%d] = {" % (sym, n))
        for i in range(0, n, 8):
            row = b[i:i + 8]
            tail = "" if i + 8 >= n else ","
            print("    %s%s" % (", ".join("%2d" % x for x in row), tail))
        print("};")


if __name__ == "__main__":
    main()
