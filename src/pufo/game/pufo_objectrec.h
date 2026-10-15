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
    char szName[36];       /* 0x00 the object name, and it starts at the record's first byte.
                            * Two independent proofs. (1) The shipped OBJECTS.DAT record already
                            * carries it here: slot 59 begins "Sectoid corpse", slot 77 "Sectoid
                            * soldier". (2) Geoscape_InitUI writes it at record offset 0 -- 0x86A64
                            * loads apObjects[counter+1] and 0x86A70 is STRB R2,[R0] with no
                            * offset -- from text record 1446+slot, and the shipped record name
                            * equals that text record for every slot checked. The field ends where
                            * nObjParam begins at 0x24, so it is 36 bytes (the IDB's ObjectRec has
                            * char gap_0[36] here). */
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

/* aItemQty has 52 entries; Game_StartNew clears [0..49] and leaves the last two. */
#define PUFO_ITEM_QTY_COUNT 52

/* The object slots the port reaches by number. The NUMBERS are the original's own apObjects
 * indexing (the IDB names several of them Map_apObjectsN). The NAMES are the shipped
 * OBJECTS.DAT record name at that slot (main.dat, name at record+0x00), which is also what
 * Geoscape_InitUI @ 0x86A58 writes there from text record 1446+slot; the record number is in
 * the comment. Every name below used to be one slot high: the copy was read as "record
 * 1447+slot into slot i", while 0x86A64 loads apObjects[counter+1] and 0x86A58 uses
 * 1447+counter, so slot j is named by record 1446+j. Slots 70..72 share one shipped name; the
 * Russian names are glossed here. */
typedef enum ObjectSlot {
    OBJ_TANK_CANNON              = 31, /* rec 1477 "Tank - cannon" */
    OBJ_HOVERTANK_PLASMA         = 34, /* rec 1480 "Hovertank - plasma" */
    OBJ_HEAVY_CANNON_AP_CLIP     = 43, /* rec 1489 "Heavy cannon [AP] clip" */
    OBJ_TANK_CANNON_SHELLS       = 56, /* rec 1502 "Tank - cannon shells" */
    OBJ_CELATID_CORPSE           = 67, /* rec 1513 "Celatid corpse" */
    OBJ_SILACOID_CORPSE          = 68, /* rec 1514 "Silacoid corpse" */
    OBJ_SECTOPOD_REMAINS         = 69, /* rec 1515 "Sectopod remains" */
    OBJ_OPERATIVE_CORPSE         = 70, /* rec 1516, Russian for "operative corpse" */
    OBJ_OPERATIVE_CORPSE_B       = 72, /* rec 1518, the same shipped name a third time */
    OBJ_MALE_CORPSE              = 73, /* rec 1519, Russian for "male corpse" */
    OBJ_TANK_WRECKAGE            = 75, /* rec 1521, Russian for "tank wreckage" */
    OBJ_SECTOID_NAVIGATOR        = 79, /* rec 1525 "Sectoid navigator" */
    OBJ_SECTOID_LEADER           = 80, /* rec 1526 "Sectoid lider" (sic) */
    OBJ_SECTOID_COMMANDER        = 81, /* rec 1527 "Sectoid commander" */
    OBJ_SNAKEMAN_SOLDIER         = 82, /* rec 1528 "Snakeman soldier" */
    OBJ_FLOATER_ENGINEER         = 88, /* rec 1534 "Floater engineer" */
    OBJ_FLOATER_NAVIGATOR        = 89, /* rec 1535 "Floater navigator" */
    OBJ_ETHEREAL_ENGINEER        = 96, /* rec 1542 "Ethereal engineer" */
    OBJ_CYBERDISK                = 99, /* rec 1545 "Cyberdisk" */
    OBJ_CHRYSSALID               = 100, /* rec 1546 "Chryssalid" */
    OBJ_RIPER                    = 101, /* rec 1547 "Riper"; its corpse at 66 is "Reaper corpse" */
    OBJ_SILACOID                 = 102, /* rec 1548 "Silacoid" */
    OBJ_CELACID                  = 103, /* rec 1549 "Celacid" */
    OBJ_SECTOPOD                 = 104, /* rec 1550 "Sectopod" */
    OBJ_ELERIUM_115              = 105, /* rec 1551 "Elerium-115" */
    OBJ_ALIEN_ALLOYS             = 106, /* rec 1552 "Alien alloys" */
    OBJ_UFO_POWER_SOURCE         = 107, /* rec 1553 "UFO power source" */
    OBJ_ALIEN_SURGERY            = 108, /* rec 1554 "Alien surgery" */
    OBJ_UFO_NAVIGATION           = 109  /* rec 1555 "UFO navigation" */
} ObjectSlot;

/* bAmmoCap 255 means the weapon never runs out of ammo. Readers test this exact value rather than
 * treating 255 as a large count: the shot gate (pufo_combat.c), the inventory drag stack
 * (pufo_invdrag.c), the ammo display (pufo_unit.c) and the cargo pass all branch on it. */
#define PUFO_AMMO_CAP_UNLIMITED 0xFF

#endif /* PUFO_OBJECTREC_H */
