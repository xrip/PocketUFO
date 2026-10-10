/* pufo_boot.h - startup asset loading.
 *
 * Evidence: Assets_LoadStartup @ 0x382d0, GameAssets_Shutdown @ 0x38e4c,
 *           Tact_LoadLoftData @ 0x3bd6c.
 *
 * Assets_LoadStartup opens the two archives, then loads, in this order:
 *
 *   1. objects.dat  -> ObjectRec array, count = size / 0xD0, with the parallel
 *                      pointer array apObjects[i] = &blob[i]
 *   2. fontsize.dat -> the 256-byte character advance table
 *   3. texts.dat    -> a leading u16 format marker, then (size - 2) / 200 records
 *                      of 200 bytes each, expanded into memory at a stride of 205
 *   4. the 240x320 16bpp rendering surface
 *   5. class.pal into palette slot 0x12
 *   6. three pad images (cs0, cs1, cs2) at mode 18, with origins zeroed
 *
 * Two details that are easy to get wrong and are reproduced:
 *
 *   - texts.dat records are 200 bytes on disk but 205 bytes apart in memory. The
 *     extra 5 bytes are slack so a NUL terminator plus padding always fits; the
 *     original writes the 200 bytes then leaves the remaining 5 untouched. The
 *     allocation is therefore records * 205, not records * 200.
 *   - the record count is (size - 2) / 200, NOT size / 200, because the leading
 *     u16 is a format marker. In the shipped archives that marker is 200 for both
 *     lang.dat and main.dat, which makes it look like a record size; it is not.
 */
#ifndef PUFO_BOOT_H
#define PUFO_BOOT_H

#include "asset/pufo_asset.h"
#include "game/pufo_font.h"
#include "game/pufo_mapcell.h"
#include "pufo_common.h"
#include "render/pufo_render.h"

/* ObjectRec: 208 bytes, all scalars, so the layout must match on every target. */
typedef struct ObjectRecFull {
    u8   gap_0[36];        /* 0x00 */
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
    u8   bAmmoCap;         /* 0xae */
    u8   bFireType;        /* 0xaf */
    u8   gap_AF[3];        /* 0xb0 */
    u8   abArmorTu[3];     /* 0xb3 */
    u16  uFirePower;       /* 0xb6 */
    u8   nCategory;        /* 0xb8 */
    u8   gap_B9[11];       /* 0xb9 */
    u8   bReadySel;        /* 0xc4 readiness source selector, read by Unit_ApplyCellDamage @ 0x2e5d0 and Combat_ResolveShot @ 0x2f12c */
    u8   gap_C5[3];        /* 0xc5 */
    u16  uSortKey;         /* 0xc8 */
    u16  uRecovScore;      /* 0xca */
    u16  uRecovCount;      /* 0xcc */
    u8   gap_CE[2];        /* 0xce */
} ObjectRecFull;

_Static_assert(sizeof(ObjectRecFull) == 208, "ObjectRecFull must be 208 bytes");

/* texts.dat record geometry, from Assets_LoadStartup. */
#define PUFO_TEXT_REC_SIZE_DISK 200
#define PUFO_TEXT_REC_STRIDE    205 /* 5 bytes of slack per record in memory */
#define PUFO_TEXT_HDR_SIZE      2

/* String tables extracted from the texts.dat blob by Assets_LoadStartup @ 0x382d0.
 *
 * The authoritative facts are the loop counts and strides in that function:
 *
 *   0xb1140  aszTextB  for (j = 23; ...)  stride 40   23 x 40 =  920
 *   0xb14d8  aszTextC  for (k = 71; ...)  stride 20   71 x 20 = 1420
 *   0xb1a68  aszTextD  for (m = 88; ...)  stride 20   88 x 20 = 1760
 *   0xb2148  aszTextA  for (i =  9; ...)  stride 20    9 x 20 =  180
 *   0xb2200  aszTextE  for (n =  6; ...)  stride 20    6 x 20 =  120
 *   0xb3768  aszTextF  for (ii = 10; ...) stride 30   10 x 30 =  300
 *   0xb3894  szTextG    strcpy until NUL, up to 0xb38b2
 *   0xb38b2  szTextH    strcpy until NUL, up to aTextRecs @ 0xb38d0
 *
 * Two spans close exactly on the next buffer (B -> C at 0xb14d8 and D -> A at
 * 0xb2148), which cross-checks those two counts. The others do NOT: C ends at
 * 0xb1a64 with the next buffer at 0xb1a68, and A ends at 0xb21fc with the next at
 * 0xb2200. Those 4-byte gaps are alignment padding between globals, so the loop
 * count stays the source of truth and the earlier "spans close exactly" reading
 * was wrong. szTextG/szTextH are plain NUL-terminated strings with no recorded
 * length, so their capacity is inferred from the following global, not a count.
 *
 * Every source offset into the loaded text blob is an exact multiple of the
 * 205-byte record stride (221, 198, 318, 230, 192, 181, 1609, 191 records
 * respectively), which independently confirms the stride:
 *
 *   A 0xbe9c9   B 0xbd75e   C 0xc3776   D 0xbf0fe
 *   E 0xbd290   F 0xbc9c1   G 0x104145  H 0xbd1c3
 *
 * Which table holds what, from the consumers:
 *   aszTextD  first names: 0..69 male, 70..87 female, which is why
 *             Soldier_Generate @ 0x49410 draws Game_RandU16(0x46) = 0..69 for
 *             male and Game_RandU16(0x12) + 70 for female
 *   aszTextC  last names: 0..63 male, female from entry 47 upward
 *   aszTextE  XCom rank names, indexed by bRankType (6 entries)
 *   aszTextA  alien rank names, indexed by bRankType (9 entries)
 *   aszTextB  base/facility names (23 entries, 40 bytes each)
 *   aszTextF  craft/terrain names (10 entries, 30 bytes each)
 */
#define PUFO_TXT_B_COUNT  23
#define PUFO_TXT_B_STRIDE 40
#define PUFO_TXT_C_COUNT  71
#define PUFO_TXT_C_STRIDE 20
#define PUFO_TXT_D_COUNT  88
#define PUFO_TXT_D_STRIDE 20
#define PUFO_TXT_A_COUNT  9
#define PUFO_TXT_A_STRIDE 20
#define PUFO_TXT_E_COUNT  6
#define PUFO_TXT_E_STRIDE 20
#define PUFO_TXT_F_COUNT  10
#define PUFO_TXT_F_STRIDE 30
#define PUFO_TXT_G_STRIDE 20
#define PUFO_TXT_H_STRIDE 20
/* szTextG/szTextH capacity, inferred from the next global rather than a count. */
#define PUFO_TXT_G_CAP 30
#define PUFO_TXT_H_CAP 30

/* Source byte offsets into the loaded text blob (aTextRecs @ 0xb38d0). */
#define PUFO_TXT_A_SRC 45305u
#define PUFO_TXT_B_SRC 40590u
#define PUFO_TXT_C_SRC 65190u
#define PUFO_TXT_D_SRC 47150u
#define PUFO_TXT_E_SRC 39360u
#define PUFO_TXT_F_SRC 37105u
#define PUFO_TXT_G_SRC 329845u
#define PUFO_TXT_H_SRC 39155u

/* The extracted tables. Allocated by Boot_LoadStartup from the text blob. */
typedef struct PufoTextTables {
    char (*pB)[PUFO_TXT_B_STRIDE]; /* 23 x 40 */
    char (*pC)[PUFO_TXT_C_STRIDE]; /* 71 x 20 */
    char (*pD)[PUFO_TXT_D_STRIDE]; /* 88 x 20 */
    char (*pA)[PUFO_TXT_A_STRIDE]; /*  9 x 20 */
    char (*pE)[PUFO_TXT_E_STRIDE]; /*  6 x 20 */
    char (*pF)[PUFO_TXT_F_STRIDE]; /* 10 x 30 */
    char  *pG;                     /* 20 */
    char  *pH;                     /* 20 */
    bool   bLoaded;
} PufoTextTables;

extern PufoTextTables g_textTables;

/* Copies the eight string tables out of the loaded texts.dat blob, exactly as
 * Assets_LoadStartup @ 0x382d0 does. Each table is a slice of whole 205-byte
 * records starting at a fixed source offset, so this only succeeds when the blob
 * is large enough to hold the last table it needs.
 *
 * Returns false when the blob is too short (the original would read past its
 * buffer instead); the tables are then left empty. */
bool TextTables_Extract(const u8 *pBlob, u32 cbBlob);

void TextTables_Free(void);

/* Named-table accessors used by the entity layer. Each returns "" rather than
 * NULL when the tables are not loaded, so callers can concatenate safely. */
const char *TextTables_FirstName(u8 nIndex);  /* aszTextD, 0..87 */
const char *TextTables_LastName(u8 nIndex);   /* aszTextC, 0..70 */
const char *TextTables_XcomRank(u8 nRank);    /* aszTextE, 0..5  */
const char *TextTables_AlienRank(u8 nRank);   /* aszTextA, 0..8  */

/* The game's loaded startup state, mirroring the original's globals. */
typedef struct BootAssets {
    /* objects.dat */
    ObjectRecFull *pObjectBlob;   /* unk_157190 @ 0x157190 */
    u16            nObjectCount;  /* pCtxBase.nObjectCount, = size / 0xD0 */
    ObjectRecFull **apObjects;    /* pCtxBase.apObjects, parallel pointer array */
    u32            nObjectCap;

    /* texts.dat, expanded at STRIDE */
    u8  *pTextRecs;               /* aTextRecs @ 0xb38d0 */
    u32  nTextRecCount;

    /* The rendering surface, 240x320 16bpp (unk_156A7C @ 0x156a7c). */
    SoftSurf *pSurface;

    /* The three startup pads (apPadCs @ 0x186c30 and friends). */
    PadImage aPadCs[3];
} BootAssets;

/* Assets_LoadStartup @ 0x382d0, in the original's order.
 *
 * ppszArchivePaths[0] = lang.dat, [1] = main.dat. Returns true when the archives
 * opened and the mandatory members were found. A missing optional member is not
 * fatal in the original either.
 *
 * pfnRegisterSurface is called with the created surface so the caller can bind it
 * as the render target; the original writes it to the unk_156A7C global. Pass
 * NULL to skip (the test path). */
bool Boot_LoadStartup(BootAssets *pAssets, const char *const *ppszArchivePaths);

/* GameAssets_Shutdown @ 0x38e4c: frees the blobs, closes the archives, releases
 * the pads and the surface. Safe to call twice. */
void Boot_Shutdown(BootAssets *pAssets);

#endif /* PUFO_BOOT_H */
