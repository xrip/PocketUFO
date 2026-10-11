/* test_uibank.c - the widget bank and pad bank the HUD is built from.
 *
 * Evidence: Tact_InitUiControls @ 0x3b498, Tact_LoadUiPads @ 0x3b068.
 */
#include "pufo_test.h"
#include "ui/pufo_uibank.h"

#include <string.h>

static UiWidgetBank g_Bank;
static UiControl    g_Look;
static PadImage     g_HudPad;

/* One distinct stub per widget, so a wrong slot is visible. */
static void Stub0(void) {}
static void Stub1(void) {}
static void Stub2(void) {}
static void Stub3(void) {}
static void Stub4(void) {}
static void Stub5(void) {}
static void Stub6(void) {}
static void Stub7(void) {}
static void Stub8(void) {}
static void Stub9(void) {}
static void Stub10(void) {}
static void Stub11(void) {}
static void Stub12(void) {}
static void Stub13(void) {}
static void Stub14(void) {}
static void Stub15(void) {}
static void Stub16(void) {}
static void Stub17(void) {}
static void Stub18(void) {}
static void Stub19(void) {}
static void Stub20(void) {}
static void Stub21(void) {}
static void Stub22(void) {}
static void Stub23(void) {}
static void Stub24(void) {}
static void Stub25(void) {}
static void Stub26(void) {}
static void Stub27(void) {}
static void Stub28(void) {}
static void Stub29(void) {}
static void Stub30(void) {}
static void Stub31(void) {}
static void Stub32(void) {}
static void Stub33(void) {}
static void Stub34(void) {}
static void Stub35(void) {}
static void Stub36(void) {}
static void Stub37(void) {}
static void Stub38(void) {}
static void Stub39(void) {}
static void Stub40(void) {}
static void Stub41(void) {}
static void Stub42(void) {}
static void Stub43(void) {}
static void Stub44(void) {}
static void Stub45(void) {}
static void Stub46(void) {}
static void Stub47(void) {}
static void Stub48(void) {}
static void Stub49(void) {}
static void Stub50(void) {}
static void Stub51(void) {}
static void Stub52(void) {}
static void Stub53(void) {}
static void Stub54(void) {}
static void Stub55(void) {}
static void Stub56(void) {}
static void Stub57(void) {}
static void Stub58(void) {}
static void Stub59(void) {}
static void Stub60(void) {}
static void Stub61(void) {}
static void Stub62(void) {}
static void Stub63(void) {}
static void Stub64(void) {}
static void Stub65(void) {}
static void Stub66(void) {}
static void Stub67(void) {}
static void Stub68(void) {}
static void Stub69(void) {}
static void Stub70(void) {}
static void Stub71(void) {}
static void Stub72(void) {}
static void Stub73(void) {}
static void Stub74(void) {}
static void Stub75(void) {}
static void Stub76(void) {}
static void Stub77(void) {}
static void Stub78(void) {}
static void Stub79(void) {}
static void Stub80(void) {}
static void Stub81(void) {}
static void Stub82(void) {}
static void Stub83(void) {}
static void Stub84(void) {}
static void Stub85(void) {}
static void Stub86(void) {}
static void Stub87(void) {}
static void Stub88(void) {}
static void Stub89(void) {}
static void Stub90(void) {}
static void Stub91(void) {}
static void Stub92(void) {}
static void Stub93(void) {}
static void Stub94(void) {}
static void Stub95(void) {}
static void Stub96(void) {}
static void Stub97(void) {}
static void Stub98(void) {}
static void Stub99(void) {}
static void Stub100(void) {}
static void Stub101(void) {}
static void Stub102(void) {}
static void Stub103(void) {}
static void Stub104(void) {}
static void Stub105(void) {}
static void Stub106(void) {}
static void Stub107(void) {}
static void Stub108(void) {}
static void Stub109(void) {}
static void Stub110(void) {}
static void Stub111(void) {}
static void Stub112(void) {}
static void Stub113(void) {}
static void Stub114(void) {}
static void Stub115(void) {}
static void Stub116(void) {}
static void Stub117(void) {}
static void Stub118(void) {}
static void Stub119(void) {}
static void Stub120(void) {}
static void Stub121(void) {}
static void Stub122(void) {}
static void Stub123(void) {}
static void Stub124(void) {}
static void Stub125(void) {}
static void Stub126(void) {}
static void Stub127(void) {}
static void Stub128(void) {}
static void Stub129(void) {}

typedef void (*StubFn)(void);
static const StubFn g_aStub[PUFO_PBANK_WIDGET_COUNT] = {
    Stub0,   Stub1,   Stub2,   Stub3,   Stub4,   Stub5,   Stub6,   Stub7,
    Stub8,   Stub9,   Stub10,  Stub11,  Stub12,  Stub13,  Stub14,  Stub15,
    Stub16,  Stub17,  Stub18,  Stub19,  Stub20,  Stub21,  Stub22,  Stub23,
    Stub24,  Stub25,  Stub26,  Stub27,  Stub28,  Stub29,  Stub30,  Stub31,
    Stub32,  Stub33,  Stub34,  Stub35,  Stub36,  Stub37,  Stub38,  Stub39,
    Stub40,  Stub41,  Stub42,  Stub43,  Stub44,  Stub45,  Stub46,  Stub47,
    Stub48,  Stub49,  Stub50,  Stub51,  Stub52,  Stub53,  Stub54,  Stub55,
    Stub56,  Stub57,  Stub58,  Stub59,  Stub60,  Stub61,  Stub62,  Stub63,
    Stub64,  Stub65,  Stub66,  Stub67,  Stub68,  Stub69,  Stub70,  Stub71,
    Stub72,  Stub73,  Stub74,  Stub75,  Stub76,  Stub77,  Stub78,  Stub79,
    Stub80,  Stub81,  Stub82,  Stub83,  Stub84,  Stub85,  Stub86,  Stub87,
    Stub88,  Stub89,  Stub90,  Stub91,  Stub92,  Stub93,  Stub94,  Stub95,
    Stub96,  Stub97,  Stub98,  Stub99,  Stub100, Stub101, Stub102, Stub103,
    Stub104, Stub105, Stub106, Stub107, Stub108, Stub109, Stub110, Stub111,
    Stub112, Stub113, Stub114, Stub115, Stub116, Stub117, Stub118, Stub119,
    Stub120, Stub121, Stub122, Stub123, Stub124, Stub125, Stub126, Stub127,
    Stub128, Stub129,
};

static void TestGroupCoverage(void)
{
    i32 aSeen[PUFO_PBANK_WIDGET_COUNT];
    i32 nGroup;
    i32 i;
    i32 nTotal = 0;
    i32 nNamed = 0;

    printf("Descriptor groups\n");
    memset(aSeen, 0, sizeof(aSeen));
    for (i = 0; i < 19; ++i) {
        aSeen[i]++;
    }
    nTotal += 19;
    for (nGroup = 0; nGroup < 12; ++nGroup) {
        for (i = 0; i < g_aUiBankGroup[nGroup].nCount; ++i) {
            aSeen[g_aUiBankGroup[nGroup].nFirstWidget + i]++;
        }
        nTotal += (i32)g_aUiBankGroup[nGroup].nCount;
    }
    TestEqI32("  115 widgets are laid out", nTotal, 115);
    for (i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        if (i <= 112 || i == 128 || i == 129) {
            TestEqI32("  widget is laid out exactly once", aSeen[i], 1);
        } else {
            TestEqI32("  widgets 113..127 are never laid out", aSeen[i], 0);
        }
    }
    for (i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        if (g_aUiClickName[i] != NULL) {
            nNamed++;
        }
    }
    TestEqI32("  115 click handlers are bound", nNamed, 115);
    TestCheck("  widget 0 is Tact_CycleNextUnit",
              strcmp(g_aUiClickName[0], "Tact_CycleNextUnit") == 0);
    TestCheck("  widget 40 is the special action pad",
              strcmp(g_aUiClickName[40], "TactHud_UseSpecialItem20") == 0);
    TestCheck("  widget 112 is MenuBtn_Confirm5",
              strcmp(g_aUiClickName[112], "MenuBtn_Confirm5") == 0);
    TestCheck("  widget 127 has no handler", g_aUiClickName[127] == NULL);
    TestCheck("  widget 129 is MenuBtn_Cancel1",
              strcmp(g_aUiClickName[129], "MenuBtn_Cancel1") == 0);
}

/* Control pad loading is recorded, not performed: the names are the parity fact. */
static const char *g_apzSeen[8];
static i32         g_nSeen;

static bool LoadCtlPads(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser)
{
    (void)pCtl;
    (void)pUser;
    if (g_nSeen < 8) {
        /* The empty pad 0 is what UiControl_LoadPads skips, so recording pad 1 is
         * enough to show which descriptor slot was read. */
        g_apzSeen[g_nSeen++] = (psz0[0] != '\0') ? psz0 : psz1;
    }
    (void)psz2;
    return true;
}

static void TestInitControls(void)
{
    UiBankInitCtx ctx;
    i32           i;

    printf("Tact_InitUiControls\n");
    memset(&g_Bank, 0, sizeof(g_Bank));
    memset(&g_Look, 0, sizeof(g_Look));
    memset(&g_HudPad, 0, sizeof(g_HudPad));
    g_HudPad.uWidth  = 200;
    g_HudPad.uHeight = 100;

    memset(&ctx, 0, sizeof(ctx));
    ctx.pHudPad = &g_HudPad;
    ctx.pLookCtl = &g_Look;
    ctx.apfnClick = g_aStub;
    ctx.pfnLoadPads = LoadCtlPads;
    ctx.pfnLookClick = Stub0;

    Tact_InitUiControls(&g_Bank, &ctx);

    /* The HUD pad is centred on x only; y is bottom-aligned. */
    TestEqI32("  HUD origin x = (240 - 200) >> 1", g_HudPad.nOriginX, 20);
    TestEqI32("  HUD origin y = 320 - 100", g_HudPad.nOriginY, 220);

    /* The tactical group is positioned from that origin. */
    TestEqI32("  widget 0 x = origin + 8", g_Bank.aCtl[0].x, 28);
    TestEqI32("  widget 0 y = origin + 20", g_Bank.aCtl[0].y, 240);
    TestEqI32("  widget 5 y = origin + 20", g_Bank.aCtl[5].y, 240);
    TestEqI32("  widget 6 y = origin + 38", g_Bank.aCtl[6].y, 258);
    TestEqI32("  widget 14 x = origin + 0", g_Bank.aCtl[14].x, 20);
    TestEqI32("  widget 15 x = origin + 206", g_Bank.aCtl[15].x, 226);

    /* Modes: the four radio pads are mode 1, the three hand/command pads are mode 2. */
    TestEqI32("  widget 10 radio mode", g_Bank.aCtl[10].nMode, 1);
    TestEqI32("  widget 13 radio mode", g_Bank.aCtl[13].nMode, 1);
    TestEqI32("  widget 9 plain mode", g_Bank.aCtl[9].nMode, 0);
    TestEqI32("  widget 16 inert mode", g_Bank.aCtl[16].nMode, 2);
    TestEqI32("  widget 18 inert mode", g_Bank.aCtl[18].nMode, 2);

    /* The hand-written sizes, which mode 2 would otherwise leave unset. */
    TestEqU16("  widget 16 width", g_Bank.aCtl[16].uWidth, 100);
    TestEqU16("  widget 16 height", g_Bank.aCtl[16].uHeight, 30);
    TestEqU16("  widget 17 width", g_Bank.aCtl[17].uWidth, 34);
    TestEqU16("  widget 17 height", g_Bank.aCtl[17].uHeight, 53);
    TestEqU16("  widget 18 width", g_Bank.aCtl[18].uWidth, 34);
    TestEqU16("  widget 18 height", g_Bank.aCtl[18].uHeight, 53);

    TestEqI32("  widget 14 blits through BlitEx", g_Bank.aCtl[14].bUnk17, 1);
    TestEqI32("  widget 15 blits through BlitEx", g_Bank.aCtl[15].bUnk17, 1);
    TestEqI32("  widget 31 blits through BlitEx", g_Bank.aCtl[31].bUnk17, 1);
    TestEqI32("  widget 10 starts selected", g_Bank.aCtl[10].nState, 1);
    TestEqI32("  widget 11 starts unselected", g_Bank.aCtl[11].nState, 0);

    /* Every other group is positioned at 0,0. */
    TestEqI32("  inventory widget 19 x", g_Bank.aCtl[19].x, 7);
    TestEqI32("  inventory widget 19 y", g_Bank.aCtl[19].y, 224);
    TestEqI32("  action pad 32 y", g_Bank.aCtl[32].y, 67);
    TestEqI32("  action pad 43 y", g_Bank.aCtl[43].y, 136);
    TestEqI32("  deploy grid 44 x", g_Bank.aCtl[44].x, 84);
    TestEqI32("  deploy grid 46 x", g_Bank.aCtl[46].x, 132);
    TestEqI32("  menu column 53 y", g_Bank.aCtl[53].y, 60);
    TestEqI32("  options 60 x", g_Bank.aCtl[60].x, 61);
    TestEqI32("  options 70 flag pad y", g_Bank.aCtl[70].y, 220);
    TestEqI32("  options 75 close pad x", g_Bank.aCtl[75].x, 224);
    TestEqI32("  research 89 x", g_Bank.aCtl[89].x, 43);
    TestEqI32("  save slot 99 up pad y", g_Bank.aCtl[99].y, 280);
    TestEqI32("  load slot 102 prev pad x", g_Bank.aCtl[102].x, 148);
    TestEqI32("  quit 104 x", g_Bank.aCtl[104].x, 224);
    TestEqI32("  list 112 close pad x", g_Bank.aCtl[112].x, 224);
    TestEqI32("  cancel 128 y", g_Bank.aCtl[128].y, 290);
    TestEqI32("  cancel 129 y", g_Bank.aCtl[129].y, 290);

    /* Widgets 113..127 are left as the bank found them. */
    TestEqI32("  widget 113 untouched", g_Bank.aCtl[113].x, 0);
    TestCheck("  widget 127 has no handler", g_Bank.aCtl[127].pfnClick == NULL);

    /* Handlers land in the slot the original names them in. */
    for (i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        if (g_aUiClickName[i] == NULL) {
            continue;
        }
        TestCheck("  handler installed in its own slot",
                  g_Bank.aCtl[i].pfnClick == g_aStub[i]);
    }

    /* The look control. */
    TestEqI32("  look control disabled", g_Look.bEnabled, 0);
    TestEqI32("  look control bUnk17", g_Look.bUnk17, 1);
    TestEqI32("  look control at 0,0", g_Look.x, 0);
    TestEqI32("  look control mode", g_Look.nMode, 0);
    TestCheck("  look control handler", g_Look.pfnClick == Stub0);
    TestEqI32("  look control pad name", strcmp(g_apzSeen[0], "launch") == 0, 1);

    /* The odd +1 branch: a pad wider than the field centres one pixel left. */
    memset(&g_Bank, 0, sizeof(g_Bank));
    g_HudPad.uWidth = 241;
    g_HudPad.uHeight = 100;
    g_HudPad.nOriginX = -1;
    Tact_InitUiControls(&g_Bank, &ctx);
    TestEqI32("  oversized HUD pad origin x", g_HudPad.nOriginX, 0);
    TestEqI32("  oversized pad shifts widget 0 to 8", g_Bank.aCtl[0].x, 8);

    Tact_InitUiControls(NULL, &ctx);
    Tact_InitUiControls(&g_Bank, NULL);
    TestCheck("  NULL arguments are safe", true);
}

/* ------------------------------------------------------------------ pad loading */

static PadImage  g_aPadStore[PUFO_UIPAD_SLOT_COUNT];
static i32       g_nPadStore;
static PadImage *g_apPad[PUFO_UIPAD_SLOT_COUNT];
static const char *g_apzPadName[PUFO_UIPAD_SLOT_COUNT];

static PadImage *AllocPad(void *pUser)
{
    (void)pUser;
    if (g_nPadStore >= PUFO_UIPAD_SLOT_COUNT) {
        return NULL;
    }
    memset(&g_aPadStore[g_nPadStore], 0, sizeof(PadImage));
    return &g_aPadStore[g_nPadStore++];
}

static bool LoadPad(PadImage *pPad, const char *pszName, u8 nMode, void *pUser)
{
    (void)pUser;
    /* The sizes are invented here; only the centring rule reads them. The minimap
     * cursor is given an odd size so the >> 1 truncation is visible. */
    pPad->uWidth  = 201;
    pPad->uHeight = 321;
    pPad->nMode   = nMode;
    g_apzPadName[g_nPadStore - 1] = pszName;
    return true;
}

static void StorePad(i32 nSlot, PadImage *pPad, void *pUser)
{
    (void)pUser;
    g_apPad[nSlot] = pPad;
}

static void TestLoadPads(void)
{
    UiPadHooks hooks;
    i32        i;

    printf("Tact_LoadUiPads\n");
    memset(g_apPad, 0, sizeof(g_apPad));
    memset(g_apzPadName, 0, sizeof(g_apzPadName));
    g_nPadStore = 0;

    memset(&hooks, 0, sizeof(hooks));
    hooks.pfnAlloc = AllocPad;
    hooks.pfnLoad = LoadPad;
    hooks.pfnStore = StorePad;

    TestCheck("  all 54 pads load", Tact_LoadUiPads(&hooks));
    TestEqI32("  32 + 11 + 11 pads allocated", g_nPadStore, 54);

    /* Manual pads: mode 34, origin forced to 50,55. */
    TestEqI32("  manual pad 0 mode", g_apPad[PUFO_UIPAD_MAN(0)]->nMode, 34);
    TestEqI32("  manual pad 0 origin x", g_apPad[PUFO_UIPAD_MAN(0)]->nOriginX, 50);
    TestEqI32("  manual pad 0 origin y", g_apPad[PUFO_UIPAD_MAN(0)]->nOriginY, 55);
    TestEqI32("  manual pad 31 mode", g_apPad[PUFO_UIPAD_MAN(31)]->nMode, 34);
    TestEqI32("  manual pad 31 origin y", g_apPad[PUFO_UIPAD_MAN(31)]->nOriginY, 55);
    TestEqI32("  32 manual pad names", strcmp(g_aUiManPadName[31], "MAN_3F3") == 0, 1);
    TestEqI32("  first manual pad name", strcmp(g_aUiManPadName[0], "MAN_0M0") == 0, 1);
    TestCheck("  pad 0 loaded as MAN_0M0",
              strcmp(g_apzPadName[0], "MAN_0M0") == 0);
    TestCheck("  pad 31 loaded as MAN_3F3",
              strcmp(g_apzPadName[31], "MAN_3F3") == 0);
    TestCheck("  the named pads load in table order",
              strcmp(g_apzPadName[32], "invv") == 0 &&
              strcmp(g_apzPadName[41], "minimcur") == 0 &&
              strcmp(g_apzPadName[42], "dpanelv") == 0);
    TestCheck("  button 0 loaded as b0",
              strcmp(g_apzPadName[43], "b0") == 0);
    TestCheck("  button 10 loaded as b10",
              strcmp(g_apzPadName[53], "b10") == 0);

    /* Named pads, in load order. */
    TestEqI32("  invv mode", g_apPad[PUFO_UIPAD_INV]->nMode, 1);
    TestEqI32("  invv origin cleared", g_apPad[PUFO_UIPAD_INV]->nOriginX, 0);
    TestEqI32("  soldier mode", g_apPad[PUFO_UIPAD_SOLDIER]->nMode, 8);
    TestEqI32("  mscanv mode", g_apPad[PUFO_UIPAD_MARKER]->nMode, 16);
    TestEqI32("  selsold mode", g_apPad[PUFO_UIPAD_SELSOLD]->nMode, 34);
    TestEqI32("  selsold origin untouched", g_apPad[PUFO_UIPAD_SELSOLD]->nOriginY, 0);
    TestEqI32("  enemystp mode", g_apPad[PUFO_UIPAD_ENEMYSTEP]->nMode, 9);
    TestEqI32("  endmiss mode", g_apPad[PUFO_UIPAD_ENDMISSION]->nMode, 11);
    TestEqI32("  twonone mode", g_apPad[PUFO_UIPAD_TWONONE]->nMode, 15);
    TestEqI32("  dpanelv mode", g_apPad[PUFO_UIPAD_HUD]->nMode, 1);

    /* The minimap cursor is centred with the same rule: (240 - 201) >> 1 = 19, and
     * (321 - 321) >> 1 = 0. */
    TestEqI32("  minimcur centred x", g_apPad[PUFO_UIPAD_MINICURSOR]->nOriginX, 19);
    TestEqI32("  minimcur centred y", g_apPad[PUFO_UIPAD_MINICURSOR]->nOriginY, 0);

    /* The eleven buttons. */
    TestEqI32("  button 0 mode", g_apPad[PUFO_UIPAD_BTN(0)]->nMode, 1);
    TestEqI32("  button 10 mode", g_apPad[PUFO_UIPAD_BTN(10)]->nMode, 1);
    TestEqI32("  button 10 origin cleared",
              g_apPad[PUFO_UIPAD_BTN(10)]->nOriginY, 0);

    for (i = 0; i < PUFO_UIPAD_SLOT_COUNT; ++i) {
        TestCheck("  every slot is filled", g_apPad[i] != NULL);
    }

    TestCheck("  NULL hooks are safe", !Tact_LoadUiPads(NULL));
}

int main(void)
{
    TestGroupCoverage();
    TestInitControls();
    TestLoadPads();
    return TestSummary("test_uibank");
}
