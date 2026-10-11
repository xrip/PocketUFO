"""Decode the original's UiControlDesc tables out of pufobeta.exe.

UiControl_Setup @ 0x1a75c reads the descriptor at fixed offsets proven from its
ARM: xOff +0, yOff +2, szPad0 +4, szPad1 +13, szPad2 +22, nMode +31, stride 32.
"""
import struct
import sys
from dump_pe import rd

sys.stdout.reconfigure(encoding='utf-8', errors='backslashreplace')

TABLES = [
    ('off_A7ED8', 0xA7ED8, 19),
    ('unk_A8158', 0xA8158, 13),
    ('unk_A8418', 0xA8418, 12),
    ('unk_A8918', 0xA8918, 9),
    ('unk_A8838', 0xA8838, 7),
    ('off_A8A38', 0xA8A38, 29),
    ('unk_A82F8', 0xA82F8, 9),
    ('unk_A8598', 0xA8598, 3),
    ('unk_A86B8', 0xA86B8, 3),
    ('stru_A8718', 0xA8718, 1),
    ('unk_A8738', 0xA8738, 8),
    ('stru_A7CB8', 0xA7CB8, 1),
    ('stru_A7CD8', 0xA7CD8, 1),
]


def name(b):
    return b.split(b'\0')[0].decode('latin1')


for label, addr, count in TABLES:
    b = rd(addr, count * 32)
    print('=== %s @ %s x %d ===' % (label, hex(addr), count))
    for i in range(count):
        rec = b[i * 32:(i + 1) * 32]
        x, y = struct.unpack_from('<hh', rec, 0)
        p0 = name(rec[4:13])
        p1 = name(rec[13:22])
        p2 = name(rec[22:31])
        mode = rec[31]
        print('  [%3d] x=%-5d y=%-4d mode=%-3d  %-11s %-11s %-11s' %
              (i, x, y, mode, ascii(p0), ascii(p1), ascii(p2)))

print('=== pad name list @ 0xA90F8 x 32 (stride 9) ===')
b = rd(0xA90F8, 32 * 9)
for i in range(32):
    print('  [%2d] %s' % (i, ascii(name(b[i * 9:(i + 1) * 9]))))
