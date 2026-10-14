/* pufo_font.c - bitmap font loading, glyph blitting and text measurement.
 *
 * Evidence: see pufo_font.h for the full address list.
 */
#include "game/pufo_font.h"

#include <stdlib.h>
#include <string.h>

#include "asset/pufo_asset.h"
#include "game/pufo_mapctx.h"   /* PUFO_MAP_CELL_DIM */
#include "render/pufo_render.h"
#include "util/pufo_str.h"

PufoFont    g_fontSys;
FontMetrics g_fontMetrics;

/* SoftSurf_BlitGlyph_Inner @ 0xa41d4 command bytes. */
#define GLYPH_END_ROW  0xFEu
#define GLYPH_END      0xFFu

void PufoFont_Init(PufoFont *pFont)
{
    memset(pFont, 0, sizeof(*pFont));
    pFont->bInited = 1;
    /* The original clears the 40 apMc1Data slots here; memset above already
     * covers them, so only the flags need setting. */
}

void PufoFont_Free(PufoFont *pFont)
{
    u8 i;

    for (i = 0; i < pFont->nSlotCount && i < PUFO_FONT_SLOTS; ++i) {
        free(pFont->apSprData[i]);
        pFont->apSprData[i] = NULL;
        free(pFont->apMc1Data[i]);
        pFont->apMc1Data[i] = NULL;
    }
    free(pFont->apMc1Index);
    pFont->apMc1Index = NULL;
    free(pFont->apSprIndex);
    pFont->apSprIndex = NULL;
    pFont->nSlotCount  = 0;
    pFont->nTotalMc1   = 0;
    pFont->nTotalSprFrames = 0;
    pFont->bInited     = 0;
}

/* .spr header: u16 count, then count u16 offsets relative to the member start.
 * Verified against lang.dat FONT.SPR: count 256, first offset 514 == 2 + 256*2. */
u16 Spr_ParseHeader(const u8 *pSpr, u32 cbSpr, u16 *pOutOffsets, u16 nOutCap)
{
    u16 nCount;
    u32 i;

    if (pSpr == NULL || cbSpr < 2) {
        return 0;
    }
    nCount = (u16)pSpr[0] | ((u16)pSpr[1] << 8);
    if ((u32)nCount > (cbSpr - 2u) / 2u) {
        return 0; /* table does not fit in the member */
    }
    if (pOutOffsets != NULL) {
        u16 nCopy = (nCount < nOutCap) ? nCount : nOutCap;

        for (i = 0; i < nCopy; ++i) {
            pOutOffsets[i] = (u16)pSpr[2 + 2 * i] | ((u16)pSpr[3 + 2 * i] << 8);
        }
    }
    return nCount;
}

const u8 *Font_GetGlyph(const PufoFont *pFont, u8 ch)
{
    const u8 *pSpr = (const u8 *)pFont->apSprData[0];
    u16       nCount;
    u16       nOff;

    if (pSpr == NULL) {
        return NULL;
    }
    nCount = (u16)pSpr[0] | ((u16)pSpr[1] << 8);
    if ((u16)ch >= nCount) {
        return NULL;
    }
    nOff = (u16)pSpr[2 + 2 * (u16)ch] | ((u16)pSpr[3 + 2 * (u16)ch] << 8);
    return pSpr + nOff;
}

bool Font_LoadSpr(PufoFont *pFont, const char *pszName)
{
    AssetReader rdr;
    char        szName[64];
    u8          nSlot = pFont->nSlotCount;
    u16         nSprFrames = 0;

    if (nSlot >= PUFO_FONT_SLOTS) {
        return false;
    }

    pFont->anMc1Base[nSlot] = pFont->nTotalMc1;
    pFont->anSprBase[nSlot] = pFont->nTotalSprFrames;
    pufo_strncpy(pFont->aszName[nSlot], pszName, PUFO_FONT_NAMELEN);

    /* --- .spr --- */
    pufo_strncpy(szName, pszName, sizeof(szName));
    if (!pufo_strcat(szName, ".spr", sizeof(szName))) {
        return false;
    }
    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, szName)) {
        return false;
    }
    {
        u32 cb = AssetReader_Size(&rdr);

        pFont->apSprData[nSlot] = malloc(cb);
        if (pFont->apSprData[nSlot] == NULL) {
            return false;
        }
        if (!AssetReader_Read(&rdr, pFont->apSprData[nSlot], cb)) {
            free(pFont->apSprData[nSlot]);
            pFont->apSprData[nSlot] = NULL;
            return false;
        }
        /* The original reads the frame count as the leading u16. */
        nSprFrames = ((const u8 *)pFont->apSprData[nSlot])[0]
                   | ((u16)((const u8 *)pFont->apSprData[nSlot])[1] << 8);
    }

    /* --- .mc1 (optional) --- */
    pufo_strncpy(szName, pszName, sizeof(szName));
    if (!pufo_strcat(szName, ".mc1", sizeof(szName))) {
        return false;
    }
    AssetReader_Clear(&rdr);
    if (AssetReader_Find(&rdr, szName)) {
        u32 cb = AssetReader_Size(&rdr);
        u16 nRecs = (u16)(cb / (u32)sizeof(Mc1Rec));

        pFont->apMc1Data[nSlot] = (Mc1Rec *)malloc((size_t)nRecs * sizeof(Mc1Rec));
        if (pFont->apMc1Data[nSlot] == NULL) {
            return false;
        }
        if (!AssetReader_Read(&rdr, pFont->apMc1Data[nSlot], cb)) {
            free(pFont->apMc1Data[nSlot]);
            pFont->apMc1Data[nSlot] = NULL;
            return false;
        }
        pFont->anMc1Count[nSlot] = nRecs;
        /* The original stamps each record with its slot and global index. */
        for (u16 i = 0; i < nRecs; ++i) {
            pFont->apMc1Data[nSlot][i].nSlot      = nSlot;
            pFont->apMc1Data[nSlot][i].nGlobalIdx = (u16)(pFont->nTotalMc1 + i);
        }
    } else {
        pFont->anMc1Count[nSlot] = 0;
    }

    pFont->nSlotCount += 1;
    pFont->nTotalMc1  = (u16)(pFont->nTotalMc1 + pFont->anMc1Count[nSlot]);
    pFont->nTotalSprFrames = (u16)(pFont->nTotalSprFrames + nSprFrames);
    return true;
}

/* Font_BuildIndex @ 0x46480. Builds two flat tables:
 *   apSprIndex[i]  -> pointer to glyph i, walking the offset table of each slot
 *   apMc1Index[i]  -> &apMc1Data[slot][k], walking each slot's record count
 * The original frees existing tables first and aborts if that leaves a dangling
 * pointer, so the count is recomputed here on every call. */
void Font_BuildIndex(PufoFont *pFont)
{
    u32 nSprTotal = 0;
    u32 nMc1Total = 0;
    u32 nSpr = 0;
    u32 nMc1 = 0;
    u8  nSlot;

    free(pFont->apSprIndex);
    free(pFont->apMc1Index);
    pFont->apSprIndex = NULL;
    pFont->apMc1Index = NULL;

    for (nSlot = 0; nSlot < pFont->nSlotCount && nSlot < PUFO_FONT_SLOTS; ++nSlot) {
        const u8 *pSpr = (const u8 *)pFont->apSprData[nSlot];

        if (pSpr != NULL) {
            nSprTotal += (u32)((u16)pSpr[0] | ((u16)pSpr[1] << 8));
        }
        nMc1Total += pFont->anMc1Count[nSlot];
    }

    pFont->apSprIndex = (u8 **)malloc(sizeof(u8 *) * (nSprTotal != 0 ? nSprTotal : 1u));
    pFont->apMc1Index = (Mc1Rec **)malloc(sizeof(Mc1Rec *) * (nMc1Total != 0 ? nMc1Total : 1u));
    if (pFont->apSprIndex == NULL || pFont->apMc1Index == NULL) {
        return;
    }
    pFont->nSprIndexCount = nSprTotal;
    pFont->nMc1IndexCount = nMc1Total;

    for (nSlot = 0; nSlot < pFont->nSlotCount && nSlot < PUFO_FONT_SLOTS; ++nSlot) {
        const u8 *pSpr = (const u8 *)pFont->apSprData[nSlot];

        if (pSpr != NULL) {
            u16 nFrames = (u16)pSpr[0] | ((u16)pSpr[1] << 8);

            for (u16 k = 0; k < nFrames; ++k) {
                u16 nOff = (u16)pSpr[2 + 2 * k] | ((u16)pSpr[3 + 2 * k] << 8);

                pFont->apSprIndex[nSpr++] = (u8 *)(pSpr + nOff);
            }
        }
        for (u16 k = 0; k < pFont->anMc1Count[nSlot]; ++k) {
            pFont->apMc1Index[nMc1++] = &pFont->apMc1Data[nSlot][k];
        }
    }
}

Mc1Rec *Font_GetSprByIndex(const PufoFont *pFont, u16 nIndex)
{
    if (pFont->apMc1Index == NULL || (u32)nIndex >= pFont->nMc1IndexCount) {
        return NULL;
    }
    return pFont->apMc1Index[nIndex];
}

/* The ARM is `LDRH R2, [R0,#2*nFlag]` off fontSys, where fontSys is the struct itself: bInited is at
 * offset 0, so indexing the block as a u16 array by nFlag lands on the right two bytes. */
u16 Font_GetFlagSprBase(const PufoFont *pFont, u8 nFlag)
{
    return ((const u16 *)pFont)[nFlag];
}

/* The original writes the palette bytes of a craft sprite with a counted byte loop (STRB [R3],#1 /
 * SUBS / BPL) starting at record offset 8. Two of the three calls run twelve bytes, one runs five. */
static void Font_SetPalBytes6(Mc1Rec *pSpr, i32 nCount)
{
    i32 i;

    if (pSpr == NULL) {
        return;
    }
    for (i = 0; i < nCount; ++i) {
        pSpr->abPal[i] = 6;
    }
}

/* Map_PatchCraftSprites @ 0x25ab4.
 *
 * Runs only on a two-layer map. Three things happen:
 *
 * 1. Palette bytes. Craft sprites +13 and +3 get all twelve abPal bytes set to 6; sprite +21 gets only
 *    its first five. abPal is at record offset 8 (ARM: ADD R3, R0, #8 then STRB [R3],#1), and the twelve
 *    byte loops cover offsets 8 through 19.
 * 2. A flag clear. Sprites +42, +43, +44 and +45 each get the u16 at record offset 0x14 set to 0
 *    (ARM: STRH R7, [R0,#0x14]). Hex-Rays calls this field uUnk20; the offset is 0x14, and what it means
 *    is not established, so the port keeps the name and records the writer.
 * 3. Twelve cell stamps on layer 1, all writing pWallSprA.
 *
 * The cell coordinates are the part to be careful about. The original puts 10 * nCraftMapY + row into the
 * slot that every other reader calls nMapX, and 10 * nCraftMapX + col into the nMapY slot. That is the
 * same X/Y swap the MapView_DrawUnitHeightGlyph gate shows (nMapX against nFireTgtY, nMapY against
 * nFireTgtX), so it is a convention in this game rather than a mistake, and the port reproduces it.
 *
 * Hex-Rays hides this by advancing the Map base: at 0x25BD4 it renders v10 = v10 + 86329 and then reads
 * v10->bTactFlag. 0x1139B8 + 86329 + offsetof(Map, bTactFlag) lands on Map_nCraftMapX, so the operand is
 * the craft tile column, not a flag. */
void Map_PatchCraftSprites(PufoFont *pFont, const CraftSpritePatchEnv *pEnv)
{
    /* row goes to the nMapX slot, col to the nMapY slot, both added to 10 * the craft tile coordinate. */
    static const struct {
        i8 nRow;
        i8 nCol;
        u8 nSprOffset;
    } aStamps[12] = {
        { 2,  7, 42 }, { 4,  5, 42 }, { 3,  6, 42 },
        { 8,  9, 43 }, { 7, 10, 43 }, { 6, 11, 43 },
        { 6,  5, 44 }, { 7,  6, 44 }, { 8,  7, 44 },
        { 2,  9, 45 }, { 3, 10, 45 }, { 4, 11, 45 },
    };
    i32 i;

    if (pEnv->nMapLayers != 2) {                          /* 0x25ACC */
        return;
    }

    /* 0x25AE0, 0x25B10, 0x25B38: the palette bytes. */
    Font_SetPalBytes6(Font_GetSprByIndex(pFont, (u16)(pEnv->nCraftSprBase + 13)), 12);
    Font_SetPalBytes6(Font_GetSprByIndex(pFont, (u16)(pEnv->nCraftSprBase + 3)), 12);
    Font_SetPalBytes6(Font_GetSprByIndex(pFont, (u16)(pEnv->nCraftSprBase + 21)), 5);

    /* 0x25B64 through 0x25BA0: the flag clear. */
    for (i = 0; i < 4; ++i) {
        Mc1Rec *pSpr = Font_GetSprByIndex(pFont, (u16)(pEnv->nCraftSprBase + 42 + i));
        if (pSpr != NULL) {
            pSpr->uUnk20 = 0;
        }
    }

    /* 0x25BF0 through 0x25E58: the twelve stamps. */
    for (i = 0; i < 12; ++i) {
        i32 nMapX = 10 * pEnv->nCraftMapY + aStamps[i].nRow;
        i32 nMapY = 10 * pEnv->nCraftMapX + aStamps[i].nCol;
        Mc1Rec *pSpr = Font_GetSprByIndex(pFont, (u16)(pEnv->nCraftSprBase + aStamps[i].nSprOffset));
        MapCell *pCell;

        if (nMapX < 0 || nMapX >= PUFO_MAP_CELL_DIM ||
            nMapY < 0 || nMapY >= PUFO_MAP_CELL_DIM ||
            pSpr == NULL) {
            continue;
        }

        pCell = pEnv->apCells[PUFO_MAP_CELL_DIM * (PUFO_MAP_CELL_DIM * 1 + nMapX) + nMapY];
        if (pCell != NULL) {
            pCell->pWallSprA = pSpr;
        }
    }
}

void FontMetrics_Load(void)
{
    AssetReader rdr;

    memset(&g_fontMetrics, 0, sizeof(g_fontMetrics));
    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, "fontsize.dat")) {
        return;
    }
    {
        u32 cb = AssetReader_Size(&rdr);

        /* The table is 256 bytes; the shipped file is exactly that. */
        if (cb > sizeof(g_fontMetrics.aAdvance)) {
            cb = sizeof(g_fontMetrics.aAdvance);
        }
        if (AssetReader_Read(&rdr, g_fontMetrics.aAdvance, cb)) {
            g_fontMetrics.bLoaded = true;
        }
    }
}

/* Font_BlitGlyph @ 0xa41d4, the RLE decoder. This is the whole glyph rasteriser;
 * keeping it in one function makes the command stream easy to compare with the
 * disassembly. */
void Font_BlitGlyph(const u8 *pGlyph, i32 x, i32 y, const u32 *pPal)
{
    i32       nPenX;
    i32       nPenY;
    const u8 *pCmd;

    if (pGlyph == NULL || pPal == NULL) {
        return;
    }
    if (g_blitState.nPixelMode != 1 && g_blitState.nPixelMode != 2) {
        return; /* the original only implements 16bpp glyphs */
    }

    nPenY = y + pGlyph[0]; /* leading u8 is the start Y */
    nPenX = x + pGlyph[1]; /* second u8 is the start X */
    pCmd  = pGlyph + 1;

    for (;;) {
        u8 nRun = *++pCmd;
        i32 nSurf0 = g_blitState.nSurf0;
        i32 nSurf1 = g_blitState.nSurf1;

        if (nRun == GLYPH_END) {
            break;
        }
        if (nRun == GLYPH_END_ROW) {
            /* Next byte is the new row's start X. */
            u8 nNewX;

            ++nPenY;
            nNewX = *++pCmd;
            nPenX = (i32)nNewX + x;
            continue;
        }
        /* Any other byte is a palette entry -- except 0. The ARM at 0xa4288 tests
         * `*v5 != 0` and writes only when it is non-zero, so index 0 is transparency:
         * the pen advances and the pixel is left alone. */
        if (nRun != 0 && nPenX >= 0 && nPenY >= 0 && nPenX < nSurf0 && nPenY < nSurf1) {
            u16 *pDst = (u16 *)((u8 *)g_blitState.pSrc
                                + 2 * (nSurf0 * nPenY + nPenX));

            *pDst = (u16)pPal[nRun];
        }
        ++nPenX;
    }
}

void Font_DrawGlyph(i32 x, i32 y, u16 nIdx, i32 nPal)
{
    const u8 *pGlyph;

    if (nPal > PUFO_FONT_PAL_MAX) {
        nPal = PUFO_FONT_PAL_MAX;
    }
    if (g_fontSys.apSprIndex == NULL
        || (u32)nIdx >= g_fontSys.nSprIndexCount) {
        return;
    }
    pGlyph = g_fontSys.apSprIndex[nIdx];
    Blit_LoadSurface(g_pMainSurface);
    /* Font_BlitGlyphToSoft @ 0x9ec48 picks the palette as slot (unk_1183F4 + nPal) -- the
     * glyph base the shell's Render_Frame step will maintain. At startup it is 0. */
    Font_BlitGlyph(pGlyph, x, y,
                   g_aPalette[(g_nGlyphPalSlotBase + nPal) & (PUFO_PAL_SLOTS - 1)]);
}

void Font_DrawGlyph_IndexedOffset(i32 x, i32 y, const Mc1Rec *pTbl, u8 nIdx, i32 nPal)
{
    u16 nSprIdx;

    if (pTbl == NULL) {
        return;
    }
    nSprIdx = (u16)(g_fontSys.anSprBase[pTbl->nSlot] + nIdx);
    Font_DrawGlyph(x, y, nSprIdx, nPal);
}

/* Text_MeasureWidth @ 0x2dca8: sum the advance byte of every character. */
i32 Text_MeasureWidth(const char *psz)
{
    i32 nWidth = 0;

    for (; *psz != '\0'; ++psz) {
        nWidth += g_fontMetrics.aAdvance[(u8)*psz];
    }
    return nWidth;
}

/* Text_DrawString @ 0x2d798. Note the original saves a dword at pCtx+19004 and
 * overwrites it with 2 for the duration of the draw, then restores it. That
 * cell is a text-mode/state flag; it is reproduced via a local so the visible
 * behaviour (glyph index base nGlyphBase + char) is what matters here. */
void Text_DrawString(i32 x, i32 y, const char *psz)
{
    for (; *psz != '\0'; ++psz) {
        u8 ch = (u8)*psz;

        Font_DrawGlyph(x, y, (u16)(g_fontSys.nGlyphBase + ch), 0);
        x += g_fontMetrics.aAdvance[ch];
    }
}
