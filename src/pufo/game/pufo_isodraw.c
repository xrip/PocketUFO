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

/* unk_AA018, read from the binary: 00 08 0C 10 00 04 00 00. */
static const u8 g_aAnimGlyphOffset[8] = { 0, 8, 12, 16, 0, 4, 0, 0 };

/* TactUnit_DrawAnimGlyphs @ 0x1c6ac. */
void TactUnit_DrawAnimGlyphs(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim,
                             const IsoSpriteBases *pBases)
{
    i32 nKind;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }

    nKind = (i32)(pIso->pCell->nFlags32 & 7u);
    if (nKind == 0) {
        return;
    }

    (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY,
                         (u16)(g_aAnimGlyphOffset[nKind] + (pAnim->nAnimPhase >> 1) +
                               pBases->nSmokeBase),
                         pIso->nPal);

    if (nKind == 4) {
        /* The second glyph uses the entry for kind 1, which is 8. */
        (void)Font_DrawGlyph(pIso->nDrawX, pIso->nDrawY,
                             (u16)(g_aAnimGlyphOffset[1] + (pAnim->nAnimPhase >> 1) +
                                   pBases->nSmokeBase),
                             pIso->nPal);
    }
}

/* TactUnit_DrawFacingGlyph @ 0x1c754. */
void TactUnit_DrawFacingGlyph(IsoDrawCtx *pIso, const IsoAnimGlobals *pAnim,
                              const IsoSpriteBases *pBases)
{
    u32 nFlagsField;

    if (pIso == NULL || pIso->pCell == NULL) {
        return;
    }
    if (pAnim->bPhaseActive == 0) {
        return;
    }

    nFlagsField = pIso->pCell->nFlags32 & 0x78u;
    if (nFlagsField == 0 || nFlagsField > 0x40u) {
        return;
    }

    (void)Font_DrawGlyph(pIso->nDrawX - 48, pIso->nDrawY - 12,
                         (u16)(pBases->nExplBase - ((pIso->pCell->nFlags32 >> 3) & 0xFu) + 8u),
                         0);
}
