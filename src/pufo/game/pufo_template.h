/* pufo_template.h - mission map templates (.ma1) and their application to cells.
 *
 * Evidence: Map_LoadMa1 @ 0x54ba4, CDialogTemplate ctor @ 0x54c3c,
 *           GetTemplateByte0 @ 0x54c68, GetTemplateByte1 @ 0x54c5c,
 *           GetLevelCount @ 0x54c74, GetCellByte @ 0x54c80, *           Map_ApplyTemplateToCells @ 0x54cf4.
 *
 * ## .ma1 layout
 *
 * Confirmed byte-for-byte against all 159 `.ma1` members of the shipped
 * `main.dat`: every one satisfies `size == 3 + 4 * nRows * nCols * levels`.
 *
 *     u8  nRows     template height in map cells  (header byte 0)
 *     u8  nCols     template width  in map cells  (header byte 1)
 *     u8  nLevels   number of z levels
 *     u8  aCells[4 * nRows * nCols * nLevels]
 *
 * The cell data is indexed
 *     aCells[4 * (nRows * nCols * z + nCols * y + x) + nSlot]
 * so each cell carries 4 slot bytes.
 *
 * **Header byte 0 is the Y extent and header byte 1 is the X extent, not the other way round.**
 * That is not obvious from the format and an earlier version of this header had it backwards. Three
 * independent places in the original agree: `Map_ApplyTemplateToCells @ 0x54cf4` builds its
 * destination as `60 * (10 * nOffY + row) + (10 * nOffX + col)` where `row` loops to byte 0 and
 * `col` loops to byte 1; `CDialogTemplate_GetCellByte @ 0x54c80` uses byte 1 as the row stride; and
 * every `Map_LoadRmp` call in `Map_BuildMissionTerrain (0x22e10)` passes `nSizeX = byte1 / 10` and
 * `nSizeY = byte0 / 10`. The names `nRows` / `nCols` are chosen so the mistake cannot recur.
 *
 * `Map_ApplyTemplateToCells` reads those four
 * slots as floor, wall A, wall B and ceiling sprite indices:
 *
 *     slot 0 -> floor
 *     slot 1 -> wall A
 *     slot 2 -> wall B
 *     slot 3 -> ceiling
 *
 * and a slot value of 0 or 1 means "no sprite" (the original tests `<= 1`), while
 * any higher value is looked up as `fontSys.anSprBase[slot] + value - 2`.
 */
#ifndef PUFO_TEMPLATE_H
#define PUFO_TEMPLATE_H

#include "game/pufo_mapcell.h"
#include "pufo_common.h"

/* Header size of a .ma1 member. */
#define PUFO_MA1_HDR 3

/* Bytes per template cell: four sprite slots. */
#define PUFO_MA1_CELL_BYTES 4

/* Slot indices inside a template cell. */
#define PUFO_MA1_SLOT_FLOOR   0
#define PUFO_MA1_SLOT_WALL_A  1
#define PUFO_MA1_SLOT_WALL_B  2
#define PUFO_MA1_SLOT_CEIL    3

/* A loaded .ma1 template. Mirrors CDialogTemplate @ 0x54c3c, which is 20 bytes:
 * pTemplate is the loaded blob and the other four pointers are views into it. */
typedef struct MapTemplate {
    u8  *pBlob;      /* the loaded .ma1, or NULL */
    u32  cbBlob;
    u8   nRows;      /* pBlob[0], the Y extent in cells */
    u8   nCols;      /* pBlob[1], the X extent in cells */
    u8   nLevels;    /* pBlob[2] */
    u8   nFlag;      /* the byte Map_LoadMa1 stores at pOut + 4 */
} MapTemplate;

/* Map_LoadMa1 @ 0x54ba4. Reads "<pszBase>.ma1" from the archives. Returns false
 * when the member is missing or too small to hold its own header. Frees any
 * previous blob, exactly as the original does. */
/* Map_LoadMa1 @ 0x54ba4, and the fold of CDialogTemplate @ 0x54c3c: that constructor does one thing,
 * `this->pTemplate = nullptr` (IDB 0x54C3C, sizeof 0x18 from the stack stride in sub_22E10, no vptr).
 * The port's MapTemplate records start zeroed at the call site, so this is where the cleared pointer
 * appears; the constructor has no function of its own here. */
bool MapTemplate_Load(MapTemplate *pTpl, const char *pszBase, u8 nFlag);

void MapTemplate_Free(MapTemplate *pTpl);

/* CDialogTemplate_GetLevelCount @ 0x54c74. Header byte 2. */
u8 MapTemplate_GetLevelCount(const MapTemplate *pTpl);

/* CDialogTemplate_GetTemplateByte0 @ 0x54c68: `LDR R3,[R0]` then `LDRB R0,[R3]`. Header byte 0, the
 * Y extent. The original re-reads it from the blob on every call; Map_BuildMissionTerrain divides it
 * by 10 to turn cells into tiles. */
u8 MapTemplate_GetHeaderByte0(const MapTemplate *pTpl);

/* CDialogTemplate_GetTemplateByte1 @ 0x54c5c: `LDR R3,[R0]` then `LDRB R0,[R3,#1]`. Header byte 1,
 * the X extent. */
u8 MapTemplate_GetHeaderByte1(const MapTemplate *pTpl);

/* CDialogTemplate_GetCellByte @ 0x54c80. Returns 0 when the coordinates are out
 * of range; the original indexes unchecked (see the parity notes). */
u8 MapTemplate_GetCellByte(const MapTemplate *pTpl, u32 nX, u32 nY, u32 nZ, u32 nSlot);

/* The sprite index in a slot, or 0 when the slot means "no sprite".
 * `Map_ApplyTemplateToCells` treats 0 and 1 alike as absent. */
u8 MapTemplate_GetSpriteIndex(const MapTemplate *pTpl, u32 nX, u32 nY, u32 nZ, u32 nSlot);

/* Map_ApplyTemplateToCells @ 0x54cf4.
 *
 * Walks the template row by row -- nRows outer, nCols inner, nLevels innermost, which is the order
 * the ARM nests them -- and stamps each cell's four sprite pointers. pfnLookup resolves the
 * destination cell. pfnGetSprite maps a slot's sprite index to a sprite record; pass NULL to store
 * no sprites (useful for testing the traversal).
 *
 * nOffX and nOffY are TILE coordinates, not cell coordinates. The original's destination is
 *     (x, y) = (PUFO_CELLS_PER_TILE * nOffX + col, PUFO_CELLS_PER_TILE * nOffY + row)
 * which it builds as `10 * (6 * (10 * nOffY + row) + nOffX) + col`: the 6 and the 10 multiply to the
 * 60-cell row stride, so the flat index is the same one the cell array uses. */
typedef Mc1Rec *(*TemplateSpriteLookup)(u8 nSlotValue, void *pUser);

void MapTemplate_ApplyToCells(const MapTemplate *pTpl,
                              MapCellLookup pfnLookup, void *pUser,
                              i32 nOffX, i32 nOffY,
                              TemplateSpriteLookup pfnGetSprite,
                              void *pSpriteUser);

#endif /* PUFO_TEMPLATE_H */
