/* pufo_isodraw.h - the per-cell isometric glyph draw helpers.
 *
 * Evidence: MapView_DrawCellOccupantGlyph @ 0x1b45c,
 *           MapView_DrawCellFloorGlyph @ 0x1c904,
 *           MapView_SetWallPalFromSlotA @ 0x1c7dc,
 *           MapView_SetWallPalFromSlotB @ 0x1c870.
 *
 * The original threads a 316-byte IsoDrawCtx through the isometric renderer. Only
 * the fields these four helpers touch are modelled:
 *
 *   pCell        0x00
 *   nDrawX       0x04
 *   nDrawY       0x08
 *   bWallPalIdx  0x64
 *   nPal         0x6c
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
    u8       bWallPalIdx; /* 0x64 */
    i32      nPal;        /* 0x6c */
} IsoDrawCtx;

typedef struct IsoAnimGlobals {
    u8 nAnimPhase;         /* pCtxBase.nAnimPhase @ 0x37 */
    u8 nAnimPhaseScratch0; /* nAnimPhaseScratch[0] */
} IsoAnimGlobals;

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

#endif /* PUFO_ISODRAW_H */
