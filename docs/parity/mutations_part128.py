#!/usr/bin/env python3
"""Part 128's mutation proofs: the 30 `render` rows row 121a had uncited.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part128.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part128.py HiRes

Rows: Palette_Load 0x3e47c, SoftSurf_Blit 0x9e558, SoftSurf_BlitFlip 0x9e590,
SoftSurf_BlitIndexed 0x9e9f4, SoftSurf_BlitIndexedKeyed 0x9ea34, SoftSurf_BlitRect 0x9ebb0,
SoftSurf_BlitSrcRect 0x9ebf8, SoftSurf_ctor 0x9f96c, SoftSurf_dtor 0x9f97c, SoftSurf_GetWidth 0x9f994,
SoftSurf_GetHeight 0x9f99c, SoftSurf_GetPixels 0x9fcb4, Render_BlitSoft_LoRes 0x9fa7c,
Render_BlitSoft_HiRes 0x9fa90, SoftSurf_BlitIndexedEx 0x9fadc, SoftSurf_BlitIndexedKeyedEx 0x9fb0c,
SoftSurf_BlitEx 0x9fb44, SoftSurf_BlitFlipEx 0x9fb80, SoftSurf_FillRectEx 0x9fbbc,
SoftSurf_BlitGlyph 0x9fc00 (in pufo_font.c, where the pair is inlined), SoftSurf_BlitRectEx 0x9fc30,
SoftSurf_BlitSrcRectEx 0x9fc70, Blit_SetColorKey 0xa3624, SoftSurf_BlitIndexedKeyed_Inner 0xa3638,
Blit_LoadSurface 0xa3964, Blit_CopyRows_HiRes 0xa3bb8, Blit_SetRectAndColor 0xa3f5c,
BlitParams_Set 0xa47b0, SoftSurf_Blit_Inner 0xa47d0, SoftSurf_BlitFlip_Inner 0xa49a0.

Each case attacks the one thing the row's own checks pin: which surface a wrapper loads, which
argument a wrapper forwards into which slot, which field an accessor reads, which operand a setter
publishes, or which field of a packed colour a kernel writes. `src/` is CRLF, so `case()` translates a
"\n" in the anchor into the file's own line ending -- otherwise a multi-line anchor silently matches
nothing and the runner prints SKIP.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

C = "src/pufo/render/pufo_render.c"
F = "src/pufo/game/pufo_font.c"
T = "test_render"
TF = "test_font"

CASES = []


def case(name, target, path, old, new, show=8):
    data = open(path, "rb").read()
    crlf = data.count(b"\r\n") > data.count(b"\n") - data.count(b"\r\n")
    eol = "\r\n" if crlf else "\n"
    CASES.append((name, target, path, old.replace("\n", eol).encode(), new.replace("\n", eol).encode(), show))


# Palette_Load @ 0x3e47c: the format bits are tested in order, 0x40 (555) first. Swapping the first
# test into 565 makes a member with both bits convert with the wrong form.
case("Palette-0x3e47c-format-order", T, C,
     "        if ((g_ffFormat & PUFO_FF_555) != 0) {",
     "        if ((g_ffFormat & PUFO_FF_565) != 0) {")

# SoftSurf_Blit @ 0x9e558 forwards its fifth argument as the alpha of SoftSurf_BlitEx @ 0x9fb44.
case("Blit-0x9e558-alpha-arg", T, C,
     "    SoftSurf_BlitEx(g_pMainSurface, x, y, pGlyph, nPalSlot, nAlpha);",
     "    SoftSurf_BlitEx(g_pMainSurface, x, y, pGlyph, nAlpha, nPalSlot);")

# SoftSurf_BlitFlip @ 0x9e590 forwards the palette POINTER unchanged; the mistake is a slot lookup.
case("BlitFlip-0x9e590-raw-palette", T, C,
     "    SoftSurf_BlitFlipEx(g_pMainSurface, x, y, pGlyph, pPal, pMask);",
     "    SoftSurf_BlitFlipEx(g_pMainSurface, x, y, pGlyph, g_aPalette[7], pMask);")

# SoftSurf_BlitIndexed @ 0x9e9f4 keeps x and y in their own positions.
case("BlitIndexed-0x9e9f4-xy-order", T, C,
     "    SoftSurf_BlitIndexedEx(g_pMainSurface, x, y, pGlyph, nPalSlot);",
     "    SoftSurf_BlitIndexedEx(g_pMainSurface, y, x, pGlyph, nPalSlot);")

# SoftSurf_BlitIndexedKeyed @ 0x9ea34 forwards the key it was given.
case("BlitIndexedKeyed-0x9ea34-key-arg", T, C,
     "    SoftSurf_BlitIndexedKeyedEx(g_pMainSurface, x, y, pGlyph, nPalSlot, nKey);",
     "    SoftSurf_BlitIndexedKeyedEx(g_pMainSurface, x, y, pGlyph, nPalSlot, 0);")

# SoftSurf_BlitRect @ 0x9ebb0 truncates the three colour operands to bytes (LDRB).
case("BlitRect-0x9ebb0-truncation", T, C,
     "    SoftSurf_BlitRectEx(g_pMainSurface, x, y, nRectW, nRectH, nIdx & 0xFFu,\n"
     "                        nColorMid & 0xFF, nColorLow & 0xFF);",
     "    SoftSurf_BlitRectEx(g_pMainSurface, x, y, nRectW, nRectH, nIdx,\n"
     "                        nColorMid, nColorLow);")

# SoftSurf_BlitSrcRect @ 0x9ebf8 maps srcX onto the middle operand and srcY onto the low one.
case("BlitSrcRect-0x9ebf8-operand-map", T, C,
     "    SoftSurf_BlitSrcRectEx(g_pMainSurface, x, y, nRectW, nRectH, nIdx,\n"
     "                           nColorMid, nColorLow, nKey);",
     "    SoftSurf_BlitSrcRectEx(g_pMainSurface, x, y, nRectW, nRectH, nIdx,\n"
     "                           nColorLow, nColorMid, nKey);")

# SoftSurf_ctor @ 0x9f96c stores two pointers and reaches no third field.
case("ctor-0x9f96c-third-field", T, C,
     "void SoftSurf_ctor(SoftSurf *pSurf)\n{\n    pSurf->pPixels = NULL;",
     "void SoftSurf_ctor(SoftSurf *pSurf)\n{\n    pSurf->nWidth  = 0;\n    pSurf->pPixels = NULL;")

# SoftSurf_dtor @ 0x9f97c frees through pPixels; the port also clears both (stated deviation), which is
# what a second dtor pins. Dropping the clear makes the second run a second free.
case("dtor-0x9f97c-clears-both", T, C,
     "    if (pSurf->pPixels != NULL) {\n        free(pSurf->pAlloc);\n    }\n"
     "    pSurf->pPixels = NULL;\n    pSurf->pAlloc  = NULL;",
     "    if (pSurf->pPixels != NULL) {\n        free(pSurf->pAlloc);\n    }")

# SoftSurf_GetWidth @ 0x9f994 reads +0x00, not the +0x18 blit descriptor.
case("GetWidth-0x9f994-field", T, C,
     "    return pSurf->nWidth;\n}",
     "    return pSurf->nBlitW;\n}")

# SoftSurf_GetHeight @ 0x9f99c reads +0x04, not +0x1C.
case("GetHeight-0x9f99c-field", T, C,
     "    return pSurf->nHeight;\n}",
     "    return pSurf->nBlitH;\n}")

# SoftSurf_GetPixels @ 0x9fcb4 reads +0x14, not the +0x10 allocation base or the +0x24 blit source.
case("GetPixels-0x9fcb4-field", T, C,
     "u8 *SoftSurf_GetPixels(const SoftSurf *pSurf)\n{\n    return pSurf->pPixels;",
     "u8 *SoftSurf_GetPixels(const SoftSurf *pSurf)\n{\n    return pSurf->pAlloc;")

# Render_BlitSoft_LoRes @ 0x9fa7c tails into the 1:1 copy.
case("LoRes-0x9fa7c-tail-call", T, C,
     "    Blit_LoadSurface(pSurf);\n    Blit_CopyRows();\n}",
     "    Blit_LoadSurface(pSurf);\n    Blit_CopyRows_HiRes();\n}")

# Render_BlitSoft_HiRes @ 0x9fa90 tails into the 2x2 upscale.
case("HiRes-0x9fa90-tail-call", T, C,
     "    Blit_CopyRows_HiRes();",
     "    Blit_CopyRows();")

# SoftSurf_BlitIndexedEx @ 0x9fadc loads the surface it is given before the inner runs.
case("BlitIndexedEx-0x9fadc-loads-surface", T, C,
     "    Blit_LoadSurface(pSurf);\n"
     "    Blit_Indexed(x, y, pGlyph, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)], false, 0);",
     "    Blit_Indexed(x, y, pGlyph, g_aPalette[nPalSlot & (PUFO_PAL_SLOTS - 1)], false, 0);")

# SoftSurf_BlitIndexedKeyedEx @ 0x9fb0c publishes the key through blitColorKey before the inner runs.
case("BlitIndexedKeyedEx-0x9fb0c-publishes-key", T, C,
     "    Blit_SetColorKey(nKey);\n    Blit_Indexed(",
     "    Blit_Indexed(")

# SoftSurf_BlitEx @ 0x9fb44 publishes the argument as the alpha (dword_A3040), not as the mask.
case("BlitEx-0x9fb44-publishes-alpha", T, C,
     "    BlitParams_Set(nAlpha, 0);",
     "    BlitParams_Set(0, nAlpha);")

# SoftSurf_BlitFlipEx @ 0x9fb80 hands the inner the caller's palette pointer.
case("BlitFlipEx-0x9fb80-raw-pointer", T, C,
     "    Blit_FlipBlend(x, y, pGlyph, pPal, pMask);",
     "    Blit_FlipBlend(x, y, pGlyph, g_aPalette[7], pMask);")

# SoftSurf_FillRectEx @ 0x9fbbc publishes the size it was given (both colour operands zero).
case("FillRectEx-0x9fbbc-published-size", T, C,
     "    Blit_SetRectAndColor(nRectW, nRectH, 0, 0);",
     "    Blit_SetRectAndColor(nRectW - 1, nRectH, 0, 0);")

# SoftSurf_BlitGlyph @ 0x9fc00 is the pair Font_DrawGlyph inlines: load g_pMainSurface, then blit.
case("BlitGlyph-0x9fc00-loads-main-surface", TF, F,
     "    Blit_LoadSurface(g_pMainSurface);\n",
     "")

# SoftSurf_BlitRectEx @ 0x9fc30 publishes the two colour operands before the fill.
case("BlitRectEx-0x9fc30-operands", T, C,
     "    Blit_SetRectAndColor(nRectW, nRectH, nColorMid, nColorLow);\n    Blit_CopyToXY(x, y, nIdx);",
     "    Blit_SetRectAndColor(nRectW, nRectH, nColorLow, nColorMid);\n    Blit_CopyToXY(x, y, nIdx);")

# SoftSurf_BlitSrcRectEx @ 0x9fc70 passes the key through to the blending inner.
case("BlitSrcRectEx-0x9fc70-key-through", T, C,
     "    SoftSurf_BlitSrcRect_Inner(x, y, nIdx, nKey);",
     "    SoftSurf_BlitSrcRect_Inner(x, y, nIdx, 255);")

# Blit_SetColorKey @ 0xa3624 returns the key it stored.
case("SetColorKey-0xa3624-returns-key", T, C,
     "    s_nColorKey = nKey;\n    return nKey;",
     "    s_nColorKey = nKey;\n    return 0;")

# SoftSurf_BlitIndexedKeyed_Inner @ 0xa3638 has no mode > 2 arm at all: it computes the row pointer
# and leaves through the bare POP at 0xA3960. Moving the gate one up lets the 24bpp case draw.
case("KeyedInner-0xa3638-mode-gate", T, C,
     "    if (bKeyed && g_blitState.nSurf2 > 2) {",
     "    if (bKeyed && g_blitState.nSurf2 > 3) {")

# Blit_LoadSurface @ 0xa3964 copies the +0x24 blit source, not the surface's own pixel base.
case("LoadSurface-0xa3964-descriptor-src", T, C,
     "    g_blitState.pSrc   = pSurf->pBlitSrc;",
     "    g_blitState.pSrc   = pSurf->pPixels;")

# Blit_CopyRows_HiRes @ 0xa3bb8, mode > 2 arm: all three sub-writes read the SAME byte [R9]
# (0xA3D44, 0xA3D60, 0xA3D7C) and R9 only advances at 0xA3D98.
case("CopyRowsHiRes-0xa3bb8-same-byte", T, C,
     "                    Blit_PutHiRes16(p + nPitchX, pSrc[0]);",
     "                    Blit_PutHiRes16(p + nPitchX, pSrc[1]);")

# Blit_SetRectAndColor @ 0xa3f5c: the two colour operands land in their own globals.
case("SetRectAndColor-0xa3f5c-operands", T, C,
     "    s_nColorMid = nColorMid;\n    s_nColorLow = nColorLow;",
     "    s_nColorMid = nColorLow;\n    s_nColorLow = nColorMid;")

# BlitParams_Set @ 0xa47b0: alpha is dword_A3040 and the mask pointer is dword_A303C.
case("ParamsSet-0xa47b0-alpha-vs-mask", T, C,
     "    s_nBlendAlpha = nAlpha;\n    s_nMaskPtr    = nMaskPtr;",
     "    s_nBlendAlpha = nMaskPtr;\n    s_nMaskPtr    = nAlpha;")

# SoftSurf_Blit_Inner @ 0xa47d0 sums six field terms into one accumulator: dropping the destination's
# blue term is what a per-field rewrite would do.
case("BlitInner-0xa47d0-dst-blue-term", T, C,
     "            u32 nAcc = ((nDst & 0x001Fu) * nInv) >> 8;",
     "            u32 nAcc = 0;")

# SoftSurf_BlitFlip_Inner @ 0xa49a0 weights the destination with 256-mask, not 255-mask.
case("FlipInner-0xa49a0-256-weight", T, C,
     "            u32 nInv  = 256u - nMask;",
     "            u32 nInv  = 255u - nMask;")


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    skipped = 0
    inert = 0
    total_checks = 0
    for name, target, path, old, new, show in CASES:
        if only and only not in name:
            continue
        r = run_case(name, target, path, old, new, show)
        if r is None:
            skipped += 1
        elif len(r) == 0:
            inert += 1
        else:
            total_checks += len(r)
    print("cases:", len(CASES), "skipped:", skipped, "inert:", inert,
          "failing checks in total:", total_checks)


if __name__ == "__main__":
    main()
