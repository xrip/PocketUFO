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
#include "game/pufo_statusmsg.h" /* StatusStack, StatusDrawHooks */
#include "game/pufo_tacthud.h"   /* TactHudPanelEnv, HudPadBlitFn, UiControlBlitFn */
#include "render/pufo_render.h" /* PadImage, for the selection frame */
#include "pufo_common.h"

typedef struct IsoDrawCtx {
    MapCell *pCell;       /* 0x00 */
    i32      nDrawX;      /* 0x04 */
    i32      nDrawY;      /* 0x08 */
    i32      nDrawMode;   /* 0x0C, MapView_DrawUnit: the aUnitFacingDrawOff x term */
    i32      nDrawAux;    /* 0x10, MapView_DrawUnit: the running y term */
    i32      nSpriteX;    /* 0x14, MapView_DrawUnit */
    i32      nSpriteY;    /* 0x18, MapView_DrawUnit */
    i32      nIsoOffA;    /* 0x1C */
    i32      nIsoOffB;    /* 0x20 */
    u8       bOob;        /* 0x24 */
    i32      nTileX;      /* 0x28 */
    i32      nTileY;      /* 0x2C */
    i32      nHeightOff;  /* 0x30, written by MapView_DrawUnitHeightGlyph */
    i32      nHeightAux;  /* 0x34, the same */
    u8       nLayer;      /* 0x38 */
    i32      nGlyphA;     /* 0x3C */
    i32      nGlyphB;     /* 0x40 */

    /* --- the MapView_DrawUnit @ 0x1c958 walk state ---
     * The original indexes abUnitFlag by bAnimStep, so those four bytes have to stay
     * adjacent. abUnitFlag[3] is the step count, not a flag. */
    u8       bAnimStep;      /* 0x44, the walk-step index 0..4 */
    i32      nAnimFrame;     /* 0x48 */
    i32      nAnimParam;     /* 0x4C, read as a u16 by every glyph index that uses it */
    u8       bAnimKind;      /* 0x50, the aUnitWalkKind entry for the current step */

    i32      nObjParam;   /* 0x54 */
    Soldier *pUnit;       /* 0x58 */
    u8       bWallPalIdx; /* 0x64 */
    u8       abUnitFlag[4]; /* 0x65, 0x66, 0x67 are direction codes 4..7 indexed by
                             * bAnimStep; 0x68 is the number of steps to walk */
    u8       bUnitGlyphRow;  /* 0x69, the aUnitWalkGlyph row: 2 when nAnimPose is 18,
                              * otherwise (bFlags & 2) ? 1 : 0 */
    char     szText[80];     /* 0xAC, the scratch string MapView_DrawIsoLayer fills with itoa output
                              * and with a copy of the active pad's label. The label copy runs to its
                              * NUL with no bound test (0x22254), so the buffer is only as safe as the
                              * text record it is copied from. It stops at 0xF4 because the minimap
                              * keeps a dword scratch at 0xFC. */
    i32      nScratch;       /* 0xFC, the glyph-index scratch MapView_DrawMinimap stores per sprite
                              * and per marker, and reads back as a u16 when it draws */
    i32      nScratch2;      /* 0x100 */
    i32      nRosterIndex;   /* 0x114, IDA nRosterAux2: the multi-target roster cursor
                              * MapView_DrawUnitFacingArrow walks over 0..9 */
    i32      nCmdTickCol;    /* 0x124, the command pad ruler column counter Tact_DrawCommandPads
                              * runs 0..23 (the ARM masks it to a byte before scaling) */
    i32      nCmdTickRow;    /* 0x128, its row counter 0..11 */
    u8       bTwoHandWeapon; /* 0x12C, set from the two hands and cleared when the unit is
                              * armed with a weapon whose ObjectRec+0xA4 is zero */
    i32      nPal;        /* 0x6c */
    i32      nHpGlyph;    /* 0x70 */
    double   dHeightA;    /* 0x78, the three operands after their modulo, kept as doubles */
    double   dHeightB;    /* 0x80 */
    double   dHeightC;    /* 0x88 */
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
 * Font_LoadTactSprites (0x257c4, ported at pufo_font.c:356) is the only writer, and the ARM shows the rule
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
    u16 nScangBase;   /* fontSys + 0x2E, the minimap glyph group MapView_DrawMinimap draws from */
    u16 nSmokeBase;   /* fontSys + 0x30 */
    u16 nExplBase;    /* fontSys + 0x32 */
    u16 nGlyphBase;   /* fontSys + 0x28, the same base Text_DrawString adds a character to */
} IsoSpriteBases;

/* The u16 group bases at fontSys + 0x00, indexed by Tact_MapTypeToGlyphIdx. The ARM reads them as
 * `LDRH [fontSys + 2*idx]`; Hex-Rays renders the same thing as `gap_0[2*idx]`.
 *
 * 28 entries, not 18, because the index is not clamped anywhere. ARM at 0x1CE70 is `AND R3,R0,#0xFF`
 * then `ADD R3,R5,R3,LSL#1` then `LDRH R3,[R3]` with no bound test, and Tact_MapTypeToGlyphIdx
 * returns 18 for unit types 5..8 and `nType + 5` for anything the probe misses -- so a type 22 unit
 * reaches fontSys + 0x36. The documented bases run to +0x32 (expl); +0x34 and +0x36 are the two
 * words past them. */
#define PUFO_ISO_GROUP_BASE_COUNT 28

/* What MapView_DrawUnit @ 0x1c958 reaches outside the context. */
typedef struct IsoUnitDrawGate {
    MapCell *const   *apCells;      /* Map.apCells @ 0x15338, 3600 slots per layer */
    Soldier          *pSelUnit;     /* Map.pSelUnit @ 0x15134 */
    u8                bQueueActive; /* TactState.bQueueActive, IDA unk_157198 */
    u8                bSelectFlag;  /* unk_128AD6 @ Map+0x11E */
    u8                nAnimPhase;   /* nAnimPhase @ Map+0xF8 (IDB 0x113AB0) */
    u8                nMapPhase;    /* unk_1139EF @ Map+0x37 */
    const u16        *paGroupBase;  /* fontSys + 0x00, PUFO_ISO_GROUP_BASE_COUNT entries */
    u16               nCursorBase;  /* fontSys + 0x02 */
    u16               nHandobBase;  /* nHandobSprBase, fontSys + 0x20 */
    TactItem        **apSelWeapon;  /* pSelWeapon @ 0x113A00: the selected weapon ITEM. Both reads go
                                     * through its pObj -- 0x1CB60 is *(u8*)(pSelWeapon->pObj + 0xA4)
                                     * (bListPri) and 0x1D308 is pSelWeapon->pObj->aItemQty[51]. The cell
                                     * is a TactItem*, not an object pointer (Tact_SetFireModeAimed
                                     * @ 0x17790 reads pSelWeapon's +6 ammo byte, then pObj's +0xAE cap). */
    PadImage         *pSelectFrame; /* unk_156928, the selection frame pad */
    const u8         *paTypeProbe;  /* Map_abSpriteSet @ Map+0x15116..19, the four bytes
                                     * Tact_MapTypeToGlyphIdx scans (0x466A0: first match wins, then
                                     * the nType + 5 fallback) */
} IsoUnitDrawGate;

/* MapView_DrawUnit @ 0x1c958 (3,220 bytes). See the header note in pufo_isodraw.c for the decode.
 *
 * The original's return value is whatever R0 happens to hold at the exit -- it is register debris,
 * not a status, and no caller in the original tests it. The port returns 0. */
int MapView_DrawUnit(IsoDrawCtx *pIso, const IsoUnitDrawGate *pGate);

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

/* Math_StoreSqrtConst @ 0x37098. */void Math_StoreSqrtConst(void);

/* Math_StoreAtan2Const @ 0x370e8. */
void Math_StoreAtan2Const(void);

/* What MapView_DrawUnitHeightGlyph reads out of the Map block.
 *
 * The original reaches all of it through the Map base; the port takes it as an argument so the
 * glyph is testable without a Map. */
typedef struct IsoHeightGate {
    u8  bFireStatus2;      /* Map+0x52 */
    u8  bFireStatus9;      /* Map+0x59 */
    i32 nFireTgtX;         /* Map+0x14040 */
    i32 nFireTgtY;         /* Map+0x14044 */
    i32 nFireTgtZ;         /* Map+0x14048 */
    u32 nHeightGlyphWord;  /* Map+0x14000: the gate tests the whole dword, the index uses the low half */
    i32 nHeightSrcX;       /* Map+0x14060 */
    i32 nHeightSrcY;       /* Map+0x14064 */
    i32 nHeightSrcZ;       /* Map+0x14068 */
    i32 nFireTickProgress; /* Map+0x1405C, used as its low half */
} IsoHeightGate;

/* MapView_DrawUnitHeightGlyph @ 0x1c240.
 *
 * Draws the firing-arc height glyph, but only on the cell that IS the current fire target. Six
 * conditions must all hold, and note that X and Y are compared swapped against the iso fields:
 *
 *   bFireStatus2 and bFireStatus9 both non-zero
 *   nMapX == nFireTgtY, nMapY == nFireTgtX, nLayer == nFireTgtZ
 *   the dword at nHeightGlyphWord is non-zero
 *
 * With A = nHeightSrcX % 16, B = nHeightSrcY % 16, C = nHeightSrcZ % 12, the original computes:
 *
 *   nHeightOff = ((A*sin*scale) + 2.0) - (B*sin*scale) - 2.0
 *   nHeightAux = ((B*cos) + (A*cos)) * scale + 40.0 - 2.0 - (C*2.0) - 20.0 - 8.0
 *
 * The port reproduces that operation order rather than the simplified form, because the two
 * constants make the products land on integers: sin*scale is exactly 1.0 and cos*scale exactly 0.5,
 * so a different association can move a value across a truncation boundary. Simplified, the same
 * arithmetic is nHeightOff = A - B and nHeightAux = (A + B)/2 - 2*C + 10 - that is the isometric
 * rotation of a position pair, which is why the three operands are X, Y and Z.
 *
 * The glyph is 10 * LOWORD(nHeightGlyphWord) + LOWORD(nFireTickProgress) + nSmokeBase + 16, palette
 * 1, at (nHeightOff + nDrawX, nDrawY + nHeightAux + 15).
 *
 * Two asymmetries are reproduced rather than tidied. The gate tests the whole dword at 0x14000 while
 * the index reads it as a u16, so a non-zero high half passes the gate and contributes nothing. And
 * the original's return value on a skipped path is whatever gate byte it last read - register
 * residue - so the port returns 1 when it drew and 0 when it did not. */
int MapView_DrawUnitHeightGlyph(IsoDrawCtx *pIso, const IsoHeightGate *pGate,
                                const IsoSpriteBases *pBases);

/* What MapView_DrawUnitFacingArrow reads out of the Map and TactState blocks.
 *
 * The original reaches all of it through the two base registers (Map at 0x1139B8, and the
 * TactState pointer at 0x157198); the port takes it as an argument so the arrow is testable
 * without either block. */
typedef struct IsoArrowGate {
    u8          bFireAnimActive; /* TactState+0x111E1: the fire animation is running. Set by move
                                  * command 0x12 (which also zeroes nPhaseCounter) and cleared by
                                  * command 0x10. IDA leaves it inside gap_70112. */
    i32         nPhaseCounter;   /* TactState+0x1118C */
    u8          bFireStatus1;    /* Map+0x51 */
    const double *adMtX;         /* Map+0x13EE8, 10 entries: the multi-target trajectory roster */
    const double *adMtY;         /* Map+0x13F38, 10 entries */
    const double *adMtZ;         /* Map+0x13F88, 10 entries */
    const i32   *anAimRow;       /* Map+0x13FD8, 11 entries: the copied aim row */
    i32          nRowId;         /* Map+0x14088: selects the glyph-offset group, 10 * nRowId + n */
    u8           bTrajHitStop;   /* Map+0x1406D */
    TactItem    *pThrownItem;    /* Map+0x14070, written by Tact_SimulateThrowArc @ 0x319f4 */
} IsoArrowGate;

/* MapView_DrawUnitFacingArrow @ 0x1b7bc.
 *
 * Draws the glyph that marks a shot at the cell it passes through. Three mutually exclusive
 * branches, all behind the one gate `pCell->bFlags29 & 0x20` (the shot-trace flag):
 *
 *   1. bFireAnimActive: the muzzle smoke, nSmokeBase + (nPhaseCounter % 4) + 56, at the unit's own
 *      facing offset (nDrawX + nDrawOffX, nDrawY + nObjParam + nDrawOffY). Needs pUnit.
 *   2. bFireStatus1: walk the ten-entry roster in pIso->nRosterIndex. An entry is drawn when its
 *      floored voxel coordinates are this cell: floor(X/16) == nX, floor(Y/16) == nY, (int)Z/12 == nZ,
 *      and the aim-row dword is > 0. The glyph is LOWORD(anAimRow[n]) + nBulletsBase - 1.
 *   3. bTrajHitStop: the thrown item's own floor-object glyph, pThrownItem->pObj->aItemQty[50]
 *      + nFloorobBase - 1, at the end of entry 0 of the roster.
 *
 * X and Y are floored (ARM MOVS / ADDMI #0xF / ASR #4) while Z uses C-style truncating division,
 * exactly as in the arc code. The three height operands are the remainders, stored as doubles at
 * 0x78/0x80/0x88, and the projection is the same iso rotation MapView_DrawUnitHeightGlyph uses -
 * but with its own constants: the roster branch ends at +40.0 - 16.0 - 2.0*C, the throw branch
 * keeps the height glyph's - 20.0 - 8.0 tail as well.
 *
 * The original's return value is register residue and the caller (MapView_DrawIsoLayer @ 0x21fc8)
 * drops it, so the port returns the number of glyphs drawn, like its siblings return 1 or 0. */
int MapView_DrawUnitFacingArrow(IsoDrawCtx *pIso, const IsoArrowGate *pGate,
                                const IsoSpriteBases *pBases);

/* ------------------------------------------------------------ the tactical map frame
 *
 * Everything MapView_DrawIsoLayer @ 0x21e2c reaches outside the draw context. It is the only
 * function that drives the per-cell chain, so it is also the first place all the per-cell gates have
 * to be assembled together; Render_Frame (0x22310) will own this env when it is ported.
 *
 * The offsets in the comments are Map-context offsets unless the field names a global. */
typedef struct IsoLayerEnv {
    /* --- the iso camera and the loop bounds --- */
    i32  nMapDimX;        /* Map.nMapDimX @ 0x1511B, read by Map_IsoOffsetA */
    i32  nMapDimY;        /* Map.nMapDimY @ 0x1511C, read by Map_IsoOffsetB */
    i32  nCamScrollX;     /* Map.nCamScrollX @ 0x15130 (IDA unk_128AE8) */
    i32  nCamScrollY;     /* Map.nCamScrollY @ 0x15134 (IDA unk_128AEC) */
    i8   nLayerCount;     /* Map.nLayerScratch @ 0x15138, signed byte, read with LDRSB */
    u8   nTileCountX;     /* Map+0x34 (IDA unk_1139EC), written by Input_OnLeft/OnRight */
    u8   nTileCountY;     /* Map+0x35 (IDA unk_1139ED), written by Input_OnUp/OnDown */
    u8   nMapPhase;       /* Map+0x37 (IDA unk_1139EF), the phase the bars and the pad tick on */

    /* --- the per-cell chain, each with the gate that callee already takes --- */
    const IsoGrid          *pGrid;
    const IsoAnimGlobals   *pAnim;
    const IsoSelectGate    *pSelect;
    const IsoUnitDrawGate  *pUnit;
    const IsoSpriteBases   *pBases;
    const IsoHeightGate    *pHeight;
    const IsoArrowGate     *pArrow;

    /* --- the frame furniture --- */
    const IsoViewportGuide *pGuide;
    UiControl              *pBank;      /* pBank @ 0xAC6D0, the control bank the panel re-bases */
    TactHudPanelEnv        *pPanel;
    const GfxColorEnv      *pColor;
    HudPadBlitFn            pfnDecimalBlit;
    void                   *pDecimalUser;
    UiControlBlitFn         pfnCtlBlit;
    UiControlBlitFn         pfnCtlBlitEx;
    void                   *pCtlUser;
    StatusStack            *pStatus;
    const StatusDrawHooks  *pStatusHooks;

    /* --- the multi-target bars drawn under the selected unit --- */
    Soldier *pSelUnit;          /* Map.pSelUnit @ 0x15334 */
    u8       bDeployPanelOpen;  /* Map+0x15110 */
    i32      nHudPadHeight;     /* Map.pHudPad (Map+0xFC) -> uHeight at +0xA */

    /* --- the active control pad and its label --- */
    UiControl  *pLookCtl;       /* dword_1571E4, the pad Input_OnPointer last touched */
    const char *pPadLabel;      /* aTextRecs @ 0xB38D0 + 0x35A7 */

    u16 nUiScreen;              /* Map_nUiScreen @ 0x3A */
} IsoLayerEnv;

/* MapView_DrawIsoLayer @ 0x21e2c — the tactical map frame, and the only caller of the per-cell chain.
 *
 * Two passes over every layer, not one: pass 1 draws only the floor sprite, pass 2 draws everything
 * else. That is why the floor is under the units rather than interleaved with them. Both passes walk
 * nTileCountY outer and nTileCountX inner, and both fall back to the same thing on a cell that does
 * not resolve: on layer 0 only, the blank group glyph at fontSys+0x00 with palette 0.
 *
 * Pass 2's chain, in the original's order: select marker, wall palette A, wall palette B, occupant
 * glyph, hp glyphs, the unit, the facing arrow, the height glyph, the animation glyphs, the high
 * select marker, the facing glyph.
 *
 * Then the frame: the viewport guide, the selected-unit panel, up to five multi-target bars for the
 * selected unit (207, 300 - 18*i, shortened by 17 when the deploy panel is open and by the hud pad's
 * height otherwise), the status stack, the active pad and its centred label, and finally a full
 * 240 by 320 blit of index 55 at key 128 unless the screen id is 0 or 13.
 *
 * The original returns register debris and no caller tests it; the port returns void. */
void MapView_DrawIsoLayer(IsoDrawCtx *pIso, const IsoLayerEnv *pEnv);

/* ------------------------------------------------------------ the minimap
 *
 * Everything MapView_DrawMinimap @ 0x1ed4c reaches outside the draw context.
 *
 * The two span cells are the tile-size pair. unk_1139E8 / unk_1139EA (Map+0x30 / Map+0x32) are the
 * same two bytes Map_InitFacingScratch types as nMapTileW / nMapTileH; they are read here as the loop
 * bounds (value >> 2) and by MapView_EnterLookMode as the camera-centre input (value >> 1). The census
 * finds no xref writer, and the reason is the store's shape: Map_InitFacingScratch @ 0x26F38 / 0x26F40
 * writes STRH 240 / STRH 320 through the Map pointer, which leaves no xref in this IDB. Tact_Init-
 * Mission's call at 0x3CB30 therefore leaves 60 and 80 as the bounds and BOTH cell passes run, over
 * the whole field, Map_IsValidCell doing the clipping. The port hands the live pair. */
#define PUFO_PBANK_MINIMAP_FIRST 98 /* pBank + 0xC40: the three screen-2 (look mode) controls */
#define PUFO_PBANK_MINIMAP_COUNT  3
#define PUFO_TEXT_REC_MINIMAP_LABEL 17 /* aTextRec17 @ 0xB466D, the only reference being the copy at 0x1F4DC */

typedef struct IsoMinimapEnv {
    i32 *pnScrollX;      /* Map_nMinimapScrollX @ 0x118864 (Map+0x4EAC): clamped here, panned by
                          * Input_OnLeft / Input_OnRight while Map_nUiScreen is 2 */
    i32 *pnScrollY;      /* Map_nMinimapScrollY @ 0x118868 (Map+0x4EB0) */
    u16  nSpanX;         /* unk_1139E8 @ Map+0x30: the x loop bound is this >> 2. No writer exists. */
    u16  nSpanY;         /* unk_1139EA @ Map+0x32: the y loop bound is this >> 2. No writer exists. */
    i8   nLayerCount;    /* Map.nLayerScratch @ 0x15138, signed byte, read with LDRSB */
    u8   nMapPhase;      /* Map+0x37 */
    const IsoGrid        *pGrid;   /* apCells is the cell grid; nMapDimX/Y feed the scroll clamp */
    const IsoSpriteBases *pBases;  /* nScangBase is the glyph group */
    const char  *pLabel;           /* aTextRecs @ 0xB38D0 + 205*17 (0xB466D): text record 17, loaded
                                    * from lang.dat and never rewritten - the copy at 0x1f4dc is the
                                    * only reference to it in the whole binary */
    const PadImage *pMiniCursor;   /* unk_1571F0: the 'minimcur' pad stored by Tact_LoadUiPads
                                    * @ 0x3b068 and cleared by Tact_Shutdown @ 0x3d568 */
    UiControl         *pBank;      /* pBank @ 0xAC6D0; widgets 98..100 are blitted */
    const GfxColorEnv *pColor;
    UiControlBlitFn   pfnCtlBlit;
    UiControlBlitFn   pfnCtlBlitEx;
    void             *pCtlUser;
} IsoMinimapEnv;

/* MapView_DrawMinimap @ 0x1ed4c — the look-mode overview: every map cell as a 4 by 4 block.
 *
 * It clears the whole 240 by 320 surface to index 0, clamps the scroll to
 *   -30 <= scrollX <= 40 * (nMapDimX - 3) / 4   and   -40 <= scrollY <= 40 * (nMapDimY - 4) / 4
 * (the ARM's MOVS / ADDMI #3 / ASR #2 is truncating division, like C's), then runs two passes that
 * paint each valid cell at 4 * (tile - scroll):
 *
 *   Pass 1 (layer 0 only, and it runs even when nLayerCount is 0): every valid cell gets one ground
 *     glyph, nScangBase + (sprite2->uUnk20 + 35) with the cell's bUnk56 as palette. The sprite is
 *     Font_GetSprByIndex(fontSys, 2) - a fixed record, so the pass paints one uniform glyph. The
 *     original dereferences the lookup result with no null test (0x1eea4).
 *   Pass 2 (layers 0..nLayerCount-1, validity still tested at layer 0): the cell's four sprite
 *     records in order (floor, wall A, wall B, ceiling), each drawn as nScangBase + (uUnk20 + 35)
 *     and skipped when uUnk20 is 0; then the item marker nScangBase + (nMapPhase / 3 + 9) when the
 *     cell has an item list AND bFlags29 bit 4; then the occupant marker - a large unit picks its
 *     glyph by how its own cell sits against the drawn one (same cell 4*(phase/3+3), same column
 *     +14, same row +13, neither +15), a small unit uses phase/3, and either adds 12 or 3 when
 *     bStatus is not OPERATIVE. The sprite palette is the cell's bUnk56 when bFlags29 bit 4 is set,
 *     else 255; the markers always use 0.
 *
 * Then the furniture: the 'minimcur' pad at key 0x3F, the label (text record 17 copied to its NUL
 * with no bound - ORIGINAL BUG (0x1f4dc) - with the decimal layer count appended at szText[50]), the
 * white/grey colour pair, the label drawn at (0,0), and the three screen-2 controls.
 *
 * The original returns the control-loop counter; no caller tests it, so the port returns void. */
void MapView_DrawMinimap(IsoDrawCtx *pIso, const IsoMinimapEnv *pEnv);

/* ------------------------------------------------------------ the command pad panel (screen 4)
 *
 * Everything Tact_DrawCommandPads @ 0x1fc8c reaches outside the draw context. It is
 * Render_Frame's screen-4 branch: the selected unit's status panel. */
#define PUFO_PBANK_CMD_FIRST 101 /* pBank + 0xCA0 (0xAD370): the panel's bclose / bprevs / bnexts */
#define PUFO_PBANK_CMD_COUNT  3

/* The row labels are aTextRecs records 56..66 (0xB65A8 .. 0xB6DAA at stride 205). They are read
 * straight from the loaded blob and never rewritten. */
#define PUFO_CMDPAD_REC_FIRST 56

typedef struct TactCmdPadEnv {
    Soldier *pSelUnit;           /* Map_pSelUnit @ 0x128CEC: the original dereferences it with no null
                                  * test at 0x1FCC4, and its openers do not gate on a selection either --
                                  * Tact_OpenCommandScreen @ 0x16ffc and Tact_OpenCommandScreenFlagged @
                                  * 0x198c4 both write screen 4 straight out. The port hands the live cell,
                                  * NULL included: the same fault, in the same place. */
    const PadImage *pSoldierPad; /* unk_128AC0 (Map+0x15108): the 'soldier' pad stored by
                                  * Tact_LoadUiPads, blitted as the panel backdrop */
    PadImage *apCsPad[3];        /* 0x186C30 / 0x186C34 / 0x186C38: the 'cs0' / 'cs1' / 'cs2' pads,
                                  * loaded by Assets_LoadStartup @ 0x387f4..0x38894 (mode 0x12,
                                  * origin zeroed) and freed by GameAssets_Shutdown. The panel
                                  * rewrites their origins to (5,2) and (39,2) before each blit,
                                  * so they are not const */
    const u8 *pTextRecs;         /* aTextRecs @ 0xB38D0: records 56..66 are the row labels */
    const char *panRankLow;      /* aszTextE @ 0xB2200, for Unit_FormatDisplayName */
    const char *panRankOther;    /* aszTextA @ 0xB2148 */
    u8  bLookCommitFlag;         /* bLookCommitFlag @ 0x128A44: when zero, widgets 102 and 103 are
                                  * blitted too */
    UiControl         *pBank;    /* pBank @ 0xAC6D0; widgets 101..103 */
    const GfxColorEnv *pColor;
    UiControlBlitFn   pfnCtlBlit;
    UiControlBlitFn   pfnCtlBlitEx;
    void             *pCtlUser;
} TactCmdPadEnv;

/* Tact_DrawCommandPads @ 0x1fc8c — the selected unit's status panel.
 *
 * Backdrop first: the 'soldier' pad. Then the two 32x32 command pad slots at (5,2) and (39,2),
 * unless the unit type is in the craft family (4..8, 14..20, 22 — the gate at 0x1FCEC):
 *   uMissions <= 3  -> empty slots: a grey 32x32 frame with a black 30x30 inside at each position.
 *   otherwise       -> nClass 0..5 picks two of the three cs pads: 0=(cs0,cs0) 1=(cs1,cs1)
 *                      2=(cs2,cs2) 3=(cs0,cs1) 4=(cs1,cs2) 5=(cs0,cs2); any other class draws
 *                      nothing. The pads' own origins are rewritten before every blit.
 *
 * Then the text: Unit_FormatDisplayName mode 1 at (75,2); mode 2 at (75,20) for the normal
 * families, or centred as 120 - width/2 at y 24 for the craft family (the original calls mode 2
 * twice there, 0x1FFB4 / 0x1FFC4 — the second call only feeds Text_MeasureWidth and returns the
 * same buffer, so the port calls once). Then eleven label/value rows, each a colour pair, the
 * label record at (10,y) and the value built in szText at (170,y), most with a bar under them:
 *   45  TU as uTu/auStat[0]        69  stun as uStunOrArmor/auStat[9]   93  HP as uHp/auStat[1]
 *   117 bKneelOrPrimed alone, plus one 12x6 block per point at y 133
 *   141 auStat[2]                  165 morale as bMorale "/100", bar fixed 200 wide
 *   189 auStat[4]                  213 auStat[5]      237 auStat[8]
 *   261/285 auStat[7] / auStat[6], only when bPsiActive
 * The "remaining" bars are (max>>1) - (cur>>1) - 2 wide at (cur>>1)+11 — negative when the unit is
 * at full value, which the bounds check simply drops.
 *
 * Then the ruler - ORIGINAL BUG (0x20ad4): 12 rows x 24 columns of 5x1 ticks at y = 24*row + 45,
 * x = 5*(col+1) with index 5*(col+20) and x = 5*(col+23) with index 250 - 5*col, all with
 * idx == mid == low. The rows land exactly on the label rows the function has just drawn, so the
 * ruler cuts a 1-pixel colour ramp through every label and value. Reproduced as written.
 *
 * Then the panel frame (top 230x1 at (5,2), left and right 1x308 at x 5 and 235, index 0x96) and
 * the controls: bank widget 101 always, 102 and 103 only while bLookCommitFlag is zero.
 *
 * The original returns the control-loop counter; Render_Frame ignores it, so the port returns void. */
void Tact_DrawCommandPads(IsoDrawCtx *pIso, const TactCmdPadEnv *pEnv);

/* ------------------------------------------------------------ iso unit animation tables
 *
 * The nine static tables MapView_DrawUnit (0x1c958) reads. They land here before the function
 * itself because their extents are settled facts about the shipped image, and each one is checked
 * byte for byte by docs/parity/audit_tables.py. The definitions are in pufo_isodraw.c, dumped by
 * tools/gen_isodraw_tables.py.
 *
 * PUFO_ISO_UNIT_TYPE_COUNT is 23, not the 24 rows of g_aUnitTypeFlag0: these tables cover unit
 * types 0 through 22, and two of them cover fewer than that. g_aUnitWalkGlyph has 22 rows because
 * type 22 has no walk data, and g_aUnitFacingDrawOff has 14 rows because the 224-byte table ends at
 * 0xA9488, where the u16 table Map_TraceVoxelOccupancy (0x3242c) indexes as
 * 4 * (8 * nUnitType + bFacing) begins. */
#define PUFO_ISO_UNIT_TYPE_COUNT       23
#define PUFO_ISO_WALK_KIND_COUNT       (PUFO_ISO_UNIT_TYPE_COUNT * 80)
#define PUFO_ISO_ANIM_FRAME_COUNT      78
#define PUFO_ISO_WALK_GLYPH_TYPE_COUNT 22
#define PUFO_ISO_POSE_FRAME_COUNT      152
#define PUFO_ISO_FACING_OFF_TYPE_COUNT 14

extern const u8  g_aUnitWalkKind[PUFO_ISO_WALK_KIND_COUNT];
extern const u16 g_aUnitAnimFrame[PUFO_ISO_ANIM_FRAME_COUNT][3];
extern const u8  g_aUnitWalkGlyph[PUFO_ISO_WALK_GLYPH_TYPE_COUNT][3][9];
extern const u8  g_aPoseFrameTable[PUFO_ISO_POSE_FRAME_COUNT][5];
extern const u8  g_aUnitTypePoseBase[PUFO_ISO_UNIT_TYPE_COUNT];
extern const i8  g_aUnitHeightOff[PUFO_ISO_UNIT_TYPE_COUNT][8];
extern const i8  g_aBobPhaseY[8];
extern const i8  g_aUnitWeaponYOff[PUFO_ISO_UNIT_TYPE_COUNT];
extern const i8  g_aUnitFacingDrawOff[PUFO_ISO_FACING_OFF_TYPE_COUNT][8][2];

#endif /* PUFO_ISODRAW_H */
