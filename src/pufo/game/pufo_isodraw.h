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
    i32      nObjParam;   /* 0x54 */
    Soldier *pUnit;       /* 0x58 */
    u8       bWallPalIdx; /* 0x64 */
    i32      nPal;        /* 0x6c */
    i32      nMapY;       /* 0x138 */
    i32      nMapX;       /* 0x13C */
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
    u16 nFloorobBase; /* fontSys + 0x22 */
    u16 nBulletsBase; /* fontSys + 0x2A */
    u16 nSmokeBase;   /* fontSys + 0x30 */
    u16 nExplBase;    /* fontSys + 0x32 */
} IsoSpriteBases;

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

#endif /* PUFO_ISODRAW_H */
