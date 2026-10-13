/* pufo_font.h - bitmap font loading, glyph blitting and text measurement.
 *
 * Evidence: Font_Init @ 0x460e8, Font_Free @ 0x46144, Font_LoadSpr @ 0x461d8,
 *           Font_BuildIndex @ 0x46480, Font_GetSprByIndex @ 0x46600,
 *           Font_DrawGlyph @ 0x4661c, Font_DrawGlyph_IndexedOffset @ 0x46630,
 *           Font_BlitGlyphToSoft @ 0x9ec48, SoftSurf_BlitGlyph @ 0x9fc00,
 *           SoftSurf_BlitGlyph_Inner @ 0xa41d4,
 *           Text_DrawString @ 0x2d798, Text_MeasureWidth @ 0x2dca8.
 *
 * Two asset files make up a font:
 *   "<name>.spr"  glyph pixel data
 *   "<name>.mc1"  tile-sprite records (52 bytes each)
 *
 * .spr layout, confirmed byte-for-byte against lang.dat FONT.SPR (20551 bytes,
 * first u16 == 256, first offset == 514 == 2 + 256*2):
 *
 *   u16 nGlyphCount
 *   u16 anOffset[nGlyphCount]   relative to the start of the .spr member
 *   <glyph records>
 *
 * Glyph record, decoded from SoftSurf_BlitGlyph_Inner @ 0xa41d4. It is a
 * run-length encoded bitstream, not a fixed-size bitmap:
 *
 *   u8 nStartY        added to the pen y
 *   u8 nStartX        added to the pen x
 *   then a command stream, one byte at a time:
 *     0xFF  end of glyph
 *     0xFE  end of row; the NEXT byte is the new nStartX for that row
 *     0x00  transparent pixel (advance x, draw nothing)
 *     else  draw a pixel with palette index = this byte, advance x
 *
 * Note this is a different record shape from the .pad glyphs, which use a
 * u16 width/height header (see pufo_render.h). Both exist in the original.
 *
 * .mc1 records are 52 bytes each and are stored contiguously; Font_LoadSpr sets
 * each record's nSlot and nGlobalIdx as it loads.
 */
#ifndef PUFO_FONT_H
#define PUFO_FONT_H

#include "game/pufo_mapcell.h"
#include "pufo_common.h"

/* The original's Font is 1436 bytes with 40 slots and 40 base indices. */
#define PUFO_FONT_SLOTS   40
#define PUFO_FONT_NAMELEN 20

/* Palette slot cap applied by Font_BlitGlyphToSoft @ 0x9ec48. */
#define PUFO_FONT_PAL_MAX 19

typedef struct PufoFont {
    u8        bInited;                              /* 0x598 */
    u8        nSlotCount;                           /* 0x400 */
    u16       nTotalSprFrames;                      /* 0x36 */
    u16       nTotalMc1;                            /* 0x38 */
    u16       anMc1Base[PUFO_FONT_SLOTS];           /* 0x3a */
    u16       anSprBase[PUFO_FONT_SLOTS];           /* 0x8a */
    /* Glyph index base used by Text_DrawString @ 0x2d798, which reads a u16 at
     * Font+0x28 (IDA's fontSys.gap_32) and adds the character byte to it. */
    u16       nGlyphBase;                           /* 0x28 */
    char      aszName[PUFO_FONT_SLOTS][PUFO_FONT_NAMELEN]; /* 0xe0 */
    void     *apSprData[PUFO_FONT_SLOTS];           /* 0x4f4 */
    Mc1Rec   *apMc1Data[PUFO_FONT_SLOTS];           /* 0x404 */
    u16       anMc1Count[PUFO_FONT_SLOTS];          /* 0x4a4 */
    /* Flattened lookup tables, built by Font_BuildIndex @ 0x46480. */
    Mc1Rec  **apMc1Index;                           /* 0x594 */
    u8      **apSprIndex;                           /* 0xdc sibling */
    u32       nMc1IndexCount;
    u32       nSprIndexCount;
} PufoFont;

/* Per-character advance widths, 256 bytes. Assets_LoadStartup @ 0x382d0 reads
 * "fontsize.dat" into unk_156A54 @ 0x156a54 and Text_MeasureWidth @ 0x2dca8
 * indexes it directly by character byte. */
typedef struct FontMetrics {
    u8 aAdvance[256];
    bool bLoaded;
} FontMetrics;

extern FontMetrics g_fontMetrics;

/* The original's fontSys @ 0x158268. */
extern PufoFont g_fontSys;

void PufoFont_Init(PufoFont *pFont);

/* Font_Free @ 0x46144. Releases each loaded slot's .spr and .mc1 block, then the two
 * index arrays, then clears the init flag.
 *
 * Three places the port is stricter than the original:
 *   - The original wraps everything in `if (pFont->bInited)`. PufoFont_Free runs unconditionally;
 *     freeing NULL pointers is a no-op, so the only difference is a font that was never inited.
 *   - It deletes apMc1Index only inside the `apSprIndex != nullptr` branch, so a font with a null
 *     spr index and a live mc1 index leaks. The port frees both.
 *   - It leaves nSlotCount, the slot pointers and the index pointers dangling; only bInited goes
 *     to 0. The port clears them so a second free cannot double-release. */
void PufoFont_Free(PufoFont *pFont);

/* Loads "<pszName>.spr" and, if present, "<pszName>.mc1". Returns true if the
 * .spr was found. Increments the slot count on success. */
bool Font_LoadSpr(PufoFont *pFont, const char *pszName);

/* Builds the flat glyph/record lookup tables. Safe to call repeatedly; the
 * original frees the previous tables first. */
void Font_BuildIndex(PufoFont *pFont);

/* Record lookup by flat index. Returns NULL when out of range. */
Mc1Rec *Font_GetSprByIndex(const PufoFont *pFont, u16 nIndex);

/* What Map_PatchCraftSprites @ 0x25ab4 reads out of the font block and the Map block. The port owns
 * neither, so both come in here. */
typedef struct CraftSpritePatchEnv {
    u16     nCraftSprBase;   /* fontSys + 0x06, the craft sprite group */
    MapCell *const *apCells; /* Map.apCells @ 0x15338, flattened [4][60][60] */
    i32     nMapLayers;      /* Map+0x15114 */
    i32     nCraftMapX;      /* Map_nCraftMapX @ 0x15199 */
    i32     nCraftMapY;      /* Map_nCraftMapY @ 0x1519A */
} CraftSpritePatchEnv;

/* Map_PatchCraftSprites @ 0x25ab4. Only runs when the map has exactly two layers: it recolours three
 * craft sprites, clears a flag on four more, and stamps those four onto twelve cells of layer 1 around
 * the craft's tile. */
void Map_PatchCraftSprites(PufoFont *pFont, const CraftSpritePatchEnv *pEnv);

/* Font_LoadSpr from the archive into g_fontMetrics. Missing file leaves the
 * table empty and bLoaded false. */
void FontMetrics_Load(void);

/* Parses the .spr header: glyph count and the offset table. Exposed so the
 * format can be verified against real data. pOutOffsets may be NULL; when
 * non-NULL it must hold at least nGlyphCount entries.
 * Returns the glyph count, or 0 if the member is too small. */
u16 Spr_ParseHeader(const u8 *pSpr, u32 cbSpr, u16 *pOutOffsets, u16 nOutCap);

/* Text_DrawString @ 0x2d798 / Text_MeasureWidth @ 0x2dca8. */
i32  Text_MeasureWidth(const char *psz);
void Text_DrawString(i32 x, i32 y, const char *psz);

/* Glyph helpers from the original, exposed for the UI layer. */
void Font_DrawGlyph(i32 x, i32 y, u16 nIdx, i32 nPal);
/* nIdx is added to pFont->anSprBase[pTbl->nSlot]; see Font_DrawGlyph_IndexedOffset
 * @ 0x46630. */
void Font_DrawGlyph_IndexedOffset(i32 x, i32 y, const Mc1Rec *pTbl, u8 nIdx, i32 nPal);

/* Decode one RLE glyph record and blit it into the main surface.
 * Exposed so the format can be tested against real .spr data without a screen. */
void Font_BlitGlyph(const u8 *pGlyph, i32 x, i32 y, const u32 *pPal);

/* Returns the glyph record for a character byte, or NULL. */
const u8 *Font_GetGlyph(const PufoFont *pFont, u8 ch);

#endif /* PUFO_FONT_H */
