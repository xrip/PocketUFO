"""Emit the nine iso unit-animation tables as C initialiser text.

These are the static tables MapView_DrawUnit (0x1c958) reads. Every extent here comes from the
image rather than the pseudocode, and every index shape comes from ARM inside MapView_DrawUnit:

  aUnitWalkKind      0xA6B00  5 * (bFacing + 16 * nUnitType + 8 * bWalkHalf) + bAnimStep   0x1CF6C
  aUnitAnimFrame     0xA7230  6 * nAnimFrame, three u16 per frame                           0x1D048
  aUnitWalkGlyph     0xA7408  27 * nUnitType + 9 * bUnitFlag4 + nDirection                 0x1CBD0
  aPoseFrameTable    0xA7660  5 * (nAnimParam + aUnitTypePoseBase[nUnitType]) + nWalkKind  0x1CFE4
  aUnitTypePoseBase  0xA7958  nUnitType                                                     0x1CFE0
  aUnitHeightOff     0xA7A28  8 * nUnitType + nAnimPhase                                    0x1D190
  aBobPhaseY         0xA7BE0  Map + 0x37                                                    0x1D568
  aUnitWeaponYOff    0xA9310  nUnitType                                                     0x1D3F4
  aUnitFacingDrawOff 0xA93A8  2 * (8 * nUnitType + bFacing) + nAxis                         0x1D290

Run with no argument to print the C block; run with --check to print the boundary evidence the
extents rest on.
"""
import struct
import sys

from dump_pe import rd


def block(va, n, fmt):
    b = rd(va, n)
    if b is None:
        raise SystemExit('not file-backed: %s' % hex(va))
    return list(struct.unpack('<' + fmt, b))


def rows(vals, per, indent='    '):
    out = []
    for i in range(0, len(vals), per):
        tail = ',' if i + per < len(vals) else ''
        out.append(indent + ', '.join('%4d' % v for v in vals[i:i + per]) + tail)
    return '\n'.join(out)


def grouped(rows_of, indent='    '):
    """Emit nested rows: each entry is one brace group."""
    return '\n'.join(indent + '{ ' + ', '.join('%4d' % v for v in r) + ' },'
                     for r in rows_of)


def walk_glyph_rows():
    """22 types, each three flag4 rows of nine directions."""
    b = rd(0xA7408, 22 * 27)
    out = []
    for t in range(22):
        rows = ['{ ' + ', '.join('%4d' % v for v in b[t * 27 + f * 9: t * 27 + f * 9 + 9]) + ' }'
                for f in range(3)]
        out.append('    /* type %2d */ { %s },' % (t, ',\n                   '.join(rows)))
    return '\n'.join(out)


def facing_off_rows():
    """14 types, each eight facings of two axes."""
    b = rd(0xA93A8, 14 * 16)
    out = []
    for t in range(14):
        rows = ['{ %3d, %3d }' % (struct.unpack_from('<2b', b, t * 16 + f * 2))
                for f in range(8)]
        out.append('    /* type %2d */ { %s },' % (t, ',\n                   '.join(rows)))
    return '\n'.join(out)


def emit():
    walk_kind = block(0xA6B00, 23 * 80, '1840B')
    anim_frame = block(0xA7230, 78 * 6, '234H')
    pose_frame = block(0xA7660, 152 * 5, '760B')
    pose_base = block(0xA7958, 23, '23B')
    height_off = block(0xA7A28, 23 * 8, '184b')
    bob_phase = block(0xA7BE0, 8, '8b')
    weapon_y = block(0xA9310, 23, '23b')

    print('/* ------------------------------------------------------------ iso unit animation tables')
    print(' *')
    print(' * The nine static tables MapView_DrawUnit (0x1c958) reads. Dumped from pufobeta.exe and')
    print(' * checked byte for byte by docs/parity/audit_tables.py. Extents come from the image, not')
    print(' * from the pseudocode; the reasoning is in docs/parity/game-core.md.')
    print(' *')
    print(' * Index shapes, all proven from ARM in MapView_DrawUnit:')
    print(' *   g_aUnitWalkKind      5 * (bFacing + 16 * nUnitType + 8 * bWalkHalf) + bAnimStep  0x1CF6C')
    print(' *   g_aUnitAnimFrame      3 * nAnimFrame + k, three u16 per frame                    0x1D048')
    print(' *   g_aUnitWalkGlyph     27 * nUnitType + 9 * bUnitFlag4 + nDirection                0x1CBD0')
    print(' *   g_aPoseFrameTable    5 * (nAnimParam + g_aUnitTypePoseBase[nUnitType]) + nWalkKind 0x1CFE4')
    print(' *   g_aUnitTypePoseBase   nUnitType                                                  0x1CFE0')
    print(' *   g_aUnitHeightOff      8 * nUnitType + nAnimPhase                                 0x1D190')
    print(' *   g_aBobPhaseY          Map + 0x37                                                 0x1D568')
    print(' *   g_aUnitWeaponYOff     nUnitType                                                  0x1D3F4')
    print(' *   g_aUnitFacingDrawOff  2 * (8 * nUnitType + bFacing) + nAxis                      0x1D290')
    print(' */')
    print()

    print('/* aUnitWalkKind @ 0xa6b00. 23 unit types x 80 bytes. Each type block is two 40-byte halves,')
    print(' * selected by the ADDNE #8 at 0x1CF78, and each half is eight facings x five walk steps.')
    print(' * The five values are a permutation of the animation slots for that facing. */')
    print('const u8 g_aUnitWalkKind[PUFO_ISO_WALK_KIND_COUNT] = {')
    print(rows(walk_kind, 10))
    print('};')
    print()

    print('/* aUnitAnimFrame @ 0xa7230. 78 frames of three u16: a base, a phase scale and a facing scale.')
    print(' * 78 * 6 = 468 bytes and the four bytes after it are zero padding, so the table ends at')
    print(' * 0xA7404 rather than at the 0xA7408 where aUnitWalkGlyph begins. */')
    print('const u16 g_aUnitAnimFrame[PUFO_ISO_ANIM_FRAME_COUNT][3] = {')
    print(grouped([anim_frame[i * 3:i * 3 + 3] for i in range(78)]))
    print('};')
    print()

    print('/* aUnitWalkGlyph @ 0xa7408. 27 bytes per unit type: three rows of nine, one row per value')
    print(' * of bUnitFlag4. Types 0 through 21 have data and type 22 does not - 27 * 22 = 594 and the')
    print(' * pose table begins six bytes later, at 0xA7660. */')
    print('const u8 g_aUnitWalkGlyph[PUFO_ISO_WALK_GLYPH_TYPE_COUNT][3][9] = {')
    print(walk_glyph_rows())
    print('};')
    print()

    print('/* aPoseFrameTable @ 0xa7660. 152 rows of five. The row is nAnimParam plus the unit type\'s')
    print(' * base from g_aUnitTypePoseBase, and the column is the walk-kind value just read out of')
    print(' * g_aUnitWalkKind. 152 rows is forced, not guessed: the largest base is 133, the largest')
    print(' * pose offset is 17, and 5 * (133 + 17) + 4 = 754 fits 760 and nothing smaller. */')
    print('const u8 g_aPoseFrameTable[PUFO_ISO_POSE_FRAME_COUNT][5] = {')
    print(grouped([pose_frame[i * 5:i * 5 + 5] for i in range(152)]))
    print('};')
    print()

    print('/* aUnitTypePoseBase @ 0xa7958. One row base per unit type. Types 0 through 10 share row 0;')
    print(' * the rest are multiples of 19. Type 22 is 133, which is what fixes the pose table extent. */')
    print('const u8 g_aUnitTypePoseBase[PUFO_ISO_UNIT_TYPE_COUNT] = {')
    print(rows(pose_base, 12))
    print('};')
    print()

    print('/* aUnitHeightOff @ 0xa7a28. Eight signed bytes per unit type, indexed by the animation')
    print(' * phase. 23 * 8 = 184 bytes, and aDirDeltaXY begins at 0xA7AE0, so the extent is closed')
    print(' * at both ends. */')
    print('const i8 g_aUnitHeightOff[PUFO_ISO_UNIT_TYPE_COUNT][8] = {')
    print(grouped([height_off[i * 8:i * 8 + 8] for i in range(23)]))
    print('};')
    print()

    print('/* aBobPhaseY @ 0xa7be0. The bob cycle for the eight animation phases. The extent is not a')
    print(' * guess: the very next bytes are the ASCII string xcom_0. */')
    print('const i8 g_aBobPhaseY[8] = {')
    print(rows(bob_phase, 8))
    print('};')
    print()

    print('/* aUnitWeaponYOff @ 0xa9310. One signed byte per unit type. Only type 9 (5) and type 11')
    print(' * (-1) are not zero. */')
    print('const i8 g_aUnitWeaponYOff[PUFO_ISO_UNIT_TYPE_COUNT] = {')
    print(rows(weapon_y, 12))
    print('};')
    print()

    print('/* aUnitFacingDrawOff @ 0xa93a8. Sixteen signed bytes per unit type, two per facing, eight')
    print(' * facings. The table is 224 bytes, so it covers 14 unit types and no more. The next table,')
    print(' * unnamed in the IDB at 0xA9488, is a u16 table Map_TraceVoxelOccupancy (0x3242c) indexes')
    print(' * as 4 * (8 * nUnitType + bFacing); it runs 736 bytes and ends exactly at')
    print(' * g_aUnitMuzzleOffset @ 0xa9768. A unit of type 14 or above')
    print(' * that reaches the facing branch at 0x1D288 reads into that other table. */')
    print('const i8 g_aUnitFacingDrawOff[PUFO_ISO_FACING_OFF_TYPE_COUNT][8][2] = {')
    print(facing_off_rows())
    print('};')


def check():
    def hx(a, n):
        b = rd(a, n)
        return 'NOT FILE-BACKED' if b is None else ' '.join('%02X' % x for x in b)

    print('walkKind  0xA6B00 + 1840 = 0x%X  (aUnitAnimFrame at 0xA7230)' % (0xA6B00 + 1840))
    print('animFrame last two frames 0xA73F8:', hx(0xA73F8, 12), ' padding:', hx(0xA7404, 4))
    print('walkGlyph 0xA7408 + 594 = 0x%X  tail to aPoseFrameTable:' % (0xA7408 + 594),
          hx(0xA765A, 6))
    print('poseFrame 0xA7660 + 760 = 0x%X  (aUnitTypePoseBase at 0xA7958)' % (0xA7660 + 760))
    print('poseBase  0xA7958 + 23  = 0x%X' % (0xA7958 + 23))
    print('heightOff 0xA7A28 + 184 = 0x%X  (aDirDeltaXY at 0xA7AE0)' % (0xA7A28 + 184))
    print('bobPhaseY 0xA7BE0 + 8   = 0x%X  next bytes:' % (0xA7BE0 + 8), hx(0xA7BE8, 7))
    print('weaponY   0xA9310 + 23  = 0x%X' % (0xA9310 + 23))
    print('facingOff 0xA93A8 + 224 = 0x%X  next table:' % (0xA93A8 + 224), hx(0xA9488, 16))
    print('word_A9488 0xA9488 + 736 = 0x%X  (g_aUnitMuzzleOffset at 0xA9768)' % (0xA9488 + 736))


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--check':
        check()
    else:
        emit()
