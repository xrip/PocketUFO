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
    u8   gap_B9[1];        /* 0xb9 */
    u16  uManufWorkPerUnit;/* 0xba the manufacturing work one unit of this item costs: the divisor
                            * that turns an allocated quantity into finished units.
                            * ManufScreen_CommitProduction (0x6F3EC, not ported) does
                            * `LDRH R0,[R3,#0xBA]` and hands it to _rt_sdiv as the DIVISOR of the
                            * accumulated allocation, so the quotient is how many units the work paid
                            * for, compared against the units already committed (the u16 table at
                            * 0x15D09C). ManufScreen_Draw (0x6E704) and ManufScreen_BuildPanels
                            * (0x6F060), both not ported, use it the same way for the panel percent:
                            * accumulated * 100 / (this * target). BuySellScreen_BuildControls @ 0x76318
                            * requires it to be ZERO before a slot gets a buy row, and in the shipped
                            * OBJECTS.DAT the two sets are exactly complementary -- the 35 records with
                            * a non-zero value (2, 3, 5, 6, 9, 11, 12, 13, 18, 20, 21, 23, 24, 28, 29,
                            * 30, 33, 34, 35, 37, 39, 49, 50, 51, 55, 58, 106, 107, 109, 119, 120, 121,
                            * 122, 123, 124) are precisely the manufacturable items (Laser pistol 576,
                            * Plasma cannon 960, Fusion ball 1056, Power suit 1968, Fly suit 2784,
                            * FIRE STORM 27984, LIGTHNING 35712, AVENGER 55712), every one has a zero
                            * uPrice, and they are the only records with any abManufComponents byte
                            * set. The two craft that are never built (117 Skyranger, 118 Interceptor)
                            * are zero here; 119..121 are the three the manufacturing hangar branch
                            * covers. The byte at 0xb9 is zero in every shipped record and is never
                            * read. */
    u32  uPrice;           /* 0xbc the item price: Geoscape_InitUI @ 0x86A14 seeds it from
                            * dword_AA2C0 (g_anObjectPrice), and the BuySell screens read it
                            * back. A dword store (`+188`), not a u16. */
    u8   abManufComponents[4];
                           /* 0xc0 the four components one manufactured unit consumes, in the order
                            * the original tests them: [0] against apObjects[105] Elerium-115, [1]
                            * against [106] Alien alloys, [2] against [107] UFO power source, [3]
                            * against [109] UFO navigation -- slot 108 (Alien surgery) is never a
                            * component. ManufScreen_CommitProduction gates a product on them at
                            * 0x6F580..0x6F5C8 (blocked when the base's stock is below the
                            * requirement) and then subtracts the same four bytes from those same four
                            * stocks through BaseRec_SubItemQty at 0x6F6C8..0x6F708. Shipped values
                            * that make the order obvious: Plasma cannon 15 Elerium, Mind probe 1
                            * Elerium, Fly suit 6 Elerium + 16 alloys, FIRE STORM 65 alloys + 1 power
                            * source + 1 navigation, LIGTHNING 85 + 1 + 1, AVENGER 120 + 2 + 1. Only
                            * the 35 manufacturable records have any of them set. */
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
    OBJ_CRAFT_CANNON_ROUNDS      = 52, /* rec 1498 "Craft cannon rounds[x50]", the Craft cannon's ammo */
    OBJ_STINGRAY_ROCKET          = 53, /* rec 1499 "Stingray rocket" */
    OBJ_AVALANCHE_ROCKET         = 54, /* rec 1500 "Avalanche rocket" */
    OBJ_FUSION_BALL              = 55, /* rec 1501 "Fusion ball" */
    OBJ_TANK_CANNON_SHELLS       = 56, /* rec 1502 "Tank - cannon shells" */
    OBJ_SECTOID_CORPSE           = 59, /* rec 1505 "Sectoid corpse" */
    OBJ_SNAKEMAN_CORPSE          = 60, /* rec 1506 "Snakeman corpse" */
    OBJ_FLOATER_CORPSE           = 61, /* rec 1507 "Floater corpse" */
    OBJ_MUTON_CORPSE             = 62, /* rec 1508 "Muton corpse" */
    OBJ_ETHEREAL_CORPSE          = 63, /* rec 1509 "Ethereal corpse" */
    OBJ_CYBERDISK_REMAINS        = 64, /* rec 1510 "Cyberdisk remains" */
    OBJ_CHRYSSALID_CORPSE        = 65, /* rec 1511 "Chryssalid corpse" */
    OBJ_REAPER_CORPSE            = 66, /* rec 1512 "Reaper corpse", the corpse of the Riper at 101 */
    OBJ_CELATID_CORPSE           = 67, /* rec 1513 "Celatid corpse" */
    OBJ_SILACOID_CORPSE          = 68, /* rec 1514 "Silacoid corpse" */
    OBJ_SECTOPOD_REMAINS         = 69, /* rec 1515 "Sectopod remains" */
    OBJ_OPERATIVE_CORPSE         = 70, /* rec 1516, Russian for "operative corpse" */
    OBJ_OPERATIVE_CORPSE_B       = 72, /* rec 1518, the same shipped name a third time */
    OBJ_MALE_CORPSE              = 73, /* rec 1519, Russian for "male corpse" */
    OBJ_TANK_WRECKAGE            = 75, /* rec 1521, Russian for "tank wreckage" */
    /* 77..104 is the alien-soldier block, five race groups of five ranks plus the four
     * singletons at 99..104. Smap_GrantSiteRecovery treats the whole block (with 114 and
     * 144..148) as the containment-gated slots; the group ends below are its switch bounds. */
    OBJ_SECTOID_SOLDIER          = 77, /* rec 1523 "Sectoid soldier", first alien slot */
    OBJ_SECTOID_NAVIGATOR        = 79, /* rec 1525 "Sectoid navigator" */
    OBJ_SECTOID_LEADER           = 80, /* rec 1526 "Sectoid lider" (sic) */
    OBJ_SECTOID_COMMANDER        = 81, /* rec 1527 "Sectoid commander", last Sectoid */
    OBJ_SNAKEMAN_SOLDIER         = 82, /* rec 1528 "Snakeman soldier" */
    OBJ_SNAKEMAN_COMMANDER       = 86, /* rec 1532 "Snakeman commander", last Snakeman */
    OBJ_FLOATER_SOLDIER          = 87, /* rec 1533 "Floater soldier", first Floater */
    OBJ_FLOATER_ENGINEER         = 88, /* rec 1534 "Floater engineer" */
    OBJ_FLOATER_NAVIGATOR        = 89, /* rec 1535 "Floater navigator" */
    OBJ_FLOATER_COMMANDER        = 91, /* rec 1537 "Floater commander", last Floater */
    OBJ_MUTON_NAVIGATOR          = 94, /* rec 1540 "Muton navigator", last Muton */
    OBJ_ETHEREAL_ENGINEER        = 96, /* rec 1542 "Ethereal engineer" */
    OBJ_ETHEREAL_COMMANDER       = 98, /* rec 1544 "Ethereal commander", last Ethereal */
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
    OBJ_UFO_NAVIGATION           = 109, /* rec 1555 "UFO navigation" */
    OBJ_ETHEREAL_NAVIGATOR       = 114, /* rec 1560 "Ethereal navigator", the alien slot outside 77..104 */
    /* 117..121 are the per-site new-craft counts Smap_GrantSiteRecovery spawns from; the shipped
     * name of each slot is the craft type it holds, and the spawn maps every slot onto its own
     * CraftType (117->0, 118->3, 119->4, 120->1, 121->2). */
    OBJ_CRAFT_COUNT_SKYRANGER    = 117, /* rec 1563 "SKYRANGER" */
    OBJ_CRAFT_COUNT_INTERCEPTOR  = 118, /* rec 1564 "INTERCEPTOR" */
    OBJ_CRAFT_COUNT_FIRESTORM    = 119, /* rec 1565 "FIRE STORM" */
    OBJ_CRAFT_COUNT_LIGHTNING    = 120, /* rec 1566 "LIGTHNING" (sic) */
    OBJ_CRAFT_COUNT_AVENGER      = 121, /* rec 1567 "AVENGER" */
    OBJ_SNAKEMAN_MEDIC           = 144, /* rec 1590 "Snakeman medic", first of the medic block */
    OBJ_MUTON_MEDIC              = 145, /* rec 1591 "Muton medic" */
    OBJ_FLOATER_MEDIC            = 146, /* rec 1592 "Floater medic" */
    OBJ_ETHEREAL_MEDIC           = 147, /* rec 1593 "Ethereal medic" */
    OBJ_SECTOID_MEDIC            = 148  /* rec 1594 "Sectoid medic", last of the medic block */
} ObjectSlot;

/* bAmmoCap 255 means the weapon never runs out of ammo. Readers test this exact value rather than
 * treating 255 as a large count: the shot gate (pufo_combat.c), the inventory drag stack
 * (pufo_invdrag.c), the ammo display (pufo_unit.c) and the cargo pass all branch on it. */
#define PUFO_AMMO_CAP_UNLIMITED 0xFF

#endif /* PUFO_OBJECTREC_H */
