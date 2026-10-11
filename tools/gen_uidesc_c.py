"""Emit the original's UiControlDesc tables as C initialiser text.

UiControl_Setup @ 0x1a75c reads the descriptor at offsets proven from its ARM:
xOff +0, yOff +2, szPad0 +4, szPad1 +13, szPad2 +22, nMode +31, stride 32.
"""
import struct
from dump_pe import rd

TABLES = [
    ('pDescLook', 0xA8138, 1),
    ('aUiDescTact', 0xA7ED8, 19),
    ('aUiDescInv', 0xA8158, 13),
    ('aUiDescAction', 0xA8418, 12),
    ('aUiDescDeploy', 0xA8918, 9),
    ('aUiDescSpeed', 0xA8838, 7),
    ('aUiDescOptions', 0xA8A38, 29),
    ('aUiDescResearch', 0xA82F8, 9),
    ('aUiDescSaveSlot', 0xA8598, 3),
    ('aUiDescLoadSlot', 0xA86B8, 3),
    ('aUiDescQuit', 0xA8718, 1),
    ('aUiDescList', 0xA8738, 8),
    ('aUiDescCancel0', 0xA7CB8, 1),
    ('aUiDescCancel1', 0xA7CD8, 1),
]


def name(b):
    return b.split(b'\0')[0].decode('latin1')


for label, addr, count in TABLES:
    b = rd(addr, count * 32)
    if b is None:
        raise SystemExit('not file-backed: %s' % hex(addr))
    print('static const UiControlDesc %s[%d] = {' % (label, count))
    for i in range(count):
        rec = b[i * 32:(i + 1) * 32]
        x, y = struct.unpack_from('<hh', rec, 0)
        p = [name(rec[4:13]), name(rec[13:22]), name(rec[22:31])]
        for v in p:
            if any(ord(c) > 126 or ord(c) < 32 for c in v):
                raise SystemExit('bad name in %s[%d]: %r' % (label, i, v))
        print('    { %5d, %5d, "%s", "%s", "%s", %2d },' %
              (x, y, p[0], p[1], p[2], rec[31]))
    print('};')
