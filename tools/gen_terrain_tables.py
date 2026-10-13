"""Emit Map_BuildMissionTerrain's module-name tables as C initialiser text.

All four are nine-byte-stride ASCII name tables in .data, and all four are read only by
Map_BuildMissionTerrain (0x22e10). Extents come from the image -- each one is closed by the next
named symbol -- not from IDA's own declarations:

  aUfo1a                     0xA6590  9 * (3 * (nTerrainVariant - 1) + nTier)   0x2303C, 0x236DC
      15 slots, ends at 0xA6617; aAlienRaceCounts starts at 0xA6618. IDA declares char aUfo1a[216],
      which runs 81 bytes past the end into the alien tables. Seven slots are empty strings.
  aPlane                     0xA6668  9 * (nMissionType - 1)                    0x231EC
      3 slots, ends at 0xA6687; aHwpsDeployOffsets starts at 0xA6688.
  aTerrainNameByMissionType  0xA67F8  9 * nMissionType                          0x2335C, 0x234A4
      10 slots, 90 bytes.
  aTerrainNameSuffix0        0xA7B20  9 * nMissionType                          0x233AC
      10 slots, 90 bytes. "0" for every mission type except urban (index 5), which is "4".

Run with no argument to print the C block; run with --check to print the slot layout and the
boundary bytes the extents rest on.
"""
import sys

from dump_pe import rd

TABLES = [
    ("g_aUfoModuleName",        0xA6590, 15, "ufo1a; tier slots 1,2,5,8,11,13,14 are empty strings"),
    ("g_aPlaneName",            0xA6668,  3, "indexed by nMissionType - 1"),
    ("g_aTerrainNameByMission", 0xA67F8, 10, "indexed by nMissionType"),
    ("g_aTerrainNameSuffix",    0xA7B20, 10, "the suffix strcat'd onto the name above"),
]


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
        else:
            # Byte-array form, so docs/parity/audit_tables.py can read the initialiser back.
            # Same shape as s_aSpriteSetName in pufo_tactctl.c.
            print("/* %s @ 0x%08X: %d names of 9 bytes. %s */" % (sym, va, n, note))
            print("const char %s[%d][9] = {" % (sym, n))
            for i, (s, raw) in enumerate(rows):
                tail = "" if i == n - 1 else ","
                body = ", ".join("%3d" % b for b in raw)
                print("    /* %2d %-8s */ { %s }%s" % (i, s, body, tail))
            print("};")
            print()


if __name__ == "__main__":
    main()
