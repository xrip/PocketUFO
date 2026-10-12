/* pufo_isodraw.h - the per-cell isometric glyph draw helpers.
 *
 * Evidence: MapView_DrawCellOccupantGlyph @ 0x1b45c,
 *           MapView_DrawCellFloorGlyph @ 0x1c904,
 *           MapView_SetWallPalFromSlotA @ 0x1c7dc,
 *           MapView_SetWallPalFromSlotB @ 0x1c870,
 *           TactUnit_DrawAnimGlyphs @ 0x1c6ac,
 *           TactUnit_DrawFacingGlyph @ 0x1c754.
 *
 * The original threads a 316-byte IsoDrawCtx through the isometric renderer. Only
 * the fields these helpers touch are modelled:
 *
 *   pCell        0x00
 *   nDrawX       0x04
 *   nDrawY       0x08
 *   nIsoOffA     0x1C
 *   nIsoOffB     0x20
 *   bOob         0x24
 *   nTileX       0x28
 *   nTileY       0x2C
 *   nLayer       0x38
 *   nGlyphA      0x3C
 *   nGlyphB      0x40
 *   nObjParam    0x54
 *   pUnit        0x58
 *   bWallPalIdx  0x64
 *   nPal         0x6c
 *   nMapY        0x138
 *   nMapX        0x13C
 *
 * Those offsets are the ARM displacements in MapView_bResolveIsoCell, not the pseudocode: Hex-Rays
 * reaches several of them through pIso[1].pCell and other invented aliases, which is how it renders a
 * dword at 0x13C as if it were a struct pointer field.
 *
 * The animation phase and its scratch byte are `pCtxBase.nAnimPhase` (0x37) and
 * `nAnimPhaseScratch[0]`; they are passed in as IsoAnimGlobals so the helpers stay
 * testable.
 *
 * Sprite byte offsets that matter: `gap_25` (0x25) is the draw height, and
 * `bBlocksMove` (0x2a) selects the wall palette path.
 */
#ifndef PUFO_ISODRAW_H
#define PUFO_ISODRAW_H

#include "game/pufo_mapcell.h"
#include "pufo_common.h"

typedef struct IsoDrawCtx {
    MapCell *pCell;       /* 0x00 */
    i32      nDrawX;      /* 0x04 */
    i32      nDrawY;      /* 0x08 */
    i32      nIsoOffA;    /* 0x1C */
    i32      nIsoOffB;    /* 0x20 */
    u8       bOob;        /* 0x24 */
    i32      nTileX;      /* 0x28 */
    i32      nTileY;      /* 0x2C */
    u8       nLayer;      /* 0x38 */
    i32      nGlyphA;     /* 0x3C */
    i32      nGlyphB;     /* 0x40 */
    i32      nObjParam;   /* 0x54 */
    Soldier *pUnit;       /* 0x58 */
    u8       bWallPalIdx; /* 0x64 */
    i32      nPal;        /* 0x6c */
    i32      nHpGlyph;    /* 0x70 */
    i32      nMapY;       /* 0x138 */
    i32      nMapX;       /* 0x13C */
    TactItem *pItemCursor;/* 0x134, the item-list walk cursor */
} IsoDrawCtx;

typedef struct IsoAnimGlobals {
    u8 nAnimPhase;         /* pCtxBase.nAnimPhase @ 0x37 */
    u8 nAnimPhaseScratch0; /* nAnimPhaseScratch[0] */
    u8 bPhaseActive;       /* TactState.bPhaseActive, IDA unk_157198->bUnk70115 */
} IsoAnimGlobals;

/* One u16 base per sprite group, kept inside the fontSys block at 0x158268.
 *
 * Font_LoadTactSprites (0x257c4, not ported) is the only writer, and the ARM shows the rule
 * exactly: LDR name; LDRH total; STRH base; BL Font_LoadSpr. Each base is the running frame count
 * captured immediately BEFORE that group is loaded, so it is the group's first index.
 *
 * The full table, from the STRH displacements (Hex-Rays renders these as gap_8[n] / gap_32[n] and
 * its indices do not match the ARM):
 *
 *   +0x00 blanks   +0x02 cursor   +0x04 mission terrain   +0x06 craft
 *   +0x20 handob   +0x22 floorob  +0x24 tanks             +0x26 bigobs
 *   +0x2A bullets  +0x2E scang    +0x30 smoke             +0x32 expl
 *
 * Only the four these helpers read are modelled. */
typedef struct IsoSpriteBases {
    u16 nCursorBase;  /* fontSys + 0x02, the cursor group. Both select markers draw from it. */
    u16 nFloorobBase; /* fontSys + 0x22 */
    u16 nBulletsBase; /* fontSys + 0x2A */
    u16 nSmokeBase;   /* fontSys + 0x30 */
    u16 nExplBase;    /* fontSys + 0x32 */
    u16 nGlyphBase;   /* fontSys + 0x28, the same base Text_DrawString adds a character to */
} IsoSpriteBases;

/* What the two select markers read out of the map and the tactical queue. */
#define PUFO_ISODRAW_MULTI_TARGET_SLOTS 8 /* Map.apMultiTargets @ 0x15338+0x08, eight entries */

typedef struct IsoSelectGate {
    u8      bQueueActive;                        /* TactState.bQueueActive, IDA unk_157198 */
    u8      nActionLatch;                        /* Map.gap_2[1], the port's UnitRoster.bActionLatch */
    i32     nMapLayers;                          /* Map.nMapLayers */
    i32     nAnimPhase;                          /* Map.nAnimPhase */
    MapCell *apMultiTargets[PUFO_ISODRAW_MULTI_TARGET_SLOTS];
} IsoSelectGate;

/* MapView_DrawCellFloorGlyph @ 0x1c904. Draws the floor sprite at
 * (nDrawX, nDrawY - sprite gap_25) using the animation phase as the glyph index. */
void MapView_DrawCellFloorGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim);

/* MapView_DrawCellOccupantGlyph @ 0x1b45c. Draws the ceiling/occupant sprite at
 * (nDrawX, nDrawY - sprite gap_25). */
void MapView_DrawCellOccupantGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim);

/* MapView_SetWallPalFromSlotA @ 0x1c7dc / SlotB @ 0x1c870. When the wall sprite's
 * bBlocksMove is set the palette index is derived from the cell flags: bit 0
 * (0x1) selects the animation scratch byte, bit 2 (0x4) selects 7, otherwise 0;
 * with bBlocksMove clear the animation phase is used. The chosen index is stored
 * in pIso->bWallPalIdx and used to draw the wall sprite. */
void MapView_SetWallPalFromSlotA(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim);
void MapView_SetWallPalFromSlotB(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim);

/* TactUnit_DrawAnimGlyphs @ 0x1c6ac.
 *
 * The animation kind is the low 3 bits of the cell's 32-bit flags. Kind 0 draws nothing. The glyph
 * index is the kind's offset from the table at unk_AA018, plus half the animation phase, plus the
 * smoke group base.
 *
 * Kind 4 is the one that draws twice: once at the table's own entry for 4, which is 0, and then a
 * second glyph at the entry for 1, which is 8. Both use the same position and palette. */
void TactUnit_DrawAnimGlyphs(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim,
                             const IsoSpriteBases *pBases);

/* TactUnit_DrawFacingGlyph @ 0x1c754.
 *
 * Gated on the phase-active flag first, then on the cell flags: the 0x78 field must be non-zero and
 * must not exceed 0x40. The glyph is drawn 48 left and 12 above the cell's draw position, and the
 * index counts DOWN as the field grows: expl base - ((flags >> 3) & 0xF) + 8. Unlike its siblings
 * this one always uses palette 0, never the cell palette. */
void TactUnit_DrawFacingGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim,
                              const IsoSpriteBases *pBases);

/* What the resolve needs from the map: the flattened cell pointer array and the two dimension bytes.
 * Passed in so the gate is testable without a whole GameMap. */
typedef struct IsoGrid {
    MapCell *const *apCells; /* Map.apCells @ 0x15338, MapCell *[4][60][60] flattened */
    i32 nDimX;               /* Map.nMapDimX @ 0x1511B */
    i32 nDimY;               /* Map.nMapDimY @ 0x1511C */
} IsoGrid;

/* MapView_bResolveIsoCell @ 0x1b0d4. The gate every per-cell draw in the iso layer runs through.
 *
 * Tile coordinates become map coordinates:
 *
 *   nMapY = nIsoOffA + nTileX + nTileY / 2
 *   nMapX = nIsoOffB + nTileY / 2 - nTileX, plus one when nTileY is odd
 *
 * The divide is the compiler's signed divide-by-two (MOVS / ADDMI / ASR), which truncates toward zero
 * like C. The odd test is the remainder of __rt_sdiv being non-zero, so a negative odd nTileY takes
 * the +1 too.
 *
 * Out of bounds is any negative coordinate, nMapY above 10 * nMapDimX - 1, or nMapX above
 * 10 * nMapDimY - 1. The factor of ten is there because the dimensions are stored in 6-tile units.
 *
 * Draw position: nDrawX is 32 * nTileX - 16, or - 32 when nTileY is odd; nDrawY is
 * 8 * (nTileY - 3 * nLayer - 4). The function then returns false when the cell is out of bounds, or
 * when nDrawY + 40 is not positive - so a cell whose top is more than 40 pixels below the screen is
 * skipped, and one that is merely off the bottom edge is not.
 *
 * Only on success does it bind pCell, pUnit, nObjParam and nPal. nObjParam is the ceiling sprite's
 * signed iCeilParam, or 0 with no ceiling sprite. nPal is 255 unless the cell's bFlags29 bit 4 is set,
 * in which case it is 0 when the animation kind is 4 and the cell's bUnk56 otherwise. */
bool MapView_bResolveIsoCell(IsoDrawCtx *pIso, const IsoGrid *pGrid);

/* MapView_bCellIsOccupantTarget, the predicate both select markers repeat verbatim:
 * the cell has an occupant AND (its bFlags44 bit 0 is set OR that unit is operative). */
bool MapView_bCellIsOccupantTarget(const MapCell *pCell);

/* MapView_DrawCellSelectMarker @ 0x1b280.
 *
 * Drawn when the cell is flagged (bFlags57[0] bit 3) OR marked with bFlags29 bit 6, and only while
 * the tactical queue is idle. The glyph pair is chosen by the action-latch mode:
 *
 *   not the top layer and no bFlags29 bit 6 -> A = 2
 *   mode 2 -> A = (phase >> 1) + 7 when the cell is an occupant target, else 6
 *   mode 3 with bFlags29 bit 6 -> A = 7, and B is the cell's slot in apMultiTargets plus the glyph
 *     base plus 49. That B is an ABSOLUTE index: it is drawn without adding the cursor base.
 *   mode 3 without the bit, mode 4, or mode 0/1 -> A = 0 when the cell is not an occupant target,
 *     otherwise A = phase >> 2
 *   any other mode -> A is left at whatever the previous cell stored in the context
 *
 * A is drawn at A + cursor base. B is drawn as itself. */
void MapView_DrawCellSelectMarker(IsoDrawCtx *pIso, const IsoSelectGate *pGate,
                                  const IsoSpriteBases *pBases);

/* MapView_DrawCellSelectMarkerHi @ 0x1b584.
 *
 * The same idea with a narrower gate - only bFlags57[0] bit 3, no bFlags29 bit 6 - and a different
 * table. Not the top layer gives A = 5. On the top layer: mode 0 gives A = (phase >> 2) + 3 or 3 with
 * no second glyph; mode 1 adds B = (phase >> 2) + 15; mode 2 gives A = (phase >> 1) + 7 or 6; mode 3
 * gives B = (phase >> 2) + 13; mode 4 gives B = (phase >> 2) + 11; anything else leaves A stale.
 *
 * Unlike its sibling, this one DOES add the cursor base to B. */
void MapView_DrawCellSelectMarkerHi(IsoDrawCtx *pIso, const IsoSelectGate *pGate,
                                    const IsoSpriteBases *pBases);

/* TactUnit_DrawHpGlyphs @ 0x1b4b0.
 *
 * Two glyphs, and the rules behind them are not obvious:
 *
 * 1. Walk the cell's item list from the head while the item's object has bTwoHanded == 2. If a item
 *    with any other value is reached, draw one glyph at the bullets group base + 7. If the walk runs
 *    off the end of the list, that glyph is not drawn - but rule 2 still runs.
 * 2. Read the HEAD item's object at +0x9E, which is aItemQty[50]. If it is non-zero, draw one glyph at
 *    floorob base + (value - 1). The head is used, not the item the walk stopped at; the ARM confirms
 *    it reloads pCell->pItemList->pObj rather than the loop cursor.
 *
 * Both glyphs are drawn at (nDrawX, nObjParam + nDrawY) with the cell palette, not at nDrawY alone.
 *
 * What is NOT known: why column 50. The ships chunk writes aItemQty indexed by ship slot, and no writer
 * of column 50 was found. The offset is proven from the ARM (LDRH R3, [R3,#0x9E]); its meaning is not.
 *
 * Like the original, this dereferences pObj without a check. */
void TactUnit_DrawHpGlyphs(IsoDrawCtx *pIso, const IsoSpriteBases *pBases);

/* What MapView_DrawViewportGuide reads. The original reaches all of it through the Map block and the
 * cursor global; the port takes them as arguments so the guide is testable. */
typedef struct IsoViewportGuide {
    u8  bGameTickHold;       /* Map+0x5A */
    u8  bActionReady;        /* Map.anActionPhaseSave0[94] @ 0x150D6 */
    u8  bPlayerInputEnabled; /* Map+0x38 */
    u8  bCursorBusy;         /* unk_157180->bBusy */
    u8  bDeployPanelOpen;    /* Map+0x15110 */
    i32 nHudPadHeight;       /* Map.pHudPad (Map+0xFC) -> uHeight */
    u32 nUiSlot5;            /* Map.apUiSlotsA[5]: byte0 left, byte1 right, byte2 top, byte3 bottom */
} IsoViewportGuide;

/* MapView_DrawViewportGuide @ 0x1aadc.
 *
 * The tactical viewport frame: 13 keyed blits of one source tile (172,184) at key 150, forming a
 * rectangle from (15,15) to (225,v2) with tick marks at x 80 and 160 and at the third lines. The
 * bottom edge is a full 240-wide line at y = v2.
 *
 *   v2 = 303 when the deploy panel is open, else 320 - pHudPad->uHeight
 *   v3 = v2 / 3, v4 = v2 - v2 / 3
 *
 * Note the frame is not symmetric in one place: the top horizontal run is 210 wide while the bottom
 * one is 211.
 *
 * Then one indicator, chosen by the four edge flags in priority order bottom, top, left, right:
 *
 *   bottom set -> a vertical bar at x 0 or 225 from v2 - v3 down to v2 - 15, plus a 80-wide cap on
 *     the bottom edge; left wins over right wins over centre
 *   else top set -> the same shape at the top, but the vertical run is v3 - 14 rather than v3 - 15
 *   else left set -> a 15-wide bar on the left edge between the two third lines
 *   else right set -> the same on the right edge
 *
 * The indicator blits use key 128 while the frame uses 150. The original takes the draw context and
 * never reads it. */
void MapView_DrawViewportGuide(const IsoViewportGuide *pGuide);

/* The isometric projection constants. The original computes both once and keeps them in .bss, and
 * every iso glyph placement reads them back out.
 *
 *   g_dblIsoAngle = atan2(32.0, 16.0) - the angle of the 32 by 16 iso tile, so its tangent is 2.
 *   g_dblIsoScale = sqrt(320.0) * 2^-4 = sqrt(320)/16 = sqrt(5)/2.
 *
 * The multiplier in Math_StoreSqrtConst is the double 0x3FB0000000000000, which is 2^-4 exactly. */
extern double g_dblIsoAngle; /* unk_1571F8 */
extern double g_dblIsoScale; /* unk_156AA8 */

/* Math_StoreSqrtConst @ 0x37098. */
void Math_StoreSqrtConst(void);

/* Math_StoreAtan2Const @ 0x370e8. */
void Math_StoreAtan2Const(void);

#endif /* PUFO_ISODRAW_H */
