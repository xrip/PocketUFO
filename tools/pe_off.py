"""PE RVA -> file offset helper for pufobeta.exe."""
import struct

d = open('pufobeta.exe', 'rb').read()
e = struct.unpack_from('<I', d, 0x3C)[0]
assert d[e:e + 4] == b'PE\0\0'
nsec = struct.unpack_from('<H', d, e + 6)[0]
sizeopt = struct.unpack_from('<H', d, e + 20)[0]
opt = e + 24
secoff = opt + sizeopt
SECS = []
for i in range(nsec):
    o = secoff + i * 40
    name = d[o:o + 8].rstrip(b'\0').decode('latin1')
    vsz, va, rsz, ra = struct.unpack_from('<IIII', d, o + 8)
    SECS.append((name, va, vsz, ra, rsz))


def off(rva):
    for name, va, vsz, ra, rsz in SECS:
        if va <= rva < va + vsz:
            return ra + (rva - va) if (rva - va) < rsz else None
    return None


if __name__ == '__main__':
    print('image size', struct.unpack_from('<I', d, opt + 56)[0], 'file', len(d))
    for s in SECS:
        print('%-8s va=%s vsz=%7d ra=%s rsz=%7d' %
              (s[0], hex(s[1]), s[2], hex(s[3]), s[4]))
