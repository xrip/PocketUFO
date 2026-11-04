/* pufo_mainmenu.c - see pufo_mainmenu.h for evidence and the layout table. */
#include "game/pufo_mainmenu.h"

#include "game/pufo_font.h"     /* Text_DrawString / Text_MeasureWidth */
#include "render/pufo_render.h" /* SoftSurf_BlitIndexed / SoftSurf_BlitSrcRect */
#include "game/pufo_timer.h"    /* Timer_StartB, the timers GameUI_InitStartup re-arms */
#include "asset/pufo_asset.h"   /* AssetReader, the startg/emptyb pad lookup */
#include "audio/pufo_audio.h"   /* Sound_InitAndLoadSamples / LoadAndPlayMusic */
#include "util/pufo_rand.h"     /* Game_RandU16, the row-key flicker rolls */

#include <stdlib.h>  /* malloc, the two pad blobs */
#include <string.h>  /* memset */

/* The version stamp, ::psz @ 0xAAD28. */
static const char s_szVersion[] = "V1.26";

/* The row text colour: the cyan pair scaled by key/256. The original runs itod / muld / dtoi per
 * channel; the six constants are the exact binary fractions below, so the products are exact and
 * dtoi is C's truncation. */
static void PackRowColor(const GfxColorEnv *pColor, i32 nKey)
{
    (void)Gfx_PackRgb565Pair(pColor,
                             (i32)((double)nKey * (26.0  / 256.0)),
                             (i32)((double)nKey * (244.0 / 256.0)),
                             (i32)((double)nKey * (252.0 / 256.0)),
                             (i32)((double)nKey * (13.0  / 256.0)),
                             (i32)((double)nKey * (122.0 / 256.0)),
                             (i32)((double)nKey * (126.0 / 256.0)));
}

/* One row frame: body at (x, y, w, 18) with index 0 and colours (87, 94), then the four 1-pixel
 * strips at index 1 with colours (159, 171) — top at y-1, bottom at y+18, left at x-1, right at
 * x+w. The label is centred as (w - Text_MeasureWidth) / 2 + x, one pixel below the body top. The
 * divide is the compiler's signed divide-by-two (RSB / CMP #0 / ADMI #1 / ASR #1 at 0x1DEE8),
 * which truncates toward zero exactly like C's. */
static void DrawMenuRow(const MainMenuEnv *pEnv, i32 nKey, i32 x, i32 y, i32 nWidth,
                        const char *pszLabel)
{
    i32 nTextW;

    SoftSurf_BlitSrcRect(x, y, nWidth, 18, 0, 87, 94, nKey);
    SoftSurf_BlitSrcRect(x, y - 1, nWidth, 1, 1, 159, 171, nKey);
    SoftSurf_BlitSrcRect(x, y + 18, nWidth, 1, 1, 159, 171, nKey);
    SoftSurf_BlitSrcRect(x - 1, y, 1, 18, 1, 159, 171, nKey);
    SoftSurf_BlitSrcRect(x + nWidth, y, 1, 18, 1, 159, 171, nKey);

    PackRowColor(pEnv->pColor, nKey);
    nTextW = Text_MeasureWidth(pszLabel);
    Text_DrawString((nWidth - nTextW) / 2 + x, y + 1, pszLabel);
}

static i32 RowKey(const MainMenuEnv *pEnv, i32 nRow)
{
    if (pEnv->abRowDisabled[nRow] != 0) {
        return 255;
    }
    return pEnv->anRowKeys[nRow];
}

void MainMenu_Draw(const MainMenuEnv *pEnv)
{
    i32 i;

    SoftSurf_BlitIndexed(0, 0, pEnv->pBgGlyph, 20);

    if (pEnv->nListMode == PUFO_MAINMENU_MODE_MAIN ||
        pEnv->nListMode == PUFO_MAINMENU_MODE_SLOTCLICK) {
        const i32 nRecBase = (pEnv->nListMode == PUFO_MAINMENU_MODE_MAIN)
                                 ? PUFO_MAINMENU_REC_MENU0
                                 : PUFO_MAINMENU_REC_MENU1;

        for (i = 0; i < PUFO_MAINMENU_ROW_COUNT; ++i) {
            DrawMenuRow(pEnv, RowKey(pEnv, i), 50, 115 + PUFO_MAINMENU_ROW_STEP * i, 140,
                        (const char *)(pEnv->pTextRecs
                                       + PUFO_TEXT_REC_STRIDE * (nRecBase + i)));
        }
    } else if (pEnv->nListMode == PUFO_MAINMENU_MODE_LOADLIST ||
               pEnv->nListMode == PUFO_MAINMENU_MODE_LOADING) {
        const char *pszCtlLabel;

        for (i = 0; i < PUFO_MAINMENU_SLOT_COUNT; ++i) {
            DrawMenuRow(pEnv, RowKey(pEnv, i), 25, 65 + PUFO_MAINMENU_ROW_STEP * i, 190,
                        pEnv->pSlotLabels + PUFO_MAINMENU_SLOT_LABEL_LEN * i);
        }

        (void)UiControl_BlitActivePad(pEnv->pStartCtl, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlUser);

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 26, 244, 252, 0, 122, 126);
        pszCtlLabel = (const char *)(pEnv->pTextRecs
                                     + PUFO_TEXT_REC_STRIDE * PUFO_MAINMENU_REC_CTL);
        Text_DrawString(pEnv->pStartCtl->x
                            + ((i32)pEnv->pStartCtl->uWidth - Text_MeasureWidth(pszCtlLabel)) / 2,
                        pEnv->pStartCtl->y + 1, pszCtlLabel);

        if (pEnv->nListMode == PUFO_MAINMENU_MODE_LOADING) {
            UiModalLayout layout;

            *pEnv->pnActionPhaseSave112 = 40;
            if (pEnv->pModalAnim != NULL) {
                pEnv->pModalAnim->nCursor = *pEnv->pnActionPhaseSave112;
            }
            (void)UiModal_DrawDialog(pEnv->pModalAnim, pEnv->pTextRecs, 150,
                                     PUFO_MAINMENU_REC_MODAL, 1, NULL, NULL,
                                     pEnv->pModalLayout != NULL ? pEnv->pModalLayout : &layout);
            if (pEnv->pModalAnim != NULL) {
                *pEnv->pnActionPhaseSave112 = pEnv->pModalAnim->nCursor;
            }
        }
    }

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 255, 255, 255, 90, 90, 90);
    Text_DrawString(5, 300, s_szVersion);
}

/* a2s @ 0xA7CF8: the 15 start-menu control descriptors, 32 bytes each in the image. xOff is 50
 * for all of them; yOff is 115/137/159/181 for the four main rows (repeated for controls 4..7,
 * which the original never shows because only 0..3 and 8..14 get handlers) and 65..197 step 22
 * for the seven slot rows. Every pad name field is zero -- these are click zones -- and every
 * mode is 2. */
static const struct {
    i16 xOff;
    i16 yOff;
    u8  nMode;
} s_aMenuCtlDesc[15] = {
    { 50, 115, 2 }, { 50, 137, 2 }, { 50, 159, 2 }, { 50, 181, 2 },
    { 50, 115, 2 }, { 50, 137, 2 }, { 50, 159, 2 }, { 50, 181, 2 },
    { 50,  65, 2 }, { 50,  87, 2 }, { 50, 109, 2 }, { 50, 131, 2 },
    { 50, 153, 2 }, { 50, 175, 2 }, { 50, 197, 2 }
};

void GameUI_InitStartup(GameUiStartupEnv *pEnv)
{
    AssetReader rdr;
    UiControlDesc desc;
    u32 cb;

    AssetReader_Clear(&rdr); /* 0x38A34 */
    PufoFont_Init(pEnv->pFont); /* 0x38A40 */

    pEnv->paMenuControls[0].pfnClick  = pEnv->apfnMenuClick[0];  /* 0x38A58, unk_AD4FC */
    pEnv->paMenuControls[1].pfnClick  = pEnv->apfnMenuClick[1];  /* 0x38A60 */
    pEnv->paMenuControls[2].pfnClick  = pEnv->apfnMenuClick[2];  /* 0x38A68 */
    pEnv->paMenuControls[3].pfnClick  = pEnv->apfnMenuClick[3];  /* 0x38A70 */
    pEnv->paMenuControls[8].pfnClick  = pEnv->apfnMenuClick[8];  /* 0x38A78, unk_AD5FC */
    pEnv->paMenuControls[9].pfnClick  = pEnv->apfnMenuClick[9];  /* 0x38A80 */
    pEnv->paMenuControls[10].pfnClick = pEnv->apfnMenuClick[10]; /* 0x38A88 */
    pEnv->paMenuControls[11].pfnClick = pEnv->apfnMenuClick[11]; /* 0x38A90 */
    pEnv->paMenuControls[12].pfnClick = pEnv->apfnMenuClick[12]; /* 0x38A98 */
    pEnv->paMenuControls[13].pfnClick = pEnv->apfnMenuClick[13]; /* 0x38AA0 */
    pEnv->paMenuControls[14].pfnClick = pEnv->apfnMenuClick[14]; /* 0x38AA8 */

    for (i32 i = 0; i < 15; ++i) { /* 0x38AB0..0x38AE4 */
        UiControlDesc d;
        d.xOff   = s_aMenuCtlDesc[i].xOff;
        d.yOff   = s_aMenuCtlDesc[i].yOff;
        d.szPad0 = ""; /* the descriptor pad-name fields are all zero in the image */
        d.szPad1 = "";
        d.szPad2 = "";
        d.nMode  = s_aMenuCtlDesc[i].nMode;
        UiControl_Setup(&pEnv->paMenuControls[i], 0, 0, &d,
                        pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x38AC4 */
        pEnv->paMenuControls[i].uWidth  = 140; /* 0x38ACC */
        pEnv->paMenuControls[i].uHeight = 18;  /* 0x38AD4 */
    }

    Palette_Load("startg.pal", 0x14u); /* 0x38AF0 */
    Palette_Load("menu.pal", 1u);      /* 0x38AFC */

    (void)AssetReader_Find(&rdr, "startg.pad"); /* 0x38B08 */
    cb = AssetReader_Size(&rdr); /* 0x38B10 */
    *pEnv->ppBgGlyph = (u8 *)malloc(cb); /* 0x38B1C */
    if (*pEnv->ppBgGlyph != NULL) {
        cb = AssetReader_Size(&rdr); /* 0x38B24: the size is re-read, not kept */
        AssetReader_Read(&rdr, *pEnv->ppBgGlyph, cb); /* 0x38B34 */
    }

    (void)AssetReader_Find(&rdr, "emptyb.pad"); /* 0x38B40 */
    cb = AssetReader_Size(&rdr); /* 0x38B48 */
    *pEnv->ppEmptyBtnPad = (u8 *)malloc(cb); /* 0x38B54 */
    if (*pEnv->ppEmptyBtnPad != NULL) {
        cb = AssetReader_Size(&rdr); /* 0x38B5C */
        AssetReader_Read(&rdr, *pEnv->ppEmptyBtnPad, cb); /* 0x38B6C */
    }

    pEnv->pFont->nGlyphBase = pEnv->pFont->nTotalSprFrames; /* 0x38B78, before the font loads */
    (void)Font_LoadSpr(pEnv->pFont, "font"); /* 0x38B80 */
    Font_BuildIndex(pEnv->pFont); /* 0x38B88 */

    for (i32 i = 0; i < 7; ++i) {
        pEnv->abRowDisabled[i] = 0; /* 0x38B98..0x38BB4 */
        pEnv->anRowKeySrc[i]   = 0; /* 0x38BB8..0x38BD4, unk_1571C8..0x1571E0 */
        pEnv->anRowKeys[i]     = 0; /* 0x38BD8..0x38BF0 */
    }

    memset(&desc, 0, sizeof(desc)); /* deviation: the original leaves nMode as stack residue */
    desc.xOff   = 'V';              /* 0x38BF8: strcpy(&pDesc,"V") lands in the first word */
    desc.yOff   = 230;              /* 0x38C00 */
    desc.szPad0 = "emptyb1";        /* 0x38C0C */
    desc.szPad1 = "cemptyb1";       /* 0x38C5C */
    desc.szPad2 = "";               /* 0x38C74: the empty string at 0x10DA38 */

    UiControl_ctor(pEnv->pStartCtl); /* 0x38C88..0x38D1C: operator new(0x20) + ctor */
    UiControl_Setup(pEnv->pStartCtl, 0, 0, &desc,
                    pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x38D20 */
    pEnv->pStartCtl->pfnClick = pEnv->pfnEmptyClick; /* 0x38D30: EmptyBtn_OnClick */

    *pEnv->pnListMode = 0;  /* 0x38D38 */
    *pEnv->pnUiScreen = 17; /* 0x38D40: Render_Frame's MainMenu_Draw branch */

    (void)Sound_InitAndLoadSamples("tact"); /* 0x38D48 */
    Sound_LoadAndPlayMusic(0); /* 0x38D50 */
    Sound_ApplyMusicVolume(Options_MusicLevel(pEnv->pOptions), pEnv->pOptions->bMusicOff); /* 0x38D54 */
    Sound_ApplySfxVolume(Options_SfxLevel(pEnv->pOptions), pEnv->pOptions->bSoundOff);     /* 0x38D58 */
    Timer_StartB(); /* 0x38D64: StartTimers_B */
}

void MainMenu_TickRowKeys(i32 *anRowKeys, i32 *anRowKeySrc, i32 nAnimPhase, i32 *pnAccum)
{
    for (i32 i = 0; i < 7; ++i) { /* 0xA0AD0 */
        if (anRowKeySrc[i] != 0) { /* 0xA0AE4 */
            i32 nVal = nAnimPhase;                /* 0xA0AE8 */
            *pnAccum = nAnimPhase;                /* 0xA0AEC: re-seeded every row */
            if (nAnimPhase < 4) {                 /* 0xA0AF4 */
                nVal = *pnAccum + (i32)Game_RandU16((u16)(8 - nAnimPhase)); /* 0xA0B10 */
                *pnAccum = nVal;                  /* 0xA0B14 */
            }
            nVal = 32 * nVal - 1;                 /* 0xA0B1C */
            anRowKeys[i] = nVal;                  /* 0xA0B20 */
            if (nVal < 0) {
                anRowKeys[i] = 0;                 /* 0xA0B28 */
            }
        } else {
            anRowKeys[i] = 200;                   /* 0xA0B54 */
        }

        if (Game_RandU16(0x190u) != 0) {          /* 0xA0B60 */
            if (anRowKeySrc[i] != 0 && Game_RandU16(0x14u) == 0) {
                anRowKeySrc[i] = 0;               /* 0xA0BB8 */
            }
        } else {
            for (i32 k = 0; k < 7; ++k) {         /* 0xA0B74..0xA0B8C */
                anRowKeySrc[k] = 0;
            }
            anRowKeySrc[i] = 1;                   /* 0xA0B94 */
        }
    }
}
