"""Read initialized data out of pufobeta.exe by IDA virtual address.

Helper for the port: the original's static tables (UI control descriptors, pad
name lists) are file-backed, so reading them out of the executable is cheaper and
less error-prone than paging them through the IDB.

Address model, verified against string anchors in the file:
  ImageBase 0x10000, .text VA 0x11000, .data VA 0xA6000 (raw 0x6800 @ 0x93000,
  virtual size 0xE2D70 so the block reaches 0x188D70 in memory; everything past
  0xAC800 is uninitialised and has no file bytes).
  IDA reports the real VA, so
      file_offset = (ida_va - (ImageBase + section_va)) + section_raw
  Proof: the descriptor table at 0xA7ED8 maps to file 0x94ED8, and its szPad1
  at +13 is 'nextu', found at file 0x94EE5.
"""
import os
import struct
import sys

PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pufobeta.exe')


def load():
    d = open(PATH, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsize = struct.unpack_from('<H', d, pe + 20)[0]
    imgbase = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    secoff = pe + 24 + optsize
    secs = []
    for i in range(nsec):
        o = secoff + 40 * i
        name = d[o:o + 8].rstrip(b'\0').decode('latin1')
        vsize, va, size, raw = struct.unpack_from('<IIII', d, o + 8)
        secs.append((name, va, size, raw))
    return d, imgbase, secs


D, BASE, SECS = load()


def sections():
    for name, va, size, raw in SECS:
        print('%-8s va=%-9s raw_size=%-8s raw=%s' %
              (name, hex(BASE + va), hex(size), hex(raw)))


def rd(ida_va, n):
    r = ida_va - BASE
    for name, va, size, raw in SECS:
        if va <= r < va + size:
            off = raw + (r - va)
            if off + n > len(D):
                return None
            return D[off:off + n]
    return None


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'secs':
        sections()
    else:
        addr = int(sys.argv[1], 16)
        n = int(sys.argv[2], 10)
        b = rd(addr, n)
        if b is None:
            print('not file-backed: %s' % hex(addr))
        else:
            print(b.hex())
