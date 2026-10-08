/* pufo_template.h - mission map templates (.ma1) and their application to cells.
 *
 * Evidence: Map_LoadMa1 @ 0x54ba4, CDialogTemplate ctor @ 0x54c3c,
 *           GetTemplateByte0 @ 0x54c68, GetTemplateByte1 @ 0x54c5c,
 *           GetLevelCount @ 0x54c74, GetCellByte @ 0x54c80,
 *           Map_ApplyTemplateToCells @ 0x54cf4.
 *
 * ## .ma1 layout
 *
 * Confirmed byte-for-byte against all 159 `.ma1` members of the shipped
 * `main.dat`: every one satisfies `size == 3 + 4 * dimX * dimY * levels`.
 *
 *     u8  nDimX     template width  in map cells
 *     u8  nDimY     template height in map cells
 *     u8  nLevels   number of z levels
 *     u8  aCells[4 * nDimX * nDimY * nLevels]
 *
 * The cell data is indexed
 *     aCells[4 * (nDimX * nDimY * z + nDimY * y + x) + nSlot]
 * so each cell carries 4 slot bytes. `Map_ApplyTemplateToCells` reads those four
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
    u8   nDimX;      /* pTemplate[0] */
    u8   nDimY;      /* pTemplate[1] */
    u8   nLevels;    /* pTemplate[2] */
    u8   nFlag;      /* the byte Map_LoadMa1 stores at pOut + 4 */
} MapTemplate;

/* Map_LoadMa1 @ 0x54ba4. Reads "<pszBase>.ma1" from the archives. Returns false
 * when the member is missing or too small to hold its own header. Frees any
 * previous blob, exactly as the original does. */
bool MapTemplate_Load(MapTemplate *pTpl, const char *pszBase, u8 nFlag);

void MapTemplate_Free(MapTemplate *pTpl);

/* CDialogTemplate_GetCellByte @ 0x54c80. Returns 0 when the coordinates are out
 * of range; the original indexes unchecked (see the parity notes). */
u8 MapTemplate_GetCellByte(const MapTemplate *pTpl, u32 nX, u32 nY, u32 nZ, u32 nSlot);

/* The sprite index in a slot, or 0 when the slot means "no sprite".
 * `Map_ApplyTemplateToCells` treats 0 and 1 alike as absent. */
u8 MapTemplate_GetSpriteIndex(const MapTemplate *pTpl, u32 nX, u32 nY, u32 nZ, u32 nSlot);

/* Map_ApplyTemplateToCells @ 0x54cf4.
 *
 * Walks the template in x, then y, then z order and stamps each cell's four
 * sprite pointers. pfnLookup resolves the destination cell. pfnGetSprite maps a
 * slot's sprite index to a sprite record; pass NULL to store no sprites (useful
 * for testing the traversal).
 *
 * The offsets are added to a base cell, which is how the original places a
 * template at a position inside the larger map: nOffX and nOffY are in template
 * units and the grid stride is the map's own dimensions. */
typedef Mc1Rec *(*TemplateSpriteLookup)(u8 nSlotValue, void *pUser);

void MapTemplate_ApplyToCells(const MapTemplate *pTpl,
                              MapCellLookup pfnLookup, void *pUser,
                              i32 nOffX, i32 nOffY,
                              TemplateSpriteLookup pfnGetSprite,
                              void *pSpriteUser);

#endif /* PUFO_TEMPLATE_H */
