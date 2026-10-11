/* pufo_uibank.c - the widget bank and the pad bank the HUD is built from. */
#include "ui/pufo_uibank.h"

#include <stddef.h>

/* ------------------------------------------------------------------ descriptors */
/* Read out of pufobeta.exe with tools/gen_uidesc_c.py; the record layout is the
 * one UiControl_Setup @ 0x1a75c names in its own ARM (xOff +0, yOff +2, szPad0 +4,
 * szPad1 +13, szPad2 +22, nMode +31). */

static const UiControlDesc aUiDescTact[19] = {
    {     8,    20, "",          "nextu",    "",         0 },
    {    46,    20, "",          "uplev",    "",         0 },
    {    84,    20, "",          "cuplev",   "",         0 },
    {   121,    20, "",          "map",      "",         0 },
    {   159,    20, "",          "invent",   "",         0 },
    {   197,    20, "",          "endturn",  "",         0 },
    {    46,    38, "",          "dnlev",    "",         0 },
    {    84,    38, "",          "cdnlev",   "",         0 },
    {   121,    38, "",          "sit",      "",         0 },
    {   159,    38, "",          "center",   "",         0 },
    {    50,    60, "",          "alwaysrs", "alwaysr",  1 },
    {    69,    60, "",          "singles",  "single",   1 },
    {    50,    73, "",          "aims",     "aim",      1 },
    {    69,    73, "",          "autos",    "auto",     1 },
    {     0,     3, "",          "mainmenu", "",         0 },
    {   206,     3, "",          "pmenu",    "",         0 },
    {    92,    60, "",          "",         "",         2 },
    {     9,    39, "",          "",         "",         2 },
    {   198,    39, "",          "",         "",         2 },
};

static const UiControlDesc aUiDescInv[13] = {
    {     7,   224, "",          "prev",     "",         0 },
    {    35,   224, "",          "next",     "",         0 },
    {   182,   241, "",          "devicevs", "devicev",  1 },
    {   190,   281, "",          "scrollr",  "",         0 },
    {   190,   299, "",          "scrolll",  "",         0 },
    {   190,   262, "",          "alls",     "all",      1 },
    {    86,   224, "",          "pistolvs", "pistolv",  1 },
    {   134,   224, "",          "grenvs",   "grenv",    1 },
    {   182,   224, "",          "ammovs",   "ammov",    1 },
    {    86,   241, "",          "riflevs",  "riflev",   1 },
    {   134,   241, "",          "rocketvs", "rocketv",  1 },
    {   225,     3, "bclose",    "bcloseu",  "",         0 },
    {     7,   244, "",          "binfo",    "",         0 },
};

/* Nine of the twelve action pads share 27,136. That is not a decoding error: the
 * screen-3 menu only ever enables the modes the current weapon offers, so the
 * overlapping slots are inert. */
static const UiControlDesc aUiDescAction[12] = {
    {    27,    67, "firebtn",   "cfirebtn", "",         0 },
    {    27,    90, "firebtn",   "cfirebtn", "",         0 },
    {    27,   113, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
    {    27,   136, "firebtn",   "cfirebtn", "",         0 },
};

static const UiControlDesc aUiDescDeploy[9] = {
    {    84,   124, "initg",     "cinitg",   "",         0 },
    {   108,   124, "initg",     "cinitg",   "",         0 },
    {   132,   124, "initg",     "cinitg",   "",         0 },
    {    84,   148, "initg",     "cinitg",   "",         0 },
    {   108,   148, "initg",     "cinitg",   "",         0 },
    {   132,   148, "initg",     "cinitg",   "",         0 },
    {    84,   172, "initg",     "cinitg",   "",         0 },
    {   108,   172, "initg",     "cinitg",   "",         0 },
    {   132,   172, "initg",     "cinitg",   "",         0 },
};

static const UiControlDesc aUiDescMenu[7] = {
    {    27,    60, "firebtn",   "cfirebtn", "",         0 },
    {    27,    83, "firebtn",   "cfirebtn", "",         0 },
    {    27,   106, "firebtn",   "cfirebtn", "",         0 },
    {    27,   129, "firebtn",   "cfirebtn", "",         0 },
    {    27,   152, "firebtn",   "cfirebtn", "",         0 },
    {    27,   175, "firebtn",   "cfirebtn", "",         0 },
    {    27,   198, "firebtn",   "cfirebtn", "",         0 },
};

static const UiControlDesc aUiDescOptions[29] = {
    {    61,    20, "initg",     "cinitg",   "cinitg",   1 },
    {    85,    20, "initg",     "cinitg",   "cinitg",   1 },
    {   109,    20, "initg",     "cinitg",   "cinitg",   1 },
    {   133,    20, "initg",     "cinitg",   "cinitg",   1 },
    {   157,    20, "initg",     "cinitg",   "cinitg",   1 },
    {    61,    70, "initg",     "cinitg",   "cinitg",   1 },
    {    85,    70, "initg",     "cinitg",   "cinitg",   1 },
    {   109,    70, "initg",     "cinitg",   "cinitg",   1 },
    {   133,    70, "initg",     "cinitg",   "cinitg",   1 },
    {   157,    70, "initg",     "cinitg",   "cinitg",   1 },
    {    20,   220, "flag",      "sflag",    "cflag",    1 },
    {    20,   240, "flag",      "sflag",    "cflag",    1 },
    {    20,   260, "flag",      "sflag",    "cflag",    1 },
    {    20,   280, "flag",      "sflag",    "cflag",    1 },
    {    20,   300, "flag",      "sflag",    "cflag",    1 },
    {   224,     1, "bclose",    "bcloseu",  "",         0 },
    {    20,   120, "soundf",    "ssoundf",  "csoundf",  1 },
    {    20,   170, "soundf",    "ssoundf",  "csoundf",  1 },
    {    61,   120, "initg",     "cinitg",   "cinitg",   1 },
    {    85,   120, "initg",     "cinitg",   "cinitg",   1 },
    {   109,   120, "initg",     "cinitg",   "cinitg",   1 },
    {   133,   120, "initg",     "cinitg",   "cinitg",   1 },
    {   157,   120, "initg",     "cinitg",   "cinitg",   1 },
    {    61,   170, "initg",     "cinitg",   "cinitg",   1 },
    {    85,   170, "initg",     "cinitg",   "cinitg",   1 },
    {   109,   170, "initg",     "cinitg",   "cinitg",   1 },
    {   133,   170, "initg",     "cinitg",   "cinitg",   1 },
    {   157,   170, "initg",     "cinitg",   "cinitg",   1 },
    {    20,   200, "flag",      "sflag",    "cflag",    1 },
};

static const UiControlDesc aUiDescResearch[9] = {
    {    43,    67, "pushpu",    "pushpd",   "",         0 },
    {    43,    90, "pushpu",    "pushpd",   "",         0 },
    {    43,   113, "pushpu",    "pushpd",   "",         0 },
    {    43,   136, "pushpu",    "pushpd",   "",         0 },
    {    43,   136, "pushpu",    "pushpd",   "",         0 },
    {    43,   159, "pushpu",    "pushpd",   "",         0 },
    {    43,   159, "pushpu",    "pushpd",   "",         0 },
    {    43,   159, "pushpu",    "pushpd",   "",         0 },
    {    43,   159, "pushpu",    "pushpd",   "",         0 },
};

static const UiControlDesc aUiDescSaveSlot[3] = {
    {   224,     1, "bclose",    "bcloseu",  "",         0 },
    {   215,   280, "up",        "sup",      "",         0 },
    {   215,   300, "down",      "sdown",    "",         0 },
};

static const UiControlDesc aUiDescLoadSlot[3] = {
    {   223,     2, "bclose",    "bcloseu",  "",         0 },
    {   148,     3, "bprevs",    "bprev",    "",         0 },
    {   183,     3, "bnexts",    "bnext",    "",         0 },
};

static const UiControlDesc aUiDescQuit[1] = {
    {   224,     1, "bclose",    "bcloseu",  "",         0 },
};

static const UiControlDesc aUiDescList[8] = {
    {    27,    60, "firebtn",   "cfirebtn", "",         0 },
    {    27,    83, "firebtn",   "cfirebtn", "",         0 },
    {    27,   106, "firebtn",   "cfirebtn", "",         0 },
    {    27,   129, "firebtn",   "cfirebtn", "",         0 },
    {    27,   152, "firebtn",   "cfirebtn", "",         0 },
    {    27,   175, "firebtn",   "cfirebtn", "",         0 },
    {    27,   198, "firebtn",   "cfirebtn", "",         0 },
    {   224,     1, "bclose",    "bcloseu",  "",         0 },
};

static const UiControlDesc aUiDescCancel0[1] = {
    {    46,   290, "pushpu",    "pushpd",   "",         0 },
};

static const UiControlDesc aUiDescCancel1[1] = {
    {    46,   290, "pushpu",    "pushpd",   "",         0 },
};

/* pDesc @ 0xA8138, the descriptor of the heap-allocated screen-0 look control. */
const UiControlDesc g_uiLookDesc = { 0, 0, "launch", "claunch", "", 0 };

const UiBankGroup g_aUiBankGroup[12] = {
    {  53,  7, aUiDescMenu     }, /* runs first of the zero-based groups */
    {  19, 13, aUiDescInv      },
    {  32, 12, aUiDescAction   },
    {  44,  9, aUiDescDeploy   },
    {  60, 29, aUiDescOptions  },
    {  89,  9, aUiDescResearch },
    {  98,  3, aUiDescSaveSlot },
    { 101,  3, aUiDescLoadSlot },
    { 104,  1, aUiDescQuit     },
    { 105,  8, aUiDescList     },
    { 128,  1, aUiDescCancel0  },
    { 129,  1, aUiDescCancel1  },
};

/* ---------------------------------------------------------------- click bindings */

const char *const g_aUiClickName[PUFO_PBANK_WIDGET_COUNT] = {
    "Tact_CycleNextUnit",                 /* 0x16684 */
    "TactHud_ClimbUp",                    /* 0x1674c */
    "MapView_LayerUp",                    /* 0x16530 */
    "MapView_EnterLookMode",              /* 0x163d4 */
    "Inv_OpenScreen",                     /* 0x16ef4 */
    "MenuBtn_EndTurn",                    /* 0x16494 */
    "TactHud_ClimbDown",                  /* 0x16af0 */
    "MapView_LayerDown",                  /* 0x164f0 */
    "TactHud_ToggleKneel",                /* 0x16570 */
    "MapView_FocusSelected",              /* 0x16710 */
    "UiBank_SelectRadio0_Play11",         /* 0x16e04 */
    "UiBank_SelectRadio1_Play11",         /* 0x16e40 */
    "UiBank_SelectRadio2_Play11",         /* 0x16e7c */
    "UiBank_SelectRadio3_Play11",         /* 0x16eb8 */
    "Map_UiScreen7_PlaySample0E",         /* 0x164b8 */
    "Map_UiScreen0F_PlaySample0F",        /* 0x164d4 */
    "Tact_OpenCommandScreen",             /* 0x16ffc */
    "Tact_OpenLeftHandMenu",              /* 0x17030 */
    "Tact_OpenRightHandMenu",             /* 0x17148 */
    "Tact_SelectPrevUnit",                /* 0x17260 */
    "Tact_SelectNextUnit",                /* 0x17304 */
    "Inv_SetFilter5",                     /* 0x17710 */
    "Inv_ScrollDown",                     /* 0x173a8 */
    "Inv_ScrollUp",                       /* 0x173e4 */
    "Inv_SetFilterAll",                   /* 0x17410 */
    "Inv_SetFilter0",                     /* 0x17490 */
    "Inv_SetFilter3",                     /* 0x17510 */
    "Inv_SetFilter4",                     /* 0x17590 */
    "Inv_SetFilter1",                     /* 0x17610 */
    "Inv_SetFilter2",                     /* 0x17690 */
    "MenuBtn_Confirm2",                   /* 0x19884 */
    "Tact_OpenCommandScreenFlagged",      /* 0x198c4 */
    "Tact_SetFireModeAuto",               /* 0x17868 */
    "Tact_SetFireModeSnap",               /* 0x177fc */
    "Tact_SetFireModeAimed",              /* 0x17790 */
    "TactAction_ConfirmUseItem",          /* 0x178d4 */
    "Tact_TryFireMode4",                  /* 0x179e4 */
    "Tact_TryFireMode5",                  /* 0x17b1c */
    "TactAction_ConfirmMeleeMode6",       /* 0x17bb8 */
    "Tact_BeginMultiTarget",              /* 0x18380 */
    "TactHud_UseSpecialItem20",           /* 0x17d54 */
    "TactAction_ConfirmSpecial9",         /* 0x18260 */
    "Tact_TryFireMode10",                 /* 0x17a80 */
    "Map_PlaySample11_SetMode0B",         /* 0x179c0 */
    "Map_SetSubMode0_Play11",             /* 0x18418 */
    "Map_SetSubMode1_Play11",
    "Map_SetSubMode2_Play11",
    "Map_SetSubMode3_Play11",
    "Map_SetSubMode4_Play11",
    "Map_SetSubMode5_Play11",
    "Map_SetSubMode6_Play11",
    "Map_SetSubMode7_Play11",
    "Map_SetSubMode8_Play11",
    "Menu_OpenLoadSlots",
    "Menu_OpenSaveSlots",
    "Options_SyncUiFromMap_GotoScreen12",
    "Map_BuildDeployConfirmYesNo",
    "Menu_ConfirmQuitDialog",
    "MenuBtn_ResizeWindow",
    "Map_PlaySamples10_0E_ClearUiScreen",
    "MenuBtn_SetSpeed0",
    "MenuBtn_SetSpeed1",
    "MenuBtn_SetSpeed2",
    "MenuBtn_SetSpeed3",
    "MenuBtn_SetSpeed4",
    "MenuBtn_SetGamma0",
    "MenuBtn_SetGamma1",
    "MenuBtn_SetGamma2",
    "MenuBtn_SetGamma3",
    "MenuBtn_SetGamma4",
    "Map_ToggleFlag128A8D_Play11",
    "Map_ToggleFlag128A8E_Play11",
    "Map_ToggleFlag128A8F_Play11",
    "Map_ToggleFlag128A90_Play11",
    "Map_ToggleFlag128A91_Play11",
    "MenuBtn_Confirm4",
    "Ui_ToggleMusicMute",
    "Ui_ToggleSfxMute",
    "Options_SetMusicVol1",
    "Options_SetMusicVol2",
    "Options_SetMusicVol3",
    "Options_SetMusicVol4",
    "Options_SetMusicVol5",
    "MenuBtn_SetSfxVol1",
    "MenuBtn_SetSfxVol2",
    "MenuBtn_SetSfxVol3",
    "MenuBtn_SetSfxVol4",
    "MenuBtn_SetSfxVol5",
    "TactHud_ToggleFlag128A9C",
    "MenuBtn_Secondary",
    "MenuBtn_ActionBack1",
    "MenuBtn_SecondaryDismiss",
    "MenuBtn_ConfirmClear",
    "MenuBtn_ActionBack0",
    "Map_PlaySamples10_7_SetFlag_ClearScreen",
    "Map_PlaySamples10_7_ClearFlag_ClearScreen",
    "Map_UiScreen7_PlaySamples10_0E",
    "MenuBtn_ConfirmSnd15",
    "MenuBtn_Confirm0",
    "Map_IncByte128AF0_Play11",
    "Map_DecByte128AF0_Play11",
    "MenuBtn_Confirm1",
    "Tact_SelectPrevUnitFiltered",
    "Tact_SelectNextUnitFiltered",
    "MenuBtn_Confirm3",
    "SaveLoad_Slot0",
    "SaveLoad_Slot1",
    "SaveLoad_Slot2",
    "SaveLoad_Slot3",
    "SaveLoad_Slot4",
    "SaveLoad_Slot5",
    "SaveLoad_Slot6",
    "MenuBtn_Confirm5",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, /* 113..120, never installed */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,       /* 121..127 */
    "MenuBtn_Cancel0",
    "MenuBtn_Cancel1",
};

/* -------------------------------------------------------------------- pad bank */

const char *const g_aUiManPadName[PUFO_UIBANK_MAN_PAD_COUNT] = {
    "MAN_0M0", "MAN_0M1", "MAN_0M2", "MAN_0M3",
    "MAN_0F0", "MAN_0F1", "MAN_0F2", "MAN_0F3",
    "MAN_1M0", "MAN_1M1", "MAN_1M2", "MAN_1M3",
    "MAN_1F0", "MAN_1F1", "MAN_1F2", "MAN_1F3",
    "MAN_2M0", "MAN_2M1", "MAN_2M2", "MAN_2M3",
    "MAN_2F0", "MAN_2F1", "MAN_2F2", "MAN_2F3",
    "MAN_3M0", "MAN_3M1", "MAN_3M2", "MAN_3M3",
    "MAN_3F0", "MAN_3F1", "MAN_3F2", "MAN_3F3",
};

const char *const g_aUiBtnPadName[PUFO_UIBANK_BTN_PAD_COUNT] = {
    "b0", "b1", "b2", "b3", "b4", "b5",
    "b6", "b7", "b8", "b9", "b10",
};

const UiPadSpec g_aUiNamedPad[PUFO_UIBANK_NAMED_PAD_COUNT] = {
    { "invv",     1, PUFO_UIPAD_INV,        PUFO_PAD_ORIGIN_ZERO  },
    { "soldier",  8, PUFO_UIPAD_SOLDIER,    PUFO_PAD_ORIGIN_ZERO  },
    { "mscanv",  16, PUFO_UIPAD_MARKER,     PUFO_PAD_ORIGIN_ZERO  },
    { "selsold", 34, PUFO_UIPAD_SELSOLD,    PUFO_PAD_ORIGIN_KEEP  },
    { "sbsold",  16, PUFO_UIPAD_SBSOLD,     PUFO_PAD_ORIGIN_KEEP  },
    { "ssold",   16, PUFO_UIPAD_SOLD,       PUFO_PAD_ORIGIN_KEEP  },
    { "enemystp", 9, PUFO_UIPAD_ENEMYSTEP,  PUFO_PAD_ORIGIN_ZERO  },
    { "endmiss", 11, PUFO_UIPAD_ENDMISSION, PUFO_PAD_ORIGIN_ZERO  },
    { "twonone", 15, PUFO_UIPAD_TWONONE,    PUFO_PAD_ORIGIN_KEEP  },
    { "minimcur",16, PUFO_UIPAD_MINICURSOR, PUFO_PAD_ORIGIN_CENTRE },
    { "dpanelv",  1, PUFO_UIPAD_HUD,        PUFO_PAD_ORIGIN_KEEP  },
};

/* The +1 branch only fires when the pad is bigger than the field, and the shift is
 * arithmetic. Both halves of the rule are used for the minimap cursor; the HUD pad
 * uses only the x half (its y is bottom-aligned, not centred). */
static i32 Pad_CentreX(u16 uWidth)
{
    i32 n = (i32)PUFO_UIBANK_FIELD_W - (i32)uWidth;

    if (uWidth > PUFO_UIBANK_FIELD_W) {
        n = (i32)PUFO_UIBANK_FIELD_W + 1 - (i32)uWidth;
    }
    return n >> 1;
}

static i32 Pad_CentreY(u16 uHeight)
{
    i32 n = (i32)PUFO_UIBANK_FIELD_H - (i32)uHeight;

    if (uHeight > PUFO_UIBANK_FIELD_H) {
        n = (i32)PUFO_UIBANK_FIELD_H + 1 - (i32)uHeight;
    }
    return n >> 1;
}

static PadImage *LoadOnePad(const UiPadHooks *pHooks, const char *pszName, u8 nMode,
                            i32 nSlot)
{
    PadImage *pPad = pHooks->pfnAlloc(pHooks->pUser);

    if (pPad == NULL) {
        /* Deviation 2: the original stores the NULL and then loads through it. */
        pHooks->pfnStore(nSlot, NULL, pHooks->pUser);
        return NULL;
    }
    pHooks->pfnStore(nSlot, pPad, pHooks->pUser);
    if (!pHooks->pfnLoad(pPad, pszName, nMode, pHooks->pUser)) {
        return NULL;
    }
    return pPad;
}

bool Tact_LoadUiPads(const UiPadHooks *pHooks)
{
    i32     i;
    PadImage *pMini;

    if (pHooks == NULL || pHooks->pfnAlloc == NULL || pHooks->pfnLoad == NULL ||
        pHooks->pfnStore == NULL) {
        return false;
    }

    /* 32 manual pads: mode 34, origin forced to 50,55 after the load. */
    for (i = 0; i < PUFO_UIBANK_MAN_PAD_COUNT; ++i) {
        PadImage *pPad = LoadOnePad(pHooks, g_aUiManPadName[i], PUFO_UIPAD_MAN_MODE,
                                    PUFO_UIPAD_MAN(i));
        if (pPad == NULL) {
            return false;
        }
        pPad->nOriginX = PUFO_UIPAD_MAN_ORIGIN_X;
        pPad->nOriginY = PUFO_UIPAD_MAN_ORIGIN_Y;
    }

    pMini = NULL;
    for (i = 0; i < PUFO_UIBANK_NAMED_PAD_COUNT; ++i) {
        const UiPadSpec *pSpec = &g_aUiNamedPad[i];
        PadImage        *pPad  = LoadOnePad(pHooks, pSpec->szName, pSpec->nMode,
                                            pSpec->nSlot);

        if (pPad == NULL) {
            return false;
        }
        if (pSpec->nOrigin == PUFO_PAD_ORIGIN_ZERO) {
            pPad->nOriginX = 0;
            pPad->nOriginY = 0;
        } else if (pSpec->nOrigin == PUFO_PAD_ORIGIN_CENTRE) {
            pPad->nOriginX = Pad_CentreX(pPad->uWidth);
            pPad->nOriginY = Pad_CentreY(pPad->uHeight);
            pMini          = pPad;
        }
    }

    /* Eleven buttons, named 'b' + the decimal index (the original uses itoa). */
    for (i = 0; i < PUFO_UIBANK_BTN_PAD_COUNT; ++i) {
        PadImage *pPad = LoadOnePad(pHooks, g_aUiBtnPadName[i], PUFO_UIPAD_BTN_MODE,
                                    PUFO_UIPAD_BTN(i));
        if (pPad == NULL) {
            return false;
        }
        pPad->nOriginX = 0;
        pPad->nOriginY = 0;
    }
    (void)pMini;
    return true;
}

/* ------------------------------------------------------------- the bank builder */

void Tact_InitUiControls(UiWidgetBank *pBank, const UiBankInitCtx *pCtx)
{
    i32      i;
    i32      nGroup;
    i32      nX;
    i32      nY;

    if (pBank == NULL || pCtx == NULL) {
        return;
    }

    /* 1. The click handlers, in the order the original stores them. Widgets 113..127
     *    have no entry in the table and are left exactly as the bank found them. */
    for (i = 0; i < PUFO_PBANK_WIDGET_COUNT; ++i) {
        if (g_aUiClickName[i] == NULL) {
            continue;
        }
        pBank->aCtl[i].pfnClick = (pCtx->apfnClick != NULL) ? pCtx->apfnClick[i]
                                                             : NULL;
    }

    /* 2. The screen-0 look control. The original allocates it here; the port takes it
     *    from the caller because the bank is not a heap owner. */
    if (pCtx->pLookCtl != NULL) {
        UiControl_ctor(pCtx->pLookCtl);
        pCtx->pLookCtl->bEnabled = 0;
        pCtx->pLookCtl->pfnClick = pCtx->pfnLookClick;
        UiControl_Setup(pCtx->pLookCtl, 0, 0, &g_uiLookDesc, pCtx->pfnLoadPads,
                        pCtx->pUser);
        pCtx->pLookCtl->bUnk17 = 1;
    }

    /* 3. The HUD pad origin. Only x is centred; y is bottom-aligned, and the +1
     *    branch is not applied to y at all here. */
    if (pCtx->pHudPad != NULL) {
        nX = Pad_CentreX(pCtx->pHudPad->uWidth);
        nY = (i32)PUFO_UIBANK_FIELD_H - (i32)pCtx->pHudPad->uHeight;
        pCtx->pHudPad->nOriginX = nX;
        pCtx->pHudPad->nOriginY = nY;
    } else {
        nX = 0;
        nY = 0;
    }

    /* 4. The tactical group, positioned from that origin. */
    for (i = 0; i < 19; ++i) {
        UiControl_Setup(&pBank->aCtl[i], (i16)nX, (i16)nY, &aUiDescTact[i],
                        pCtx->pfnLoadPads, pCtx->pUser);
    }

    /* 5. The twelve groups at base 0,0. */
    for (nGroup = 0; nGroup < 12; ++nGroup) {
        const UiBankGroup *pGroup = &g_aUiBankGroup[nGroup];

        for (i = 0; i < pGroup->nCount; ++i) {
            UiControl_Setup(&pBank->aCtl[pGroup->nFirstWidget + i], 0, 0,
                            &pGroup->pDesc[i], pCtx->pfnLoadPads, pCtx->pUser);
        }
        /* The original writes two single bytes between groups: widget 10 nState after
         * the menu column, widget 31 bUnk17 after the inventory group. No later group
         * touches either widget, so applying them with the rest of the overrides below
         * is the same observable result. */
    }

    /* 6. The overrides. Widgets 16, 17 and 18 are mode 2, the inert mode that keeps
     *    its own size, which is why the size has to be written by hand. */
    pBank->aCtl[16].uWidth  = 100;
    pBank->aCtl[16].uHeight = 30;
    pBank->aCtl[17].uWidth  = 34;
    pBank->aCtl[17].uHeight = 53;
    pBank->aCtl[18].uWidth  = 34;
    pBank->aCtl[18].uHeight = 53;
    pBank->aCtl[14].bUnk17  = 1;
    pBank->aCtl[15].bUnk17  = 1;
    pBank->aCtl[31].bUnk17  = 1;
    pBank->aCtl[10].nState  = 1;
}
