/* pufo_isodraw.c - see pufo_isodraw.h for evidence and the field map. */
#include "game/pufo_isodraw.h"

#include "game/pufo_font.h"   /* Font_DrawGlyph_IndexedOffset / g_fontSys */

void MapView_DrawCellFloorGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    Mc1Rec *pFloorSpr;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }
    pFloorSpr = pIso->pCell->pFloorSpr;
    if (pFloorSpr == NULL) {
        return;
    }
    Font_DrawGlyph_IndexedOffset(pIso->nDrawX, pIso->nDrawY - (i32)pFloorSpr->gap_25,
                                 pFloorSpr, pAnim->nAnimPhase, pIso->nPal);
}

void MapView_DrawCellOccupantGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    Mc1Rec *pCeilSpr;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }
    pCeilSpr = pIso->pCell->pCeilSpr;
    if (pCeilSpr == NULL) {
        return;
    }
    Font_DrawGlyph_IndexedOffset(pIso->nDrawX, pIso->nDrawY - (i32)pCeilSpr->gap_25,
                                 pCeilSpr, pAnim->nAnimPhase, pIso->nPal);
}

/* Shared body of the two wall helpers; only the sprite field differs. */
static void MapView_DrawWallGlyph(IsoDrawCtx *pIso, Mc1Rec *pWallSpr,
                                  const IsoAnimGlobals *pAnim)
{
    u8 bPal;

    if (pWallSpr->bBlocksMove != 0) {
        if ((pIso->pCell->bFlags29 & 1) != 0) {
            bPal = pAnim->nAnimPhaseScratch0;
        } else if ((pIso->pCell->bFlags29 & 4) != 0) {
            bPal = 7;
        } else {
            bPal = 0;
        }
        pIso->bWallPalIdx = bPal;
    } else {
        pIso->bWallPalIdx = pAnim->nAnimPhase;
    }
    Font_DrawGlyph_IndexedOffset(pIso->nDrawX, pIso->nDrawY, pWallSpr,
                                 pIso->bWallPalIdx, pIso->nPal);
}

void MapView_SetWallPalFromSlotA(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    if (pIso == NULL || pIso->pCell == NULL || pIso->pCell->pWallSprA == NULL) {
        return;
    }
    MapView_DrawWallGlyph(pIso, pIso->pCell->pWallSprA, pAnim);
}

void MapView_SetWallPalFromSlotB(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim)
{
    if (pIso == NULL || pIso->pCell == NULL || pIso->pCell->pWallSprB == NULL) {
        return;
    }
    MapView_DrawWallGlyph(pIso, pIso->pCell->pWallSprB, pAnim);
}
