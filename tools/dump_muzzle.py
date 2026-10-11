"""Dump the original's unit muzzle-offset table (0xA9768) and the fire-sound
table (0xAA168) from pufobeta.exe so the port can pin them byte-for-byte.

IDA addresses in pufobeta.exe.i64 are RVA + 0x10000 (the loader base IDA chose);
this script takes IDA addresses and maps them through the PE section table.
"""
import struct
from pe_off import off, SECS  # noqa: E402

BASE = 0x10000
d = open('pufobeta.exe', 'rb').read()


def words(ida_addr, n, signed=True):
    o = off(ida_addr - BASE)
    if o is None:
        raise KeyError('ida %s is uninitialized (.bss)' % hex(ida_addr))
    fmt = '<%d%s' % (n, 'h' if signed else 'H')
    return list(struct.unpack_from(fmt, d, o))


if __name__ == '__main__':
    v = words(0xA9768, 2208 // 2)
    for t in range(23):
        blk = v[t * 48:(t + 1) * 48]
        rows = ['{%d,%d,%d}' % tuple(blk[i * 3:i * 3 + 3]) for i in range(16)]
        print('    /* type %2d */ %s,' % (t, ', '.join(rows)))
