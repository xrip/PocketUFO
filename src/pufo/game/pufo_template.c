/* pufo_template.c - mission map templates (.ma1) and their application to cells.
 *
 * Evidence: Map_LoadMa1 @ 0x54ba4, CDialogTemplate_GetCellByte @ 0x54c80,
 *           Map_ApplyTemplateToCells @ 0x54cf4.
 */
#include "game/pufo_template.h"

#include <stdlib.h>
#include <string.h>

#include "asset/pufo_asset.h"
#include "util/pufo_str.h"

/* Map_LoadMa1 @ 0x54ba4. The original appends ".ma1" to the caller's base name
 * and reads the whole member; it stores the flag byte at pOut + 4. */
bool MapTemplate_Load(MapTemplate *pTpl, const char *pszBase, u8 nFlag)
{
    AssetReader rdr;
    char        szName[64];
    u32         cb;

    MapTemplate_Free(pTpl);

    pufo_strncpy(szName, pszBase, sizeof(szName));
    if (!pufo_strcat(szName, ".ma1", sizeof(szName))) {
        return false;
    }

    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, szName)) {
        return false;
    }
    cb = AssetReader_Size(&rdr);
    if (cb < PUFO_MA1_HDR) {
        return false;
    }
    pTpl->pBlob = malloc(cb);
    if (pTpl->pBlob == NULL) {
        return false;
    }
    if (!AssetReader_Read(&rdr, pTpl->pBlob, cb)) {
        MapTemplate_Free(pTpl);
        return false;
    }

    pTpl->cbBlob  = cb;
    pTpl->nDimX   = pTpl->pBlob[0];
    pTpl->nDimY   = pTpl->pBlob[1];
    pTpl->nLevels = pTpl->pBlob[2];
    pTpl->nFlag   = nFlag;
    return true;
}

void MapTemplate_Free(MapTemplate *pTpl)
{
    free(pTpl->pBlob);
    pTpl->pBlob   = NULL;
    pTpl->cbBlob  = 0;
    pTpl->nDimX   = 0;
    pTpl->nDimY   = 0;
    pTpl->nLevels = 0;
}

/* CDialogTemplate_GetCellByte @ 0x54c80.
 *
 * The original computes
 *     pTemplate[4 * dimX * dimY * nZ + 4 * dimY * nY + 4 * nX + nSlot + 3]
 * with no bound check, and reads dimX/dimY fresh from the header each call. The
 * trailing +3 skips the header. Reproduced, with a bounds check added because the
 * original's behaviour out of range is undefined rather than defined. */
u8 MapTemplate_GetCellByte(const MapTemplate *pTpl, u32 nX, u32 nY, u32 nZ, u32 nSlot)
{
    u32 nIndex;

    if (pTpl->pBlob == NULL) {
        return 0;
    }
    if (nX >= pTpl->nDimX || nY >= pTpl->nDimY || nZ >= pTpl->nLevels
        || nSlot >= PUFO_MA1_CELL_BYTES) {
        return 0;
    }
    nIndex = PUFO_MA1_CELL_BYTES
           * ((u32)pTpl->nDimX * pTpl->nDimY * nZ + (u32)pTpl->nDimY * nY + nX)
           + nSlot + PUFO_MA1_HDR;
    if (nIndex >= pTpl->cbBlob) {
        return 0;
    }
    return pTpl->pBlob[nIndex];
}

/* Map_ApplyTemplateToCells tests `value <= 1` to mean "no sprite", so both 0 and
 * 1 are absent. Everything above is a real index. */
u8 MapTemplate_GetSpriteIndex(const MapTemplate *pTpl, u32 nX, u32 nY, u32 nZ, u32 nSlot)
{
    u8 nValue = MapTemplate_GetCellByte(pTpl, nX, nY, nZ, nSlot);

    return (nValue <= 1u) ? 0u : nValue;
}

/* Map_ApplyTemplateToCells @ 0x54cf4.
 *
 * Traversal order is x inner, then y, then z: the disassembly walks nX from 0 to
 * dimX-1, then nY from 0 to dimY-1, then nZ from 0 to nLevels-1. For each cell it
 * resolves the four slot sprites and calls MapCell_SetTileSprites with
 * (floor, wallA, wallB, ceiling).
 *
 * The original's destination stride is the MAP's cell grid (2400 bytes per layer,
 * 240 per tile row in the IDB's byte math), not the template's. Here the lookup
 * takes absolute cell coordinates so the caller owns the grid geometry. */
void MapTemplate_ApplyToCells(const MapTemplate *pTpl,
                              MapCellLookup pfnLookup, void *pUser,
                              i32 nOffX, i32 nOffY,
                              TemplateSpriteLookup pfnGetSprite,
                              void *pSpriteUser)
{
    u32 nX;
    u32 nY;
    u32 nZ;

    if (pTpl->pBlob == NULL || pfnLookup == NULL) {
        return;
    }

    for (nZ = 0; nZ < pTpl->nLevels; ++nZ) {
        for (nY = 0; nY < pTpl->nDimY; ++nY) {
            for (nX = 0; nX < pTpl->nDimX; ++nX) {
                MapCell *pCell = pfnLookup((i32)nX + nOffX, (i32)nY + nOffY,
                                           (i32)nZ, pUser);
                Mc1Rec  *pFloor  = NULL;
                Mc1Rec  *pWallA  = NULL;
                Mc1Rec  *pWallB  = NULL;
                Mc1Rec  *pCeil   = NULL;

                if (pCell == NULL) {
                    continue;
                }
                if (pfnGetSprite != NULL) {
                    u8 nFloor = MapTemplate_GetSpriteIndex(pTpl, nX, nY, nZ, PUFO_MA1_SLOT_FLOOR);
                    u8 nWA    = MapTemplate_GetSpriteIndex(pTpl, nX, nY, nZ, PUFO_MA1_SLOT_WALL_A);
                    u8 nWB    = MapTemplate_GetSpriteIndex(pTpl, nX, nY, nZ, PUFO_MA1_SLOT_WALL_B);
                    u8 nCeil  = MapTemplate_GetSpriteIndex(pTpl, nX, nY, nZ, PUFO_MA1_SLOT_CEIL);

                    if (nFloor != 0) {
                        pFloor = pfnGetSprite(nFloor, pSpriteUser);
                    }
                    if (nWA != 0) {
                        pWallA = pfnGetSprite(nWA, pSpriteUser);
                    }
                    if (nWB != 0) {
                        pWallB = pfnGetSprite(nWB, pSpriteUser);
                    }
                    if (nCeil != 0) {
                        pCeil = pfnGetSprite(nCeil, pSpriteUser);
                    }
                }
                MapCell_SetTileSprites(pCell, pFloor, pWallA, pWallB, pCeil);
            }
        }
    }
}
