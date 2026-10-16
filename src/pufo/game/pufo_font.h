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
    /* The five group bases the .ma1 templates index. Map_ApplyTemplateToCells @ 0x54cf4 resolves every
     * template slot as Font_GetSprByIndex(fontSys, *(u16 *)(fontSys + 2 * nFlag) + value - 2), so the
     * flag byte selects one of these five slots -- they are the font block's first five u16s, at
     * +0x00, +0x02, +0x04, +0x06 and +0x08. Font_LoadTactSprites @ 0x257c4 is their only writer, and
     * the shipped templates only ever carry flags 2, 3 and 4. */
    u16       anFlagSprBase[5];                     /* 0x00 blanks, 0x02 cursor, 0x04 terrain modules,
                                                     * 0x06 craft (fontSys_nCraftSprBase), 0x08 UFO modules */
    u8        bInited;                              /* 0x598 */
    u8        nSlotCount;                           /* 0x400 */
    /* The four alien sprite-set bases Tact_SpawnAliens @ 0x3a2ac writes at 0x3AAFC: for each of
     * Map_abSpriteSet[0..3] that is not 23, the running nTotalSprFrames at the moment that set was
     * loaded. Slot i is left untouched when abSpriteSet[i] is 23, so it keeps whatever the previous
     * mission left there - the original does not clear it. */
    u16       anAlienSprBase[4];                    /* 0x12, fontSys_anAlienSprBase @ 0x15827a */
    u16       nTotalSprFrames;                      /* 0x36 */
    u16       nTotalMc1;                            /* 0x38 */
    u16       anMc1Base[PUFO_FONT_SLOTS];           /* 0x3a */
    u16       anSprBase[PUFO_FONT_SLOTS];           /* 0x8a */
    /* Glyph index base used by Text_DrawString @ 0x2d798, which reads a u16 at
     * Font+0x28 (IDA's fontSys.gap_32) and adds the character byte to it. */
    u16       nGlyphBase;                           /* 0x28 */
    /* The three geoscape sprite-group bases Geoscape_InitUI @ 0x857f4 captures before
     * Font_LoadSpr("bigobs"/"basebits"/"mapbits"), same rule as every group base: the
     * running nTotalSprFrames at load time is the group's first frame index. */
    u16       nBigObsBase;                          /* 0x26, nBigObsSprBase */
    u16       nBaseBitsBase;                        /* 0x2C, fontSys_nBaseBitsSprBase */
    u16       nMapBitsBase;                         /* 0x34, fontSys_nMapBitsSprBase */
    /* The rest of the group bases Font_LoadTactSprites @ 0x257c4 writes. Each is the running counter
     * at the moment its own group is loaded -- nTotalSprFrames for the .spr groups, nTotalMc1 for the
     * two .mc1-backed slots in anFlagSprBase. */
    u16       anXcomSprBase[4];                     /* 0x0A, 0x0C, 0x0E, 0x10: the xcom_0..3 sets */
    u16       nCivmSprBase;                         /* 0x1A, urban missions only */
    u16       nCivfSprBase;                         /* 0x1C, urban missions only */
    u16       nHandobBase;                          /* 0x20, nHandobSprBase */
    u16       nFloorobBase;                         /* 0x22, nFloorobSprBase */
    u16       nTanksBase;                           /* 0x24, nTanksSprBase */
    u16       nBulletsBase;                         /* 0x2A, nBulletsSprBase */
    u16       nScangBase;                           /* 0x2E, nScangSprBase */
    u16       nSmokeBase;                           /* 0x30, nSmokeSprBase */
    u16       nExplBase;                            /* 0x32, nExplSprBase */
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

/* The sprite-index base a .ma1 template's four slots are added to. Map_ApplyTemplateToCells @ 0x54cf4
 * resolves every slot as Font_GetSprByIndex(fontSys, *(u16 *)(fontSys + 2 * nFlag) + value - 2), where
 * nFlag is the byte Map_LoadMa1 stored at CDialogTemplate + 4. So the base is chosen by the flag the
 * template was loaded with: 2 for terrain modules, 3 for the craft pad, 4 for the UFO module. The
 * flag-3 base is the u16 at fontSys + 0x06, which is also what Map_PatchCraftSprites reads as the
 * craft sprite group -- the craft pad and the craft sprites come from the same group. */
u16 Font_GetFlagSprBase(const PufoFont *pFont, u8 nFlag);

/* --- Font_LoadTactSprites @ 0x257c4 (752 B) : the tactical sprite set and its group bases ---
 *
 * Reached from Map_BuildMissionTerrain (0x22e10) at 0x22E4C and from Save_ReadTGameData (0x44248) at
 * 0x4444C; both already had it as a hook because it was not ported. It is the only writer of the
 * group bases in the font block, and the rule is the same for every one of them: the base is the
 * running counter captured immediately BEFORE that group's Font_LoadSpr, so it is the group's first
 * frame index. The counter is fontSys.nTotalSprFrames (+0x36) for the .spr groups and
 * fontSys.nTotalMc1 (+0x38) for the two .mc1-backed slots at +0x04 and +0x06.
 *
 * 1. blanks -> anFlagSprBase[0], then Font_LoadSpr("blanks").
 * 2. cursor -> anFlagSprBase[1], then Font_LoadSpr("cursor").
 * 3. the mission-terrain group -> anFlagSprBase[2], from nTotalMc1 read once here; then four names
 *    aTactTerrainSetName[4 * nMissionType + i] for i = 0..3 (0xA6858, 40 names of 9), each skipped
 *    when it is the empty string. nTotalMc1 is re-read after every load that actually ran.
 * 4. the craft group -> anFlagSprBase[3], only when nMapLayers != 0, from the nTotalMc1 the step-3
 *    loop left behind; the name is g_aPlaneName[nMapLayers - 1] (aPlane + 9 * nMapLayers - 9).
 * 5. the UFO-module group -> anFlagSprBase[4], only when nTerrainVariant != 0, again from the
 *    nTotalMc1 left by step 4; the class is abTactUfoClassByVariant[3 * (nTerrainVariant - 1) +
 *    nTerrainParam] (0xA6AE0, 16 bytes) and the four names are aTactUfoSetName[4 * class + i]
 *    (0xA69C0, 32 names of 9). nTotalMc1 is NOT re-read inside this loop, so the base stored at
 *    +0x08 is the value from before the UFO loads -- and nothing later reads it as a running total.
 * 6. "font" -> nGlyphBase, then xcom_0/1/2/3 -> anXcomSprBase[0..3], each from nTotalSprFrames.
 * 7. urban missions only (nMissionType == 5): "civm" (aXcom0 + 0x99) -> nCivmSprBase, then "civf"
 *    (aXcom0 + 0x90) -> nCivfSprBase. The two names are out of table order -- civm sits after civf.
 * 8. "tanks" -> nTanksBase, "floorob" -> nFloorobBase, "handob" -> nHandobBase, "bigobs" ->
 *    nBigObsBase, "bullets" -> nBulletsBase, "scang" -> nScangBase, "smoke" -> nSmokeBase,
 *    "expl" -> nExplBase. The first three names come out of the aXcom0 table (+0x24, and floorob /
 *    handob / bigobs are their own strings); the order the loads run is tanks, floorob, handob,
 *    bigobs even though the slots are 0x24, 0x22, 0x20, 0x26.
 * 9. Font_BuildIndex.
 *
 * Deviation: the original reads nMissionType, nMapLayers, nTerrainVariant and the Map+0x1510C dword
 * straight out of the global Map block, and indexes the three name tables with no bound at all -- a
 * mission type above 9 or a terrain variant above 5 reads past the end of aTactTerrainSetName /
 * abTactUfoClassByVariant. The port takes the four bytes through the env and bounds the two indexes,
 * skipping the group instead of reading out of the table. */
typedef struct TactSpriteEnv {
    u8   nMissionType;     /* Map+0x1511A */
    u8   nMapLayers;       /* Map+0x15114 */
    u8   nTerrainVariant;  /* Map+0x15111 */
    i32  nTerrainParam;    /* Map+0x1510C, the dword Map_BuildMissionTerrain also takes the tier from */
    const char (*paTerrainSets)[9]; /* aTactTerrainSetName @ 0xA6858, 40 slots */
    const char (*paUfoSets)[9];     /* aTactUfoSetName @ 0xA69C0, 32 slots */
    const char (*paPlaneNames)[9];  /* g_aPlaneName @ 0xA6668, 3 slots, taken as [nMapLayers - 1] here */
    const u8   *paUfoClass;         /* abTactUfoClassByVariant @ 0xA6AE0, 16 bytes */
    i32  nTerrainSetCount;          /* 10 mission types in the shipped table */
    i32  nUfoClassCount;            /* 8 classes in the shipped table */
    i32  nVariantCount;             /* 5 variants in the shipped table */
} TactSpriteEnv;

/* The three shipped tables, dumped by tools/gen_tact_sprite_tables.py and checked by
 * docs/parity/audit_tables.py. */
extern const char g_aTactTerrainSetName[40][9];
extern const char g_aTactUfoSetName[32][9];
extern const u8   g_abTactUfoClassByVariant[16];

void Font_LoadTactSprites(PufoFont *pFont, const TactSpriteEnv *pEnv);

/* What Map_PatchCraftSprites @ 0x25ab4 reads out of the font block and the Map block. The port owns
 * neither, so both come in here. */
typedef struct CraftSpritePatchEnv {
    u16     nCraftSprBase;   /* fontSys + 0x06, the craft sprite group */
    MapCell *const *apCells; /* Map.apCells @ 0x15338, flattened [4][60][60] */
    i32     nMapLayers;      /* Map+0x15114 */
    i32     nCraftMapX;      /* Map_nCraftMapX @ Map+0x15139 (0x128AF1) */
    i32     nCraftMapY;      /* Map_nCraftMapY @ Map+0x1513A (0x128AF2) */
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

/* Text_DrawString @ 0x2d798 / Text_MeasureWidth @ 0x2dca8. Text_DrawString forces the glyph
 * palette slot base (g_nGlyphPalSlotBase, Map + 0x4A3C) to 2 for the duration of the draw and
 * restores it: Gfx_PackRgb565Pair writes the current colour pair into slot 2 entries 27/33, and
 * the font glyphs use exactly those indices. */
i32  Text_MeasureWidth(const char *psz);
void Text_DrawString(i32 x, i32 y, const char *psz);

/* Glyph helpers from the original, exposed for the UI layer.
 * Font_DrawGlyph @ 0x4661c tail-calls Font_BlitGlyphToSoft @ 0x9ec48, which clamps nPal to 19
 * and picks the palette as slot (g_nGlyphPalSlotBase + nPal) -- unk_1183F4 is 0 at startup. */
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
