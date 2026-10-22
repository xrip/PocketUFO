/* pufo_render.h - software surface, blitter, and palette loading.
 *
 * Evidence:
 *   SoftSurf_ctor @ 0x9f96c, SoftSurf_dtor @ 0x9f97c,
 *   SoftSurf_AllocPixels @ 0x9f9a4, SoftSurf_GetWidth @ 0x9f994,
 *   SoftSurf_GetHeight @ 0x9f99c, SoftSurf_GetPixels @ 0x9fcb4,
 *   Render_SetTarget @ 0x9faa4, Render_BlitSoft_LoRes @ 0x9fa7c,
 *   Render_BlitSoft_HiRes @ 0x9fa90, Render_Frame (0x22310),
 *   Blit_LoadSurface @ 0xa3964, Blit_SetTargetParams @ 0xa39ac,
 *   Blit_CopyRows @ 0xa3a14, Blit_CopyRows_HiRes @ 0xa3bb8,
 *   SoftSurf_BlitCopyRaw @ 0xa3db8 (a duplicate of Blit_CopyRows in the original),
 *   SoftSurf_SamplePixel @ 0xa4630, SoftSurf_BlurPixels @ 0xa2000,
 *   SoftSurf_BlitRawGlyph @ 0xa33c8 (named SoftSurf_BlitIndexed_Mode12 in the original),
 *   Gfx_PackRgb565Pair @ 0x2d82c,
 *   BlitParams_Set @ 0xa47b0, Blit_SetRectAndColor @ 0xa3f5c, Blit_SetColorKey @ 0xa3624,
 *   Blit_CopyToXY @ 0xa42e4, SoftSurf_BlitSrcRect_Inner @ 0xa4450,
 *   SoftSurf_Blit_Inner @ 0xa47d0, SoftSurf_BlitFlip_Inner @ 0xa49a0,
 *   SoftSurf_BlitIndexed_Inner @ 0xa306c,
 *   SoftSurf_BlitIndexedKeyed_Inner @ 0xa3638,
 *   SoftSurf_FillRect_Inner @ 0xa3f94,
 *   SoftSurf_BlitIndexedEx @ 0x9fadc, SoftSurf_BlitIndexedKeyedEx @ 0x9fb0c,
 *   SoftSurf_FillRectEx @ 0x9fbbc, SoftSurf_GetAlloc @ 0x9fcbc,
 *   BlitState_Get @ 0xa479c, SoftSurf_BlitRectProxy @ 0x2dce4,
 *   SoftSurf_BlitIndexed @ 0x9e9f4, SoftSurf_BlitIndexedKeyed @ 0x9ea34,
 *   SoftSurf_Blit @ 0x9e558, SoftSurf_BlitFlip @ 0x9e590,
 *   SoftSurf_BlitRectEx @ 0x9fc30, SoftSurf_BlitSrcRectEx @ 0x9fc70,
 *   SoftSurf_BlitRect @ 0x9ebb0, SoftSurf_BlitSrcRect @ 0x9ebf8,
 *   PadImage_ctor @ 0x11000, PadImage_FreeData @ 0x1100c,
 *   PadImage_Load @ 0x11020, PadImage_Blit @ 0x110dc,
 *   Palette_Load @ 0x3e47c.
 *
 * The original renders into one 240x320 16bpp surface (created in
 * Assets_LoadStartup @ 0x382d0 via SoftSurf_AllocPixels(240,320,2)) and blits
 * it to the GAPI framebuffer. This module reproduces that surface, its glyph
 * format, and the blitter's clipping and pixel-mode branches.
 *
 * Glyph / pad format (from SoftSurf_AllocPixels and PadImage_Load):
 *     u16 cxWidth; u16 cyHeight; u8 pixels[cyWidth * cyHeight];
 * The 4-byte header is the allocation prefix, which is why AllocPixels
 * allocates W*H*bytesPP + 4 and returns pAlloc + 4 as pPixels.
 */
#ifndef PUFO_RENDER_H
#define PUFO_RENDER_H

#include "pufo_common.h"

/* Palette table: 256 entries per slot, stored as u32. aPalette @ 0x1472d0.
 * The original never bounds-checks the slot; the highest slot used anywhere in
 * the binary is 0x3F, so 64 slots is the allocation. */
#define PUFO_PAL_SLOTS 64
#define PUFO_PAL_SIZE  256

extern u32 g_aPalette[PUFO_PAL_SLOTS][PUFO_PAL_SIZE];

/* unk_1183F4 @ 0x1183f4: palette slot base added to the glyph palette index by
 * Font_BlitGlyphToSoft @ 0x9ec48 only. 0 at startup; Render_Frame's default branch sets 34. */
extern i32 g_nGlyphPalSlotBase;

/* Pixel modes seen in the code. 2 is the surface the game renders into
 * (16bpp RGB565); 3 is 24bpp, used only for direct-to-framebuffer panels. */
typedef enum PixelMode {
    PIXMODE_8BPP  = 0,
    PIXMODE_16BPP = 2,
    PIXMODE_24BPP = 3
} PixelMode;

/* SoftSurf, 40 bytes in the original (10 dwords). Kept field-for-field. */
typedef struct SoftSurf {
    i32   nWidth;     /* 0x00 */
    i32   nHeight;    /* 0x04 */
    i32   nPixelMode; /* 0x08 */
    i32   nBytesPP;   /* 0x0c */
    u8   *pAlloc;     /* 0x10, allocation base (holds the 4-byte header) */
    u8   *pPixels;    /* 0x14, pAlloc + 4 */
    i32   nBlitW;     /* 0x18 */
    i32   nBlitH;     /* 0x1c */
    i32   nBlitMode;  /* 0x20 */
    u8   *pBlitSrc;   /* 0x24 */
} SoftSurf;

void  SoftSurf_ctor(SoftSurf *pSurf);
void  SoftSurf_dtor(SoftSurf *pSurf);
/* Allocates W*H*bytesPP + 4. Returns pPixels, or NULL on failure. A pixel mode
 * outside 0..3 leaves nBytesPP at 0, exactly as the original does (which then
 * allocates a 4-byte buffer). */
void *SoftSurf_AllocPixels(SoftSurf *pSurf, i32 nWidth, i32 nHeight, i32 nPixelMode);
i32   SoftSurf_GetWidth(const SoftSurf *pSurf);
i32   SoftSurf_GetHeight(const SoftSurf *pSurf);
u8   *SoftSurf_GetPixels(const SoftSurf *pSurf);

/* The original's global 240x320 surface (unk_156A7C @ 0x156a7c). Created in
 * Assets_LoadStartup and used by every non-Ex blit wrapper. */
extern SoftSurf *g_pMainSurface;

/* Render target, 24 bytes in the original. Replaces the GAPI framebuffer
 * binding: GX_PresentFrame @ 0x9ea7c fills this from the GX/rawFrame pitches. */
typedef struct RenderTarget {
    i32  nWidth;      /* 0x00 */
    i32  nHeight;     /* 0x04 */
    i32  nPixelBytes; /* 0x08 */
    i32  nPitchX;     /* 0x0c */
    i32  nPitchY;     /* 0x10 */
    u8  *pBits;       /* 0x14 */
} RenderTarget;

extern RenderTarget g_renderTarget;

void Render_SetTarget(i32 nW, i32 nH, i32 nBytes, i32 nPitchX, i32 nPitchY, u8 *pBits);

/* Blitter state, 40 bytes in the original, global at 0xa3000. */
typedef struct BlitState {
    i32  nSurf0;      /* 0x00, surface width in pixels (stride + x bound) */
    i32  nSurf1;      /* 0x04, surface height (y bound) */
    i32  nSurf2;      /* 0x08, surface pixel mode */
    u8  *pSrc;        /* 0x0c, surface pixel base */
    i32  nWidth;      /* 0x10, copy width  (from Render_SetTarget) */
    i32  nHeight;     /* 0x14, copy height (from Render_SetTarget) */
    i32  nDstPitchX;  /* 0x18 */
    i32  nDstPitchY;  /* 0x1c */
    i32  nPixelMode;  /* 0x20 */
    u8  *pDst;        /* 0x24 */
} BlitState;

extern BlitState g_blitState;

void Blit_LoadSurface(const SoftSurf *pSurf);
void Blit_SetTargetParams(const RenderTarget *pTarget);
void BlitParams_Set(i32 nAlpha, i32 nMaskOrZero);
void Blit_SetRectAndColor(i32 nRectW, i32 nRectH, i32 nColorMid, i32 nColorLow);
i32  Blit_SetColorKey(i32 nKey);
void Blit_CopyToXY(i32 x, i32 y, u32 nIdx);
void SoftSurf_BlitSrcRect_Inner(i32 x, i32 y, u32 nIdx, i32 nKey);
void SoftSurf_BlitRectEx(const SoftSurf *pSurf, i32 x, i32 y, i32 nRectW, i32 nRectH,
                         u32 nIdx, i32 nColorMid, i32 nColorLow);
void SoftSurf_BlitSrcRectEx(const SoftSurf *pSurf, i32 x, i32 y, i32 nRectW, i32 nRectH,
                            u32 nIdx, i32 nColorMid, i32 nColorLow, i32 nKey);
void SoftSurf_BlitRect(i32 x, i32 y, i32 nRectW, i32 nRectH, u32 nIdx,
                       i32 nColorMid, i32 nColorLow);
void SoftSurf_BlitSrcRect(i32 x, i32 y, i32 nRectW, i32 nRectH, u32 nIdx,
                          i32 nColorMid, i32 nColorLow, i32 nKey);

/* Full-surface row copy to the render target. */
void Blit_CopyRows(void);

/* Blit the whole logical surface to the render target
 * (Render_BlitSoft_LoRes @ 0x9fa7c / HiRes @ 0x9fa90). */
void Render_BlitSurface(const SoftSurf *pSurf);

/* --- glyph blits --- */
/* pGlyph points at a {u16 w, u16 h, u8 idx[]} record. The non-Ex forms draw
 * into g_pMainSurface, matching the original's wrappers. The Ex forms take an
 * explicit destination surface. */

void SoftSurf_BlitIndexedEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot);
void SoftSurf_BlitIndexedKeyedEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph,
                                 i32 nPalSlot, u8 nKey);
void SoftSurf_BlitEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph,
                     i32 nPalSlot, i32 nAlpha);
/* Masked blend. The 5th/6th parameters are a raw palette pointer and a mask
 * byte array, NOT a slot index: SoftSurf_BlitFlipEx @ 0x9fb80 forwards nW as the
 * glyph and nH straight through as the palette base with no aPalette[256*slot]
 * conversion, and the RadarScreen call sites pass a computed buffer pointer.
 * IDA's names (nW, nH, nKey) are wrong for this function. */
void SoftSurf_BlitFlipEx(const SoftSurf *pSurf, i32 x, i32 y, const u8 *pGlyph,
                         const u32 *pPal, const u8 *pMask);
void SoftSurf_FillRectEx(const SoftSurf *pSurf, i32 x, i32 y, i32 nRectW, i32 nRectH,
                         i32 nPalSlot, i32 nIdx);
void SoftSurf_FillRect_Inner(i32 x, i32 y, i32 nIdx, const u32 *pPal);
void *SoftSurf_GetAlloc(const SoftSurf *pSurf);
i32  BlitState_Get(void);
i32  SoftSurf_SamplePixel(i32 x, i32 y);
u8  *SoftSurf_BlurPixels(u8 *pPixels, i32 nWidth, i32 nHeight);
void SoftSurf_BlitRawGlyph(i32 x, i32 y, const u8 *pGlyph);
/* What Gfx_PackRgb565Pair reads out of the world. The port has no owner for either value yet,
 * so they are passed in rather than reached through a global. */
typedef struct GfxColorEnv {
    /* The gamma the packer divides by. The original has no field here: 0x2d860 loads
     * Map_nGamma @ 0x128AB8 (Map+0x15100) on EVERY call and turns it into 0.1*g + 0.7 (0x2D870 itod,
     * 0x2D878/0x2D880 the 0.1, 0x2D890/0x2D898 the 0.7), so a gamma press changes the colours as well as
     * the palettes. The port hands the env a POINTER to the live cell rather than a copy: World_NewGame
     * and World_LoadSlot point it at the world's Map+0x15100 mirror, and a test fixture points it at a
     * cell of its own. NULL says the caller never stated one, and 0 is then the value -- what a fresh
     * Map block holds. */
    const i32 *pnGamma;
    i32 nFadeCount;   /* Map.apUiSlotsA[1], the fade counter */
} GfxColorEnv;

u32 Gfx_PackRgb565Pair(const GfxColorEnv *pEnv, i32 r1, i32 g1, i32 b1,
                       i32 r2, i32 g2, i32 b2);

/* unk_147B54 and unk_147B3C @ 0x147b54 / 0x147b3c are not scratch: they are palette slot 2
 * entries 33 and 27 (0x1472D0 + 2*1024 + 4*33 / + 4*27). Gfx_PackRgb565Pair writes the packed pair there,
 * and Text_DrawString @ 0x2d798 forces the glyph palette base to 2 while it draws, so the font's two glyph
 * indices (27 main, 33 secondary - measured over FONT.SPR) pick up exactly the pair the caller just packed.
 * That is how all game text gets its colour, and why the pair calls in a draw arm are observable. */
#define PUFO_TEXT_PAL_SLOT 2
#define PUFO_TEXT_PAL_IDX_FIRST 33  /* unk_147B54 = the (r1,g1,b1) pack */
#define PUFO_TEXT_PAL_IDX_SECOND 27 /* unk_147B3C = the (r2,g2,b2) pack, the returned value */

void SoftSurf_BlitRectProxy(i32 nIgnored, i32 x, i32 y, i32 nRectW, i32 nRectH,
                            u8 nIdx, u8 nColorMid, u8 nColorLow);

void SoftSurf_BlitIndexed(i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot);
void SoftSurf_BlitIndexedKeyed(i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot, u8 nKey);
/* nAlpha is a source weight 0..255, not a colour key (see Blit @ 0x9fb44). */
void SoftSurf_Blit(i32 x, i32 y, const u8 *pGlyph, i32 nPalSlot, i32 nAlpha);
/* Masked variant: per-pixel alpha comes from pMask, not a constant. */
void SoftSurf_BlitFlip(i32 x, i32 y, const u8 *pGlyph, const u32 *pPal, const u8 *pMask);
void SoftSurf_FillRect(i32 x, i32 y, i32 nW, i32 nH, i32 nPalSlot, i32 nIdx);
/* Load a 768-byte RGB palette triple set into slot nSlot (Palette_Load
 * @ 0x3e47c). Converts using g_ffFormat. */
void Palette_Load(const char *pszName, u8 nSlot);

/* Display format flags, the original's gxDisp.ffFormat @ 0x188398+0x14.
 * Values verified from the Palette_Load @ 0x3e47c conversion branches:
 *   0x0040 -> ((r>>3)<<10) | ((g>>3)<<5) | (b>>3)
 *   0x0080 -> ((r>>3)<<11) | ((g>>2)<<5) | (b>>3)    RGB565
 *   0x0100 -> b | (g<<8) | (r<<16)                    BGR888
 * The Win64 target is RGB565, so 0x0080 is the active flag. */
#define PUFO_FF_555 0x0040 /* bit 6 */
#define PUFO_FF_565 0x0080 /* bit 7 */
#define PUFO_FF_888 0x0100 /* bit 8 */
extern u32 g_ffFormat;

/* Palette slots the tactical brightness ramp occupies.
 *
 * Tact_LoadPalettes @ 0x377ec loads tactical.pal into slot 34 and then derives 20 slots from it,
 * 34 through 53. Tact_BuildPaletteLuts @ 0x3734c then walks slots 0 through 56, so the LUT pass covers
 * every slot the ramp writes and nothing above it. */
#define PUFO_PAL_TACTICAL   34
#define PUFO_PAL_RAMP_COUNT 20
#define PUFO_PAL_LUT_SLOTS  57

/* Map_nGamma (IDB name for unk_128AB8), Map+0x15100, a dword: the five handlers store it with STR at
 * 0x190BC / 0x1911C / 0x1917C / 0x191DC / 0x1923C. Two writers, not one -- Map_InitAndLoadRegistry @ 0x22648
 * puts the registry "gamma" here at 0x227A0 (atoi'd at 0x22760, a value above 4 by the CMP at 0x22764 or a
 * negative one by the BMI at 0x22778 falls back to 2 at 0x22794), and MenuBtn_SetGamma0..4 store 0..4 after
 * a press. Three readers: Tact_BuildPaletteLuts @ 0x3734c (the LDR at 0x37368), Gfx_PackRgb565Pair @ 0x2d82c
 * (the LDR at 0x2d860 -- the divisor of every packed colour pair, over 100 call sites) and
 * Options_SyncUiFromMap_GotoScreen12 @ 0x188e4 (0x18944, the gamma pads); Options_SaveToRegistry @ 0x114d8
 * takes the same cell back out to the registry at 0x11510. The port models the registry pair in the options
 * layer: Options_Load clamps "gamma" to 0..4 with the default 2 (pufo_options.c:126) and Options_Save writes
 * it back (pufo_options.c:106).
 * The LUT divisor is 0.1 * gamma + 0.7, so 3 is neutral (divisor 1.0), 0 brightens (1/0.7) and 4 dims
 * (1/1.1). */
#define PUFO_PAL_GAMMA_MAX 4

/* The port does not own the Map block, so the gamma cell is handed in once, the same way the render
 * target is (Render_SetTarget). ARM takes no argument here: 0x37368 loads Map_nGamma into R1 before the
 * slot loop, and Tact_LoadPalettes @ 0x377ec / Tact_BuildPaletteLuts @ 0x3734c are both `__fastcall()`
 * with an empty argument list. Install the SAME cell the gamma handlers write. With none installed the cell
 * reads 0, and that is a fixture value rather than the original's startup value: Map_InitAndLoadRegistry
 * puts the registry "gamma" (default 2) into Map+0x15100 at 0x227A0 before anything can press a widget. */
void Render_SetGammaCell(const i32 *pCell);

/* Tact_BuildPaletteLuts @ 0x3734c. Rescale every channel of palette slots 0..56 in place by
 * 1 / (0.1 * gamma + 0.7), in the field layout selected by g_ffFormat. The gamma cell is read once,
 * before the loop, exactly as 0x37368 reads it once into R1. */
void Tact_BuildPaletteLuts(void);

/* Tact_LoadPalettes @ 0x377ec. Load the eight tactical palettes, derive the 20-slot brightness ramp
 * from tactical.pal, then run the gamma pass (the tail call at 0x37A98). */
void Tact_LoadPalettes(void);

/* Pad image (PadImage, 20 bytes). pData is a glyph record. */
typedef struct PadImage {
    i32  nOriginX; /* 0x00 */
    i32  nOriginY; /* 0x04 */
    u16  uWidth;   /* 0x08 */
    u16  uHeight;  /* 0x0a */
    u8  *pData;    /* 0x0c */
    u8   nMode;    /* 0x10, palette slot */
} PadImage;

void PadImage_ctor(PadImage *pPad);
void PadImage_FreeData(PadImage *pPad);
/* Loads "<pszName>.pad" from the archives. nMode is the palette slot. */
bool PadImage_Load(PadImage *pPad, const char *pszName, u8 nMode);
void PadImage_Blit(const PadImage *pPad);
void PadImage_BlitEx(const PadImage *pPad, u8 nKey);

#endif /* PUFO_RENDER_H */
