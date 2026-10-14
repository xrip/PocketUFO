/* pufo_objectrec.h - the item/weapon record the game loads from objects.dat.
 *
 * Evidence: the record is 208 bytes (0xD0); `Assets_LoadStartup @ 0x382d0` reads
 * objects.dat with `count = size / 0xD0` and builds a parallel pointer array.
 *
 * This header exists so the record has exactly one definition. `pufo_mapcell.h`
 * used to carry a 165-byte stand-in (`ObjectRec`, only `bListPri` named) because
 * the cell layer needs nothing else, and every module that needed a later field
 * had to work around it with an injected hook. `TactItem.pObj` points at the real
 * 208-byte record, so the stand-in was a type lie: reading `uSortKey` at 0xC8
 * through it reads past the end of the declared type.
 */
#ifndef PUFO_OBJECTREC_H
#define PUFO_OBJECTREC_H

#include "pufo_common.h"

/* All scalars, no pointers, so the layout must match on every target. */
typedef struct ObjectRecFull {
    u8   gap_0[4];         /* 0x00 */
    char szName[32];       /* 0x04 the object name; Geoscape_InitUI @ 0x86A58 copies text
                            * record 1447+slot here (slots 0..147). The field ends where
                            * nObjParam begins; the shipped names fit. */
    i8   nObjParam;        /* 0x24 used by the LOS ceiling test */
    i8   nDrawH;           /* 0x25 */
    u8   bUnk38;           /* 0x26 */
    u8   bUnk39;           /* 0x27 */
    u8   bTwoHanded;       /* 0x28 */
    u8   bUnk41;           /* 0x29 */
    u16  auPerBaseVal[8];  /* 0x2a */
    u16  aItemQty[52];     /* 0x3a */
    u16  uIconId;          /* 0xa2 */
    u8   bListPri;         /* 0xa4 list priority, used by MapCell_ListInsertByPri */
    u8   bWeight;          /* 0xa5 */
    u8   nFootW;           /* 0xa6 */
    u8   nFootH;           /* 0xa7 */
    u16  anAmmoTypeId[3];  /* 0xa8 */
    u8   bAmmoCap;         /* 0xae 255 means the weapon never runs out of ammo */
    u8   bFireType;        /* 0xaf fire type, and the base of the per-mode table */
    u8   abCostPct[3];     /* 0xb0 per-mode accuracy percent, read as bFireType[mode + 1] */
    u8   abArmorTu[3];     /* 0xb3 per-mode TU cost basis, auStat[0] * abArmorTu[m] / 100 */
    u16  uFirePower;       /* 0xb6 */
    u8   nCategory;        /* 0xb8 */
    u8   gap_B9[3];        /* 0xb9 */
    u32  uPrice;           /* 0xbc the item price: Geoscape_InitUI @ 0x86A14 seeds it from
                            * dword_AA2C0 (g_anObjectPrice), and the BuySell screens read it
                            * back. A dword store (`+188`), not a u16. */
    u8   gap_C0[4];        /* 0xc0 */
    u8   bReadySel;        /* 0xc4 readiness source selector, read by
                            * Unit_ApplyCellDamage @ 0x2e5d0 and
                            * Combat_ResolveShot @ 0x2f12c */
    u8   gap_C5[1];        /* 0xc5 */
    u8   bCargoSize;       /* 0xc6 the per-unit size the cargo hold test charges:
                            * Base_CalcWeaponStorageUsed sums bCargoSize * aItemQty[craft],
                            * and Craft_LoadStoresFromBase gates each single unit on
                            * used + bCargoSize < Craft_GetTypeCargoCap. IDA shows it as
                            * gap_B9[13] or +198 depending on the base register. */
    u8   gap_C7[1];        /* 0xc7 */
    u16  uSortKey;         /* 0xc8 weapon id: sound index, research index key, category */
    u16  uRecovScore;      /* 0xca */
    u16  uRecovCount;      /* 0xcc */
    u8   gap_CE[2];        /* 0xce */
} ObjectRecFull;

_Static_assert(sizeof(ObjectRecFull) == 208, "ObjectRecFull must be 208 bytes");

/* Three fire modes; abCostPct and abArmorTu are parallel arrays indexed by them. */
#define PUFO_FIRE_MODE_COUNT 3

/* bAmmoCap 255 means the weapon never runs out of ammo. Readers test this exact value rather than
 * treating 255 as a large count: the shot gate (pufo_combat.c), the inventory drag stack
 * (pufo_invdrag.c), the ammo display (pufo_unit.c) and the cargo pass all branch on it. */
#define PUFO_AMMO_CAP_UNLIMITED 0xFF

#endif /* PUFO_OBJECTREC_H */
