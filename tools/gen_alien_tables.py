"""Emit the Tact_SpawnAliens static data tables as C initialiser text.

These are the tables Tact_SpawnAliens (0x3a2ac) reads. Every extent here is closed by the image,
not taken from the pseudocode, and every index shape comes from ARM inside Tact_SpawnAliens:

  aXcom0                   0xA7BE8  9 * nUnitType                                    0x3AB04
  aAlienRaceCounts         0xA6618  3 * nTerrainVariant + bUnk15112 - 3             0x3A47C
  aAlienSecondaryCounts    0xA6658  same index                                      0x3A484
  aAlienRaceComposition    0xA6628  5 * nAlienSetIndex + i, i = 0..4                0x3A34C
  aAlienRankTypes          0xA6650  i, the composition slot whose count was spent   0x3A83C
  aAlienMissionTotals      0xAA020  nAlienSetIndex                                  0x3AB44
  aAlienCompositionDivisors 0xAA030 i, 0..7                                         0x3AB6C
  aAlienBeltObjects        0xAA008  7 - k, k = 0..7                                 0x3AD5C
  aAlienUnitStats          0xA8F38  32 * (nUnitType - 9)                            0x3A934

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
    return '\n'.join(indent + '{ ' + ', '.join('%5d' % v for v in r) + ' },'
                     for r in rows_of)


def stat_rows(stats):
    """One record per line, with the five nReady halfwords in their own brace group: they are an
    array member of AlienUnitStatRec and clang's -Wmissing-braces wants the subobject braced."""
    out = []
    for i in range(len(stats) // 16):
        r = stats[i * 16:i * 16 + 16]
        head = ', '.join('%5d' % v for v in r[:9])
        ready = '{ ' + ', '.join('%5d' % v for v in r[9:14]) + ' }'
        tail = ', '.join('%5d' % v for v in r[14:])
        out.append('    { %s, %s, %s },' % (head, ready, tail))
    return '\n'.join(out)


def name_rows(names, indent='    '):
    out = []
    for i, raw in enumerate(names):
        tail = ',' if i + 1 < len(names) else ''
        cells = ', '.join('%3d' % b for b in raw)
        out.append('%s/* %2d %-8s */ { %s }%s' % (indent, i, raw.split(b'\0')[0].decode(), cells, tail))
    return '\n'.join(out)


def emit():
    names = [rd(0xA7BE8 + 9 * i, 9) for i in range(23)]
    race_counts = block(0xA6618, 16, '16B')
    composition = block(0xA6628, 40, '40B')
    rank_types = block(0xA6650, 5, '5B')
    secondary = block(0xA6658, 16, '16B')
    belt = block(0xAA008, 16, '8h')
    totals = block(0xAA020, 16, '8H')
    divisors = block(0xAA030, 16, '8H')
    stats = block(0xA8F38, 14 * 32, '224h')

    print('/* ------------------------------------------------------------ alien mission tables')
    print(' *')
    print(' * The static data Tact_SpawnAliens (0x3a2ac) reads. Dumped from pufobeta.exe and checked')
    print(' * byte for byte by docs/parity/audit_tables.py. Extents come from the image, not from the')
    print(' * pseudocode; the reasoning is in docs/parity/game-core.md.')
    print(' *')
    print(' * Index shapes, all proven from ARM in Tact_SpawnAliens:')
    print(' *   g_aSpriteSetName            9 * nUnitType                              0x3AB04')
    print(' *   g_aAlienRaceCounts          3 * nTerrainVariant + nAlienTier - 3       0x3A47C')
    print(' *   g_aAlienSecondaryCounts     same index                                 0x3A484')
    print(' *   g_aAlienRaceComposition     5 * nAlienSetIndex + i, i = 0..4           0x3A34C')
    print(' *   g_aAlienRankTypes           i, the composition slot that was spent     0x3A83C')
    print(' *   g_aAlienMissionTotals       nAlienSetIndex                             0x3AB44')
    print(' *   g_aAlienCompositionDivisor  i, 0..7                                    0x3AB6C')
    print(' *   g_aAlienBeltObjects         7 - k, k = 0..7                            0x3AD5C')
    print(' *   g_aAlienUnitStats           32 * (nUnitType - 9)                       0x3A934')
    print(' */')
    print()

    print('/* aXcom0 @ 0xa7be8. One nine-byte sprite-set name per unit type, 23 entries. Entry 23 does')
    print(' * not exist: the byte at 0xA7CB7 is 00 and the bytes after it are 2E 00 22 01 "push".')
    print(' * Types 4..8 all read "tanks", which is why the deploy code can treat them as one group. */')
    print('const char g_aSpriteSetName[PUFO_ALIEN_SPRITE_SET_NAME_COUNT][9] = {')
    print(name_rows(names))
    print('};')
    print()

    print('/* aAlienRaceCounts @ 0xa6618 and aAlienSecondaryCounts @ 0xa6658. Both are 16 bytes and both')
    print(' * are indexed 3 * nTerrainVariant + nAlienTier - 3, so five terrain rows of three tiers.')
    print(' * The secondary column is only read when the lead race is neither 10 nor 12. The two are')
    print(' * parts of one 80-byte block: 0xa6618 + 16 = aAlienRaceComposition, + 56 = aAlienRankTypes,')
    print(' * + 64 = aAlienSecondaryCounts, and the block ends at 0xa6668 where "plane" begins. */')
    print('const u8 g_aAlienRaceCounts[16] = {')
    print(rows(race_counts, 3))
    print('};')
    print()
    print('const u8 g_aAlienSecondaryCounts[16] = {')
    print(rows(secondary, 3))
    print('};')
    print()

    print('/* aAlienRaceComposition @ 0xa6628. Eight rows of five, selected by nAlienSetIndex. Row 0 and')
    print(' * row 7 are all zero, so those mission sets spawn no aliens at all through this path. The')
    print(' * five slots are the race groups; aAlienRankTypes gives the bRankType each group gets. */')
    print('const u8 g_aAlienRaceComposition[8][5] = {')
    print(grouped([composition[i * 5:i * 5 + 5] for i in range(8)]))
    print('};')
    print()
    print('const u8 g_aAlienRankTypes[5] = {')
    print(rows(rank_types, 5))
    print('};')
    print()

    print('/* aAlienMissionTotals @ 0xaa020 and aAlienCompositionDivisor @ 0xaa030, both eight entries,')
    print(' * both indexed by nAlienSetIndex / slot. The recomposed count is')
    print(' * (g_aAlienMissionTotals[nAlienSetIndex] + nAlienPoolAddend) / g_aAlienCompositionDivisor[i]. */')
    print('const u16 g_aAlienMissionTotals[8] = {')
    print(rows(totals, 8))
    print('};')
    print()
    print('const u16 g_aAlienCompositionDivisor[8] = {')
    print(rows(divisors, 8))
    print('};')
    print()

    print('/* aAlienBeltObjects @ 0xaa008. Eight signed halfwords walked backwards, paired with a counter')
    print(' * running 14, 12, 10, 8, 6, 4, 2, 0. The -1 sentinel at index 2 is skipped by the counter')
    print(' * reaching 4, not by a test on the value. */')
    print('const i16 g_aAlienBeltObjects[8] = {')
    print(rows(belt, 8))
    print('};')
    print()

    print('/* aAlienUnitStats @ 0xa8f38. Fourteen 32-byte records, index nUnitType - 9, so unit types')
    print(' * 9 through 22. 14 * 32 = 448 ends at 0xa90f8, where the ASCII "MAN_0M0" name block begins,')
    print(' * which closes the extent at both ends. Field map, from ARM in Tact_SpawnAliens:')
    print(' *   +00 nTu      +02 nStat8   +04 nStat9   +06 nStat2   +08 nHp    +0A nStat3')
    print(' *   +0C nStat6   +0E nStat7   +10 nUnk248  +12..+1A nReady[5]      +1C nStat4  +1E nStat5')
    print(' * nStat6 == -1 means "roll both": stat6 and stat7 become rand(50) + 30 each and bPsiActive is')
    print(' * set to 1. nStat2, nStat3 and nHp each take a rand(20) - 10 jitter on top. */')
    print('const AlienUnitStatRec g_aAlienUnitStats[14] = {')
    print(stat_rows(stats))
    print('};')


def check():
    def hx(a, n):
        b = rd(a, n)
        return 'NOT FILE-BACKED' if b is None else ' '.join('%02X' % b[i] for i in range(n))

    print('spriteName  0xA7BE8 + 207 = 0x%X  tail:' % (0xA7BE8 + 207), hx(0xA7CB7, 8))
    print('raceCounts  0xA6618 + 16  = 0x%X  (aAlienRaceComposition at 0xA6628)' % (0xA6618 + 16))
    print('composition 0xA6628 + 40  = 0x%X  (aAlienRankTypes at 0xA6650)' % (0xA6628 + 40))
    print('rankTypes   0xA6650 + 5   = 0x%X  tail to aAlienSecondaryCounts:' % (0xA6650 + 5),
          hx(0xA6655, 3))
    print('secondary   0xA6658 + 16  = 0x%X  next bytes:' % (0xA6658 + 16), hx(0xA6668, 7))
    print('beltObjects 0xAA008 + 16  = 0x%X  tail to aAlienMissionTotals:' % (0xAA008 + 16),
          hx(0xAA018, 8))
    print('totals      0xAA020 + 16  = 0x%X  (aAlienCompositionDivisors at 0xAA030)' % (0xAA020 + 16))
    print('divisors    0xAA030 + 16  = 0x%X  next table (g_aShotSound at 0xAA040):' % (0xAA030 + 16),
          hx(0xAA040, 8))
    print('unitStats   0xA8F38 + 448 = 0x%X  next bytes:' % (0xA8F38 + 448), hx(0xA90F8, 16))


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--check':
        check()
    else:
        emit()
