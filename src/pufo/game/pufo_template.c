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

/* Map_LoadMa1 @ 0x54ba4. The original appends ".ma1" to the caller's base name and reads the whole
 * member; it stores the flag byte at pOut + 4. Three things the instructions show and this port does
 * differently, all of them stated rather than assumed:
 *
 *   - ??0CDialogTemplate@@QAA@XZ @ 0x54c3c is the template ctor, one store `pTemplate = 0` at 0x54C40.
 *     MapTemplate_Load reproduces it with its leading MapTemplate_Free(pTpl) (the same store the
 *     original's own inline guard-and-delete at 0x54BC8 leaves behind), so a load that fails leaves a
 *     NULL blob exactly as the ctor+dtor pair does.
 *   - the flag store at 0x54BD0 happens BEFORE the name copy, the Find and the read, so a failed load
 *     in the original still records the flag. Here the flag is stored only on the success path, which
 *     is the answer the shell's own callers can see (a failed load frees the old blob and the caller
 *     keeps the previous template state instead of a half-written one).
 *   - the name goes into a 16-byte stack buffer (pszName[16]) by an unbounded byte loop at
 *     0x54BDC..0x54BEC followed by strcat ".ma1" at 0x54BF8, so a base name of 12 characters already
 *     writes 17 bytes past that buffer. AssetReader_Find's result is never tested either: on a miss
 *     the size is 0, `operator new(0)` at 0x54C14 still returns a pointer, and the function returns
 *     AssetReader_Read's refusal value. The port bounds the name (szName[64]) and refuses. */
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
    pTpl->nRows   = pTpl->pBlob[0];   /* header byte 0 is the Y extent */
    pTpl->nCols   = pTpl->pBlob[1];   /* header byte 1 is the X extent */
    pTpl->nLevels = pTpl->pBlob[2];
    pTpl->nFlag   = nFlag;
    return true;
}

/* ??1CDialogTemplate@@QAA@XZ @ 0x54c48: the template dtor, `if (pTemplate) operator delete(pTemplate)`,
 * called four times from Map_BuildMissionTerrain (mirrored at pufo_terrain.c). MapTemplate_Free is its
 * port counterpart: the port's template struct is caller-owned, so the heap part the dtor releases is
 * the blob, not the struct. */
void MapTemplate_Free(MapTemplate *pTpl)
{
    free(pTpl->pBlob);
    pTpl->pBlob   = NULL;
    pTpl->cbBlob  = 0;
    pTpl->nRows   = 0;
    pTpl->nCols   = 0;
    pTpl->nLevels = 0;
}

/* CDialogTemplate_GetLevelCount @ 0x54c74. The level count is header byte 2.
 * The original reads it through the pointer every call; the port keeps it in the struct
 * when the template loads, which is the same value. */
u8 MapTemplate_GetLevelCount(const MapTemplate *pTpl)
{
    return pTpl->nLevels;
}

/* CDialogTemplate_GetTemplateByte0 @ 0x54c68. `LDR R3,[R0]` then `LDRB R0,[R3]`: header byte 0, the
 * Y extent. Map_BuildMissionTerrain divides it by 10 to turn cells into tiles. */
u8 MapTemplate_GetHeaderByte0(const MapTemplate *pTpl)
{
    return pTpl->nRows;
}

/* CDialogTemplate_GetTemplateByte1 @ 0x54c5c. `LDR R3,[R0]` then `LDRB R0,[R3,#1]`: header byte 1,
 * the X extent. */
u8 MapTemplate_GetHeaderByte1(const MapTemplate *pTpl)
{
    return pTpl->nCols;
}

/* CDialogTemplate_GetCellByte @ 0x54c80 (116 bytes). It calls the accessors three times -- byte 1 at
 * 0x54C94, byte 0 at 0x54CA0 and byte 1 AGAIN at 0x54CAC -- and then, with every factor masked to
 * eight bits (AND #0xFF at 0x54CB0, 0x54CB4, 0x54CBC, 0x54CC4, 0x54CC8, 0x54CD0, so a caller that
 * passes 259 reaches cell 3), builds
 *
 *     pTemplate[4 * (byte0 * byte1 * nZ) + 4 * (byte1 * nY) + 4 * nX + nSlot + 3]
 *
 * from MUL at 0x54CB8/0x54CC0/0x54CCC, ADD at 0x54CD4/0x54CD8, `ADD R3, R2, R3, LSL #2` at 0x54CE0
 * (nSlot is the fourth argument and arrives on the stack: `LDRB R2,[SP,#nSlot]` at 0x54CDC), and the
 * final `LDRB R0,[R3,#3]` at 0x54CEC. The row stride is byte 1 and the level stride is byte0*byte1,
 * which is what makes byte 1 the X extent. There is no bound check anywhere.
 *
 * The port adds the bounds check the original lacks (out of range reads outside the allocation in the
 * original, which is undefined rather than a value), and takes u32 arguments: an over-range index is
 * refused here instead of being truncated to its low byte. */
u8 MapTemplate_GetCellByte(const MapTemplate *pTpl, u32 nX, u32 nY, u32 nZ, u32 nSlot)
{
    u32 nIndex;

    if (pTpl->pBlob == NULL) {
        return 0;
    }
    if (nX >= pTpl->nCols || nY >= pTpl->nRows || nZ >= pTpl->nLevels
        || nSlot >= PUFO_MA1_CELL_BYTES) {
        return 0;
    }
    nIndex = PUFO_MA1_CELL_BYTES
           * ((u32)pTpl->nRows * pTpl->nCols * nZ + (u32)pTpl->nCols * nY + nX)
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
 * Nesting is the original's: header byte 0 outer (the rows), header byte 1 inner, then the level
 * count innermost. For each cell it resolves the four slot sprites and calls MapCell_SetTileSprites
 * with (floor, wallA, wallB, ceiling).
 *
 * The destination is a cell of the MAP grid, and nOffX / nOffY are tile coordinates: the ARM builds
 * `10 * (6 * (10 * nOffY + row) + nOffX) + col`, which is the flat index of
 * (x, y) = (10 * nOffX + col, 10 * nOffY + row). The template coordinates (col, row) are what the
 * slot reads take; the map coordinates are what the lookup takes. */
void MapTemplate_ApplyToCells(const MapTemplate *pTpl,
                              MapCellLookup pfnLookup, void *pUser,
                              i32 nOffX, i32 nOffY,
                              TemplateSpriteLookup pfnGetSprite,
                              void *pSpriteUser)
{
    u32 nRow;
    u32 nCol;
    u32 nZ;

    if (pTpl->pBlob == NULL || pfnLookup == NULL) {
        return;
    }

    for (nRow = 0; nRow < pTpl->nRows; ++nRow) {
        for (nCol = 0; nCol < pTpl->nCols; ++nCol) {
            for (nZ = 0; nZ < pTpl->nLevels; ++nZ) {
                MapCell *pCell = pfnLookup(PUFO_CELLS_PER_TILE * nOffX + (i32)nCol,
                                           PUFO_CELLS_PER_TILE * nOffY + (i32)nRow,
                                           (i32)nZ, pUser);
                Mc1Rec  *pFloor  = NULL;
                Mc1Rec  *pWallA  = NULL;
                Mc1Rec  *pWallB  = NULL;
                Mc1Rec  *pCeil   = NULL;

                if (pCell == NULL) {
                    continue;
                }
                if (pfnGetSprite != NULL) {
                    u8 nFloor = MapTemplate_GetSpriteIndex(pTpl, nCol, nRow, nZ, PUFO_MA1_SLOT_FLOOR);
                    u8 nWA    = MapTemplate_GetSpriteIndex(pTpl, nCol, nRow, nZ, PUFO_MA1_SLOT_WALL_A);
                    u8 nWB    = MapTemplate_GetSpriteIndex(pTpl, nCol, nRow, nZ, PUFO_MA1_SLOT_WALL_B);
                    u8 nCeil  = MapTemplate_GetSpriteIndex(pTpl, nCol, nRow, nZ, PUFO_MA1_SLOT_CEIL);

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
