/* test_cmdpads.c - Tact_DrawCommandPads @ 0x1fc8c, the screen-4 status panel.
 *
 * The decisions worth pinning are the unit-type gate (craft family 4..8 / 14..20 / 22 skips the
 * pad slots), the uMissions <= 3 empty-slot frames, the class -> cs pad pair and the origin
 * rewriting, the bar widths (max >> 1, remainder (max >> 1) - (cur >> 1) - 2, negative dropped),
 * the kneel tick count, the morale bar, the bUnk422 rows, the ruler that scribbles over the label
 * rows, and the control set (widget 101 always, 102/103 only while bLookCommitFlag is zero).
 *
 * Ink is the observable for the rects; the cs pads ink one pixel at their origin plus (1,1), so a
 * slot is proven by ink at (6,3) / (40,3) - clear of the panel frame - and by the pad's own origin
 * fields. The label and value strings are not ink-checked: no font is loaded here and
 * Text_DrawString is a no-op without one; the bars pin the same numbers. */
#include <stdio.h>
#include <string.h>

#include "game/pufo_isodraw.h"
#include "game/pufo_unit.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

#define CMD_REC_COUNT 67 /* records 56..66 are addressed */

static SoftSurf  g_surf;
static IsoDrawCtx g_iso;
static u8        g_aTextRecs[CMD_REC_COUNT * PUFO_TEXT_REC_STRIDE];
static Soldier   g_unit;
static PadImage  g_soldierPad;
static PadImage  g_csPad[3];
static u8        g_aSoldierBlob[5];
static u8        g_aCsBlob[3][8];
static UiControl g_bank[PUFO_PBANK_WIDGET_COUNT];
static PadImage  g_bankPad;
static u8        g_aBankBlob[5];
static char      g_rankLow[20];
static char      g_rankOther[20];
static GfxColorEnv g_color;
static TactCmdPadEnv g_env;
static i32       g_nCtlBlits;

static void CtlHook_BlitPad(const PadImage *pPad, void *pUser)
{
    (void)pPad; (void)pUser;
    ++g_nCtlBlits;
}

/* A 2x2 pad blob whose only inked pixel is the last one, at (1,1), so the blit lands at
 * origin + (1,1): (6,3) for slot 0 and (40,3) for slot 1, both clear of the panel frame. */
static void MakeCsBlob(u8 *pBlob)
{
    pBlob[0] = 2; pBlob[1] = 0; /* uWidth  */
    pBlob[2] = 2; pBlob[3] = 0; /* uHeight */
    pBlob[4] = 0x00;
    pBlob[5] = 0x00;
    pBlob[6] = 0x00;
    pBlob[7] = 0x30; /* inks at (1,1): palette slot 1, entry 0x30 */
}

static void Fixture_Reset(void)
{
    i32 i;

    memset(g_aTextRecs, 0, sizeof(g_aTextRecs));
    memset(&g_unit, 0, sizeof(g_unit));
    memset(&g_color, 0, sizeof(g_color));
    memset(&g_env, 0, sizeof(g_env));
    memset(g_bank, 0, sizeof(g_bank));

    g_nCtlBlits = 0;

    /* Palette slot 1, entry 0x30: the pad ink. Entry 0 stays zero so the other blob pixels
     * draw nothing. */
    memset(g_aPalette, 0, sizeof(g_aPalette));
    g_aPalette[1][0x30] = 0xFFFFu;

    for (i = 56; i <= 66; ++i) {
        (void)snprintf((char *)(g_aTextRecs + PUFO_TEXT_REC_STRIDE * i), 205, "L%d", i);
    }

    (void)snprintf(g_rankLow, sizeof(g_rankLow), "RANK");
    (void)snprintf(g_rankOther, sizeof(g_rankOther), "ARANK");
    (void)snprintf(g_unit.szName, sizeof(g_unit.szName), "Bob");

    g_unit.nUnitType     = 0;
    g_unit.uMissions     = 5;
    g_unit.nClass        = 3;
    g_unit.uTu           = 20;
    g_unit.auStat[0]     = 40;
    g_unit.uStunOrArmor  = 10;
    g_unit.auStat[9]     = 30;
    g_unit.uHp           = 15;
    g_unit.auStat[1]     = 30;
    g_unit.bKneelOrPrimed = 3;
    g_unit.auStat[2]     = 20;
    g_unit.bMorale       = 50;
    g_unit.auStat[4]     = 10;
    g_unit.auStat[5]     = 12;
    g_unit.auStat[8]     = 14;
    g_unit.bUnk422       = 1;
    g_unit.auStat[7]     = 8;
    g_unit.auStat[6]     = 6;

    /* The soldier backdrop pad: 1x1, pixel 0 (palette entry 0) so it never inks. */
    g_aSoldierBlob[0] = 1; g_aSoldierBlob[1] = 0;
    g_aSoldierBlob[2] = 1; g_aSoldierBlob[3] = 0;
    g_aSoldierBlob[4] = 0x00;
    PadImage_ctor(&g_soldierPad);
    g_soldierPad.pData   = g_aSoldierBlob;
    g_soldierPad.uWidth  = 1;
    g_soldierPad.uHeight = 1;
    g_soldierPad.nMode   = 1;

    for (i = 0; i < 3; ++i) {
        MakeCsBlob(g_aCsBlob[i]);
        PadImage_ctor(&g_csPad[i]); /* only nulls pData - the origins are zeroed here */
        g_csPad[i].pData   = g_aCsBlob[i];
        g_csPad[i].uWidth  = 2;
        g_csPad[i].uHeight = 2;
        g_csPad[i].nMode   = 1;
        g_csPad[i].nOriginX = 0;
        g_csPad[i].nOriginY = 0;
    }

    /* The three panel controls, made drawable: mode 0, state 1, a key-0xFF pad in slot 1. */
    g_aBankBlob[0] = 1; g_aBankBlob[1] = 0;
    g_aBankBlob[2] = 1; g_aBankBlob[3] = 0;
    g_aBankBlob[4] = 0xFF;
    PadImage_ctor(&g_bankPad);
    g_bankPad.pData   = g_aBankBlob;
    g_bankPad.uWidth  = 1;
    g_bankPad.uHeight = 1;
    for (i = PUFO_PBANK_CMD_FIRST; i < PUFO_PBANK_CMD_FIRST + PUFO_PBANK_CMD_COUNT; ++i) {
        g_bank[i].bEnabled  = 1;
        g_bank[i].nMode     = 0;
        g_bank[i].nState    = 1;
        g_bank[i].apPads[1] = &g_bankPad;
    }

    g_env.pSelUnit      = &g_unit;
    g_env.pSoldierPad   = &g_soldierPad;
    g_env.apCsPad[0]    = &g_csPad[0];
    g_env.apCsPad[1]    = &g_csPad[1];
    g_env.apCsPad[2]    = &g_csPad[2];
    g_env.pTextRecs     = g_aTextRecs;
    g_env.panRankLow    = g_rankLow;
    g_env.panRankOther  = g_rankOther;
    g_env.bLookCommitFlag = 1;
    g_env.pBank         = g_bank;
    g_env.pColor        = &g_color;
    g_env.pfnCtlBlit    = CtlHook_BlitPad;
    g_env.pfnCtlBlitEx  = CtlHook_BlitPad;

    memset(g_surf.pPixels, 0, (size_t)g_surf.nWidth * (size_t)g_surf.nHeight * 2u);
}

static i32 InkAt(i32 x, i32 y)
{
    const u16 *p = (const u16 *)g_surf.pPixels;

    return (i32)p[y * g_surf.nWidth + x];
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    SoftSurf_ctor(&g_surf);
    SoftSurf_AllocPixels(&g_surf, 240, 320, PIXMODE_16BPP);
    g_pMainSurface = &g_surf;

    /* Blit_CopyToXY (what SoftSurf_BlitRectProxy ends in) runs only for pixel mode 1 or 2, which
     * the render target supplies - the app shell sets this at GX init. */
    Render_SetTarget(240, 320, 2, 2, 640, g_surf.pPixels);

    printf("Tact_DrawCommandPads @ 0x1fc8c\n");

    /* --- uMissions <= 3: two grey 32x32 frames with a black 30x30 inside --- */
    Fixture_Reset();
    g_unit.uMissions = 3;
    Tact_DrawCommandPads(&g_iso, &g_env);
    Check(InkAt(5, 2) != 0, "slot 0 frame inked at (5, 2)");
    Check(InkAt(36, 33) != 0, "slot 0 frame reaches (36, 33)");
    Check(InkAt(68, 33) != 0, "slot 1 frame reaches (68, 33)");
    CheckEqI32(InkAt(6, 3), 0, "the 30x30 inside writes zero");
    CheckEqI32(g_csPad[0].nOriginX, 0, "and no cs pad is touched");

    /* --- class 3: cs0 at slot 0, cs1 at slot 1, origins rewritten --- */
    Fixture_Reset();
    Tact_DrawCommandPads(&g_iso, &g_env);
    Check(InkAt(6, 3) != 0, "cs0 blitted at slot 0 (inks 5+1, 2+1)");
    Check(InkAt(40, 3) != 0, "cs1 blitted at slot 1 (inks 39+1, 2+1)");
    CheckEqI32(g_csPad[0].nOriginX, 5, "cs0 origin x left at 5");
    CheckEqI32(g_csPad[1].nOriginX, 39, "cs1 origin x left at 39");
    CheckEqI32(g_csPad[2].nOriginX, 0, "cs2 untouched");
    CheckEqI32(InkAt(36, 33), 0, "no empty-slot frames in the class path");

    /* --- class 0: the same pad in both slots, drawn twice --- */
    Fixture_Reset();
    g_unit.nClass = 0;
    Tact_DrawCommandPads(&g_iso, &g_env);
    Check(InkAt(6, 3) != 0, "cs0 drawn at slot 0");
    Check(InkAt(40, 3) != 0, "and again at slot 1");
    CheckEqI32(g_csPad[0].nOriginX, 39, "its origin ends at 39");

    /* --- the craft family skips the slots entirely --- */
    Fixture_Reset();
    g_unit.nUnitType = 5;
    Tact_DrawCommandPads(&g_iso, &g_env);
    CheckEqI32(InkAt(6, 3), 0, "type 5 draws no pad at slot 0");
    CheckEqI32(InkAt(36, 33), 0, "and no empty frame");
    CheckEqI32(g_csPad[0].nOriginX, 0, "the pads stay untouched");

    /* --- bars: max >> 1 wide, remainder (max >> 1) - (cur >> 1) - 2 --- */
    Fixture_Reset();
    Tact_DrawCommandPads(&g_iso, &g_env);
    Check(InkAt(29, 61) != 0, "TU bar 40 >> 1 = 20 wide ends at (29, 61)");
    CheckEqI32(InkAt(30, 61), 0, "and stops there");
    Check(InkAt(21, 62) != 0, "TU remainder starts at (20 >> 1) + 11");
    Check(InkAt(28, 62) != 0, "and is 20 - 10 - 2 = 8 wide");
    Check(InkAt(24, 85) != 0, "stun bar 30 >> 1 = 15 wide");
    Check(InkAt(24, 109) != 0, "HP bar 30 >> 1 = 15 wide");
    Check(InkAt(19, 157) != 0, "auStat[2] bar 20 >> 1 = 10 wide");

    /* --- a full-value unit leaves a negative remainder, which the bounds check drops --- */
    Fixture_Reset();
    g_unit.uTu = 40;
    Tact_DrawCommandPads(&g_iso, &g_env);
    CheckEqI32(InkAt(31, 62), 0, "remainder width 20 - 20 - 2 = -2 draws nothing");

    /* --- kneel blocks: one 12x6 per point at y 133 --- */
    Check(InkAt(38, 133) != 0, "block 2 at 14 * 2 + 10 = 38");
    CheckEqI32(InkAt(52, 133), 0, "and only 3 blocks for bKneelOrPrimed = 3");

    /* --- morale: fixed 200-wide bar, remainder 2 * (99 - morale) --- */
    Check(InkAt(209, 181) != 0, "morale bar spans to (209, 181)");
    Check(InkAt(111, 182) != 0, "remainder starts at 2 * 50 + 11");
    Check(InkAt(208, 182) != 0, "and is 2 * 49 = 98 wide");
    CheckEqI32(InkAt(210, 182), 0, "stopping at (208, 182); the 6-tall full bar still covers (209, 182)");

    /* --- bUnk422 gates the last two rows --- */
    Check(InkAt(12, 277) != 0, "auStat[7] bar 8 >> 1 = 4 wide");
    Check(InkAt(11, 301) != 0, "auStat[6] bar 6 >> 1 = 3 wide");

    /* --- the ruler cuts through every label row (ORIGINAL BUG 0x20AD4) --- */
    Check(InkAt(5, 45) != 0, "left tick at (5, 45), index 5 * 20");
    Check(InkAt(120, 45) != 0, "last left tick at 5 * 24");
    Check(InkAt(230, 45) != 0, "right tick at 5 * 46 = (230, 45), index 250 - 115");
    Check(InkAt(5, 309) != 0, "row 11 at 24 * 11 + 45");
    CheckEqI32(InkAt(4, 45), 0, "nothing left of x 5");

    /* --- the panel frame --- */
    Check(InkAt(235, 150) != 0, "right frame column at x 235");
    CheckEqI32(InkAt(236, 150), 0, "and nothing outside it");

    /* --- controls: bLookCommitFlag gates widgets 102 and 103 --- */
    CheckEqI32(g_nCtlBlits, 1, "flag set: only widget 101 blits");
    Fixture_Reset();
    g_env.bLookCommitFlag = 0;
    Tact_DrawCommandPads(&g_iso, &g_env);
    CheckEqI32(g_nCtlBlits, 3, "flag clear: widgets 101, 102 and 103 blit");

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}

