"""Emit Geoscape_InitUI's static tables as C initialiser text.

Two tables, both read by Geoscape_InitUI (0x857f4) and Game_StartNew (0x94554):

  UiControlDesc records, stride 32 (xOff +0, yOff +2, szPad0 +4, szPad1 +13,
  szPad2 +22, nMode +31 — proven from UiControl_Setup @ 0x1a75c's ARM):
      0xAB4B0  10   Geoscape_BuildNavControls' base-nav controls (NewBase..Sell)
      0xAB5F0   2   the soldier panel prev/next pair
      0xAB630   3   the soldier find pair plus the transfer button
      0xABED8  13   the geoscape root controls (zoom, speed, options, funding)
      0xAC078  13   the geoscape control singletons

  dword_AA2C0 149  the per-object initial price (ObjectRecFull.uPrice @ 0xBC),
      indexed by object slot 1..nObjectCount-1; objects.dat is 30992 = 149 * 208.

Run with no argument to print the C block; run with --check to print the slot layout.
"""
import struct
import sys

from dump_pe import rd

DESCS = [
    ("g_aGeodescNav",     0xAB4B0, 10, "Geoscape_BuildNavControls @ 0x67b1c, base nav row"),
    ("g_aGeodescPanel",   0xAB5F0,  2, "SoldierScreen_PanelPrev/Next pair"),
    ("g_aGeodescFind",    0xAB630,  3, "SoldierScreen_Find pair plus Ui_SetTransferFlag"),
    ("g_aGeodescRoot",    0xABED8, 13, "the 13 root controls at 0x1864F8 + 4*i"),
    ("g_aGeodescSingle",  0xAC078, 13, "the 13 control singletons"),
    ("g_aBaseFacPanelDesc", 0xAB220, 2, "BaseFacScreen_Init @ 0x5a198, the build-dialog up/down pair"),
]


def name(b):
    return b.split(b"\0")[0].decode("latin1")


def main():
    check = "--check" in sys.argv
    for sym, va, n, note in DESCS:
        b = rd(va, n * 32)
        if b is None:
            raise SystemExit("not file-backed: %s" % hex(va))
        if check:
            print("== %s @ 0x%08X, %d records of 32, ends 0x%08X  (%s)"
                  % (sym, va, n, va + 32 * n - 1, note))
            for i in range(n):
                rec = b[i * 32:(i + 1) * 32]
                x, y = struct.unpack_from("<hh", rec, 0)
                print("   %2d: x=%-5d y=%-5d p0=%-9s p1=%-9s p2=%-9s mode=%d"
                      % (i, x, y, name(rec[4:13]), name(rec[13:22]),
                         name(rec[22:31]), rec[31]))
        else:
            print("/* %s @ 0x%08X: %d UiControlDesc records. %s */" % (sym, va, n, note))
            print("static const UiControlDesc %s[%d] = {" % (sym, n))
            for i in range(n):
                rec = b[i * 32:(i + 1) * 32]
                x, y = struct.unpack_from("<hh", rec, 0)
                tail = "" if i == n - 1 else ","
                print('    { %5d, %5d, "%s", "%s", "%s", %d }%s'
                      % (x, y, name(rec[4:13]), name(rec[13:22]),
                         name(rec[22:31]), rec[31], tail))
            print("};")
            print()

    va, n = 0xAA2C0, 149
    b = rd(va, n * 4)
    if b is None:
        raise SystemExit("not file-backed: %s" % hex(va))
    if check:
        print("== g_anObjectPrice @ 0x%08X, %d dwords" % (va, n))
        for i in range(n):
            print("   %3d: %d" % (i, int.from_bytes(b[i * 4:i * 4 + 4], "little")))
    else:
        print("/* g_anObjectPrice @ 0xAA2C0: the initial ObjectRecFull.uPrice per object slot. */")
        print("static const u32 g_anObjectPrice[%d] = {" % n)
        for i in range(n):
            v = int.from_bytes(b[i * 4:i * 4 + 4], "little")
            tail = "" if i == n - 1 else ","
            print("    %10d%s  /* %d */" % (v, tail, i))
        print("};")


if __name__ == "__main__":
    main()
