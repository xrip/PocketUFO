"""One-shot patch: correct the unit-type tables and add the muzzle / fire-sound
tables, all dumped from pufobeta.exe (see tools/dump_muzzle.py, tools/pe_off.py).
"""
import struct
from pe_off import off

d = open('pufobeta.exe', 'rb').read()
BASE = 0x10000


def bytes_at(a, n):
    return d[off(a - BASE):off(a - BASE) + n]


def u16s(a, n):
    return list(struct.unpack('<%dH' % n, bytes_at(a, 2 * n)))


# --- unit type table, byte_A7B80, 24 rows x 4 bytes -------------------------
tb = bytes_at(0xA7B80, 96)
flag0 = [tb[t * 4] for t in range(24)]
large = [tb[t * 4 + 1] for t in range(24)]


def rows(vals, per=8):
    out = []
    for i in range(0, len(vals), per):
        out.append('    ' + ', '.join(str(v) for v in vals[i:i + per]) +
                   (',' if i + per < len(vals) else ''))
    return '\n'.join(out)


# --- muzzle table, unk_A9768, 23 blocks x 2 x 8 x 3 i16 --------------------
mv = list(struct.unpack('<%dh' % (2208 // 2), bytes_at(0xA9768, 2208)))
muz = []
for t in range(23):
    blk = mv[t * 48:(t + 1) * 48]
    half = []
    for h in range(2):
        e = blk[h * 24:h * 24 + 24]
        half.append('{ ' + ', '.join('{%d,%d,%d}' % tuple(e[i * 3:i * 3 + 3])
                                     for i in range(8)) + ' }')
    muz.append('    /* type %2d */ { %s },' % (t, ',\n                   '.join(half)))
muz = '\n'.join(muz)

# --- sound tables: 0xAA040 (Combat_ResolveShot) and 0xAA168 (move FSM) ------
shot = u16s(0xAA040, 148)
fire = u16s(0xAA168, 148)

p = 'src/pufo/game/pufo_unit.h'
s = open(p).read()
old = """/* Unit type table, byte_A7B80 @ 0xa7b80. Stride 4; byte[+0] is the "flag 0"
 * column and byte[+1] the "is large" flag. Decoded from the 64 bytes at that
 * address for the first 16 types. */
#define PUFO_UNIT_TYPE_COUNT 16
extern const u8 g_aUnitTypeIsLarge[PUFO_UNIT_TYPE_COUNT];
extern const u8 g_aUnitTypeFlag0[PUFO_UNIT_TYPE_COUNT];"""
new = """/* Unit type table, byte_A7B80 @ 0xa7b80. Stride 4, 24 rows (96 bytes; the byte
 * at +0x60 is not part of it). byte[+0] is the "flag 0" column and byte[+1] the
 * "is large" flag that Unit_bIsLarge @ 0x49ecc tests against 1.
 *
 * The table used to be 16 rows and the is-large column had 1s at types 11 and
 * 13. Both were wrong: those two rows are {1,0,1,0}, so flag 0 is set and the
 * unit is NOT large. Types 16..23 exist and are reachable - the switch in
 * Tact_ProcessMoveAction @ 0x4f3f0 has cases up to 0x16. */
#define PUFO_UNIT_TYPE_COUNT 24
extern const u8 g_aUnitTypeIsLarge[PUFO_UNIT_TYPE_COUNT];
extern const u8 g_aUnitTypeFlag0[PUFO_UNIT_TYPE_COUNT];

/* Unit muzzle-offset table, unk_A9768 @ 0xa9768: 23 blocks of 96 bytes, one per
 * unit type, each 2 groups of 8 facings, each entry 3 i16 (dx, dy, dz).
 *
 * Index proven from ARM at 0x50074..0x50094:
 *     idx = pUnit->bFacing + 8 * (bSel + 2 * pUnit->nUnitType)
 * where bSel is ObjectRecFull.bListPri (pObj+0xA4) clamped to 0 or 1, and the
 * byte offset is 6 * idx. The offsets are added to 16*cell->nX / 16*cell->nY /
 * 12*cell->nZ to get the shot origin in sub-cell units (0x50098..0x500e0).
 *
 * Types 14..22 are zero blocks except type 18 group 1, which is {0,0,-3} for all
 * eight facings. */
#define PUFO_UNIT_MUZZLE_TYPE_COUNT 23
typedef struct MuzzleOffset {
    i16 nDx;
    i16 nDy;
    i16 nDz;
} MuzzleOffset;
extern const MuzzleOffset g_aUnitMuzzleOffset[PUFO_UNIT_MUZZLE_TYPE_COUNT][2][8];"""
assert old in s
open(p, 'w').write(s.replace(old, new))

p = 'src/pufo/game/pufo_unit.c'
s = open(p).read()
old = """/* byte_A7B80 @ 0xa7b80, stride 4: byte[+0] is "flag 0" and byte[+1] the "is large"
 * flag. Both columns decoded from the 64 bytes at that address. */
const u8 g_aUnitTypeIsLarge[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0
};
const u8 g_aUnitTypeFlag0[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0
};"""
new = """/* byte_A7B80 @ 0xa7b80, stride 4, 24 rows. Dumped from the binary. */
const u8 g_aUnitTypeIsLarge[PUFO_UNIT_TYPE_COUNT] = {
%s
};
const u8 g_aUnitTypeFlag0[PUFO_UNIT_TYPE_COUNT] = {
%s
};

/* unk_A9768 @ 0xa9768, 23 x 2 x 8 MuzzleOffset. Dumped from the binary. */
const MuzzleOffset g_aUnitMuzzleOffset[PUFO_UNIT_MUZZLE_TYPE_COUNT][2][8] = {
%s
};""" % (rows(large), rows(flag0), muz)
assert old in s
open(p, 'w').write(s.replace(old, new))

p = 'src/pufo/game/pufo_combat.h'
s = open(p).read()
old = """/* off_AA040 @ 0xaa040, 360 bytes = 180 u16: weapon uSortKey -> the sound id
 * Combat_ResolveShot passes to Sound_Play. Dumped from the IDB. */
#define PUFO_SHOT_SOUND_COUNT 180
extern const u16 g_aShotSound[PUFO_SHOT_SOUND_COUNT];"""
new = """/* Two parallel 148-entry tables, both indexed by ObjectRecFull.uSortKey with no
 * bound check in the original:
 *   off_AA040 @ 0xaa040  - the sound Combat_ResolveShot plays on impact.
 *   word_AA168 @ 0xaa168 - the sound Tact_ProcessMoveAction @ 0x4f3f0 plays for a
 *                          queued shot (case 0xE at 0x50338 and case 0x12 at 0x50330).
 * 0xAA040 + 148*2 = 0xAA168, so 148 is the real extent of the first table; the
 * port used to claim 180, which ran 32 entries into the second table. */
#define PUFO_SHOT_SOUND_COUNT 148
extern const u16 g_aShotSound[PUFO_SHOT_SOUND_COUNT];
extern const u16 g_aFireSoundBySortKey[PUFO_SHOT_SOUND_COUNT];"""
assert old in s
open(p, 'w').write(s.replace(old, new))

p = 'src/pufo/game/pufo_combat.c'
s = open(p).read()
i = s.index('/* off_AA040 @ 0xaa040, 360 bytes = 180 u16, dumped from the IDB. Indexed by')
j = s.index('};', s.index('const u16 g_aShotSound[PUFO_SHOT_SOUND_COUNT] = {')) + 2
new = """/* off_AA040 @ 0xaa040 and word_AA168 @ 0xaa168, 148 u16 each, indexed by
 * ObjectRecFull.uSortKey. Dumped from the binary. */
const u16 g_aShotSound[PUFO_SHOT_SOUND_COUNT] = {
%s
};
const u16 g_aFireSoundBySortKey[PUFO_SHOT_SOUND_COUNT] = {
%s
};""" % (rows(shot, 12), rows(fire, 12))
open(p, 'w').write(s[:i] + new + s[j:])
print('patched')
