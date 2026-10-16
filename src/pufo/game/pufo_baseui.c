/* pufo_baseui.c - see pufo_baseui.h for evidence. */
#include "game/pufo_baseui.h"
#include <stdlib.h>  /* atexit, for StrategList_RegisterAtexit @ 0x838a0 */
#include <stdio.h>   /* snprintf, the bounded stand-in for the original's j_itoa */
#include <string.h>  /* memset, the include table BuySellScreen_BuildPanels clears */
#include "util/pufo_str.h" /* pufo_strcpy/pufo_strcat, the line writes of ResearchScreen_RefreshPanels */
#include "audio/pufo_audio.h" /* Sound_Play, the screen-open sample of SoldierScreen_Toggle */
#include "game/pufo_strateg.h" /* StrategList_Clear */
#include "game/pufo_font.h"    /* Text_DrawString / Text_MeasureWidth, the dialog's labels */
#include "game/pufo_boot.h"    /* PUFO_TEXT_REC_STRIDE, the craft-name records */
#include "game/pufo_mainmenu.h" /* PUFO_MAINMENU_MODE_*, the list mode the slot handlers set */
#include "game/pufo_titles.h"   /* EndScreen_Init, the bankruptcy close of Geoscape_CloseUI */
#include "util/pufo_rand.h"     /* Game_RandU16, the unlock roll of ResearchScreen_RebuildTopicList */
#include "platform/pufo_reg.h"  /* Reg_SetString, the four audio values Options_SaveAudioSettings writes */

void BaseUi_SetScratch4(ScreenScratch *pScratch)
{
    if (pScratch == NULL) {
        return;
    }
    pScratch->nLineAux145 = 4;
}

void BaseUi_SetScratch3AndUiY(ScreenScratch *pScratch)
{
    if (pScratch == NULL) {
        return;
    }
    pScratch->nLineAux145 = 3;
    pScratch->nControl0Y = 120;
    pScratch->nControl1Y = 142;
}

i32 BaseUi_Cache186BE8(ScreenScratch *pScratch, i32 nUnk3032)
{
    if (pScratch == NULL) {
        return nUnk3032;
    }
    pScratch->nCache186BE8 = nUnk3032;
    return nUnk3032;
}

i32 BaseUi_PlaySample7Cache160C74(ScreenScratch *pScratch, i32 nUnk3032,
                                  void (*pfnPlaySample)(u32 nSample, void *pUser),
                                  void *pUser)
{
    if (pfnPlaySample != NULL) {
        pfnPlaySample(7, pUser);
    }
    if (pScratch == NULL) {
        return nUnk3032;
    }
    pScratch->bFlag160C80 = true;
    pScratch->bFlag187080 = 0;
    pScratch->nCache160C74 = nUnk3032;
    return nUnk3032;
}

/* Ui_OpenScreen18 @ 0x84e9c. */
void Ui_OpenScreen18(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, pHooks->pUser);
    }
    if (pState != NULL) {
        pState->nGeoscapeScreen = 18;
    }
    if (pHooks != NULL && pHooks->pfnOptionsRebuildWithSound != NULL) {
        pHooks->pfnOptionsRebuildWithSound(pHooks->pUser);
    }
}

/* Ui_CloseScreen18 @ 0x84ecc. Note it also sets the screen id to 18. */
void Ui_CloseScreen18(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, pHooks->pUser);
    }
    if (pState != NULL) {
        pState->nGeoscapeScreen = 18;
    }
    if (pHooks != NULL && pHooks->pfnOptionsOpenWithSound != NULL) {
        pHooks->pfnOptionsOpenWithSound(pState != NULL ? pState->nLineAux119 : 0,
                                        pHooks->pUser);
    }
}

/* Ui_OpenFunding @ 0x85164. */
void Ui_OpenFunding(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, pHooks->pUser);
    }
    if (pState != NULL) {
        pState->nGeoscapeScreen = 15;
    }
    if (pHooks != NULL && pHooks->pfnFundingOpen != NULL) {
        pHooks->pfnFundingOpen(pHooks->pUser);
    }
}

/* ------------------------------------------------- strategic screen list paging */

void BaseFacScreen_PanelPrev(UiPanel *pPanel)
{
    if (pPanel == NULL) {
        return; /* the original would fault on a null unk_15886C */
    }
    UiPanel_ScrollPrev(pPanel); /* 0x589cc is a tail call to 0x3f040 */
}

void BaseFacScreen_PanelNext(UiPanel *pPanel)
{
    if (pPanel == NULL) {
        return;
    }
    UiPanel_ScrollNext(pPanel); /* 0x589dc is a tail call to 0x3f00c */
}

void SoldierScreen_PanelPrev(UiPanel *pFirst, UiPanel *pSecond)
{
    /* 0x61874 / 0x61880: both panels move, apLines[186] first, anLineAux[106] second. */
    if (pFirst != NULL) {
        UiPanel_ScrollPrev(pFirst);
    }
    if (pSecond != NULL) {
        UiPanel_ScrollPrev(pSecond);
    }
}

void SoldierScreen_PanelNext(UiPanel *pFirst, UiPanel *pSecond)
{
    /* 0x618a8 and the returned second call. */
    if (pFirst != NULL) {
        UiPanel_ScrollNext(pFirst);
    }
    if (pSecond != NULL) {
        UiPanel_ScrollNext(pSecond);
    }
}

/* 0x76138 and 0x96ab0 are the same seven instructions as 0x7afec, so both forward to
 * the cache that already owns dword_186BE8. Only the source object differs between the
 * three, and the port takes the +0xBD8 value as an argument. */
i32 BuySellScreen_SyncSelection(ScreenScratch *pScratch, i32 nSel0BD8)
{
    return BaseUi_Cache186BE8(pScratch, nSel0BD8);
}

i32 Transfer_CacheSelIndex(ScreenScratch *pScratch, i32 nSel0BD8)
{
    return BaseUi_Cache186BE8(pScratch, nSel0BD8);
}

i32 ManufScreen_SyncSelection(i32 *pnLineAux147, i32 nSel0BD8)
{
    if (pnLineAux147 != NULL) {
        *pnLineAux147 = nSel0BD8; /* 0x6DD3C */
    }
    return nSel0BD8; /* 0x6DD40 */
}

signed char BaseInfoScreen_OnOk(BaseInfoOkState *pState)
{
    if (pState == NULL) {
        return 0;
    }
    pState->bPsiPending = 1;                        /* 0x557D0, MOV #1 / STRB */
    pState->nCurrentBase = pState->nPendingBase;    /* 0x557DC LDRSB, 0x557E0 STRB */
    return pState->nPendingBase;                    /* 0x557E4 returns R0 */
}

/* 0x716a8 is the same eight instructions as 0x557c8, down to the LDRSB. */
signed char PsiScreen_OnOk(BaseInfoOkState *pState)
{
    return BaseInfoScreen_OnOk(pState);
}

void BuySellScreen_ListPanelPrev(UiPanel *pFirst, UiPanel *pSecond)
{
    if (pFirst != NULL) {
        UiPanel_ScrollPrev(pFirst); /* 0x79A2C, unk_161EDC */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollPrev(pSecond); /* 0x79A38, unk_161A3C */
    }
}

void BuySellScreen_ListPanelNext(UiPanel *pFirst, UiPanel *pSecond)
{
    if (pFirst != NULL) {
        UiPanel_ScrollNext(pFirst); /* 0x79A54 */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollNext(pSecond); /* 0x79A60 */
    }
}

/* The mover loop every handler below is. The original writes the last call of each body as a
 * tail call (B rather than BL); that is a code-generation detail, not a behaviour. */
static void ScrollOnePanel(UiPanel *pPanel, bool bPrev)
{
    if (pPanel == NULL) {
        return; /* the original loads the global and calls straight through; see 0x6DD4C */
    }
    if (bPrev) {
        UiPanel_ScrollPrev(pPanel);
    } else {
        UiPanel_ScrollNext(pPanel);
    }
}

static void ScrollPanelSet(UiPanel *const apPanels[], i32 nCount, bool bPrev)
{
    for (i32 i = 0; i < nCount; ++i) {
        ScrollOnePanel(apPanels[i], bPrev);
    }
}

static void ScrollPanelsPrev(UiPanel *const apPanels[], i32 nCount)
{
    ScrollPanelSet(apPanels, nCount, true);
}

static void ScrollPanelsNext(UiPanel *const apPanels[], i32 nCount)
{
    ScrollPanelSet(apPanels, nCount, false);
}

void ManufScreen_PanelPrev(UiPanel *const apPanels[4])
{
    ScrollPanelsPrev(apPanels, 4); /* 0x6DD54, 0x6DD64, 0x6DD74, 0x6DD84 */
}

void ManufScreen_PanelNext(UiPanel *const apPanels[4])
{
    ScrollPanelsNext(apPanels, 4); /* 0x6DD9C, 0x6DDAC, 0x6DDBC, 0x6DDCC */
}

/* PsiScreen_PanelPrev @ 0x715e8. The header's evidence comment cites it across a line break, which
 * coverage.py's `@ 0x...` pattern cannot follow, so the entry address was never credited even though
 * the handler is here. Cited on one line now. */
void PsiScreen_PanelPrev(UiPanel *const apPanels[4])
{
    ScrollPanelsPrev(apPanels, 4); /* 0x715F8, 0x71610, 0x71628, 0x71640 */
}

void PsiScreen_PanelNext(UiPanel *const apPanels[4])
{
    ScrollPanelsNext(apPanels, 4); /* 0x71658, 0x71670, 0x71688, 0x716A0 */
}

/* BuySellScreen_PanelPrev @ 0x76088 / PanelNext @ 0x760e0, and the byte-for-byte identical
 * Transfer_ScrollPrevAll @ 0x967e4 / ScrollNextAll @ 0x9683c over the same five globals. */
void BuySellScreen_PanelPrev(UiPanel *const apPanels[5])
{
    ScrollPanelsPrev(apPanels, 5); /* 0x76090, 0x760A0, 0x760B0, 0x760C0, 0x760D0 */
}

void BuySellScreen_PanelNext(UiPanel *const apPanels[5])
{
    ScrollPanelsNext(apPanels, 5); /* 0x760E8, 0x760F8, 0x76108, 0x76118, 0x76128 */
}

void BuySellScreen_DetailPanelPrev(UiPanel *const apPanels[5])
{
    ScrollPanelsPrev(apPanels, 5); /* 0x79A78, 0x79A88, 0x79A98, 0x79AA8, 0x79AB8 */
}

void BuySellScreen_DetailPanelNext(UiPanel *const apPanels[5])
{
    ScrollPanelsNext(apPanels, 5); /* 0x79AD0, 0x79AE0, 0x79AF0, 0x79B00, 0x79B10 */
}

/* BuySell_RoleToItemId @ 0x76d88, and its byte-identical twin Transfer_RoleToIconId @ 0x96ad8. */
i32 BuySell_RoleToItemId(i32 nRole)
{
    switch ((i8)nRole) { /* 0x76D88..0x76D8C sign-extends the low byte first */
    case 0: return 117;  /* 0x76DD8 */
    case 1: return 120;  /* 0x76DD0 */
    case 2: return 121;  /* 0x76DC8 */
    case 3: return 118;  /* 0x76DC0 */
    case 4: return 119;  /* 0x76DB8, the fall-through */
    default: return 0xFF; /* 0x76DB0, MOVNE R0,#0xFF */
    }
}

/* BuySell_ItemIdToAlienRaceId @ 0x6ad30. The original is a 25-case jump table over slots
 * 77..101 (SUB R0,R1,#0x4D / CMP R0,#0x18 / ADDLS PC,PC,R0,LSL#2) plus a chain of signed
 * compares for 102, 103, 104, 114 and 144..148. The ranges below are those groups; five of
 * them land on the same MOV as a medic or the extra Ethereal rank, which is what the shared
 * block addresses in the comments record. Everything else answers -1 (0x6AE38). */
static const struct {
    i16 nFirst;
    i16 nLast;
    PufoAlienRaceId nRace;
} g_aAlienRaceSlots[] = {
    { 77,  81,  PUFO_ALIEN_RACE_SECTOID },    /* 0x6AE3C, jumptable cases 0..4; 148 too */
    { 82,  86,  PUFO_ALIEN_RACE_SNAKEMAN },   /* 0x6ADFC, cases 5..9; 144 too */
    { 87,  91,  PUFO_ALIEN_RACE_FLOATER },    /* 0x6AE4C, cases 10..14; 146 too */
    { 92,  94,  PUFO_ALIEN_RACE_MUTON },      /* 0x6AE14, cases 15..17; 145 too */
    { 95,  98,  PUFO_ALIEN_RACE_ETHEREAL },   /* 0x6AE44, cases 18..21; 114 and 147 too */
    { 99,  99,  PUFO_ALIEN_RACE_CYBERDISK },  /* 0x6ADB0, case 22 */
    { 100, 100, PUFO_ALIEN_RACE_CHRYSSALID }, /* 0x6ADB8, case 23 */
    { 101, 101, PUFO_ALIEN_RACE_REAPER },     /* 0x6ADC0, case 24 */
    { 102, 102, PUFO_ALIEN_RACE_SILACOID },   /* 0x6ADC8, the CMP #0x66 before the table */
    { 103, 103, PUFO_ALIEN_RACE_CELATID },    /* 0x6AE0C, the CMP #0x67 */
    { 104, 104, PUFO_ALIEN_RACE_SECTOPOD },   /* 0x6AE04, the CMP #0x68 */
    { 114, 114, PUFO_ALIEN_RACE_ETHEREAL },   /* 0x6AE44 again, the CMP #0x72 */
    { 144, 144, PUFO_ALIEN_RACE_SNAKEMAN },   /* 0x6ADFC again, the CMP #0x90 */
    { 145, 145, PUFO_ALIEN_RACE_MUTON },      /* 0x6AE14 again, the CMP #0x91 */
    { 146, 146, PUFO_ALIEN_RACE_FLOATER },    /* 0x6AE4C again, the CMP #0x92 */
    { 147, 147, PUFO_ALIEN_RACE_ETHEREAL },   /* 0x6AE44 again, the CMP #0x93 */
    { 148, 148, PUFO_ALIEN_RACE_SECTOID },    /* 0x6AE3C again, the CMP #0x94 */
};

PufoAlienRaceId BuySell_ItemIdToAlienRaceId(i32 nSlotId)
{
    for (i32 k = 0; k < (i32)(sizeof(g_aAlienRaceSlots) / sizeof(g_aAlienRaceSlots[0])); ++k) {
        if (nSlotId >= g_aAlienRaceSlots[k].nFirst && nSlotId <= g_aAlienRaceSlots[k].nLast) {
            return g_aAlienRaceSlots[k].nRace;
        }
    }
    return PUFO_ALIEN_RACE_NONE; /* 0x6AE38, MOV R0,#-1 */
}

/* The shared body of Geoscape_UiFocusPrev @ 0x8afa0 and Geoscape_UiFocusNext @ 0x8b1f4. The two
 * originals are 596-byte mirrors that differ only in the mover they call and in which flag byte
 * the 0/2 case writes (Prev stores to unk_1864F4, Next to unk_1864F6 at +2), so one body with a
 * direction flag carries both. The case order and the panel order inside each case follow the
 * disassembly; the addresses below are the case entries in Prev. */
static void Geoscape_FocusScroll(GeoscapeFocusEnv *pEnv, bool bPrev)
{
    if (pEnv == NULL) {
        return;
    }
    switch (pEnv->nScreenId) {
    case 0:
    case 2:
        /* 0x8B004: no panel at all, just MOV R1,#1 / STRB R1,[R0]. */
        if (bPrev) {
            if (pEnv->pbFocusFlagA != NULL) {
                *pEnv->pbFocusFlagA = 1;
            }
        } else if (pEnv->pbFocusFlagB != NULL) {
            *pEnv->pbFocusFlagB = 1;
        }
        return;
    case 4:
        ScrollOnePanel(pEnv->pSolPanelNames, bPrev); /* 0x8B018 */
        ScrollOnePanel(pEnv->pSolPanelCraft, bPrev); /* 0x8B024 */
        return;
    case 6:
        if (pEnv->bPlaneStoresCommitted != 0) { /* 0x8B034, ANDS/BEQ skips the five */
            ScrollPanelSet(pEnv->apPlanePanels, 5, bPrev);
        }
        if (pEnv->bPlaneListPanelsBuilt == 0) { /* 0x8B080, the second gate returns */
            return;
        }
        ScrollOnePanel(pEnv->pPlaneCrewPanel, bPrev);        /* 0x8B094 */
        ScrollOnePanel(pEnv->pPlaneCrewStatusPanel, bPrev);  /* 0x8B0A0 */
        return;
    case 7:
        ScrollOnePanel(pEnv->pBaseFacPanel, bPrev); /* 0x8B0C8 */
        return;
    case 8:
        ScrollPanelSet(pEnv->apResearchPanels, 3, bPrev); /* 0x8B0D4, 0x8B0E4, 0x8B0F4 */
        return;
    case 9:
        ScrollPanelSet(pEnv->apManufPanels, 4, bPrev); /* 0x8B100 .. 0x8B130 */
        return;
    case 10:
    case 11:
    case 12:
        ScrollPanelSet(pEnv->apBuySellPanels, 5, bPrev); /* 0x8B13C .. 0x8B17C */
        return;
    case 16:
        ScrollOnePanel(pEnv->pScreen16Panel, bPrev); /* 0x8B188 */
        return;
    case 17:
        ScrollPanelSet(pEnv->apPsiPanels, 4, bPrev); /* 0x8B198 .. 0x8B1CC, one per base */
        return;
    default:
        return; /* cases 1, 3, 5 and 13..15 fall through to the shared POP */
    }
}

void Geoscape_UiFocusPrev(GeoscapeFocusEnv *pEnv)
{
    Geoscape_FocusScroll(pEnv, true);
}

void Geoscape_UiFocusNext(GeoscapeFocusEnv *pEnv)
{
    Geoscape_FocusScroll(pEnv, false);
}

/* stru_ABBA0 @ 0xABBA0: the four controls the buy/sell screen installs, in the order the
 * UiControl_Setup loop at 0x76E74 consumes them. "up" and "down" are the two scroll arrows the
 * pfnClick stores at 0x76E64 / 0x76E70 name; "bclose" and "emptyb" get their handlers elsewhere. */
static const UiControlDesc g_aBuySellCtlDesc[4] = {
    { 0x00CD, 0x0012, "up",     "sup",     "", 0 }, /* 0xABBA0 */
    { 0x00DC, 0x0026, "down",   "sdown",   "", 0 }, /* 0xABBC0 */
    { 0x0000, 0x0001, "bclose", "bcloseu", "", 0 }, /* 0xABBE0 */
    { 0x003E, 0x0129, "emptyb", "cemptyb", "", 0 }, /* 0xABC00 */
};

/* unk_AAA04 @ 0xAAA04, named g_aBuySellRowOrder in the IDB: the sequence of panel A aux values
 * the sort pass places, read up to nObjectCount - 1 of them. The first eight are the five craft
 * item slots, the soldier marker 1000, and the two crew slots; then the plain object slots with
 * each weapon followed by its two ammunition lines; then the research items, the three armour
 * slots, and the alien rank slots interleaved with their medic slots. 124 entries are non-zero;
 * the shipped map reads all 148 and the rest match nothing. */
#define PUFO_BUYSELL_ORDER_COUNT 148 /* the sort pass reads nObjectCount - 1 of them */
#define PUFO_BUYSELL_SLOT_FLAGS  255 /* the original's v108[255] at 0x76FC8 */

static const i32 g_aBuySellRowOrder[PUFO_BUYSELL_ORDER_COUNT] = {
    117, 118, 119, 120, 121, 1000, 112, 113,
    1, 36, 4, 38, 7, 40, 41, 42,
    8, 43, 44, 45, 10, 46, 47, 48,
    2, 5, 9, 3, 37, 6, 39, 11,
    49, 12, 50, 13, 51, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24,
    25, 52, 26, 53, 27, 54, 28, 29,
    30, 55, 31, 56, 32, 57, 33, 34,
    35, 58, 59, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 69, 122, 123, 124,
    105, 106, 107, 108, 109, 110, 111, 140,
    142, 143, 77, 78, 79, 80, 81, 148,
    82, 83, 84, 85, 86, 144, 87, 88,
    89, 90, 91, 146, 92, 93, 94, 145,
    95, 96, 114, 97, 98, 147, 99, 100,
    101, 102, 103, 104,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

/* The two slot-range tests, from the conditions at 0x77030 and 0x77114. */
static bool BuySellSlotIsStockedItem(i32 nSlot)
{
    return (nSlot > 0 && nSlot < 70) || (nSlot > 104 && nSlot < 112) ||
           (nSlot > 121 && nSlot < 125) || nSlot == 142 || nSlot == 143;
}

static bool BuySellSlotIsAlien(i32 nSlot)
{
    return (nSlot >= 77 && nSlot <= 104) || (nSlot >= 144 && nSlot <= 148) || nSlot == 114;
}

/* 0x77730..0x7776C: the name goes through an 18-byte buffer and is cut to 17 characters when it
 * is longer. The original's buffer is `char v106[17]` followed by `char v107`, and its copy loop
 * also copies the terminator, so a name of 18 characters or more writes one byte past the buffer
 * before the strlen test truncates it. The port bounds the copy instead. */
static void BuySellCopyName(char *pszDst, const char *pszSrc)
{
    i32 i = 0;
    while (i < 18 && pszSrc[i] != 0) {
        pszDst[i] = pszSrc[i];
        ++i;
    }
    pszDst[i < 18 ? i : 17] = 0;
}

static UiPanel *BuySellNewPanel(UiPanel **ppSlot, i32 nLineCount)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel)); /* operator new(0x1BB4) */
    if (pPanel == NULL) {
        return NULL; /* deviation: the original stores through the null panel at 0x77184 */
    }
    if (!UiPanel_ctor(pPanel, nLineCount)) {
        free(pPanel);
        return NULL;
    }
    if (ppSlot != NULL) {
        *ppSlot = pPanel;
    }
    return pPanel;
}

/* A row's "<" and ">" columns and the zero pending quantity, which every row gets the same way. */
static void BuySellFillQtyColumns(UiPanel *pB, UiPanel *pC, UiPanel *pE, i32 nRow)
{
    if (pB != NULL) {
        UiPanel_SetLineText(pB, nRow, "<"); /* unk_ABC24 @ 0xABC24 */
    }
    if (pC != NULL) {
        UiPanel_SetLineText(pC, nRow, ">"); /* unk_ABC20 @ 0xABC20 */
    }
    if (pE != NULL) {
        UiPanel_SetLineAux(pE, nRow, 0);
    }
}

/* The corpse slot each alien race moves with, from the switch at 0x74CA0..0x74D38. */
static i32 BuySellCorpseSlotForRace(PufoAlienRaceId nRace)
{
    switch (nRace) {
    case PUFO_ALIEN_RACE_SECTOID:    return 59; /* 0x74CE8 */
    case PUFO_ALIEN_RACE_SNAKEMAN:   return 60; /* 0x74CF0 */
    case PUFO_ALIEN_RACE_FLOATER:    return 61; /* 0x74CF8 */
    case PUFO_ALIEN_RACE_MUTON:      return 62; /* 0x74D00 */
    case PUFO_ALIEN_RACE_ETHEREAL:   return 63; /* 0x74D0C */
    case PUFO_ALIEN_RACE_CYBERDISK:  return 64; /* 0x74D10 */
    case PUFO_ALIEN_RACE_CHRYSSALID: return 65; /* 0x74D1C */
    case PUFO_ALIEN_RACE_REAPER:     return 66; /* 0x74D24 */
    case PUFO_ALIEN_RACE_CELATID:    return 67; /* 0x74D34 */
    case PUFO_ALIEN_RACE_SILACOID:   return 68; /* 0x74D2C */
    case PUFO_ALIEN_RACE_SECTOPOD:   return 69; /* 0x74D38 */
    default:                         return -1;
    }
}

/* BuySellScreen_CommitTransaction @ 0x74bf8. See the header for the six phases. */
void BuySellScreen_CommitTransaction(BuySellCommitEnv *pEnv)
{
    i32 nCraftRows = 0;
    i32 nSoldierRows = 0;
    i32 i;

    if (pEnv == NULL) {
        return;
    }

    /* Phase 1, 0x74C0C..0x74D74: apply the pending per-slot delta, and move the matching race
     * corpse the other way. SubItemQty with a negated delta is the base gaining. */
    for (i = 0; i < pEnv->nObjectCount; ++i) {
        i32 nDelta = pEnv->anSlotDelta != NULL ? pEnv->anSlotDelta[i] : 0;
        ObjectRecFull *pItem = pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL;
        i32 nCorpse;
        if (pItem != NULL) {
            BaseRec_SubItemQty(pEnv->pBase, pItem, -nDelta); /* 0x74C60 */
        }
        nCorpse = BuySellCorpseSlotForRace(BuySell_ItemIdToAlienRaceId(i));
        if (nCorpse >= 0 && pEnv->apObjects != NULL && nCorpse < pEnv->nObjectCount &&
            pEnv->apObjects[nCorpse] != NULL) {
            BaseRec_AddItemQty(pEnv->pBase, pEnv->apObjects[nCorpse], -nDelta); /* 0x74D3C */
        }
    }

    /* Phase 2, 0x74D88..0x74E64: the selected craft rows into the assigned-ship list. */
    if (pEnv->pPanelA != NULL && pEnv->pPanelC != NULL && pEnv->pPanelE != NULL) {
        for (i = 0; i < pEnv->pPanelA->nLineCount; ++i) {
            i32 nAux = pEnv->pPanelA->anLineAux[i];
            if (nAux >= PUFO_ROW_CRAFT_FIRST && nAux <= PUFO_ROW_CRAFT_LAST
                && pEnv->pPanelE->anLineAux[i] == 1) {
                i32 nIdx = Ship_FindIndexById(pEnv->apCrafts, pEnv->nCraftCount,
                                              pEnv->pPanelC->anLineAux[i]); /* 0x74DF4 */
                if (pEnv->anAssignedList != NULL && nCraftRows < pEnv->nAssignedCapacity) {
                    pEnv->anAssignedList[nCraftRows] = nIdx;
                }
                ++nCraftRows;
            }
        }
    }
    if (pEnv->pnAssignedCount != NULL) {
        *pEnv->pnAssignedCount = nCraftRows; /* 0x74E64 */
    }

    /* Phase 3, 0x74E70..0x74F14: each listed craft unloads its whole cargo into the base. */
    for (i = 0; i < nCraftRows; ++i) {
        i32 nCraftIdx = (pEnv->anAssignedList != NULL && i < pEnv->nAssignedCapacity)
                            ? pEnv->anAssignedList[i] : -1;
        Ship *pCraft = (nCraftIdx >= 0 && pEnv->apCrafts != NULL && nCraftIdx < pEnv->nCraftCount)
                           ? pEnv->apCrafts[nCraftIdx] : NULL;
        i32 s;
        for (s = 0; s < pEnv->nObjectCount; ++s) {
            ObjectRecFull *pItem = pEnv->apObjects != NULL ? pEnv->apObjects[s] : NULL;
            i32 nQty;
            if (pItem == NULL) {
                continue;
            }
            nQty = (nCraftIdx >= 0 && nCraftIdx < PUFO_ITEM_QTY_COUNT)
                       ? (i32)pItem->aItemQty[nCraftIdx] /* 0x74E98 */
                       : 0; /* deviation: the original indexes aItemQty with an unchecked index */
            BaseRec_AddItemQty(pEnv->pBase, pItem, nQty);   /* 0x74EB4 */
            if (pCraft != NULL) {
                InvSel_SubItemQty(pCraft, pItem, (i16)nQty); /* 0x74ED4 */
            }
        }
    }

    /* Phase 4, 0x74F38..0x74FB0: the selected soldier rows into the scratch id list. Only rows
     * marked 1000 qualify, and only when the row is selected. */
    nSoldierRows = 0;
    if (pEnv->pPanelA != NULL && pEnv->pPanelC != NULL && pEnv->pPanelE != NULL) {
        for (i = 0; i < pEnv->pPanelA->nLineCount; ++i) {
            if (pEnv->pPanelA->anLineAux[i] == PUFO_ROW_SOLDIER && pEnv->pPanelE->anLineAux[i] == 1) {
                if (pEnv->anSoldierIdList != NULL && nSoldierRows < pEnv->nSoldierIdCapacity) {
                    pEnv->anSoldierIdList[nSoldierRows] = pEnv->pPanelC->anLineAux[i]; /* 0x74F88 */
                }
                ++nSoldierRows;
            }
        }
    }
    if (pEnv->pnSoldierIdCount != NULL) {
        *pEnv->pnSoldierIdCount = nSoldierRows; /* 0x74FB0 */
    }

    /* Phase 5, 0x74FB8..0x75064: a sold soldier's armour goes back to the base. */
    for (i = 0; i < nSoldierRows; ++i) {
        i32 nRosterId = (pEnv->anSoldierIdList != NULL && i < pEnv->nSoldierIdCapacity)
                            ? pEnv->anSoldierIdList[i] : 0;
        i32 s;
        for (s = 0; s < pEnv->nSoldierCount; ++s) {
            Soldier *pSol = pEnv->apSoldiers != NULL ? pEnv->apSoldiers[s] : NULL;
            if (pSol == NULL || pSol->nUnk440 != nRosterId) {
                continue; /* 0x74FFC, against Soldier.nUnk440 */
            }
            if (pSol->nUnitType != 0 && pEnv->apObjects != NULL && pSol->nUnitType <= 3 &&
                PUFO_SOLDIER_EQUIP_OBJ_BASE + pSol->nUnitType < pEnv->nObjectCount) {
                BaseRec_AddItemQty(pEnv->pBase,
                                   pEnv->apObjects[PUFO_SOLDIER_EQUIP_OBJ_BASE + pSol->nUnitType],
                                   1); /* 0x7502C */
            }
        }
    }

    /* Phase 6, 0x75070..0x75078. */
    if (pEnv->pnFundsSnapshot != NULL) {
        *pEnv->pnFundsSnapshot = pEnv->nFunds;
    }
    Geoscape_CloseUI(pEnv->pClose); /* 0x75078, the tail call */
}

/* The repeat bound both row editors -- BuySellScreen_FillBuyPanel @ 0x75160 and
 * BuySellScreen_FillSellPanel @ 0x75464 -- uses, re-read from the global on every iteration.
 * ORIGINAL BUG, reproduced: 0xAC224 is nRadarPulseTimer, the radar screen's pulse countdown. */
static i32 BuySellRepeatLimit(const i32 *pnTimer)
{
    return pnTimer != NULL ? *pnTimer / 10 : 0;
}

static i32 BuySellRowDelta(const BuySellRowEditEnv *pEnv, i32 nSlot)
{
    if (pEnv->anSlotDelta == NULL || nSlot < 0 || nSlot >= pEnv->nSlotDeltaCount) {
        return 0;
    }
    return pEnv->anSlotDelta[nSlot];
}

static void BuySellRowDeltaAdd(BuySellRowEditEnv *pEnv, i32 nSlot, i32 nDelta)
{
    if (pEnv->anSlotDelta != NULL && nSlot >= 0 && nSlot < pEnv->nSlotDeltaCount) {
        pEnv->anSlotDelta[nSlot] += nDelta;
    }
}

static ObjectRecFull *BuySellObjectAt(ObjectRecFull *const *apObjects, i32 nObjectCount, i32 nSlot)
{
    if (apObjects == NULL || nSlot < 0 || nSlot >= nObjectCount) {
        return NULL;
    }
    return apObjects[nSlot];
}

static ObjectRecFull *BuySellSlotRec(const BuySellRowEditEnv *pEnv, i32 nSlot)
{
    return BuySellObjectAt(pEnv->apObjects, pEnv->nObjectCount, nSlot);
}

/* BuySellScreen_FillSellPanel @ 0x75464, panel C's click: mark one more unit to sell. */
void BuySellScreen_FillSellPanel(BuySellRowEditEnv *pEnv)
{
    i32 nRow;
    i32 nAux;
    i32 nLimit;
    i32 k;

    if (pEnv == NULL || pEnv->pPanelA == NULL || pEnv->pPanelC == NULL
        || pEnv->pPanelD == NULL || pEnv->pPanelE == NULL) {
        return;
    }
    nRow = pEnv->pPanelC->nUnk3032; /* 0x75484, the clicked panel's own row */
    if (nRow < 0 || nRow >= pEnv->pPanelA->nLineCount) {
        return; /* deviation: the original trusts the row */
    }
    nAux = pEnv->pPanelA->anLineAux[nRow];

    if (nAux == PUFO_ROW_SOLDIER || (nAux >= PUFO_ROW_CRAFT_FIRST && nAux <= PUFO_ROW_CRAFT_LAST)) {
        /* A soldier row (0x754A8) and a craft row (0x7558C) share one body. */
        if (pEnv->pPanelD->anLineAux[nRow] <= 0) {
            return;
        }
        UiPanel_SetLineMeta(pEnv->pPanelE, nRow, PUFO_BUYSELL_META_SELL_A,
                            PUFO_BUYSELL_META_SELL_G, PUFO_BUYSELL_META_SELL_A,
                            PUFO_BUYSELL_META_SELL_B, PUFO_BUYSELL_META_SELL_BLUE,
                            PUFO_BUYSELL_META_SELL_B); /* 0x755D8 */
        pEnv->pPanelE->anLineAux[nRow] = 1;            /* 0x755F4, the selection CommitTransaction reads */
        pEnv->pPanelD->anLineAux[nRow] -= 1;           /* 0x755FC */
        return;
    }

    if (nAux > PUFO_ROW_ALIEN_OFFSET) { /* an alien row, 0x75514 */
        if (pEnv->pPanelD->anLineAux[nRow] <= 0) {
            return;
        }
        BuySellRowDeltaAdd(pEnv, nAux - PUFO_ROW_ALIEN_OFFSET, -1); /* 0x75548 */
        pEnv->pPanelE->anLineAux[nRow] += 1;       /* 0x75570 */
        pEnv->pPanelD->anLineAux[nRow] -= 1;       /* 0x755FC */
        return;
    }

    /* A plain object row: the repeat loop, 0x75670..0x7586C. */
    nLimit = BuySellRepeatLimit(pEnv->pnRadarPulseTimer);
    for (k = 0; k <= nLimit; ++k) {
        if (BuySellRowDelta(pEnv, nAux) + Base_GetItemQty(BuySellSlotRec(pEnv, nAux),
                                                          pEnv->nSelectedBaseIdx) <= 0) {
            break; /* 0x75694 */
        }
        if (nAux == PUFO_ROW_ENGINEER || nAux == PUFO_ROW_SCIENTIST) {
            i32 nCrew = (nAux == PUFO_ROW_ENGINEER)
                            ? Base_GetEngineerCount(BuySellSlotRec(pEnv, nAux), pEnv->apManufCol,
                                                    pEnv->nSelectedBaseIdx, pEnv->nCrewEntries)
                            : Base_GetScientistCount(BuySellSlotRec(pEnv, nAux),
                                                     pEnv->apResearchCol, pEnv->nSelectedBaseIdx,
                                                     pEnv->nCrewEntries);
            if (BuySellRowDelta(pEnv, nAux) + nCrew > 0) { /* 0x75750 / 0x75700 */
                BuySellRowDeltaAdd(pEnv, nAux, -1);
                pEnv->pPanelD->anLineAux[nRow] -= 1;
            }
            /* LABEL_25, reached either way for the two crew slots. */
            UiPanel_SetLineMeta(pEnv->pPanelE, nRow, PUFO_BUYSELL_META_SELL_A,
                                PUFO_BUYSELL_META_SELL_G, PUFO_BUYSELL_META_SELL_A,
                                PUFO_BUYSELL_META_SELL_B, PUFO_BUYSELL_META_SELL_BLUE,
                                PUFO_BUYSELL_META_SELL_B); /* 0x7580C */
            pEnv->pPanelE->anLineAux[nRow] = -BuySellRowDelta(pEnv, nAux); /* 0x7582C */
        } else {
            BuySellRowDeltaAdd(pEnv, nAux, -1);          /* 0x756B0 */
            pEnv->pPanelD->anLineAux[nRow] -= 1;         /* 0x756D0 */
            i32 nPrice = (i32)(BuySellSlotRec(pEnv, nAux) != NULL
                                   ? BuySellSlotRec(pEnv, nAux)->uPrice
                                   : 0);
            if (pEnv->pnTransTotal != NULL) {
                *pEnv->pnTransTotal += nPrice; /* 0x757AC, unk_161A2C */
            }
            if (pEnv->pnFunds != NULL) {
                *pEnv->pnFunds += nPrice; /* 0x757BC */
            }
            pEnv->pPanelE->anLineAux[nRow] += nPrice; /* 0x757D4 */
        }
    }
}

/* BuySellScreen_FillBuyPanel @ 0x75160, panel B's click: take one marked unit back. */
void BuySellScreen_FillBuyPanel(BuySellRowEditEnv *pEnv)
{
    i32 nRow;
    i32 nAux;
    i32 nLimit;
    i32 k;

    if (pEnv == NULL || pEnv->pPanelA == NULL || pEnv->pPanelB == NULL
        || pEnv->pPanelD == NULL || pEnv->pPanelE == NULL) {
        return;
    }
    nRow = pEnv->pPanelB->nUnk3032; /* 0x75180 */
    if (nRow < 0 || nRow >= pEnv->pPanelA->nLineCount) {
        return;
    }
    nAux = pEnv->pPanelA->anLineAux[nRow];

    if (nAux == PUFO_ROW_SOLDIER || (nAux >= PUFO_ROW_CRAFT_FIRST && nAux <= PUFO_ROW_CRAFT_LAST)) {
        /* Soldier (0x7519C) and craft (0x75278) rows share one body. */
        if (pEnv->pPanelE->anLineAux[nRow] <= 0) {
            return;
        }
        UiPanel_SetLineMeta(pEnv->pPanelE, nRow, PUFO_BUYSELL_META_BUY_A, PUFO_BUYSELL_META_BUY_A,
                            PUFO_BUYSELL_META_BUY_A, PUFO_BUYSELL_META_BUY_B,
                            PUFO_BUYSELL_META_BUY_B, PUFO_BUYSELL_META_BUY_B); /* 0x752BC */
        pEnv->pPanelE->anLineAux[nRow] = 0;   /* 0x752D8 */
        pEnv->pPanelD->anLineAux[nRow] += 1;  /* 0x752E8 */
        return;
    }

    if (nAux > PUFO_ROW_ALIEN_OFFSET) { /* an alien row, 0x751F4 */
        if (pEnv->pPanelE->anLineAux[nRow] <= 0) {
            return;
        }
        BuySellRowDeltaAdd(pEnv, nAux - PUFO_ROW_ALIEN_OFFSET, 1);  /* 0x75234 */
        pEnv->pPanelE->anLineAux[nRow] -= 1;       /* 0x75250 */
        pEnv->pPanelD->anLineAux[nRow] += 1;       /* 0x752E8 */
        return;
    }

    /* A plain object row, 0x7533C..0x75458 of BuySellScreen_FillBuyPanel @ 0x75160.
     * ORIGINAL BUG, reproduced: the guard only skips the body and keeps the loop turning, unlike
     * the sell side, which breaks out of it, and the money subtraction has no floor, so a row
     * whose pending money is below one unit's price ends up negative. */
    nLimit = BuySellRepeatLimit(pEnv->pnRadarPulseTimer);
    for (k = 0; k <= nLimit; ++k) {
        if (pEnv->pPanelE->anLineAux[nRow] <= 0) {
            continue; /* 0x75350 */
        }
        pEnv->pPanelD->anLineAux[nRow] += 1; /* 0x7537C */
        BuySellRowDeltaAdd(pEnv, nAux, 1);   /* 0x7538C */
        if (nAux == PUFO_ROW_ENGINEER || nAux == PUFO_ROW_SCIENTIST) {
            UiPanel_SetLineMeta(pEnv->pPanelE, nRow, PUFO_BUYSELL_META_BUY_A,
                                PUFO_BUYSELL_META_BUY_A, PUFO_BUYSELL_META_BUY_A,
                                PUFO_BUYSELL_META_BUY_B, PUFO_BUYSELL_META_BUY_B,
                                PUFO_BUYSELL_META_BUY_B); /* 0x75418 */
            pEnv->pPanelE->anLineAux[nRow] -= 1; /* 0x75428 */
        } else {
            i32 nPrice = (i32)(BuySellSlotRec(pEnv, nAux) != NULL
                                   ? BuySellSlotRec(pEnv, nAux)->uPrice
                                   : 0);
            if (pEnv->pnTransTotal != NULL) {
                *pEnv->pnTransTotal -= nPrice; /* 0x753B4, unk_161A2C */
            }
            if (pEnv->pnFunds != NULL) {
                *pEnv->pnFunds -= nPrice; /* 0x753C4 */
            }
            pEnv->pPanelE->anLineAux[nRow] -= nPrice; /* 0x753D8 */
        }
    }
}

/* 0x75F28 / 0x75F48 / 0x75F64 / 0x75F74 / 0x75F90: the five ways a pass can fail. The flag is set
 * only when it is still clear on the hangar, stores and product-price paths, and unconditionally on
 * the living-quarters and crew-price paths; the reason code always lands in dword_1870B4. */
static void BuySellSetShortfallFlag(const BuySellCapacityEnv *pEnv, bool bOnlyIfClear)
{
    if (pEnv->pdwShortfall == NULL) {
        return;
    }
    if (!bOnlyIfClear || *pEnv->pdwShortfall == 0) {
        *pEnv->pdwShortfall = 1;
    }
}

static i32 BuySellShortfall(const BuySellCapacityEnv *pEnv, i32 nReason, bool bOnlyIfClear)
{
    BuySellSetShortfallFlag(pEnv, bOnlyIfClear);
    if (pEnv->pnResidue != NULL) {
        *pEnv->pnResidue = nReason;
    }
    return nReason;
}

/* 0x75F64: the product-price shortfall stores the flag's own value as the reason -- 1 either way,
 * because nothing in the program writes a different one into byte_1870C8. */
static i32 BuySellFundsShortfall(const BuySellCapacityEnv *pEnv)
{
    BuySellSetShortfallFlag(pEnv, true);
    if (pEnv->pnResidue != NULL) {
        *pEnv->pnResidue = (pEnv->pdwShortfall != NULL) ? (i32)*pEnv->pdwShortfall
                                                        : PUFO_BUYSELL_SHORTFALL_FUNDS;
    }
    return PUFO_BUYSELL_SHORTFALL_FUNDS;
}

/* The transfer flow's working site: the record AT the site count, which Transfer_ResetSite
 * (0x9690c, not ported) clears and BuySellScreen_ConfirmSiteAndClose commits. */
static SmapSite *BuySellSiteAt(SmapSite *aSites, i32 nSiteCount)
{
    if (aSites == NULL || nSiteCount < 0 || nSiteCount >= PUFO_SMAP_SITE_COUNT) {
        return NULL; /* deviation: the original indexes aSmapSites[nSiteCount] blindly */
    }
    return &aSites[nSiteCount];
}

static SmapSite *BuySellWorkingSite(const BuySellCapacityEnv *pEnv)
{
    return BuySellSiteAt(pEnv->aSites, pEnv->nSiteCount);
}

static void BuySellPanelCountAdd(const BuySellCapacityEnv *pEnv, i32 nRow, i32 nDelta)
{
    if (pEnv->pPanelD == NULL || nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return;
    }
    pEnv->pPanelD->anLineAux[nRow] += nDelta;
}

/* BuySellScreen_ValidateCapacity @ 0x75b88, the buy screen's ">" control: queue one more unit to
 * buy, if the base can take it. The header carries the branch map. */
i32 BuySellScreen_ValidateCapacity(const BuySellCapacityEnv *pEnv)
{
    i32 nRow;
    i32 nAux;
    i32 nLimit;
    i32 k;

    if (pEnv == NULL || pEnv->pPanelA == NULL || pEnv->pPanelD == NULL || pEnv->pnRowCursor == NULL) {
        return PUFO_BUYSELL_SHORTFALL_NONE;
    }
    nRow = *pEnv->pnRowCursor;
    if (nRow < 0 || nRow >= pEnv->pPanelA->nLineCount) {
        return PUFO_BUYSELL_SHORTFALL_NONE; /* deviation: the original trusts the row */
    }
    nAux = pEnv->pPanelA->anLineAux[nRow];
    nLimit = BuySellRepeatLimit(pEnv->pnRadarPulseTimer);
    if (nLimit < 0) {
        return PUFO_BUYSELL_SHORTFALL_NONE;
    }

    for (k = 0; k <= nLimit; ++k) {
        SmapSite *pSite = BuySellWorkingSite(pEnv);

        if (nAux == PUFO_ROW_SOLDIER || nAux == PUFO_ROW_ENGINEER || nAux == PUFO_ROW_SCIENTIST) {
            const i32 nOccupied = Base_CalcOccupiedSpace(pEnv->apSoldiers, pEnv->nSoldierCount,
                                                         pEnv->apObjects, pEnv->nObjectCount,
                                                         pEnv->nSelectedBaseIdx); /* 0x75E2C */
            const i32 nOutbound = Base_CountOutboundTransferSpace(pEnv->aSites, pEnv->nSiteCount,
                                                                  pEnv->nSiteCount, pEnv->apBases,
                                                                  pEnv->nSelectedBaseIdx,
                                                                  true); /* 0x75E48 */
            const i32 nCap = Base_CalcLivingQuartersCap(pEnv->abFacilityGrid,
                                                        pEnv->nSelectedBaseIdx); /* 0x75E54 */

            if (nOutbound + nOccupied + 1 > nCap) { /* 0x75E60 */
                return BuySellShortfall(pEnv, PUFO_BUYSELL_SHORTFALL_LIVING, false);
            }

            if (nAux == PUFO_ROW_ENGINEER || nAux == PUFO_ROW_SCIENTIST) {
                const i32 nPrice = (nAux == PUFO_ROW_ENGINEER) ? PUFO_BUYSELL_PRICE_ENGINEER
                                                               : PUFO_BUYSELL_PRICE_SCIENTIST;
                if (pEnv->pnFunds == NULL || *pEnv->pnFunds < nPrice) { /* 0x75FB0, 0x76018 */
                    BuySellSetShortfallFlag(pEnv, false); /* 0x75F74 */
                    if (pEnv->pnResidue != NULL) {
                        *pEnv->pnResidue = PUFO_BUYSELL_SHORTFALL_FUNDS;
                    }
                    return PUFO_BUYSELL_SHORTFALL_FUNDS;
                }
                *pEnv->pnFunds -= nPrice;                        /* 0x75FBC, 0x76030 */
                BuySellPanelCountAdd(pEnv, nRow, 1);             /* 0x75FDC, 0x76050 */
                if (pEnv->pnTransTotal != NULL) {
                    *pEnv->pnTransTotal += nPrice;               /* 0x75FEC, 0x76060 */
                }
                if (pSite != NULL) {
                    pSite->auItemQty[nAux] = (u16)(pSite->auItemQty[nAux] + 1); /* 0x75FF8, 0x76078 */
                }
                /* 0x76008 / 0x7607C: both crew paths return out of the whole function, so one click
                 * queues at most one crew member however long the loop is. */
                return PUFO_BUYSELL_SHORTFALL_NONE;
            }

            if (pEnv->pnFunds == NULL || *pEnv->pnFunds < PUFO_BUYSELL_PRICE_SOLDIER) { /* 0x75E84 */
                BuySellSetShortfallFlag(pEnv, false); /* 0x75F74 */
                if (pEnv->pnResidue != NULL) {
                    *pEnv->pnResidue = PUFO_BUYSELL_SHORTFALL_FUNDS;
                }
                return PUFO_BUYSELL_SHORTFALL_FUNDS;
            }
            if (pEnv->pnSoldierHireCount != NULL) {
                *pEnv->pnSoldierHireCount += 1; /* 0x75E94, unk_159D90 */
            }
            *pEnv->pnFunds -= PUFO_BUYSELL_PRICE_SOLDIER;        /* 0x75E9C */
            BuySellPanelCountAdd(pEnv, nRow, 1);                 /* 0x75EC0 */
            if (pEnv->pnTransTotal != NULL) {
                *pEnv->pnTransTotal += PUFO_BUYSELL_PRICE_SOLDIER; /* 0x75ED8 */
            }
            if (pSite != NULL) {
                pSite->uRecoveredSoldiers = (u16)(pSite->uRecoveredSoldiers + 1); /* 0x75EF0 */
            }
            continue; /* 0x75EF0 -> LABEL_22 */
        }

        if (nAux == PUFO_ROW_CRAFT_FIRST || nAux == PUFO_ROW_CRAFT_SECOND) {
            const i32 nShips = Base_CountShipsAtBase(pEnv->apCraft, pEnv->nCraftCount, pEnv->apBases,
                                                     pEnv->nBaseCount, pEnv->abGridBytes,
                                                     pEnv->nSelectedBaseIdx); /* 0x75C4C */
            const i32 nInbound = Base_CountInboundTransfers(pEnv->aSites, pEnv->nSiteCount,
                                                            pEnv->nSiteCount, pEnv->apBases,
                                                            pEnv->nSelectedBaseIdx,
                                                            true); /* 0x75C68 */
            const i32 nCap = Base_CalcHangarCap(pEnv->abFacilityGrid,
                                                pEnv->nSelectedBaseIdx); /* 0x75C74 */
            if (nInbound + nShips + 1 > nCap) { /* 0x75C80 */
                return BuySellShortfall(pEnv, PUFO_BUYSELL_SHORTFALL_HANGAR, true);
            }
        } else {
            const i32 nUsed = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount,
                                                   pEnv->nSelectedBaseIdx); /* 0x75CBC */
            const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount,
                                                             pEnv->nSiteCount, pEnv->apBases,
                                                             pEnv->nSelectedBaseIdx, pEnv->apObjects,
                                                             pEnv->nObjectCount, true); /* 0x75CE0 */
            const i32 nCap = Base_CalcStoresCapacity(pEnv->abFacilityGrid,
                                                     pEnv->nSelectedBaseIdx); /* 0x75CF8 */
            const ObjectRecFull *pRec = BuySellObjectAt(pEnv->apObjects, pEnv->nObjectCount, nAux);
            const i32 nCargo = (pRec != NULL) ? (i32)pRec->bCargoSize : 0;
            if (nAcross + nCargo + nUsed > nCap) { /* 0x75D0C */
                return BuySellShortfall(pEnv, PUFO_BUYSELL_SHORTFALL_STORES, true);
            }
        }

        {
            const ObjectRecFull *pRec = BuySellObjectAt(pEnv->apObjects, pEnv->nObjectCount, nAux);
            const i32 nPrice = (pRec != NULL) ? (i32)pRec->uPrice : 0;
            const i32 nFunds = (pEnv->pnFunds != NULL) ? *pEnv->pnFunds : 0;

            if ((u32)nPrice > (u32)nFunds) { /* 0x75D28, an unsigned compare */
                return BuySellFundsShortfall(pEnv);
            }
            if (nAux == PUFO_ROW_CRAFT_FIRST || nAux == PUFO_ROW_CRAFT_SECOND) {
                if (pSite != NULL) {
                    pSite->bUnk518 = (u8)(pSite->bUnk518 + 1); /* 0x75D54, read as a signed byte */
                }
            }
            BuySellPanelCountAdd(pEnv, nRow, 1); /* 0x75D84 */
            if (pSite != NULL && nAux >= 0 && nAux < (i32)(sizeof(pSite->auItemQty) /
                                                           sizeof(pSite->auItemQty[0]))) {
                pSite->auItemQty[nAux] = (u16)(pSite->auItemQty[nAux] + 1); /* 0x75DB8 */
            }
            if (pEnv->pnFunds != NULL) {
                *pEnv->pnFunds = nFunds - nPrice; /* 0x75DC8 */
            }
            if (pEnv->pnTransTotal != NULL) {
                *pEnv->pnTransTotal += nPrice; /* 0x75DD8 */
            }
        }
    }
    return PUFO_BUYSELL_SHORTFALL_NONE;
}

/* BuySellScreen_UpdatePrices @ 0x75878, the buy screen's "<" control: take one queued unit back. No
 * capacity test at all -- panel D's count is the only guard, and a zero count just skips the body. */
i32 BuySellScreen_UpdatePrices(const BuySellCapacityEnv *pEnv)
{
    i32 nRow;
    i32 nAux;
    i32 nLimit;
    i32 k;

    if (pEnv == NULL || pEnv->pPanelA == NULL || pEnv->pPanelD == NULL || pEnv->pnRowCursor == NULL) {
        return PUFO_BUYSELL_SHORTFALL_NONE;
    }
    nRow = *pEnv->pnRowCursor;
    if (nRow < 0 || nRow >= pEnv->pPanelA->nLineCount) {
        return PUFO_BUYSELL_SHORTFALL_NONE;
    }
    nAux = pEnv->pPanelA->anLineAux[nRow];
    nLimit = BuySellRepeatLimit(pEnv->pnRadarPulseTimer);
    if (nLimit < 0) {
        return PUFO_BUYSELL_SHORTFALL_NONE;
    }

    for (k = 0; k <= nLimit; ++k) {
        if (pEnv->pPanelD->anLineAux[nRow] <= 0) { /* 0x75928, 0x75A08 */
            continue;
        }
        if (nAux == PUFO_ROW_ENGINEER || nAux == PUFO_ROW_SCIENTIST) {
            const i32 nPrice = (nAux == PUFO_ROW_ENGINEER) ? PUFO_BUYSELL_PRICE_ENGINEER
                                                           : PUFO_BUYSELL_PRICE_SCIENTIST;
            SmapSite *pSite = BuySellWorkingSite(pEnv);

            if (pEnv->pnFunds != NULL) {
                *pEnv->pnFunds += nPrice; /* 0x75B38, 0x75ADC */
            }
            if (pEnv->pnTransTotal != NULL) {
                *pEnv->pnTransTotal -= nPrice; /* 0x75B44, 0x75AE8 */
            }
            BuySellPanelCountAdd(pEnv, nRow, -1); /* 0x75B5C, 0x75B00 */
            if (pSite != NULL) {
                pSite->auItemQty[nAux] = (u16)(pSite->auItemQty[nAux] - 1); /* 0x75B7C, 0x75B20 */
            }
            return PUFO_BUYSELL_SHORTFALL_NONE; /* 0x75B7C / 0x75B24: out of the whole function */
        }

        if (nAux == PUFO_ROW_SOLDIER) {
            SmapSite *pSite = BuySellWorkingSite(pEnv);

            if (pEnv->pnSoldierHireCount != NULL) {
                *pEnv->pnSoldierHireCount -= 1; /* 0x75A24 */
            }
            if (pEnv->pnFunds != NULL) {
                *pEnv->pnFunds += PUFO_BUYSELL_PRICE_SOLDIER; /* 0x75A38 */
            }
            if (pEnv->pnTransTotal != NULL) {
                *pEnv->pnTransTotal -= PUFO_BUYSELL_PRICE_SOLDIER; /* 0x75A3C */
            }
            BuySellPanelCountAdd(pEnv, nRow, -1); /* 0x75A50 */
            if (pSite != NULL) {
                pSite->uRecoveredSoldiers = (u16)(pSite->uRecoveredSoldiers - 1); /* 0x75A80 */
            }
            continue;
        }

        {
            SmapSite *pSite = BuySellWorkingSite(pEnv);
            const ObjectRecFull *pRec = BuySellObjectAt(pEnv->apObjects, pEnv->nObjectCount, nAux);
            const i32 nPrice = (pRec != NULL) ? (i32)pRec->uPrice : 0;

            BuySellPanelCountAdd(pEnv, nRow, -1); /* 0x75938 */
            if (pSite != NULL) {
                if (nAux == PUFO_ROW_CRAFT_FIRST || nAux == PUFO_ROW_CRAFT_SECOND) {
                    pSite->bUnk518 = (u8)(pSite->bUnk518 - 1); /* 0x75974, read as a signed byte */
                }
                if (nAux >= 0 && nAux < (i32)(sizeof(pSite->auItemQty) / sizeof(pSite->auItemQty[0]))) {
                    pSite->auItemQty[nAux] = (u16)(pSite->auItemQty[nAux] - 1); /* 0x759A0 */
                }
            }
            if (pEnv->pnFunds != NULL) {
                *pEnv->pnFunds += nPrice; /* 0x759B8 */
            }
            if (pEnv->pnTransTotal != NULL) {
                *pEnv->pnTransTotal -= nPrice; /* 0x759C8 */
            }
        }
    }
    return PUFO_BUYSELL_SHORTFALL_NONE;
}

static PadImage *BuySellPadNew(void)
{
    /* deviation: the original's operator new(0x14) leaves nOriginX / nOriginY / uWidth / uHeight
     * uninitialised -- PadImage_ctor @ 0x11000 only clears pData -- and the port zeroes the block. */
    PadImage *pPad = (PadImage *)calloc(1, sizeof(PadImage)); /* 0x78C4C */

    if (pPad == NULL) {
        return NULL;
    }
    PadImage_ctor(pPad); /* 0x78C5C */
    return pPad;
}

/* BuySellScreen_Init @ 0x78c30: the bring-up both BuySellScreen_OpenBuy and BuySellScreen_OpenSell
 * tail call. The header carries the branch map. */
void BuySellScreen_Init(const BuySellInitEnv *pEnv)
{
    PadImage *pPad;
    i32 i;

    if (pEnv == NULL || pEnv->ppScreen == NULL) {
        return;
    }

    if (pEnv->pnFundsSnapshot != NULL && pEnv->pnFunds != NULL) {
        *pEnv->pnFundsSnapshot = *pEnv->pnFunds; /* 0x78C44 */
    }

    pPad = BuySellPadNew();
    *pEnv->ppScreen = pPad; /* 0x78C70: the ctor's result, not the raw allocation */

    (void)Palette_Load(PUFO_BUYSSELL_PALETTE_NAME, PUFO_BUYSSELL_PALETTE_SLOT); /* 0x78C7C */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_BUYSSELL_PAD_NAME, PUFO_BUYSSELL_PALETTE_SLOT); /* 0x78C8C */
        pPad->nOriginX = 0; /* 0x78C98 */
        pPad->nOriginY = 0; /* 0x78CA4 */
    }

    if (pEnv->pnTransTotal != NULL) {
        *pEnv->pnTransTotal = 0; /* 0x78CAC */
    }
    if (pEnv->pnSoldierHireCount != NULL) {
        *pEnv->pnSoldierHireCount = 0; /* 0x78CB4 */
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->x = 224; /* 0x78CBC, a STRH of 0xE0 */
        pEnv->pCloseCtl->y = 1;   /* 0x78CC8 */
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseClick; /* 0x78CD4 */
    }
    if (pEnv->pConfirmCtl != NULL) {
        pEnv->pConfirmCtl->x = (i16)(120 - (pEnv->pConfirmCtl->uWidth >> 1)); /* 0x78CE8 */
        pEnv->pConfirmCtl->y = 297; /* 0x78CF8: 0x128 built as ORR #1 */
    }

    if (pEnv->anSlotDelta != NULL) {
        for (i = 0; i < pEnv->nSlotDeltaCount; ++i) {
            pEnv->anSlotDelta[i] = 0; /* 0x78D3C, over Map_nObjectCount entries */
        }
    }

    if (pEnv->nScreenId == PUFO_BUYSSELL_SCREEN_BUY) {
        if (pEnv->pConfirmCtl != NULL) {
            pEnv->pConfirmCtl->pfnClick = pEnv->pfnConfirmBuy; /* 0x78D60 */
        }
        if (pEnv->pfnBuildControls != NULL) {
            pEnv->pfnBuildControls(); /* 0x78D64 */
        }
    } else if (pEnv->nScreenId == PUFO_BUYSSELL_SCREEN_SELL) {
        /* 0x78D70..0x78D7C is LDREQ / STREQ / BLEQ: both the handler store and the call are
         * conditional on the id being read a second time. */
        if (pEnv->pConfirmCtl != NULL) {
            pEnv->pConfirmCtl->pfnClick = pEnv->pfnConfirmSell;
        }
        if (pEnv->pfnBuildPanels != NULL) {
            pEnv->pfnBuildPanels();
        }
    }
}

/* The selection sort both buy/sell builders end with: each g_aBuySellRowOrder entry places one row,
 * and the scan does not stop at the match, so a repeated aux value swaps again from the next slot --
 * with unique aux values that second scan finds nothing. BuildPanels at 0x77C48, BuildControls at
 * 0x76AF0; both swap the five panels together in the order A, E, B, C, D. */
static void BuySellSortRows(UiPanel *pA, UiPanel *pE, UiPanel *pB, UiPanel *pC, UiPanel *pD,
                            i32 nLimit)
{
    i32 nPlaced = 0;
    i32 i;

    if (nLimit > PUFO_BUYSELL_ORDER_COUNT) {
        nLimit = PUFO_BUYSELL_ORDER_COUNT; /* the table has 148 entries and the original does not bound */
    }
    for (i = 0; i < nLimit; ++i) {
        const i32 nWant = g_aBuySellRowOrder[i];
        i32 r;

        if (pA == NULL) {
            break;
        }
        for (r = 0; r < pA->nLineCount; ++r) {
            if (pA->anLineAux[r] == nWant) {
                UiPanel_SwapLines(pA, nPlaced, r); /* 0x77CB4 / 0x76B48 */
                UiPanel_SwapLines(pE, nPlaced, r); /* 0x77CCC / 0x76B60 */
                UiPanel_SwapLines(pB, nPlaced, r); /* 0x77CE4 / 0x76B78 */
                UiPanel_SwapLines(pC, nPlaced, r); /* 0x77CFC / 0x76B8C */
                UiPanel_SwapLines(pD, nPlaced, r); /* 0x77D10 / 0x76BA0 */
                ++nPlaced;
            }
        }
    }
}

/* BuySellScreen_BuildPanels @ 0x76de0. See the header for the branch map and the row model. */
void BuySellScreen_BuildPanels(BuySellBuildEnv *pEnv)
{
    i32 abInclude[PUFO_BUYSELL_SLOT_FLAGS]; /* slot -> "this slot gets a row" */
    UiPanel *pA, *pB, *pC, *pD, *pE;
    UiPanel *apFree[5];
    char szName[18];
    char szLine[PUFO_PANEL_LINE_BYTES];
    i32 nRows = 0;
    i32 nRow = 0;
    i32 nEngineers = 0;
    i32 nScientists = 0;
    i32 i;

    if (pEnv == NULL || pEnv->pbPanelsBuilt == NULL) {
        return;
    }

    if (*pEnv->pbPanelsBuilt != 0) {
        /* The teardown branch, 0x77D60..0x77E50. */
        *pEnv->pbPanelsBuilt = 0;
        for (i = 0; i < 4; ++i) {
            if (pEnv->apCtl[i] != NULL) {
                UiControl_FreePads(pEnv->apCtl[i]); /* 0x77D80 */
            }
            /* 0x77D88 also operator delete's the control; the port's controls are caller-owned. */
        }
        apFree[0] = pEnv->ppPanelA != NULL ? *pEnv->ppPanelA : NULL;
        apFree[1] = pEnv->ppPanelB != NULL ? *pEnv->ppPanelB : NULL;
        apFree[2] = pEnv->ppPanelE != NULL ? *pEnv->ppPanelE : NULL;
        apFree[3] = pEnv->ppPanelC != NULL ? *pEnv->ppPanelC : NULL;
        apFree[4] = pEnv->ppPanelD != NULL ? *pEnv->ppPanelD : NULL;
        for (i = 0; i < 5; ++i) {
            if (apFree[i] != NULL) {
                UiPanel_FreeLines(apFree[i]); /* 0x77DB0, 0x77DD0, 0x77DF0, 0x77E10, 0x77E30 */
                free(apFree[i]);
            }
        }
        if (pEnv->ppPanelA != NULL) *pEnv->ppPanelA = NULL;
        if (pEnv->ppPanelB != NULL) *pEnv->ppPanelB = NULL;
        if (pEnv->ppPanelE != NULL) *pEnv->ppPanelE = NULL;
        if (pEnv->ppPanelC != NULL) *pEnv->ppPanelC = NULL;
        if (pEnv->ppPanelD != NULL) *pEnv->ppPanelD = NULL;
        if (pEnv->pnFunds != NULL && *pEnv->pnFunds != pEnv->nFundsSnapshot) {
            *pEnv->pnFunds = pEnv->nFundsSnapshot; /* 0x77E50 */
        }
        return;
    }

    *pEnv->pbPanelsBuilt = 1; /* 0x76E0C */

    for (i = 0; i < 4; ++i) {
        if (pEnv->apCtl[i] != NULL) {
            UiControl_ctor(pEnv->apCtl[i]); /* 0x76E28, on the freshly allocated block */
        }
    }
    if (pEnv->apCtl[0] != NULL) {
        pEnv->apCtl[0]->pfnClick = pEnv->pfnPanelPrev; /* 0x76E64 */
    }
    if (pEnv->apCtl[1] != NULL) {
        pEnv->apCtl[1]->pfnClick = pEnv->pfnPanelNext; /* 0x76E70 */
    }
    for (i = 0; i < 4; ++i) {
        if (pEnv->apCtl[i] != NULL) {
            UiControl_Setup(pEnv->apCtl[i], 0, 0, &g_aBuySellCtlDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x76E74..0x76E94 */
        }
    }

    if (pEnv->pnAssignedShipCount != NULL) {
        *pEnv->pnAssignedShipCount = 0; /* 0x76EA0 */
    }
    if (pEnv->pnSoldierIdCount != NULL) {
        *pEnv->pnSoldierIdCount = 0; /* 0x76EAC */
    }

    /* Count pass 1, 0x76EC0..0x76F10: soldiers at this base that no craft carries. */
    for (i = 0; i < pEnv->nSoldierCount; ++i) {
        const Soldier *pSol = pEnv->apSoldiers != NULL ? pEnv->apSoldiers[i] : NULL;
        if (pSol == NULL) {
            continue; /* deviation: the original dereferences the array slot blindly */
        }
        if ((i32)pSol->nKind == (i32)pEnv->nSelectedBaseIdx && pSol->bUnk183 == 0xFF) {
            ++nRows;
        }
    }

    /* Count pass 2, 0x76F18..0x76FA4: craft based here, empty, and present. */
    for (i = 0; i < pEnv->nCraftCount; ++i) {
        const Ship *pShip = pEnv->apCrafts != NULL ? pEnv->apCrafts[i] : NULL;
        if (pShip == NULL) {
            continue;
        }
        if ((i32)pShip->bHomeBase == (i32)pEnv->nSelectedBaseIdx &&
            Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount, i) == 0 &&
            pShip->bAtBase != 0) {
            ++nRows;
        }
    }

    /* Count pass 3, 0x76FB4..0x770AC: the stocked item slots, plus the two crew slots. */
    memset(abInclude, 0, sizeof(abInclude));
    for (i = 1; i < pEnv->nObjectCount && i < PUFO_BUYSELL_SLOT_FLAGS; ++i) {
        if (BuySellSlotIsStockedItem(i) &&
            Base_GetItemQty(pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL,
                            pEnv->nSelectedBaseIdx) != 0) {
            abInclude[i] = 1;
            ++nRows;
        }
        if (i == PUFO_ROW_ENGINEER) {
            nEngineers = Base_GetEngineerCount(pEnv->apObjects != NULL ? pEnv->apObjects[112] : NULL,
                                               pEnv->apManufCol, pEnv->nSelectedBaseIdx,
                                               pEnv->nCrewEntries);
            if (nEngineers <= 0) {
                continue; /* 0x77050, the goto LABEL_42: this slot gets no row */
            }
            abInclude[112] = 1;
            ++nRows;
        }
        if (i == PUFO_ROW_SCIENTIST) {
            nScientists = Base_GetScientistCount(pEnv->apObjects != NULL ? pEnv->apObjects[113] : NULL,
                                                 pEnv->apResearchCol, pEnv->nSelectedBaseIdx,
                                                 pEnv->nCrewEntries);
            if (nScientists > 0) {
                abInclude[113] = 1;
                ++nRows;
            }
        }
    }

    /* Count pass 4, 0x770B0..0x7712C: the alien slots the base holds. */
    for (i = 1; i < pEnv->nObjectCount && i < PUFO_BUYSELL_SLOT_FLAGS; ++i) {
        if (BuySellSlotIsAlien(i) &&
            Base_GetItemQty(pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL,
                            pEnv->nSelectedBaseIdx) != 0) {
            abInclude[i] = 1;
            ++nRows;
        }
    }

    /* The five panels, 0x77138..0x776B0. All five take the same row total. */
    pA = BuySellNewPanel(pEnv->ppPanelA, nRows);
    if (pA != NULL) {
        pA->nStyle = 13;                            /* 0x77184 */
        pA->nFont = 16;                             /* 0x77198 */
        pA->pfnClick = pEnv->pfnSyncSelection;     /* 0x771B4, BuySellScreen_SyncSelection */
        pA->nPosX = 10;                             /* 0x771C8 */
        pA->nPosY = 70;                             /* 0x771DC */
        pA->nUnk7052 = 5;                           /* 0x771F0 */
        pA->nUnk7056 = 70;                          /* 0x77204 */
        pA->nViewW = 125;                           /* 0x7720C */
        UiPanel_SetBytes0to2(pA, 0xB4, 0xB4, 0xB4);                /* 0x77218 */
        UiPanel_SetBytes6to11(pA, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14); /* 0x7723C */
        pA->bFlag7073 = 1;                          /* 0x77250 */
        pA->bUnk7088 = (u8)0x96;                    /* 0x77264, MOV R0,#0xFFFFFF96 */
    }
    pE = BuySellNewPanel(pEnv->ppPanelE, nRows);
    if (pE != NULL) {
        pE->nStyle = 13;
        pE->nFont = 16;
        pE->pfnClick = NULL;                        /* 0x772CC */
        pE->nPosX = 232;
        pE->nPosY = 70;
        pE->nUnk7052 = 190;
        pE->nUnk7056 = 70;
        pE->nViewW = 45;
        UiPanel_SetBytes0to2(pE, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pE, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pE->bFlag7073 = 0;
        pE->bFlag7074 = 1;
        pE->nUnk7076 = 2;
    }
    pB = BuySellNewPanel(pEnv->ppPanelB, nRows);
    if (pB != NULL) {
        pB->nStyle = 13;
        pB->nFont = 16;
        pB->pfnClick = pEnv->pfnFillBuyPanel;       /* 0x773D0, BuySellScreen_FillBuyPanel */
        pB->nPosX = 164;
        pB->nPosY = 70;
        pB->nUnk7052 = 160;
        pB->nUnk7056 = 70;
        pB->nViewW = 15;
        UiPanel_SetBytes0to2(pB, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pB, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pB->bFlag7073 = 1;
        UiPanel_SetBytes3to5(pB, 0x32, 0x32, 0xFA); /* 0x7745C */
        pB->nUnk7084 = 5;
        pB->bUnk7088 = (u8)0x96;
    }
    pC = BuySellNewPanel(pEnv->ppPanelC, nRows);
    if (pC != NULL) {
        pC->nStyle = 13;
        pC->nFont = 16;
        pC->pfnClick = pEnv->pfnFillSellPanel;      /* 0x774EC, BuySellScreen_FillSellPanel */
        pC->nPosX = 180;
        pC->nPosY = 70;
        pC->nUnk7052 = 176;
        pC->nUnk7056 = 70;
        pC->nViewW = 15;
        UiPanel_SetBytes0to2(pC, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pC, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pC->bFlag7073 = 1;
        UiPanel_SetBytes3to5(pC, 0x32, 0x32, 0xFA);
        pC->nUnk7084 = 5;
        pC->bUnk7088 = (u8)0x96;
    }
    pD = BuySellNewPanel(pEnv->ppPanelD, nRows);
    if (pD != NULL) {
        pD->nStyle = 13;
        pD->nFont = 16;
        pD->pfnClick = NULL;                        /* 0x77608 */
        pD->nPosX = 158;
        pD->nPosY = 70;
        pD->nUnk7052 = 130;
        pD->nUnk7056 = 70;
        pD->nViewW = 50;
        UiPanel_SetBytes0to2(pD, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pD, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pD->bFlag7073 = 0;
        pD->bFlag7074 = 1;
        pD->nUnk7076 = 2;
    }

    /* Fill pass 1, 0x776D8..0x77860: the soldier rows. */
    for (i = 0; i < pEnv->nSoldierCount; ++i) {
        const Soldier *pSol = pEnv->apSoldiers != NULL ? pEnv->apSoldiers[i] : NULL;
        if (pSol == NULL) {
            continue;
        }
        if ((i32)pSol->nKind != (i32)pEnv->nSelectedBaseIdx || pSol->bUnk183 != 0xFF) {
            continue;
        }
        BuySellCopyName(szName, pSol->szName);
        if (pA != NULL) {
            UiPanel_SetLineText(pA, nRow, szName);
            UiPanel_SetLineAux(pA, nRow, PUFO_ROW_SOLDIER); /* the soldier row marker */
        }
        if (pD != NULL) {
            UiPanel_SetLineAux(pD, nRow, 1);
        }
        BuySellFillQtyColumns(pB, pC, pE, nRow);
        if (pC != NULL) {
            UiPanel_SetLineAux(pC, nRow, pSol->nUnk440); /* the roster id the sell path removes by */
        }
        if (pB != NULL) {
            UiPanel_SetLineAux(pB, nRow, i); /* the index into apSoldiers */
        }
        ++nRow;
    }

    /* Fill pass 2, 0x77880..0x77A38: the craft rows. TEXTS[415 + bCraftType] + "-" + bType. */
    for (i = 0; i < pEnv->nCraftCount; ++i) {
        const Ship *pShip = pEnv->apCrafts != NULL ? pEnv->apCrafts[i] : NULL;
        if (pShip == NULL) {
            continue;
        }
        if ((i32)pShip->bHomeBase != (i32)pEnv->nSelectedBaseIdx ||
            Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount, i) != 0 ||
            pShip->bAtBase == 0) {
            continue;
        }
        snprintf(szLine, sizeof(szLine), "%s-%d",
                 pEnv->pTextRecs != NULL
                     ? pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * (PUFO_CRAFT_NAME_TEXT_REC + pShip->bCraftType)
                     : "",
                 (int)pShip->bType); /* 0x77904..0x7795C, j_itoa over Ship.bType */
        if (pA != NULL) {
            UiPanel_SetLineText(pA, nRow, szLine);
            UiPanel_SetLineAux(pA, nRow, BuySell_RoleToItemId(pShip->bCraftType)); /* 117..121 */
        }
        if (pD != NULL) {
            UiPanel_SetLineAux(pD, nRow, 1);
        }
        BuySellFillQtyColumns(pB, pC, pE, nRow);
        if (pC != NULL) {
            UiPanel_SetLineAux(pC, nRow, pShip->bId);
        }
        ++nRow;
    }

    /* Fill pass 3, 0x77A48..0x77C44: the object rows, in slot order, only where the include flag
     * is set. The engineer and scientist counts are the ones count pass 3 already computed; the
     * original calls the two getters again here and gets the same numbers. */
    for (i = 1; i < pEnv->nObjectCount && i < PUFO_BUYSELL_SLOT_FLAGS; ++i) {
        const ObjectRecFull *pObj = pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL;
        i32 nAux;
        if (abInclude[i] != 1) {
            continue;
        }
        BuySellCopyName(szName, pObj != NULL ? pObj->szName : "");
        nAux = i;
        if (pA != NULL) {
            UiPanel_SetLineText(pA, nRow, szName);
        }
        BuySellFillQtyColumns(pB, pC, pE, nRow);
        if (i == PUFO_ROW_ENGINEER) {
            if (pD != NULL) {
                UiPanel_SetLineAux(pD, nRow, nEngineers); /* 0x77C20 */
            }
        } else if (i == PUFO_ROW_SCIENTIST) {
            if (pD != NULL) {
                UiPanel_SetLineAux(pD, nRow, nScientists);
            }
        } else {
            if (BuySellSlotIsAlien(i)) {
                nAux = i + PUFO_ROW_ALIEN_OFFSET; /* 0x77BAC */
            }
            if (pD != NULL) {
                UiPanel_SetLineAux(pD, nRow,
                                   Base_GetItemQty(pObj, pEnv->nSelectedBaseIdx)); /* 0x77BD0 */
            }
        }
        if (pA != NULL) {
            UiPanel_SetLineAux(pA, nRow, nAux);
        }
        ++nRow;
    }

    /* The sort pass, 0x77C48..0x77D50. */
    BuySellSortRows(pA, pE, pB, pC, pD, pEnv->nObjectCount - 1);
}

/* The shop's include table, 0x762FC: the catalogue slots, as opposed to the sell mode's
 * BuySellSlotIsStockedItem/BuySellSlotIsAlien pair. 1..58 is the human equipment block, 112 and
 * 113 the engineer and scientist crew, 117 and 118 the first two craft roles. */
static bool BuySellSlotIsBuyable(i32 nSlot)
{
    return (nSlot > 0 && nSlot < 59) || (nSlot > 111 && nSlot < 114) ||
           (nSlot > 116 && nSlot < 119);
}

/* BuySellScreen_BuildControls @ 0x76160. The header carries the branch map and the row model. */
void BuySellScreen_BuildControls(BuySellControlsEnv *pEnv)
{
    i8 abInclude[PUFO_BUYSELL_SLOT_FLAGS]; /* the original's stack dword table, v79[4*i+104] */
    UiPanel *pA, *pB, *pC, *pD, *pE;
    UiPanel *apFree[5];
    SmapSite *pSite;
    char szName[18];
    i32 nRows = 0;
    i32 nRow = 0;
    i32 i;

    if (pEnv == NULL || pEnv->pbControlsBuilt == NULL) {
        return;
    }

    if (*pEnv->pbControlsBuilt != 0) {
        /* The teardown branch, 0x76C08..0x76D70. */
        pSite = BuySellSiteAt(pEnv->aSites, pEnv->nSiteCount);
        if (pSite != NULL) {
            memset(pSite->auItemQty, 0, sizeof(pSite->auItemQty));   /* 0x76C18 */
            pSite->bFlag0 = 0;                                       /* 0x76C30 */
            pSite->bFlag1 = 0;                                       /* 0x76C38 */
            pSite->uRecoveredSoldiers = 0;                           /* 0x76C3C */
            pSite->bRecoveredSoldierAssign = 0;                     /* 0x76C48 */
            pSite->bRecoveredCraftCount = 0;                        /* 0x76C4C */
            memset(pSite->anSoldierIds, 0, sizeof(pSite->anSoldierIds)); /* 0x76C54 */
            memset(pSite->anCraftIds, 0, sizeof(pSite->anCraftIds));     /* 0x76C74 */
            /* bUnk518 is NOT cleared here -- only the build branch writes it, at 0x761DC. */
        }
        *pEnv->pbControlsBuilt = 0; /* 0x76C80 */
        for (i = 0; i < 4; ++i) {
            if (pEnv->apCtl[i] != NULL) {
                UiControl_FreePads(pEnv->apCtl[i]); /* 0x76CA0 */
            }
            /* 0x76CA8 also operator delete's the control; the port's controls are caller-owned. */
        }
        apFree[0] = pEnv->ppPanelA != NULL ? *pEnv->ppPanelA : NULL;
        apFree[1] = pEnv->ppPanelB != NULL ? *pEnv->ppPanelB : NULL;
        apFree[2] = pEnv->ppPanelE != NULL ? *pEnv->ppPanelE : NULL;
        apFree[3] = pEnv->ppPanelC != NULL ? *pEnv->ppPanelC : NULL;
        apFree[4] = pEnv->ppPanelD != NULL ? *pEnv->ppPanelD : NULL;
        for (i = 0; i < 5; ++i) {
            if (apFree[i] != NULL) {
                UiPanel_FreeLines(apFree[i]); /* 0x76CD0, 0x76CF0, 0x76D10, 0x76D30, 0x76D50 */
                free(apFree[i]);
            }
        }
        if (pEnv->ppPanelA != NULL) *pEnv->ppPanelA = NULL;
        if (pEnv->ppPanelB != NULL) *pEnv->ppPanelB = NULL;
        if (pEnv->ppPanelE != NULL) *pEnv->ppPanelE = NULL;
        if (pEnv->ppPanelC != NULL) *pEnv->ppPanelC = NULL;
        if (pEnv->ppPanelD != NULL) *pEnv->ppPanelD = NULL;
        if (pEnv->pnFunds != NULL && *pEnv->pnFunds != pEnv->nFundsSnapshot) {
            *pEnv->pnFunds = pEnv->nFundsSnapshot; /* 0x76D70 */
        }
        return;
    }

    /* Opening the working site, 0x7619C..0x7620C. */
    pSite = BuySellSiteAt(pEnv->aSites, pEnv->nSiteCount);
    if (pSite != NULL) {
        pSite->bFlag0 = PUFO_BUYSSELL_SITE_FLAG_NEW; /* 0x761A8 */
        if (pEnv->apBases != NULL && pEnv->nSelectedBaseIdx >= 0 &&
            pEnv->nSelectedBaseIdx < PUFO_FAC_BASE_COUNT) {
            pSite->bFlag1 = pEnv->apBases[pEnv->nSelectedBaseIdx]->bUnk4; /* 0x761D0 */
        }
        pSite->bUnk518 = 0;         /* 0x761DC */
        pSite->uRecoveredSoldiers = 0; /* 0x761E8 */
        memset(pSite->auItemQty, 0, sizeof(pSite->auItemQty)); /* 0x761EC */
    }

    for (i = 0; i < 4; ++i) {
        if (pEnv->apCtl[i] != NULL) {
            UiControl_ctor(pEnv->apCtl[i]); /* 0x76234, on the freshly allocated block */
        }
    }
    if (pEnv->apCtl[0] != NULL) {
        pEnv->apCtl[0]->pfnClick = pEnv->pfnPanelPrev; /* 0x76268 */
    }
    if (pEnv->apCtl[1] != NULL) {
        pEnv->apCtl[1]->pfnClick = pEnv->pfnPanelNext; /* 0x76274 */
    }
    for (i = 0; i < 4; ++i) {
        if (pEnv->apCtl[i] != NULL) {
            UiControl_Setup(pEnv->apCtl[i], 0, 0, &g_aBuySellCtlDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x76288 */
        }
    }

    *pEnv->pbControlsBuilt = 1; /* 0x762A4 */

    /* Include pass, 0x762A8..0x76334: the priced catalogue slots. uPrice is the second test and the
     * u16 at ObjectRecFull+0xBA the third; in the shipped OBJECTS.DAT the two agree. */
    memset(abInclude, 0, sizeof(abInclude));
    for (i = 1; i < pEnv->nObjectCount && i < PUFO_BUYSELL_SLOT_FLAGS; ++i) {
        const ObjectRecFull *pObj = pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL;
        if (!BuySellSlotIsBuyable(i) || pObj == NULL) {
            continue;
        }
        if (pObj->uPrice != 0 && pObj->uManufWorkPerUnit == 0) {
            abInclude[i] = 1;
            ++nRows;
        }
    }
    nRows += 1; /* 0x76344: the soldier row is counted whether or not slot 113 got one of its own */

    /* The five panels, 0x76348..0x768B4. Same globals as the sell mode, different geometry and
     * nStyle 14 instead of 13. */
    pA = BuySellNewPanel(pEnv->ppPanelA, nRows);
    if (pA != NULL) {
        pA->nStyle = 14;                            /* 0x76384 */
        pA->nFont = 16;
        pA->pfnClick = pEnv->pfnSyncSelection;     /* 0x763B4, BuySellScreen_SyncSelection */
        pA->nPosX = 10;
        pA->nPosY = 70;
        pA->nUnk7052 = 5;
        pA->nUnk7056 = 70;
        pA->nViewW = 125;
        UiPanel_SetBytes0to2(pA, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pA, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pA->bFlag7073 = 1;
        pA->bUnk7088 = (u8)0x96;                    /* 0x76464, MOV R0,#0xFFFFFF96 */
    }
    pE = BuySellNewPanel(pEnv->ppPanelE, nRows);
    if (pE != NULL) {
        pE->nStyle = 14;
        pE->nFont = 16;
        pE->pfnClick = NULL;                        /* 0x764CC */
        pE->nPosX = 174;
        pE->nPosY = 70;
        pE->nUnk7052 = 130;
        pE->nUnk7056 = 70;
        pE->nViewW = 50;
        UiPanel_SetBytes0to2(pE, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pE, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pE->bFlag7073 = 0;
        pE->bFlag7074 = 1;
        pE->nUnk7076 = 2;
    }
    pB = BuySellNewPanel(pEnv->ppPanelB, nRows);
    if (pB != NULL) {
        pB->nStyle = 14;
        pB->nFont = 16;
        pB->pfnClick = pEnv->pfnUpdatePrices;       /* 0x765D0, BuySellScreen_UpdatePrices */
        pB->nPosX = 180;
        pB->nPosY = 70;
        pB->nUnk7052 = 176;
        pB->nUnk7056 = 70;
        pB->nViewW = 15;
        UiPanel_SetBytes0to2(pB, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pB, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pB->bFlag7073 = 1;
        UiPanel_SetBytes3to5(pB, 0x32, 0x32, 0xFA);
        pB->nUnk7084 = 5;
        pB->bUnk7088 = (u8)0x96;
    }
    pC = BuySellNewPanel(pEnv->ppPanelC, nRows);
    if (pC != NULL) {
        pC->nStyle = 14;
        pC->nFont = 16;
        pC->pfnClick = pEnv->pfnValidateCapacity;   /* 0x766EC, BuySellScreen_ValidateCapacity */
        pC->nPosX = 196;
        pC->nPosY = 70;
        pC->nUnk7052 = 192;
        pC->nUnk7056 = 70;
        pC->nViewW = 15;
        UiPanel_SetBytes0to2(pC, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pC, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pC->bFlag7073 = 1;
        UiPanel_SetBytes3to5(pC, 0x32, 0x32, 0xFA);
        pC->nUnk7084 = 5;
        pC->bUnk7088 = (u8)0x96;
    }
    pD = BuySellNewPanel(pEnv->ppPanelD, nRows);
    if (pD != NULL) {
        pD->nStyle = 14;
        pD->nFont = 16;
        pD->pfnClick = NULL;                        /* 0x76808 */
        pD->nPosX = 232;
        pD->nPosY = 70;
        pD->nUnk7052 = 190;
        pD->nUnk7056 = 70;
        pD->nViewW = 45;
        UiPanel_SetBytes0to2(pD, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pD, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pD->bFlag7073 = 0;
        pD->bFlag7074 = 1;
        pD->nUnk7076 = 2;
    }

    /* Fill pass, 0x768DC..0x76AC4: the catalogue rows in slot order, panel E carrying the price the
     * buy path charges. The soldier row is written inside the slot-113 branch, so when slot 113 has
     * no price the counted-but-never-written last row stays empty. */
    for (i = 1; i < pEnv->nObjectCount && i < PUFO_BUYSELL_SLOT_FLAGS; ++i) {
        const ObjectRecFull *pObj = pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL;
        if (abInclude[i] != 1) {
            continue;
        }
        BuySellCopyName(szName, pObj != NULL ? pObj->szName : "");
        if (pA != NULL) {
            UiPanel_SetLineText(pA, nRow, szName);
            UiPanel_SetLineAux(pA, nRow, i); /* 0x76978 */
        }
        BuySellFillQtyColumns(pB, pC, pE, nRow);
        if (pE != NULL) {
            UiPanel_SetLineAux(pE, nRow, (i32)pObj->uPrice); /* 0x769E4 */
        }
        if (pD != NULL) {
            UiPanel_SetLineAux(pD, nRow, 0); /* 0x769EC */
        }
        ++nRow;
        if (i == PUFO_ROW_SCIENTIST) {
            if (pA != NULL) {
                UiPanel_SetLineText(pA, nRow,
                                    pEnv->pTextRecs != NULL
                                        ? pEnv->pTextRecs
                                              + PUFO_TEXT_REC_STRIDE * PUFO_BUYSSELL_SOLDIER_TEXT_REC
                                        : ""); /* unk_BB823 = aTextRecs + 205*159, "Soldier" */
                UiPanel_SetLineAux(pA, nRow, PUFO_ROW_SOLDIER); /* 0x76A40 */
            }
            BuySellFillQtyColumns(pB, pC, pE, nRow);
            if (pE != NULL) {
                UiPanel_SetLineAux(pE, nRow, PUFO_BUYSELL_PRICE_SOLDIER); /* 0x76AA8 */
            }
            if (pD != NULL) {
                UiPanel_SetLineAux(pD, nRow, 0); /* 0x76AB0 */
            }
            ++nRow;
        }
    }

    /* The sort pass, 0x76AF0..0x76BE8. */
    BuySellSortRows(pA, pE, pB, pC, pD, pEnv->nObjectCount - 1);
}

/* g_aTransferCtlDesc @ 0xAC3B8: the transfer screen's four control descriptors. The first two are the
 * scroll pair at the same x as the buy/sell pair; the third and fourth are the reset and confirm
 * buttons, both drawn from the "emptyb" / "cemptyb" pad pair. */
static const UiControlDesc g_aTransferCtlDesc[4] = {
    { 0x00CD, 0x0012, "up",     "sup",     "", 0 }, /* 0xAC3B8 */
    { 0x00CD, 0x0026, "down",   "sdown",   "", 0 }, /* 0xAC3D8 */
    { 0x0001, 0x012C, "emptyb", "cemptyb", "", 0 }, /* 0xAC3F8 */
    { 0x0078, 0x012C, "emptyb", "cemptyb", "", 0 }, /* 0xAC418 */
};

/* Ui_InitTransfer @ 0x9955c: the tail call of TransferScreen_Open @ 0x614fc. The header carries the
 * branch map. */
void Ui_InitTransfer(TransferInitEnv *pEnv)
{
    PadImage *pPad;
    i32 i;

    if (pEnv == NULL || pEnv->ppRoot == NULL) {
        return;
    }

    pPad = BuySellPadNew(); /* 0x99568, operator new(0x14) + PadImage_ctor */
    pEnv->ppRoot[0] = pPad; /* 0x9958C: the ctor's result, not the raw allocation */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_TRANSFER_PAD_NAME, PUFO_TRANSFER_PAD_MODE); /* 0x99590 */
        pPad->nOriginX = 0; /* 0x99598 */
        pPad->nOriginY = 0; /* 0x995A0 */
    }

    if (pEnv->pfnBuildPanels != NULL) {
        pEnv->pfnBuildPanels(); /* 0x995A4, before the controls exist */
    }
    if (pEnv->pbTransferOk != NULL) {
        *pEnv->pbTransferOk = 1; /* 0x995B8 */
    }

    for (i = 0; i < 4; ++i) {
        pEnv->ppRoot[i + 1] = pEnv->apCtl[i]; /* 0x995E4, the original's operator new(0x20) slots */
    }
    if (pEnv->apCtl[0] != NULL) {
        pEnv->apCtl[0]->pfnClick = pEnv->pfnPanelPrev; /* 0x99604 */
    }
    if (pEnv->apCtl[1] != NULL) {
        pEnv->apCtl[1]->pfnClick = pEnv->pfnPanelNext; /* 0x99614 */
    }
    if (pEnv->apCtl[2] != NULL) {
        pEnv->apCtl[2]->pfnClick = pEnv->pfnResetSite; /* 0x99624 */
    }
    if (pEnv->apCtl[3] != NULL) {
        pEnv->apCtl[3]->pfnClick = pEnv->pfnCommit; /* 0x99630 */
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->x = 224; /* 0x9963C */
        pEnv->pCloseCtl->y = 1;   /* 0x99648 */
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseClick; /* 0x99654 */
    }
    if (pEnv->pConfirmCtl != NULL) {
        pEnv->pConfirmCtl->pfnClick = pEnv->pfnCloseClick; /* 0x9965C, before the Setup pass */
    }

    for (i = 0; i < 4; ++i) {
        if (pEnv->apCtl[i] != NULL) {
            UiControl_Setup(pEnv->apCtl[i], 0, 0, &g_aTransferCtlDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x99670 */
        }
    }

    if (pEnv->pConfirmCtl != NULL) {
        pEnv->pConfirmCtl->x = (i16)(120 - (pEnv->pConfirmCtl->uWidth >> 1)); /* 0x99698 */
        if (pEnv->pbBaseCount != NULL) {
            pEnv->pConfirmCtl->y = (i16)(28 * (i32)*pEnv->pbBaseCount + 10); /* 0x996B8, LDRSB */
        }
    }
}

/* Transfer_ResetSite @ 0x9690c: the transfer screen's reset button. The header carries the branch
 * map and the gate. */
SmapSite *Transfer_ResetSite(TransferResetEnv *pEnv)
{
    SmapSite *pSite;
    bool bBlocked = false;
    i32 i;

    if (pEnv == NULL) {
        return NULL;
    }

    if (pEnv->pbTransferOk != NULL) {
        *pEnv->pbTransferOk = 1; /* 0x9691C */
    }

    /* 0x96920..0x96974: the site index is the site count -- the transfer flow's working record --
     * the base index is the one the psi screen left in byte_1870F4, and the scan flag is OFF. */
    if (Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount, pEnv->nSiteCount,
                                     pEnv->apBases, pEnv->nPendingBase,
                                     pEnv->apObjects, pEnv->nObjectCount, false) != 0) {
        bBlocked = true;
    }
    if (!bBlocked && pEnv->pbScratch1870D0 != NULL && (i8)*pEnv->pbScratch1870D0 != 0) {
        bBlocked = true; /* 0x96944, LDRSB then LSL #24 / ASR #24 */
    }
    if (!bBlocked && pEnv->pbScratch1870F0 != NULL && (i8)*pEnv->pbScratch1870F0 != 0) {
        bBlocked = true; /* 0x96958 */
    }
    if (!bBlocked && Smap_SumSiteScores(pEnv->aSites, pEnv->nSiteCount) > 0) {
        bBlocked = true; /* 0x9696C */
    }

    if (bBlocked) {
        if (pEnv->pbTransferOk != NULL) {
            *pEnv->pbTransferOk = 0; /* 0x96980 */
        }
        if (pEnv->pdwShortfall != NULL) {
            *pEnv->pdwShortfall = 1; /* 0x96984, a dword store into a byte global */
        }
        if (pEnv->pnResidue != NULL) {
            *pEnv->pnResidue = PUFO_BUYSELL_SHORTFALL_TRANSFER; /* 0x96990 */
        }
    }

    /* 0x96994..0x96A04: fold the pending quantities back into the count column. */
    if (pEnv->pPanelA != NULL && pEnv->pPanelD != NULL && pEnv->pPanelE != NULL) {
        for (i = 0; i < pEnv->pPanelA->nLineCount; ++i) {
            if (i >= PUFO_PANEL_LINES) {
                break;
            }
            pEnv->pPanelD->anLineAux[i] += pEnv->pPanelE->anLineAux[i];
            pEnv->pPanelE->anLineAux[i] = 0;
        }
    }

    pSite = BuySellSiteAt(pEnv->aSites, pEnv->nSiteCount);
    if (pSite == NULL) {
        return NULL;
    }
    pSite->bUnk518 = 0;                /* 0x96A3C */
    pSite->uRecoveredSoldiers = 0;     /* 0x96A48 */
    pSite->bRecoveredCraftCount = 0;   /* 0x96A54 */
    pSite->bRecoveredSoldierAssign = 0; /* 0x96A58 */
    pSite->bHasLoot = 0;               /* 0x96A5C */
    for (i = 0; i < pEnv->nObjectCount && i < 256; ++i) {
        pSite->auItemQty[i] = 0; /* 0x96AA0, over Map_nObjectCount entries, not the full 256 */
    }
    return pSite;
}

/* The transfer screen's slot filter, 0x96D00..0x96D48. Against the sell mode's
 * BuySellSlotIsStockedItem / BuySellSlotIsAlien pair this is the same set with 142 dropped: the
 * sell mode counts slot 142 and this screen does not. */
static bool TransferSlotIsTransferrable(i32 nSlot)
{
    return (nSlot > 0 && nSlot < 70) || (nSlot > 76 && nSlot < 112) || nSlot == 114 ||
           (nSlot > 121 && nSlot < 125) || (nSlot > 142 && nSlot < 149);
}

/* TransferScreen_BuildPanels @ 0x96b30. The header carries the branch map and the row model. */
void TransferScreen_BuildPanels(TransferBuildEnv *pEnv)
{
    i8 abInclude[PUFO_BUYSELL_SLOT_FLAGS]; /* the original's stack dword table, v100[255] */
    UiPanel *pA, *pB, *pC, *pD, *pE;
    UiPanel *apFree[5];
    SmapSite *pSite;
    PadImage *pPad;
    char szName[18];
    char szLine[PUFO_PANEL_LINE_BYTES];
    i32 nRows = 0;
    i32 nRow = 0;
    i32 nEngineers = 0;
    i32 nScientists = 0;
    i32 i;

    if (pEnv == NULL || pEnv->pbPanelsBuilt == NULL) {
        return;
    }

    if (*pEnv->pbPanelsBuilt != 0) {
        /* The teardown branch, 0x97994..0x97A88. No funds rollback: the transfer screen spends none. */
        *pEnv->pbPanelsBuilt = 0; /* 0x97994 */
        if (pEnv->pbTransferOk != NULL) {
            *pEnv->pbTransferOk = 0; /* 0x979A0 */
        }
        pPad = pEnv->ppPad != NULL ? *pEnv->ppPad : NULL;
        if (pPad != NULL) {
            PadImage_FreeData(pPad); /* 0x979B4 */
            free(pPad);
            *pEnv->ppPad = NULL;
        }
        for (i = 0; i < 4; ++i) {
            if (pEnv->apCtl[i] != NULL) {
                UiControl_FreePads(pEnv->apCtl[i]); /* 0x979D4 */
            }
            /* 0x979DC also operator delete's the control; the port's controls are caller-owned. */
        }
        apFree[0] = pEnv->ppPanelA != NULL ? *pEnv->ppPanelA : NULL;
        apFree[1] = pEnv->ppPanelB != NULL ? *pEnv->ppPanelB : NULL;
        apFree[2] = pEnv->ppPanelE != NULL ? *pEnv->ppPanelE : NULL;
        apFree[3] = pEnv->ppPanelC != NULL ? *pEnv->ppPanelC : NULL;
        apFree[4] = pEnv->ppPanelD != NULL ? *pEnv->ppPanelD : NULL;
        for (i = 0; i < 5; ++i) {
            if (apFree[i] != NULL) {
                UiPanel_FreeLines(apFree[i]); /* 0x97A00, 0x97A20, 0x97A40, 0x97A60, 0x97A80 */
                free(apFree[i]);
            }
        }
        if (pEnv->ppPanelA != NULL) *pEnv->ppPanelA = NULL;
        if (pEnv->ppPanelB != NULL) *pEnv->ppPanelB = NULL;
        if (pEnv->ppPanelE != NULL) *pEnv->ppPanelE = NULL;
        if (pEnv->ppPanelC != NULL) *pEnv->ppPanelC = NULL;
        if (pEnv->ppPanelD != NULL) *pEnv->ppPanelD = NULL;
        return;
    }

    *pEnv->pbPanelsBuilt = 1; /* 0x96B60 */

    /* Opening the working site, 0x96B80..0x96BDC -- the same five bytes and the same
     * Map_nObjectCount-long quantity clear Transfer_ResetSite uses. */
    pSite = BuySellSiteAt(pEnv->aSites, pEnv->nSiteCount);
    if (pSite != NULL) {
        pSite->bUnk518 = 0;                 /* 0x96B98 */
        pSite->uRecoveredSoldiers = 0;      /* 0x96BA4 */
        pSite->bRecoveredCraftCount = 0;    /* 0x96BB0 */
        pSite->bRecoveredSoldierAssign = 0; /* 0x96BB4 */
        pSite->bHasLoot = 0;                /* 0x96BB8 */
        for (i = 0; i < pEnv->nObjectCount && i < 256; ++i) {
            pSite->auItemQty[i] = 0;
        }
    }

    /* Count pass 1, 0x96BF0..0x96C3C: soldiers at this base that no craft carries. */
    for (i = 0; i < pEnv->nSoldierCount; ++i) {
        const Soldier *pSol = pEnv->apSoldiers != NULL ? pEnv->apSoldiers[i] : NULL;
        if (pSol == NULL) {
            continue; /* deviation: the original dereferences the array slot blindly */
        }
        if ((i32)pSol->nKind == (i32)pEnv->nSelectedBaseIdx && pSol->bUnk183 == 0xFF) {
            ++nRows;
        }
    }

    /* Count pass 2, 0x96C44..0x96CB4: craft homed here with nobody aboard.
     *
     * ORIGINAL BUG: the fill pass at 0x97550 adds a third test this count pass does not have --
     * Ship.bAtBase (0x06) must be non-zero -- so a craft homed at this base, carrying nobody and
     * currently away adds a row to the row total that no fill pass ever writes, and the list shows
     * one empty row. BuySellScreen_BuildPanels' own count pass at 0x76FB4 does test bAtBase, so the
     * two screens disagree about the same craft. */
    for (i = 0; i < pEnv->nCraftCount; ++i) {
        const Ship *pShip = pEnv->apCrafts != NULL ? pEnv->apCrafts[i] : NULL;
        if (pShip == NULL) {
            continue;
        }
        if ((i32)pShip->bHomeBase == (i32)pEnv->nSelectedBaseIdx &&
            Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount, i) == 0) {
            ++nRows;
        }
    }

    /* Count pass 3, 0x96CC4..0x96DC0: the transferable slots the base holds, plus the two crew
     * slots. There is no separate alien pass here -- the filter already covers them. */
    memset(abInclude, 0, sizeof(abInclude));
    for (i = 1; i < pEnv->nObjectCount && i < PUFO_BUYSELL_SLOT_FLAGS; ++i) {
        const ObjectRecFull *pObj = pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL;
        if (TransferSlotIsTransferrable(i) &&
            Base_GetItemQty(pObj, pEnv->nSelectedBaseIdx) != 0) {
            abInclude[i] = 1;
            ++nRows;
        }
        if (i == PUFO_ROW_ENGINEER) {
            nEngineers = Base_GetEngineerCount(pEnv->apObjects != NULL ? pEnv->apObjects[112] : NULL,
                                               pEnv->apManufCol, pEnv->nSelectedBaseIdx,
                                               pEnv->nCrewEntries); /* 0x96D74 */
            if (nEngineers <= 0) {
                continue;
            }
            abInclude[112] = 1;
            ++nRows;
        }
        if (i == PUFO_ROW_SCIENTIST) {
            nScientists = Base_GetScientistCount(pEnv->apObjects != NULL ? pEnv->apObjects[113] : NULL,
                                                 pEnv->apResearchCol, pEnv->nSelectedBaseIdx,
                                                 pEnv->nCrewEntries); /* 0x96DA0 */
            if (nScientists > 0) {
                abInclude[113] = 1;
                ++nRows;
            }
        }
    }

    /* The five panels, 0x96DC4..0x9732C. Same nStyle 13 as the sell mode, B / C / D eight pixels
     * further right. */
    pA = BuySellNewPanel(pEnv->ppPanelA, nRows);
    if (pA != NULL) {
        pA->nStyle = 13;                            /* 0x96E00 */
        pA->nFont = 16;
        pA->pfnClick = pEnv->pfnCacheSelIndex;     /* 0x96E30, Transfer_CacheSelIndex */
        pA->nPosX = 10;
        pA->nPosY = 70;
        pA->nUnk7052 = 5;
        pA->nUnk7056 = 70;
        pA->nViewW = 125;
        UiPanel_SetBytes0to2(pA, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pA, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pA->bFlag7073 = 1;
        pA->bUnk7088 = (u8)0x96;
    }
    pE = BuySellNewPanel(pEnv->ppPanelE, nRows);
    if (pE != NULL) {
        pE->nStyle = 13;
        pE->nFont = 16;
        pE->pfnClick = NULL;                        /* 0x96F48 */
        pE->nPosX = 232;
        pE->nPosY = 70;
        pE->nUnk7052 = 190;
        pE->nUnk7056 = 70;
        pE->nViewW = 45;
        UiPanel_SetBytes0to2(pE, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pE, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pE->bFlag7073 = 0;
        pE->bFlag7074 = 1;
        pE->nUnk7076 = 2;
    }
    pB = BuySellNewPanel(pEnv->ppPanelB, nRows);
    if (pB != NULL) {
        pB->nStyle = 13;
        pB->nFont = 16;
        pB->pfnClick = pEnv->pfnMoveOneItem;        /* 0x9704C, Transfer_MoveOneItem */
        pB->nPosX = 172;
        pB->nPosY = 70;
        pB->nUnk7052 = 168;
        pB->nUnk7056 = 70;
        pB->nViewW = 15;
        UiPanel_SetBytes0to2(pB, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pB, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pB->bFlag7073 = 1;
        UiPanel_SetBytes3to5(pB, 0x32, 0x32, 0xFA);
        pB->nUnk7084 = 5;
        pB->bUnk7088 = (u8)0x96;
    }
    pC = BuySellNewPanel(pEnv->ppPanelC, nRows);
    if (pC != NULL) {
        pC->nStyle = 13;
        pC->nFont = 16;
        pC->pfnClick = pEnv->pfnValidateCapacity;   /* 0x97168, TransferScreen_ValidateCapacity */
        pC->nPosX = 188;
        pC->nPosY = 70;
        pC->nUnk7052 = 184;
        pC->nUnk7056 = 70;
        pC->nViewW = 15;
        UiPanel_SetBytes0to2(pC, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pC, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pC->bFlag7073 = 1;
        UiPanel_SetBytes3to5(pC, 0x32, 0x32, 0xFA);
        pC->nUnk7084 = 5;
        pC->bUnk7088 = (u8)0x96;
    }
    pD = BuySellNewPanel(pEnv->ppPanelD, nRows);
    if (pD != NULL) {
        pD->nStyle = 13;
        pD->nFont = 16;
        pD->pfnClick = NULL;                        /* 0x97284 */
        pD->nPosX = 166;
        pD->nPosY = 70;
        pD->nUnk7052 = 130;
        pD->nUnk7056 = 70;
        pD->nViewW = 50;
        UiPanel_SetBytes0to2(pD, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pD, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pD->bFlag7073 = 0;
        pD->bFlag7074 = 1;
        pD->nUnk7076 = 2;
    }

    /* Fill pass 1, 0x97330..0x974DC: the soldier rows. */
    for (i = 0; i < pEnv->nSoldierCount; ++i) {
        const Soldier *pSol = pEnv->apSoldiers != NULL ? pEnv->apSoldiers[i] : NULL;
        if (pSol == NULL) {
            continue;
        }
        if ((i32)pSol->nKind != (i32)pEnv->nSelectedBaseIdx || pSol->bUnk183 != 0xFF) {
            continue;
        }
        BuySellCopyName(szName, pSol->szName);
        if (pA != NULL) {
            UiPanel_SetLineText(pA, nRow, szName);
            UiPanel_SetLineAux(pA, nRow, PUFO_ROW_SOLDIER); /* 0x9741C */
        }
        if (pD != NULL) {
            UiPanel_SetLineAux(pD, nRow, 1); /* 0x97428, one soldier, always */
        }
        BuySellFillQtyColumns(pB, pC, pE, nRow);
        if (pC != NULL) {
            UiPanel_SetLineAux(pC, nRow, pSol->nUnk440); /* 0x974A0, the roster id */
        }
        if (pB != NULL) {
            UiPanel_SetLineAux(pB, nRow, i); /* 0x974AC, the index into apSoldiers */
        }
        ++nRow;
    }

    /* Fill pass 2, 0x974E4..0x976B4: the craft rows. TEXTS[415 + bCraftType] + "-" + bType. This
     * pass is where the bAtBase test the count pass is missing lives (0x97550). */
    for (i = 0; i < pEnv->nCraftCount; ++i) {
        const Ship *pShip = pEnv->apCrafts != NULL ? pEnv->apCrafts[i] : NULL;
        if (pShip == NULL) {
            continue;
        }
        if ((i32)pShip->bHomeBase != (i32)pEnv->nSelectedBaseIdx ||
            Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount, i) != 0 ||
            pShip->bAtBase == 0) {
            continue;
        }
        snprintf(szLine, sizeof(szLine), "%s-%d",
                 pEnv->pTextRecs != NULL
                     ? pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * (PUFO_CRAFT_NAME_TEXT_REC + pShip->bCraftType)
                     : "",
                 (int)pShip->bType); /* 0x97580..0x975D8, strcat("-") then strcat(j_itoa(bType)) */
        if (pA != NULL) {
            UiPanel_SetLineText(pA, nRow, szLine);
            UiPanel_SetLineAux(pA, nRow, BuySell_RoleToItemId(pShip->bCraftType)); /* 0x97604 */
        }
        if (pD != NULL) {
            UiPanel_SetLineAux(pD, nRow, 1); /* 0x97610, one craft, always */
        }
        BuySellFillQtyColumns(pB, pC, pE, nRow);
        if (pC != NULL) {
            UiPanel_SetLineAux(pC, nRow, pShip->bId); /* 0x97690 */
        }
        ++nRow;
    }

    /* Fill pass 3, 0x976C0..0x97858: the object rows, in slot order. Panel A aux is the plain slot
     * for every row -- this screen has no alien +2000 offset -- and panel E stays zero because the
     * transfer screen has no price column. */
    for (i = 1; i < pEnv->nObjectCount && i < PUFO_BUYSELL_SLOT_FLAGS; ++i) {
        const ObjectRecFull *pObj = pEnv->apObjects != NULL ? pEnv->apObjects[i] : NULL;
        if (abInclude[i] != 1) {
            continue;
        }
        BuySellCopyName(szName, pObj != NULL ? pObj->szName : "");
        if (pA != NULL) {
            UiPanel_SetLineText(pA, nRow, szName);
            UiPanel_SetLineAux(pA, nRow, i); /* 0x9777C */
        }
        BuySellFillQtyColumns(pB, pC, pE, nRow);
        if (i == PUFO_ROW_ENGINEER) {
            if (pD != NULL) {
                UiPanel_SetLineAux(pD, nRow, nEngineers); /* 0x97858 */
            }
        } else if (i == PUFO_ROW_SCIENTIST) {
            if (pD != NULL) {
                UiPanel_SetLineAux(pD, nRow, nScientists);
            }
        } else {
            if (pD != NULL) {
                UiPanel_SetLineAux(pD, nRow, Base_GetItemQty(pObj, pEnv->nSelectedBaseIdx)); /* 0x97810 */
            }
        }
        ++nRow;
    }

    /* The sort pass, 0x97880..0x97988. */
    BuySellSortRows(pA, pE, pB, pC, pD, pEnv->nObjectCount - 1);
}

/* Transfer_MoveOneItem @ 0x9602c: the transfer screen's decrement column. The header carries the
 * branch map. */
i32 Transfer_MoveOneItem(TransferMoveEnv *pEnv)
{
    SmapSite *pSite;
    i32 nLimit;
    i32 nSlot;
    i32 nRow;
    i32 nAux;
    i32 nIter;

    if (pEnv == NULL || pEnv->pnRowCursor == NULL || pEnv->pnRadarPulseTimer == NULL) {
        return 0;
    }
    if (pEnv->pPanelA == NULL || pEnv->pPanelB == NULL || pEnv->pPanelD == NULL
        || pEnv->pPanelE == NULL) {
        return 0;
    }

    /* 0x9606C: the selected row's slot id, read at the row index panel B cached. */
    if (pEnv->pPanelB->nUnk3032 < 0 || pEnv->pPanelB->nUnk3032 >= PUFO_PANEL_LINES) {
        return 0; /* deviation: the original indexes panel A with whatever nUnk3032 holds */
    }
    nSlot = pEnv->pPanelA->anLineAux[pEnv->pPanelB->nUnk3032];

    nLimit = *pEnv->pnRadarPulseTimer / 10; /* 0x96084, signed division */
    if (nLimit < 0) {
        return nLimit; /* 0x9608C, BMI */
    }

    pSite = BuySellSiteAt(pEnv->aSites, pEnv->nSiteCount);

    if (nSlot == PUFO_ROW_SOLDIER) {
        /* 0x961C0: the soldier row is handled once, outside the repeat loop. */
        nRow = *pEnv->pnRowCursor;
        if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
            return nLimit;
        }
        if (pEnv->pPanelE->anLineAux[nRow] > 0) {
            pEnv->pPanelE->anLineAux[nRow] = 0;   /* 0x961D8 */
            pEnv->pPanelD->anLineAux[nRow] += 1;  /* 0x961F4 */
            if (pSite != NULL) {
                pSite->bRecoveredSoldierAssign =
                    (u8)((i8)pSite->bRecoveredSoldierAssign - 1); /* 0x96218, LDRSB / STRB at +0x204 */
            }
        }
        return nLimit;
    }

    for (nIter = 0;; ++nIter) {
        nRow = *pEnv->pnRowCursor; /* 0x960B0, the cached row is re-read every iteration */
        if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
            return nLimit;
        }
        nAux = pEnv->pPanelA->anLineAux[nRow];
        if (nAux > PUFO_TRANSFER_ROW_CRAFT_FIRST - 1 && nAux < PUFO_TRANSFER_ROW_CRAFT_LAST + 1) {
            /* 0x96220: a craft row moves at most one unit and then stops. */
            if (pEnv->pPanelE->anLineAux[nRow] > 0) {
                pEnv->pPanelE->anLineAux[nRow] = 0;  /* 0x96238 */
                pEnv->pPanelD->anLineAux[nRow] += 1; /* 0x96254 */
                if (pSite != NULL) {
                    pSite->bRecoveredCraftCount =
                        (u8)((i8)pSite->bRecoveredCraftCount - 1); /* 0x9627C, LDRSB / STRB at +0x205 */
                }
            }
            return nLimit;
        }
        if (pEnv->pPanelE->anLineAux[nRow] > 0) {
            pEnv->pPanelD->anLineAux[nRow] += 1;  /* 0x9610C */
            pEnv->pPanelE->anLineAux[nRow] -= 1;  /* 0x9612C */
            if (pSite != NULL && nSlot >= 0 && nSlot < 256) {
                pSite->auItemQty[nSlot] -= 1; /* 0x96160 */
            }
        }
        if (nIter >= nLimit) {
            break; /* 0x96194: `CMP count, timer/10; BLE` back to the top */
        }
    }
    return nLimit;
}

/* 0x96614 / 0x9662C: the transfer gate writes byte_1870C8 as a dword. The containment and living
 * quarters sites write it unconditionally; the hangar and stores sites write it only when it is
 * currently zero, so an earlier gate's flag survives a later gate that would not have fired. */
static void TransferSetShortfallFlag(const TransferCapacityEnv *pEnv, bool bOnlyIfClear)
{
    if (pEnv->pdwShortfall != NULL && (!bOnlyIfClear || *pEnv->pdwShortfall == 0)) {
        *pEnv->pdwShortfall = 1;
    }
}

static i32 TransferShortfall(const TransferCapacityEnv *pEnv, i32 nReason, bool bOnlyIfClear)
{
    TransferSetShortfallFlag(pEnv, bOnlyIfClear);
    if (pEnv->pnResidue != NULL) {
        *pEnv->pnResidue = nReason;
    }
    return nReason;
}

/* TransferScreen_ValidateCapacity @ 0x96288: the transfer screen's increment column. The header
 * carries the branch map and the two-base-index asymmetry. */
i32 TransferScreen_ValidateCapacity(TransferCapacityEnv *pEnv)
{
    SmapSite *pSite;
    i32 nLimit;
    i32 nSlot;
    i32 nRow;
    i32 nAux;
    i32 nIter;

    if (pEnv == NULL || pEnv->pnRowCursor == NULL || pEnv->pnRadarPulseTimer == NULL) {
        return PUFO_BUYSELL_SHORTFALL_NONE;
    }
    if (pEnv->pPanelA == NULL || pEnv->pPanelC == NULL || pEnv->pPanelD == NULL
        || pEnv->pPanelE == NULL) {
        return PUFO_BUYSELL_SHORTFALL_NONE;
    }

    /* 0x962D0: the selected row's slot id, read at the row index panel C cached. */
    if (pEnv->pPanelC->nUnk3032 < 0 || pEnv->pPanelC->nUnk3032 >= PUFO_PANEL_LINES) {
        return PUFO_BUYSELL_SHORTFALL_NONE; /* deviation: the original indexes panel A blindly */
    }
    nSlot = pEnv->pPanelA->anLineAux[pEnv->pPanelC->nUnk3032];

    nLimit = *pEnv->pnRadarPulseTimer / 10; /* 0x962F0, signed division */
    if (nLimit < 0) {
        return nLimit; /* 0x962F0, BMI */
    }

    pSite = BuySellSiteAt(pEnv->aSites, pEnv->nSiteCount);

    if (nSlot == PUFO_ROW_SOLDIER || nSlot == PUFO_ROW_ENGINEER || nSlot == PUFO_ROW_SCIENTIST) {
        /* 0x96358: the while condition fails on the first test for these three slot ids, so the
         * repeat loop is never entered and the whole pass runs exactly once. */
        const i32 nOccupied = Base_CalcOccupiedSpace(pEnv->apSoldiers, pEnv->nSoldierCount,
                                                     pEnv->apObjects, pEnv->nObjectCount,
                                                     pEnv->nTransferBaseIdx); /* 0x96668 */
        const i32 nOutbound = Base_CountOutboundTransferSpace(pEnv->aSites, pEnv->nSiteCount,
                                                              pEnv->nSiteCount, pEnv->apBases,
                                                              pEnv->nTransferBaseIdx,
                                                              true); /* 0x96688 */
        const i32 nCap = Base_CalcLivingQuartersCap(pEnv->abFacilityGrid,
                                                    pEnv->nTransferBaseIdx); /* 0x96658 */

        if (nCap < nOccupied + nOutbound + 1) { /* 0x96690 */
            return TransferShortfall(pEnv, PUFO_BUYSELL_SHORTFALL_LIVING, false); /* 0x96694 */
        }

        nRow = *pEnv->pnRowCursor;
        if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
            return nLimit;
        }

        if (pEnv->pPanelD->anLineAux[nRow] > 0 && nSlot == PUFO_ROW_SOLDIER) {
            pEnv->pPanelE->anLineAux[nRow] = 1;  /* 0x966E8, assigned 1, not incremented */
            pEnv->pPanelD->anLineAux[nRow] -= 1; /* 0x96704 */
            if (pSite != NULL) {
                pSite->bRecoveredSoldierAssign =
                    (u8)((i8)pSite->bRecoveredSoldierAssign + 1); /* 0x9672C, LDRSB / STRB at +0x204 */
            }
        }

        if (nSlot == PUFO_ROW_ENGINEER || nSlot == PUFO_ROW_SCIENTIST) {
            const ObjectRecFull *pRec = BuySellObjectAt(pEnv->apObjects, pEnv->nObjectCount, nSlot);
            const i32 nAvail = Base_GetItemQty(pRec, pEnv->nSelectedBaseIdx);
            const i32 nPending = (pSite != NULL) ? (i32)pSite->auItemQty[nSlot] : 0;
            if (nAvail > nPending) { /* 0x9678C */
                if (pSite != NULL) {
                    pSite->auItemQty[nSlot] = (u16)(pSite->auItemQty[nSlot] + 1); /* 0x967A4 */
                }
                pEnv->pPanelD->anLineAux[nRow] -= 1;  /* 0x967BC */
                pEnv->pPanelE->anLineAux[nRow] += 1;  /* 0x967D8 */
            }
        }
        return nLimit;
    }

    for (nIter = 0;; ++nIter) {
        nRow = *pEnv->pnRowCursor; /* 0x96368, the cached row is re-read every iteration */
        if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
            return nLimit;
        }
        nAux = pEnv->pPanelA->anLineAux[nRow];

        if (nAux > PUFO_TRANSFER_ROW_CRAFT_FIRST - 1 && nAux < PUFO_TRANSFER_ROW_CRAFT_LAST + 1) {
            /* 0x96374: a craft row gates on the hangar and moves at most one craft per click. */
            const i32 nShips = Base_CountShipsAtBase(pEnv->apCraft, pEnv->nCraftCount, pEnv->apBases,
                                                     pEnv->nBaseCount, pEnv->abFacilityGrid,
                                                     pEnv->nTransferBaseIdx); /* 0x9653C */
            const i32 nInbound = Base_CountInboundTransfers(pEnv->aSites, pEnv->nSiteCount,
                                                            pEnv->nSiteCount, pEnv->apBases,
                                                            pEnv->nTransferBaseIdx,
                                                            true); /* 0x96558 */
            const i32 nCap = Base_CalcHangarCap(pEnv->abFacilityGrid,
                                                pEnv->nTransferBaseIdx); /* 0x96570 */
            if (nInbound + nShips + 1 > nCap) { /* 0x96580 */
                return TransferShortfall(pEnv, PUFO_BUYSELL_SHORTFALL_HANGAR, true); /* 0x96584 */
            }
            if (pEnv->pPanelD->anLineAux[nRow] > 0) {
                pEnv->pPanelE->anLineAux[nRow] = 1;  /* 0x965C0, assigned 1 */
                pEnv->pPanelD->anLineAux[nRow] -= 1; /* 0x965DC */
                if (pSite != NULL) {
                    pSite->bRecoveredCraftCount =
                        (u8)((i8)pSite->bRecoveredCraftCount + 1); /* 0x96604, LDRSB / STRB at +0x205 */
                }
            }
            return nLimit; /* the craft branch leaves the function after one unit */
        }

        {
            const ObjectRecFull *pRec = BuySellObjectAt(pEnv->apObjects, pEnv->nObjectCount, nSlot);
            const i32 nAvail = Base_GetItemQty(pRec, pEnv->nSelectedBaseIdx);
            const i32 nPending = (pSite != NULL && nSlot >= 0 && nSlot < 256)
                                     ? (i32)pSite->auItemQty[nSlot]
                                     : 0;

            if (nAvail - nPending > 0) { /* 0x963C0: the base must actually have the item */
                if ((nSlot > 76 && nSlot < 105) || nSlot == 114) {
                    /* 0x963D8: an alien containment slot. */
                    const i32 nContain = Base_CalcAlienContainmentCap(pEnv->abFacilityGrid,
                                                                      pEnv->nTransferBaseIdx);
                    const i32 nAmmo = Base_CountAmmoItems(pEnv->apObjects, pEnv->nObjectCount,
                                                          pEnv->nTransferBaseIdx);
                    if (nContain < nAmmo + 1) { /* 0x96400 */
                        return TransferShortfall(pEnv, PUFO_BUYSELL_SHORTFALL_CONTAINMENT,
                                                 false); /* 0x96614, written unconditionally */
                    }
                }
                const i32 nUsed = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount,
                                                       pEnv->nTransferBaseIdx); /* 0x96418 */
                const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount,
                                                                 pEnv->nSiteCount, pEnv->apBases,
                                                                 pEnv->nTransferBaseIdx,
                                                                 pEnv->apObjects, pEnv->nObjectCount,
                                                                 true); /* 0x96434 */
                const i32 nCap = Base_CalcStoresCapacity(pEnv->abFacilityGrid,
                                                         pEnv->nTransferBaseIdx); /* 0x96458 */
                const i32 nCargo = (pRec != NULL) ? (i32)pRec->bCargoSize : 0;
                if (nAcross + nCargo + nUsed > nCap) { /* 0x96464 */
                    return TransferShortfall(pEnv, PUFO_BUYSELL_SHORTFALL_STORES,
                                             true); /* 0x9662C, only when the flag is clear */
                }
                if (pSite != NULL && nSlot >= 0 && nSlot < 256) {
                    pSite->auItemQty[nSlot] = (u16)(pSite->auItemQty[nSlot] + 1); /* 0x96494 */
                }
                pEnv->pPanelD->anLineAux[nRow] -= 1; /* 0x964A8 */
                pEnv->pPanelE->anLineAux[nRow] += 1; /* 0x964C8 */
            }
        }
        if (nIter >= nLimit) {
            break; /* 0x9650C: `CMP count, timer/10; BLE` back to the top */
        }
    }
    return nLimit;
}

/* TransferScreen_CommitTransfer @ 0x97aa4: the transfer screen's confirm control. The header carries
 * the row pass, the commit gate and the commit body. */
SmapSite *TransferScreen_CommitTransfer(TransferCommitEnv *pEnv)
{
    SmapSite *pSite;
    i32 nSiteCount;
    i32 nSoldiersQueued = 0;
    i32 nCraftsQueued = 0;
    i32 i;

    if (pEnv == NULL || pEnv->pnSiteCount == NULL) {
        return NULL;
    }
    if (pEnv->pPanelA == NULL || pEnv->pPanelC == NULL || pEnv->pPanelE == NULL) {
        return NULL;
    }

    nSiteCount = *pEnv->pnSiteCount;
    pSite = BuySellSiteAt(pEnv->aSites, nSiteCount);
    if (pSite == NULL) {
        return NULL; /* deviation: the original indexes aSmapSites[nSmapSiteCount] blindly */
    }

    for (i = 0; i < pEnv->pPanelA->nLineCount; ++i) {
        const i32 nAux = pEnv->pPanelA->anLineAux[i];
        const i32 nRowId = pEnv->pPanelC->anLineAux[i];

        if (pEnv->pPanelE->anLineAux[i] != 1) {
            continue; /* 0x97B28 / 0x97C18: both branches want panel E aux exactly 1 */
        }

        if (nAux == PUFO_ROW_SOLDIER) {
            /* 0x97B58: the roster id lands in the site's soldier list (the original stores a dead
             * "+1" there first, then overwrites it). */
            if (nSoldiersQueued < (i32)(sizeof(pSite->anSoldierIds) / sizeof(pSite->anSoldierIds[0]))) {
                pSite->anSoldierIds[nSoldiersQueued] = (u16)nRowId;
            }
            ++nSoldiersQueued;
            for (i32 s = 0; s < pEnv->nSoldierCount; ++s) {
                Soldier *pSol = (pEnv->apSoldiers != NULL) ? pEnv->apSoldiers[s] : NULL;
                if (pSol != NULL && pSol->nUnk440 == nRowId) {
                    /* 0x97BB0: STRB #0xFF into Soldier.nKind (0x0B6) -- the soldier leaves the base. */
                    pSol->nKind = 0xFF;
                }
            }
        } else if (nAux > PUFO_TRANSFER_ROW_CRAFT_FIRST - 1 && nAux < PUFO_TRANSFER_ROW_CRAFT_LAST + 1) {
            /* 0x97C48: the craft id lands in the site's craft list. */
            if (nCraftsQueued < (i32)(sizeof(pSite->anCraftIds) / sizeof(pSite->anCraftIds[0]))) {
                pSite->anCraftIds[nCraftsQueued] = (u16)nRowId;
            }
            ++nCraftsQueued;
            for (i32 c = 0; c < pEnv->nCraftCount; ++c) {
                Ship *pShip = (pEnv->apCraft != NULL) ? pEnv->apCraft[c] : NULL;
                if (pShip != NULL && (i32)pShip->bId == nRowId) {
                    const i32 nHome = (i32)pShip->bHomeBase;
                    if (pEnv->abFacilityGrid != NULL && nHome >= 0 && nHome < PUFO_FAC_BASE_COUNT) {
                        /* 0x97CCC: clear the hangar cell, layer2[bHangarY][bHangarX] of the home base. */
                        pEnv->abFacilityGrid[444 + 144 * nHome + 6 * pShip->bHangarY + pShip->bHangarX] = 0;
                    }
                    pShip->bHomeBase = 0xFF; /* 0x97CD4 */
                }
            }
        }
    }

    {
        const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, nSiteCount, nSiteCount,
                                                         pEnv->apBases, pEnv->nTransferBaseIdx,
                                                         pEnv->apObjects, pEnv->nObjectCount,
                                                         false); /* 0x97D30, scan off */
        const i32 nCrew = (i32)pSite->auItemQty[112] + (i32)pSite->auItemQty[113]; /* 0x97D74 */
        const i32 nScores = Smap_SumSiteScores(pEnv->aSites, nSiteCount);           /* 0x97D88 */

        if (nAcross == 0 && nCraftsQueued == 0 && nSoldiersQueued == 0 && nCrew == 0 && nScores <= 0) {
            return NULL; /* deviation: the original returns the score count as a pointer */
        }
    }

    pSite->bFlag0 = PUFO_TRANSFER_SITE_FLAG_COMMIT; /* 0x97DA4 */
    if (pEnv->apBases != NULL && pEnv->nTransferBaseIdx >= 0 && pEnv->nTransferBaseIdx < PUFO_FAC_BASE_COUNT
        && pEnv->apBases[pEnv->nTransferBaseIdx] != NULL) {
        pSite->bFlag1 = pEnv->apBases[pEnv->nTransferBaseIdx]->bUnk4; /* 0x97DBC */
    }

    for (i = 0; i < pEnv->pPanelA->nLineCount; ++i) {
        pEnv->pPanelE->anLineAux[i] = 0; /* 0x97DE8 */
    }

    if (pEnv->apBases != NULL && pEnv->nSelectedBaseIdx >= 0
        && pEnv->nSelectedBaseIdx < PUFO_FAC_BASE_COUNT
        && pEnv->apBases[pEnv->nSelectedBaseIdx] != NULL) {
        BaseRec *pSrc = pEnv->apBases[pEnv->nSelectedBaseIdx];
        for (i = 0; i < pEnv->nObjectCount; ++i) {
            /* 0x97E5C: the pending quantities leave the selected base. */
            if (pEnv->apObjects != NULL && pEnv->apObjects[i] != NULL) {
                BaseRec_SubItemQty(pSrc, pEnv->apObjects[i], (i32)pSite->auItemQty[i]);
            }
        }
    }

    nSiteCount += 1; /* 0x97E8C */
    *pEnv->pnSiteCount = nSiteCount;

    for (i = 0; i < pEnv->nObjectCount; ++i) {
        if (Base_GetItemQty((pEnv->apObjects != NULL) ? pEnv->apObjects[i] : NULL,
                            pEnv->nSelectedBaseIdx) > 60000) { /* 0x97ED8, CMP #0xEA60 */
            if (pEnv->pfnShowTError != NULL) {
                pEnv->pfnShowTError(pEnv->nSelectedBaseIdx, i, pEnv->pUser); /* 0x97EE4 */
            }
        }
    }

    pSite = BuySellSiteAt(pEnv->aSites, nSiteCount);
    if (pSite != NULL) {
        pSite->bUnk518 = 0;                /* 0x97F30 */
        pSite->uRecoveredSoldiers = 0;     /* 0x97F3C */
        pSite->bRecoveredCraftCount = 0;   /* 0x97F48 */
        pSite->bRecoveredSoldierAssign = 0; /* 0x97F54 */
        pSite->bHasLoot = 0;               /* 0x97F58 */
        for (i = 0; i < pEnv->nObjectCount; ++i) {
            pSite->auItemQty[i] = 0;       /* 0x97F90 */
        }
    }
    return pSite;
}

static const char *TransferUi_BaseName(const TransferDrawEnv *pEnv, i32 nBaseIdx)
{
    if (pEnv->apSiteRecs == NULL || nBaseIdx < 0 || nBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return ""; /* deviation: the original indexes the eight records blindly */
    }
    return pEnv->apSiteRecs[nBaseIdx].abName; /* 0x186C40 + 64*i + 26 */
}

static const char *TransferUi_Text(const TransferDrawEnv *pEnv, i32 nRec)
{
    if (pEnv->pTextRecs == NULL) {
        return "";
    }
    return pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec;
}

/* 0x989C4 / 0x98A98: the working site's bFlag1 mirrors the destination base's type byte. */
static void TransferUi_SiteFlagFromBase(const TransferDrawEnv *pEnv, i32 nBaseIdx)
{
    const i32 nSiteCount = (pEnv->pnSiteCount != NULL) ? *pEnv->pnSiteCount : -1;
    SmapSite *pSite = BuySellSiteAt(pEnv->aSites, nSiteCount);

    if (pSite == NULL || pEnv->apBases == NULL || nBaseIdx < 0 || nBaseIdx >= PUFO_FAC_BASE_COUNT
        || pEnv->apBases[nBaseIdx] == NULL) {
        return;
    }
    pSite->bFlag1 = pEnv->apBases[nBaseIdx]->bUnk4;
}

static void TransferUi_DrawCentered(i32 nCenterX, i32 y, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz); /* 0x98400 */
    Text_DrawString(nCenterX - nWidth / 2, y, psz);
}

/* The shared screen frame rails: TransferScreen_Draw (0x97FB8..0x98150), ManufScreen_DrawFrame
 * (0x6F9C4..0x6FB54) and ResearchScreen_Draw (0x73A54..0x73BD8) each carry the same 96 blits with the
 * same constants -- the same loop bounds (CMP #0xA8 over a step of 7, CMP #0x78 over a step of 5), the
 * same x/y pairs, the same ramp formulas. Every index triple is stored with STRB, so each index is the
 * low byte of the ramp arithmetic. */
static void BaseUi_DrawFrameRails(void)
{
    /* The two side rails, 24 steps of 7. */
    for (i32 k = 0; k < PUFO_FRAME_RAIL_STEPS; ++k) {
        const i32 nY = 7 * k;
        const u8 nRise = (u8)(PUFO_FRAME_RAMP_STEP * (k + PUFO_FRAME_RAMP_BASE)); /* 100..215 */
        const u8 nFall = (u8)(PUFO_FRAME_RAMP_SIDE - PUFO_FRAME_RAMP_STEP * k);   /* 230..115 */

        SoftSurf_BlitRectProxy(0, 5, nY + 1, 1, 7, nRise, nRise, nRise);     /* 0x98004 / 0x6F9F4 / 0x73A84 */
        SoftSurf_BlitRectProxy(0, 5, nY + 151, 1, 7, nFall, nFall, nFall);   /* 0x98030 / 0x6FA24 / 0x73AB4 */
        SoftSurf_BlitRectProxy(0, 234, nY + 1, 1, 7, nRise, nRise, nRise);   /* 0x98054 / 0x6FA48 / 0x73AD8 */
        SoftSurf_BlitRectProxy(0, 234, nY + 151, 1, 7, nFall, nFall, nFall); /* 0x98078 / 0x6FA6C / 0x73AFC */
    }

    /* The top and bottom rails, 24 steps of 5. */
    for (i32 k = 0; k < PUFO_FRAME_RAIL_STEPS; ++k) {
        const i32 nXLo = 5 * k + 1;
        const i32 nXHi = 5 * k + 119;
        const u8 nRise = (u8)(PUFO_FRAME_RAMP_STEP * (k + PUFO_FRAME_RAMP_BASE)); /* 100..215 */
        const u8 nFall = (u8)(PUFO_FRAME_RAMP_EDGE - PUFO_FRAME_RAMP_STEP * k);   /* 250..135 */

        SoftSurf_BlitRectProxy(0, nXLo, 5, 5, 1, nRise, nRise, nRise);     /* 0x980C8 / 0x6FAC4 / 0x73B50 */
        SoftSurf_BlitRectProxy(0, nXLo, 315, 5, 1, nRise, nRise, nRise);   /* 0x980F0 / 0x6FAEC / 0x73B78 */
        SoftSurf_BlitRectProxy(0, nXHi, 5, 5, 1, nFall, nFall, nFall);     /* 0x98120 / 0x6FB1C / 0x73BA8 */
        SoftSurf_BlitRectProxy(0, nXHi, 315, 5, 1, nFall, nFall, nFall);   /* 0x98148 / 0x6FB44 / 0x73BD0 */
    }
}

/* TransferScreen_Draw @ 0x97fa4: the transfer screen's frame, panels, labels and destination
 * overlay. The header carries the branch map. */
i32 TransferScreen_Draw(const TransferDrawEnv *pEnv)
{
    char szLabel[PUFO_TRANSFER_LABEL_MAX];
    i32 nBand;

    if (pEnv == NULL) {
        return 0;
    }

    BaseUi_DrawFrameRails(); /* 0x97FB8..0x98150 */

    /* 0x98178: the row band is 16 pixels per row, capped by the panel's style. */
    nBand = (pEnv->pPanelB != NULL) ? 16 * pEnv->pPanelB->nLineCount : 0;
    if (pEnv->pPanelB != NULL && pEnv->pPanelB->nLineCount >= pEnv->pPanelB->nStyle) {
        nBand = 16 * pEnv->pPanelB->nStyle; /* 0x98194 */
    }

    SoftSurf_BlitRectProxy(0, 130, 69, 1, nBand + 2, PUFO_TRANSFER_SEP_INDEX,
                           PUFO_TRANSFER_SEP_INDEX, PUFO_TRANSFER_SEP_INDEX); /* 0x981A8 */
    SoftSurf_BlitRectProxy(0, 168, 69, 1, nBand + 2, PUFO_TRANSFER_SEP_INDEX,
                           PUFO_TRANSFER_SEP_INDEX, PUFO_TRANSFER_SEP_INDEX); /* 0x981CC */
    SoftSurf_BlitRectProxy(0, 183, 69, 1, nBand + 2, PUFO_TRANSFER_SEP_INDEX,
                           PUFO_TRANSFER_SEP_INDEX, PUFO_TRANSFER_SEP_INDEX); /* 0x981F0 */
    SoftSurf_BlitRectProxy(0, 198, 69, 1, nBand + 2, PUFO_TRANSFER_SEP_INDEX,
                           PUFO_TRANSFER_SEP_INDEX, PUFO_TRANSFER_SEP_INDEX); /* 0x98214 */

    if (pEnv->pbPanelsBuilt == NULL || *pEnv->pbPanelsBuilt == 0) {
        return 0; /* 0x98224 */
    }

    /* 0x98240..0x9825C: the original routes panel C's value through unk_161F0C, setting it to 1 and
     * back to 0 on the way; the observable effect is both panels flagged. */
    if (pEnv->pPanelB != NULL) {
        pEnv->pPanelB->bUnk7080 = 1; /* 0x98248 */
    }
    if (pEnv->pPanelC != NULL) {
        pEnv->pPanelC->bUnk7080 = 1; /* 0x98258 */
    }

    if (pEnv->pPanelA != NULL) {
        UiPanel_DrawText(pEnv->pPanelA); /* 0x98264 */
    }
    if (pEnv->pPanelE != NULL) {
        UiPanel_DrawText(pEnv->pPanelE); /* 0x98270 */
    }
    if (pEnv->pPanelC != NULL) {
        UiPanel_DrawText(pEnv->pPanelC); /* 0x98278 */
    }
    if (pEnv->pPanelB != NULL) {
        UiPanel_DrawText(pEnv->pPanelB); /* 0x98280 */
    }
    if (pEnv->pPanelD != NULL) {
        UiPanel_DrawText(pEnv->pPanelD); /* 0x9828C */
    }

    /* 0x98290..0x98340: the row under the map cursor, named at (10, 282). */
    if ((pEnv->pbOverlayOpen == NULL || *pEnv->pbOverlayOpen == 0)
        && pEnv->pbPreviewOk != NULL && *pEnv->pbPreviewOk != 0
        && pEnv->pnCursorY != NULL && *pEnv->pnCursorY > 75 && *pEnv->pnCursorY < nBand + 75) {
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 150, 150, 255, 20, 20, 55); /* 0x982F4 */
        if (pEnv->pPanelA != NULL && pEnv->pnRowCursor != NULL
            && *pEnv->pnRowCursor >= 0 && *pEnv->pnRowCursor < PUFO_PANEL_LINES) {
            const i32 nSlot = pEnv->pPanelA->anLineAux[*pEnv->pnRowCursor]; /* 0x98310 */
            const char *pszName;
            if (nSlot == PUFO_ROW_SOLDIER) {
                pszName = TransferUi_Text(pEnv, PUFO_BUYSSELL_SOLDIER_TEXT_REC); /* 0x98328, "Soldier" */
            } else {
                const ObjectRecFull *pRec = BuySellObjectAt(pEnv->apObjects, pEnv->nObjectCount,
                                                            nSlot);
                pszName = (pRec != NULL) ? (const char *)pRec : ""; /* 0x98330, szName at +0 */
            }
            Text_DrawString(10, 282, pszName); /* 0x98340 */
        }
    }

    (void)UiControl_BlitActivePad(pEnv->pCtlPrev, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser);   /* 0x98388 */
    (void)UiControl_BlitActivePad(pEnv->pCtlNext, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser);   /* 0x98390 */
    (void)UiControl_BlitActivePad(pEnv->pCtlReset, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser);   /* 0x9839C */

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x983C8 */
    TransferUi_DrawCentered(60, 300, TransferUi_Text(pEnv, PUFO_TRANSFER_LABEL_DEST_CAP));  /* 0x98420 */
    TransferUi_DrawCentered(180, 300, TransferUi_Text(pEnv, PUFO_TRANSFER_LABEL_XFER_CAP)); /* 0x98478 */

    /* 0x98498..0x98510: "From: " with the source base's name. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 20, 20, 20);
    pufo_strncpy(szLabel, TransferUi_Text(pEnv, PUFO_TRANSFER_LABEL_FROM), sizeof(szLabel));
    (void)pufo_strcat(szLabel, TransferUi_BaseName(pEnv, pEnv->nSelectedBaseIdx), sizeof(szLabel));
    TransferUi_DrawCentered(120, 20, szLabel);

    /* 0x98538..0x986B4: the shortfall panel. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30);
    if (pEnv->pdwShortfall != NULL && *pEnv->pdwShortfall == 1 && pEnv->pnResidue != NULL) {
        i32 nRec = -1;
        switch (*pEnv->pnResidue) { /* 0x98558 */
        case PUFO_BUYSELL_SHORTFALL_LIVING:      nRec = PUFO_TRANSFER_SHORTFALL_REC;      break;
        case PUFO_BUYSELL_SHORTFALL_FUNDS:       nRec = PUFO_TRANSFER_SHORTFALL_REC + 1;  break;
        case PUFO_BUYSELL_SHORTFALL_HANGAR:      nRec = PUFO_TRANSFER_SHORTFALL_REC + 2;  break;
        case PUFO_BUYSELL_SHORTFALL_STORES:      nRec = PUFO_TRANSFER_SHORTFALL_REC + 3;  break;
        case PUFO_BUYSELL_SHORTFALL_CONTAINMENT: nRec = PUFO_TRANSFER_SHORTFALL_ALIEN;    break;
        case PUFO_BUYSELL_SHORTFALL_TRANSFER:    nRec = PUFO_TRANSFER_SHORTFALL_BLOCK;    break;
        default: break;
        }
        if (nRec >= 0) {
            (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1);
            TransferUi_DrawCentered(120, 158, TransferUi_Text(pEnv, nRec));
            return 0; /* 0x98B44: the original returns Text_DrawString's value */
        }
    }

    /* 0x986BC..0x986D8: with the overlay closed, "TO: " with the destination base's name. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 20, 20, 20);
    if (pEnv->pbOverlayOpen == NULL || *pEnv->pbOverlayOpen == 0) {
        pufo_strncpy(szLabel, TransferUi_Text(pEnv, PUFO_TRANSFER_LABEL_TO), sizeof(szLabel));
        (void)pufo_strcat(szLabel, TransferUi_BaseName(pEnv, pEnv->nDestBaseIdx), sizeof(szLabel));
        TransferUi_DrawCentered(120, 32, szLabel);
        return 0; /* 0x98B2C */
    }

    /* 0x986F8..0x988CC: the destination overlay. */
    SoftSurf_BlitSrcRect(0, 0, 240, 320, 0, 50, 119, 200); /* 0x986F8 */
    const i32 nBaseCount = (pEnv->pbBaseCount != NULL) ? (i32)(i8)*pEnv->pbBaseCount : 0;
    SoftSurf_BlitRectProxy(0, 208, 0, 32, 28 * nBaseCount + 4, 0, 0, 0); /* 0x98738 */
    if (pEnv->pfnDrawIconStrip != NULL) {
        pEnv->pfnDrawIconStrip(pEnv->nSelectedBaseIdx, pEnv->pStripUser); /* 0x98744 */
    }
    SoftSurf_BlitRectProxy(0, 0, 0, 210, 28 * nBaseCount + 4, 0, 0, 0); /* 0x9877C */

    for (i32 i = 0; i < nBaseCount; ++i) {
        const i32 nY = 28 * i;
        SoftSurf_BlitRectProxy(0, 3, nY + 3, 205, 26, 0x46, 0xA0, 0xB3);  /* 0x987D8 */
        SoftSurf_BlitRectProxy(0, 3, nY + 4, 204, 24, 0x00, 0x6E, 0x77);  /* 0x9880C */
        SoftSurf_BlitRectProxy(0, 5, nY + 5, 201, 22, 0x04, 0x46, 0x4B);  /* 0x9883C */
        pufo_strncpy(szLabel, TransferUi_BaseName(pEnv, i), sizeof(szLabel));
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);  /* 0x9887C */
        TransferUi_DrawCentered(110, nY + 7, szLabel);                    /* 0x988A8 */
    }

    (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser);    /* 0x988DC */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);
    TransferUi_DrawCentered(120, 28 * nBaseCount + 10,
                            TransferUi_Text(pEnv, PUFO_TRANSFER_LABEL_STOP)); /* 0x98968 */

    /* 0x98980..0x989F4: resolve the tap and close the overlay when it lands on another base. */
    const i32 nTap = Base_HitTestFacilityStrip(pEnv->pStripHit,
                                               (pEnv->pnCursorX != NULL) ? *pEnv->pnCursorX : 0,
                                               (pEnv->pnCursorY != NULL) ? *pEnv->pnCursorY : 0);
    if (pEnv->pbTransferBase != NULL) {
        *pEnv->pbTransferBase = (i8)nTap; /* 0x98994 */
    }
    TransferUi_SiteFlagFromBase(pEnv, nTap); /* 0x989C4 */
    /* ORIGINAL BUG (0x989D4): the compare at 0x989D0 and the store it guards at 0x989E0 are
     * unreachable. The draw returns at 0x98224 unless bTransferPanelsBuilt @ 0x1870EC is set, and
     * Base_HitTestFacilityStrip disables the strip on that same byte -- so the overlay path can only
     * ever run with the gate open, the gate forces a miss, and a miss returns nSelectedBaseIdx, the
     * very value compared here. The overlay is therefore never closed by this code, and the store at
     * 0x98994 pins the destination base byte_1870F4 to the source base on every overlay frame. The
     * overlay is closed by the geoscape pointer handlers (Geoscape_OnPointer 0x88928,
     * Geoscape_OnPointerUp 0x8A674) instead. Reproduced, not fixed. */
    if (nTap != (i32)pEnv->nSelectedBaseIdx && pEnv->pbOverlayOpen != NULL) {
        *pEnv->pbOverlayOpen = 0; /* 0x989E0 */
    }
    if (pEnv->pnCursorX != NULL) {
        *pEnv->pnCursorX = 0; /* 0x989E8 */
    }
    if (pEnv->pnCursorY != NULL) {
        *pEnv->pnCursorY = 0; /* 0x989F4 */
    }

    const i32 nPhase = (pEnv->pnMapPhase != NULL) ? *pEnv->pnMapPhase : 0;
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250 - 40 * nPhase, 200, 200, 20, 20, 20); /* 0x98A1C */
    TransferUi_DrawCentered(120, 282, TransferUi_Text(pEnv, PUFO_TRANSFER_LABEL_SELECT)); /* 0x98A78 */

    TransferUi_SiteFlagFromBase(pEnv, (pEnv->pbTransferBase != NULL) ? (i32)*pEnv->pbTransferBase
                                                                     : nTap); /* 0x98A98 */
    return (pEnv->pnSiteCount != NULL) ? *pEnv->pnSiteCount : 0; /* 0x98A80 */
}

/* The six openers share one body. bIdAfterInit carries the ordering difference the
 * disassembly shows: Manuf (0x614EC), Transfer (0x6151C) and BaseInfo (0x6169C) write the
 * screen id after the Init, BuySell (0x617AC, 0x617DC) and BaseFac (0x6185C) before it. */
static void Geoscape_OpenScreen(GeoscapeScreenState *pState, i32 nScreenId,
                                bool bIdAfterInit, const ScreenInitHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(17, 4, pHooks->pUser); /* MOV R1, #4 / MOV R0, #0x11 */
    }
    if (!bIdAfterInit && pState != NULL) {
        pState->nGeoscapeScreen = nScreenId;
    }
    if (pHooks != NULL && pHooks->pfnInit != NULL) {
        pHooks->pfnInit(pHooks->pSlot, pHooks->pUser);
    }
    if (bIdAfterInit && pState != NULL) {
        pState->nGeoscapeScreen = nScreenId;
    }
}

void ManufScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 9, true, pHooks);
}

void TransferScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 10, true, pHooks);
}

void BaseInfoScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 3, true, pHooks);
}

void BuySellScreen_OpenBuy(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 11, false, pHooks);
}

void BuySellScreen_OpenSell(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 12, false, pHooks);
}

void BaseFacScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 7, false, pHooks);
}

/* BasePlaneScreen_Open @ 0x617F4: sample 17, the id 6 written at 0x61828 BEFORE the Init
 * tail call to BasePlaneScreen_Init (0x7d9e4) with the slot at unk_161A48. Same shape as
 * BuySell and BaseFac, so it reuses the shared opener. */
void BasePlaneScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks)
{
    Geoscape_OpenScreen(pState, 6, false, pHooks);
}

void BaseAtScreen_Open(UiControl *pCtl, i32 nMode, const BaseAtScreenOpenHooks *pHooks)
{
    if (pCtl != NULL) {
        pCtl->x = (i16)(120 - (pCtl->uWidth >> 1)); /* 0x588E4: LDRH, LSR #1, RSB #0x78 */
        pCtl->y = (i16)(0x120 | 2);                 /* 0x588F4: 290 built as ORR #2 */
        if (pHooks != NULL) {
            pCtl->pfnClick = pHooks->pfnClick;      /* 0x58900: STR [R1,#0xC] */
        }
    }
    if (pHooks != NULL && pHooks->pfnUpdate != NULL) {
        pHooks->pfnUpdate(nMode); /* 0x58908, tail call. nMode is R1; R0 was overwritten. */
    }
}

/* The two release leaves the dispatcher uses. The original frees through UiControl_FreePads /
 * PadImage_FreeData plus operator delete and never nulls the slot it read; the port nulls it,
 * because 0x159888 and 0x161A08 are read again by the next build of the same screen. */
static void CloseFreeCtlArray(UiControl **apCtl, i32 nCount)
{
    for (i32 i = 0; i < nCount; ++i) {
        if (apCtl[i] != NULL) {
            UiControl_FreePads(apCtl[i]); /* 0x8BEB8 / 0x8BEEC / 0x8BF1C */
            free(apCtl[i]);               /* 0x8BEC0 / 0x8BEF4 / 0x8BF24 */
            apCtl[i] = NULL;
        }
    }
}

static void CloseFreePad(PadImage **ppPad)
{
    if (ppPad != NULL && *ppPad != NULL) {
        PadImage_FreeData(*ppPad); /* 0x8BE10 / 0x8C0F0 / 0x8C13C / 0x8BB4C */
        free(*ppPad);              /* 0x8BE18 / 0x8C0F8 / 0x8C144 / 0x8BB54 */
        *ppPad = NULL;
    }
}

static void CloseClampScroll(const GeoscapeCloseEnv *pEnv)
{
    if (pEnv->pView != NULL) {
        const i32 nStripH = (pEnv->pStripPad != NULL) ? (i32)pEnv->pStripPad->uHeight : 0;
        Smap_ClampScrollY(pEnv->pView, nStripH); /* 0x8BE60 / 0x8BF38 */
    }
}

static i32 CloseSelectedBase(const GeoscapeCloseEnv *pEnv)
{
    return (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
}

static i32 CloseBaseCount(const GeoscapeCloseEnv *pEnv)
{
    return (pEnv->pnBaseCount != NULL) ? (i32)*pEnv->pnBaseCount : 0;
}

/* Geoscape_CloseUI @ 0x8b94c: the dispatcher every screen close reaches. The header carries the
 * case-by-case evidence; the addresses below are the ARM sites, not Hex-Rays' rendering. */
void Geoscape_CloseUI(const GeoscapeCloseEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pnScreenId == NULL) {
        return;
    }

    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pSoundUser); /* 0x8B95C: Sound_Play(17, 4) */
    }
    if (pEnv->pnActionFail != NULL) {
        *pEnv->pnActionFail = 0;                   /* 0x8B96C: STR, a dword clear */
    }
    if (pEnv->pbPopupA != NULL) { *pEnv->pbPopupA = 0; } /* 0x8B978: STRB [R5,#0x34] */
    if (pEnv->pbPopupB != NULL) { *pEnv->pbPopupB = 0; } /* 0x8B97C: STRB [R5,#0x35] */

    /* 0x8B980..0x8B9D0. The 3600 arm calls SetSpeedIdx4 and then branches into the 43200 arm's
     * SetSpeedIdx5 at 0x8B9E0, so a 3600 tick lands on index 5 and re-arms the sixth control. */
    if (pEnv->pWorld != NULL && pEnv->apSpeedCtl != NULL) {
        const i32 nTick = pEnv->pWorld->nTickSeconds;
        if (nTick == 5) {
            GameTime_Set5Sec(pEnv->pWorld, pEnv->apSpeedCtl);      /* 0x8B9FC */
        } else if (nTick == 60) {
            GameTime_Set1Min(pEnv->pWorld, pEnv->apSpeedCtl);      /* 0x8B9F4 */
        } else if (nTick == 300) {
            GameTime_Set5Min(pEnv->pWorld, pEnv->apSpeedCtl);      /* 0x8B9EC */
        } else if (nTick == 1800) {
            GameTime_SetSpeedIdx3(pEnv->pWorld, pEnv->apSpeedCtl); /* 0x8B9E4 */
        } else if (nTick == 3600) {
            /* ORIGINAL BUG (0x8B9E0): the 3600 arm calls SetSpeedIdx4 and then branches into the
             * 43200 arm's SetSpeedIdx5, so a 3600 tick lands on index 5 and re-arms the sixth
             * speed control. The chain's constants also disagree with the table it writes --
             * 1800 and 43200 are values no speed control produces, and every arm that does fire
             * rewrites the tick (1800 -> 1380, 3600 -> 2460 -> 3600, 43200 -> 3600). */
            GameTime_SetSpeedIdx4(pEnv->pWorld, pEnv->apSpeedCtl); /* 0x8B9DC */
            GameTime_SetSpeedIdx5(pEnv->pWorld, pEnv->apSpeedCtl); /* 0x8B9E0 -> 0x8B9CC */
        } else if (nTick == 43200) {
            GameTime_SetSpeedIdx5(pEnv->pWorld, pEnv->apSpeedCtl); /* 0x8B9CC */
        }
    }

    /* 0x8BA00..0x8BA80: the five needed-flag passes. nBaseCount is loaded with LDRSB at 0x8BA10
     * and re-loaded at 0x8BA70 on every iteration, so a negative count runs nothing and a count
     * that grows mid-loop runs on. The counter it leaves in R2 is case 18's third argument. */
    i32 nBaseCount = CloseBaseCount(pEnv);
    if (pEnv->abFacilityGrid != NULL) {
        for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {
            const i32 nSpace = Base_CalcOccupiedSpace(pEnv->apSoldiers, pEnv->nSoldierCount,
                                                      pEnv->apObjects, pEnv->nObjectCount, nBase);
            const i32 nStores = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase);
            (void)Base_UpdateLivingQuartersNeeded(pEnv->abFacilityGrid, nBase,
                nSpace + Base_CountOutboundTransferSpace(pEnv->apSites, pEnv->nSiteCount, -1,
                                                         pEnv->apBases, nBase, true)); /* 0x8BA34 */
            (void)Base_UpdateStoresNeeded(pEnv->abFacilityGrid, nBase,
                nStores + Base_CountItemQtyAcrossBases(pEnv->apSites, pEnv->nSiteCount, -1,
                                                       pEnv->apBases, nBase, pEnv->apObjects,
                                                       pEnv->nObjectCount, true));      /* 0x8BA40 */
            (void)Base_UpdatePsiLabNeeded(pEnv->abFacilityGrid, nBase,
                Base_CountPsiSoldiers(pEnv->apSoldiers, pEnv->nSoldierCount, nBase));  /* 0x8BA4C */
            (void)Base_UpdateLabNeeded(pEnv->abFacilityGrid, nBase,
                Base_CountActiveResearch(pEnv->apResearchCol + PUFO_RESEARCH_TOPIC_COUNT * nBase,
                                         pEnv->nResearchEntries));                      /* 0x8BA58 */
            (void)Base_UpdateWorkshopNeeded(pEnv->abFacilityGrid, nBase,
                Base_CountActiveManuf(pEnv->apManufCol + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                      PUFO_PRODUCT_ENTRIES_PER_BASE));                  /* 0x8BA64 */
            nBaseCount = CloseBaseCount(pEnv);                                         /* 0x8BA70 */
        }
    }

    switch (*pEnv->pnScreenId) {
    case 1: {
        *pEnv->pnScreenId = 0;                                        /* 0x8BE94 */
        if (pEnv->pbPlaceActive != NULL) { *pEnv->pbPlaceActive = 0; } /* 0x8BE9C, [2] first */
        if (pEnv->pbPlaceFail != NULL) { *pEnv->pbPlaceFail = 0; }     /* 0x8BEA4, then [1] */
        if (pEnv->pNav != NULL) {
            CloseFreeCtlArray(pEnv->pNav->apNav, PUFO_NAV_CTLS);        /* 0x8BEA8, 10 slots */
            CloseFreeCtlArray(pEnv->pNav->apPanel, PUFO_NAV_PANEL_CTLS); /* 0x8BEDC, 2 slots */
            CloseFreeCtlArray(pEnv->pNav->apFind, PUFO_NAV_FIND_CTLS);   /* 0x8BF0C, 3 slots */
        }
        CloseClampScroll(pEnv);                                        /* 0x8BF38 */
        return;
    }
    case 2:
        if (pEnv->pbBaseFacConfirm != NULL && *pEnv->pbBaseFacConfirm != 0) {
            return;                                                    /* 0x8BE58 */
        }
        CloseClampScroll(pEnv);                                        /* 0x8BE60 */
        *pEnv->pnScreenId = 1;                                         /* 0x8BE68 */
        if (pEnv->pbBaseFacDone != NULL) { *pEnv->pbBaseFacDone = 0; } /* 0x8BE70, +4 of 0x187074 */
        if (pEnv->pbBaseFacArmed != NULL) { *pEnv->pbBaseFacArmed = 0; } /* 0x8BE74 -> 0x8BFF4 */
        return;
    case 3:
        *pEnv->pnScreenId = 1;                                         /* 0x8BDE8 */
        if (pEnv->pbBaseInfoBuilt != NULL && *pEnv->pbBaseInfoBuilt != 0 &&
            pEnv->pfnBuildBaseInfoPanels != NULL) {
            pEnv->pfnBuildBaseInfoPanels(pEnv->pUser);                 /* 0x8BDF8 */
        }
        CloseFreePad(pEnv->ppBaseInfoPad);                             /* 0x8BE10 / 0x8BE18 */
        return;
    case 4:
        *pEnv->pnScreenId = 1;                                         /* 0x8BFC4 */
        if (pEnv->pbSoldierOpen != NULL && *pEnv->pbSoldierOpen != 0) {
            SoldierScreen_Toggle(pEnv->pSoldierScreen);                /* 0x8BFD8 */
        }
        return;
    case 5:
        if (pEnv->pbTransferFlag != NULL && *pEnv->pbTransferFlag != 0) {
            *pEnv->pbTransferFlag = 0;                                 /* 0x8BFF0 -> 0x8BFF4 */
            return;
        }
        if (pEnv->pbPopupA != NULL) { *pEnv->pbPopupA = 0; }           /* 0x8C000 */
        if (pEnv->pbPopupB != NULL) { *pEnv->pbPopupB = 0; }           /* 0x8C004 */
        if (pEnv->pbTransferFlag != NULL) { *pEnv->pbTransferFlag = 0; } /* 0x8C008 */
        *pEnv->pnScreenId = 4;                                         /* 0x8C00C -> 0x8C010 */
        return;
    case 6:
        if (pEnv->pbPlaneListBuilt != NULL && *pEnv->pbPlaneListBuilt != 0) {
            if (pEnv->pfnBuildPlaneListPanels != NULL) {
                pEnv->pfnBuildPlaneListPanels(pEnv->pUser);            /* 0x8C028 */
            }
            return;
        }
        if (pEnv->pbPlaneStores != NULL && *pEnv->pbPlaneStores != 0) {
            if (pEnv->pfnBuildPlaneControls != NULL) {
                pEnv->pfnBuildPlaneControls(pEnv->pUser);              /* 0x8C044 */
            }
            return;
        }
        if ((pEnv->pbStatPanelA != NULL && *pEnv->pbStatPanelA != 0) ||
            (pEnv->pbStatPanelB != NULL && *pEnv->pbStatPanelB != 0)) {
            if (pEnv->pbStatPanelA != NULL) { *pEnv->pbStatPanelA = 0; } /* 0x8C0B0 */
            if (pEnv->pbStatPanelB != NULL) { *pEnv->pbStatPanelB = 0; } /* 0x8C0B4 */
            return;
        }
        if (pEnv->pbPlaneBuilt == NULL || *pEnv->pbPlaneBuilt == 0) {
            return;                                                    /* 0x8C07C */
        }
        if (pEnv->pbPlaneDetail != NULL && *pEnv->pbPlaneDetail != 0) {
            *pEnv->pbPlaneDetail = 0;                                  /* 0x8C08C -> 0x8BFF4 */
            return;
        }
        *pEnv->pnScreenId = 1;                                         /* 0x8C094 */
        if (pEnv->pfnBuildPlanePanels != NULL) {
            pEnv->pfnBuildPlanePanels(pEnv->pUser);                    /* 0x8C098 */
        }
        return;
    case 7: {
        const bool bDone = (pEnv->pbBaseFacDone != NULL && *pEnv->pbBaseFacDone != 0);
        const bool bDialog = (pEnv->pbBaseFacDialog != NULL && *pEnv->pbBaseFacDialog != 0);
        if (bDone && bDialog) {
            /* ORIGINAL BUG (0x8BF74): LDRSB / SUB #1 / STRB with no floor, so a zero
             * nSelectedBaseIdx wraps to 255 and the facility screen is then toggled for a base
             * that does not exist. */
            if (pEnv->pnSelectedBaseIdx != NULL) {
                *pEnv->pnSelectedBaseIdx = (i8)(u8)((i32)*pEnv->pnSelectedBaseIdx - 1);
            }
            BaseFacScreen_Toggle(pEnv->pFacToggle);                    /* 0x8BF80 */
            *pEnv->pnScreenId = 1;                                     /* 0x8BF88 */
        } else if (pEnv->pbBaseFacArmed == NULL || *pEnv->pbBaseFacArmed == 0) {
            if (bDialog) {
                BaseFacScreen_Toggle(pEnv->pFacToggle);                /* 0x8BFB8 -> 0x8BF80 */
            }
            *pEnv->pnScreenId = 1;                                     /* 0x8BF84 */
        } else {
            *pEnv->pbBaseFacArmed = 0;                                 /* 0x8BFA4 */
        }
        if (pEnv->pbPopupA != NULL) { *pEnv->pbPopupA = 0; }           /* 0x8BFA8 */
        if (pEnv->pbPopupB != NULL) { *pEnv->pbPopupB = 0; }           /* 0x8BFAC */
        return;
    }
    case 8:
        if (pEnv->pbRowSelected != NULL && *pEnv->pbRowSelected != 0) {
            (void)ResearchScreen_StartSelected(pEnv->pResearchStart);  /* 0x8BDB4 */
        } else {
            *pEnv->pnScreenId = 1;                                     /* 0x8BDC4 */
            if (pEnv->pbResearchBuilt != NULL && *pEnv->pbResearchBuilt != 0) {
                ResearchScreen_BuildPanels(pEnv->pResearchBuild);      /* 0x8BDD8 */
            }
        }
        return;
    case 9:
        if (pEnv->pbManufSelected != NULL && *pEnv->pbManufSelected != 0) {
            ManufScreen_Draw(pEnv->pManufDraw);                        /* 0x8BD78 */
        } else if (pEnv->pbManufBuilt != NULL && *pEnv->pbManufBuilt != 0) {
            ManufScreen_BuildPanels(pEnv->pManufBuild);                /* 0x8BD94 */
            *pEnv->pnScreenId = 1;                                     /* 0x8BD98 -> 0x8C010 */
        }
        return;
    case 10:
        *pEnv->pnScreenId = 1;                                         /* 0x8BE2C */
        if (pEnv->pbTransferBuilt != NULL && *pEnv->pbTransferBuilt != 0) {
            TransferScreen_BuildPanels(pEnv->pTransferBuild);          /* 0x8BE40 */
        }
        return;
    case 11:
        if (pEnv->pbPopupB != NULL) { *pEnv->pbPopupB = 1; }           /* 0x8C0C4 */
        if (pEnv->pbBuySellCtlBuilt != NULL && *pEnv->pbBuySellCtlBuilt != 0) {
            BuySellScreen_BuildControls(pEnv->pBuySellCtl);            /* 0x8C0D0 */
        }
        *pEnv->pnScreenId = 1;                                         /* 0x8C0D8 */
        CloseFreePad(pEnv->ppBuySellPad);                              /* 0x8C0F0 / 0x8C0F8 */
        if (pEnv->pbPopupA != NULL) { *pEnv->pbPopupA = 0; }           /* 0x8BFA8 */
        if (pEnv->pbPopupB != NULL) { *pEnv->pbPopupB = 0; }           /* 0x8BFAC: the 1 above
                                                                        * lived only for the build */
        return;
    case 12:
        if (pEnv->pbPopupB != NULL) { *pEnv->pbPopupB = 1; }           /* 0x8C110 */
        if (pEnv->pbBuySellBuilt != NULL && *pEnv->pbBuySellBuilt != 0) {
            BuySellScreen_BuildPanels(pEnv->pBuySell);                 /* 0x8C11C */
        }
        *pEnv->pnScreenId = 1;                                         /* 0x8C124 */
        CloseFreePad(pEnv->ppBuySellPad);                              /* 0x8C13C / 0x8C144 */
        if (pEnv->pbPopupA != NULL) { *pEnv->pbPopupA = 0; }           /* 0x8C148 */
        if (pEnv->pbPopupB != NULL) { *pEnv->pbPopupB = 0; }           /* 0x8C150 */
        (void)Base_RemoveAllShips(pEnv->pClearShips, CloseSelectedBase(pEnv)); /* 0x8C160 */
        (void)Base_RemoveAllSoldiers(pEnv->anSoldierIdList, pEnv->apSoldiers,
                                     pEnv->nSoldierCount);             /* 0x8C174 */
        return;
    case 15:
        (void)FundingScreen_Open(pEnv->pFundingToggle, pEnv->pFundingOpen); /* 0x8BCB0 */
        *pEnv->pnScreenId = 0;                                         /* 0x8BCB4 */
        return;
    case 16:
        if (pEnv->pbUfopaediaDirty != NULL && *pEnv->pbUfopaediaDirty != 0) {
            if (pEnv->pfnUfopaediaRefresh != NULL) {
                pEnv->pfnUfopaediaRefresh(pEnv->pUser);                /* 0x8BD3C */
            }
        } else if (pEnv->pbUfopaediaOpen != NULL && *pEnv->pbUfopaediaOpen != 0) {
            *pEnv->pbUfopaediaOpen = 0;                                /* 0x8BD48 -> 0x8BFF4 */
        } else {
            if (pEnv->pfnUfopaediaOpen != NULL) {
                pEnv->pfnUfopaediaOpen(pEnv->pUser);                   /* 0x8BD58 */
            }
            *pEnv->pnScreenId = 0;                                     /* 0x8BD5C */
            CloseClampScroll(pEnv);                                    /* 0x8BF38 */
        }
        return;
    case 18:
        if (pEnv->pbOptionsActive != NULL && *pEnv->pbOptionsActive != 0) {
            const i32 nMode = (pEnv->pnOptionsMode != NULL) ? (i32)*pEnv->pnOptionsMode : 0;
            if (nMode != 0) {
                if (pEnv->pnOptionsMode != NULL) { *pEnv->pnOptionsMode = 0; } /* 0x8BCFC */
                if (pEnv->pSaveAudio != NULL) {
                    /* ORIGINAL BUG (0x8BD00): a1 is the gOptionsScratch block, a2 the signed byte just
                     * read (LDRSB at 0x8BCD0), and a3 is whatever the base loop left in R2 at 0x8BA70
                     * -- the base count, not a value this function computes. The callee reads none of
                     * the three: 0x6B824 overwrites R0 with Map_nSoundVolume before its first call and
                     * R1/R2 are never read, so the port hands one env instead. */
                    (void)Options_SaveAudioSettings(pEnv->pSaveAudio);
                }
            } else {
                OptionsScreen_RebuildWithSound(pEnv->pOptionsHooks);   /* 0x8BCE4 */
                *pEnv->pnScreenId = 0;                                 /* 0x8BCE8 */
            }
        }
        if (pEnv->pbOptionsOpen != NULL && *pEnv->pbOptionsOpen != 0) {
            *pEnv->pnScreenId = 0;                                     /* 0x8BD14 */
            if (pEnv->pOptionsOpen != NULL) {
                OptionsScreen_Open(pEnv->pOptionsOpen, -1);            /* 0x8BD18 */
            }
        }
        if (pEnv->pbDragActive != NULL) { *pEnv->pbDragActive = 0; }   /* 0x8BD1C -> 0x8BFF4 */
        return;
    case 19: {
        CloseFreePad(pEnv->ppFundPad);                                 /* 0x8BB4C / 0x8BB54 */
        nBaseCount = CloseBaseCount(pEnv);                             /* 0x8BB60 */

        i32 nUpkeep = 0;                                               /* R8, zeroed at 0x8B968 */
        for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {             /* 0x8BB74 gates on > 0 */
            nUpkeep += Base_CalcMonthlyUpkeep(pEnv->apCraft,
                                              (pEnv->pnCraftCount != NULL)
                                                  ? (i32)*pEnv->pnCraftCount : 0,
                                              pEnv->apSoldiers, pEnv->nSoldierCount,
                                              pEnv->apSites, pEnv->nSiteCount, pEnv->apObjects,
                                              pEnv->abFacilityGrid, nBase); /* 0x8BB84 */
            nBaseCount = CloseBaseCount(pEnv);                         /* 0x8BB90 */
        }

        if (pEnv->pnFunds != NULL && pEnv->anFundBar != NULL) {
            *pEnv->pnFunds += pEnv->anFundBar[2] - nUpkeep + pEnv->anFundBar[1]
                            + pEnv->anFundBar[3] + pEnv->anFundBar[0] + pEnv->anFundBar[4];
            if (*pEnv->pnFunds < PUFO_FUNDS_BANKRUPT_LIMIT) {          /* 0x8BBE8: BGE skips this */
                if (pEnv->pfnFreeGeoscapeUi != NULL) {
                    pEnv->pfnFreeGeoscapeUi(1, pEnv->pUser);           /* 0x8BBF0 */
                }
                if (pEnv->pEndScreen != NULL) {
                    EndScreen_Init(pEnv->pEndScreen, 0);               /* 0x8BBF8 */
                }
                if (pEnv->pbTickHold != NULL) { *pEnv->pbTickHold = 1; }    /* 0x8BC04 */
                if (pEnv->pbBattleEnded != NULL) { *pEnv->pbBattleEnded = 1; } /* 0x8BC0C */
                return;
            }
        }

        i32 nPsiBase = -1;
        if (pEnv->abFacilityGrid != NULL) {
            for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {          /* 0x8BC30 gates on > 0 */
                if (Base_CalcPsiLabCap(pEnv->abFacilityGrid, nBase) > 0) { /* 0x8BC40, BGT found */
                    nPsiBase = nBase;
                    break;
                }
                nBaseCount = CloseBaseCount(pEnv);                      /* 0x8BC54 */
            }
        }
        if (nPsiBase >= 0) {
            if (pEnv->pnShownBaseIdx != NULL) { *pEnv->pnShownBaseIdx = (u8)nPsiBase; } /* 0x8BC74 */
            if (pEnv->pnPsiBaseIdx != NULL) { *pEnv->pnPsiBaseIdx = (i8)nPsiBase; }     /* 0x8BC80 */
            *pEnv->pnScreenId = 17;                                     /* 0x8BC90 */
            if (pEnv->pfnPsiInit != NULL) {
                pEnv->pfnPsiInit(pEnv->pUser);                          /* 0x8BC98 */
            }
        } else {
            *pEnv->pnScreenId = 0;                                      /* 0x8BCB4 */
        }
        return;
    }
    case 20:
        if (pEnv->pfnBaseAtOpen != NULL) {
            pEnv->pfnBaseAtOpen(pEnv->pUser);                           /* 0x8BB00 */
        }
        *pEnv->pnScreenId = 0;                                          /* 0x8BB08 */
        if (pEnv->pbBaseAtAssault != NULL && *pEnv->pbBaseAtAssault != 0) {
            if (pEnv->pbMissionMode != NULL) { *pEnv->pbMissionMode = 1; } /* 0x8BB20 */
            (void)Smap_BeginCraftSelect(pEnv->pCraftSelect);           /* 0x8BB24 */
        }
        return;
    default:
        return;                                                         /* def_8BA94, 13/14/17 */
    }
}

/* Thirteen thunks, one tail call: LDR R0, =0x1864F8 / B Geoscape_CloseUI. */
void BaseInfoScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void BaseFacScreen_CloseThunkA(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void BaseFacScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void SoldierScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void ManufScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void ResearchScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void BuySellScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void NewBaseScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    if (pEnv != NULL && pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pSoundUser); /* 0x615BC, and CloseUI plays 17 again */
    }
    Geoscape_CloseUI(pEnv); /* 0x615C8 */
}

i32 OptionsScreen_ReturnToTitle(ScreenScratch *pScratch, const TitleReturnHooks *pHooks)
{
    if (pScratch != NULL) {
        pScratch->nLineAux145 = 0; /* 0x6B478 */
    }
    if (pHooks != NULL && pHooks->pfnCloseUi != NULL) {
        pHooks->pfnCloseUi(pHooks->pUser); /* 0x6B480 */
    }
    if (pHooks != NULL && pHooks->pfnFreeGeoscapeUi != NULL) {
        pHooks->pfnFreeGeoscapeUi(1, pHooks->pUser); /* 0x6B488 */
    }
    if (pHooks != NULL && pHooks->pfnStartupUi != NULL) {
        return pHooks->pfnStartupUi(pHooks->pUser); /* 0x6B490 tail call */
    }
    return 0;
}

/* Five more copies of the same two instructions. */
void FundingScreen_CloseThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void FundingScreen_ExitThunk(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void Geoscape_CloseUI_ThunkA(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void Geoscape_CloseUI_ThunkB(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

void Geoscape_CloseUI_ThunkC(const GeoscapeCloseEnv *pEnv)
{
    Geoscape_CloseUI(pEnv);
}

i32 Geoscape_OnMapClick_Thunk(const MapClickHooks *pHooks)
{
    /* 0x857EC tail-calls Geoscape_OnMapClick (0x8b4a8) and drops the four callback
     * arguments the widget framework passes. */
    if (pHooks != NULL && pHooks->pfnMapClick != NULL) {
        return pHooks->pfnMapClick(pHooks->pUser);
    }
    return 0;
}

void ResearchScreen_PanelPrev(UiPanel *pFirst, UiPanel *pSecond, UiPanel *pThird)
{
    if (pFirst != NULL) {
        UiPanel_ScrollPrev(pFirst); /* 0x72E3C, unk_160C68 */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollPrev(pSecond); /* 0x72E48, unk_15EEFC */
    }
    if (pThird != NULL) {
        UiPanel_ScrollPrev(pThird); /* 0x72E54, unk_15EF00 */
    }
}

void ResearchScreen_PanelNext(UiPanel *pFirst, UiPanel *pSecond, UiPanel *pThird)
{
    if (pFirst != NULL) {
        UiPanel_ScrollNext(pFirst); /* 0x72E74 */
    }
    if (pSecond != NULL) {
        UiPanel_ScrollNext(pSecond); /* 0x72E80 */
    }
    if (pThird != NULL) {
        UiPanel_ScrollNext(pThird); /* 0x72E8C */
    }
}

/* Research_TopicNeedsItem @ 0x72c8c. The gate is two ARM bands, not the boolean the
 * pseudocode reconstructed: 0x72C9C branches on <=54 into the 117/143 test, so a topic at or
 * below 54 answers 0; 55..65 fall through to the body; 66..117 reach the same test and answer
 * 0; 118..142 reach the body; >=143 answers 0. 135 and 140 are excluded first, at 0x72C8C and
 * 0x72C94. The body reads the item id from the in-memory record at +14 and answers whether the
 * object's per-base count at the selected base is above zero (LDRH/CMP/BHI at 0x72D00). */
i32 Research_TopicNeedsItem(const ResearchTopicLive *aTopics,
                            ObjectRecFull *const *apObjects, i32 nTopic, i8 nBaseIdx)
{
    if (nTopic == 135 || nTopic == 140) { /* 0x72C8C, 0x72C94 */
        return 0;
    }
    if (!(nTopic > 54 && nTopic < 66) && !(nTopic > 117 && nTopic < 143)) {
        return 0; /* 0x72C9C..0x72CB8 */
    }
    const ObjectRecFull *pItem = apObjects[aTopics[nTopic].abNeedItems[0]]; /* 0x72CD4, 0x72CEC */
    return pItem->auPerBaseVal[nBaseIdx] != 0 ? 1 : 0; /* 0x72CF4..0x72D04 */
}

/* ResearchScreen_RefreshPanels @ 0x73008. Both passes walk topics 1..count-1 (the walk starts
 * at 0x15EF2A, one record past the table base), and only bActive topics take a line. The
 * quantity entry index is the topic number itself: the ARM composes 0x15EF08 + 4900 + 288*base
 * + 2*topic (MUL #0x90, ADD topic, LSL #1 at 0x730C0..0x730E4) — the "+4" Hex-Rays showed is
 * the 0xBE4 apLines offset leaking through register R4, not a column header. */
i32 ResearchScreen_RefreshPanels(const ResearchTopicLive *aTopics, i32 nTopicCount,
                                 const u16 *apResearchQty, UiPanel *pPanelCount,
                                 UiPanel *pPanelQty, UiPanel *pPanelProgress)
{
    i32 nActive = 0; /* 0x73014 */
    if (nTopicCount > 1) { /* 0x73028 */
        for (i32 nTopic = 1; nTopic < nTopicCount; ++nTopic) {
            if (aTopics[nTopic].bActive == 1) { /* 0x73034, 0x73044 */
                ++nActive;
            }
        }
    }

    if (pPanelCount != NULL)    { pPanelCount->nLineCount    = nActive; } /* 0x73070 */
    if (pPanelQty != NULL)      { pPanelQty->nLineCount      = nActive; } /* 0x7307C */
    if (pPanelProgress != NULL) { pPanelProgress->nLineCount = nActive; } /* 0x73084 */

    if (nTopicCount > 1) { /* 0x7309C */
        i32 nLine = 0; /* R5 counts line bytes, /4 here */
        for (i32 nTopic = 1; nTopic < nTopicCount; ++nTopic) { /* 0x73228..0x73234 */
            if (aTopics[nTopic].bActive != 1) { /* 0x730B8 */
                continue;
            }
            char szNum[16];
            const u16 wQty = apResearchQty[nTopic]; /* 0x730E8 */
            if (wQty != 0) { /* 0x730EC */
                (void)snprintf(szNum, sizeof(szNum), "%u", (unsigned)wQty); /* j_itoa, 0x730F8 */
            } else {
                szNum[0] = '\0'; /* the empty string at 0x10DA38, 0x7313C */
            }
            pufo_strcpy((char *)pPanelQty->apLines[nLine], szNum); /* the LDRSB/STRB copy at 0x73108 */

            if (aTopics[nTopic].wProgress != 0 && aTopics[nTopic].wTotal != 0) { /* 0x73160 */
                const i32 nPct = (100 * (i32)aTopics[nTopic].wProgress) / (i32)aTopics[nTopic].wTotal;
                (void)snprintf(szNum, sizeof(szNum), "%d", nPct); /* rt_sdiv 0x73184, itoa 0x73198 */
                pufo_strcpy((char *)pPanelProgress->apLines[nLine], szNum);
                (void)pufo_strcat((char *)pPanelProgress->apLines[nLine], "%",
                                  PUFO_PANEL_LINE_BYTES); /* 0x731D0 */
            } else {
                *(char *)pPanelProgress->apLines[nLine] = '\0'; /* 0x731EC: the empty string */
            }
            ++nLine; /* 0x7321C */
        }
    }
    return nTopicCount; /* 0x73218, 0x73238 */
}

/* Which auResearchCol entry the two quantity buttons act on: the topic number the panel
 * carries for the cached row, plus that base's 144-entry column offset. -1 when the original
 * would have dereferenced something the port does not have. */
static i32 ResearchQtyIndex(const ResearchQtyEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pTopicPanel == NULL || pEnv->apResearchCols == NULL) {
        return -1;
    }
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT) {
        return -1;
    }
    const i32 nRow = (pEnv->pnTopicRow != NULL) ? *pEnv->pnTopicRow : 0;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -1;
    }
    const i32 nTopic = pEnv->pTopicPanel->anLineAux[nRow]; /* 0x72F30 / 0x72F88 */
    if (nTopic < 0 || nTopic >= PUFO_RESEARCH_TOPIC_COUNT) {
        return -1;
    }
    return nTopic + PUFO_RESEARCH_TOPIC_COUNT * nBase; /* MUL #0x90, ADD topic */
}

/* ResearchScreen_IncSelectedQty @ 0x72ea0. The scientist gate first, then the lab-slot gate,
 * then the increment. Base_HasResearchSlotFree returns a plain 0/1 (MOV R0,#0 / MOVLE R0,#1 at
 * 0x66334), so the original's ANDS R3,R0,#0xFF test and this one agree. */
i32 ResearchScreen_IncSelectedQty(const ResearchQtyEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;
    /* nIdx already carries the 144*base term, so the count functions get the base's own column
     * while the store goes straight into the flat array. */
    const u16 *pCol = pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;

    const i32 nSci = Base_GetScientistCount(pEnv->pScientistRec, pCol, nBase, nEntries);
    if (nSci <= 0) {
        return nSci; /* 0x72EC8: R0 still holds the count the compare tested */
    }

    const i32 nActive = Base_CountActiveResearch(pCol, nEntries); /* 0x72ED4 */
    if (!Base_HasResearchSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x72EEC */
        return 0; /* 0x72F5C: R0 holds that call's 0 */
    }

    u16 *pEntry = &pEnv->apResearchCols[nIdx];
    *pEntry = (u16)(*pEntry + 1); /* 0x72F58, the LSL #16 / LSR #16 truncation */
    return PUFO_RESEARCH_COL_OFFSET; /* 0x72F3C left 4900 in R0 */
}

/* ResearchScreen_DecSelectedQty @ 0x72f70. No gate at all beyond the entry being non-zero, and
 * 4900 in R0 whether it stored anything or not. */
i32 ResearchScreen_DecSelectedQty(const ResearchQtyEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    u16 *pEntry = &pEnv->apResearchCols[nIdx];
    if (*pEntry != 0) { /* 0x72FC8 */
        *pEntry = (u16)(*pEntry - 1); /* 0x72FE4 */
    }
    return PUFO_RESEARCH_COL_OFFSET;
}

/* ResearchScreen_UpdateProgress @ 0x72d14. The total scientist count fixes the iteration count;
 * the two gates decide how many of those iterations actually store, and because both gates get
 * worse by one per store the button commits everything the base can spare. */
i32 ResearchScreen_UpdateProgress(const ResearchQtyEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;
    const u16 *pCol = pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;
    u16 *pEntry = &pEnv->apResearchCols[nIdx];

    i32 nTotal = Base_GetItemQty(pEnv->pScientistRec, nBase); /* Base_GetItemQtyA, 0x72D30 */
    if (nTotal <= 0) {
        return nTotal; /* 0x72D38: the loop never runs */
    }

    i32 nIter = 0; /* R9 */
    for (;;) {
        const i32 nSci = Base_GetScientistCount(pEnv->pScientistRec, pCol, nBase, nEntries);
        if (nSci > 0) { /* 0x72D94 */
            const i32 nActive = Base_CountActiveResearch(pCol, nEntries); /* 0x72DA8 */
            if (Base_HasResearchSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x72DC4 */
                *pEntry = (u16)(*pEntry + 1); /* 0x72DF4 */
            }
        }
        ++nIter; /* 0x72E1C */
        nTotal = Base_GetItemQty(pEnv->pScientistRec, nBase); /* 0x72E20 */
        if (nIter >= nTotal) {
            break; /* 0x72E28: CMP R9, R0 / BLT */
        }
    }
    return nTotal; /* 0x72E2C */
}

/* ResearchScreen_CancelSelectedAndClose @ 0x73380. The store has no gate at all, and the close is
 * a tail call, so it runs whatever the store did. */
void ResearchScreen_CancelSelectedAndClose(const ResearchCancelEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    const i32 nIdx = ResearchQtyIndex(&pEnv->qty);
    if (nIdx >= 0) {
        pEnv->qty.apResearchCols[nIdx] = 0; /* 0x733D8: MOV R2,#0 / STRH R2 */
    }
    Geoscape_CloseUI(pEnv->pClose); /* 0x733DC, the tail call */
}

/* ResearchScreen_AccumulateAllocated @ 0x74914. Eight base slots regardless of how many bases
 * exist, and the topic index is the column index, so every base feeds the same 144 records. */
i32 ResearchScreen_AccumulateAllocated(const ResearchAccumulateEnv *pEnv)
{
    if (pEnv == NULL || pEnv->aTopics == NULL || pEnv->apResearchCols == NULL) {
        return 0;
    }

    i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;
    if (nEntries > PUFO_RESEARCH_TOPIC_COUNT) {
        nEntries = PUFO_RESEARCH_TOPIC_COUNT; /* deviation: the original would run one base's
                                               * column straight into the next one */
    }

    for (i32 nBase = 0; nBase < PUFO_FAC_BASE_COUNT; ++nBase) { /* 0x7491C: MOV R6,#8 */
        const u16 *pCol = pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;
        for (i32 nTopic = 0; nTopic < nEntries; ++nTopic) {
            const u16 w = pCol[nTopic]; /* 0x74950 */
            if (w != 0) { /* 0x74960 */
                pEnv->aTopics[nTopic].wProgress = (u16)(pEnv->aTopics[nTopic].wProgress + w); /* 0x7496C */
            }
        }
    }

    if (pEnv->pfnRebuild != NULL) {
        return pEnv->pfnRebuild(pEnv->pRebuildUser); /* 0x749A4: tail call */
    }
    return 0;
}

/* Which auManufCol entry the manufacturing buttons act on: the product number the panel carries
 * for the cached row, plus that base's 200-entry column offset. */
static i32 ManufQtyIndex(const ManufQtyEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pProductPanel == NULL || pEnv->apManufCols == NULL) {
        return -1;
    }
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT) {
        return -1;
    }
    const i32 nRow = (pEnv->pnProductRow != NULL) ? *pEnv->pnProductRow : 0;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -1;
    }
    const i32 nProduct = pEnv->pProductPanel->anLineAux[nRow]; /* 0x6DEF8, 0x6E0F0, 0x6DF30 */
    if (nProduct < 0 || nProduct >= PUFO_PRODUCT_ENTRIES_PER_BASE) {
        return -1;
    }
    return nProduct + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase; /* MUL #0xC8, ADD product */
}

/* ORIGINAL BUG (dead code): the gate every one of these buttons opens with. A non-zero
 * bManufQtyGate @ 0x15EE50 stops it dead and the byte itself becomes the return value -- 0x6DDE0,
 * 0x6DEC4, 0x6E0A0, 0x6E140, 0x6E334 and 0x6DFA8 all read it and bail -- but no code in the
 * original ever writes a non-zero value to that byte: the only store to it anywhere is
 * ManufScreen_SelectListed @ 0x6E970 writing 0. The gate never fires in the shipped build. The
 * port keeps it because the original has it. */
static i32 ManufQtyGateValue(const ManufQtyEnv *pEnv)
{
    return (pEnv->pbQtyGate != NULL) ? (i32)*pEnv->pbQtyGate : 0;
}

/* ManufScreen_IncSelectedQty @ 0x6dddc. The engineer gate first, then the workshop-slot gate,
 * then the increment -- the research pair's shape with the crew and facility kinds swapped. */
i32 ManufScreen_IncSelectedQty(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6DDE4 */
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puManufEntries != NULL) ? (i32)*pEnv->puManufEntries : 0;
    const u16 *pCol = pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase;
    u16 *pEntry = &pEnv->apManufCols[nIdx];

    const i32 nEng = Base_GetEngineerCount(pEnv->pEngineerRec, pCol, nBase, nEntries);
    if (nEng <= 0) {
        return nEng; /* 0x6DE0C: R0 still holds the count the compare tested */
    }
    const i32 nActive = Base_CountActiveManuf(pCol, nEntries); /* 0x6DE20 */
    if (!Base_HasWorkshopSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x6DE38 */
        return 0;
    }
    *pEntry = (u16)(*pEntry + 1); /* 0x6DEA4 */
    return PUFO_MANUF_COL_OFFSET; /* 0x6DE88 */
}

/* ManufScreen_DecSelectedQty @ 0x6e09c. No gate beyond the entry being non-zero, and 17156 in R0
 * whether it stored anything or not. */
i32 ManufScreen_DecSelectedQty(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6E0A4 */
    }

    u16 *pEntry = &pEnv->apManufCols[nIdx];
    if (*pEntry != 0) { /* 0x6E108 */
        *pEntry = (u16)(*pEntry - 1); /* 0x6E120 */
    }
    return PUFO_MANUF_COL_OFFSET; /* 0x6E0FC */
}

/* ManufScreen_ReduceSelectedQtyToOne @ 0x6dec0, renamed in the IDB from
 * ManufScreen_GetSelectedProductId because it never returns a product id. The ARM steps the value
 * down one at a time while it is above 1 (0x6DF30..0x6DF54, re-reading anLineAux each pass), which
 * is exactly a clamp to 1; the port writes the clamp directly and gets the same result. */
i32 ManufScreen_ReduceSelectedQtyToOne(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6DEC8 */
    }

    u16 *pEntry = &pEnv->apManufCols[nIdx];
    if (*pEntry > 1) { /* 0x6DF64: CMP R2, #1 / BHI */
        *pEntry = 1;
    }
    return PUFO_MANUF_COL_OFFSET; /* 0x6DF10 */
}

/* ManufScreen_DecEngineers @ 0x6e330. The engineers assigned to this product (aManufProductB, a
 * dword per product per base) may not fall to or below the quantity being made plus one. */
i32 ManufScreen_DecEngineers(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }
    const i32 nGate = ManufQtyGateValue(pEnv);
    if (nGate != 0) {
        return nGate; /* 0x6E338 */
    }

    if (pEnv->apProductB != NULL) {
        const i32 nAlloc = pEnv->apProductB[nIdx]; /* 0x6E394 */
        const i32 nQty = (i32)pEnv->apManufCols[nIdx] + 1; /* 0x6E3A4..0x6E3AC */
        if (nAlloc > nQty) { /* 0x6E3B0: CMP R4, R1 / SUBGT */
            pEnv->apProductB[nIdx] = nAlloc - 1; /* 0x6E3B8 */
        }
    }
    return PUFO_MANUF_ANCHOR; /* 0x6E3BC: R0 still holds the 0x159E98 product-block anchor */
}

/* ManufScreen_UpdateProgress @ 0x6df6c. The manufacturing twin of ResearchScreen_UpdateProgress:
 * the engineer total fixes the iteration count, the two gates decide what stores. The gate byte is
 * re-read on every iteration (0x6DFA8), and when it is set the iteration skips the gates and the
 * store but still counts. */
i32 ManufScreen_UpdateProgress(const ManufQtyEnv *pEnv)
{
    const i32 nIdx = ManufQtyIndex(pEnv);
    if (nIdx < 0) {
        return 0;
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puManufEntries != NULL) ? (i32)*pEnv->puManufEntries : 0;
    const u16 *pCol = pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase;
    u16 *pEntry = &pEnv->apManufCols[nIdx];

    i32 nTotal = Base_GetItemQty(pEnv->pEngineerRec, nBase); /* Base_GetItemQtyB, 0x6DF80 */
    if (nTotal <= 0) {
        return nTotal; /* 0x6DF88 */
    }

    i32 nIter = 0; /* R9 */
    for (;;) {
        if (ManufQtyGateValue(pEnv) == 0) { /* 0x6DFA8..0x6DFB4 */
            const i32 nEng = Base_GetEngineerCount(pEnv->pEngineerRec, pCol, nBase, nEntries);
            if (nEng > 0) { /* 0x6DFD8 */
                const i32 nActive = Base_CountActiveManuf(pCol, nEntries); /* 0x6DFEC */
                if (Base_HasWorkshopSlotFree(pEnv->abFacilityGrid, nBase, nActive + 1)) { /* 0x6E010 */
                    *pEntry = (u16)(*pEntry + 1); /* 0x6E040 */
                }
            }
        }
        ++nIter; /* 0x6E078 */
        nTotal = Base_GetItemQty(pEnv->pEngineerRec, nBase); /* 0x6E084 */
        if (nIter >= nTotal) {
            break; /* 0x6E090: CMP R9, R0 / BLT */
        }
    }
    return nTotal; /* 0x6E098 */
}

/* ManufScreen_SelectListed @ 0x6e914. The product list's row click. */
i32 ManufScreen_SelectListed(const ManufSelectEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pProductPanel == NULL) {
        return 0;
    }
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(7, pEnv->pSampleUser); /* 0x6E920 */
    }

    const i32 nRow = pEnv->pProductPanel->nUnk3032; /* 0x6E938: LDR [R0,#0xBD8] */
    if (pEnv->pnProductRow != NULL) {
        *pEnv->pnProductRow = nRow; /* 0x6E944 */
    }
    if (pEnv->pbProductSelected != NULL) {
        *pEnv->pbProductSelected = 1; /* 0x6E950 */
    }
    /* 0x6E958: MOV R2,#0 / STRB R2,[R0,#0xBD8]. A byte store into a dword field, so only the low
     * byte of the cursor is cleared and the upper 24 bits survive. */
    pEnv->pProductPanel->nUnk3032 =
        (i32)(((u32)pEnv->pProductPanel->nUnk3032) & 0xFFFFFF00u);
    if (pEnv->pbAnimStarted != NULL) {
        *pEnv->pbAnimStarted = 0; /* 0x6E964 */
    }
    if (pEnv->pbQtyGate != NULL) {
        *pEnv->pbQtyGate = 0; /* 0x6E970: the only write bManufQtyGate ever gets */
    }
    return nRow;
}

/* The manufacturing screen's selected product: the panel's cached row cursor indexes its aux
 * column, and that cell holds the product number -- the same row-to-entity mapping the hangar crew
 * list uses. -1 means the row is outside the panel, which the original never checks. */
static i32 ManufSelectedProduct(const UiPanel *pPanel, const i32 *pnRow)
{
    if (pPanel == NULL || pnRow == NULL) {
        return -1;
    }
    const i32 nRow = *pnRow;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -1;
    }
    return pPanel->anLineAux[nRow];
}

#define PUFO_PRODUCT_COLUMN_TOTAL (PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT)

void ManufScreen_ClearProductSlot(const ManufClearEnv *pEnv, i32 nBaseIndex, i32 nProduct)
{
    if (pEnv == NULL) {
        return;
    }

    nProduct &= 0xFF;                  /* 0x6F8D0 AND R3,R2,#0xFF */
    nBaseIndex = (i32)(i8)nBaseIndex;  /* 0x6F8D4 LSL#24 then ASR#24 sign-extends the byte */

    const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBaseIndex + nProduct;
    if (nIdx >= 0 && nIdx < PUFO_PRODUCT_COLUMN_TOTAL) {
        if (pEnv->apProductB != NULL) {
            pEnv->apProductB[nIdx] = 0; /* 0x6F8FC, the u32 column at 0x15B79C */
        }
        if (pEnv->apManufCols != NULL) {
            pEnv->apManufCols[nIdx] = 0; /* 0x6F910, auManufCol @ 0x15E19C */
        }
        if (pEnv->apProductA != NULL) {
            pEnv->apProductA[nIdx] = 0; /* 0x6F918, the u32 column at 0x159E9C */
        }
        if (pEnv->apProductC != NULL) {
            pEnv->apProductC[nIdx] = 0; /* 0x6F92C, the u16 column at 0x15D09C */
        }
    }

    if (nProduct < PUFO_MANUF_HANGAR_PRODUCT_FIRST || nProduct > PUFO_MANUF_HANGAR_PRODUCT_LAST) {
        return; /* 0x6F930 BLS and 0x6F938 BCS both bail on the range test */
    }

    const i32 nSelBase = (pEnv->pnSelectedBaseIdx != NULL)
                             ? (i32)(i8)*pEnv->pnSelectedBaseIdx
                             : 0;
    if (pEnv->apBases == NULL || nSelBase < 0 || nSelBase >= pEnv->nBaseCount) {
        return; /* deviation: the original dereferences apBases[byte] with no check at all */
    }

    /* 0x6F968 LDRB [R3,#4]: the selected base's type byte. GetByte0x654 reads the grid byte for
     * that type and SubByte0x654 then subtracts that very amount from the same cell, so the pair
     * zeroes it -- and SubByte0x654 leaves a zero alone, which is why this is not just a store. */
    const u8 bType = pEnv->apBases[nSelBase]->bUnk4;
    const i32 nAmount = BaseGrid_GetTypeByte(pEnv->apBases, pEnv->nBaseCount,
                                             pEnv->abGridBytes, bType);
    (void)BaseGrid_SubTypeByte(pEnv->apBases, pEnv->nBaseCount, pEnv->abGridBytes,
                               bType, (i8)nAmount);
}

/* ManufScreen_RefreshBaseTabs @ 0x70ebc. The header block has the two product classes, the
 * per-product capacity test, the message payload and the early return. */
void ManufScreen_RefreshBaseTabs(const ManufRefreshEnv *pEnv)
{
    if (pEnv == NULL || pEnv->apManufCols == NULL || pEnv->apProductA == NULL ||
        pEnv->pbBaseCount == NULL) {
        return;
    }

    const i32 nBaseCount = (i32)(i8)*pEnv->pbBaseCount; /* 0x70EE0: LDRSB of abFacilityGrid + 0x618 */
    if (nBaseCount <= 0) {
        return; /* 0x71048: ManufScreen_CommitProduction (0x6f34c, not ported) */
    }

    for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {
        for (i32 nProduct = 0; nProduct < pEnv->nObjectCount; ++nProduct) {
            const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBase + nProduct;
            const u16 nQty = pEnv->apManufCols[nIdx]; /* 0x70F50 */

            if (nQty == 0) {
                continue;
            }

            if (nProduct >= PUFO_MANUF_HANGAR_PRODUCT_FIRST &&
                nProduct <= PUFO_MANUF_HANGAR_PRODUCT_LAST) {
                pEnv->apProductA[nIdx] += (u32)nQty; /* 0x70F68: no capacity test at all */
                continue;
            }

            const i32 nUsed = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase);
            const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount, -1,
                                                             pEnv->apBases, nBase, pEnv->apObjects,
                                                             pEnv->nObjectCount, true);
            const i32 nCap = Base_CalcStoresCapacity(pEnv->abFacilityGrid, nBase);
            const i32 nCargo = (pEnv->apObjects[nProduct] != NULL)
                                   ? (i32)pEnv->apObjects[nProduct]->bCargoSize
                                   : 0;

            if (nAcross + nCargo + nUsed < nCap) {  /* 0x70FC8: BGE takes the over branch */
                pEnv->apProductA[nIdx] += (u32)nQty; /* 0x70FE8 */
                continue;
            }

            *pEnv->pdwShortfall = 1; /* 0x71070 */
            /* Six arguments: a3 in R2 and a4 in R3 both get the base (0x7107C and 0x71084), the
             * product goes to [SP+0] (STRB R5 at 0x7108C) and 0 to [SP+4] (STRB R3 at 0x71078).
             * So the payload is (32, base, base, product, 0) -- the same shape the manufacturing
             * messages 33, 34 and 35 use. */
            UiLineList_AppendWithSound(pEnv->pLines, 32, (u32)nBase, (u32)nBase, (u32)nProduct, 0u,
                                       pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x71090 */
            ManufScreen_ClearProductSlot(pEnv->pClearEnv, nBase, nProduct);    /* 0x710A0 */
            return; /* 0x710A4: B past the commit call -- one over-budget product ends the refresh */
        }
    }

    /* 0x71044 / 0x71048: the original tail-calls ManufScreen_CommitProduction here (and from the
     * zero-base-count guard), while the over-budget path at 0x710A4 branches PAST it. The port keeps
     * the two envs separate because the hourly tick is not wired into the shell yet; the commit pass
     * itself is ported below as ManufScreen_CommitProduction. */
}

/* The four component slots, in the order the original tests them at 0x6F57C..0x6F5B4. Slot 108
 * (Alien surgery) is never a component. */
static const i32 g_anManufComponentSlots[4] = {
    OBJ_ELERIUM_115, OBJ_ALIEN_ALLOYS, OBJ_UFO_POWER_SOURCE, OBJ_UFO_NAVIGATION
};

/* ManufScreen_CommitProduction @ 0x6f34c. The header block has the four tables, the gate order, the
 * blocked early return and the message payloads. */
void ManufScreen_CommitProduction(const ManufCommitEnv *pEnv)
{
    if (pEnv == NULL || pEnv->apManufCols == NULL || pEnv->apProductA == NULL ||
        pEnv->apProductB == NULL || pEnv->apProductC == NULL || pEnv->pbBaseCount == NULL ||
        pEnv->apObjects == NULL || pEnv->pnFunds == NULL) {
        return;
    }

    const i32 nBaseCount = (i32)(i8)*pEnv->pbBaseCount; /* 0x6F368: LDRSB of abFacilityGrid + 0x618 */
    if (nBaseCount <= 0) {
        return;                                         /* 0x6F370 */
    }

    for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {
        for (i32 nProduct = 0; nProduct < pEnv->nObjectCount; ++nProduct) {
            const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBase + nProduct;
            const u16 nQty = pEnv->apManufCols[nIdx];  /* 0x6F3D0 */
            if (nQty == 0) {
                continue;                               /* 0x6F3DC */
            }

            const ObjectRecFull *pRec = pEnv->apObjects[nProduct];
            const i32 nWorkPerUnit = (pRec != NULL) ? (i32)pRec->uManufWorkPerUnit : 0;

            /* 0x6F3EC..0x6F404: _rt_sdiv takes the divisor in R0 and the dividend in R1, so this is
             * the number of units the accumulated work has paid for. Deviation: a zero work cost
             * makes the original call the external COREDLL helper with a zero divisor and the IDB
             * cannot show what that answers; the port answers zero units. */
            const i32 nUnits = (nWorkPerUnit != 0)
                                   ? (i32)pEnv->apProductA[nIdx] / nWorkPerUnit
                                   : 0;

            const u32 dwPrice = (pEnv->pdwPrices != NULL) ? pEnv->pdwPrices[nProduct] : 0u;
            const double dCost = (double)dwPrice * 1.4; /* 0x6F440/0x6F448: 0x3FE66666_66666666 */
            bool bBlocked = false;

            /* 1. funds gate, 0x6F408..0x6F494: _ltd(funds - price * 1.4, 0.0) != 0. */
            if ((double)*pEnv->pnFunds - dCost < 0.0) {
                *pEnv->pdwShortfall = 1;               /* 0x6F4A8 */
                UiLineList_AppendWithSound(pEnv->pLines, 35, (u32)nBase, (u32)nBase,
                                           (u32)nProduct, 0u,
                                           pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x6F4C0 */
                bBlocked = true;                       /* 0x6F4C4 */
            }

            /* 2. stores gate, 0x6F4D0..0x6F564. Products 119..121 skip it entirely. */
            if (!bBlocked && (nProduct < PUFO_MANUF_HANGAR_PRODUCT_FIRST ||
                              nProduct > PUFO_MANUF_HANGAR_PRODUCT_LAST)) {
                const i32 nUsed = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase);
                const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount,
                                                                 pEnv->nSiteCount, /* NOT -1 here */
                                                                 pEnv->apBases, nBase,
                                                                 pEnv->apObjects, pEnv->nObjectCount,
                                                                 true);
                const i32 nCap = Base_CalcStoresCapacity(pEnv->abFacilityGrid, nBase);
                const i32 nCargo = (pRec != NULL) ? (i32)pRec->bCargoSize : 0;

                if (nUsed + nAcross + nCargo > nCap) { /* 0x6F530: BLE keeps going when equal */
                    *pEnv->pdwShortfall = 1;           /* 0x6F548 */
                    UiLineList_AppendWithSound(pEnv->pLines, 32, (u32)nBase, (u32)nBase,
                                               (u32)nProduct, 0u,
                                               pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x6F560 */
                    bBlocked = true;                   /* 0x6F564 */
                }
            }

            /* 3. component gate, 0x6F574..0x6F5C8. All four are always evaluated -- MOVCC R8,#1 on
             * each CMP, so there is no early exit in the original either. */
            if (!bBlocked && pRec != NULL) {
                for (i32 j = 0; j < 4; ++j) {
                    const ObjectRecFull *pComp = pEnv->apObjects[g_anManufComponentSlots[j]];
                    const i32 nStock = (pComp != NULL) ? (i32)pComp->auPerBaseVal[nBase] : 0;
                    if (nStock < (i32)pRec->abManufComponents[j]) {
                        bBlocked = true;
                    }
                }
            }

            /* 4. blocked tail, 0x6F870..0x6F8C4: clear the slot, message 33 only while the shortfall
             * dword is still zero, and RETURN -- one blocked product ends the whole pass. */
            if (bBlocked) {
                ManufScreen_ClearProductSlot(pEnv->pClearEnv, nBase, nProduct); /* 0x6F888 */
                if (*pEnv->pdwShortfall == 0) {        /* 0x6F890: the dword is read back */
                    *pEnv->pdwShortfall = 1;           /* 0x6F8A8 */
                    UiLineList_AppendWithSound(pEnv->pLines, 33, (u32)nBase, (u32)nBase,
                                               (u32)nProduct, 0u,
                                               pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x6F8C0 */
                }
                return;                                /* 0x6F8C4 */
            }

            /* 5. 0x6F5E8: produce only while the paid-for units exceed the committed ones. */
            const u16 nDone = pEnv->apProductC[nIdx];
            if (nUnits > (i32)nDone) {
                if (nProduct >= PUFO_MANUF_HANGAR_PRODUCT_FIRST &&
                    nProduct <= PUFO_MANUF_HANGAR_PRODUCT_LAST) {
                    /* Craft branch, 0x6F604..0x6F658. The base whose type byte is decremented is the
                     * SELECTED one (abFacilityGrid + 0x619), not the one being processed; the craft
                     * itself homes at the base being processed. */
                    const ManufClearEnv *pClear = pEnv->pClearEnv;
                    const i32 nSelBase = (pClear != NULL && pClear->pnSelectedBaseIdx != NULL)
                                             ? (i32)(i8)*pClear->pnSelectedBaseIdx
                                             : 0;
                    if (pClear != NULL && pClear->apBases != NULL && nSelBase >= 0 &&
                        nSelBase < pClear->nBaseCount && pClear->apBases[nSelBase] != NULL) {
                        (void)BaseGrid_SubTypeByte(pClear->apBases, pClear->nBaseCount,
                                                   pClear->abGridBytes,
                                                   pClear->apBases[nSelBase]->bUnk4, 1); /* 0x6F62C */
                    }
                    (void)Ship_SpawnAtSlot(pEnv->pShipEnv,
                                           (u8)Ship_ProductSlotToType(nProduct), nBase); /* 0x6F650 */
                } else {
                    BaseRec_AddItemQty(pEnv->apBases[nBase], pEnv->apObjects[nProduct], 1); /* 0x6F6A4 */
                }

                pEnv->apProductC[nIdx] = (u16)(nDone + 1); /* 0x6F6B8: LSL#16/LSR#16 truncation */

                for (i32 j = 0; j < 4; ++j) {          /* 0x6F6C8..0x6F708 */
                    BaseRec_SubItemQty(pEnv->apBases[nBase],
                                       pEnv->apObjects[g_anManufComponentSlots[j]],
                                       (i32)pRec->abManufComponents[j]);
                }

                /* 0x6F70C..0x6F788: nFunds = dtoi((double)nFunds - (double)price * 1.4). */
                *pEnv->pnFunds = (i32)((double)*pEnv->pnFunds - dCost);
            }

            /* 6. completion, 0x6F798..0x6F7F0: reached whether or not this tick produced, and it
             * does not return -- the loop continues. */
            if (nUnits == pEnv->apProductB[nIdx]) {
                ManufScreen_ClearProductSlot(pEnv->pClearEnv, nBase, nProduct); /* 0x6F7C4 */
                *pEnv->pdwShortfall = 1;               /* 0x6F7D8 */
                UiLineList_AppendWithSound(pEnv->pLines, 34, (u32)nBase, (u32)nBase,
                                           (u32)nProduct, 0u,
                                           pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x6F7F0 */
            }
        }
    }
}

void ManufScreen_ConfirmAndClose(const ManufClearEnv *pEnv, const GeoscapeCloseEnv *pClose)
{
    if (pEnv == NULL) {
        return;
    }

    const i32 nProduct = ManufSelectedProduct(pEnv->pProductPanel, pEnv->pnProductRow);
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)(i8)*pEnv->pnSelectedBaseIdx : 0;
    ManufScreen_ClearProductSlot(pEnv, nBase, nProduct); /* 0x6E9C4 */

    Geoscape_CloseUI(pClose); /* 0x6E9C8, the tail call */
}

/* g_aManufCtlDesc @ 0xAB9F8: the manufacturing screen's six control descriptors -- the base-tab scroll
 * pair, then the quantity pair, then the engineers pair. Same order as the six handlers
 * ManufScreen_Init installs onto them. */
static const UiControlDesc g_aManufCtlDesc[6] = {
    { 0x00CD, 0x0012, "up",   "sup",   "", 0 }, /* 0xAB9F8 */
    { 0x00CD, 0x0026, "down", "sdown", "", 0 }, /* 0xABA18 */
    { 0x000A, 0x00FB, "up",   "sup",   "", 0 }, /* 0xABA38 */
    { 0x000A, 0x010F, "down", "sdown", "", 0 }, /* 0xABA58 */
    { 0x008C, 0x00FB, "up",   "sup",   "", 0 }, /* 0xABA78 */
    { 0x008C, 0x010F, "down", "sdown", "", 0 }, /* 0xABA98 */
};

/* ManufScreen_Init @ 0x710b0. The header block has the seven steps, the descriptor source and the
 * deviation. */
void ManufScreen_Init(ManufInitEnv *pEnv)
{
    PadImage *pPad;
    i32 i;

    if (pEnv == NULL || pEnv->ppRoot == NULL) {
        return;
    }

    pPad = BuySellPadNew(); /* 0x710BC / 0x710CC: operator new(0x14) then PadImage_ctor */
    pEnv->ppRoot[0] = pPad; /* 0x710E0: the ctor's result, not the raw allocation */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_MANUF_PAD_NAME, PUFO_MANUF_PAD_MODE); /* 0x710E4 */
        pPad->nOriginX = 0; /* 0x710F8 */
        pPad->nOriginY = 0; /* 0x7110C */
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->x = 224; /* 0x71118 */
        pEnv->pCloseCtl->y = 1;   /* 0x71124 */
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseClick; /* 0x71130 */
    }

    for (i = 0; i < 6; ++i) {
        pEnv->ppRoot[i + 1] = pEnv->apCtl[i]; /* 0x71160, the original's operator new(0x20) slots */
    }
    if (pEnv->apCtl[0] != NULL) {
        pEnv->apCtl[0]->pfnClick = pEnv->pfnPanelPrev; /* 0x71184 */
    }
    if (pEnv->apCtl[1] != NULL) {
        pEnv->apCtl[1]->pfnClick = pEnv->pfnPanelNext; /* 0x7119C */
    }
    if (pEnv->apCtl[2] != NULL) {
        pEnv->apCtl[2]->pfnClick = pEnv->pfnIncQty; /* 0x711B0 */
    }
    if (pEnv->apCtl[3] != NULL) {
        pEnv->apCtl[3]->pfnClick = pEnv->pfnDecQty; /* 0x711C4 */
    }
    if (pEnv->apCtl[4] != NULL) {
        pEnv->apCtl[4]->pfnClick = pEnv->pfnIncEngineers; /* 0x711D8 */
    }
    if (pEnv->apCtl[5] != NULL) {
        pEnv->apCtl[5]->pfnClick = pEnv->pfnDecEngineers; /* 0x711EC */
    }

    if (pEnv->pDrawCtl != NULL) {
        pEnv->pDrawCtl->x = 40; /* 0x71200 */
        pEnv->pDrawCtl->y = 294; /* 0x71208 */
        pEnv->pDrawCtl->pfnClick = pEnv->pfnDrawClick; /* 0x7122C */
    }
    if (pEnv->pConfirmCtl != NULL) {
        pEnv->pConfirmCtl->x = 130; /* 0x71214 */
        pEnv->pConfirmCtl->y = 294; /* 0x71220 */
        pEnv->pConfirmCtl->pfnClick = pEnv->pfnConfirmClick; /* 0x71238 */
    }

    for (i = 0; i < 6; ++i) {
        if (pEnv->apCtl[i] != NULL) {
            UiControl_Setup(pEnv->apCtl[i], 0, 0, &g_aManufCtlDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x7124C */
        }
    }

    if (pEnv->pfnBuildPanels != NULL) {
        pEnv->pfnBuildPanels(); /* 0x71268, tail call into ManufScreen_BuildPanels */
    }
}

/* abManufRowFlag @ 0xAA548: the manufacturing row filter, 172 bytes of 0/1 closed by the dword table at
 * 0xAA5F4. ManufScreen_BuildPanels is its only reader in the binary (0x6EA50, 0x6EECC). Dumped from
 * the image; audit_tables.py compares it. The 35 set slots are 2, 3, 5, 6, 9, 11, 12, 13, 18, 20, 21,
 * 23, 24, 28, 29, 30, 33, 34, 35, 37, 39, 49, 50, 51, 55, 58, 106, 107, 109, 119, 120, 121, 122, 123,
 * 124 -- and against the shipped OBJECTS.DAT that is exactly the set with a non-zero
 * uManufWorkPerUnit, so this byte is the "can this slot be manufactured" flag. */
static const u8 g_abManufRowFlag[172] = {
    /*   0 */ 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1,
    /*  12 */ 1, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1,
    /*  24 */ 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1,
    /*  36 */ 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    /*  48 */ 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0,
    /*  60 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /*  72 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /*  84 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /*  96 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    /* 108 */ 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    /* 120 */ 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
    /* 132 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 144 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 156 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 168 */ 0, 0, 0, 0,
};

/* One row of the manufacturing list, in the four panels at the same index. The header block has the
 * four columns and the two _rt_sdiv calls; both divisions are 32-bit here, as the ARM's MUL and
 * rt_sdiv are, so a large accumulated count wraps the same way it does on the original. */
static void ManufFillRow(const ManufBuildEnv *pEnv, UiPanel *pA, UiPanel *pB, UiPanel *pC, UiPanel *pD,
                         i32 nRow, i32 nTopic, i32 nSlot, const ObjectRecFull *pObj)
{
    const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * (i32)pEnv->nSelectedBaseIdx + nSlot;
    const bool bIndexed = (nIdx >= 0 && nIdx < PUFO_PRODUCT_COLUMN_TOTAL);
    const u16 nAllocated = (bIndexed && pEnv->apManufCols != NULL) ? pEnv->apManufCols[nIdx] : 0u;
    const u32 nAccum = (bIndexed && pEnv->apProductA != NULL) ? pEnv->apProductA[nIdx] : 0u;
    const u32 nTarget = (bIndexed && pEnv->apProductB != NULL) ? pEnv->apProductB[nIdx] : 0u;
    const u16 nWork = (pObj != NULL) ? pObj->uManufWorkPerUnit : 0u;
    char szName[18];
    char szLine[PUFO_PANEL_LINE_BYTES];

    BuySellCopyName(szName, (pObj != NULL) ? pObj->szName : ""); /* 0x6EEDC..0x6EF24, the 17-char cut */
    if (pA != NULL) {
        (void)UiPanel_SetLineText(pA, nRow, szName);   /* 0x6EF2C..0x6EF48 */
        UiPanel_SetLineAux(pA, nRow, nSlot);           /* 0x6EF64, the slot id */
    }
    if (pC != NULL) {
        UiPanel_SetLineAux(pC, nRow, nTopic);          /* 0x6EF70, the TOPIC index, not the slot */
    }

    /* Panel B: the allocated work, or the empty string at 0x10DA38 when nothing is allocated. */
    if (nAllocated != 0) {
        (void)snprintf(szLine, sizeof(szLine), "%u", (unsigned)nAllocated); /* j_itoa, 0x6EFB0 */
    } else {
        szLine[0] = '\0'; /* 0x6F000, the empty string at 0x10DA38 */
    }
    if (pB != NULL) {
        (void)UiPanel_SetLineText(pB, nRow, szLine);   /* 0x6EFBC..0x6F014 */
    }

    if (nTarget == 0) {
        /* 0x6F03C: with nothing asked for, both remaining columns get the empty string. */
        if (pC != NULL) { (void)UiPanel_SetLineText(pC, nRow, ""); } /* 0x6F1D8 */
        if (pD != NULL) { (void)UiPanel_SetLineText(pD, nRow, ""); } /* 0x6F200 */
        return;
    }

    /* Panel C: 100 * accumulated / (work-per-unit * asked-for), then "%". The ARM puts the divisor in
     * R0 (0x6F064..0x6F068) and the dividend in R1 (0x6F070), which is _rt_sdiv's argument order. */
    {
        const i32 nNum = (i32)(nAccum * 100u);
        const i32 nDen = (i32)((u32)nWork * nTarget);
        (void)snprintf(szLine, sizeof(szLine), "%d%%", (nDen != 0) ? (nNum / nDen) : 0);
    }
    if (pC != NULL) {
        (void)UiPanel_SetLineText(pC, nRow, szLine);   /* 0x6F098..0x6F0B4, then strcat("%") at 0x6F0CC */
    }

    /* Panel D: what was asked for, then the units the accumulated work has paid for. */
    {
        const i32 nUnits = (nWork != 0u) ? ((i32)nAccum / (i32)nWork) : 0; /* 0x6F174..0x6F180 */
        (void)snprintf(szLine, sizeof(szLine), "%u/%d", (unsigned)nTarget, nUnits);
    }
    if (pD != NULL) {
        (void)UiPanel_SetLineText(pD, nRow, szLine);   /* 0x6F110..0x6F140, then strcat("/") at 0x6F140 */
    }
}

/* Does this topic's fifth-of-five slot id take a row? Both passes ask the same question, so the row
 * total and the lines written can never disagree. The original indexes abManufRowFlag with the raw byte
 * (LDRSB at 0x6EA50) and so reads past its 172 bytes for a slot id of 172 or more; the port refuses
 * those instead. */
static bool ManufRowTakesLine(i32 nSlot)
{
    return (nSlot >= 0 && nSlot < (i32)sizeof(g_abManufRowFlag) && g_abManufRowFlag[nSlot] == 1);
}

/* ManufScreen_BuildPanels @ 0x6e9e8. The header block has the branch map, the four panels' geometry,
 * the row model and the deviations. */
void ManufScreen_BuildPanels(ManufBuildEnv *pEnv)
{
    UiPanel *apFree[4];
    UiPanel *pA, *pB, *pC, *pD;
    PadImage *pPad;
    i32 nTopicCount;
    i32 nRows = 0;
    i32 nRow = 0;
    i32 i;

    if (pEnv == NULL || pEnv->pbPanelsBuilt == NULL) {
        return;
    }

    if (*pEnv->pbPanelsBuilt != 0) {
        /* The teardown, 0x6F264..0x6F340. No funds rollback: this screen spends none here. */
        *pEnv->pbPanelsBuilt = 0; /* 0x6F26C */
        pPad = (pEnv->ppRoot != NULL) ? (PadImage *)pEnv->ppRoot[0] : NULL;
        if (pPad != NULL) {
            PadImage_FreeData(pPad); /* 0x6F280 */
            free(pPad);
            pEnv->ppRoot[0] = NULL;
        }
        apFree[0] = (pEnv->ppPanelA != NULL) ? *pEnv->ppPanelA : NULL;
        apFree[1] = (pEnv->ppPanelC != NULL) ? *pEnv->ppPanelC : NULL; /* C before B, as 0x6F2AC does */
        apFree[2] = (pEnv->ppPanelB != NULL) ? *pEnv->ppPanelB : NULL;
        apFree[3] = (pEnv->ppPanelD != NULL) ? *pEnv->ppPanelD : NULL;
        for (i = 0; i < 4; ++i) {
            if (apFree[i] != NULL) {
                UiPanel_FreeLines(apFree[i]); /* 0x6F2A0, 0x6F2C0, 0x6F2E0, 0x6F300 */
                free(apFree[i]);
            }
        }
        /* The original stores nothing back and leaves ten dangling pointers; the port clears the four
         * panel slots it owns. */
        if (pEnv->ppPanelA != NULL) { *pEnv->ppPanelA = NULL; }
        if (pEnv->ppPanelB != NULL) { *pEnv->ppPanelB = NULL; }
        if (pEnv->ppPanelC != NULL) { *pEnv->ppPanelC = NULL; }
        if (pEnv->ppPanelD != NULL) { *pEnv->ppPanelD = NULL; }
        for (i = 0; i < 6; ++i) {
            if (pEnv->apCtl[i] != NULL) {
                UiControl_FreePads(pEnv->apCtl[i]); /* 0x6F328 */
            }
            /* 0x6F330 also operator delete's the control; the port's six are caller-owned. */
        }
        return;
    }

    *pEnv->pbPanelsBuilt = 1; /* 0x6EA04 */
    nTopicCount = (pEnv->pbTopicCount != NULL) ? (i32)*pEnv->pbTopicCount : 0;

    /* Count pass, 0x6EA0C..0x6EA78: topics 1..count-1, five slot ids each, abManufRowFlag as the filter. */
    if (nTopicCount > 1 && pEnv->aTopics != NULL) {
        for (i32 nTopic = 1; nTopic < nTopicCount; ++nTopic) {
            if (pEnv->aTopics[nTopic].bUnlocked != 1) { /* 0x6EA30 LDRB [R2,#0xD] */
                continue;
            }
            for (i = 0; i < 5; ++i) { /* 0x6EA64 CMP R0,#5 */
                if (ManufRowTakesLine(pEnv->aTopics[nTopic].abItemIds[i])) { /* 0x6EA50 */
                    ++nRows;
                }
            }
        }
    }

    /* The four panels, 0x6EA7C..0x6EE5C. All four share nStyle 13, nFont 16, nPosY 70, nUnk7056 70 and
     * the two style byte groups; only A gets bytes 3..5. */
    pA = BuySellNewPanel(pEnv->ppPanelA, nRows);
    if (pA != NULL) {
        pA->nStyle = 13;   /* 0x6EAC0 */
        pA->nFont = 16;    /* 0x6EAD4 */
        pA->pfnClick = pEnv->pfnSelectListed; /* 0x6EAF0 */
        pA->nPosX = 10;    /* 0x6EB04 */
        pA->nPosY = 70;    /* 0x6EB18 */
        pA->nUnk7052 = 5;  /* 0x6EB2C */
        pA->nUnk7056 = 70; /* 0x6EB40 */
        pA->nViewW = 230;  /* 0x6EB4C */
        UiPanel_SetBytes0to2(pA, 0xB4, 0xB4, 0xB4);                        /* 0x6EB58 */
        UiPanel_SetBytes6to11(pA, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);     /* 0x6EB7C */
        pA->bFlag7073 = 1;                                                 /* 0x6EB90 */
        UiPanel_SetBytes3to5(pA, 0x32, 0x32, 0xFA);                        /* 0x6EBA4 */
        pA->bUnk7088 = (u8)0x96;                                           /* 0x6EBB8 */
    }
    pB = BuySellNewPanel(pEnv->ppPanelB, nRows);
    if (pB != NULL) {
        pB->nStyle = 13;
        pB->nFont = 16;
        pB->nPosX = 150;   /* 0x6EC20, R11 = 0x1B84 */
        pB->nPosY = 70;
        pB->nUnk7052 = 155; /* 0x6EC34, R8 = 0x1B8C */
        pB->nUnk7056 = 70;
        pB->nViewW = 30;   /* 0x6EC48, R6 = 0x1B94 */
        UiPanel_SetBytes0to2(pB, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pB, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pB->nUnk7076 = 2;  /* 0x6EC88 */
    }
    pC = BuySellNewPanel(pEnv->ppPanelC, nRows);
    if (pC != NULL) {
        pC->nStyle = 13;
        pC->nFont = 16;
        pC->nPosX = 187;   /* 0x6ECEC */
        pC->nPosY = 70;
        pC->nUnk7052 = 185; /* 0x6ED00 */
        pC->nUnk7056 = 70;
        pC->nViewW = 30;   /* 0x6ED14 */
        UiPanel_SetBytes0to2(pC, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pC, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pC->nUnk7076 = 2;  /* 0x6ED54 */
    }
    pD = BuySellNewPanel(pEnv->ppPanelD, nRows);
    if (pD != NULL) {
        pD->nStyle = 13;
        pD->nFont = 16;
        pD->pfnClick = pEnv->pfnSyncSelection; /* 0x6EDB8 */
        pD->nPosX = 233;   /* 0x6EDC8 */
        pD->nPosY = 70;
        pD->nUnk7052 = 5;  /* 0x6EDDC */
        pD->nUnk7056 = 70;
        pD->nViewW = 230;  /* 0x6EDF0 */
        UiPanel_SetBytes0to2(pD, 0xB4, 0xB4, 0xB4);
        UiPanel_SetBytes6to11(pD, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
        pD->nUnk7076 = 2;  /* 0x6EE34 */
        pD->bFlag7073 = 1; /* 0x6EE48 */
        pD->bUnk7088 = (u8)0x96; /* 0x6EE5C */
    }

    /* Fill pass, 0x6EE60..0x6F25C. No sort pass after it: the rows stay in topic order, which is what
     * tells this build apart from the buy/sell and transfer ones. */
    if (nTopicCount > 1 && pEnv->aTopics != NULL) {
        for (i32 nTopic = 1; nTopic < nTopicCount; ++nTopic) {
            if (pEnv->aTopics[nTopic].bUnlocked != 1) { /* 0x6EEB0 */
                continue;
            }
            for (i = 0; i < 5; ++i) { /* 0x6F23C */
                const i32 nSlot = pEnv->aTopics[nTopic].abItemIds[i]; /* 0x6EEC8 LDRB [R7,R11]! */
                const ObjectRecFull *pObj;

                if (!ManufRowTakesLine(nSlot)) { /* 0x6EECC LDRSB abManufRowFlag, CMP #1 */
                    continue;
                }
                pObj = (pEnv->apObjects != NULL && nSlot < pEnv->nObjectCount)
                           ? pEnv->apObjects[nSlot] : NULL; /* deviation: 0x6EEDC derefs blindly */
                if (nRow < PUFO_PANEL_LINES) {
                    ManufFillRow(pEnv, pA, pB, pC, pD, nRow, nTopic, nSlot, pObj);
                    ++nRow; /* 0x6F21C */
                }
            }
        }
    }
}

/* ManufScreen_Draw @ 0x6e3e0. The manufacturing screen's "draw" click: refresh the three numeric
 * columns for the selected row, seed a product that has work allocated but none accumulated yet, and
 * re-run the two needed-facility passes on the selected base. */
void ManufScreen_Draw(ManufDrawEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pProductPanel == NULL) {
        return;
    }

    const i32 nSlot = ManufSelectedProduct(pEnv->pProductPanel, pEnv->pnProductRow); /* 0x6E40C */
    const i32 nRow = (pEnv->pnProductRow != NULL) ? *pEnv->pnProductRow : 0;
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;

    if (pEnv->pbProductSelected != NULL) {
        *pEnv->pbProductSelected = 0; /* 0x6E410 */
    }
    pEnv->pProductPanel->bFlag7073 = 1; /* 0x6E424 */
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pSampleUser); /* 0x6E430 */
    }

    const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBase + nSlot; /* 0x6E4D0 */
    const bool bIndexed = (nSlot >= 0 && nIdx >= 0 && nIdx < PUFO_PRODUCT_COLUMN_TOTAL);

    if (bIndexed) {
        if (pEnv->apProductB != NULL && pEnv->apProductB[nIdx] == 0 && pEnv->pClearEnv != NULL) {
            ManufScreen_ClearProductSlot(pEnv->pClearEnv, nBase, nSlot & 0xFF); /* 0x6E48C */
        }

        /* Re-read after the clear: the original reloads all three from memory at 0x6E4E4. */
        u32 nAccum = (pEnv->apProductA != NULL) ? pEnv->apProductA[nIdx] : 0u;
        const i32 nTarget = (pEnv->apProductB != NULL) ? pEnv->apProductB[nIdx] : 0;
        const u16 nAllocated = (pEnv->apManufCols != NULL) ? pEnv->apManufCols[nIdx] : 0u;

        if (nAccum == 0u && nTarget != 0 && nAllocated != 0u && pEnv->apProductA != NULL) {
            pEnv->apProductA[nIdx] = 1u; /* 0x6E51C STRNE R11,#1 */
            /* 0x6E6D4 reloads apProductA from memory after the seed, so the seeded row takes the
             * percent branch with one unit of work in it, not the empty-string branch. */
            nAccum = pEnv->apProductA[nIdx];
        }

        /* ORIGINAL BUG (0x6E52C, dead code): the hangar branch tests the dword at panel offset 0
         * against 119..121 instead of the selected product id, and BuildPanels leaves that dword at its
         * style bytes 0x32B4B4B4, so it never runs in the shipped build. Reproduced as written; the
         * test forces the dword into range to show what the branch would do. */
        const i32 nStyleDword = *(const i32 *)(const void *)pEnv->pProductPanel->gap_0;
        if (nStyleDword > 118 && nStyleDword < 122) { /* 0x6E530 / 0x6E538: products 119..121 */
            if (pEnv->apBases != NULL && nBase >= 0 && nBase < pEnv->nBaseCount
                && pEnv->apBases[nBase] != NULL && pEnv->abGridBytes != NULL) {
                const u8 nType = pEnv->apBases[nBase]->bUnk4; /* 0x6E5A4 LDRB [R3,#4] */
                const i32 nCurrent = BaseGrid_GetTypeByte(pEnv->apBases, pEnv->nBaseCount,
                                                          pEnv->abGridBytes, nType); /* 0x6E558 */
                /* 0x6E58C LDRB [R3,#0x1904] is the LOW BYTE of apProductB[idx]; 0x6E590 adds it to the
                 * byte just read and 0x6E5A8 writes the sum back as a byte. */
                (void)BaseGrid_SetTypeByte(pEnv->apBases, pEnv->nBaseCount, pEnv->abGridBytes, nType,
                                           (i8)(u8)(nCurrent + (i32)((u32)nTarget & 0xFFu)));
            }
        }

        char szLine[PUFO_PANEL_LINE_BYTES];

        /* Panel B: the allocated work, or the empty string at 0x10DA38 when nothing is allocated. */
        if (nAllocated != 0u) {
            (void)snprintf(szLine, sizeof(szLine), "%u", (unsigned)nAllocated); /* j_itoa, 0x6E614 */
        } else {
            szLine[0] = '\0'; /* 0x6E680 */
        }
        if (pEnv->pPanelB != NULL) {
            (void)UiPanel_SetLineText(pEnv->pPanelB, nRow, szLine); /* 0x6E630..0x6E6A0 */
        }

        const ObjectRecFull *pObj = (pEnv->apObjects != NULL && nSlot < pEnv->nObjectCount)
                                        ? pEnv->apObjects[nSlot] : NULL; /* deviation: 0x6E6F8 derefs blindly */
        const u16 nWork = (pObj != NULL) ? pObj->uManufWorkPerUnit : 0u; /* 0x6E704 LDRH [R3,#0xBA] */

        if (nAccum != 0u) {
            /* Panel C: 100 * accumulated / (work-per-unit * asked-for), then "%". Divisor in R0 at
             * 0x6E708, dividend in R1 at 0x6E714 -- _rt_sdiv's argument order. */
            const i32 nNum = (i32)(nAccum * 100u);
            const i32 nDen = (i32)((u32)nWork * (u32)nTarget);
            (void)snprintf(szLine, sizeof(szLine), "%d%%", (nDen != 0) ? (nNum / nDen) : 0);
            if (pEnv->pPanelC != NULL) {
                (void)UiPanel_SetLineText(pEnv->pPanelC, nRow, szLine); /* 0x6E744, then strcat("%") 0x6E774 */
            }

            /* Panel D: what was asked for, then the units the accumulated work has paid for. */
            const i32 nUnits = (nWork != 0u) ? ((i32)nAccum / (i32)nWork) : 0; /* 0x6E83C..0x6E848 */
            (void)snprintf(szLine, sizeof(szLine), "%u/%d", (unsigned)(u32)nTarget, nUnits);
            if (pEnv->pPanelD != NULL) {
                (void)UiPanel_SetLineText(pEnv->pPanelD, nRow, szLine); /* 0x6E7B8, "/" at 0x6E7E8, 0x6E86C */
            }
        } else {
            if (pEnv->pPanelC != NULL) { (void)UiPanel_SetLineText(pEnv->pPanelC, nRow, ""); } /* 0x6E898 */
            if (pEnv->pPanelD != NULL) { (void)UiPanel_SetLineText(pEnv->pPanelD, nRow, ""); } /* 0x6E8C8 */
        }
    }

    if (pEnv->abFacilityGrid != NULL) {
        const i32 nEntries = (pEnv->pbEntryCount != NULL) ? (i32)*pEnv->pbEntryCount : 0;
        const i32 nActiveManuf = (pEnv->apManufCols != NULL)
                                     ? Base_CountActiveManuf(pEnv->apManufCols +
                                                             PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                                             nEntries)
                                     : 0;
        const i32 nActiveResearch = (pEnv->apResearchCols != NULL)
                                        ? Base_CountActiveResearch(pEnv->apResearchCols +
                                                                   PUFO_RESEARCH_TOPIC_COUNT * nBase,
                                                                   nEntries)
                                        : 0;
        (void)Base_UpdateWorkshopNeeded(pEnv->abFacilityGrid, nBase, nActiveManuf); /* 0x6E8F4 */
        (void)Base_UpdateLabNeeded(pEnv->abFacilityGrid, nBase, nActiveResearch);    /* 0x6E908: tail call */
    }
}

/* ManufScreen_DrawFrame @ 0x6f9a4. Every label is a TEXTS.DAT record: the ARM builds each address as
 * `LDR R0, =(<addr> - 0xB38D0)` against R8 = 0xB38D0, so each is aTextRecs + 205*n. */
static const char *ManufUi_Text(const ManufFrameEnv *pEnv, i32 nRec)
{
    if (pEnv->pTextRecs == NULL) {
        return "";
    }
    return pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec;
}

static void ManufUi_DrawCentered(i32 nCenterX, i32 y, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz); /* 0x6FC54, 0x70088, 0x70BC8... */
    Text_DrawString(nCenterX - nWidth / 2, y, psz);
}

static const char *ManufUi_BaseName(const ManufFrameEnv *pEnv, i32 nBaseIdx)
{
    if (pEnv->apSiteRecs == NULL || nBaseIdx < 0 || nBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return "";
    }
    return pEnv->apSiteRecs[nBaseIdx].abName; /* 0x6FC30: 0x186C40 + 64*i + 26 */
}

/* The four component rows' labels, in the same order as g_anManufComponentSlots above: the label at
 * index k is answered by the stock of the object slot at index k. */
static const i32 g_anManufCompText[4] = { PUFO_MANUF_TEXT_COMP_ELERIUM, PUFO_MANUF_TEXT_COMP_ALLOY,
                                          PUFO_MANUF_TEXT_COMP_POWER, PUFO_MANUF_TEXT_COMP_NAV };

/* 0x70DB8..0x70EB4: the shortfall panel, reached from all three exits of the frame. */
static void ManufUi_DrawShortfall(const ManufFrameEnv *pEnv, char *szLabel, size_t cchLabel)
{
    const u32 nShortfall = (pEnv->pdwShortfall != NULL) ? *pEnv->pdwShortfall : 0u;
    if (nShortfall != 1u) {
        return; /* 0x70DC4 BNE: the value is the original's return value, which Geoscape_Draw drops */
    }
    const i32 nReason = (pEnv->pnShortfallReason != NULL) ? *pEnv->pnShortfallReason : 0;
    if (nReason == 0) {
        (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1); /* 0x70E44 */
        pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_NO_STORES), cchLabel);
    } else if (nReason == 1) {
        (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1); /* 0x70DE0 */
        pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_NO_HANGAR), cchLabel);
    } else {
        return; /* 0x70DDC BNE */
    }
    ManufUi_DrawCentered(120, 158, szLabel); /* 0x70E9C */
}

/* ManufScreen_DrawFrame @ 0x6f9a4: the manufacturing screen's frame, panels, labels, component
 * requirements, crew and capacity read-out, buttons and shortfall message. The header carries the
 * branch map. */
void ManufScreen_DrawFrame(const ManufFrameEnv *pEnv)
{
    char szLabel[PUFO_MANUF_LABEL_MAX];
    char szNum[52]; /* var_34 @ sp+0x88, the j_itoa buffer */
    i32 nBand = 0;

    if (pEnv == NULL) {
        return;
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->pbEntryCount != NULL) ? (i32)*pEnv->pbEntryCount : 0;

    BaseUi_DrawFrameRails(); /* 0x6F9C4..0x6FB54 */

    /* 0x6FC78..0x6FC98: the row band is 16 pixels per row, capped by the panel's style. */
    if (pEnv->pPanelA != NULL) {
        nBand = 16 * pEnv->pPanelA->nLineCount;
        if (pEnv->pPanelA->nLineCount >= pEnv->pPanelA->nStyle) {
            nBand = 16 * pEnv->pPanelA->nStyle;
        }
    }

    if (pEnv->pbPanelsBuilt != NULL && *pEnv->pbPanelsBuilt != 0) {
        if (pEnv->pPanelA != NULL) {
            UiPanel_DrawText(pEnv->pPanelA); /* 0x6FB84 */
        }
        if (pEnv->pPanelB != NULL) {
            UiPanel_DrawText(pEnv->pPanelB); /* 0x6FB90 */
        }
        if (pEnv->pPanelC != NULL) {
            UiPanel_DrawText(pEnv->pPanelC); /* 0x6FB9C */
        }
        if (pEnv->pPanelD != NULL) {
            UiPanel_DrawText(pEnv->pPanelD); /* 0x6FBA8 */
        }

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 0, 0, 0); /* 0x6FBCC */
        pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_HEADER), sizeof(szLabel));
        Text_DrawString(10, 52, szLabel); /* 0x6FC08: "Производство          Инж. Вып. Зак./Гот." */

        pufo_strncpy(szLabel, ManufUi_BaseName(pEnv, nBase), sizeof(szLabel));
        ManufUi_DrawCentered(120, 13, szLabel); /* 0x6FC74 */

        /* 0x6FC94..0x6FD3C: the object under the map cursor, named from PANEL D's row cursor. */
        if ((pEnv->pbProductSelected == NULL || *pEnv->pbProductSelected == 0)
            && pEnv->pbPreviewOk != NULL && *pEnv->pbPreviewOk != 0
            && pEnv->pnCursorY != NULL && *pEnv->pnCursorY > 70 && *pEnv->pnCursorY < nBand + 70
            && pEnv->pPanelA != NULL && pEnv->pPanelD != NULL) {
            const i32 nRow = pEnv->pPanelD->nUnk3032; /* 0x6FCD8 */
            const ObjectRecFull *pObj = NULL;
            if (nRow >= 0 && nRow < PUFO_PANEL_LINES) {
                const i32 nSlot = pEnv->pPanelA->anLineAux[nRow]; /* 0x6FCE4 */
                if (nSlot >= 0 && nSlot < pEnv->nObjectCount && pEnv->apObjects != NULL) {
                    pObj = pEnv->apObjects[nSlot];
                }
            }
            (void)Gfx_PackRgb565Pair(pEnv->pColor, 150, 150, 250, 0, 0, 0); /* 0x6FD28 */
            Text_DrawString(10, 300, (pObj != NULL) ? (const char *)pObj->szName : ""); /* 0x6FD3C */
        }

        /* 0x6FD40..0x6FD54: the three column separators. */
        SoftSurf_BlitRectProxy(0, 130, 69, 1, nBand + 2, PUFO_MANUF_SEP_INDEX,
                               PUFO_MANUF_SEP_INDEX, PUFO_MANUF_SEP_INDEX);
        SoftSurf_BlitRectProxy(0, 152, 69, 1, nBand + 2, PUFO_MANUF_SEP_INDEX,
                               PUFO_MANUF_SEP_INDEX, PUFO_MANUF_SEP_INDEX); /* 0x6FD94 */
        SoftSurf_BlitRectProxy(0, 187, 69, 1, nBand + 2, PUFO_MANUF_SEP_INDEX,
                               PUFO_MANUF_SEP_INDEX, PUFO_MANUF_SEP_INDEX); /* 0x6FDD8 -- see below */
    }

    if (pEnv->pbProductSelected == NULL || *pEnv->pbProductSelected == 0) {
        /* 0x70D84..0x70D94: no product selected -- the reset control, then the teal pair. */
        (void)UiControl_BlitActivePad(pEnv->pCtlReset, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlUser);
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);
        ManufUi_DrawShortfall(pEnv, szLabel, sizeof(szLabel));
        return;
    }

    /* 0x6FE00: the detail panel. When the animation is not ready nothing else is drawn. */
    if (!Ui_AnimExpandBlit(pEnv->pAnim, 0, 30, 236, 284, 4, 0x28, 0x2D, 0xC8, 3)) {
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30); /* 0x70D58 */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x70D94 */
        ManufUi_DrawShortfall(pEnv, szLabel, sizeof(szLabel));
        return;
    }

    const i32 nSlot = ManufSelectedProduct(pEnv->pPanelA, pEnv->pnProductRow); /* 0x6FE40 */
    const bool bSlot = (nSlot >= 0 && nSlot < pEnv->nObjectCount && pEnv->apObjects != NULL);
    const ObjectRecFull *pObj = bSlot ? pEnv->apObjects[nSlot] : NULL;

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 0, 0, 0); /* 0x6FE2C */
    pufo_strncpy(szLabel, (pObj != NULL) ? (const char *)pObj->szName : "", sizeof(szLabel));
    ManufUi_DrawCentered(120, 45, szLabel); /* 0x6FE90 */

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 0, 0, 0); /* 0x6FEB0 */
    (void)snprintf(szNum, sizeof(szNum), "%u",
                   (unsigned)((pObj != NULL) ? pObj->uManufWorkPerUnit : 0u)); /* j_itoa, 0x6FED8 */
    pufo_strncpy(szLabel, szNum, sizeof(szLabel));
    (void)pufo_strcat(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_WORK_HOURS), sizeof(szLabel)); /* 0x6FF04 */
    Text_DrawString(10, 69, szLabel);

    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_ONE_UNIT), sizeof(szLabel));
    Text_DrawString(10, 83, szLabel); /* 0x6FF58: "одной единицы" */

    /* 0x6FF88..0x70014: the price of one unit, at 70 percent of the record's own price. The 0.7 is
     * the double the ARM builds from 0x66666666 / 0x3FE66666; CommitProduction uses 1.4 for the same
     * table, so the screen quotes half of what committing costs. */
    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_PRICE), sizeof(szLabel));
    {
        const u32 nPrice = (bSlot && pEnv->pdwPrices != NULL) ? pEnv->pdwPrices[nSlot] : 0u;
        (void)snprintf(szNum, sizeof(szNum), "%d", (int)((double)nPrice * PUFO_MANUF_PRICE_FACTOR));
    }
    (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
    (void)pufo_strcat(szLabel, "$", sizeof(szLabel)); /* asc_AB1D8, 0x70000 */
    Text_DrawString(10, 97, szLabel);

    /* 0x7002C..0x7073C: the four component rows. A zero component draws nothing and does not advance
     * the row counter, so the rows stack only where something is required. */
    {
        i32 nRows = 0;
        for (i32 k = 0; k < 4; ++k) {
            const u8 nNeed = (pObj != NULL) ? pObj->abManufComponents[k] : 0u; /* 0x70048 LDRB #0xC0 */
            if (nNeed == 0u) {
                continue; /* 0x70050 BLS */
            }
            ManufUi_DrawCentered(140, 114, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_REQUIRED));  /* 0x700A8 */
            ManufUi_DrawCentered(200, 114, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_AVAILABLE)); /* 0x700FC */

            const i32 nStockSlot = g_anManufComponentSlots[k];
            u32 nStock = 0u;
            if (nStockSlot < pEnv->nObjectCount && pEnv->apObjects != NULL
                && pEnv->apObjects[nStockSlot] != NULL && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT) {
                nStock = pEnv->apObjects[nStockSlot]->auPerBaseVal[nBase]; /* 0x7015C LDRH #0x2A */
            }
            if ((u32)nNeed > nStock) {
                const i32 nPhase = (pEnv->pbMapPhase != NULL) ? (i32)*pEnv->pbMapPhase : 0;
                (void)Gfx_PackRgb565Pair(pEnv->pColor, 20 * (nPhase + 5), 10, 10, 50, 0, 0); /* 0x70194 */
                if (pEnv->pbQtyGate != NULL) {
                    *pEnv->pbQtyGate = 1; /* 0x7019C, and its three twins */
                }
            }
            const i32 nY = nRows * 14 + 130; /* 0x701C8 ADD R5,R9,#0x82, with R9 stepping by 0xE */
            Text_DrawString(10, nY, ManufUi_Text(pEnv, g_anManufCompText[k]));
            (void)snprintf(szNum, sizeof(szNum), "%u", (unsigned)nNeed);
            Text_DrawString(140, nY, szNum);
            (void)snprintf(szNum, sizeof(szNum), "%u", (unsigned)nStock);
            Text_DrawString(200, nY, szNum); /* 0x70700 */
            (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 0, 0, 0); /* 0x70724 */
            ++nRows;
        }
    }

    /* 0x70740..0x707F4: engineers available, and free places in workshops. */
    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_ENG_FREE), sizeof(szLabel));
    {
        const u16 *pCol = (pEnv->apManufCols != NULL)
                              ? pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase : NULL;
        const i32 nEngineers = (pEnv->pEngineerRec != NULL && pCol != NULL)
                                   ? Base_GetEngineerCount(pEnv->pEngineerRec, pCol, nBase, nEntries)
                                   : 0;
        (void)snprintf(szNum, sizeof(szNum), "%d", nEngineers);
    }
    (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
    Text_DrawString(10, 185, szLabel);

    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_WS_FREE), sizeof(szLabel));
    {
        const u16 *pCol = (pEnv->apManufCols != NULL)
                              ? pEnv->apManufCols + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase : NULL;
        const i32 nActive = (pCol != NULL) ? Base_CountActiveManuf(pCol, nEntries) : 0;
        const i32 nCap = (pEnv->abFacilityGrid != NULL)
                             ? Base_CalcWorkshopCap(pEnv->abFacilityGrid, nBase) : 0;
        (void)snprintf(szNum, sizeof(szNum), "%d", nCap - nActive); /* 0x70808 SUB R0,R0,R1 */
    }
    (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
    Text_DrawString(10, 199, szLabel);

    /* 0x7080C..0x709F4: the two column captions and the values under them. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 150, 150, 255, 20, 20, 120);
    Text_DrawString(10, 216, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_ENG_CAP));  /* 0x70868 "Инженеров" */
    Text_DrawString(140, 216, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_UNIT_CAP)); /* 0x70968 "Единиц" */

    const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBase + nSlot;
    const bool bIndexed = (nIdx >= 0 && nIdx < PUFO_PRODUCT_COLUMN_TOTAL);
    const u16 nAllocated = (bIndexed && pEnv->apManufCols != NULL) ? pEnv->apManufCols[nIdx] : 0u;
    const i32 nAsked = (bIndexed && pEnv->apProductB != NULL) ? pEnv->apProductB[nIdx] : 0;

    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_ENG_USED), sizeof(szLabel));
    (void)snprintf(szNum, sizeof(szNum), "%u", (unsigned)nAllocated); /* 0x7090C */
    (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
    Text_DrawString(10, 230, szLabel); /* 0x7092C "занято: " */

    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_UNIT_ASK), sizeof(szLabel));
    (void)snprintf(szNum, sizeof(szNum), "%d", nAsked); /* 0x709D4 */
    (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
    Text_DrawString(140, 230, szLabel); /* 0x709F4 "произвести: " */

    /* 0x709F8..0x70A70: the two arrow-button captions, one pair per button column. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);
    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_ADD), sizeof(szLabel));
    Text_DrawString(38, 251, szLabel);  /* 0x70A5C */
    Text_DrawString(170, 251, szLabel); /* 0x70A70 */
    pufo_strncpy(szLabel, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_REMOVE), sizeof(szLabel));
    Text_DrawString(38, 271, szLabel);  /* 0x70B04 region */
    Text_DrawString(170, 271, szLabel);

    /* 0x70AF8..0x70B14: the last four of the six arrow controls. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 0, 0, 0);
    for (i32 k = 2; k < 6; ++k) {
        (void)UiControl_BlitActivePad(pEnv->apCtl[k], pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlUser);
    }

    /* 0x70B38..0x70B94: the two quantity buttons are MOVED and re-pointed every frame. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);
    if (pEnv->pCtlAllocAll != NULL) {
        pEnv->pCtlAllocAll->x = 94;  /* 0x70B48 STRH #0x5E */
        pEnv->pCtlAllocAll->y = 251; /* 0x70B50 STRH #0xFB */
        (void)UiControl_BlitActivePad(pEnv->pCtlAllocAll, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlUser);
        pEnv->pCtlAllocAll->pfnClick = pEnv->pfnAllocAllClick; /* 0x70B68 */
    }
    if (pEnv->pCtlClampOne != NULL) {
        pEnv->pCtlClampOne->x = 94;  /* 0x70B78 */
        pEnv->pCtlClampOne->y = 271; /* 0x70B80: 0x10F built as ORR #3 */
        (void)UiControl_BlitActivePad(pEnv->pCtlClampOne, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlUser);
        pEnv->pCtlClampOne->pfnClick = pEnv->pfnClampOneClick; /* 0x70B94 */
    }

    ManufUi_DrawCentered(114, 252, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_ALL));       /* 0x70BE8 "ВСЕ" */
    ManufUi_DrawCentered(113, 272, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_CLAMP_ONE)); /* 0x70C3C: empty */

    /* 0x70C40..0x70D08: the two bottom controls and their captions. */
    (void)UiControl_BlitActivePad(pEnv->pCtlDraw, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlUser);
    ManufUi_DrawCentered(73, 295, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_OK));    /* 0x70CA0 "ОК" */
    (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlUser);
    ManufUi_DrawCentered(164, 295, ManufUi_Text(pEnv, PUFO_MANUF_TEXT_CANCEL)); /* 0x70D08 "Отмена" */

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30); /* 0x70D58 */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x70D94 */
    ManufUi_DrawShortfall(pEnv, szLabel, sizeof(szLabel));
}

void ManufScreen_IncEngineers(const ManufEngineerEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pbQtyGate != NULL && (*pEnv->pbQtyGate & 0xFFu) != 0) {
        return; /* 0x6E14C */
    }

    /* No byte mask here, unlike ClearProductSlot: this function uses the aux cell's raw dword for
     * both the column index and the object lookup. */
    const i32 nProduct = ManufSelectedProduct(pEnv->pProductPanel, pEnv->pnProductRow);
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)(i8)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nIdx = PUFO_PRODUCT_ENTRIES_PER_BASE * nBase + nProduct;
    const bool bWritable = (nIdx >= 0 && nIdx < PUFO_PRODUCT_COLUMN_TOTAL &&
                            pEnv->apProductB != NULL);

    if (nProduct >= PUFO_MANUF_HANGAR_PRODUCT_FIRST && nProduct <= PUFO_MANUF_HANGAR_PRODUCT_LAST) {
        const i32 nCap = Base_CalcHangarCap(pEnv->abFacilityGrid, nBase);
        const i32 nAt = Base_CountShipsAtBase(pEnv->apCraft, pEnv->nCraftCount,
                                              pEnv->apBases, pEnv->nBaseCount,
                                              pEnv->abGridBytes, nBase);
        const i32 nFree = nCap - nAt; /* 0x6E1B0 SUBS LR, R11, R0 */

        if (nCap == nAt) { /* 0x6E1B4: a full hangar, so the free-slot count is zero */
            if (pEnv->pdwShortfall != NULL) {
                *pEnv->pdwShortfall = 1; /* 0x6E1BC, a dword store into byte_1870C8 */
            }
            if (pEnv->pnResidue != NULL) {
                *pEnv->pnResidue = 1; /* 0x6E1C8 */
            }
        }

        if (pEnv->pbQtyGate != NULL && (*pEnv->pbQtyGate & 0xFFu) != 0) {
            return; /* 0x6E1CC: the gate is read a second time, after those two stores */
        }

        /* With a full hangar nFree is 0 and the allocation is never negative, so this test fails
         * and the flags above are the whole result. */
        if (bWritable && pEnv->apProductB[nIdx] < nFree) {
            pEnv->apProductB[nIdx] = pEnv->apProductB[nIdx] + 1; /* 0x6E228 */
        }
        return;
    }

    const i32 nUsed = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase);   /* 0x6E274 */
    const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, pEnv->nSiteCount, nBase,
                                                     pEnv->apBases, nBase,
                                                     pEnv->apObjects, pEnv->nObjectCount, true);
    const i32 nCap = Base_CalcStoresCapacity(pEnv->abFacilityGrid, nBase);                /* 0x6E29C */

    /* 0x6E214: the object is apObjects[nProduct] -- the PRODUCT number indexes the object table,
     * not the base -- and only its bCargoSize byte at +0xC6 is read. */
    i32 nCargo = 0;
    if (pEnv->apObjects != NULL && nProduct >= 0 && nProduct < pEnv->nObjectCount) {
        nCargo = (i32)pEnv->apObjects[nProduct]->bCargoSize;
    }

    if (!bWritable) {
        return;
    }

    const i32 nAlloc = pEnv->apProductB[nIdx];
    if (nCargo * nAlloc + nAcross + nUsed < nCap) { /* 0x6E300 CMP R2, R0 */
        pEnv->apProductB[nIdx] = nAlloc + 1;        /* 0x6E30C */
    } else {
        if (pEnv->pdwShortfall != NULL) {
            *pEnv->pdwShortfall = 1; /* 0x6E314 */
        }
        if (pEnv->pnResidue != NULL) {
            *pEnv->pnResidue = 0; /* 0x6E320: 0 here, 1 on the full-hangar path */
        }
    }
}

/* The four item columns the buy/sell confirm test adds to the cargo sum and the soldier count
 * (0x750E8, 0x750F4, 0x750EC, 0x750F8 -- the ARM loads them out of order and sums them). */
static const i32 g_anBuySellConfirmItems[4] = {112, 113, 117, 118};

/* BuySellScreen_ConfirmSiteAndClose @ 0x75090. Promote the transfer flow's working site -- the
 * record AT the site count -- into a real site when it holds anything, then close the screen. */
void BuySellScreen_ConfirmSiteAndClose(const BuySellConfirmEnv *pEnv,
                                       const GeoscapeCloseEnv *pClose)
{
    if (pEnv == NULL) {
        return;
    }

    const i32 nSiteCount = (pEnv->pnSiteCount != NULL) ? *pEnv->pnSiteCount : 0;
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)(i8)*pEnv->pnSelectedBaseIdx : 0;

    if (pEnv->aSites != NULL && nSiteCount >= 0 && nSiteCount < PUFO_SMAP_SITE_COUNT) {
        SmapSite *pSite = &pEnv->aSites[nSiteCount];
        bool bConfirm = false;

        /* 0x750B0: the site index is the site count, the base index is nSelectedBaseIdx, and the
         * scan-sites flag is OFF -- so only this one record's cargo is measured. */
        bConfirm = Base_CountItemQtyAcrossBases(pEnv->aSites, nSiteCount, nSiteCount, pEnv->apBases,
                                                nBase, pEnv->apObjects, pEnv->nObjectCount,
                                                false) != 0;

        if (!bConfirm && pSite->uRecoveredSoldiers != 0) {
            bConfirm = true; /* 0x750DC */
        }
        if (!bConfirm) {
            i32 nSum = 0;
            for (i32 i = 0; i < (i32)(sizeof(g_anBuySellConfirmItems) /
                                     sizeof(g_anBuySellConfirmItems[0])); ++i) {
                nSum += pSite->auItemQty[g_anBuySellConfirmItems[i]];
            }
            bConfirm = nSum != 0; /* 0x75104 ADDS / BEQ */
        }

        if (bConfirm) {
            pSite->bFlag0 = PUFO_SITE_FLAG_CONFIRM; /* 0x75120 STRB */
            if (pEnv->pnSiteCount != NULL) {
                *pEnv->pnSiteCount = nSiteCount + 1; /* 0x75124 */
            }
        }
    }

    if (pEnv->pnFundsSnapshot != NULL) {
        *pEnv->pnFundsSnapshot = pEnv->nFunds; /* 0x75130, the buy/sell scratch copy */
    }
    Geoscape_CloseUI(pClose); /* 0x7513C, the tail call */
}

/* region id minus 0x80 -> the funding slot it feeds, -1 when the region gives no bonus. The order is
 * the compare chain's, not the slot order: 0x84 feeds slot 1 while 0x82 and 0x83 feed slots 2
 * and 3. */
static const i8 g_anFundingSlotByRegion[PUFO_REGION_ID_COUNT] = {0, 0, 2, 3, 1, 4, -1, -1, 0, -1};

/* Funding_AddRegionBonus @ 0x7debc. One radar contact in one region adds two to that region's
 * country score, or -- for the ocean tile -- to the monthly multiplier. */
void Funding_AddRegionBonus(i32 *anFundingSlots, i32 nRegionId)
{
    if (anFundingSlots == NULL) {
        return;
    }

    const u32 bRegion = (u32)(u8)nRegionId; /* 0x7DEBC AND R0,R1,#0xFF */
    i32 nSlot = -1;

    if (bRegion >= PUFO_REGION_ID_FIRST && bRegion < PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
        nSlot = g_anFundingSlotByRegion[bRegion - PUFO_REGION_ID_FIRST];
    } else if (bRegion == PUFO_REGION_ID_NONE) {
        nSlot = PUFO_FUNDING_MULT_SLOT; /* 0x7DF48 */
    }

    if (nSlot >= 0) {
        anFundingSlots[nSlot] += PUFO_FUNDING_BONUS;
    }
}

/* The report's row order against the country score slots. FundingScreen_DrawExtended reads the five
 * shares 0x186580, 0x186588, 0x18658C, 0x186584, 0x186590 for rows 0..4 (0x5B804..0x5B8A4) and
 * FundingScreen_Init writes the same five in the same order (0x5C34C..0x5C38C). */
const i32 g_anFundingRowSlot[PUFO_FUND_ROW_COUNT] = {0, 2, 3, 1, 4};

/* 0x5C22C..0x5C324: the monthly upkeep subtracted from each row. The ARM loads three figures per
 * month class -- one shared by USA, Euro and Asia, one for Australia, one for Africa -- and the four
 * classes are the branches of the compare chain at 0x5C22C..0x5C250. Note that month <= 0 and month
 * >= 6 land in the SAME block (0x5C230 BLE and 0x5C250 BNE both go to 0x5C31C), so a month counter
 * of zero is not "no upkeep" but the heaviest upkeep. */
static const i32 g_anFundingUpkeep[4][PUFO_FUND_ROW_COUNT] = {
    {-1100, -1100, -1100, -300, -800}, /* month <= 0 or month >= 6 */
    { -300,  -300,  -300, -100, -200}, /* month 1, 2, 3 */
    { -500,  -500,  -500, -200, -350}, /* month 4 */
    { -700,  -700,  -700, -300, -500}, /* month 5 */
};

static i32 FundingUpkeepClass(i32 nMonth)
{
    if (nMonth <= 0 || nMonth >= 6) {
        return 0;
    }
    if (nMonth <= 3) {
        return 1;
    }
    return (nMonth == 4) ? 2 : 3;
}

/* FundingScreen_Init @ 0x5c084. See the evidence block in pufo_baseui.h. */
void FundingScreen_Init(FundingInitEnv *pEnv)
{
    if (pEnv == NULL || pEnv->ppPadSlot == NULL || pEnv->pnPendingTotal == NULL
        || pEnv->apBases == NULL) {
        return;
    }

    PadImage *const pPad = BuySellPadNew(); /* 0x5C090: operator new(0x14) then PadImage_ctor */
    *pEnv->ppPadSlot = pPad;                /* 0x5C0B4: the ctor's result, not the raw allocation */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_FUNDING_PAD_NAME, PUFO_FUNDING_PAD_MODE); /* 0x5C0B8 */
        pPad->nOriginX = 0; /* 0x5C0C8 */
        pPad->nOriginY = 0; /* 0x5C0D4 */
    }
    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->x = 224;                 /* 0x5C0E0 */
        pEnv->pCloseCtl->y = 1;                   /* 0x5C0EC */
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseClick; /* 0x5C0F8 */
    }

    const i32 nPending = *pEnv->pnPendingTotal;   /* 0x5C11C */
    i32 nSum = 0;
    for (i32 i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        nSum += pEnv->anCountryScore[i];             /* 0x5C100..0x5C120 */
    }
    const i32 nDiv = nSum + 1;                    /* 0x5C128 */
    const i32 nBonus = (nPending / nDiv) * pEnv->anCountryScore[PUFO_FUNDING_MULT_SLOT]; /* 0x5C148 */
    const i32 nTotal = nPending + nBonus;         /* 0x5C154 */
    *pEnv->pnPendingTotal = nTotal;               /* 0x5C15C */
    const i32 nShare = nTotal / nDiv;             /* 0x5C164 */

    for (i32 i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        pEnv->anShare[i] = pEnv->anCountryScore[i] * nShare; /* 0x5C168..0x5C1AC */
    }

    /* 0x5C1BC..0x5C220: every base whose region feeds a slot adds three shares to that slot. The
     * count is read once as a SIGNED byte (LDRSB at 0x5C188, BLE at 0x5C1B0), so a negative count
     * runs nothing at all. */
    for (i32 b = 0; b < (i32)pEnv->nBaseCount; ++b) {
        const BaseRec *const pBase = pEnv->apBases[b];
        if (pBase == NULL) {
            continue; /* deviation: the original dereferences every slot */
        }
        const u32 bRegion = pBase->bRegionId;           /* BaseRec.bRegionId @ +0x11, 0x5C1C8 LDRB #0x11 */
        if (bRegion < PUFO_REGION_ID_FIRST || bRegion >= PUFO_REGION_ID_FIRST + PUFO_REGION_ID_COUNT) {
            continue; /* 0x5C1CC..0x5C200: only 0x80..0x88 have an arm, and 0x86/0x87/0x89 none */
        }
        const i32 nSlot = g_anFundingSlotByRegion[bRegion - PUFO_REGION_ID_FIRST];
        if (nSlot >= 0) {
            pEnv->anShare[nSlot] += 3 * nShare;   /* 0x5C20C, 0x5C29C, 0x5C2B8, 0x5C2D4, 0x5C2E8 */
        }
    }

    const i32 nUpkeepClass = FundingUpkeepClass(pEnv->nMonthCounter);
    for (i32 nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
        const i32 nUpkeep = g_anFundingUpkeep[nUpkeepClass][nRow];
        pEnv->anBaseline[nRow] = nUpkeep;                          /* 0x5C33C..0x5C388 */
        pEnv->anShare[g_anFundingRowSlot[nRow]] += nUpkeep;        /* 0x5C34C..0x5C38C */
    }

    for (i32 nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
        const i32 nSlot = g_anFundingRowSlot[nRow];
        i32 nLevel = Funding_ScoreToBarLevel(nRow,
                         Funding_DeltaToArrow(pEnv->anShare[nSlot], pEnv->anBaseline[nRow]));
        pEnv->anArrow[nSlot] = nLevel;              /* 0x5C3A4, 0x5C3E8, 0x5C428, 0x5C468, 0x5C4A8 */
        if (pEnv->nMonthCounter < 3 && nLevel < 0) {
            nLevel = 0;                             /* 0x5C3AC..0x5C3BC and its four copies */
            pEnv->anArrow[nSlot] = 0;
        }
        pEnv->anBarTotal[nSlot] += nLevel;          /* 0x5C3CC, 0x5C40C, 0x5C44C, 0x5C48C, 0x5C4C8 */
    }

    for (i32 nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
        const i32 nSlot = g_anFundingRowSlot[nRow];
        if (pEnv->anBarTotal[nSlot] < 0) {
            pEnv->anBarTotal[nSlot] = 0;            /* 0x5C4D4..0x5C53C: a negative bar wipes its */
            pEnv->anArrow[nSlot] = 0;               /* arrow too, so the row shows no arrow */
        }
    }

    *pEnv->pnPendingTotal = 0;                      /* 0x5C544 */
}

/* FundingScreen_OpenMonthly @ 0x5d160. The month counter goes up BEFORE the recompute, so the first
 * report of a new game runs at month 1 and not at month 0 -- which matters, because month 0 is the
 * heaviest upkeep class rather than a no-upkeep case. nullsub_7 at 0x5D180 is an empty stub. */
void FundingScreen_OpenMonthly(FundingInitEnv *pEnv, i32 *pnScreenId, u8 *pbAnimFlag)
{
    if (pEnv == NULL) {
        return;
    }
    pEnv->nMonthCounter += 1;                       /* 0x5D170 */
    if (pnScreenId != NULL) {
        *pnScreenId = 19;                           /* 0x5D18C: Geoscape_CloseUI's case 19 */
    }
    FundingScreen_Init(pEnv);                       /* 0x5D194 */
    if (pbAnimFlag != NULL) {
        *pbAnimFlag = 0;                            /* 0x5D1A0: bUiAnimStarted[0] */
    }
}

/* The funding screen's five country names in slot order -- the order its own names panel copies
 * them in (0x5AF5C..0x5B038), which is what pins the slots: 0 USA, 1 Australia, 2 Euro, 3 Asia,
 * 4 Africa. */
const i32 g_anFundingSlotText[PUFO_FUND_ROW_COUNT] = { 1302, 1305, 1303, 1304, 1306 };

static void FundingFreePanel(UiPanel **ppPanel)
{
    UiPanel *pPanel;

    if (ppPanel == NULL) {
        return;
    }
    pPanel = *ppPanel;
    if (pPanel != NULL) {
        UiPanel_FreeLines(pPanel);                  /* 0x5B188 */
        free(pPanel);                               /* 0x5B190: operator delete */
        *ppPanel = NULL;
    }
}

/* One of the funding screen's three panels: operator new(0x1BB4) + UiPanel_ctor(5) plus the field
 * setup all three share. The names panel keeps the ctor's bFlag7072 = 1 and bFlag7074 = 0; the
 * bars and arrows panels clear bFlag7072 and set bFlag7074. */
static UiPanel *FundingNewPanel(i32 nPosX, u8 b0, u8 b1, u8 b2,
                                u8 bFlag7072, u8 bFlag7074, u8 bUnk7089)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel)); /* 0x5AC60 */

    if (pPanel == NULL || !UiPanel_ctor(pPanel, PUFO_FUND_PANEL_LINES)) { /* 0x5AC70 */
        free(pPanel);
        return NULL;
    }
    pPanel->nStyle = 5;                             /* 0x5AC98 */
    pPanel->nFont = 16;                             /* 0x5ACAC */
    pPanel->nPosX = nPosX;                          /* 0x5ACC0 */
    pPanel->nPosY = 70;                             /* 0x5ACD4 */
    UiPanel_SetBytes0to2(pPanel, b0, b1, b2);       /* 0x5AD14 */
    pPanel->bUnk7088 = 0x96;                        /* 0x5AD28, written as -106 */
    UiPanel_SetBytes6to11(pPanel, 0xC8, 0xB4, 0xF0, 0x14, 0x14, 0x14); /* 0x5AD4C */
    UiPanel_SetBytes3to5(pPanel, 0x32, 0x32, 0xFA); /* 0x5AD60 */
    pPanel->bFlag7073 = 0;                          /* 0x5AD70 */
    pPanel->bFlag7072 = bFlag7072;
    pPanel->bFlag7074 = bFlag7074;
    pPanel->bUnk7089 = bUnk7089;
    return pPanel;
}

/* FundingScreen_Toggle @ 0x5abe4. Open it and it builds the pad and the three panels; close it and
 * it frees exactly those. See the header for the field-by-field evidence. */
void FundingScreen_Toggle(FundingToggleEnv *pEnv)
{
    UiPanel *pNames = NULL;
    UiPanel *pBars = NULL;
    UiPanel *pArrows = NULL;
    i32 i;

    if (pEnv == NULL || pEnv->pbOpen == NULL || pEnv->pnTotal == NULL) {
        return;
    }

    if (*pEnv->pbOpen != 0) {
        *pEnv->pbOpen = 0;                          /* 0x5B170 */
        FundingFreePanel(pEnv->ppPanelNames);       /* 0x5B17C..0x5B190 */
        FundingFreePanel(pEnv->ppPanelBars);        /* 0x5B19C..0x5B1B0 */
        FundingFreePanel(pEnv->ppPanelArrows);      /* 0x5B1BC..0x5B1D0 */
        if (pEnv->ppPad != NULL && *pEnv->ppPad != NULL) {
            PadImage_FreeData(*pEnv->ppPad);        /* 0x5B1E8 */
            free(*pEnv->ppPad);                     /* 0x5B1F0 */
            *pEnv->ppPad = NULL;
        }
        return;
    }

    *pEnv->pbOpen = 1;                              /* 0x5AC00 */
    *pEnv->pnTotal = 0;                             /* 0x5AC0C */

    if (pEnv->ppPad != NULL) {
        PadImage *const pPad = BuySellPadNew();     /* 0x5AC14..0x5AC20 */

        *pEnv->ppPad = pPad;                        /* 0x5AC40 */
        if (pPad != NULL) {
            (void)PadImage_Load(pPad, PUFO_FUNDING_PAD_NAME, PUFO_FUNDING_PAD_MODE); /* 0x5AC44 */
            pPad->nOriginX = 0;                     /* 0x5AC4C */
            pPad->nOriginY = 0;                     /* 0x5AC54 */
        }
    }

    if (pEnv->ppPanelNames != NULL) {
        pNames = FundingNewPanel(10, 0x64, 0x96, 0x64, 1, 0, 0);
        *pEnv->ppPanelNames = pNames;               /* 0x5AC90 */
        if (pNames != NULL) {
            pNames->nUnk7052 = 5;                   /* 0x5ACDC */
            pNames->nUnk7056 = 70;                  /* 0x5ACF0 */
            pNames->nViewW = 230;                   /* 0x5AD04 */
            for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
                if (pEnv->pTextRecs == NULL) {
                    break; /* deviation: the original reads aTextRecs unconditionally */
                }
                (void)UiPanel_SetLineText(pNames, i, (const char *)(pEnv->pTextRecs
                                  + (size_t)PUFO_TEXT_REC_STRIDE * g_anFundingSlotText[i]));
            }
        }
    }

    if (pEnv->ppPanelBars != NULL) {
        pBars = FundingNewPanel(100, 0xB4, 0xB4, 0xB4, 0, 1, 0);
        *pEnv->ppPanelBars = pBars;                 /* 0x5ADA8 */
        if (pBars != NULL && pEnv->anBar != NULL) {
            for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
                pBars->anLineAux[i] = pEnv->anBar[i]; /* 0x5B058..0x5B0A0 */
            }
        }
    }

    if (pEnv->ppPanelArrows != NULL) {
        pArrows = FundingNewPanel(170, 0xB4, 0xB4, 0xB4, 0, 1, 1);
        *pEnv->ppPanelArrows = pArrows;             /* 0x5AE88 */
        if (pArrows != NULL && pEnv->anLevel != NULL) {
            for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
                pArrows->anLineAux[i] = pEnv->anLevel[i]; /* 0x5B0AC..0x5B0E8 */
            }
        }
    }

    if (pBars == NULL) {
        return; /* deviation: the original walks the bars panel whatever the allocation did */
    }

    /* 0x5B0EC..0x5B11C: a bar that is not positive shows no bar and no arrow. */
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        if (pBars->anLineAux[i] <= 0) {
            pBars->anLineAux[i] = 0;
            if (pArrows != NULL) {
                pArrows->anLineAux[i] = 0;
            }
        }
    }

    /* 0x5B124..0x5B144: the "$" line the draw prints is the sum of the five bars. */
    for (i = 0; i < PUFO_FUND_ROW_COUNT; ++i) {
        *pEnv->pnTotal += pBars->anLineAux[i];
    }
}

/* One of the funding screen's three TEXTS strings, copied into the draw's scratch buffer. The
 * original copies byte by byte with no bound into a 104-byte buffer; the records here are short
 * and the port bounds the copy anyway. */
static void FundingCopyTextRec(char *pszDst, size_t cchDst, const u8 *pTextRecs, i32 nRec)
{
    if (pTextRecs == NULL || cchDst == 0) {
        if (cchDst != 0) {
            pszDst[0] = '\0';
        }
        return; /* deviation: the original reads aTextRecs unconditionally */
    }
    pufo_strncpy(pszDst, (const char *)(pTextRecs + (size_t)PUFO_TEXT_REC_STRIDE * nRec), cchDst);
}

/* FundingScreen_Open @ 0x5b560. */
UiControl *FundingScreen_Open(FundingToggleEnv *pToggle, const FundingOpenEnv *pEnv)
{
    FundingScreen_Toggle(pToggle);                        /* 0x5B564 */
    if (pEnv == NULL || pEnv->pCloseCtl == NULL) {
        return NULL;
    }
    pEnv->pCloseCtl->x = 224;                             /* 0x5B574 */
    pEnv->pCloseCtl->y = 1;                               /* 0x5B580 */
    pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseClick;      /* 0x5B58C */
    return pEnv->pCloseCtl;                               /* 0x5B590 */
}

/* FundingScreen_Draw @ 0x5b1fc. Nothing is drawn while the screen is closed. */
void FundingScreen_Draw(const FundingDrawEnv *pEnv)
{
    char szLabel[PUFO_FUND_LABEL_MAX];                    /* 0x5B1FC: char psz[104] */
    char szNum[100];                                      /* 0x5B4DC: var_64, the itoa scratch */
    i32 nWidth;

    if (pEnv == NULL || pEnv->pbOpen == NULL || *pEnv->pbOpen == 0) {
        return;                                           /* 0x5B210 */
    }

    if (pEnv->pPad != NULL) {
        PadImage_Blit(pEnv->pPad);                        /* 0x5B218 */
    }
    BaseUi_DrawFrameRails();                              /* 0x5B220..0x5B3AC */
    (void)UiControl_BlitActivePad(pEnv->pCloseCtl, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser); /* 0x5B3C0 */

    /* 0x5B3D8..0x5B440: "Инвестирование стран", centred at y 20. */
    FundingCopyTextRec(szLabel, sizeof(szLabel), pEnv->pTextRecs, PUFO_FUND_TEXT_TITLE);
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 210, 190, 250, 20, 20, 20); /* 0x5B414 */
    nWidth = Text_MeasureWidth(szLabel);                  /* 0x5B428 */
    Text_DrawString(120 - nWidth / 2, 20, szLabel);       /* 0x5B440 */

    /* 0x5B454..0x5B480: "Страна Инвест. изм. $", the caption over the three columns. */
    FundingCopyTextRec(szLabel, sizeof(szLabel), pEnv->pTextRecs, PUFO_FUND_TEXT_COLUMNS);
    Text_DrawString(10, 50, szLabel);                     /* 0x5B480 */

    /* 0x5B48C..0x5B4A4: names, bars, arrows. */
    if (pEnv->pPanelNames != NULL) {
        UiPanel_DrawText(pEnv->pPanelNames);
    }
    if (pEnv->pPanelBars != NULL) {
        UiPanel_DrawText(pEnv->pPanelBars);
    }
    if (pEnv->pPanelArrows != NULL) {
        UiPanel_DrawText(pEnv->pPanelArrows);
    }

    /* 0x5B4B4..0x5B52C: "Итого: " + the total + "$". The total is whatever the toggle built; this
     * draw never recomputes it. */
    FundingCopyTextRec(szLabel, sizeof(szLabel), pEnv->pTextRecs, PUFO_FUND_TEXT_TOTAL);
    if (pEnv->pnTotal != NULL) {
        (void)snprintf(szNum, sizeof(szNum), "%d", (int)*pEnv->pnTotal); /* 0x5B4E0: j_itoa */
        (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));              /* 0x5B4EC */
    }
    (void)pufo_strcat(szLabel, "$", sizeof(szLabel));     /* 0x5B4F8: asc_AB1D8 */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 180, 240, 20, 20, 20);   /* 0x5B518 */
    Text_DrawString(10, 160, szLabel);                    /* 0x5B52C */
}

/* The monthly report's commentary, TEXTS[1307..1333], as [arrow + 2][group][line]. Group 1 is
 * rows 1 and 2 -- Euro and Asia, which the original words separately (0x5BA34, 0x5BB80, 0x5BD70,
 * 0x5BEA4); group 0 is rows 0, 3 and 4. */
static const i32 g_anFundingCommentary[5][2][2] = {
    { { 1309, 1310 }, { 1307, 1308 } }, /* arrow -2 */
    { { 1315, 1316 }, { 1313, 1314 } }, /* arrow -1 */
    { { 1321, 1322 }, { 1319, 1320 } }, /* arrow  0 */
    { { 1326, 1327 }, { 1324, 1325 } }, /* arrow +1 */
    { { 1331, 1332 }, { 1329, 1330 } }  /* arrow +2 */
};

/* The third line of each row. Only the two negative arrows have a "this country is in the red"
 * variant (0x5BF60, 0x5BE34); 0, +1 and +2 use one line whichever way the flag falls. */
static const i32 g_anFundingCommentaryTail[5][2] = {
    { 1312, 1311 }, /* arrow -2: not red / red */
    { 1318, 1317 }, /* arrow -1: not red / red */
    { 1323, 1323 }, /* arrow  0 */
    { 1328, 1328 }, /* arrow +1 */
    { 1333, 1333 }  /* arrow +2 */
};

/* FundingScreen_DrawExtended @ 0x5b59c. The monthly report: five rows of title plus three
 * commentary lines, re-derived from the funding arrays. See the header for the branch map. */
void FundingScreen_DrawExtended(const FundingDrawEnv *pEnv)
{
    char aTitle[PUFO_FUND_ROW_COUNT][100]; /* 0x5B908..: v100..v104, 100 bytes each */
    char szHead[200];                      /* 0x5B59C: unsigned char psz[200] */
    char szLine1[200], szLine2[200], szLine3[200]; /* v98, v99 and the reused psz */
    i8 anArrow[PUFO_FUND_ROW_COUNT];
    u8 abRed[PUFO_FUND_ROW_COUNT];
    i32 nRow;
    i32 nWidth;

    if (pEnv == NULL || pEnv->anShare == NULL || pEnv->anBaseline == NULL || pEnv->anBar == NULL) {
        return; /* deviation: the original reads the six globals unconditionally */
    }

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 240, 240, 240, 20, 20, 20); /* 0x5B5C8, before the pad */
    if (pEnv->pPad != NULL) {
        PadImage_Blit(pEnv->pPad);                     /* 0x5B5D4 */
    }
    BaseUi_DrawFrameRails();                           /* 0x5B5DC..0x5B768 */
    (void)UiControl_BlitActivePad(pEnv->pCloseCtl, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser); /* 0x5B77C */

    FundingCopyTextRec(szHead, sizeof(szHead), pEnv->pTextRecs, PUFO_FUND_TEXT_MONTHLY);
    nWidth = Text_MeasureWidth(szHead);                /* 0x5B7BC */
    Text_DrawString(120 - nWidth / 2, 5, szHead);      /* 0x5B7D4 */

    /* 0x5B7E8..0x5B8C0: each row's arrow, and whether its bar says the country is in the red. */
    for (nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
        const i32 nSlot = g_anFundingRowSlot[nRow];
        const i32 nRaw = Funding_DeltaToArrow(pEnv->anShare[nSlot], pEnv->anBaseline[nRow]);

        anArrow[nRow] = (i8)nRaw;                      /* v96[m] = (char)v7, taken before the bar */
        abRed[nRow] = (((nRaw & 0x80) != 0) && pEnv->anBar[nSlot] < 0) ? 1 : 0;
    }

    /* 0x5B8CC..0x5B8F0: the first two months never show a falling country. */
    if (pEnv->nMonthCounter < 3) {
        for (nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
            if (anArrow[nRow] < 0) {
                anArrow[nRow] = 0;
            }
        }
    }

    /* 0x5B908..0x5B9D4: the five row titles, in report order. */
    for (nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
        FundingCopyTextRec(aTitle[nRow], sizeof(aTitle[nRow]), pEnv->pTextRecs,
                           PUFO_FUND_TEXT_ROW0 + nRow);
    }

    /* 0x5B9E0..0x5C058: the five rows. */
    for (nRow = 0; nRow < PUFO_FUND_ROW_COUNT; ++nRow) {
        const i8 nArrow = anArrow[nRow];
        const i32 nGroup = (nRow == 1 || nRow == 2) ? 1 : 0;

        if (nArrow >= -2 && nArrow <= 2) {
            const i32 nIdx = nArrow + 2;

            FundingCopyTextRec(szLine1, sizeof(szLine1), pEnv->pTextRecs,
                               g_anFundingCommentary[nIdx][nGroup][0]);
            FundingCopyTextRec(szLine2, sizeof(szLine2), pEnv->pTextRecs,
                               g_anFundingCommentary[nIdx][nGroup][1]);
            FundingCopyTextRec(szLine3, sizeof(szLine3), pEnv->pTextRecs,
                               g_anFundingCommentaryTail[nIdx][abRed[nRow] != 0 ? 1 : 0]);
        }
        /* An arrow outside -2..2 copies nothing: the original leaves the three buffers holding the
         * previous row's text and draws those again. */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 40, 240, 40, 20, 20, 20);   /* 0x5BFC8 */
        Text_DrawString(5, 60 * nRow + 20, aTitle[nRow]);                  /* 0x5BFF8 */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 180, 180, 240, 20, 20, 20); /* 0x5C018 */
        Text_DrawString(5, 60 * nRow + 35, szLine1);                       /* 0x5C02C */
        Text_DrawString(5, 60 * nRow + 45, szLine2);                       /* 0x5C040 */
        Text_DrawString(5, 60 * nRow + 55, szLine3);                       /* 0x5C058 */
    }
}

/* ResearchScreen_StartSelected @ 0x73240. Allocation turns into running work only when the topic
 * is idle and this base has actually allocated something to it. */
i32 ResearchScreen_StartSelected(const ResearchStartEnv *pEnv)
{
    const i32 nIdx = ResearchQtyIndex(&pEnv->qty);
    if (nIdx < 0 || pEnv->aTopics == NULL) {
        return 0;
    }

    if (pEnv->pbTopicSelected != NULL) {
        *pEnv->pbTopicSelected = 0; /* 0x7324C */
    }
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pSampleUser); /* 0x73258 */
    }

    const i32 nBase = (pEnv->qty.pnSelectedBaseIdx != NULL) ? (i32)*pEnv->qty.pnSelectedBaseIdx : 0;
    const i32 nTopic = nIdx - PUFO_RESEARCH_TOPIC_COUNT * nBase;
    const i32 nEntries = (pEnv->qty.puResearchEntries != NULL) ? (i32)*pEnv->qty.puResearchEntries : 0;
    const u16 *pResearchCol = pEnv->qty.apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase;

    if (pEnv->aTopics[nTopic].wProgress == 0 && pEnv->qty.apResearchCols[nIdx] != 0) {
        pEnv->aTopics[nTopic].wProgress = 1; /* 0x732C8 tests, 0x732D8 stores */
    }

    (void)ResearchScreen_RefreshPanels(pEnv->aTopics, nEntries, pResearchCol,
                                       pEnv->qty.pTopicPanel, pEnv->pPanelQty,
                                       pEnv->pPanelProgress); /* 0x732DC */

    if (pEnv->qty.abFacilityGrid != NULL) {
        const i32 nActiveManuf = (pEnv->apManufCols != NULL)
                                     ? Base_CountActiveManuf(pEnv->apManufCols +
                                                             PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                                             nEntries)
                                     : 0;
        (void)Base_UpdateWorkshopNeeded(pEnv->qty.abFacilityGrid, nBase, nActiveManuf); /* 0x732F0 */
        (void)Base_UpdateLabNeeded(pEnv->qty.abFacilityGrid, nBase,
                                   Base_CountActiveResearch(pResearchCol, nEntries)); /* 0x73304 */
    }

    if (pEnv->pfnRebuild != NULL) {
        return pEnv->pfnRebuild(pEnv->pRebuildUser); /* 0x73320: tail call */
    }
    return 0;
}

/* Base_EnterBuildMode @ 0x84e20. */
void Base_EnterBuildMode(const BaseBuildModeEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, pEnv->pSampleUser); /* 0x84E2C */
    }

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->puResearchEntries != NULL) ? (i32)*pEnv->puResearchEntries : 0;

    /* Deviation: the original feeds nSelectedBaseIdx straight through, so an index outside 0..7
     * reads auManufCol/auResearchCol outside the eight-block array and, for base 8, writes the
     * needed grid past its 1524 bytes. The port refuses the pair the same way ResearchQtyIndex
     * does; the sample, the screen id, the nav rebuild and the parked control are unaffected. */
    if (pEnv->abFacilityGrid != NULL && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT) {
        const i32 nActiveManuf = (pEnv->apManufCols != NULL)
                                     ? Base_CountActiveManuf(pEnv->apManufCols +
                                                             PUFO_PRODUCT_ENTRIES_PER_BASE * nBase,
                                                             nEntries)
                                     : 0;
        (void)Base_UpdateWorkshopNeeded(pEnv->abFacilityGrid, nBase, nActiveManuf); /* 0x84E44 */
        const i32 nActiveResearch = (pEnv->apResearchCols != NULL)
                                        ? Base_CountActiveResearch(pEnv->apResearchCols +
                                                                   PUFO_RESEARCH_TOPIC_COUNT * nBase,
                                                                   nEntries)
                                        : 0;
        (void)Base_UpdateLabNeeded(pEnv->abFacilityGrid, nBase, nActiveResearch); /* 0x84E58 */
    }

    if (pEnv->pnScreenId != NULL) {
        *pEnv->pnScreenId = 1; /* 0x84E64 */
    }
    (void)Geoscape_BuildNavControls(pEnv->pNavOut, pEnv->pNavEnv); /* 0x84E6C */

    if (pEnv->pParkCtl != NULL) {
        pEnv->pParkCtl->x = 1000; /* 0x84E80 */
        pEnv->pParkCtl->y = 1000; /* 0x84E88 */
    }
}

/* Smap_SetWaypoint @ 0x85440. Colour slot 0 of message line 0 carries the ship index. */
void Smap_SetWaypoint(const SmapWaypointEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }

    const i32 nShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x85460 */
    if (pEnv->pnMsgShipIdx != NULL) {
        *pEnv->pnMsgShipIdx = nShipIdx; /* 0x85460 */
    }
    if (pEnv->pnSelectedShipIdx != NULL) {
        *pEnv->pnSelectedShipIdx = nShipIdx; /* 0x85468 */
    }

    (void)Ship_ClearWaypointIntercept(pEnv->pShipEnv, nShipIdx); /* 0x85470 */
    UiLineList_Remove(pEnv->pLines, 0); /* 0x8547C */

    /* 0x85490 reloads the ship index with LDRB from 0x187054, and UiPanel_InsertLine masks its
     * slot-0 argument with AND R3,R3,#0xFF (0x5A2F4) and byte-loads slots 1..3 from the stack.
     * Every colour slot this call can write is therefore a byte; the cast is the original's LDRB. */
    UiLineList_Insert(pEnv->pLines, 0, 11, (u32)(u8)nShipIdx, 0, 0, 0); /* 0x854A0 */

    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 1; /* 0x854B0: GameTime_Pause is STRB #1 at 0x15961C */
    }
}

/* Smap_CancelTarget @ 0x84254. The target index comes from colour SLOT 1 of message line 0, not
 * slot 0 -- Smap_SetWaypoint above writes the ship index into slot 0 of the same line. */
void Smap_CancelTarget(const SmapCancelTargetEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }

    const i32 nStrategIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 1); /* 0x84268 */
    const i32 nShipIdx = (pEnv->pnSelectedShipIdx != NULL) ? *pEnv->pnSelectedShipIdx : 0;

    (void)Ship_LaunchIntercept(pEnv->pShipEnv, nShipIdx, nStrategIdx); /* 0x8427C */

    if (pEnv->pbSelectionFlag != NULL) {
        *pEnv->pbSelectionFlag = 0; /* 0x84284, bMapSelectionFlag */
    }
    if (pEnv->pbFlag187098 != NULL) {
        *pEnv->pbFlag187098 = 0; /* 0x84290 */
    }

    UiLineList_Remove(pEnv->pLines, 0); /* 0x8429C */

    /* 0x842A4 reads LDRB [R4,#0xD48] with R4 = 0x158890: the root panel's line-list count byte,
     * read AFTER the removal. The clock is un-paused only when no message line is left. */
    if (pEnv->pLines->nLineCount == 0 && pEnv->pTime != NULL) {
        GameTime_Unpause(pEnv->pTime); /* 0x842B4 */
    }

    if (pEnv->pbFlag18708C != NULL) {
        *pEnv->pbFlag18708C = 0; /* 0x842BC */
    }
    if (pEnv->pbFlag187090 != NULL) {
        *pEnv->pbFlag187090 = 0; /* 0x842C8 */
    }
}

/* The two "keep the course" answers. Both read the ship index out of colour SLOT 0 of message line 0
 * and both end on Smap_ClearSelection; the dismiss half of the pair lives in game/pufo_strateg.c. */
void Smap_ConfirmSelected(const SmapSelectedEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }
    const i32 nShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x845EC */
    (void)Ship_ClearWaypointIntercept(pEnv->pShipEnv, nShipIdx);       /* 0x845FC: keep the course */
    (void)Smap_ClearSelection(pEnv->pLines, pEnv->pTime, pEnv->pbSelectionFlag); /* 0x84604 */
}

/* The same body with the tail reordered: the selection flag goes first at 0x85420 and the
 * line-count test after it at 0x8542C, where the other two have them the other way round. */
void Smap_ConfirmSelectedAlt(const SmapSelectedEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }
    const i32 nShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x853FC */
    (void)Ship_ClearWaypointIntercept(pEnv->pShipEnv, nShipIdx);       /* 0x8540C */
    UiLineList_Remove(pEnv->pLines, 0);                                /* 0x85414 */
    if (pEnv->pbSelectionFlag != NULL) {
        *pEnv->pbSelectionFlag = 0; /* 0x85420, BEFORE the count test */
    }
    if (pEnv->pLines->nLineCount == 0 && pEnv->pTime != NULL) {
        GameTime_Unpause(pEnv->pTime); /* 0x8542C */
    }
}

/* Smap_ConfirmCraftTarget @ 0x842ec. Replace the waypoint a craft is chasing with a new one at the
 * pending click and send the craft to it. */
void Smap_ConfirmCraftTarget(const SmapConfirmTargetEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL || pEnv->pStrategList == NULL) {
        return;
    }

    const i32 nShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x842FC */
    if (pEnv->pnSelectedShipIdx != NULL) {
        *pEnv->pnSelectedShipIdx = nShipIdx; /* 0x84310 */
    }

    /* 0x84318..0x84374. The bIntercepting clear is INSIDE the type-5 branch: a craft chasing a real
     * contact keeps its intercept flag here. */
    const ShipRemovalEnv *pRem = pEnv->pShipEnv;
    if (pRem != NULL && pRem->apCraft != NULL && pRem->pnShipCount != NULL &&
        nShipIdx >= 0 && nShipIdx < (i32)*pRem->pnShipCount) {
        Ship *pShip = pRem->apCraft[nShipIdx];
        if (pShip != NULL && pShip->bIntercepting != 0) {
            const StrategItem *pWaypoint =
                (const StrategItem *)PtrArray_Get(pEnv->pStrategList, pShip->nStrategIdx);
            if (pWaypoint != NULL && (i8)pWaypoint->nType == 5) { /* 0x84348 */
                (void)StrategList_RemoveAt(pEnv->pStrategList, pRem->pWorld, pShip->nStrategIdx,
                                           pRem->pnSpawnOnceFlag, pRem->pSpawnCtx);
                pShip->bIntercepting = 0; /* 0x84374 */
            }
        }
    }

    /* 0x8437C..0x843C8: the new waypoint. nId -1 and bCraftClass 0 are what a type-5 item gets;
     * StrategList_AddItem scales x and y out of the viewport's current zoom mode. */
    if (pEnv->pnPickX != NULL && pEnv->pnPickY != NULL && pEnv->pView != NULL) {
        (void)StrategList_AddItem(pEnv->pStrategList, pEnv->pnSeed, pEnv->pView->nMode,
                                  5, 1, 0, *pEnv->pnPickX, *pEnv->pnPickY, -1, 1);
    }

    const i32 nStrategIdx = PtrArray_GetCount(pEnv->pStrategList) - 1; /* 0x843D0 */
    const i32 nShipIdx2 = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x843E0, read a second time */
    (void)Ship_LaunchIntercept(pEnv->pShipEnv, nShipIdx2, nStrategIdx); /* 0x843D8 */

    UiLineList_Remove(pEnv->pLines, 0); /* 0x843E4 */
    if (pEnv->pLines->nLineCount == 0 && pEnv->pTime != NULL) {
        GameTime_Unpause(pEnv->pTime); /* 0x843EC */
    }
    if (pEnv->pbSelectionFlag != NULL) {
        *pEnv->pbSelectionFlag = 0; /* 0x843F8 */
    }
}

/* Smap_SelectMapTarget @ 0x84484. The arm that PAUSES the geoscape and puts a target on the pending
 * order; the header block has the six steps and the fourth read at the tail. */
void Smap_SelectMapTarget(const SmapSelectTargetEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }

    if (pEnv->pbSelectionFlag != NULL) {
        *pEnv->pbSelectionFlag = 1; /* 0x84498 */
    }

    const i32 nShipIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x844A4 */
    if (pEnv->pnSelectedShipIdx != NULL) {
        *pEnv->pnSelectedShipIdx = nShipIdx; /* 0x844C4 */
    }

    const ShipRemovalEnv *pRem = pEnv->pShipEnv;
    Ship *pShip = NULL;
    if (pRem != NULL && pRem->apCraft != NULL && pRem->pnShipCount != NULL
        && nShipIdx >= 0 && nShipIdx < (i32)*pRem->pnShipCount) {
        pShip = pRem->apCraft[nShipIdx];
    }

    if (pShip != NULL && pShip->bIntercepting != 0) { /* 0x844D0 */
        const StrategItem *pTarget =
            (const StrategItem *)PtrArray_Get(pEnv->pStrategList, pShip->nStrategIdx); /* 0x844E4 */
        if (pTarget != NULL && (i8)pTarget->nType == 5) { /* 0x844F0 */
            (void)StrategList_RemoveAt(pEnv->pStrategList, pRem->pWorld, pShip->nStrategIdx,
                                       pRem->pnSpawnOnceFlag, pRem->pSpawnCtx); /* 0x84504 */
            pShip->bIntercepting = 0; /* 0x84514, inside the type-5 branch */
        }
    }

    /* 0x84520..0x84544: the ship is re-fetched through the stored dword here, the way 0x844E8 and
     * 0x84518 re-read 0x187054 instead of keeping a register. */
    const i32 nStored = (pEnv->pnSelectedShipIdx != NULL) ? *pEnv->pnSelectedShipIdx : nShipIdx;
    Ship *pArmShip = pShip;
    if (pRem != NULL && pRem->apCraft != NULL && pRem->pnShipCount != NULL
        && nStored >= 0 && nStored < (i32)*pRem->pnShipCount) {
        pArmShip = pRem->apCraft[nStored];
    }
    if (pEnv->aTopics != NULL && pEnv->pbFinalMissionCtl != NULL && pArmShip != NULL
        && pEnv->aTopics[PUFO_RESEARCH_TOPIC_CYDONIA].bUnlocked != 0
        && pArmShip->bCraftType == 2) { /* 0x8453C: the Avenger */
        *pEnv->pbFinalMissionCtl = 1; /* 0x84544 */
    }

    const u32 uColour = UiLineList_GetColor(pEnv->pLines, 0, 0); /* 0x84554, the third read */
    UiLineList_Insert(pEnv->pLines, 1, 27, uColour, 0u, 0u, 0u); /* 0x84574 */
    UiLineList_Remove(pEnv->pLines, 0);                          /* 0x84580 */
    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 1; /* GameTime_Pause @ 0x5d6ac, called unconditionally at 0x84588 */
    }

    /* 0x84598: the fourth read, after the insert and the removal. Line 0 is now the flag-27 line the
     * insert just put there, so both slots receive the value the insert carried. */
    const i32 nAfter = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0);
    if (pEnv->pnSelectedShipIdx != NULL) {
        *pEnv->pnSelectedShipIdx = nAfter; /* 0x845A0 */
    }
    if (pEnv->pnSlot159E74 != NULL) {
        *pEnv->pnSlot159E74 = nAfter; /* 0x845A4 */
    }
}

/* Smap_ResolveInterceptUI @ 0x84964. The flag standing on message line 0 decides what the pending
 * map order becomes; the header block has the four steps and the tail-removal bug. */
void Smap_ResolveInterceptUI(const SmapResolveInterceptEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }

    GameTime_Set5Sec(pEnv->pWorld, pEnv->apSpeedCtl);                      /* 0x84968 */
    if (pEnv->pbInterceptDone != NULL) {
        *pEnv->pbInterceptDone = 0;                                        /* 0x84984 */
    }

    if (UiLineList_GetFlag(pEnv->pLines, 0) == 27) {                       /* 0x84988 */
        (void)Ship_RetargetTravel(pEnv->pShipEnv,
                                  (i32)UiLineList_GetColor(pEnv->pLines, 0, 0)); /* 0x849AC */
    }

    if (UiLineList_GetFlag(pEnv->pLines, 0) == 13) {                /* 0x849B4, the flag read again */
        const u32 nSlot1 = UiLineList_GetColor(pEnv->pLines, 0, 1) & PUFO_LINE_COLOR_MAX; /* 0x849DC */
        UiLineList_Insert(pEnv->pLines, 1, 11, nSlot1, 0u, 0u, 0u);        /* 0x849F0 */
        UiLineList_Remove(pEnv->pLines, 0);                                /* 0x849FC */
        if (pEnv->pbInterceptDone != NULL) {
            *pEnv->pbInterceptDone = 1;                                    /* 0x84A04 */
        }
        if (pEnv->pTime != NULL) {
            pEnv->pTime->bPaused = 1; /* GameTime_Pause @ 0x84A0C */
        }
        return;                                /* 0x84A10 branches to 0x84A9C, past the clear below */
    }

    if (pEnv->pbFlag187090 != NULL)    { *pEnv->pbFlag187090 = 0; }        /* 0x84A24 */
    if (pEnv->pbFlag18708C != NULL)    { *pEnv->pbFlag18708C = 0; }        /* 0x84A2C */
    if (pEnv->pbSelectionFlag != NULL) { *pEnv->pbSelectionFlag = 0; }     /* 0x84A34 */
    if (pEnv->pbFlag187098 != NULL)    { *pEnv->pbFlag187098 = 0; }        /* 0x84A3C */

    /* Each dismiss removes line 0 itself, so the next test reads the line that was underneath. */
    if (UiLineList_GetFlag(pEnv->pLines, 0) == 23) {                       /* 0x84A44 */
        (void)Smap_DismissSelected(pEnv->pShipEnv, pEnv->pLines, pEnv->pTime, pEnv->pbSelectionFlag);
    }
    if (UiLineList_GetFlag(pEnv->pLines, 0) == 21) {                       /* 0x84A58 */
        (void)Smap_DismissSelected(pEnv->pShipEnv, pEnv->pLines, pEnv->pTime, pEnv->pbSelectionFlag);
    }
    if (UiLineList_GetFlag(pEnv->pLines, 0) == 22) {                       /* 0x84A6C */
        (void)Smap_DismissSelected(pEnv->pShipEnv, pEnv->pLines, pEnv->pTime, pEnv->pbSelectionFlag);
    }

    /* ORIGINAL BUG (0x84A80), reproduced: the removal is unconditional, so after a dismiss already
     * dropped line 0 this one eats the NEXT message line. */
    UiLineList_Remove(pEnv->pLines, 0);
    if (pEnv->pLines->nLineCount == 0 && pEnv->pTime != NULL) {
        GameTime_Unpause(pEnv->pTime); /* 0x84A84 reads the count byte after the removal */
    }
    if (pEnv->pbFinalMissionCtl != NULL) {
        *pEnv->pbFinalMissionCtl = 0;                                        /* 0x84A94 */
    }
}

/* The two zoom unscale factors: the exact reciprocals of the STRATEG_SCALE_MODE1/2 pair in
 * pufo_strateg.c, which is what StrategList_AddItem scales a position OUT with. Pool words
 * 0x3FE80189_5077D685 (1/1.333) and 0x3FE2E3E4_31916105 (1/1.694). */
#define SMAP_UNSCALE_MODE1 (1.0 / 1.333)
#define SMAP_UNSCALE_MODE2 (1.0 / 1.694)

/* Smap_OnPointerUp @ 0x84688. Either launch the intercept the message line offers, or recentre the
 * view on the strateg item it names and leave a crosshair for Geoscape_Draw to blit. */
void Smap_OnPointerUp(const SmapPointerUpEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pLines == NULL) {
        return;
    }

    if (pEnv->pbSelectionFlag != NULL && *pEnv->pbSelectionFlag != 0) {            /* 0x84698 */
        const i32 nStrategIdx = (i32)UiLineList_GetColor(pEnv->pLines, 0, 0);      /* 0x8491C */
        const i32 nShipIdx = (pEnv->pnSelectedShipIdx != NULL) ? *pEnv->pnSelectedShipIdx : -1;
        (void)Ship_LaunchIntercept(pEnv->pShipEnv, nShipIdx, nStrategIdx);         /* 0x84930 */
        *pEnv->pbSelectionFlag = 0;                                                /* 0x84938 */
    } else if (pEnv->pView != NULL && pEnv->pStrategList != NULL) {
        GameTime_Set5Sec(pEnv->pWorld, pEnv->apSpeedCtl);                          /* 0x8469C */

        /* 0x846B4..0x846D8: MOVS R3,Rn / ADDMI R3,R3,#1 / MOV R3,R3,ASR#1 (bytes 01 30 83 42).
         * ADDMI fires only when the tested value is NEGATIVE, so a non-negative extent is halved by
         * plain `n >> 1` and the +1 only restores C's round-toward-zero for a negative one. This is
         * C's `n / 2`, not a ceiling -- the same shape as 0x5EB44 and 0x822EC. */
        const i32 nHalfW = pEnv->pView->nMapW / 2 - 120;                                        /* 0x846C4 */
        const i32 nHalfH = pEnv->pView->nMapH / 2 - 160;                                        /* 0x846D8 */
        const StrategItem *pItem = (const StrategItem *)PtrArray_Get(pEnv->pStrategList,
                                     (i32)UiLineList_GetColor(pEnv->pLines, 0, 0)); /* 0x846DC */
        /* 0x84700 reads colour SLOT 0 a second time, so nX and nY come from the SAME item. */
        if (pItem != NULL) {
            i32 nX = pItem->nX;                                                    /* 0x846F4 */
            i32 nY = pItem->nY;                                                    /* 0x84714 */

            if (pEnv->pView->nMode == 1) {                                         /* 0x8471C */
                nX = (i32)((double)nX * SMAP_UNSCALE_MODE1);
                nY = (i32)((double)nY * SMAP_UNSCALE_MODE1);
            }
            if (pEnv->pView->nMode == 2) {                                         /* 0x847A4 */
                nX = (i32)((double)nX * SMAP_UNSCALE_MODE2);
                nY = (i32)((double)nY * SMAP_UNSCALE_MODE2);
            }

            i32 nScrollY = nY - nHalfH - 160;                                      /* 0x84830 */
            pEnv->pView->nScrollY = nScrollY;                                      /* 0x8483C */
            if (nY - 160 < 0) {                 /* 0x8484C RSBMI, on the N flag of the ADDS at 0x84838 */
                nScrollY = -nHalfH;
                pEnv->pView->nScrollY = nScrollY;                                  /* 0x84854 */
            }
            pEnv->pView->nScrollX = nX - nHalfW - 120;                             /* 0x84850 */

            const i32 uHeight = (pEnv->pStripPad != NULL)
                                    ? (i32)pEnv->pStripPad->uHeight : 0;           /* 0x84858 */
            if (nScrollY + nHalfH > uHeight + pEnv->pView->nMapH - 320) {         /* 0x84870 */
                nScrollY = uHeight - nHalfH + pEnv->pView->nMapH - 320;           /* 0x8487C */
                pEnv->pView->nScrollY = nScrollY;                                  /* 0x84884 */
            }

            i32 nCrossY = nY - nScrollY - nHalfH;                                  /* 0x84888 */
            /* ORIGINAL BUG (0x84894), reproduced: 0x84890 CMP R0,R1 / 0x84894 SUBGT subtracts nMapH
             * ONCE, so a value more than nMapH above the range stays out of range (a 100x100 view with
             * the target at (120, 120) lands the crosshair on 200, twice the map height). Unreachable
             * on the shipped 400x400 map, where the value tops out near 350. */
            if (nCrossY > pEnv->pView->nMapH) {
                nCrossY -= pEnv->pView->nMapH;                                     /* 0x84894 */
            }
            i32 nCrossX = 120;              /* 0x84860 / 0x84880: (nHalfW + 120) - nHalfW */
            if (nCrossX > pEnv->pView->nMapW) {
                nCrossX -= pEnv->pView->nMapW;                                     /* 0x848A0 */
            }

            if (pEnv->pnCrosshairX != NULL)     { *pEnv->pnCrosshairX = nCrossX; } /* 0x848A4 */
            if (pEnv->pnCrosshairY != NULL)     { *pEnv->pnCrosshairY = nCrossY; } /* 0x848AC */
            if (pEnv->pbCrosshairPending != NULL) { *pEnv->pbCrosshairPending = 1; } /* 0x848B8 */
            if (pEnv->pnCooldownR != NULL)      { *pEnv->pnCooldownR = 0; }        /* 0x848C4 */
        }
        /* Deviation: with no item at that index the original still runs the whole recentre on
         * whatever PtrArray_Get handed back; the port skips it and keeps the tail. */
    }

    UiLineList_Remove(pEnv->pLines, 0);                 /* 0x84944, the tail both arms share */
    if (pEnv->pLines->nLineCount == 0 && pEnv->pTime != NULL) {
        GameTime_Unpause(pEnv->pTime);                   /* 0x84954 */
    }
}

/* Smap_CorpseSlotForAlien -- the quantity-remap switch at 0x98DBC..0x98F98. An alien item that no
 * containment cell can hold is delivered as its corpse instead: the shipped names pair every
 * soldier slot with a corpse slot, and each medic lands on its own race's corpse. The original
 * splits the map over two switches (the <= 102 half, then a > 145 half and an else-switch), and it
 * is the > 145 fall-through plus the else-switch default that give Sectoid medic (148) -> 59 and
 * Snakeman medic (144) -> 60. */
static i32 Smap_CorpseSlotForAlien(i32 nSlot)
{
    if (nSlot <= OBJ_SILACOID) { /* 0x98DB0: the <= 102 half */
        if (nSlot == OBJ_SILACOID)              return OBJ_SILACOID_CORPSE;   /* 102 -> 68 */
        if (nSlot <= OBJ_SECTOID_COMMANDER)     return OBJ_SECTOID_CORPSE;    /* 77..81 -> 59 */
        if (nSlot <= OBJ_SNAKEMAN_COMMANDER)    return OBJ_SNAKEMAN_CORPSE;   /* 82..86 -> 60 */
        if (nSlot <= OBJ_FLOATER_COMMANDER)     return OBJ_FLOATER_CORPSE;    /* 87..91 -> 61 */
        if (nSlot <= OBJ_MUTON_NAVIGATOR)       return OBJ_MUTON_CORPSE;      /* 92..94 -> 62 */
        if (nSlot <= OBJ_ETHEREAL_COMMANDER)    return OBJ_ETHEREAL_CORPSE;   /* 95..98 -> 63 */
        if (nSlot == OBJ_CYBERDISK)             return OBJ_CYBERDISK_REMAINS; /* 99 -> 64 */
        if (nSlot == OBJ_CHRYSSALID)            return OBJ_CHRYSSALID_CORPSE; /* 100 -> 65 */
        return OBJ_REAPER_CORPSE;                                             /* 101 -> 66 */
    }
    if (nSlot > OBJ_MUTON_MEDIC) { /* 0x98E9C: the > 145 half */
        if (nSlot == OBJ_FLOATER_MEDIC)         return OBJ_FLOATER_CORPSE;    /* 146 -> 61 */
        if (nSlot == OBJ_ETHEREAL_MEDIC)        return OBJ_ETHEREAL_CORPSE;   /* 147 -> 63 */
        return OBJ_SECTOID_CORPSE;                                            /* 148 -> 59 */
    }
    if (nSlot == OBJ_MUTON_MEDIC)         return OBJ_MUTON_CORPSE;     /* 145 -> 62 */
    if (nSlot == OBJ_CELACID)             return OBJ_CELATID_CORPSE;   /* 103 -> 67 */
    if (nSlot == OBJ_SECTOPOD)            return OBJ_SECTOPOD_REMAINS; /* 104 -> 69 */
    if (nSlot == OBJ_ETHEREAL_NAVIGATOR)  return OBJ_ETHEREAL_CORPSE;  /* 114 -> 63 */
    return OBJ_SNAKEMAN_CORPSE;                                        /* 144, and the default */
}

/* Smap_GrantSiteRecovery @ 0x98b50. The header block has the three phases, the containment gate,
 * the corpse remap and the deviations. */
void Smap_GrantSiteRecovery(const SmapGrantRecoveryEnv *pEnv)
{
    if (pEnv == NULL || pEnv->aSites == NULL || pEnv->pnSiteCount == NULL) {
        return;
    }

    /* 0x98B68..0x98BAC: age every countdown byte once. */
    for (i32 i = 0; i < *pEnv->pnSiteCount; ++i) {
        if ((i8)pEnv->aSites[i].bFlag0 > 0) {
            pEnv->aSites[i].bFlag0 = (u8)((i8)pEnv->aSites[i].bFlag0 - 1);
        }
    }

    i32 nAlienCarried = 0; /* 0x98B70: function-wide, NOT reset per site */

    for (i32 nSite = 0; nSite < *pEnv->pnSiteCount; ++nSite) {
        SmapSite *pSite = &pEnv->aSites[nSite];
        if (pSite->bFlag0 != 0) {
            continue; /* 0x98BD8 */
        }

        const i32 nBaseCount = (i32)(i8)*pEnv->pbBaseCount;
        i32 nBase = 0;
        if (nBaseCount > 0) {
            while (nBase < nBaseCount &&
                   (pEnv->apBases[nBase] == NULL ||
                    pEnv->apBases[nBase]->bUnk4 != pSite->bFlag1)) {
                ++nBase; /* 0x98C18 */
            }
        }
        if (nBase >= nBaseCount) {
            /* DEVIATION: the original keeps going through apBases[nBaseCount], one past the array.
             * The port marks the site delivered and skips the delivery. */
            pSite->bFlag0 = 0xFF;
            continue;
        }

        BaseRec *pBase = pEnv->apBases[nBase];
        const u8 nKind = (u8)nBase;

        if (pSite->bHasLoot != 0) {
            const i32 nUsed = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase);
            const i32 nAcross = Base_CountItemQtyAcrossBases(pEnv->aSites, *pEnv->pnSiteCount,
                                                             nSite, pEnv->apBases, nBase,
                                                             pEnv->apObjects, pEnv->nObjectCount,
                                                             true);
            if (nUsed + nAcross > Base_CalcStoresCapacity(pEnv->abFacilityGrid, nBase)) {
                *pEnv->pdwShortfall = 1; /* 0x98CDC */
                UiLineList_AppendWithSound(pEnv->pLines, 42, (u32)nBase, 0u, 0u, 0u,
                                           pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x98CF0 */
            }
        } else {
            *pEnv->pdwShortfall = 1; /* 0x98C44 */
            UiLineList_AppendWithSound(pEnv->pLines, 41, (u32)nBase, 0u, 0u, 0u,
                                       pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x98C5C */
        }

        BaseRecoverDistributeEnv dist = {
            pEnv->apCraft, pEnv->pnCraftCount, pEnv->apBases, pEnv->pbBaseCount,
            pEnv->apObjects, pEnv->nObjectCount
        };

        for (i32 nSlot = 0; nSlot < pEnv->nObjectCount; ++nSlot) {
            const bool bAlienSlot =
                (nSlot >= OBJ_SECTOID_SOLDIER && nSlot <= OBJ_SECTOPOD) ||
                nSlot == OBJ_ETHEREAL_NAVIGATOR ||
                (nSlot >= OBJ_SNAKEMAN_MEDIC && nSlot <= OBJ_SECTOID_MEDIC);
            bool bFits = true;

            if (bAlienSlot && pSite->auItemQty[nSlot] != 0) {
                ++nAlienCarried; /* 0x98D58 */
                bFits = Base_CalcAlienContainmentCap(pEnv->abFacilityGrid, nKind) >= nAlienCarried;
            }

            if (bFits) {
                if (nSlot < OBJ_CRAFT_COUNT_SKYRANGER || nSlot > OBJ_CRAFT_COUNT_AVENGER) {
                    BaseRec_AddItemQty(pBase, pEnv->apObjects[nSlot],
                                       (i32)pSite->auItemQty[nSlot]); /* 0x9900C */
                }
                if (pEnv->apObjects[nSlot] != NULL &&
                    pEnv->apObjects[nSlot]->auPerBaseVal[nBase] > 60000) {
                    /* 0x99038: Transfer_WriteErrorDump (0x996e4, not ported) writes a debug text
                     * dump when a base's column for the slot passes 60000. It changes no game
                     * state, so the port keeps the test and drops the dump. */
                }
                Base_DistributeRecoveredItems(&dist); /* 0x99040: after EVERY slot */
                continue;
            }

            *pEnv->pdwShortfall = 1; /* 0x98D90 */
            UiLineList_AppendWithSound(pEnv->pLines, 36, (u32)nKind, 0u, 0u, 0u,
                                       pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x98DA4 */
            const i32 nCorpse = Smap_CorpseSlotForAlien(nSlot);
            const i32 nQty = (i32)pSite->auItemQty[nSlot];
            pSite->auItemQty[nSlot] = 0; /* 0x98F94 */
            BaseRec_AddItemQty(pBase, pEnv->apObjects[nCorpse], nQty); /* 0x98FA0 */
        }

        for (i32 i = 0; i < (i32)pSite->uRecoveredSoldiers; ++i) {
            (void)Unit_CreateAndClassify(pEnv->pUnitEnv, nKind); /* 0x99080 */
        }

        for (i32 i = 0; i < (i32)(i8)pSite->bRecoveredCraftCount; ++i) {
            const i32 nCraftCount = (i32)*pEnv->pnCraftCount;
            i32 nCraft = 0;
            while (nCraft < nCraftCount &&
                   (pEnv->apCraft[nCraft] == NULL ||
                    pEnv->apCraft[nCraft]->bId != pSite->anCraftIds[i])) {
                ++nCraft; /* 0x99100 */
            }
            if (nCraft >= nCraftCount || pEnv->apCraft[nCraft] == NULL) {
                continue; /* DEVIATION: the original reads apCraft[Map_nCraftCount] on a miss */
            }

            Ship *pShip = pEnv->apCraft[nCraft];
            pShip->bHomeBase = nKind; /* 0x9912C: unconditional, before any cell is found */

            u8 *pFac = pEnv->abFacilityGrid + PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase;
            const u8 *pDays = pEnv->abFacilityGrid + PUFO_GRID_DAYS_STRIDE * nBase;
            bool bPlaced = false;

            for (i32 nRow = 0; nRow < 5 && !bPlaced; ++nRow) { /* 0x9921C: five rows of a six-row grid */
                for (i32 nCol = 0; nCol < 5 && !bPlaced; ++nCol) { /* 0x99208: five columns of six */
                    if (pFac[PUFO_GRID_FAC_TYPE_OFF + 6 * nRow + nCol] != (u8)FAC_HANGAR) {
                        continue; /* 0x99158 */
                    }
                    if (pFac[PUFO_GRID_LAYER2_OFF + 7 + nRow + nCol] != 0) {
                        continue; /* 0x99174: tested one column further along for every row */
                    }
                    if (pDays[6 * nRow + nCol] != 0) {
                        continue; /* 0x991A0 */
                    }

                    pShip->bHangarX = (u8)(nCol + 1); /* 0x991B8, Ship + 0x12 */
                    pShip->bHangarY = (u8)(nRow + 1); /* 0x991C8, Ship + 0x13 */

                    u8 nGlyph;
                    switch (pShip->bCraftType) {
                        case CRAFT_TYPE_SKYRANGER:   nGlyph = 33; break; /* 0x99310 */
                        case CRAFT_TYPE_LIGHTNING:   nGlyph = 34; break; /* 0x992E0 */
                        case CRAFT_TYPE_AVENGER:     nGlyph = 35; break; /* 0x992B0 */
                        case CRAFT_TYPE_INTERCEPTOR: nGlyph = 36; break; /* 0x99280 */
                        case CRAFT_TYPE_FIRESTORM:   nGlyph = 37; break; /* 0x99250 */
                        default: continue; /* 0x991F8: an unknown type CONTINUES the scan */
                    }
                    pFac[PUFO_GRID_LAYER2_OFF + 6 * (nRow + 1) + (nCol + 1)] = nGlyph; /* 0x99314 */
                    bPlaced = true;
                }
            }
        }

        /* 0x99360..0x993A8 is the original's inline switch on the slot INDEX (0->0, 1->3, 2->4,
         * 3->1, 4->2). Ship_ProductSlotToType @ 0x65a6c answers the same five values from the slot
         * id itself, so the port shares that one map instead of carrying a second copy. */
        for (i32 i = 0; i < 5; ++i) {
            const i32 nSlot = OBJ_CRAFT_COUNT_SKYRANGER + i;
            const i32 nQty = (i32)pSite->auItemQty[nSlot];
            for (i32 n = 0; n < nQty; ++n) {
                (void)Ship_SpawnAtSlot(pEnv->pShipEnv,
                                       (u8)Ship_ProductSlotToType(nSlot), nBase); /* 0x993AC */
            }
        }

        for (i32 i = 0; i < (i32)(i8)pSite->bRecoveredSoldierAssign; ++i) {
            const i32 nSolCount = (i32)*pEnv->pnSoldierCount;
            i32 nSol = 0;
            while (nSol < nSolCount &&
                   (pEnv->apSoldiers[nSol] == NULL ||
                    (i32)pSite->anSoldierIds[i] != pEnv->apSoldiers[nSol]->nUnk440)) {
                ++nSol; /* 0x99430 */
            }
            if (nSol < nSolCount && pEnv->apSoldiers[nSol] != NULL) {
                pEnv->apSoldiers[nSol]->nKind = nKind; /* 0x99454, Soldier + 0xB6 */
            }
        }

        pSite->bFlag0 = 0xFF; /* 0x99484: STRB of -1 marks the site delivered */
        (void)Base_UpdateAlienContainmentNeeded(pEnv->abFacilityGrid, nKind,
            Base_CountAmmoItems(pEnv->apObjects, pEnv->nObjectCount, nKind)); /* 0x99488 */
        (void)Base_UpdateLivingQuartersNeeded(pEnv->abFacilityGrid, nKind,
            Base_CalcOccupiedSpace(pEnv->apSoldiers, (i32)*pEnv->pnSoldierCount,
                                   pEnv->apObjects, pEnv->nObjectCount, nKind) +
            Base_CountOutboundTransferSpace(pEnv->aSites, *pEnv->pnSiteCount, -1,
                                            pEnv->apBases, nKind, true)); /* 0x99494 */
        (void)Base_UpdateStoresNeeded(pEnv->abFacilityGrid, nKind,
            Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nKind) +
            Base_CountItemQtyAcrossBases(pEnv->aSites, *pEnv->pnSiteCount, -1,
                                         pEnv->apBases, nKind, pEnv->apObjects,
                                         pEnv->nObjectCount, true)); /* 0x994A0 */
    }

    /* 0x994C8..0x99558: drop the delivered records. The copy length is 371*(count - index) WORDS,
     * one record more than the records still inside the count, and that extra record -- the unused
     * slot after the last live site, which carries bFlag0 == 0 -- is what ends the inner loop. The
     * index is re-tested after every shift, so a run of delivered sites collapses in one pass. */
    if (*pEnv->pnSiteCount > 0) {
        i32 nCount = *pEnv->pnSiteCount;
        for (i32 k = 0; k < nCount; ++k) {
            while (k < nCount && (i8)pEnv->aSites[k].bFlag0 == -1) {
                size_t cMove = (size_t)(nCount - k);
                if (k + (i32)cMove >= PUFO_SMAP_SITE_COUNT) {
                    /* DEVIATION: the original would read aSmapSites[200], the count dword itself. */
                    cMove = (size_t)(PUFO_SMAP_SITE_COUNT - k - 1);
                }
                if (cMove > 0) {
                    memmove(&pEnv->aSites[k], &pEnv->aSites[k + 1], cMove * sizeof(SmapSite));
                }
                --nCount;
            }
        }
        *pEnv->pnSiteCount = nCount;
    }
}

/* Byte +10 of a UiControl is nState, the selected/radio byte. The original returns the
 * sixth pointer, loaded at 0x6B9C4 / 0x6BA04 before its final store. */
static UiControl *OptionsScreen_ClearRadioGroup(UiControl *apCtl[6])
{
    for (i32 i = 0; i < 6; ++i) {
        if (apCtl[i] != NULL) {
            apCtl[i]->nState = 0;
        }
    }
    return apCtl[5];
}

UiControl *OptionsScreen_ClearMusicButtons(UiControl *apMusic[6])
{
    return OptionsScreen_ClearRadioGroup(apMusic); /* 0x6b994, anLineAux[128..133] */
}

UiControl *OptionsScreen_ClearSfxButtons(UiControl *apSfx[6])
{
    return OptionsScreen_ClearRadioGroup(apSfx); /* 0x6b9d4, anLineAux[134..139] */
}

/* The original can pass StrategList_Clear straight to atexit because its strategic list is
 * a global and its clear takes no arguments. The port's takes the list, so it registers a
 * thunk over the list handed in here. */
static PtrArray *g_pStrategListToClear;

static void StrategList_ClearAtexitThunk(void)
{
    if (g_pStrategListToClear != NULL) {
        StrategList_Clear(g_pStrategListToClear);
    }
}

int StrategList_RegisterAtexit(PtrArray *pList)
{
    g_pStrategListToClear = pList;
    return atexit(StrategList_ClearAtexitThunk); /* 0x838A0 */
}

void OptionsScreen_RebuildWithSound(const OptionsScreenHooks *pHooks)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(7, 4, pHooks->pUser); /* 0x6CF08: MOV R1, #4 */
    }
    if (pHooks != NULL && pHooks->pfnInit != NULL) {
        pHooks->pfnInit(pHooks->pUser); /* 0x6CF14, tail call */
    }
}

void OptionsScreen_OpenWithSound(i32 nIgnored, i32 nMode, const OptionsScreenHooks *pHooks)
{
    (void)nIgnored; /* 0x6DD08 saves R1 into R4 and never touches R0 */
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(7, 4, pHooks->pUser); /* 0x6DD10 */
    }
    if (pHooks != NULL && pHooks->pfnOpen != NULL) {
        pHooks->pfnOpen(nMode, pHooks->pUser); /* 0x6DD20, tail call with R4 */
    }
}

/* Funding_ScoreToBarLevel @ 0x5aa60. How much money one funding bar step is worth.
 *
 * The first argument is the report ROW (see g_anFundingRowSlot), not an amount and not a score slot:
 * FundingScreen_Init passes 0, 1, 2, 3, 4 in the order the five rows are drawn (0x5C39C, 0x5C3E0,
 * 0x5C420, 0x5C460, 0x5C4A0) while the shares those rows belong to are slots 0, 2, 3, 1, 4. The
 * mapping is 0 -> 3, 1 -> 5, 2 -> 4, 3 -> 1, 4 -> 3, and anything else gives 0. Two rows share 3.
 *
 * The second is a direction, not a magnitude: -2 and -1 mean down, 1 and 2 mean up, and 0 or
 * anything outside -2..2 returns 0 outright -- the level lookup is discarded on that path.
 *
 * Hex-Rays renders the level as `v2 << 24 >> 24`, which is the compiler widening a signed char.
 * Every level is 1..5, so the port keeps a plain int multiply. */
i32 Funding_ScoreToBarLevel(i32 nRow, i32 nDirection)
{
    static const i32 anLevelByRow[PUFO_FUND_ROW_COUNT] = { 3, 5, 4, 1, 3 }; /* 0x5AA6C..0x5AAE4 */
    i32 nAmount;

    const i32 nLevel = (nRow >= 0 && nRow < PUFO_FUND_ROW_COUNT) ? anLevelByRow[nRow] : 0;

    switch (nDirection) {
    case -2: nAmount = -50000; break;
    case -1: nAmount = -25000; break;
    case  1: nAmount =  25000; break;
    case  2: nAmount =  50000; break;
    default: return 0; /* 0x5AAD0, 0x5AAC8: the level is thrown away */
    }

    return nLevel * nAmount; /* 0x5AB2C */
}

/* OptionsScreen_PanelPrev @ 0x6d01c and OptionsScreen_PanelNext @ 0x6d064. The options screen has
 * four scrolling lists and its prev/next buttons move ALL FOUR cursors one row at once -- they are
 * not per-panel controls. Both handlers touch the same four panels in the same order, and each one
 * is the plain scroll step: prev is `if (cursor > 0) --cursor`, next is
 * `if (cursor < lineCount - style) ++cursor`.
 *
 * The original reaches the panels through gUiRootPanel.anLineAux[117], [141], [142] and [118], which
 * is a dword array doubling as a pointer array. The port takes them as an explicit four-element
 * array so the coupling is visible: a caller that passes them in the wrong order scrolls the wrong
 * lists, and nothing in the original would tell you. */
void OptionsScreen_PanelPrev(UiPanel *const apPanels[4])
{
    for (int i = 0; i < 4; ++i) {
        if (apPanels[i] != NULL) {
            UiPanel_ScrollPrev(apPanels[i]); /* 0x6D028, 0x6D034, 0x6D040, 0x6D04C */
        }
    }
}

void OptionsScreen_PanelNext(UiPanel *const apPanels[4])
{
    for (int i = 0; i < 4; ++i) {
        if (apPanels[i] != NULL) {
            UiPanel_ScrollNext(apPanels[i]); /* 0x6D070, 0x6D07C, 0x6D088, 0x6D094 */
        }
    }
}

/* Ufopaedia_CacheSelRow @ 0x99fd0. A one-shot cache, and the one-shot part is the behaviour.
 *
 * While the flag is zero it copies the ufopaedia list's current selected row into the cache and
 * raises the flag. Once the flag is raised it does nothing at all -- the cached row is never
 * refreshed by this path. So whatever row was current the first time this ran is the row the rest
 * of the ufopaedia code sees, until some other path clears the flag.
 *
 * The original returns the address of whichever global it touched: the cache when it cached, the
 * flag when it did not. The port returns whether it cached, which is the same information without
 * handing out addresses. */
bool Ufopaedia_CacheSelRow(u8 *pbFlag, i32 *pnCachedRow, i32 nPanelSelectedRow)
{
    if (pbFlag == NULL || pnCachedRow == NULL) {
        return false;
    }

    if (*pbFlag != 0) {
        return false; /* 0x99FD4: the flag is already up, nothing happens */
    }

    *pbFlag = 1;                              /* 0x99FE4 */
    *pnCachedRow = nPanelSelectedRow;         /* 0x9A000: dword_187204->nUnk3032 */
    return true;
}

/* Funding_DeltaToArrow @ 0x5ab34. Which arrow to draw next to a funding figure: -2, -1, 0, 1, 2.
 *
 * It is a chain of compares, not a switch, and the thresholds come from the total: a2/8 and a2/2,
 * with integer division truncating toward zero. That makes the bands asymmetric for a negative
 * total, which is the point -- a base in the red gets a different arrow for the same delta.
 *
 * The third test reads `a1 <= a2 / -8`, not `a2 / 8`. Dividing by -8 is what puts the middle band
 * on the other side of zero. */
i32 Funding_DeltaToArrow(i32 nDelta, i32 nTotal)
{
    if (nDelta <= nTotal / 8 + nTotal / 2) {
        return -2; /* 0x5AB54 */
    }
    if (nDelta <= nTotal / 8) {
        return -1; /* 0x5AB60 */
    }
    if (nDelta <= nTotal / -8) {
        return 0; /* 0x5AB7C */
    }
    if (nDelta > nTotal / -8 - nTotal / 2) {
        return 2; /* 0x5ABAC */
    }
    return 1; /* 0x5ABD4 */
}

/* SoldierScreen_FindNextAtBase @ 0x6192c. See the header for the evidence. */
i32 SoldierScreen_FindNextAtBase(i32 *pnCursor, Soldier *const *apSoldiers, u16 nSoldierCount,
                                 i8 nSelectedBaseIdx)
{
    i32 nIdx = *pnCursor + 1;                    /* 0x61948 ADD R0, #1 */
    if (nIdx >= (i32)nSoldierCount) {            /* 0x61950 CMP/BGE, LDRH count at 0x61940 */
        return nIdx;                             /* cursor untouched */
    }
    while ((i32)apSoldiers[nIdx]->nKind != (i32)nSelectedBaseIdx) { /* 0x61968..0x6198C */
        nIdx += 1;                               /* 0x61978/0x6197C */
        if (nIdx >= (i32)nSoldierCount) {        /* 0x6198C */
            return nIdx;                         /* == the count, cursor untouched */
        }
    }
    *pnCursor = nIdx;                            /* 0x619A0 */
    return nIdx;
}

/* SoldierScreen_FindPrevAtBase @ 0x618c4: the same walk downward. It never reads the soldier
 * count (0x618C4..0x61928 has no LDRH) — the walk is bounded by zero, not by the count. */
i32 SoldierScreen_FindPrevAtBase(i32 *pnCursor, Soldier *const *apSoldiers, i8 nSelectedBaseIdx)
{
    i32 nIdx = *pnCursor - 1;                    /* 0x618D0 SUBS R0, #1 */
    if (nIdx < 0) {                              /* 0x618D4 BMI */
        return nIdx;                             /* cursor untouched */
    }
    while ((i32)apSoldiers[nIdx]->nKind != (i32)nSelectedBaseIdx) { /* 0x618F0..0x61910 */
        nIdx -= 1;                               /* 0x61900/0x61904 */
        if (nIdx < 0) {                          /* 0x61910 BPL falls out */
            return nIdx;                         /* == -1, cursor untouched */
        }
    }
    *pnCursor = nIdx;                            /* 0x61924 */
    return nIdx;
}

/* Ui_SetTransferFlag @ 0x619a8. */
void Ui_SetTransferFlag(u8 *pbTransferFlag, void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser),
                        void *pUser)
{
    if (pfnPlaySample != NULL) {
        pfnPlaySample(17, 4, pUser); /* 0x619B4, MOV R1, #4 */
    }
    if (pbTransferFlag != NULL) {
        *pbTransferFlag = 1;         /* 0x619C0 STRB */
    }
}

/* aNewBaseCostByRegion @ 0xAB488, ten dwords, indexed by (pending region id - 0x80). */
const i32 g_anNewBaseCostByRegion[PUFO_NEW_BASE_COST_COUNT] = {
    800000, 600000, 1000000, 800000, 750000, 650000, 900000, 950000, 800000, 0
};

/* NewBaseScreen_Open @ 0x615d0. */
void NewBaseScreen_Open(GeoscapeScreenState *pState, const NewBaseOpenEnv *pEnv)
{
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, 4, pEnv->pUser); /* 0x615DC */
    }
    if (*pEnv->pbBaseCount == 8) {               /* 0x615F0 LDRSB, 0x615F4 CMP #8 */
        return;
    }
    if (pEnv->pCtlFrame != NULL) {
        pEnv->pCtlFrame->x = 224;                /* 0x61608 STRH #0xE0 */
        pEnv->pCtlFrame->y = 1;                  /* 0x61614 STRH #1 */
    }
    pState->nGeoscapeScreen = 2;                 /* 0x61620, the screen id at 0x186BC4 */
    if (pEnv->pCtlConfirm != NULL) {
        pEnv->pCtlConfirm->pfnClick = pEnv->pfnTryConfirm; /* 0x6162C */
    }
    (void)Smap_ZoomOut(pEnv->pView, pEnv->pZoomSets, pEnv->pWorldOut); /* 0x61634 */
    (void)Smap_ZoomOut(pEnv->pView, pEnv->pZoomSets, pEnv->pWorldOut); /* 0x6163C */
    *pEnv->pbFlag187080 = 0;                     /* 0x6164C STRB */
    if (pEnv->pCtlFrame != NULL) {
        pEnv->pCtlFrame->pfnClick = pEnv->pfnCloseThunk; /* 0x61654 */
    }
}

/* NewBaseScreen_TryConfirm @ 0x6152c. */
i32 NewBaseScreen_TryConfirm(u8 bPlacementPending, const NewBaseConfirmEnv *pEnv)
{
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(17, 4, pEnv->pUser); /* 0x61538 */
    }

    if (bPlacementPending == 0) {                /* 0x6153C LDRB 0x1870AC, ANDS/BNE */
        i32 nKind = (i32)pEnv->bPendingRegionId - 128;
        i32 nCost = 0; /* deviation: the original reads the table unguarded (0x61554..0x61568);
                        * an out-of-range kind reads adjacent image bytes, which are zero for
                        * the 0xFF sentinel the pointer handlers use. */
        if (nKind >= 0 && nKind < PUFO_NEW_BASE_COST_COUNT) {
            nCost = g_anNewBaseCostByRegion[nKind];
        }
        if (*pEnv->pnFunds - nCost < 0) {        /* 0x61570 SUBS, MI */
            *pEnv->pbFlag1870C8 = 1;             /* 0x61574 STRMI (dword in the original, byte
                                                  * everywhere it is read; see the header) */
            *pEnv->pnFlag1870B4 = 0;             /* 0x61580 */
            return 0;                            /* 0x61590 returns dword_1870B4, just cleared */
        }
    }

    return pEnv->pfnMapClick(pEnv->pUser);       /* 0x61588 LDR R0,=0x1864F8; BL Geoscape_OnMapClick */
}

void BaseInfoScreen_TogglePanelMode(const BaseInfoToggleEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pbModeFlag == NULL) {
        return;
    }

    /* 0x55764 ANDS R3,R1,#0xFF. Every flag test below answers THIS value. */
    const bool bWasZero = ((*pEnv->pbModeFlag & 0xFFu) == 0);

    if (bWasZero) {
        *pEnv->pbModeFlag = 1; /* 0x55778 STRBEQ */
    } else {
        *pEnv->pbModeFlag = 0; /* 0x5576C STRBNE */
    }

    /* 0x55770 MOVNE #0x10 then 0x55784 MOVEQ #0: the cursor follows the flag's ORIGINAL value, so
     * a flag that was zero now holds 1 while the cursor written is 0. */
    const i32 nCursor = bWasZero ? 0 : PUFO_BASEINFO_CURSOR_ON;

    if (pEnv->pPanelB != NULL) {
        pEnv->pPanelB->nUnk3028 = nCursor; /* 0x55788, unk_158824 */
    }
    if (pEnv->pPanelA != NULL) {
        pEnv->pPanelA->nUnk3028 = nCursor; /* 0x55798, unk_158820 */
    }
    if (pEnv->pPanelC != NULL) {
        pEnv->pPanelC->nUnk3028 = nCursor; /* 0x557A4, unk_158838 */
    }
}

/* stru_AB4B0 @ 0xAB4B0: the ten nav-button descriptors. All ten carry pads "emptyb"/"cemptyb"
 * with no third pad and mode 0; only the offsets differ. */
const UiControlDesc g_aGeodescNav[PUFO_NAV_CTLS] = {
    {   1, 229, "emptyb", "cemptyb", "", 0 },
    {   1, 247, "emptyb", "cemptyb", "", 0 },
    {   1, 265, "emptyb", "cemptyb", "", 0 },
    {   1, 283, "emptyb", "cemptyb", "", 0 },
    {   1, 301, "emptyb", "cemptyb", "", 0 },
    { 120, 229, "emptyb", "cemptyb", "", 0 },
    { 120, 247, "emptyb", "cemptyb", "", 0 },
    { 120, 265, "emptyb", "cemptyb", "", 0 },
    { 120, 283, "emptyb", "cemptyb", "", 0 },
    { 120, 301, "emptyb", "cemptyb", "", 0 },
};

/* stru_AB5F0 @ 0xAB5F0: the soldier-list page buttons. */
const UiControlDesc g_aGeodescPanel[PUFO_NAV_PANEL_CTLS] = {
    { 210, 20, "up",   "sup",   "", 0 },
    { 210, 40, "down", "sdown", "", 0 },
};

/* stru_AB630 @ 0xAB630: the soldier-find buttons. */
const UiControlDesc g_aGeodescFind[PUFO_NAV_FIND_CTLS] = {
    { 35, 37, "bnexts",  "bnext",  "", 0 },
    {  3, 37, "bprevs",  "bprev",  "", 0 },
    { 70, 37, "emptyb1", "cemptyb1", "", 0 },
};

/* operator new(0x20) + UiControl_ctor, as the three creation loops do (0x67B30, 0x67C18, 0x67CC8). */
static UiControl *NavCtl_New(void)
{
    UiControl *pCtl = (UiControl *)malloc(sizeof(UiControl));
    if (pCtl != NULL) {
        UiControl_ctor(pCtl);
    }
    return pCtl;
}

/* Geoscape_BuildNavControls @ 0x67b1c. See the header for the evidence and the deviations. */
bool Geoscape_BuildNavControls(NavControls *pOut, const NavCtlEnv *pEnv)
{
    i32 i;
    bool bOk = true;

    for (i = 0; i < PUFO_NAV_CTLS; ++i) {           /* 0x67B2C..0x67B5C */
        pOut->apNav[i] = NavCtl_New();
        if (pOut->apNav[i] == NULL) {
            bOk = false;
        }
    }
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {           /* 0x67B74..0x67BE0 */
        if (pOut->apNav[i] != NULL) {
            pOut->apNav[i]->pfnClick = pEnv->apfnNavClick[i];
        }
    }
    for (i = 0; i < PUFO_NAV_CTLS; ++i) {           /* 0x67BF4, UiControl_Setup(ctl, 0, 0, desc) */
        if (pOut->apNav[i] != NULL) {
            UiControl_Setup(pOut->apNav[i], 0, 0, &g_aGeodescNav[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {     /* 0x67C10..0x67C60 */
        pOut->apPanel[i] = NavCtl_New();
        if (pOut->apPanel[i] == NULL) {
            bOk = false;
        }
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {     /* 0x67C84/0x67C90 */
        if (pOut->apPanel[i] != NULL) {
            pOut->apPanel[i]->pfnClick = pEnv->apfnPanelClick[i];
        }
    }
    for (i = 0; i < PUFO_NAV_PANEL_CTLS; ++i) {     /* 0x67CA4 */
        if (pOut->apPanel[i] != NULL) {
            UiControl_Setup(pOut->apPanel[i], 0, 0, &g_aGeodescPanel[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {      /* 0x67CC0..0x67CF0 */
        pOut->apFind[i] = NavCtl_New();
        if (pOut->apFind[i] == NULL) {
            bOk = false;
        }
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {      /* 0x67D10..0x67D28 */
        if (pOut->apFind[i] != NULL) {
            pOut->apFind[i]->pfnClick = pEnv->apfnFindClick[i];
        }
    }
    for (i = 0; i < PUFO_NAV_FIND_CTLS; ++i) {      /* 0x67D3C */
        if (pOut->apFind[i] != NULL) {
            UiControl_Setup(pOut->apFind[i], 0, 0, &g_aGeodescFind[i],
                            pEnv->pfnLoadPads, pEnv->pLoadPadsUser);
        }
    }

    return bOk;
}

/* Geoscape_OnRadarListClick @ 0x854c4. See the header for the six branches and the deviations. */
i32 Geoscape_OnRadarListClick(const RadarClickEnv *pEnv)
{
    StrategList view;
    StrategItem *pItem;
    i32 nRowIdx;
    i32 nItemIdx;
    i32 nShipIdx;

    if (pEnv == NULL || pEnv->pLines == NULL || pEnv->pStatusPanel == NULL ||
        pEnv->pStrategList == NULL || pEnv->pnSelectedStrategIdx == NULL ||
        pEnv->pTime == NULL) {
        return 0;
    }

    nRowIdx = pEnv->pStatusPanel->nUnk3032;
    nItemIdx = pEnv->pStatusPanel->anLineAux[nRowIdx]; /* 0x85500: the aux column is the list index */

    *pEnv->pbFlag1870A0 = 0;                            /* 0x85504, byte_18709C[4] */
    *pEnv->pnSelectedStrategIdx = nItemIdx;             /* 0x8550C, the dword at 0x186BE4 */
    pEnv->pTime->bPaused = 0;                           /* 0x85510: unpause before the dispatch */

    pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, nItemIdx);
    if (pItem == NULL) {
        return 0; /* deviation: the original dereferences whatever PtrArray_Get hands back */
    }

    if (pItem->nType == 1 && pItem->bActive == 1) {     /* 0x85538 */
        pEnv->pTime->bPaused = 1;                       /* GameTime_Pause @ 0x5d6ac */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x8554C */
        /* 0x85560 reloads the index with LDRB, so the stored colour slot is its low byte --
         * UiPanel_InsertLine masks slot 0 to a byte (0x5A2F4). */
        UiLineList_Insert(pEnv->pLines, 0, 10, (u32)(u8)nItemIdx, 0u, 0u, 0u); /* 0x8556C */
    }

    if (pItem->nType == 0 && pItem->bActive == 0) {     /* 0x8559C */
        pEnv->pTime->bPaused = 1;
        *pEnv->pnScreenId = 18;                         /* 0x855B0 */
        OptionsScreen_OpenWithSound(0, pItem->nSiteId, pEnv->pOptionsHooks); /* 0x855C8 */
    }

    if (pItem->nType == 1 && pItem->bActive == 0) {     /* 0x855FC */
        pEnv->pTime->bPaused = 1;
        view.apItems = (StrategItem **)pEnv->pStrategList->apItems;
        view.nCount = pEnv->pStrategList->nCount;
        view.nNextDisplayId = 0; /* not read by the lookup */
        nShipIdx = Ship_FindIndexByStrategType(pEnv->pWorld, &view, nItemIdx); /* 0x85624 */
        *pEnv->pnSelectedShipIdx = nShipIdx;            /* 0x187054, then 0x159E74 at 0x85630 */
        *pEnv->pnMsgShipIdx = nShipIdx;
        UiLineList_AppendWithSound(pEnv->pLines, 11, (u32)nShipIdx, 0u, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x8563C */
        *pEnv->pnRadarPulseTimer = 250;                 /* 0x85644 */
    }

    if (pItem->nType == 4 && pItem->bActive == 1) {     /* 0x85674 */
        pEnv->pTime->bPaused = 1;
        *pEnv->pbFlag18708C = 1;                        /* 0x85688 */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x85694 */
        UiLineList_AppendWithSound(pEnv->pLines, 20, (u32)nRowIdx, (u32)nItemIdx, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x856B4 */
    }

    if (pItem->nType == 3 && pItem->bActive == 1) {     /* 0x856E4 */
        pEnv->pTime->bPaused = 1;                       /* 0x856EC, a direct store here */
        *pEnv->pbFlag187090 = 1;                        /* 0x856F8 */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x85704 */
        UiLineList_AppendWithSound(pEnv->pLines, 19, (u32)nRowIdx, (u32)nItemIdx, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x85724 */
        *pEnv->pnRadarPulseTimer = 250;                 /* 0x8572C */
    }

    if (pItem->nType == 0 && pItem->bActive == 1) {     /* 0x85744, the tail case */
        pEnv->pTime->bPaused = 1;                       /* 0x85764 */
        *pEnv->pbFlag187098 = 1;                        /* 0x85770 */
        UiLineList_Remove(pEnv->pLines, 0);             /* 0x8577C */
        UiLineList_AppendWithSound(pEnv->pLines, 18, (u32)nRowIdx, (u32)nItemIdx, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x8579C */
        *pEnv->pnRadarPulseTimer = 250;                 /* 0x857A4 */
        return 250;
    }

    return 0; /* the original hands back the item pointer here; nothing reads it */
}

/* Smap_HitTestStrategSelect @ 0x82be8. See the header for the gates, the two line builders, the
 * six-branch dispatch and the deviations. */

/* Screen pixel to map cell -- the arithmetic Smap_GetTileAtScreen @ 0x82244 uses, without its
 * nPickX/nPickY stores. The scan below runs it 256 times and only the flag-27 tail may publish a
 * cell, so the port cannot reuse the leaf here. */
static void SmapHit_CellAt(const SmapView *pView, i32 nX, i32 nY, i32 *pnCellX, i32 *pnCellY)
{
    i32 nCellX = pView->nScrollX + pView->nMapW / 2 + nX - 120; /* 0x82C84 */
    i32 nCellY = pView->nScrollY + pView->nMapH / 2 + nY - 160; /* 0x82CAC */

    if (nCellX < 0) {
        nCellX += pView->nMapW; /* 0x82CB4, one wrap only */
    }
    if (nCellY < 0) {
        nCellY += pView->nMapH; /* 0x82CC4 */
    }
    *pnCellX = nCellX;
    *pnCellY = nCellY;
}

/* The base search at 0x82E9C..0x82EF8. Base_FindIndexByTypeByte @ 0x6ac30 walks the same array on
 * the same field, but its argument is masked to a byte (`AND R1, R1, #0xFF` at 0x6AC58) while this
 * one compares the full nSiteId against the SIGNED field (LDRSB at 0x82ED4). A site id above 127
 * has no match here, and one above 255 would match the wrong base through the byte-typed helper,
 * so this call site keeps its own walk. */
static i32 SmapHit_FindBaseBySiteId(const SmapHitEnv *pEnv, i32 nSiteId)
{
    const i32 nCount = (pEnv->pnBaseCount != NULL) ? (i32)*pEnv->pnBaseCount : 0;

    if (pEnv->apBases == NULL || nCount <= 0) {
        return -1; /* 0x82EB0 */
    }
    for (i32 i = 0; i < nCount; ++i) {
        if (pEnv->apBases[i] == NULL) {
            continue; /* deviation 2 */
        }
        if ((i32)(i8)pEnv->apBases[i]->bUnk4 == nSiteId) {
            return i; /* 0x82ED4 / 0x82EDC */
        }
    }
    /* ORIGINAL BUG (0x82F1C): with no match the original leaves the index at nBaseCount and reads
     * byte_186C24 + 64 * nBaseCount + 54, one record past the end of the block. The port refuses the
     * read and the caller writes an empty name. */
    return -1;
}

/* One built line: the text into the panel's 50-byte line buffer, the item's list index into the
 * aux column beside it (0x8306C / 0x83230). */
static void SmapHit_AddLine(UiPanel *pPanel, i32 nLine, const char *pszText, i32 nItemIdx)
{
    /* The original writes straight through apLines[nLine] with no count gate, and the count it
     * carries at that moment is nLine itself. The port's setter gates on nLineCount, so the line is
     * opened first; the count is stored back at 0x83248 either way. */
    if (pPanel->nLineCount < nLine + 1) {
        pPanel->nLineCount = nLine + 1;
    }
    (void)UiPanel_SetLineText(pPanel, nLine, pszText);
    UiPanel_SetLineAux(pPanel, nLine, nItemIdx);
}

/* A TEXTS label into the build buffer. The original copies it with an unbounded loop and then
 * strcat's the number onto the same buffer; UiPanel_SetLineText writes into a 50-byte line, so the
 * port bounds the build at the original's own 56-byte local. */
static void SmapHit_PutLabel(const SmapHitEnv *pEnv, i32 nRec, char *pszText, size_t cchText)
{
    pszText[0] = 0;
    if (pEnv->pTextRecs != NULL) {
        pufo_strncpy(pszText, (const char *)(pEnv->pTextRecs + (size_t)PUFO_TEXT_REC_STRIDE * (size_t)nRec),
                     cchText);
    }
}

static void SmapHit_AppendNumber(char *pszText, size_t cchText, i32 nValue)
{
    char szNum[16];

    snprintf(szNum, sizeof(szNum), "%d", nValue); /* the original's j_itoa (0x9e9f0) */
    (void)pufo_strcat(pszText, szNum, cchText);   /* and its strcat (0xa1174) */
}

u8 Smap_HitTestStrategSelect(const SmapView *pView, const SmapHitEnv *pEnv,
                             i32 nClickX, i32 nClickY)
{
    char szText[PUFO_SMAP_HIT_TEXT_MAX];
    char szName[PUFO_SMAP_HIT_TEXT_MAX];
    UiPanel *pPanel;
    GeoscapeWorld world;
    StrategList view;
    i32 nLine = 0;      /* v57, the line the next match writes */
    u8 bSelectable = 0; /* v56, the answer */
    i32 nStripH;

    if (pView == NULL || pEnv == NULL || pEnv->pStatusPanel == NULL || pEnv->pLines == NULL ||
        pEnv->pStrategList == NULL || pEnv->pStripPad == NULL || pEnv->pTime == NULL ||
        pEnv->pnSelectedStrategIdx == NULL) {
        return 0;
    }
    pPanel = pEnv->pStatusPanel;
    nStripH = (i32)pEnv->pStripPad->uHeight;

    if (nClickY > PUFO_SURF_H - nStripH) { /* 0x82C18, strict: the strip edge is still a map click */
        return 0;
    }

    pPanel->nLineCount = 0; /* 0x82C3C */

    for (i32 nDy = -PUFO_SMAP_HIT_SCAN_HALF; nDy < PUFO_SMAP_HIT_SCAN_HALF; ++nDy) {     /* 0x82C48 */
        for (i32 nDx = -PUFO_SMAP_HIT_SCAN_HALF; nDx < PUFO_SMAP_HIT_SCAN_HALF; ++nDx) { /* 0x82C50 */
            i32 nCellX;
            i32 nCellY;

            SmapHit_CellAt(pView, nDx + nClickX, nDy + nClickY, &nCellX, &nCellY);

            for (i32 i = 0; i < pEnv->pStrategList->nCount; ++i) {
                const StrategItem *pItem =
                    (const StrategItem *)PtrArray_Get(pEnv->pStrategList, i);
                i32 nX;
                i32 nY;

                if (pItem == NULL) {
                    continue; /* deviation 2 */
                }

                nX = pItem->nX;
                nY = pItem->nY;
                if (pView->nMode == 1) {
                    nX = (i32)((double)nX * PUFO_SMAP_ITEM_SCALE_L1); /* 0x82D48, dtoi truncates */
                    nY = (i32)((double)nY * PUFO_SMAP_ITEM_SCALE_L1); /* 0x82D88 */
                } else if (pView->nMode == 2) {
                    /* 0x82DA4 tests the mode value loaded at 0x82D14, before the first scaling, so
                     * the original's two separate ifs can never both run. */
                    nX = (i32)((double)nX * PUFO_SMAP_ITEM_SCALE_L2); /* 0x82DD0 */
                    nY = (i32)((double)nY * PUFO_SMAP_ITEM_SCALE_L2); /* 0x82E10 */
                }

                if (nX != nCellX || nY != nCellY) {
                    continue; /* LABEL_57 @ 0x82E3C: no line, no answer byte */
                }

                if (pItem->nType == 0 || pItem->nType == 3) { /* 0x82E6C */
                    if (pItem->bActive != 0) {                /* 0x82E84 */
                        if (pItem->bVisible != 0) {           /* 0x82F8C */
                            SmapHit_PutLabel(pEnv, PUFO_TEXT_REC_SITE_LABEL, szText, sizeof(szText));
                            if (pItem->nType == 3) {
                                /* 0x82FE4: the second copy starts again at index 0, it does not
                                 * append -- an alien site's line is record 1146 alone. */
                                SmapHit_PutLabel(pEnv, PUFO_TEXT_REC_ALIEN_LABEL, szText,
                                                 sizeof(szText));
                            }
                            SmapHit_AppendNumber(szText, sizeof(szText), pItem->nSiteId); /* 0x8301C */
                            SmapHit_AddLine(pPanel, nLine, szText, i);
                            ++nLine;
                        }
                    } else if (pEnv->pbSelectionFlag == NULL || *pEnv->pbSelectionFlag == 0) {
                        const i32 nBase = SmapHit_FindBaseBySiteId(pEnv, pItem->nSiteId); /* 0x82E94 */

                        szText[0] = 0;
                        if (nBase >= 0 && pEnv->pbBaseInfoRecs != NULL) {
                            pufo_strncpy(szText,
                                         (const char *)(pEnv->pbBaseInfoRecs + 64u * (size_t)nBase + 54u),
                                         sizeof(szText)); /* 0x82F1C */
                        }
                        SmapHit_AddLine(pPanel, nLine, szText, i);
                        ++nLine;
                    }
                }

                if (pItem->nType == 1 || pItem->nType == 4) { /* 0x830B4 */
                    if (pItem->bActive != 0) {                /* 0x830D4 */
                        if (pItem->bVisible != 0) {           /* 0x831A0 */
                            SmapHit_PutLabel(pEnv, PUFO_TEXT_REC_CRAFT_LABEL, szText, sizeof(szText));
                            SmapHit_AppendNumber(szText, sizeof(szText), pItem->nDisplayId); /* 0x831E8 */
                            SmapHit_AddLine(pPanel, nLine, szText, i);
                            ++nLine;
                        }
                    } else if (pItem->bVisible != 0 &&
                               (pEnv->pbSelectionFlag == NULL || *pEnv->pbSelectionFlag == 0)) {
                        /* 0x830F4. The inline walk at 0x830F8..0x83158 is Ship_FindIndexByStrategType
                         * over the same array on the same field, so the port calls it. */
                        world.apCraft = (Ship *const *)pEnv->apCraft;
                        world.nShipCount = (pEnv->pnCraftCount != NULL) ? *pEnv->pnCraftCount : 0;
                        view.apItems = (StrategItem **)pEnv->pStrategList->apItems;
                        view.nCount = pEnv->pStrategList->nCount;
                        view.nNextDisplayId = 0; /* not read by the lookup */
                        view.nNextSiteId = 0;

                        const i32 nShipIdx = Ship_FindIndexByStrategType(&world, &view, i);

                        if (nShipIdx >= 0) {
                            (void)Ship_FormatDisplayName(pEnv->apCraft, nShipIdx, pEnv->pTextRecs,
                                                         szName, sizeof(szName)); /* 0x83164, v39 */
                            pufo_strncpy(szText, szName, sizeof(szText));
                        } else {
                            /* ORIGINAL BUG (0x83164): with no match the original formats
                             * apCraft[nCraftCount] -- the slot past the count, a stale pointer or a
                             * null one, which faults at 0x666E4. The port writes an empty name. */
                            szText[0] = 0;
                        }
                        SmapHit_AddLine(pPanel, nLine, szText, i);
                        ++nLine;
                    }
                }

                pPanel->nLineCount = nLine; /* 0x83248 */
                bSelectable = 1;            /* 0x83250, even when no line was added */
            }
        }
    }

    if (pPanel->nLineCount <= 1) {          /* 0x832B0 */
        if (pPanel->nLineCount == 1) {      /* 0x83394 */
            const i32 nItemIdx = pPanel->anLineAux[0];
            const StrategItem *pItem =
                (const StrategItem *)PtrArray_Get(pEnv->pStrategList, nItemIdx);

            if (pItem == NULL) {
                return bSelectable; /* deviation 2: the original dereferences it in every test */
            }

            if (pItem->nType == 1 && pItem->bActive == 1) { /* 0x833D0 */
                pPanel->nUnk3032 = 0;                       /* 0x833DC */
                UiLineList_Remove(pEnv->pLines, 0);         /* 0x833E4 */
                UiLineList_Insert(pEnv->pLines, 0, 10, (u32)(u8)nItemIdx, 0u, 0u, 0u); /* 0x83408 */
                pEnv->pTime->bPaused = 1;                   /* GameTime_Pause @ 0x83410 */
            }

            if (pItem->nType == 0 && pItem->bActive == 0) { /* 0x83448 */
                pPanel->nUnk3032 = 0;                       /* 0x83450 */
                pEnv->pTime->bPaused = 1;                   /* 0x83458 */
                *pEnv->pnScreenId = 18;                     /* 0x83464 */
                OptionsScreen_OpenWithSound(0, pItem->nSiteId, pEnv->pOptionsHooks); /* 0x83480 */
                if (pEnv->pOptPanelShips != NULL) {
                    pEnv->pOptPanelShips->bFlag7073 = 0;    /* 0x83494, no counterpart in the
                                                             * status-panel click handler */
                }
            }

            if (pItem->nType == 4 && pItem->bActive == 1) { /* 0x834D0 */
                pPanel->nUnk3032 = 0;                       /* 0x834D8 */
                *pEnv->pnSelectedStrategIdx = nItemIdx;     /* 0x834E4 */
                pEnv->pTime->bPaused = 1;                   /* 0x834EC */
                *pEnv->pbFlag18708C = 1;                    /* 0x834F8 */
                UiLineList_Remove(pEnv->pLines, 0);         /* 0x83504 */
                /* Both colour slots are read back out of the globals the branch just wrote, as the
                 * original does: slot 0 is the nUnk3032 it cleared, slot 1 the strateg index. */
                UiLineList_AppendWithSound(pEnv->pLines, 20, (u32)pPanel->nUnk3032,
                                           (u32)*pEnv->pnSelectedStrategIdx, 0u, 0u,
                                           pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x83524 */
            }

            if (pItem->nType == 3 && pItem->bActive == 1) { /* 0x83560 */
                pPanel->nUnk3032 = 0;                       /* 0x83568 */
                *pEnv->pnSelectedStrategIdx = nItemIdx;     /* 0x83574 */
                pEnv->pTime->bPaused = 1;                   /* 0x8357C */
                *pEnv->pbFlag187090 = 1;                    /* 0x83588 */
                UiLineList_Remove(pEnv->pLines, 0);         /* 0x83594 */
                UiLineList_AppendWithSound(pEnv->pLines, 19, (u32)pPanel->nUnk3032,
                                           (u32)*pEnv->pnSelectedStrategIdx, 0u, 0u,
                                           pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x835B4 */
                *pEnv->pnRadarPulseTimer = 250;             /* 0x835BC */
            }

            if (pItem->nType == 0 && pItem->bActive == 1) { /* 0x835F4 */
                pPanel->nUnk3032 = 0;                       /* 0x835FC */
                *pEnv->pnSelectedStrategIdx = nItemIdx;     /* 0x83608 */
                pEnv->pTime->bPaused = 1;                   /* 0x83610 */
                *pEnv->pbFlag187098 = 1;                    /* 0x8361C, byte_187090 + 8 */
                UiLineList_Remove(pEnv->pLines, 0);         /* 0x83628 */
                UiLineList_AppendWithSound(pEnv->pLines, 18, (u32)pPanel->nUnk3032,
                                           (u32)*pEnv->pnSelectedStrategIdx, 0u, 0u,
                                           pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x83648 */
                *pEnv->pnRadarPulseTimer = 250;             /* 0x83650 */
            }

            if (pItem->nType == 1 && pItem->bActive == 0) { /* 0x83688 */
                i32 nShipIdx;

                world.apCraft = (Ship *const *)pEnv->apCraft;
                world.nShipCount = (pEnv->pnCraftCount != NULL) ? *pEnv->pnCraftCount : 0;
                view.apItems = (StrategItem **)pEnv->pStrategList->apItems;
                view.nCount = pEnv->pStrategList->nCount;
                view.nNextDisplayId = 0;
                view.nNextSiteId = 0;

                nShipIdx = Ship_FindIndexByStrategType(&world, &view, nItemIdx); /* 0x836A0 */
                *pEnv->pnSelectedShipIdx = nShipIdx;        /* 0x187054, 0x836A0 */
                *pEnv->pnMsgShipIdx = *pEnv->pnSelectedShipIdx; /* 0x159E74, 0x836A4 */
                pPanel->nUnk3032 = 0;                       /* 0x836AC */
                pEnv->pTime->bPaused = 1;                   /* 0x836B4 */
                /* No RemoveLine here -- the only one of the six that keeps line 0. */
                UiLineList_AppendWithSound(pEnv->pLines, 11, (u32)*pEnv->pnSelectedShipIdx,
                                           0u, 0u, 0u, pEnv->pfnPlaySample, pEnv->pSoundUser); /* 0x836D0 */
                *pEnv->pnRadarPulseTimer = 250;             /* 0x836D8 */
            }
        }
    } else {
        pPanel->nPosX = 120;                                            /* 0x832C0 */
        pPanel->nPosY = 120 - pPanel->nLineCount / 2 * pPanel->nFont;  /* 0x832F4 */
        pPanel->nUnk7052 = 20;                                         /* 0x83308 */
        pPanel->nUnk7056 = 120 - pPanel->nLineCount / 2 * pPanel->nFont; /* 0x83334, recomputed */
        pPanel->nViewW = 200;                                          /* 0x83348 */
        pPanel->bFlag7073 = 0;                                         /* 0x83358 */
        pEnv->pTime->bPaused = 1;                                      /* 0x83360 */
        *pEnv->pbFlag1870A0 = 1;                                       /* 0x8336C */
        if (pEnv->pfnPlaySample != NULL) {
            pEnv->pfnPlaySample(7, pEnv->pSoundUser);                  /* Sound_Play @ 0x83378 */
        }
    }

    if (UiLineList_GetFlag(pEnv->pLines, 0) == 27 && nLine == 0 &&
        nClickY < PUFO_SURF_H - nStripH) {                             /* 0x83708..0x83718 */
        i32 nCellX;
        i32 nCellY;

        pEnv->pTime->bPaused = 1;                                      /* 0x83720 */
        if (pEnv->pbSelectionFlag != NULL) {
            *pEnv->pbSelectionFlag = 0;                                /* 0x8372C */
        }
        SmapHit_CellAt(pView, nClickX, nClickY, &nCellX, &nCellY);    /* 0x8374C / 0x83770 */
        if (pEnv->pnPendingPickX != NULL) {
            *pEnv->pnPendingPickX = nCellX;                            /* 0x83784 */
        }
        if (pEnv->pnPendingPickY != NULL) {
            *pEnv->pnPendingPickY = nCellY;                            /* 0x83788 */
        }
        UiLineList_Remove(pEnv->pLines, 0);                            /* 0x83794 */
        UiLineList_Insert(pEnv->pLines, 0, 12, (u32)(u8)*pEnv->pnSelectedShipIdx,
                          0u, 0u, 0u);                                 /* 0x837B4 */
        pPanel->nUnk3032 = 0;                                          /* 0x837BC */
    }

    return bSelectable;                                                /* 0x837C8 */
}

/* stru_AB220 @ 0xAB220: the two build-dialog panel buttons BaseFacScreen_Init positions. Printed
 * and checked by tools/gen_geoiinit_tables.py. */
static const UiControlDesc g_aBaseFacPanelDesc[PUFO_BASEFAC_PANEL_DESC_COUNT] = {
    { 11, 252, "up", "sup", "", 0 },
    { 11, 277, "down", "sdown", "", 0 },
};

/* BaseFacScreen_Init @ 0x5a198. See the header for the five steps and the deviations. */
i32 BaseFacScreen_Init(const BaseFacInitEnv *pEnv)
{
    UiControl *apCtl[PUFO_BASEFAC_PANEL_DESC_COUNT];
    i32 i;

    if (pEnv == NULL) {
        return 0;
    }
    apCtl[0] = pEnv->pUpCtl;
    apCtl[1] = pEnv->pDownCtl;

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseThunk; /* 0x5A1C4 */
    }
    if (pEnv->pnUnk15887C != NULL) {
        *pEnv->pnUnk15887C = 0;                          /* 0x5A1AC */
    }
    if (pEnv->pnUnk158880 != NULL) {
        *pEnv->pnUnk158880 = 0;                          /* 0x5A1B4 */
    }

    /* 0x5A1CC..0x5A200: operator new(0x20) then UiControl_ctor, stored into 0x158874 and 0x158878.
     * The port's controls are caller-owned, but the ctor still runs, as the original runs it. */
    for (i = 0; i < PUFO_BASEFAC_PANEL_DESC_COUNT; ++i) {
        if (apCtl[i] != NULL) {
            UiControl_ctor(apCtl[i]);
        }
    }

    if (apCtl[0] != NULL) {
        apCtl[0]->pfnClick = pEnv->pfnPanelPrev;         /* 0x5A214: STR [R0,#0xC] */
    }
    if (apCtl[1] != NULL) {
        apCtl[1]->pfnClick = pEnv->pfnPanelNext;         /* 0x5A220 */
    }

    for (i = 0; i < PUFO_BASEFAC_PANEL_DESC_COUNT; ++i) { /* 0x5A224..0x5A244 */
        if (apCtl[i] != NULL) {
            UiControl_Setup(apCtl[i], 0, 0, &g_aBaseFacPanelDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseThunk; /* 0x5A254, the same handler again */
        pEnv->pCloseCtl->x = 224;                        /* 0x5A260 */
        pEnv->pCloseCtl->y = 229;                        /* 0x5A26C */
    }
    if (pEnv->pbArmed != NULL) {
        *pEnv->pbArmed = 0;                              /* 0x5A274 */
    }

    if (pEnv->pfnToggle != NULL) {
        pEnv->pfnToggle(pEnv->pToggleUser);              /* 0x5A27C, the tail call */
    }
    return 0; /* the original returns BaseFacScreen_Toggle's register; the hook returns nothing */
}

/* Geoscape_OnMapClick @ 0x8b4a8. The header carries the two branches and the loop arithmetic.
 *
 * The port returns a status instead of the original's residue:
 *   0  nothing ran (any other screen id, an inactive hangar placement, or one that was applied)
 *   1  the hangar placement was refused and bBasePlaceFail was set
 *   2  the pending facility was committed to base 0
 *   3  the base-facility screen was opened
 * The original returns 6, &bBasePlaceFail (0x1870B8), &bBaseFacDone (0x187078) and
 * BaseFacScreen_Init's value in those four cases; nothing reads any of them - the function is
 * installed as a widget click handler and NewBaseScreen_TryConfirm tail-calls it. */
i32 Geoscape_OnMapClick(const MapClickEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pnScreenId == NULL || pEnv->abFacilityGrid == NULL) {
        return 0;
    }

    if (*pEnv->pnScreenId == 1) {
        i32 nSel, nFac, nDays, nTile, i;
        u8 *abGrid = pEnv->abFacilityGrid;

        if (pEnv->pbPlaceActive == NULL || *pEnv->pbPlaceActive == 0) {
            return 0;                                   /* 0x8B664: the gate is the active byte */
        }
        if (pEnv->pHangar == NULL) {
            return 0;
        }
        if (Base_CanPlaceHangarAt(pEnv->pHangar, pEnv->nTapX, pEnv->nTapY) == 0) {
            if (pEnv->pbPlaceFail != NULL) {
                *pEnv->pbPlaceFail = 1;                 /* 0x8B93C */
            }
            return 1;
        }

        nSel  = (i32)pEnv->pHangar->nSelectedBaseIdx;
        nFac  = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nSel + 6 * pEnv->nTapY + pEnv->nTapX;
        nDays = PUFO_GRID_DAYS_STRIDE * nSel + 6 * pEnv->nTapY + pEnv->nTapX;
        nTile = abGrid[nFac + PUFO_GRID_TILEA_OFF];

        if (nTile <= 8 || nTile >= 13) {
            /* 0x8B88C..0x8B92C: not a hangar quadrant, so only the tapped cell goes. */
            abGrid[nDays]                    = 0;
            abGrid[nFac + PUFO_GRID_TILEA_OFF]  = 0;
            abGrid[nFac + PUFO_GRID_TILEB_OFF]  = 0;
            abGrid[nFac + PUFO_GRID_LAYER2_OFF] = 0;
            abGrid[nFac + PUFO_GRID_FAC_TYPE_OFF] = PUFO_FAC_TYPE_NONE;
        } else {
            /* 0x8B6FC..0x8B7B8: the three-iteration loop. Its index arithmetic is worth spelling
             * out because Hex-Rays scrambles it: the four stores per iteration land at
             * 144*nBase + 36*i + 6*nY + nX + {0, 1, 6, 7}, so i walks the three 36-byte layers
             * tileA, tileB and layer2 and {0,1,6,7} is the 2 by 2 footprint inside each. */
            for (i = 0; i < 3; ++i) {
                const i32 nLayer = nFac + 36 * i;
                abGrid[nLayer]     = 0;                 /* 0x8B728 */
                abGrid[nLayer + 1] = 0;                 /* 0x8B754 */
                abGrid[nLayer + 6] = 0;                 /* 0x8B780 */
                abGrid[nLayer + 7] = 0;                 /* 0x8B7B4 */
            }
            abGrid[nFac + PUFO_GRID_FAC_TYPE_OFF] = PUFO_FAC_TYPE_NONE; /* 0x8B7E4 */
            abGrid[nDays]     = 0;                      /* 0x8B804 */
            abGrid[nDays + 6] = 0;                      /* 0x8B828: (nY+1,nX) */
            abGrid[nDays + 1] = 0;                      /* 0x8B84C: (nY,nX+1) */
            abGrid[nDays + 7] = 0;                      /* 0x8B870: the diagonal */
        }
        return 0;
    }

    if (*pEnv->pnScreenId == 2) {
        BaseRec *pBase;
        i32 i, nCount;

        if (pEnv->bPendingRegionId == 0xFF) {
            return 0;                                   /* 0x8B4D0 */
        }

        if (pEnv->pbConfirm != NULL && *pEnv->pbConfirm != 0) {
            pBase = (pEnv->apBases != NULL) ? pEnv->apBases[0] : NULL;

            if (pBase != NULL) {
                pBase->nUnk8  = pEnv->nPlaceX;          /* 0x8B504 */
                pBase->nUnk12 = pEnv->nPlaceY;          /* 0x8B518 */
                pBase->bRegionId = pEnv->bPendingRegionId;     /* 0x8B52C */
                pBase->bUnk16 = 1;                      /* 0x8B53C */
            }
            if (pEnv->apSiteRecs != NULL) {
                pEnv->apSiteRecs[0].bType = (u8)(pEnv->bPendingRegionId + 0x80); /* 0x8B548 */
            }

            /* 0x8B550..0x8B56C: the label is built in the shared scratch string and then copied
             * into the facility record's name field (0x186C5A, record + 26). */
            if (pEnv->pszLabelScratch != NULL && pEnv->apSiteRecs != NULL) {
                (void)BaseFac_FormatCostLabel(pEnv->pszLabelScratch, (size_t)pEnv->cchLabelScratch,
                                              pEnv->pTextRecs, pEnv->bPendingRegionId,
                                              pEnv->paRegionBuildCount);
                pufo_strncpy(pEnv->apSiteRecs[0].abName, pEnv->pszLabelScratch,
                             sizeof(pEnv->apSiteRecs[0].abName));
            }

            /* 0x8B570..0x8B5B4: the entry whose site id equals the base's type byte. The compare
             * is a SIGNED byte (LDRSB at 0x8B598) against the entry's dword. */
            nCount = (pEnv->pStrategList != NULL) ? PtrArray_GetCount(pEnv->pStrategList) : 0;
            for (i = 0; i < nCount; ++i) {
                StrategItem *pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, i);
                if (pItem != NULL && pBase != NULL &&
                    (i32)(i8)pBase->bUnk4 == pItem->nSiteId) {
                    break;
                }
            }
            if (i < nCount) {
                StrategItem *pItem = (StrategItem *)PtrArray_Get(pEnv->pStrategList, i);
                if (pItem != NULL) {
                    pItem->nX = pEnv->nPlaceX;          /* 0x8B5E4 */
                    pItem->nY = pEnv->nPlaceY;          /* 0x8B5F8 */
                }
            }
            /* Deviation: on a miss the original leaves i at the count and writes nX / nY past the
             * last entry. The port skips the write. */

            *pEnv->pnScreenId = 1;                      /* 0x8B600 */
            if (pEnv->pbConfirm != NULL) {
                *pEnv->pbConfirm = 0;                   /* 0x8B608 */
            }
            if (pEnv->pbFacDone != NULL) {
                *pEnv->pbFacDone = 0;                   /* 0x8B60C */
            }
            return 2;
        }

        if (pEnv->pnSelectedBaseIdx != NULL && pEnv->pbBaseCount != NULL) {
            *pEnv->pnSelectedBaseIdx = *pEnv->pbBaseCount; /* 0x8B644: the base COUNT, signed */
        }
        *pEnv->pnScreenId = 7;                          /* 0x8B64C */
        (void)BaseFacScreen_Init(pEnv->pFacInit);       /* 0x8B654, the tail call */
        return 3;
    }

    return 0; /* any other screen id: the original returns the screen id itself */
}

/* --- BaseFacScreen_Toggle @ 0x589ec --- */

void BaseFacScreen_Toggle(const BaseFacToggleEnv *pEnv)
{
    UiPanel *pPanel;
    i32 nCount = 0;
    i32 nRows = 0;
    i32 nAux;
    i32 i;

    if (pEnv == NULL || pEnv->pbOpen == NULL || pEnv->ppPanel == NULL || pEnv->paFac == NULL) {
        return;
    }

    if (*pEnv->pbOpen != 0) {
        /* The close branch (0x58D98..0x58E04). */
        *pEnv->pbOpen = 0;
        for (i = 0; i < PUFO_BASEFAC_PANEL_CTL_COUNT; ++i) {
            UiControl *pCtl = pEnv->apPanelCtl[i]; /* unk_158874, unk_158878 */
            if (pCtl != NULL) {
                UiControl_FreePads(pCtl);          /* 0x58DB4 */
            }
            /* The original also operator delete's the control (0x58DBC). The port's controls are
             * caller-owned, so only their pads are released. */
        }
        pPanel = *pEnv->ppPanel;
        if (pPanel != NULL) {
            pPanel->nLineCount += 1;               /* 0x58DE4: the build branch left it one short */
            UiPanel_FreeLines(pPanel);             /* 0x58DF8 */
            free(pPanel);                          /* 0x58E00 */
            *pEnv->ppPanel = NULL;
        }
        return;
    }

    *pEnv->pbOpen = 1;                             /* 0x58A10 */

    for (i = 0; i < PUFO_BASEFAC_COUNT; ++i) {     /* 0x58A1C..0x58A30: records 0..16 */
        if (pEnv->paFac[i].bFlag20 != 0) {
            ++nCount;
        }
    }

    pPanel = (UiPanel *)malloc(sizeof(UiPanel));   /* 0x58A44: operator new(0x1BB4) */
    if (pPanel == NULL) {
        return; /* deviation: the original stores through the null panel at 0x58A7C */
    }
    if (!UiPanel_ctor(pPanel, nCount)) {           /* 0x58A54 */
        free(pPanel);
        return;
    }
    *pEnv->ppPanel = pPanel;

    pPanel->nStyle   = 5;                          /* 0x58A7C */
    pPanel->nFont    = 16;                         /* 0x58A88 */
    pPanel->pfnClick = pEnv->pfnBuyThunk;          /* 0x58A9C, the +3020 handler */
    pPanel->nPosX    = 120;                        /* 0x58AB0 */
    pPanel->nPosY    = 234;                        /* 0x58AC4 */
    pPanel->nUnk7052 = 5;                          /* 0x58AD4 */
    pPanel->nUnk7056 = 234;                        /* 0x58AE8 */
    pPanel->nViewW   = 230;                        /* 0x58AFC */
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);                        /* 0x58B0C */
    UiPanel_SetBytes6to11(pPanel, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B);    /* 0x58B38 */
    UiPanel_SetBytes3to5(pPanel, 0x46, 0x96, 0xAA);                        /* 0x58B4C */
    pPanel->bUnk7088  = 0x96;                      /* 0x58B60, the byte -106 */
    pPanel->bFlag7073 = 1;                         /* 0x58B74 */
    pPanel->bFlag7072 = 0;                         /* 0x58B88 */
    pPanel->nUnk7076  = 1;                         /* 0x58B9C */

    nAux = 1;
    for (i = 0; i < PUFO_BASEFAC_COUNT - 1; ++i) { /* 0x58BAC..0x58C0C: records 1..16 */
        const BaseFacFlag *pFac = &pEnv->paFac[i + 1];
        if (pFac->bFlag20 != 0) {
            UiPanel_SetLineText(pPanel, nRows, pFac->szName);
            UiPanel_SetLineAux(pPanel, nRows, nAux); /* the record number is the facility kind */
            ++nRows;
        }
        ++nAux;
    }

    /* 0x58C18: the 17th row, read from aBaseFacFlags[17] -- the four bytes at 0x187024, which is
     * a UiControl pointer slot rather than a facility record (bugs.tsv). */
    UiPanel_SetLineText(pPanel, nRows, (pEnv->pszLastRowName != NULL) ? pEnv->pszLastRowName : "");
    UiPanel_SetLineAux(pPanel, nRows, 0);          /* 0x58C50 */
    pPanel->nLineCount -= 1;                       /* 0x58C68 */

    if (pEnv->pbNewBaseMode != NULL && *pEnv->pbNewBaseMode != 0) {
        const i32 nBase = (i32)((pEnv->pnSelectedBaseIdx != NULL) ? *pEnv->pnSelectedBaseIdx : 0);
        if (pEnv->pnPanelSel != NULL) {
            *pEnv->pnPanelSel = nRows;             /* 0x58C88 */
        }
        if (pEnv->pbArmed != NULL) {
            *pEnv->pbArmed = 1;                    /* 0x58C94 */
        }
        for (i = 0; i < 6; ++i) {                  /* 0x58C9C..0x58D74: the whole 6 by 6 base */
            i32 k;
            for (k = 0; k < 6; ++k) {
                const i32 nCell = 6 * i + k;
                const i32 nFac = PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase;
                pEnv->abFacilityGrid[PUFO_GRID_DAYS_STRIDE * nBase + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_TILEA_OFF + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_TILEB_OFF + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_LAYER2_OFF + nCell] = 0;
                pEnv->abFacilityGrid[nFac + PUFO_GRID_FAC_TYPE_OFF + nCell] = PUFO_FAC_TYPE_NONE;
            }
        }
    }
}

/* --- BaseFacScreen_TryBuySelected @ 0x58910 --- */

i32 BaseFacScreen_TryBuySelected(const BaseFacBuyEnv *pEnv)
{
    i32 nSel;
    i32 nKind;
    u32 uCost;
    i32 nLeft;

    if (pEnv == NULL || pEnv->pPanel == NULL || pEnv->paFac == NULL || pEnv->pnFunds == NULL) {
        return 0;
    }

    nSel = pEnv->pPanel->nUnk3032;                 /* 0x58920, +0xBD8 */
    nKind = UiPanel_GetLineAux(pEnv->pPanel, nSel);/* 0x58938, anLineAux[sel] */
    if (nKind < 0 || nKind >= PUFO_BASEFAC_COUNT) {
        /* Deviation: the original indexes aBaseFacFlags with the unchecked aux value (0x5894C).
         * The port refuses the purchase instead of reading past the table. */
        return 0;
    }
    uCost = pEnv->paFac[nKind].uCost;              /* 0x5894C..0x58954, record + 0x18 */
    nLeft = *pEnv->pnFunds - (i32)uCost;           /* 0x58958 */

    if (nLeft < 0) {
        if (pEnv->pdwShortfall != NULL) {
            *pEnv->pdwShortfall = 1;               /* 0x58960, a dword store into a byte global */
        }
        if (pEnv->pnResidue != NULL) {
            *pEnv->pnResidue = 0;                  /* 0x5896C */
        }
        return 0;
    }

    if (pEnv->pbArmed != NULL) {
        *pEnv->pbArmed = 1;                        /* 0x5897C, arms the placement */
    }
    if (pEnv->pbNewBaseMode != NULL && *pEnv->pbNewBaseMode == 0 && pEnv->pnPanelSel != NULL) {
        *pEnv->pnPanelSel = nSel;                  /* 0x58994 */
    }
    return 1;
}

/* --- SoldierScreen_Toggle @ 0x64af0 --- */

static UiPanel *SoldierScreen_AllocPanel(i32 nLineCount)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel)); /* 0x64B78: operator new(0x1BB4) */

    if (pPanel == NULL) {
        return NULL;
    }
    if (!UiPanel_ctor(pPanel, nLineCount)) {
        free(pPanel);
        return NULL;
    }
    return pPanel;
}

static void SoldierScreen_FreePanel(UiPanel **ppPanel)
{
    UiPanel *pPanel;

    if (ppPanel == NULL) {
        return;
    }
    pPanel = *ppPanel;
    if (pPanel != NULL) {
        UiPanel_FreeLines(pPanel);
        free(pPanel);
        *ppPanel = NULL;
    }
}

void SoldierScreen_Toggle(const SoldierScreenEnv *pEnv)
{
    UiPanel *pEquip;
    UiPanel *pStores;
    UiPanel *pNames;
    UiPanel *pCraft;
    PadImage *pPad;
    Soldier *const *apSoldiers;
    i32 nCount;
    i32 nRows;
    i32 i;

    if (pEnv == NULL || pEnv->pbOpen == NULL || pEnv->pnScreenId == NULL) {
        return;
    }

    Sound_Play(17);                                /* 0x64AFC */
    *pEnv->pnScreenId = 4;                         /* 0x64B08 */

    if (*pEnv->pbOpen != 0) {
        /* The close branch (0x651BC..0x65288). */
        *pEnv->pnScreenId = 1;                     /* 0x651C4 */
        *pEnv->pbOpen = 0;                         /* 0x651CC */
        if (pEnv->ppPanelEquip != NULL && *pEnv->ppPanelEquip != NULL) {
            (*pEnv->ppPanelEquip)->nLineCount = 4; /* 0x651D4, unguarded in the original */
        }
        if (pEnv->ppPanelStores != NULL && *pEnv->ppPanelStores != NULL) {
            (*pEnv->ppPanelStores)->nLineCount = 4;/* 0x651DC, unguarded in the original */
        }
        SoldierScreen_FreePanel(pEnv->ppPanelEquip);
        SoldierScreen_FreePanel(pEnv->ppPanelStores);
        if (pEnv->ppPad != NULL && *pEnv->ppPad != NULL) {
            pPad = *pEnv->ppPad;
            PadImage_FreeData(pPad);               /* 0x65230 */
            free(pPad);                            /* 0x65238 */
            *pEnv->ppPad = NULL;
        }
        SoldierScreen_FreePanel(pEnv->ppPanelNames);
        SoldierScreen_FreePanel(pEnv->ppPanelCraft);
        return;
    }

    *pEnv->pbOpen = 1;                             /* 0x64B28 */

    pPad = (PadImage *)malloc(sizeof(PadImage));   /* 0x64B30: operator new(0x14) */
    if (pPad != NULL) {
        PadImage_ctor(pPad);                       /* 0x64B40 */
        if (pEnv->pszPadName != NULL) {
            (void)PadImage_Load(pPad, pEnv->pszPadName, 8); /* 0x64B50 */
        }
        pPad->nOriginX = 0;                        /* 0x64B5C */
        pPad->nOriginY = 0;                        /* 0x64B64 */
    }
    if (pEnv->ppPad != NULL) {
        *pEnv->ppPad = pPad;
    }

    pEquip = SoldierScreen_AllocPanel(4);
    if (pEnv->ppPanelEquip != NULL) {
        *pEnv->ppPanelEquip = pEquip;
    }
    if (pEquip != NULL) {
        pEquip->nStyle   = 4;                      /* 0x64BA8 */
        pEquip->nFont    = 18;                     /* 0x64BAC */
        pEquip->pfnClick = pEnv->pfnEquipThunk;    /* 0x64BC0, +3020 */
        pEquip->nPosX    = 55;                     /* 0x64BD4 */
        pEquip->nPosY    = 102;                    /* 0x64BE0 */
        pEquip->nUnk7052 = 46;                     /* 0x64BE8 */
        pEquip->nUnk7056 = 102;                    /* 0x64BF0 */
        pEquip->nViewW   = 167;                    /* 0x64BF8 */
        UiPanel_SetBytes0to2(pEquip, 0x44, 0xF4, 0xFC);                     /* 0x64C08 */
        UiPanel_SetBytes6to11(pEquip, 0xC8, 0xF4, 0xFC, 0x04, 0x46, 0x4B);  /* 0x64C28 */
        UiPanel_SetBytes3to5(pEquip, 0x32, 0xA0, 0xBD);                     /* 0x64C84 */
        pEquip->bFlag7073 = 1;                     /* 0x64C90 */
        pEquip->bUnk7088  = 0xFF;                  /* 0x64C9C, the byte -1 */
    }

    pStores = SoldierScreen_AllocPanel(4);
    if (pEnv->ppPanelStores != NULL) {
        *pEnv->ppPanelStores = pStores;
    }
    if (pStores != NULL) {
        pStores->nStyle   = 4;                     /* 0x64CC8 */
        pStores->nFont    = 18;                    /* 0x64CE0 */
        pStores->nPosX    = 190;                   /* 0x64CE4 */
        pStores->nPosY    = 102;                   /* 0x64CEC */
        pStores->nUnk7052 = 46;                    /* 0x64CF4 */
        pStores->nUnk7056 = 102;                   /* 0x64D00 */
        pStores->nViewW   = 167;                   /* 0x64D08 */
        UiPanel_SetBytes0to2(pStores, 0x44, 0xF4, 0xFC);                    /* 0x64D14 */
        UiPanel_SetBytes6to11(pStores, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B); /* 0x64D24 */
        UiPanel_SetBytes3to5(pStores, 0x32, 0x32, 0xFA);                    /* 0x64D68 */
        pStores->bFlag7072 = 0;                    /* 0x64D78 */
    }

    /* 0x64BA4..0x64CC4: how many soldiers are on the selected base. */
    nCount = 0;
    apSoldiers = pEnv->apSoldiers;
    if (apSoldiers != NULL && pEnv->pnSoldierCount != NULL && pEnv->pnSelectedBaseIdx != NULL) {
        for (i = 0; i < (i32)*pEnv->pnSoldierCount; ++i) {
            if (apSoldiers[i] != NULL &&
                (i32)apSoldiers[i]->nKind == (i32)*pEnv->pnSelectedBaseIdx) {
                ++nCount;
            }
        }
    }

    pNames = SoldierScreen_AllocPanel(nCount);
    if (pEnv->ppPanelNames != NULL) {
        *pEnv->ppPanelNames = pNames;
    }
    if (pNames != NULL) {
        pNames->nStyle   = 13;                     /* 0x64E14 */
        pNames->nFont    = 18;                     /* 0x64E28 */
        pNames->pfnClick = pEnv->pfnSelectThunk;   /* 0x64E38, +3020 */
        pNames->nPosX    = 10;                     /* 0x64E44 */
        pNames->nPosY    = 70;                     /* 0x64E48 */
        pNames->nUnk7052 = 2;                      /* 0x64E4C */
        pNames->nUnk7056 = 70;                     /* 0x64E58 */
        pNames->nViewW   = 234;                    /* 0x64E68 */
        UiPanel_SetBytes0to2(pNames, 0xB4, 0xB4, 0xB4);                     /* 0x64E74 */
        UiPanel_SetBytes6to11(pNames, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);  /* 0x64E88 */
        UiPanel_SetBytes3to5(pNames, 0x32, 0x32, 0xFA);                     /* 0x64EA8 */
        pNames->bFlag7073 = 1;                     /* 0x64EC8 */
        pNames->bUnk7088  = 0x96;                  /* 0x64ED8, the byte -106 */
    }

    pCraft = SoldierScreen_AllocPanel(nCount);
    if (pEnv->ppPanelCraft != NULL) {
        *pEnv->ppPanelCraft = pCraft;
    }
    if (pCraft != NULL) {
        pCraft->nStyle   = 13;                     /* 0x64F1C */
        pCraft->nFont    = 18;                     /* 0x64F20 */
        pCraft->nPosX    = 230;                    /* 0x64F28 */
        pCraft->nPosY    = 70;                     /* 0x64F34 */
        pCraft->nUnk7052 = 5;                      /* 0x64F40 */
        pCraft->nUnk7056 = 70;                      /* 0x64F48 */
        pCraft->nViewW   = 40;                     /* 0x64F54 */
        UiPanel_SetBytes0to2(pCraft, 0xB4, 0xB4, 0xB4);                     /* 0x64F64 */
        UiPanel_SetBytes6to11(pCraft, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);  /* 0x64F74 */
        pCraft->bFlag7073 = 0;                     /* 0x64FB8 */
        pCraft->nUnk7076  = 2;                     /* 0x64FC4 */
        pCraft->bUnk7088  = 0x96;                  /* 0x64FD8, the byte -106 */
    }

    /* 0x65018..0x651A0: the row pass. */
    nRows = 0;
    if (pNames != NULL && pCraft != NULL && apSoldiers != NULL &&
        pEnv->pnSoldierCount != NULL && pEnv->pnSelectedBaseIdx != NULL) {
        for (i = 0; i < (i32)*pEnv->pnSoldierCount; ++i) {
            Soldier *pSol = apSoldiers[i];
            const char *pszName;

            if (pSol == NULL || (i32)pSol->nKind != (i32)*pEnv->pnSelectedBaseIdx) {
                continue;
            }
            UiPanel_SetLineText(pNames, nRows, pSol->szName);       /* 0x65048, soldier + 0xBA */
            UiPanel_SetLineAux(pNames, nRows, i);                   /* 0x65078, the roster index */
            pszName = (pEnv->pfnFormatName != NULL) ? pEnv->pfnFormatName(pEnv->pFormatUser, pSol)
                                                    : "";
            UiPanel_SetLineText(pCraft, nRows, (pszName != NULL) ? pszName : "");
            if (pSol->bUnk183 == 254) {
                UiPanel_SetLineMeta(pNames, nRows, 250, 150, 150, 20, 20, 20);  /* 0x650D0 */
                UiPanel_SetLineMeta(pCraft, nRows, 250, 150, 150, 20, 20, 20);
            } else if (pSol->bUnk183 == 255) {
                UiPanel_SetLineMeta(pNames, nRows, 150, 150, 250, 20, 20, 20);  /* 0x65124 */
                UiPanel_SetLineMeta(pCraft, nRows, 150, 150, 250, 20, 20, 20);
            }
            ++nRows;
        }
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseSoldierThunk;       /* 0x651AC */
    }
}

/* --- SoldierScreen_SelectListed @ 0x64a9c --- */

i32 SoldierScreen_SelectListed(const SoldierSelectEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pPanel == NULL || pEnv->pnCursor == NULL) {
        return 0;
    }
    if (pEnv->pnScreenId != NULL) {
        *pEnv->pnScreenId = 5;                     /* 0x64AA8 */
    }
    *pEnv->pnCursor = UiPanel_GetLineAux(pEnv->pPanel, pEnv->pPanel->nUnk3032); /* 0x64AD0 */
    return *pEnv->pnCursor;
}

/* --- SoldierScreen_EquipSelected @ 0x619cc --- */

i32 SoldierScreen_EquipSelected(const SoldierEquipEnv *pEnv)
{
    Soldier *pSol;
    BaseRec *pBase;
    i32 nCursor;
    i32 nOld;
    i32 nRow;

    if (pEnv == NULL || pEnv->apSoldiers == NULL || pEnv->pnCursor == NULL ||
        pEnv->pnSoldierCount == NULL || pEnv->pPanelEquip == NULL ||
        pEnv->pbTransferFlag == NULL) {
        return 0;
    }

    Sound_Play(17);                                          /* 0x619D8 */

    nCursor = *pEnv->pnCursor;
    if (nCursor < 0 || nCursor >= (i32)*pEnv->pnSoldierCount) {
        return 0;
    }
    pSol = pEnv->apSoldiers[nCursor];                        /* 0x619F4 */
    if (pSol == NULL) {
        return 0;
    }

    /* 0x61A1C: run when the soldier has no craft (the 254/255 sentinels) or when its craft is
     * sitting at a base; a soldier on a craft that is off-base is left alone. */
    if (pSol->bUnk183 < 0xFE) {
        Ship *pCraft;
        if (pEnv->apCraft == NULL || pSol->bUnk183 >= PUFO_FAC_BASE_COUNT) {
            return 0;
        }
        pCraft = pEnv->apCraft[pSol->bUnk183];
        if (pCraft == NULL || pCraft->bAtBase == 0) {
            return 0;
        }
    }

    if (pEnv->apBases == NULL || pEnv->pnSelectedBaseIdx == NULL ||
        *pEnv->pnSelectedBaseIdx < 0 || *pEnv->pnSelectedBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return 0;
    }
    pBase = pEnv->apBases[*pEnv->pnSelectedBaseIdx];         /* index 1117 dwords = Map + 0x1174 */

    nOld = pSol->nUnitType;
    nRow = UiPanel_GetLineAux(pEnv->pPanelEquip, pEnv->pPanelEquip->nUnk3032); /* 0x61A44 */

    switch (nRow) {
    case 0:
        if (nOld != 0) {
            if (pEnv->apObjects != NULL && nOld <= 3) {      /* 0x61B60 */
                BaseRec_AddItemQty(pBase,
                                   pEnv->apObjects[PUFO_SOLDIER_EQUIP_OBJ_BASE + nOld], 1);
            }
            pSol->nUnitType = 0;                             /* 0x61B74 */
        }
        break;
    case 1:
    case 2:
    case 3: {
        ObjectRecFull *pNew;
        pSol->nUnitType = (u8)nRow;                          /* 0x61AF0 / 0x61AE0 / 0x61A6C */
        if (pEnv->apObjects == NULL) {
            break;
        }
        pNew = pEnv->apObjects[PUFO_SOLDIER_EQUIP_OBJ_BASE + nRow];
        if (nOld != 0) {
            if (nOld <= 3) {
                /* BaseRec_AddItemQty with a negated delta is BaseRec_SubItemQty @ 0x5571c. */
                BaseRec_AddItemQty(pBase, pNew, -1);         /* 0x61A80 */
                BaseRec_AddItemQty(pBase,
                                   pEnv->apObjects[PUFO_SOLDIER_EQUIP_OBJ_BASE + nOld], 1); /* 0x61AC4 */
            }
        } else {
            BaseRec_AddItemQty(pBase, pNew, -1);             /* 0x61B04 */
        }
        break;
    }
    default:
        break;
    }

    *pEnv->pbTransferFlag = 0;                               /* 0x61B78 */
    return 1;
}

/* --- SoldierScreen_Draw @ 0x65290 --- */

i32 SoldierScreen_Draw(const SoldierDrawEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pnScreenId == NULL || pEnv->pbOpen == NULL) {
        return 0;
    }

    if (*pEnv->pnScreenId == PUFO_SOL_SCREEN_TEXT_INPUT) {   /* 0x652A4 */
        if (pEnv->pfnTextInput == NULL) {
            return 0;
        }
        return pEnv->pfnTextInput(pEnv->pnCursor != NULL ? pEnv->pnCursor[0] : 0,
                                  pEnv->pTextInputUser);     /* 0x652B0 Ui_TextInputDialog */
    }

    if (*pEnv->pbOpen == 0) {                                /* 0x652CC */
        return 0;
    }

    if (pEnv->pPad != NULL) {
        PadImage_Blit(pEnv->pPad);                           /* 0x652D4 */
    }
    if (pEnv->pPanelNames != NULL) {
        UiPanel_DrawText(pEnv->pPanelNames);                 /* 0x652E0 */
    }
    if (pEnv->pPanelCraft != NULL) {
        UiPanel_DrawText(pEnv->pPanelCraft);                 /* 0x652EC */
    }

    SoftSurf_BlitRectProxy(0, 69, 23, 97, 1,
                           PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT);   /* 0x65318 */
    SoftSurf_BlitRectProxy(0, 69, 44, 97, 1,
                           PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK);      /* 0x65340 */
    SoftSurf_BlitRectProxy(0, 69, 24, 1, 21,
                           PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK, PUFO_SOL_BEVEL_DARK);      /* 0x65368 */
    SoftSurf_BlitRectProxy(0, 166, 23, 1, 22,
                           PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT, PUFO_SOL_BEVEL_LIGHT);   /* 0x65390 */

    /* The side rails: 24 steps, i runs 174..312 while the climbing half runs 5..166 in jumps of 7. */
    for (i32 k = 0, yUp = 5, i = 174; i < 318; ++k, yUp += 7, i += 6) {                          /* 0x6539C */
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));                   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_SIDE - PUFO_SOL_RAMP_STEP * k);                     /* 230..115 */

        SoftSurf_BlitRectProxy(0, 2, yUp, 1, 8, nRise, nRise, nRise);                           /* 0x653D8 */
        SoftSurf_BlitRectProxy(0, 2, i, 1, 6, nFall, nFall, nFall);                             /* 0x65408 */
        SoftSurf_BlitRectProxy(0, 235, yUp, 1, 8, nRise, nRise, nRise);                         /* 0x65430 */
        SoftSurf_BlitRectProxy(0, 235, i, 1, 6, nFall, nFall, nFall);                           /* 0x65458 */
    }

    /* The top and bottom rails: 24 steps of 5, the left half climbing and the right half falling. */
    for (i32 j = 0, k = 0; j < 120; j += PUFO_SOL_RAMP_STEP, ++k) {                             /* 0x6547C */
        const i32 nXLo = j + 1;
        const i32 nXHi = j + 119;
        const u8 nRise = (u8)(PUFO_SOL_RAMP_STEP * (k + PUFO_SOL_RAMP_BASE));                   /* 100..215 */
        const u8 nFall = (u8)(PUFO_SOL_RAMP_EDGE - PUFO_SOL_RAMP_STEP * k);                     /* 250..135 */

        SoftSurf_BlitRectProxy(0, nXLo, 5, 5, 1, nRise, nRise, nRise);                          /* 0x654B0 */
        SoftSurf_BlitRectProxy(0, nXLo, 315, 5, 1, nRise, nRise, nRise);                        /* 0x654D8 */
        SoftSurf_BlitRectProxy(0, nXHi, 5, 5, 1, nFall, nFall, nFall);                          /* 0x65508 */
        SoftSurf_BlitRectProxy(0, nXHi, 315, 5, 1, nFall, nFall, nFall);                        /* 0x65530 */
    }

    return 0;
}

/* --- BaseFacScreen_DrawPlacementOverlay @ 0x58e10 --- */

/* The ARM keeps one flat pointer per layer (grid + 0x174 + 144*base for the fac block, grid + 36*base
 * for buildDays) and adds small deltas to it, so a 2x2 at the last row reads on into the next layer.
 * Both stay inside the 1524-byte grid for every tap the in-bounds test can produce. */
static u8 FacOverlay_TileA(const u8 *pGrid, i32 nBase, i32 nRow, i32 nCol)
{
    return pGrid[PUFO_GRID_FAC_OFF + PUFO_GRID_FAC_STRIDE * nBase + 6 * nRow + nCol];
}

static u8 FacOverlay_Days(const u8 *pGrid, i32 nBase, i32 nRow, i32 nCol)
{
    return pGrid[PUFO_GRID_DAYS_STRIDE * nBase + 6 * nRow + nCol];
}

/* The four-side walk (0x592D0..0x5957C). A neighbour only counts when its buildDays byte is zero. */
static i32 FacOverlay_Score(const FacOverlayEnv *pEnv, i32 nSpan, i32 nTapX, i32 nTapY)
{
    const u8 *pGrid = pEnv->abFacilityGrid;
    i32 nBase = pEnv->nSelectedBaseIdx;
    i32 nLimit = 5 - (nSpan / PUFO_FAC_OVERLAY_SPAN);
    i32 nFoot;
    i32 nScore = 0;

    if (nSpan != 0) {
        nFoot = FacOverlay_TileA(pGrid, nBase, nTapY, nTapX)
              + FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX)
              + FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX + 1)
              + FacOverlay_TileA(pGrid, nBase, nTapY, nTapX + 1);
    } else {
        nFoot = FacOverlay_TileA(pGrid, nBase, nTapY, nTapX);
    }
    nFoot &= 0xFF;                                           /* 0x592CC AND #0xFF */
    if (nFoot != 0) {
        return 0;                                            /* the footprint is occupied */
    }

    if (nTapY > 0) {                                         /* 0x592C8 CMP R1 -- the ROW */
        if (FacOverlay_Days(pGrid, nBase, nTapY - 1, nTapX) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY - 1, nTapX);
        if (nSpan != 0 && FacOverlay_Days(pGrid, nBase, nTapY - 1, nTapX + 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY - 1, nTapX + 1);
    }
    if (nTapY < nLimit) {
        if (FacOverlay_Days(pGrid, nBase, nTapY + 1, nTapX) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX);
        if (nSpan != 0) {
            if (FacOverlay_Days(pGrid, nBase, nTapY + 2, nTapX) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 2, nTapX);   /* v19[384] */
            if (FacOverlay_Days(pGrid, nBase, nTapY + 2, nTapX + 1) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 2, nTapX + 1); /* v19[385] */
        }
    }
    if (nTapX > 0) {
        if (FacOverlay_Days(pGrid, nBase, nTapY, nTapX - 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY, nTapX - 1);
        if (nSpan != 0 && FacOverlay_Days(pGrid, nBase, nTapY + 1, nTapX - 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX - 1);
    }
    if (nTapX < nLimit) {
        if (FacOverlay_Days(pGrid, nBase, nTapY, nTapX + 1) == 0)
            nScore += FacOverlay_TileA(pGrid, nBase, nTapY, nTapX + 1);
        if (nSpan != 0) {
            if (FacOverlay_Days(pGrid, nBase, nTapY, nTapX + 2) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY, nTapX + 2);
            if (FacOverlay_Days(pGrid, nBase, nTapY + 1, nTapX + 2) == 0)
                nScore += FacOverlay_TileA(pGrid, nBase, nTapY + 1, nTapX + 2);
        }
    }
    return nScore & 0xFF;                                    /* every accumulate is a STRB */
}

static void FacOverlay_Outline(const FacOverlayEnv *pEnv, i32 nSpan, i32 nTapX, i32 nTapY,
                               u8 nColor, bool bOk)
{
    i32 nX = pEnv->nOriginX + 32 * nTapX;
    i32 nY = pEnv->nOriginY + 32 * nTapY + 36;
    i32 nW = (nSpan != 0) ? 64 : 32;
    i32 nH = (nSpan != 0) ? 64 : 32;
    u8 nIdx = bOk ? 0 : nColor;
    u8 nLow = bOk ? nColor : 0;

    SoftSurf_BlitRectProxy(0, nX, nY, nW, 2, nIdx, 0, nLow);            /* top */
    SoftSurf_BlitRectProxy(0, nX, nY, 2, nH, nIdx, 0, nLow);            /* left */
    SoftSurf_BlitRectProxy(0, nX + nW - 1, nY, 2, nH, nIdx, 0, nLow);   /* right */
    SoftSurf_BlitRectProxy(0, nX, nY + nH - 1, nW, 2, nIdx, 0, nLow);   /* bottom */
}

i32 BaseFacScreen_DrawPlacementOverlay(const FacOverlayEnv *pEnv)
{
    i32 nSpan;
    i32 nTapX;
    i32 nTapY;
    i32 nColor;
    i32 nScore;
    i32 nKind;
    u8 *pGrid;

    if (pEnv == NULL || pEnv->abFacilityGrid == NULL || pEnv->pPanel == NULL ||
        pEnv->paFac == NULL || pEnv->pnFunds == NULL || pEnv->pnTapX == NULL ||
        pEnv->pnTapY == NULL || pEnv->pbPreviewActive == NULL || pEnv->pbPreviewOk == NULL ||
        pEnv->pbOverlayShown == NULL || pEnv->pbNewBaseMode == NULL) {
        return 0;
    }

    pGrid = pEnv->abFacilityGrid;

    nKind = UiPanel_GetLineAux(pEnv->pPanel, pEnv->nPanelSel);          /* 0x58E20 */
    if (nKind < 0 || nKind >= PUFO_BASEFAC_COUNT) {
        return 0;
    }

    nSpan = (pEnv->paFac[nKind].bFlag21 == PUFO_FAC_TYPE_HANGAR) ? PUFO_FAC_OVERLAY_SPAN : 0;
    nColor = (u8)(10 * ((i32)pEnv->nPhaseByte + 15));                   /* 0x5909C..0x590AC */

    nTapX = pEnv->nPendingX;                                            /* 0x58E58 */
    nTapY = pEnv->nPendingY;                                            /* 0x58E60 */
    *pEnv->pnTapX = nTapX;
    *pEnv->pnTapY = nTapY;

    if (*pEnv->pbPreviewActive != 0) {                                  /* 0x58E8C..0x58EC8 */
        nTapX = ((i32)pEnv->nCursorX - pEnv->nOriginX) / 32;
        nTapY = ((i32)pEnv->nCursorY - pEnv->nOriginY - 36) / 32;
        *pEnv->pnTapX = nTapX;
        *pEnv->pnTapY = nTapY;
    }

    /* The clamped outline for a 2x2 that would hang off the last row or column (0x58EDC..0x59168).
     * Always the invalid colour. */
    if (nTapX <= 5 && nTapY <= 5) {
        i32 nX = pEnv->nOriginX + 32 * nTapX;
        i32 nY = pEnv->nOriginY + 32 * nTapY + 36;

        if (nTapY == 5 && nSpan != 0) {
            if (nTapX == 5) {
                SoftSurf_BlitRectProxy(0, pEnv->nOriginX + 160, pEnv->nOriginY + 196, 32, 2,
                                       nColor, 0, 0);
                SoftSurf_BlitRectProxy(0, nX, nY, 2, 32, nColor, 0, 0);
            } else {
                SoftSurf_BlitRectProxy(0, nX, pEnv->nOriginY + 196, 64, 2, nColor, 0, 0);
                SoftSurf_BlitRectProxy(0, nX, nY, 2, 32, nColor, 0, 0);
                SoftSurf_BlitRectProxy(0, nX + 63, nY, 2, 32, nColor, 0, 0);
            }
        } else if (nTapX == 5 && nSpan != 0) {
            SoftSurf_BlitRectProxy(0, pEnv->nOriginX + 160, nY, 32, 2, nColor, 0, 0);
            SoftSurf_BlitRectProxy(0, nX, nY, 2, 64, nColor, 0, 0);
            SoftSurf_BlitRectProxy(0, nX, pEnv->nOriginY + 32 * nTapY + 99, 32, 2, nColor, 0, 0);
        }
    }

    nScore = 0;

    if (*pEnv->pbOverlayShown != 0 || *pEnv->pbPreviewActive != 0) {    /* 0x5917C */
        i32 nDx = (i32)pEnv->nCursorX - pEnv->nOriginX;
        i32 nDy = (i32)pEnv->nCursorY - pEnv->nOriginY;

        if (nDx > 0 && nDx < 192 - nSpan && nDy > 36 && nDy < 227 - nSpan) {
            *pEnv->pbOverlayShown = 1;                                  /* 0x59234 */
            if (nTapX >= 0 && nTapX <= 5 && nTapY >= 0 && nTapY <= 5) {
                nScore = FacOverlay_Score(pEnv, nSpan, nTapX, nTapY);
                if (*pEnv->pbNewBaseMode != 0) {
                    nScore = 1;                                         /* 0x597A0 */
                }
                FacOverlay_Outline(pEnv, nSpan, nTapX, nTapY, nColor, nScore != 0);
            }
        }

        if (nScore == 0) {                                              /* 0x599A0 */
            *pEnv->pbPreviewOk = 0;
            return 0;
        }
        if (*pEnv->pbPreviewOk == 0) {
            return 0;
        }
        if (nTapX != pEnv->nPendingX || nTapY != pEnv->nPendingY) {
            return 0;
        }
        if (*pEnv->pbPreviewActive == 0) {
            return 0;
        }

        {
            i32 nCost = (i32)pEnv->paFac[nKind].uCost;                 /* 0x59A1C */

            if (*pEnv->pnFunds - nCost < 0) {                          /* 0x59A24 */
                if (pEnv->pdwShortfall != NULL) {
                    *pEnv->pdwShortfall = 1;
                }
                if (pEnv->pnResidue != NULL) {
                    *pEnv->pnResidue = 0;
                }
                return 0;
            }

            (void)BaseFac_SetCellBuildState(pGrid, pEnv->nSelectedBaseIdx,
                                            pEnv->paFac[nKind].bFlag21, nTapX, (i8)nTapY, 0);
            *pEnv->pnFunds -= nCost;                                   /* 0x59AB4 */
        }

        if (*pEnv->pbNewBaseMode == 0) {
            return 1;
        }

        if (pEnv->pTextRecs != NULL && pEnv->pszLabel != NULL && pEnv->paRegionBuildCount != NULL) {
            (void)BaseFac_FormatCostLabel(pEnv->pszLabel, pEnv->cchLabel, pEnv->pTextRecs,
                                          pEnv->nPendingRegionId, pEnv->paRegionBuildCount);
        }
        if (pEnv->pCreateEnv != NULL) {
            (void)Base_CreateNamedEntry(pEnv->pCreateEnv, pEnv->nPlaceX, pEnv->nPlaceY,
                                        (i32)pEnv->nPendingRegionId, pEnv->pszLabel);
        }
        if (pEnv->paNewBaseCost != NULL && pEnv->nPendingRegionId >= 0x80 &&
            (i32)(pEnv->nPendingRegionId - 0x80) < PUFO_NEW_BASE_COST_COUNT) {
            *pEnv->pnFunds -= pEnv->paNewBaseCost[pEnv->nPendingRegionId - 0x80];
        }
        if (pEnv->pToggleEnv != NULL) {
            BaseFacScreen_Toggle(pEnv->pToggleEnv);
        }
        *pEnv->pbPreviewActive = 0;
        if (pEnv->pnScreenId != NULL) {
            *pEnv->pnScreenId = 1;
        }
        *pEnv->pbNewBaseMode = 0;
        *pEnv->pbPreviewOk = 0;
        return 1;
    }

    return 0;
}

/* --- BaseFacScreen_DrawBuildDialog @ 0x59b60 --- */

/* 0x59CEC and 0x5A168: MOVS, ADDMI #1, ASR#1. A negative width is nudged up by one before the
 * halving, which is what the ARM's arithmetic shift then does to it. */
static i32 BuildDialog_HalfWidth(i32 nWidth)
{
    if (nWidth < 0) {
        nWidth += 1;
    }
    return nWidth / 2;
}

/* The label copy loops (0x59BCC, 0x5A02C, 0x66580, 0x66650): LDRSB/STRB until the terminator, with
 * no bound on the destination. The port stops at the caller's buffer end. */
static void BaseUi_CopyLabel(char *pszDst, size_t cchDst, const char *pszSrc)
{
    size_t i = 0;

    if (pszSrc == NULL) {
        pszDst[0] = 0;
        return;
    }
    while (i + 1 < cchDst && pszSrc[i] != 0) {
        pszDst[i] = pszSrc[i];
        ++i;
    }
    pszDst[i] = 0;
}

/* The eight runtime-loaded labels may be absent in the port's env; the original always has bytes at
 * those addresses, possibly just a terminator. */
static void BuildDialog_DrawLabel(i32 x, i32 y, const char *psz)
{
    if (psz != NULL) {
        Text_DrawString(x, y, psz);
    }
}

/* 0x59BAC..0x59C1C and 0x5A078..0x5A0E4: the funds label, the funds value and a dollar sign,
 * drawn at (5, 20). */
static void BuildDialog_DrawFundsLine(const BuildDialogEnv *pEnv, char *psz)
{
    char szNum[16];

    BaseUi_CopyLabel(psz, PUFO_BUILD_LABEL_MAX, pEnv->pszFundsPrefix);
    snprintf(szNum, sizeof(szNum), "%d", *pEnv->pnFunds); /* j_itoa (0x9e9f0), base 10 */
    (void)pufo_strcat(psz, szNum, PUFO_BUILD_LABEL_MAX);
    (void)pufo_strcat(psz, "$", PUFO_BUILD_LABEL_MAX);    /* asc_AB1D8 @ 0xab1d8 */
    Text_DrawString(5, 20, psz);
}

i32 BaseFacScreen_DrawBuildDialog(const BuildDialogEnv *pEnv)
{
    char szLabel[PUFO_BUILD_LABEL_MAX]; /* psz @ sp+0x18 */
    const BaseFacFlag *pRec;
    i32 nKind;
    i32 nWidth;

    if (pEnv == NULL || pEnv->pbDialogOpen == NULL || *pEnv->pbDialogOpen == 0) {
        return 0; /* 0x59B6C: a closed dialog draws nothing at all */
    }
    if (pEnv->pnFunds == NULL) {
        return 0;
    }

    if (pEnv->pbPlaceArmed != NULL && *pEnv->pbPlaceArmed != 0) {
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 130, 130, 250, 20, 20, 90); /* 0x59BAC */
        BuildDialog_DrawFundsLine(pEnv, szLabel);

        (void)Ui_AnimExpandBlit(pEnv->pAnim, 0, 228, 236, 85, 4, 0x46, 0x4B, 0xC8, 0); /* 0x59C60 */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 50, 255, 50, 0, 20, 0);                /* 0x59C80 */
        nKind = (pEnv->pPanel != NULL && pEnv->paFac != NULL)
                    ? UiPanel_GetLineAux(pEnv->pPanel, pEnv->nPanelSel)                /* 0x59CB4 */
                    : -1;
        pRec = (nKind >= 0 && nKind < PUFO_BASEFAC_COUNT) ? &pEnv->paFac[nKind] : NULL;

        if (pRec != NULL) {
            BaseUi_CopyLabel(szLabel, PUFO_BUILD_LABEL_MAX, pRec->szName);
            nWidth = Text_MeasureWidth(szLabel);                                       /* 0x59CEC */
            Text_DrawString(120 - BuildDialog_HalfWidth(nWidth), 236, pRec->szName);  /* 0x59D0C */
        }

        BuildDialog_DrawLabel(6, 257, pEnv->pszRowCost);    /* 0x59D2C, unk_B7B47 */
        BuildDialog_DrawLabel(6, 275, pEnv->pszRowBuild);   /* 0x59D4C, unk_B7C14 */
        BuildDialog_DrawLabel(6, 293, pEnv->pszRowNewBase); /* 0x59D6C, unk_B7CE1 */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 50, 150, 255, 0, 0, 20);               /* 0x59D8C */
        if (pRec != NULL) {
            snprintf(szLabel, sizeof(szLabel), "%d", (i32)pRec->uCost);               /* 0x59DB4 */
            Text_DrawString(185, 257, szLabel);                                        /* 0x59DCC */
        }
        Text_DrawString(185, 293, "50000"); /* 0x59DE4, a50000 @ 0xab260: the new-base row */

        if (pRec != NULL) {
            snprintf(szLabel, sizeof(szLabel), "%d",
                     (i32)FacBuildSpec_GetDays(pRec->bFlag21));                       /* 0x59E20 */
            (void)pufo_strcat(szLabel, " ", PUFO_BUILD_LABEL_MAX);                    /* 0x59E2C */
            (void)pufo_strcat(szLabel, pEnv->pszDaysSuffix, PUFO_BUILD_LABEL_MAX);    /* 0x59E40 */
        }
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 50, 150, 255, 0, 0, 20);               /* 0x59E60 */
        Text_DrawString(185, 275, szLabel);                                           /* 0x59E78 */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);              /* 0x59EA4 */
        BuildDialog_DrawLabel(6, 4, pEnv->pszTitleArmed);                             /* 0x59EC0 */
        (void)UiControl_BlitActivePad(pEnv->pCtl, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlUser);            /* 0x59ECC */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30);              /* 0x59EF4 */
        (void)BaseFacScreen_DrawPlacementOverlay(pEnv->pOverlayEnv);                  /* 0x59EF8 */
    } else {
        if (pEnv->pbOverlayShown != NULL) {
            *pEnv->pbOverlayShown = 0;                                               /* 0x59FAC */
        }
        (void)Ui_AnimExpandBlit(pEnv->pAnim, 0, 228, 236, 85, 4, 0x46, 0x4B, 0xC8, 0); /* 0x59FE0 */
        if (pEnv->pPanel != NULL) {
            UiPanel_DrawText(pEnv->pPanel);                                          /* 0x59FEC */
        }
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);             /* 0x5A00C */
        BaseUi_CopyLabel(szLabel, PUFO_BUILD_LABEL_MAX, pEnv->pszTitleList);
        Text_DrawString(6, 4, szLabel);                                              /* 0x5A050 */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 130, 130, 250, 20, 20, 90);           /* 0x5A078 */
        BuildDialog_DrawFundsLine(pEnv, szLabel);
        (void)UiControl_BlitActivePad(pEnv->pCtl, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlUser);            /* 0x5A0F0 */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30);             /* 0x5A118 */
    }

    /* 0x5A138: the tail both branches share, reached only when a purchase was refused. */
    if (pEnv->pdwShortfall != NULL && *pEnv->pdwShortfall == 1
        && pEnv->pnResidue != NULL && *pEnv->pnResidue == 0) {
        (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1);         /* 0x5A138 */
        if (pEnv->pszShortfall != NULL) {
            nWidth = Text_MeasureWidth(pEnv->pszShortfall);                          /* 0x5A168 */
            Text_DrawString(120 - BuildDialog_HalfWidth(nWidth), 158,
                            pEnv->pszShortfall);                                     /* 0x5A188 */
        }
    }

    return 1;
}

/* --- Soldier_FormatDisplayName @ 0x6654c --- */

/* aTextRecs @ 0xB38D0 record indices. 93 is the "no craft" label the 0xFF sentinel gets and 140 the
 * label the 0xFE sentinel gets; 137, 138 and 139 are the three craft types this function names.
 * Ship_FormatDisplayName (0x666cc) uses the same three plus 418 and 419 for the two types that are
 * named on the craft side only. */
#define PUFO_SOLDIER_CRAFT_REC_BASE 137
#define PUFO_SOLDIER_NO_CRAFT_REC   93
#define PUFO_SOLDIER_UNASSIGNED_REC 140

char *Soldier_FormatDisplayName(const Soldier *pSol, Ship *const *apCraft, const u8 *pTextRecs,
                                char *pszOut, size_t cchOut)
{
    char szNum[16];
    Ship *pShip;
    u8 nCraft;

    if (pszOut == NULL || cchOut == 0) {
        return pszOut;
    }
    pszOut[0] = 0;
    if (pSol == NULL) {
        return pszOut;
    }
    nCraft = pSol->bUnk183;

    if (nCraft == 0xFF) {                                                          /* 0x66560 */
        if (pTextRecs != NULL) {
            BaseUi_CopyLabel(pszOut, cchOut,
                             (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE
                                            * PUFO_SOLDIER_NO_CRAFT_REC));
        }
        return pszOut;                                                             /* 0x66590 */
    }

    if (nCraft < 0xFE) {                                                           /* 0x665AC */
        if (apCraft != NULL && pTextRecs != NULL) {
            pShip = apCraft[nCraft];
            if (pShip != NULL && pShip->bCraftType < 3) {                          /* 0x665CC */
                BaseUi_CopyLabel(pszOut, cchOut,
                                 (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE
                                                * (PUFO_SOLDIER_CRAFT_REC_BASE
                                                   + pShip->bCraftType)));
                snprintf(szNum, sizeof(szNum), "%d", (i32)pShip->bType);           /* 0x66664 */
                (void)pufo_strcat(pszOut, szNum, cchOut);                          /* 0x66688 */
            }
        }
    }

    if (nCraft == 0xFE) {                                                          /* 0x66694 */
        if (pTextRecs != NULL) {
            BaseUi_CopyLabel(pszOut, cchOut,
                             (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE
                                            * PUFO_SOLDIER_UNASSIGNED_REC));
        }
    }
    return pszOut;                                                                 /* 0x666C8 */
}

/* ------------------------------------------------- the options screen's audio tab */

/* stru_AB750 @ 0xAB750: seven identical UiControlDesc records, the options screen's list rows.
 * stru_AB830 @ 0xAB830: twelve identical records, the volume radio buttons. Both tables are uniform
 * repeats of one record (dumped by tools/gen_geoiinit_tables.py), so the port keeps one each. */
static const UiControlDesc g_aOptionsListDesc = { 1, 229, "firebtn", "cfirebtn", "", 0 };
static const UiControlDesc g_aOptionsVolDesc  = { 1, 229, "initg", "cinitg", "cinitg", 0 };

/* The level the two appliers take, from Sound_ApplyMusicVolume @ 0x9E470/0x9E488:
 * 4 * (muted ? 0 : 12 * step + 4) - 1. Muted yields -1, which is the original's arithmetic. */
static i32 Options_MapLevel(i32 nStep, u8 bMuted)
{
    i32 v = (bMuted != 0) ? 0 : 12 * nStep + 4;

    return 4 * v - 1;
}

/* ORIGINAL BUG (0x6B93C / 0x6B984), not reproduced: the original indexes the 0x159E44 / 0x159E5C
 * arrays with the stored step and tests nothing, so a step outside 0..5 reads past the six pointers
 * and writes 2 into whatever it lands on. The port picks button 0 instead. */
static i32 Options_ClampStep(i32 nStep)
{
    return (nStep < 0 || nStep >= PUFO_OPTIONS_VOL_COUNT) ? 0 : nStep;
}

static void Options_SelectRadio(UiControl *pBtn)
{
    if (pBtn != NULL) {
        pBtn->nState = PUFO_CONTROL_MAX_STATE; /* 2: the selected state of a radio group */
    }
}

static void Options_SetClick(UiControl *pCtl, void (*pfn)(void))
{
    if (pCtl != NULL) {
        pCtl->pfnClick = pfn;
    }
}

/* Centre one list row on the 240-pixel field and drop it at nY. */
static void Options_CentreRow(UiControl *pCtl, i32 nY)
{
    if (pCtl != NULL) {
        pCtl->x = (i16)(120 - (pCtl->uWidth >> 1)); /* 0x6B718 / 0x6B514 */
        pCtl->y = (i16)nY;                          /* 0x6B73C / 0x6B538 */
    }
}

i32 OptionsScreen_SetMusicVol(OptionsVolumeEnv *pEnv, i32 nStep)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pnMusicVol == NULL || pEnv->pbMusicOff == NULL) {
        return 0;
    }

    *pEnv->pnMusicVol = nStep;                                            /* 0x6BA28 / 0x6BAD0 */
    OptionsScreen_ClearMusicButtons(pEnv->pCtl->apMusic);                 /* 0x6BA2C */
    Options_SelectRadio(pEnv->pCtl->apMusic[Options_ClampStep(nStep)]);   /* 0x6BA50 */
    *pEnv->pbMusicOff = (nStep == 0) ? 1 : 0;                             /* 0x6BA5C / 0x6BAA8 */
    Sound_ApplyMusicVolume(Options_MapLevel(*pEnv->pnMusicVol, *pEnv->pbMusicOff),
                           *pEnv->pbMusicOff);                             /* 0x9e454, the tail call */
    return 0;
}

i32 OptionsScreen_SetSfxVol(OptionsVolumeEnv *pEnv, i32 nStep)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pnSfxVol == NULL || pEnv->pbSfxOff == NULL) {
        return 0;
    }

    OptionsScreen_ClearSfxButtons(pEnv->pCtl->apSfx);                     /* 0x6BC20 */
    *pEnv->pnSfxVol = nStep;                                              /* 0x6BC38 */
    Options_SelectRadio(pEnv->pCtl->apSfx[Options_ClampStep(nStep)]);     /* 0x6BC48 */
    *pEnv->pbSfxOff = (nStep == 0) ? 1 : 0;                               /* 0x6BC54 / 0x6BCB8 */
    Sound_ApplySfxVolume(Options_MapLevel(*pEnv->pnSfxVol, *pEnv->pbSfxOff),
                         *pEnv->pbSfxOff);                                /* 0x6BC58 */
    Sound_Play(17);                                                       /* 0x6BC64: hear the new level */
    return 0;
}

i32 OptionsScreen_SetMusicVol0(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 0); /* 0x6ba14: step 0 is the mute step */
}

i32 OptionsScreen_SetMusicVol1(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 1); /* 0x6ba70 */
}

i32 OptionsScreen_SetMusicVol2(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 2); /* 0x6babc */
}

i32 OptionsScreen_SetMusicVol3(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 3); /* 0x6bb18 */
}

i32 OptionsScreen_SetMusicVol4(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 4); /* 0x6bb74 */
}

i32 OptionsScreen_SetMusicVol5(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetMusicVol(pEnv, 5); /* 0x6bbd0 */
}

i32 OptionsScreen_SetSfxVol0(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 0); /* 0x6bc1c: step 0 is the mute step */
}

i32 OptionsScreen_SetSfxVol1(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 1); /* 0x6bc80 */
}

i32 OptionsScreen_SetSfxVol2(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 2); /* 0x6bce4 */
}

i32 OptionsScreen_SetSfxVol3(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 3); /* 0x6bd48 */
}

i32 OptionsScreen_SetSfxVol4(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 4); /* 0x6bdac */
}

i32 OptionsScreen_SetSfxVol5(OptionsVolumeEnv *pEnv)
{
    return OptionsScreen_SetSfxVol(pEnv, 5); /* 0x6be10 */
}

i32 OptionsScreen_RefreshVolumeHighlight(OptionsVolumeEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pnMusicVol == NULL || pEnv->pbMusicOff == NULL
        || pEnv->pnSfxVol == NULL || pEnv->pbSfxOff == NULL) {
        return 0;
    }

    if (*pEnv->pbMusicOff != 0) {
        *pEnv->pnMusicVol = 0;                                            /* 0x6B91C */
    }
    Options_SelectRadio(pEnv->pCtl->apMusic[Options_ClampStep(*pEnv->pnMusicVol)]); /* 0x6B944 */

    if (*pEnv->pbSfxOff != 0) {
        *pEnv->pnSfxVol = 0;                                              /* 0x6B968 */
    }
    Options_SelectRadio(pEnv->pCtl->apSfx[Options_ClampStep(*pEnv->pnSfxVol)]); /* 0x6B96C / 0x6B984 */
    return 0;
}

UiControl *OptionsScreen_LayoutButtons(OptionsTabEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pScratch == NULL) {
        return NULL;
    }

    pEnv->pScratch->nLineAux145 = 0;                        /* 0x6B6D0: the audio tab */
    Options_SetClick(pEnv->pCtl->apList[0], pEnv->pfnBindSaveSlots); /* 0x6B6DC */
    Options_SetClick(pEnv->pCtl->apList[1], pEnv->pfnBindLoadSlots); /* 0x6B6E8 */
    Options_SetClick(pEnv->pCtl->apList[2], pEnv->pfnSetTab3);       /* 0x6B6F4 */
    Options_SetClick(pEnv->pCtl->apList[3], pEnv->pfnSetTab4);       /* 0x6B700 */

    for (i32 i = 0; i < 4; ++i) {                           /* 0x6B704..0x6B744 */
        Options_CentreRow(pEnv->pCtl->apList[i], 90 + 25 * i);
    }

    if (pEnv->pSaveCtl != NULL) {
        pEnv->pSaveCtl->x = (i16)(120 - (pEnv->pSaveCtl->uWidth >> 1)); /* 0x6B75C */
        pEnv->pSaveCtl->y = 198;                                        /* 0x6B784 */
    }
    if (pEnv->pBackCtl != NULL) {
        u16 w = (pEnv->pSaveCtl != NULL) ? pEnv->pSaveCtl->uWidth : 0;

        /* ORIGINAL BUG (0x6B764), reproduced: the width read for the return hotspot comes from
         * *pGeoscapeCtlA again (R1 is reloaded from the same R0), while the store at 0x6B778 lands on
         * *pGeoscapeCtlB. The two hotspots share a pad in practice, so the game does not show it. */
        pEnv->pBackCtl->x = (i16)(120 - (w >> 1));
    }
    return pEnv->pSaveCtl;
}

/* The save and load tabs are the same seven rows with a different tab byte and handler set. */
static void Options_BindSlotRows(OptionsTabEnv *pEnv, u8 nTab, void (*apfnSlot[])(void))
{
    pEnv->pScratch->nLineAux145 = nTab;                     /* 0x6B4A8 (save) / 0x6B594 (load) */

    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {
        Options_SetClick(pEnv->pCtl->apList[i], apfnSlot[i]);        /* 0x6B4B4..0x6B4FC */
        Options_CentreRow(pEnv->pCtl->apList[i], 40 + 22 * i);       /* 0x6B514 / 0x6B538 */
    }

    if (pEnv->pSaveCtl != NULL) {
        pEnv->pSaveCtl->y = 215;                                     /* 0x6B550 / 0x6B63C */
    }
    if (pEnv->pfnFormatSlots != NULL) {
        pEnv->pfnFormatSlots(pEnv->pUser);                           /* 0x6B558 / 0x6B644 */
    }
}

void OptionsScreen_BindSaveSlots(OptionsTabEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pScratch == NULL) {
        return;
    }
    Options_BindSlotRows(pEnv, 1, pEnv->apfnSaveSlot); /* 0x6b49c */
}

void OptionsScreen_BindLoadSlots(OptionsTabEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pScratch == NULL) {
        return;
    }
    Options_BindSlotRows(pEnv, 2, pEnv->apfnLoadSlot); /* 0x6b588 */
}

i32 Options_SaveAudioSettings(SaveAudioEnv *pEnv)
{
    if (pEnv == NULL) {
        return 0;
    }

    if (pEnv->pbOptionsOpen != NULL && *pEnv->pbOptionsOpen != 0 && pEnv->pClose != NULL) {
        Geoscape_CloseUI(pEnv->pClose);                                        /* 0x6B7C4 */
    }

    if (pEnv->pbControlsExist != NULL && *pEnv->pbControlsExist != 0) {       /* 0x6B7D4 */
        const i8 nTab = (pEnv->pbTab != NULL) ? *pEnv->pbTab : 0;             /* 0x6B7E8 LDRSB */

        if (nTab != 0) {
            if (nTab == 4) {                                                  /* 0x6B808 */
                char szNum[12];

                snprintf(szNum, sizeof(szNum), "%d",
                         (int)(pEnv->pnSfxVol != NULL ? *pEnv->pnSfxVol : 0));  /* 0x6B828 j_itoa */
                (void)Reg_SetString("soundvolume", szNum);                     /* 0x6B834 */
                snprintf(szNum, sizeof(szNum), "%d",
                         (int)(pEnv->pnMusicVol != NULL ? *pEnv->pnMusicVol : 0)); /* 0x6B84C */
                (void)Reg_SetString("musicvolume", szNum);                     /* 0x6B858 */
                (void)Reg_SetString("musicoff",
                                    (pEnv->pbMusicOff != NULL && *pEnv->pbMusicOff != 0) ? "1" : "0");
                (void)Reg_SetString("soundoff",                                /* 0x6B878..0x6B8A0 */
                                    (pEnv->pbSfxOff != NULL && *pEnv->pbSfxOff != 0) ? "1" : "0");
            }
            if (pEnv->pbTab != NULL) {
                *pEnv->pbTab = 0;                                              /* 0x6B8A8 */
            }
            (void)OptionsScreen_LayoutButtons(pEnv->pTabEnv);                  /* 0x6B8AC */
        } else if (pEnv->pClose != NULL) {
            Geoscape_CloseUI(pEnv->pClose);                                    /* 0x6B7F0 */
        }
    }

    if (pEnv->pbSelectionFlag != NULL) {
        if (*pEnv->pbSelectionFlag == 0 && pEnv->pTime != NULL) {
            pEnv->pTime->bPaused = 0;                                          /* 0x6B8C4 STRB */
        }
        *pEnv->pbSelectionFlag = 0;                                            /* 0x6B8CC */
    }
    return 0;
}

/* stru_AB9B0 @ 0xAB9B0: the overlay's two scroll controls. Two 0x20-byte records, and they are NOT
 * the identical pair the pseudocode implies -- the x offset and the pad names differ: "up"/"sup" at
 * (20, 180) and "down"/"sdown" at (50, 180). */
static const UiControlDesc g_aOptionsPairDesc[2] = {
    { 0x0014, 0x00B4, "up",   "sup",   "", 0 }, /* 0xAB9B0 */
    { 0x0032, 0x00B4, "down", "sdown", "", 0 }, /* 0xAB9D0 */
};

/* One of the overlay's four lists: operator new(0x1BB4) + UiPanel_ctor(nLines), then the fields all
 * four share. The per-panel extras are applied by the caller, in the original's own order. */
static UiPanel *OptionsNewPanel(i32 nLines, i32 nPosX, i32 nUnk7052, i32 nViewW)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel));                      /* 0x6D270 */

    if (pPanel == NULL) {
        return NULL;
    }
    if (!UiPanel_ctor(pPanel, nLines)) {                                       /* 0x6D288 */
        free(pPanel);
        return NULL;
    }
    pPanel->nStyle   = 6;                                                      /* 0x6D2C0 */
    pPanel->nFont    = 16;                                                     /* 0x6D2D4 */
    pPanel->nPosX    = nPosX;
    pPanel->nPosY    = 70;                                                     /* 0x6D310 */
    pPanel->nUnk7052 = nUnk7052;
    pPanel->nUnk7056 = 70;                                                     /* 0x6D338 */
    pPanel->nViewW   = nViewW;                                                 /* 0x6D340 */
    return pPanel;
}

/* The close half, 0x6DC30..0x6DCF4. The bOptionsScreenOpen clear at 0x6DC40 is what ends the
 * Geoscape_CloseUI / Options_SaveAudioSettings recursion. */
static void Options_CloseOverlay(OptionsOpenEnv *pEnv)
{
    UiPanel **apPanels[PUFO_OPTIONS_PANEL_COUNT];
    i32 i;

    if (pEnv->pnScreenId != NULL) {
        *pEnv->pnScreenId = 0;                                                 /* 0x6DC38 */
    }
    *pEnv->pbOptionsOpen = 0;                                                  /* 0x6DC40 */

    apPanels[0] = pEnv->ppPanelName;                                           /* 0x159E18 */
    apPanels[1] = pEnv->ppPanelStores;                                         /* 0x159E78 */
    apPanels[2] = pEnv->ppPanelRegion;                                         /* 0x159E7C */
    apPanels[3] = pEnv->ppPanelCargo;                                          /* 0x159E1C */

    for (i = 0; i < PUFO_OPTIONS_PANEL_COUNT; ++i) {                           /* 0x6DC44..0x6DCC0 */
        if (apPanels[i] == NULL || *apPanels[i] == NULL) {
            continue;
        }
        UiPanel_FreeLines(*apPanels[i]);                                       /* 0x6DC58 */
        free(*apPanels[i]);                                                    /* 0x6DC60 delete */
        *apPanels[i] = NULL; /* deviation: the original leaves the slot dangling */
    }

    if (pEnv->apPairCtl != NULL) {
        for (i = 0; i < 2; ++i) {                                              /* 0x6DCC8..0x6DCE4 */
            if (pEnv->apPairCtl[i] == NULL) {
                continue;
            }
            UiControl_FreePads(pEnv->apPairCtl[i]);                            /* 0x6DCDC */
            free(pEnv->apPairCtl[i]);                                          /* 0x6DCE4 */
            pEnv->apPairCtl[i] = NULL;                                         /* deviation, same */
        }
    }
}

/* One TEXTS record into a bounded buffer. The original copies with an unbounded LDRSB/STRB loop. */
static void Options_CopyTextRec(const u8 *pTextRecs, i32 nRec, char *pszOut, size_t cchOut)
{
    if (pTextRecs == NULL || pszOut == NULL || cchOut == 0) {
        return;
    }
    snprintf(pszOut, cchOut, "%s", (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE * nRec));
}

void OptionsScreen_Open(OptionsOpenEnv *pEnv, i32 nMode)
{
    UiPanel *pName;
    UiPanel *pStores;
    UiPanel *pRegion;
    UiPanel *pCargo;
    i32 nBaseIdx = 0;
    i32 nLines = 0;
    i32 nCraftCount;
    i32 nLineIdx = -1;                                                          /* 0x6D63C */
    i32 nShip;
    i32 i;

    if (pEnv == NULL || pEnv->pbOptionsOpen == NULL) {
        return;
    }
    if (*pEnv->pbOptionsOpen != 0) {                                            /* 0x6D0C0 */
        Options_CloseOverlay(pEnv);
        return;
    }

    *pEnv->pbOptionsOpen = 1;                                                   /* 0x6D0C8 */
    if (pEnv->pbAnimStarted != NULL) {
        *pEnv->pbAnimStarted = 0;                                               /* 0x6D0DC */
    }
    if (pEnv->pbSelectionFlag != NULL) {
        *pEnv->pbSelectionFlag = 0;                                             /* 0x6D1A8 */
    }

    nCraftCount = (pEnv->pnCraftCount != NULL) ? (i32)*pEnv->pnCraftCount : 0;

    if (nMode == -1) {                                                          /* 0x6D0EC */
        nLines = nCraftCount;                                                   /* 0x6D0FC, 0x6D194 */
    } else {
        const i8 nBaseCount = (pEnv->pnBaseCount != NULL) ? *pEnv->pnBaseCount : 0;

        if (nBaseCount > 0 && pEnv->apBases != NULL) {                          /* 0x6D118 BLE */
            for (nBaseIdx = 0; nBaseIdx < (i32)nBaseCount; ++nBaseIdx) {
                if ((i8)pEnv->apBases[nBaseIdx]->bUnk4 == nMode) {              /* 0x6D12C LDRSB */
                    break;
                }
            }
        }
        nLines = 0;                                                             /* 0x6D14C */
        if (nCraftCount > 0 && pEnv->apCraft != NULL) {                         /* 0x6D168 */
            for (nShip = 0; nShip < nCraftCount; ++nShip) {                     /* 0x6D16C..0x6D190 */
                if ((i32)pEnv->apCraft[nShip]->bHomeBase == nBaseIdx) {
                    ++nLines;
                }
            }
        }
    }

    /* 0x6D1B0..0x6D23C: the two scroll controls, handlers first, then the descriptors. */
    if (pEnv->apPairCtl != NULL) {
        for (i = 0; i < 2; ++i) {
            UiControl *pCtl = (UiControl *)malloc(sizeof(UiControl));           /* 0x6D1B0 new(0x20) */

            if (pCtl != NULL) {
                UiControl_ctor(pCtl);                                           /* 0x6D1C0 */
            }
            pEnv->apPairCtl[i] = pCtl;                                          /* 0x159E3C / 0x159E40 */
        }
        if (pEnv->apPairCtl[0] != NULL) {
            pEnv->apPairCtl[0]->pfnClick = pEnv->pfnPanelPrev;                  /* 0x6D208 */
        }
        if (pEnv->apPairCtl[1] != NULL) {
            pEnv->apPairCtl[1]->pfnClick = pEnv->pfnPanelNext;                  /* 0x6D214 */
        }
        for (i = 0; i < 2; ++i) {
            if (pEnv->apPairCtl[i] != NULL) {
                UiControl_Setup(pEnv->apPairCtl[i], 0, 0, &g_aOptionsPairDesc[i],
                                pEnv->pfnLoadPads, pEnv->pPadUser);             /* 0x6D228 */
            }
        }
    }

    if (pEnv->pSaveCtl != NULL) {                                               /* 0x6D240..0x6D26C */
        pEnv->pSaveCtl->pfnClick = pEnv->pfnSaveAudio;                          /* 0x6D24C */
        pEnv->pSaveCtl->x = (i16)(120 - (pEnv->pSaveCtl->uWidth >> 1));         /* 0x6D25C */
        pEnv->pSaveCtl->y = 179;                                                /* 0x6D26C */
    }

    pName   = OptionsNewPanel(nLines, 5,   5,   230);                           /* 0x6D270 */
    pStores = OptionsNewPanel(nLines, 115, 155, 30);                            /* 0x6D3BC */
    pRegion = OptionsNewPanel(nLines, 175, 185, 30);                            /* 0x6D490 */
    pCargo  = OptionsNewPanel(nLines, 233, 5,   230);                           /* 0x6D55C */

    if (pEnv->ppPanelName != NULL)   { *pEnv->ppPanelName   = pName; }          /* 0x6D2B8 */
    if (pEnv->ppPanelStores != NULL) { *pEnv->ppPanelStores = pStores; }        /* 0x6D3EC */
    if (pEnv->ppPanelRegion != NULL) { *pEnv->ppPanelRegion = pRegion; }        /* 0x6D4BC */
    if (pEnv->ppPanelCargo != NULL)  { *pEnv->ppPanelCargo  = pCargo; }         /* 0x6D588 */

    if (pName != NULL) {
        pName->pfnClick = pEnv->pfnOnConfirm;                                    /* 0x6D2E8 */
        UiPanel_SetBytes0to2(pName, 0x0A, 0x50, 0x5A);                          /* 0x6D34C */
        UiPanel_SetBytes6to11(pName, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B);       /* 0x6D374 */
        pName->bFlag7073 = 1;                                                   /* 0x6D388 */
        UiPanel_SetBytes3to5(pName, 0x44, 0xF4, 0xFC);                          /* 0x6D39C */
        pName->bUnk7088 = 100;                                                  /* 0x6D3B0 */
    }
    if (pStores != NULL) {
        UiPanel_SetBytes0to2(pStores, 0x0A, 0x50, 0x5A);                        /* 0x6D444 */
        UiPanel_SetBytes6to11(pStores, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B);     /* 0x6D46C */
        pStores->nUnk7076 = 1;                                                  /* 0x6D484 */
    }
    if (pRegion != NULL) {
        UiPanel_SetBytes0to2(pRegion, 0x0A, 0x50, 0x5A);                        /* 0x6D514 */
        UiPanel_SetBytes6to11(pRegion, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B);     /* 0x6D540 */
        pRegion->nUnk7076 = 1;                                                  /* 0x6D550 */
    }
    if (pCargo != NULL) {
        UiPanel_SetBytes0to2(pCargo, 0x0A, 0x50, 0x5A);                         /* 0x6D5E0 */
        UiPanel_SetBytes6to11(pCargo, 0x44, 0xF4, 0xFC, 0x04, 0x46, 0x4B);      /* 0x6D60C */
        pCargo->nUnk7076 = 2;                                                   /* 0x6D61C */
        pCargo->bUnk7088 = 150;                                                 /* 0x6D630, -106 as a byte */
    }

    if (nCraftCount == 0) {                                                     /* 0x6D654 BLS */
        return;
    }

    /* 0x6D66C..0x6DC18. The bound is re-read on every iteration (0x6DC08), so a count that changes
     * while the list is being built changes the number of lines. */
    for (nShip = 0; nShip < ((pEnv->pnCraftCount != NULL) ? (i32)*pEnv->pnCraftCount : 0); ++nShip) {
        Ship *pShip;
        BaseRec *pBase;
        char szRegion[104];                                                     /* v95 */
        char szCargo[104];
        char szFlags[2];
        i32 nLine;
        i32 nAux;

        if (pEnv->apCraft == NULL || pEnv->apCraft[nShip] == NULL) {
            continue;
        }
        pShip = pEnv->apCraft[nShip];

        if (nMode == -1) {
            nLine = nShip;                                                      /* 0x6D674 MOVEQ R4,R9 */
        } else if ((i32)pShip->bHomeBase != nBaseIdx) {                         /* 0x6D69C BNE */
            continue;
        } else {
            nLine = ++nLineIdx;                                                 /* 0x6D6A4 */
        }

        /* Names column: the craft display name, and the aux dword is the craft index. */
        if (pName != NULL && nLine >= 0 && nLine < PUFO_PANEL_LINES) {
            (void)Ship_FormatDisplayName(pEnv->apCraft, nShip, pEnv->pTextRecs,
                                         (char *)pName->apLines[nLine], PUFO_PANEL_LINE_BYTES);
            pName->anLineAux[nLine] = nShip;                                    /* 0x6D700 */
        }

        /* Stores column. The aux dword starts at -1 and only the "both full" case lifts it to 0;
         * a craft that is not at base puts it back to -1 even if it was full. */
        nAux = -1;                                                              /* 0x6D718 */
        if (pStores != NULL && nLine >= 0 && nLine < PUFO_PANEL_LINES) {
            if (pShip->uRangeRemain == pShip->uRangeMax &&
                pShip->nFuel == (i16)pShip->uFuelMax) {                         /* 0x6D728 / 0x6D738 */
                Options_CopyTextRec(pEnv->pTextRecs, 151,
                                    (char *)pStores->apLines[nLine], PUFO_PANEL_LINE_BYTES);
                nAux = 0;                                                       /* 0x6D77C */
            }
            if (pShip->uRangeRemain != pShip->uRangeMax) {                      /* 0x6D790 */
                Options_CopyTextRec(pEnv->pTextRecs, 150,
                                    (char *)pStores->apLines[nLine], PUFO_PANEL_LINE_BYTES);
            }
            if (pShip->nFuel != (i16)pShip->uFuelMax) {                         /* 0x6D7D4 */
                Options_CopyTextRec(pEnv->pTextRecs, 149,
                                    (char *)pStores->apLines[nLine], PUFO_PANEL_LINE_BYTES);
            }
            if (pShip->bAtBase == 0) {                                          /* 0x6D818 */
                Options_CopyTextRec(pEnv->pTextRecs, 152,
                                    (char *)pStores->apLines[nLine], PUFO_PANEL_LINE_BYTES);
                nAux = -1;                                                      /* 0x6D858 */
            }
            pStores->anLineAux[nLine] = nAux;
        }

        /* Region column: the base's region name, "-", then the base's signed ordinal. */
        szRegion[0] = '\0';
        pBase = (pEnv->apBases != NULL && (i32)pShip->bHomeBase < PUFO_FAC_BASE_COUNT)
                    ? pEnv->apBases[pShip->bHomeBase] : NULL;
        if (pBase != NULL) {
            switch (pBase->bRegionId) {                                         /* 0x6D884 */
            case 0x80: Options_CopyTextRec(pEnv->pTextRecs, 1165, szRegion, sizeof(szRegion)); break;
            case 0x81: Options_CopyTextRec(pEnv->pTextRecs, 1166, szRegion, sizeof(szRegion)); break;
            case 0x82: Options_CopyTextRec(pEnv->pTextRecs, 1167, szRegion, sizeof(szRegion)); break;
            case 0x83: Options_CopyTextRec(pEnv->pTextRecs, 1036, szRegion, sizeof(szRegion)); break;
            case 0x84: Options_CopyTextRec(pEnv->pTextRecs, 1168, szRegion, sizeof(szRegion)); break;
            case 0x85: Options_CopyTextRec(pEnv->pTextRecs, 1169, szRegion, sizeof(szRegion)); break;
            case 0x86: Options_CopyTextRec(pEnv->pTextRecs, 1170, szRegion, sizeof(szRegion)); break;
            case 0x87: Options_CopyTextRec(pEnv->pTextRecs, 1171, szRegion, sizeof(szRegion)); break;
            case 0x88: Options_CopyTextRec(pEnv->pTextRecs, 1172, szRegion, sizeof(szRegion)); break;
            default:   szRegion[0] = '\0'; break;                              /* ORIGINAL BUG (0x6D884) */
            }
            snprintf(szRegion + strlen(szRegion), sizeof(szRegion) - strlen(szRegion), "-%d",
                     (int)(i8)pBase->bUnk16);                                   /* 0x6DA68 / 0x6DA8C */
        }
        if (pRegion != NULL && nLine >= 0 && nLine < PUFO_PANEL_LINES) {
            snprintf((char *)pRegion->apLines[nLine], PUFO_PANEL_LINE_BYTES, "%s", szRegion);
        }

        /* Cargo column: crew, "\", weapon slots, "\", then the hot/intercepting flags. */
        snprintf(szCargo, sizeof(szCargo), "%d\\%d\\",
                 Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount, nShip),
                 Craft_CountWeaponSlotsCarried(pEnv->apObjects, pEnv->nObjectCount, nShip));
        szFlags[0] = '0';
        szFlags[1] = '\0';
        if (pShip->bWeaponsHot != 0 || pShip->bIntercepting != 0) {
            szFlags[0] = '1';                                                   /* 0x6DB6C */
        }
        if (pShip->bWeaponsHot != 0 && pShip->bIntercepting != 0) {
            szFlags[0] = '2';                                                   /* 0x6DBA4 */
        }
        snprintf(szCargo + strlen(szCargo), sizeof(szCargo) - strlen(szCargo), "%s", szFlags);
        if (pCargo != NULL && nLine >= 0 && nLine < PUFO_PANEL_LINES) {
            snprintf((char *)pCargo->apLines[nLine], PUFO_PANEL_LINE_BYTES, "%s", szCargo);
        }
    }
}

void OptionsScreen_OnConfirm(OptionsConfirmEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }

    UiPanel *pName   = (pEnv->ppPanelName  != NULL) ? *pEnv->ppPanelName  : NULL;
    UiPanel *pStores = (pEnv->ppPanelStores != NULL) ? *pEnv->ppPanelStores : NULL;
    if (pName == NULL || pStores == NULL) {
        return;
    }

    const i32 nRow = pName->nUnk3032;                                        /* 0x6CF3C */
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return;
    }

    const i32 nShip = pName->anLineAux[nRow];                                /* 0x6CF4C */
    if (pEnv->pnMsgShipIdx != NULL) {
        *pEnv->pnMsgShipIdx = nShip;                                       /* 0x6CF4C */
    }
    if (pEnv->pnSelectedShipIdx != NULL) {
        *pEnv->pnSelectedShipIdx = nShip;                                    /* 0x6CF50 */
    }

    if (pStores->anLineAux[nRow] != 0) {                                     /* 0x6CF6C */
        return;                                                              /* 0x6CFF0 */
    }

    if (pEnv->pbSelectionFlag != NULL) {
        *pEnv->pbSelectionFlag = 1;                                          /* 0x6CF78 */
    }
    if (pEnv->pfnPlaySample != NULL) {
        pEnv->pfnPlaySample(7, pEnv->pUser);                                 /* 0x6CF84 */
    }
    if (pEnv->pLines != NULL) {
        /* 0x6CFA4. The index is re-read LDRB at 0x6CF8C, so a -1 selection appends 255. */
        UiLineList_AppendWithSound(pEnv->pLines, 27, (u32)(u8)nShip, 0u, 0u, 0u,
                                   pEnv->pfnPlaySample, pEnv->pUser);
    }

    const Ship *pCraft = (pEnv->apCraft != NULL && nShip >= 0 && pEnv->apCraft[nShip] != NULL)
                             ? pEnv->apCraft[nShip] : NULL;
    if (pEnv->aTopics != NULL && pCraft != NULL &&
        pEnv->aTopics[PUFO_RESEARCH_TOPIC_CYDONIA].bUnlocked != 0 &&
        (i32)pCraft->bCraftType == CRAFT_TYPE_AVENGER) {                    /* 0x6CFA8 / 0x6CFD8 */
        if (pEnv->pbFinalMissionCtl != NULL) {
            *pEnv->pbFinalMissionCtl = 1;                                    /* 0x6CFE0 */
        }
    }

    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 1; /* GameTime_Pause @ 0x5d6ac is a store of 1 to 0x15961C */
    }
}

void OptionsScreen_Init(OptionsInitEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCtl == NULL || pEnv->pbControlsExist == NULL) {
        return;
    }

    if (*pEnv->pbControlsExist != 0) {                        /* 0x6BE84 takes the teardown branch */
        *pEnv->pbControlsExist = 0;                           /* 0x6C0FC */

        for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {   /* 0x6C108..0x6C130 */
            UiControl_FreePads(pEnv->pCtl->apList[i]);        /* 0x6C118; 0x6C120 deletes, caller-owned here */
        }
        for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {    /* 0x6C134..0x6C15C, the twelve buttons */
            UiControl_FreePads(pEnv->pCtl->apMusic[i]);
            UiControl_FreePads(pEnv->pCtl->apSfx[i]);
        }
        return;
    }

    *pEnv->pbControlsExist = 1;                               /* 0x6BE90 */

    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {       /* 0x6BEA0..0x6BED0 */
        if (pEnv->pCtl->apList[i] != NULL) {
            UiControl_ctor(pEnv->pCtl->apList[i]);
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_LIST_COUNT; ++i) {       /* 0x6BED4..0x6BEF0 */
        if (pEnv->pCtl->apList[i] != NULL) {
            UiControl_Setup(pEnv->pCtl->apList[i], 0, 0, &g_aOptionsListDesc,
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {        /* 0x6BF34..0x6BF70, music then sfx */
        if (pEnv->pCtl->apMusic[i] != NULL) {
            UiControl_ctor(pEnv->pCtl->apMusic[i]);
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (pEnv->pCtl->apSfx[i] != NULL) {
            UiControl_ctor(pEnv->pCtl->apSfx[i]);
        }
    }

    Options_SetClick(pEnv->pSaveCtl, pEnv->pfnSaveAudio);     /* 0x6BF1C */
    Options_SetClick(pEnv->pBackCtl, pEnv->pfnReturnToTitle); /* 0x6BF2C */

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {        /* 0x6BF74..0x6BF90 */
        if (pEnv->pCtl->apMusic[i] != NULL) {
            UiControl_Setup(pEnv->pCtl->apMusic[i], 0, 0, &g_aOptionsVolDesc,
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }
    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (pEnv->pCtl->apSfx[i] != NULL) {
            UiControl_Setup(pEnv->pCtl->apSfx[i], 0, 0, &g_aOptionsVolDesc,
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {
        if (pEnv->pCtl->apMusic[i] != NULL) {
            pEnv->pCtl->apMusic[i]->y = 107;                  /* 0x6BFBC */
            pEnv->pCtl->apMusic[i]->x = (i16)(46 + 25 * i);   /* 0x6BFDC */
        }
        if (pEnv->pCtl->apSfx[i] != NULL) {
            pEnv->pCtl->apSfx[i]->y = 147;                    /* 0x6BFF8 */
            pEnv->pCtl->apSfx[i]->x = (i16)(46 + 25 * i);     /* 0x6C018: 25 * (i + 6) - 104 */
        }
    }

    for (i32 i = 0; i < PUFO_OPTIONS_VOL_COUNT; ++i) {        /* 0x6C02C..0x6C0B0 */
        Options_SetClick(pEnv->pCtl->apMusic[i], pEnv->apfnMusicVol[i]);
        Options_SetClick(pEnv->pCtl->apSfx[i], pEnv->apfnSfxVol[i]);
    }

    (void)OptionsScreen_RefreshVolumeHighlight(pEnv->pVolEnv); /* 0x6C0B4 */
    (void)OptionsScreen_LayoutButtons(pEnv->pTabEnv);          /* 0x6C0B8 */
}

/* ------------------------------------------------- the options screen's save and load slots */

i32 SaveGame_Slot_CloseUI(SaveSlotEnv *pEnv, i32 nSlot)
{
    if (pEnv == NULL || pEnv->pDat == NULL || pEnv->pIn == NULL || pEnv->pnListMode == NULL
        || pEnv->pScratch == NULL) {
        return 0;
    }

    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SAVING;              /* 0x6AF90: no rows while writing */
    pEnv->pIn->nSlot = nSlot;                                   /* the original's second argument */
    (void)SaveGame(pEnv->pDat, pEnv->pIn);                      /* 0x6AF98 */
    pEnv->pScratch->nLineAux145 = 0;                            /* 0x6AFA4 */
    *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;           /* 0x6AFAC */

    Geoscape_CloseUI(pEnv->pClose);                             /* 0x6AFB8, the tail call */
    return 0;
}

i32 LoadGame_Slot_InitOptions(SaveSlotEnv *pEnv, i32 nSlot)
{
    i32 nRet;

    if (pEnv == NULL || pEnv->pDat == NULL || pEnv->pLoadEnv == NULL || pEnv->pnListMode == NULL
        || pEnv->pScratch == NULL) {
        return 0;
    }

    pEnv->pScratch->nLineAux145 = 0;                            /* 0x6B1A4 */
    if (pEnv->pnGeoscapeScreen != NULL) {
        *pEnv->pnGeoscapeScreen = 0;                            /* 0x6B1B0 */
    }
    *pEnv->pnListMode = PUFO_MAINMENU_MODE_LOADING;             /* 0x6B1B8 */
    if (pEnv->pbControlsExist != NULL) {
        *pEnv->pbControlsExist = 1;                             /* 0x6B1C4 */
    }
    OptionsScreen_Init(pEnv->pOptionsInit);                     /* 0x6B1C8, so this is the teardown */

    pEnv->pLoadEnv->nSlot = nSlot;                              /* the original's second argument */
    nRet = LoadGame(pEnv->pDat, pEnv->pLoadEnv);               /* 0x6B1D4 */

    if (((u8)nRet) == 0) {                                      /* 0x6B1DC: the test is on the low byte */
        *pEnv->pnListMode = PUFO_MAINMENU_MODE_SLOTCLICK;       /* 0x6B1E0 */
        return 1;
    }
    return nRet;
}

void SaveGame_Slot0_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 0); /* 0x6af80 */
}

void SaveGame_Slot1_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 1); /* 0x6afcc */
}

void SaveGame_Slot2_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 2); /* 0x6b018 */
}

void SaveGame_Slot3_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 3); /* 0x6b064 */
}

void SaveGame_Slot4_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 4); /* 0x6b0b0 */
}

void SaveGame_Slot5_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 5); /* 0x6b0fc */
}

void SaveGame_Slot6_CloseUI(SaveSlotEnv *pEnv)
{
    (void)SaveGame_Slot_CloseUI(pEnv, 6); /* 0x6b148 */
}

i32 LoadGame_Slot0_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 0); /* 0x6b194 */
}

i32 LoadGame_Slot1_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 1); /* 0x6b1fc */
}

i32 LoadGame_Slot2_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 2); /* 0x6b264 */
}

i32 LoadGame_Slot3_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 3); /* 0x6b2cc */
}

i32 LoadGame_Slot4_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 4); /* 0x6b334 */
}

i32 LoadGame_Slot5_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 5); /* 0x6b39c */
}

i32 LoadGame_Slot6_InitOptions(SaveSlotEnv *pEnv)
{
    return LoadGame_Slot_InitOptions(pEnv, 6); /* 0x6b404 */
}

/* The two scratch clears the three openers share. Both are strcpy loops reading from 0x10DA38,
 * which is all zero in the image, so the only byte that lands is the terminator. */
static void ScreenOpen_ClearScratch(char *psz, size_t cch)
{
    if (psz != NULL && cch > 0) {
        psz[0] = 0;
    }
}

/* The research "active" walk. The bound is the byte at byte_160BBC + 144 and it is re-read only
 * after a store (0x6173C / 0x84B9C / 0x85234 are MOVEQ / LDRBEQ), and the loop is a do/while, so a
 * non-zero count always examines entry 0. Returns the index reached, which Ui_OpenBaseInfo and
 * Geoscape_PrepScreenOpen spill into dword_186B08. */
static i32 Research_PromotePending(u8 *abActive, const u8 *pbCount)
{
    if (abActive == NULL || pbCount == NULL) {
        return 0;
    }

    i32 nCount = (i32)(*pbCount & 0xFFu);
    if (nCount == 0) {
        return 0; /* 0x61718 / 0x84B78 / 0x85214: CMP #0 / BLS out */
    }

    i32 i = 0;
    for (;;) {
        if ((abActive[i] & 0xFFu) == 1) {         /* 0x61724 CMP R3,#1 */
            abActive[i] = 2;                      /* 0x6172C STRBEQ */
            nCount = (i32)(*pbCount & 0xFFu);     /* 0x6173C LDRBEQ, only after a store */
        }
        ++i;                                      /* 0x61720 ADD R1,R1,#1 */
        /* 0x61748 CMP R1,R3 / BLT back edge; the second test is the port's bound. */
        if (i >= nCount || i >= PUFO_RESEARCH_TOPIC_COUNT) {
            break;
        }
    }
    return i;
}

/* The prologue all three run, in the originals' order. */
static void ScreenOpen_Prologue(const ScreenResetOpenEnv *pEnv)
{
    ScreenOpen_ClearScratch(pEnv->pszMsg, pEnv->cchMsg);
    ScreenOpen_ClearScratch(pEnv->pszScratch, pEnv->cchScratch);
    (void)Research_PromotePending(pEnv->abResearchActive, pEnv->pbResearchCount);
    if (pEnv->pdwShortfall != NULL) {
        *pEnv->pdwShortfall = 0; /* 0x61754 / 0x84BB0 / 0x8524C, a dword store into byte_1870C8 */
    }
    GameTime_Set5Sec(pEnv->pWorld, pEnv->apSpeedCtl); /* 0x61760 / 0x84BC4 / 0x85260 */
}

/* ResearchScreen_Open @ 0x616ac. Sample first, id 8 written before the Init tail call. */
void ResearchScreen_Open(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->init.pfnPlaySample != NULL) {
        pEnv->init.pfnPlaySample(17, 4, pEnv->init.pUser); /* 0x616B4 */
    }
    ScreenOpen_Prologue(pEnv);
    if (pEnv->pState != NULL) {
        pEnv->pState->nGeoscapeScreen = 8; /* 0x6176C */
    }
    if (pEnv->init.pfnInit != NULL) {
        pEnv->init.pfnInit(pEnv->init.pSlot, pEnv->init.pUser); /* 0x61774 -> ResearchScreen_Init */
    }
}

/* Ui_OpenBaseInfo @ 0x84AF8. Same shape, id 16, and the Init receives the root at unk_1872E0. */
void Ui_OpenBaseInfo(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->init.pfnPlaySample != NULL) {
        pEnv->init.pfnPlaySample(17, 4, pEnv->init.pUser); /* 0x84AFC */
    }
    ScreenOpen_Prologue(pEnv);
    if (pEnv->pState != NULL) {
        pEnv->pState->nGeoscapeScreen = 16; /* 0x84BD8 */
    }
    if (pEnv->init.pfnInit != NULL) {
        pEnv->init.pfnInit(pEnv->init.pSlot, pEnv->init.pUser); /* 0x84BE8 -> Ui_InitBaseInfo */
    }
}

/* Geoscape_PrepScreenOpen @ 0x85194. No sample and no screen id: it un-pauses the clock, runs the
 * prologue, drops line 0 of the root message list, and re-pauses when that line's flag lands in
 * (30, 50). */
/* The tail Geoscape_PrepScreenOpen and Geoscape_PrepScreenOpenSfx share: drop message line 0, then
 * pause the clock when the line that moved into slot 0 has a flag above 30 AND below 50. The two
 * reads are separate calls in the original (0x85270 / 0x85284 and 0x85398 / 0x853AC). */
static void ScreenOpen_DropLine0AndPause(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv->pLines == NULL) {
        return;
    }
    UiLineList_Remove(pEnv->pLines, 0); /* 0x8525C / 0x85384 */
    if (UiLineList_GetFlag(pEnv->pLines, 0) > 30) { /* 0x85270 / 0x85398, CMP #0x1E / BLE out */
        if (UiLineList_GetFlag(pEnv->pLines, 0) < 50) { /* the flag read a second time */
            if (pEnv->pTime != NULL) {
                pEnv->pTime->bPaused = 1; /* GameTime_Pause @ 0x5d6ac */
            }
        }
    }
}

void Geoscape_PrepScreenOpen(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 0; /* 0x8519C STRB at 0x15961C */
    }
    ScreenOpen_Prologue(pEnv);
    ScreenOpen_DropLine0AndPause(pEnv);
}

/* Geoscape_PrepScreenOpenSfx @ 0x852b8. The same body with a sample inserted after the pause clear
 * and with GameTime_Set5Sec dropped: the only one of the six that leaves the tick interval alone. */
void Geoscape_PrepScreenOpenSfx(const ScreenResetOpenEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pTime != NULL) {
        pEnv->pTime->bPaused = 0; /* 0x852C8 */
    }
    if (pEnv->init.pfnPlaySample != NULL) {
        pEnv->init.pfnPlaySample(17, 4, pEnv->init.pUser); /* 0x852D0 */
    }
    ScreenOpen_ClearScratch(pEnv->pszMsg, pEnv->cchMsg);
    ScreenOpen_ClearScratch(pEnv->pszScratch, pEnv->cchScratch);
    (void)Research_PromotePending(pEnv->abResearchActive, pEnv->pbResearchCount);
    if (pEnv->pdwShortfall != NULL) {
        *pEnv->pdwShortfall = 0; /* 0x85374 */
    }
    /* No GameTime_Set5Sec here: 0x8537C falls straight through to the line removal. */
    ScreenOpen_DropLine0AndPause(pEnv);
}

/* Ui_OpenBaseInfoAlt @ 0x84d18. Sets the ufopaedia reselect flag, runs the whole
 * Geoscape_PrepScreenOpen, and only then runs the prologue a second time. */
void Ui_OpenBaseInfoAlt(const ScreenResetOpen2Env *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pbUfopaediaReselect != NULL) {
        *pEnv->pbUfopaediaReselect = 1; /* 0x84D1C */
    }
    Geoscape_PrepScreenOpen(&pEnv->base); /* 0x84D24 */
    ScreenOpen_Prologue(&pEnv->base);
    if (pEnv->base.pState != NULL) {
        pEnv->base.pState->nGeoscapeScreen = 16; /* 0x84DE4 */
    }
    if (pEnv->base.init.pfnInit != NULL) {
        pEnv->base.init.pfnInit(pEnv->base.init.pSlot, pEnv->base.init.pUser); /* 0x84DF4 */
    }
}

/* Ui_OpenResearch @ 0x84bf8. The prologue without the tick, then the full prep, then the tick, then
 * the nav rebuild and the parked control. */
void Ui_OpenResearch(const ScreenResetOpen2Env *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    const ScreenResetOpenEnv *pBase = &pEnv->base;

    ScreenOpen_ClearScratch(pBase->pszMsg, pBase->cchMsg);
    ScreenOpen_ClearScratch(pBase->pszScratch, pBase->cchScratch);
    (void)Research_PromotePending(pBase->abResearchActive, pBase->pbResearchCount);
    if (pBase->pdwShortfall != NULL) {
        *pBase->pdwShortfall = 0; /* 0x84C9C */
    }
    Geoscape_PrepScreenOpen(pBase);          /* 0x84CBC */
    GameTime_Set5Sec(pBase->pWorld, pBase->apSpeedCtl); /* 0x84CC0 */
    (void)Geoscape_BuildNavControls(pEnv->pNavOut, pEnv->pNavEnv); /* 0x84CC8, R0 = abFacilityGrid */
    if (pEnv->pParkCtl != NULL) {
        pEnv->pParkCtl->x = 1000; /* 0x84CD8 STRH of 0x3E8 */
        pEnv->pParkCtl->y = 1000; /* 0x84CE4 STRH of 0x3E8 */
    }
    if (pBase->pState != NULL) {
        pBase->pState->nGeoscapeScreen = 8; /* 0x84CEC */
    }
    if (pBase->init.pfnInit != NULL) {
        pBase->init.pfnInit(pBase->init.pSlot, pBase->init.pUser); /* 0x84CF4 -> ResearchScreen_Init */
    }
}

/* g_aResearchCtlDesc @ 0xABB10: the research screen's four control descriptors -- the topic-list
 * scroll pair, then the quantity pair. Same order as the four handlers ResearchScreen_Init installs,
 * and the scroll pair carries the same offsets the manufacturing tab pair has. */
static const UiControlDesc g_aResearchCtlDesc[4] = {
    { 0x00CD, 0x0012, "up",   "sup",   "", 0 }, /* 0xABB10 */
    { 0x00CD, 0x0026, "down", "sdown", "", 0 }, /* 0xABB30 */
    { 0x000A, 0x00A3, "up",   "sup",   "", 0 }, /* 0xABB50 */
    { 0x000A, 0x00B7, "down", "sdown", "", 0 }, /* 0xABB70 */
};

/* ResearchScreen_Init @ 0x749a8. The header block has the ten steps, the descriptor source and the
 * two control positions that arrive only at the end. */
void ResearchScreen_Init(ResearchInitEnv *pEnv)
{
    PadImage *pPad;
    i32 i;

    if (pEnv == NULL || pEnv->ppRoot == NULL) {
        return;
    }

    pPad = BuySellPadNew(); /* 0x749B4 / 0x749C4: operator new(0x14) then PadImage_ctor */
    pEnv->ppRoot[0] = pPad; /* 0x749D8: the ctor's result, not the raw allocation */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_RESEARCH_PAD_NAME, PUFO_RESEARCH_PAD_MODE); /* 0x749DC */
        pPad->nOriginX = 0; /* 0x749EC */
        pPad->nOriginY = 0; /* 0x74A00 */
    }

    for (i = 0; i < 4; ++i) {
        pEnv->ppRoot[i + 1] = pEnv->apCtl[i]; /* 0x74A28, the original's operator new(0x20) slots */
    }
    if (pEnv->apCtl[0] != NULL) {
        pEnv->apCtl[0]->pfnClick = pEnv->pfnPanelPrev; /* 0x74A4C */
    }
    if (pEnv->apCtl[1] != NULL) {
        pEnv->apCtl[1]->pfnClick = pEnv->pfnPanelNext; /* 0x74A64 */
    }
    if (pEnv->apCtl[2] != NULL) {
        pEnv->apCtl[2]->pfnClick = pEnv->pfnIncQty; /* 0x74A7C */
    }
    if (pEnv->apCtl[3] != NULL) {
        pEnv->apCtl[3]->pfnClick = pEnv->pfnDecQty; /* 0x74A94 */
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->x = PUFO_RESEARCH_CLOSE_X; /* 0x74AA0 */
        pEnv->pCloseCtl->y = PUFO_RESEARCH_CLOSE_Y; /* 0x74AAC */
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseClick; /* 0x74AB8 */
    }

    /* The two shared bottom controls are pointed at their research handlers here and POSITIONED
     * only at 0x74B98, after the topic walk. */
    if (pEnv->pCancelCtl != NULL) {
        pEnv->pCancelCtl->pfnClick = pEnv->pfnCancelClick; /* 0x74AC4 */
    }
    if (pEnv->pStartCtl != NULL) {
        pEnv->pStartCtl->pfnClick = pEnv->pfnStartClick; /* 0x74AD0 */
    }

    for (i = 0; i < 4; ++i) {
        if (pEnv->apCtl[i] != NULL) {
            UiControl_Setup(pEnv->apCtl[i], 0, 0, &g_aResearchCtlDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x74AE4 */
        }
    }

    if (pEnv->pfnRebuildTopicList != NULL) {
        pEnv->pfnRebuildTopicList(); /* 0x74B00 */
    }

    const i32 nEntries = (pEnv->pbEntryCount != NULL) ? (i32)*pEnv->pbEntryCount : 0;
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;

    /* 0x74B08..0x74B8C: a topic that is not on the screen list has its allocation for this base
     * dropped. Deviation: the original bounds only by the count byte (a byte, so up to 255 of the
     * 144 entries) and passes nSelectedBaseIdx through untested; the port stops at the table size
     * and refuses a base index outside the eight columns. */
    if (pEnv->aTopics != NULL && pEnv->apResearchCols != NULL
        && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT) {
        const i32 nWalk = (nEntries < PUFO_RESEARCH_TOPIC_COUNT) ? nEntries : PUFO_RESEARCH_TOPIC_COUNT;
        for (i = 0; i < nWalk; ++i) {
            if (pEnv->aTopics[i].bActive == 0) {
                pEnv->apResearchCols[PUFO_RESEARCH_TOPIC_COUNT * nBase + i] = 0; /* 0x74B68 STRH */
            }
        }
    }

    if (pEnv->pCancelCtl != NULL) {
        pEnv->pCancelCtl->x = PUFO_RESEARCH_BOTTOM_X; /* 0x74B98 */
        pEnv->pCancelCtl->y = PUFO_RESEARCH_CANCEL_Y; /* 0x74BA0 */
    }
    if (pEnv->pStartCtl != NULL) {
        pEnv->pStartCtl->x = PUFO_RESEARCH_BOTTOM_X; /* 0x74BAC */
        pEnv->pStartCtl->y = PUFO_RESEARCH_START_Y;  /* 0x74BB4 */
    }

    if (pEnv->pfnBuildPanels != NULL) {
        pEnv->pfnBuildPanels(); /* 0x74BB8 */
    }

    /* 0x74BC4: the tail call. The original hands in the count byte and the selected base's column
     * straight out of the globals; the port passes the same two values it just walked over. */
    if (pEnv->aTopics != NULL && pEnv->apResearchCols != NULL
        && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT) {
        (void)ResearchScreen_RefreshPanels(pEnv->aTopics, nEntries,
                                           pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase,
                                           pEnv->pPanelTopic, pEnv->pPanelQty,
                                           pEnv->pPanelProgress);
    }
}

/* ResearchScreen_BuildPanels @ 0x733f4. The header block has the build/teardown split, the topic+0x4
 * name index, and the percentage this function computes the other way round from RefreshPanels. */
void ResearchScreen_BuildPanels(ResearchBuildEnv *pEnv)
{
    UiPanel **apSlot[3];
    UiPanel *apFree[3];
    PadImage *pPad;
    i32 nRows = 0;
    i32 i;

    if (pEnv == NULL || pEnv->pbPanelsBuilt == NULL) {
        return;
    }

    apSlot[0] = pEnv->ppPanelTopic;
    apSlot[1] = pEnv->ppPanelB;
    apSlot[2] = pEnv->ppPanelC;

    if (*pEnv->pbPanelsBuilt != 0) {
        /* The teardown, 0x73944..0x73A28. The three nLineCount stores come FIRST, before anything is
         * released -- the original is preparing them for a reader that no longer exists. */
        *pEnv->pbPanelsBuilt = 0; /* 0x7394C */
        for (i = 0; i < 3; ++i) {
            if (apSlot[i] != NULL && *apSlot[i] != NULL) {
                (*apSlot[i])->nLineCount = PUFO_RESEARCH_TOPIC_COUNT; /* 0x73964, 0x73974, 0x73980 */
            }
        }
        pPad = (pEnv->ppRoot != NULL) ? (PadImage *)pEnv->ppRoot[0] : NULL;
        if (pPad != NULL) {
            PadImage_FreeData(pPad); /* 0x73994 */
            free(pPad);              /* 0x7399C */
            pEnv->ppRoot[0] = NULL;
        }
        for (i = 0; i < 3; ++i) {
            apFree[i] = (apSlot[i] != NULL) ? *apSlot[i] : NULL;
        }
        for (i = 0; i < 3; ++i) { /* A, B, C, the order 0x739A4 / 0x739C0 / 0x739DC walks */
            if (apFree[i] != NULL) {
                UiPanel_FreeLines(apFree[i]);
                free(apFree[i]);
            }
            if (apSlot[i] != NULL) {
                *apSlot[i] = NULL;
            }
        }
        for (i = 0; i < 4; ++i) {
            if (pEnv->apCtl[i] != NULL) {
                UiControl_FreePads(pEnv->apCtl[i]); /* 0x73A10 */
            }
            /* 0x73A18 also operator delete's the control itself; the port's four are caller-owned. */
        }
        return;
    }

    *pEnv->pbPanelsBuilt = 1; /* 0x73410 */
    if (pEnv->aTopics != NULL) {
        pEnv->aTopics[0].bActive = 1; /* 0x7341C: topic 0 is always on the list */
    }

    const i32 nEntries = (pEnv->pbEntryCount != NULL) ? (i32)*pEnv->pbEntryCount : 0;
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const bool bBaseOk = (nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT);

    /* The three panels, 0x73428..0x73700. Constructed with the full 144 rows (UiPanel_ctor(144) at
     * 0x73438), not with the row count -- the count is only written into nLineCount at the end. */
    UiPanel *pA = BuySellNewPanel(pEnv->ppPanelTopic, PUFO_RESEARCH_TOPIC_COUNT);
    if (pA != NULL) {
        pA->nStyle = PUFO_RESEARCH_PANEL_STYLE;  /* 0x73464 */
        pA->nFont = PUFO_RESEARCH_PANEL_FONT;    /* 0x73478 */
        pA->pfnClick = pEnv->pfnPanelClick;      /* 0x7348C, PlaySample7_SetFlags_Cache0xBD8 */
        pA->nPosX = 10;                          /* 0x734A0 */
        pA->nPosY = PUFO_RESEARCH_PANEL_POSY;    /* 0x734B4 */
        pA->nUnk7052 = 5;                        /* 0x734C8 */
        pA->nUnk7056 = PUFO_RESEARCH_PANEL_POSY; /* 0x734DC */
        pA->nViewW = 230;                        /* 0x734E8 */
        UiPanel_SetBytes0to2(pA, 0xB4, 0xB4, 0xB4);                        /* 0x734F4 */
        UiPanel_SetBytes6to11(pA, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);     /* 0x73518 */
        pA->bFlag7073 = 1;                                                 /* 0x7352C */
        UiPanel_SetBytes3to5(pA, 0x32, 0x32, 0xFA);                        /* 0x73540 */
        pA->bUnk7088 = (u8)0x94;                                           /* 0x73554 */
    }
    UiPanel *pB = BuySellNewPanel(pEnv->ppPanelB, PUFO_RESEARCH_TOPIC_COUNT);
    if (pB != NULL) {
        pB->nStyle = PUFO_RESEARCH_PANEL_STYLE;  /* 0x73594 */
        pB->nFont = PUFO_RESEARCH_PANEL_FONT;    /* 0x735A8 */
        pB->nPosX = 197;                         /* 0x735BC */
        pB->nPosY = PUFO_RESEARCH_PANEL_POSY;    /* 0x735CC */
        pB->nUnk7052 = 175;                      /* 0x735D4 */
        pB->nUnk7056 = PUFO_RESEARCH_PANEL_POSY; /* 0x735E0 */
        pB->nViewW = 30;                         /* 0x735E8 */
        UiPanel_SetBytes0to2(pB, 0xB4, 0xB4, 0xB4);                        /* 0x735F4 */
        UiPanel_SetBytes6to11(pB, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);     /* 0x73614 */
        pB->nUnk7076 = 2;                                                  /* 0x73628, right-aligned */
    }
    UiPanel *pC = BuySellNewPanel(pEnv->ppPanelC, PUFO_RESEARCH_TOPIC_COUNT);
    if (pC != NULL) {
        pC->nStyle = PUFO_RESEARCH_PANEL_STYLE;  /* 0x73668 */
        pC->nFont = PUFO_RESEARCH_PANEL_FONT;    /* 0x73678 */
        pC->nPosX = 233;                         /* 0x7368C */
        pC->nPosY = PUFO_RESEARCH_PANEL_POSY;    /* 0x7369C */
        pC->nUnk7052 = 205;                      /* 0x736A4 */
        pC->nUnk7056 = PUFO_RESEARCH_PANEL_POSY; /* 0x736B0 */
        pC->nViewW = 30;                         /* 0x736B8 */
        UiPanel_SetBytes0to2(pC, 0xB4, 0xB4, 0xB4);                        /* 0x736C4 */
        UiPanel_SetBytes6to11(pC, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);     /* 0x736E4 */
        pC->nUnk7076 = 2;                                                  /* 0x736FC */
    }

    /* Fill pass, 0x73700..0x73914: topics 1..count-1, bActive == 1 only, and the row counter advances
     * only for an active topic. Deviation: the original bounds only by the count byte (a byte, so up
     * to 255 of the 144 topics) and writes apLines[row] with no test; the port stops at the table. */
    if (pA != NULL && pEnv->aTopics != NULL) {
        const i32 nWalk = (nEntries < PUFO_RESEARCH_TOPIC_COUNT) ? nEntries : PUFO_RESEARCH_TOPIC_COUNT;
        for (i32 nTopic = 1; nTopic < nWalk; ++nTopic) {
            if (pEnv->aTopics[nTopic].bActive != 1) { /* 0x73734 LDRB [R10,#0xC], CMP #1 */
                continue;
            }
            /* 0x7373C: the topic's name is TEXTS[topic + 4], the same u16 the weapon fire gate reads. */
            const char *pszName = (pEnv->pTextRecs != NULL)
                                      ? pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * pEnv->aTopics[nTopic].wGate4
                                      : "";
            (void)UiPanel_SetLineText(pA, nRows, pszName);       /* 0x7375C..0x73774 */
            UiPanel_SetLineAux(pA, nRows, nTopic);               /* 0x7378C */

            const u16 wQty = (pEnv->apResearchCols != NULL && bBaseOk)
                                 ? pEnv->apResearchCols[PUFO_RESEARCH_TOPIC_COUNT * nBase + nTopic]
                                 : 0u;
            if (wQty != 0u) {
                char szNum[16];
                (void)snprintf(szNum, sizeof(szNum), "%u", (unsigned)wQty); /* j_itoa, 0x737CC */
                (void)UiPanel_SetLineText(pB, nRows, szNum);
            } else {
                (void)UiPanel_SetLineText(pB, nRows, ""); /* 0x7380C, the empty string at 0x10DA38 */
            }

            const u16 wProgress = pEnv->aTopics[nTopic].wProgress;
            const u16 wTotal = pEnv->aTopics[nTopic].wTotal;
            if (wProgress != 0u && wTotal != 0u) {
                /* ORIGINAL BUG (0x73870), reproduced: this divides before it multiplies -- _rt_sdiv on
                 * wProgress and wTotal, then MUL R0,R3,#0x64 on the quotient -- while
                 * ResearchScreen_RefreshPanels at 0x73168 multiplies first and divides after. A topic
                 * one quarter done reads 0% from here and 25% from there, on the same data. */
                char szNum[16];
                (void)snprintf(szNum, sizeof(szNum), "%d%%",
                               100 * ((i32)wProgress / (i32)wTotal));
                (void)UiPanel_SetLineText(pC, nRows, szNum);
            } else {
                (void)UiPanel_SetLineText(pC, nRows, ""); /* 0x738C4 */
            }
            ++nRows; /* 0x738F0 */
        }
    }

    /* 0x73924, 0x73934, 0x7393C: all three panels end with the row count. */
    if (pA != NULL) { pA->nLineCount = nRows; }
    if (pB != NULL) { pB->nLineCount = nRows; }
    if (pC != NULL) { pC->nLineCount = nRows; }
}

/* The topic -> facility-kind map both switches encode as a jumptable permutation: case k of the
 * switch on (nTopic - 37) writes 1 into aBaseFacFlags[kind].bFlag20, and the kind is the offset from
 * 0x186E48 over the 28-byte record size -- 0x186E5C is kind 0, 0x186FC8 is kind 13, 0x186F74 is
 * kind 10. Case 10 (topic 47) has no target at all: its jumptable entry is the default label, which
 * is also where the other sixteen cases land. FAC_HANGAR is the kind no case writes. LoadGame
 * @ 0x4539C carries the same seventeen-entry table over the same base with the same -37 offset and
 * the same unmapped case 10, which is what pins the permutation down. */
static const i8 g_anResearchTopicFacKind[PUFO_BASEFAC_COUNT] = {
    FAC_ACCESS_LIFT,         /* 37 -> 0x186E5C */
    FAC_LIVING_QUARTERS,     /* 38 -> 0x186E78 */
    FAC_LABORATORY,          /* 39 -> 0x186E94 */
    FAC_WORKSHOP,            /* 40 -> 0x186EB0 */
    FAC_SHORT_RANGE_RADAR,   /* 41 -> 0x186ECC */
    FAC_LONG_RANGE_RADAR,    /* 42 -> 0x186EE8 */
    FAC_MISSILE_DEFENCES,    /* 43 -> 0x186FC8 */
    FAC_GENERAL_STORES,      /* 44 -> 0x186F04 */
    FAC_ALIEN_CONTAINMENT,   /* 45 -> 0x186F20 */
    FAC_LASER_DEFENCE,       /* 46 -> 0x186FE4 */
    -1,                      /* 47: the default entry, no flag written */
    FAC_PLASMA_DEFENCE,      /* 48 -> 0x187000 */
    FAC_FUSION_BALL_DEFENCE, /* 49 -> 0x18701C */
    FAC_GRAVITY_SHIELD,      /* 50 -> 0x186FAC */
    FAC_MIND_SHIELD,         /* 51 -> 0x186F74 */
    FAC_PSI_LABORATORY,      /* 52 -> 0x186F3C */
    FAC_HYPER_WAVE_DECODER   /* 53 -> 0x186F58 */
};

static void ResearchRebuild_MarkFacFlag(const ResearchRebuildEnv *pEnv, i32 nTopic)
{
    const i32 nCase = nTopic - PUFO_RESEARCH_FAC_TOPIC_BASE; /* 0x74454 SUB R10,R5,#0x25 */
    if (nCase < 0 || nCase >= PUFO_BASEFAC_COUNT) {
        return;
    }
    const i32 nKind = g_anResearchTopicFacKind[nCase];
    if (nKind < 0) {
        return; /* 47 */
    }
    pEnv->paFac[nKind].bFlag20 = 1;
}

/* 0x745AC / 0x74868: the two (lo,hi) pairs live at 0x15EF08 + 34*(nTopic + 1) -- the four head bytes
 * of the record AFTER the one being processed, not of the record itself. For nTopic == 143 that
 * address is 0x160228, the four-byte gap between the 144-record table and auResearchCol @ 0x16022C;
 * nothing in the IDB writes it, so the port reads zeros there, which makes lo == hi and skips the
 * roll for the last topic. */
static const u8 g_abTopicRangeZero[4] = { 0, 0, 0, 0 };

static const u8 *ResearchTopic_RangeBytes(const ResearchTopicLive *aTopics, i32 nTopic)
{
    if (nTopic + 1 >= PUFO_RESEARCH_TOPIC_COUNT) {
        return g_abTopicRangeZero;
    }
    return aTopics[nTopic + 1].abUnk0;
}

/* 0x74780: the line the finished branch appended is overwritten in place, at the index the Append
 * just filled (nLineCount - 1, and 0 - 1 wraps to 255 the way the ARM's SUB R0,#1 does). */
static void ResearchRebuild_RewriteLine(const ResearchRebuildEnv *pEnv, i32 nTopic, u16 nRoll)
{
    if (pEnv->pLines == NULL) {
        return;
    }
    UiLineList_SetLine(pEnv->pLines, (u8)(pEnv->pLines->nLineCount - 1), PUFO_RESEARCH_LINE_FLAG,
                       (u32)(u8)nTopic, (u32)nRoll, PUFO_RESEARCH_LINE_COLOR2, 0u);
}

/* ResearchScreen_RebuildTopicList @ 0x74270 -- the evidence block is in pufo_baseui.h. */
i32 ResearchScreen_RebuildTopicList(const ResearchRebuildEnv *pEnv)
{
    ResearchTopicLive *const aTopics = pEnv->aTopics;
    const i32 nEntryCount = (i32)*pEnv->pbEntryCount;
    const i8 nBase = *pEnv->pnSelectedBaseIdx;
    const i32 nLast = PUFO_RESEARCH_TOPIC_COUNT - 1;

    /* 1. 0x74294..0x742E4: record 143 unlocks when all five of its prerequisites are unlocked. The
     * product is masked to a byte after every multiply (AND R2,R2,#0xFF), so it is 1 only when every
     * prerequisite is exactly 1. */
    if (aTopics[nLast].bUnlocked == 0) {
        u8 bAll = 1;
        for (i32 i = 0; i < 5; ++i) { /* 0x742AC LDRB [R3,R1] over record 143 +4..+8 */
            bAll = (u8)(bAll * aTopics[aTopics[nLast].abPreTopics[i]].bUnlocked);
        }
        if (bAll == 1) {
            aTopics[nLast].bUnlocked = 1;
        }
    }

    if (nEntryCount <= 1) {
        return nEntryCount; /* 0x74300 BLS loc_7490C */
    }

    for (i32 nTopic = 1; nTopic < nEntryCount; ++nTopic) { /* 0x74314, topic 0 is the screen block */
        ResearchTopicLive *pT = &aTopics[nTopic];

        if (pT->bUnlocked == 1) { /* 0x74334 */
            pEnv->abResearchZero[nTopic] = 1;
            if (Research_TopicNeedsItem(aTopics, pEnv->apObjects, nTopic, nBase) == 0) {
                pT->bActive = 0; /* 0x74360 */
            }
        }

        /* 0x74364: the gate runs unless the topic is locked with progress already booked, and a
         * failure skips the whole iteration (0x74388). */
        if (pT->bUnlocked != 0 || pT->wTotal == 0) {
            if (Research_TopicNeedsItem(aTopics, pEnv->apObjects, nTopic, nBase) == 0) {
                continue;
            }
        }

        if ((i32)pT->wTotal > (i32)pT->wProgress) {
            /* 5a. 0x747A0..0x748E8: still running. Rebuild the availability gate. */
            u8 bGate = 1;
            i32 nSum = 0;
            for (i32 i = 0; i < 5; ++i) { /* 0x747AC */
                const u8 nPre = pT->abPreTopics[i];
                if (nPre != 0) {
                    nSum += nPre; /* 0x747BC: the original adds the topic numbers themselves */
                    bGate = (u8)(bGate * aTopics[nPre].bUnlocked);
                }
            }
            for (i32 i = 0; i < 5; ++i) { /* 0x747E4, the five needed-item bytes */
                const u8 nItem = pT->abNeedItems[i];
                if (nItem == 0) {
                    continue;
                }
                if (nItem == 0xFE || nItem == 0xFF) { /* 0x747F4..0x74800 */
                    bGate = 0;
                }
                nSum += nItem; /* 0x74808 */
                if ((i32)nItem <= (i32)*pEnv->pnObjectCount) { /* 0x74814..0x74824 */
                    if (pEnv->apObjects[nItem]->auPerBaseVal[nBase] == 0) { /* 0x74848 */
                        bGate = 0;
                        pT->bActive = 0; /* 0x74858 */
                    }
                }
            }
            const u8 *const pbRange = ResearchTopic_RangeBytes(aTopics, nTopic);
            const i32 nLo2 = pbRange[2]; /* 0x7486C */
            const i32 nHi2 = pbRange[3]; /* 0x74868 */
            const i32 nTotal = nLo2 + nHi2 + nSum; /* 0x74874 */
            if (nHi2 != nLo2) { /* 0x7487C */
                const i32 nLimit = nHi2 + 1;
                if (nLo2 >= nLimit) { /* 0x74888 */
                    bGate = 0;
                } else {
                    i32 nScan = nLo2; /* 0x74894..0x748C8 */
                    while (aTopics[nScan].bUnlocked != 1 && pEnv->abResearchZero[nScan] != 1) {
                        ++nScan;
                        if (nScan >= nLimit) {
                            bGate = 0;
                            break;
                        }
                    }
                }
            }
            if (bGate == 1 && nTotal != 0) { /* 0x748D0..0x748E8 */
                pT->bActive = 1;
            }
            continue;
        }

        /* 5b. 0x743A4..0x74798: finished. */
        if (Research_TopicNeedsItem(aTopics, pEnv->apObjects, nTopic, nBase) != 0) {
            BaseRec_SubItemQty(pEnv->apBases[nBase], pEnv->apObjects[pT->abNeedItems[0]], 1); /* 0x743E8 */
            pT->wProgress = 0; /* 0x743F8 */
        }
        pT->bUnlocked = 1; /* 0x7440C */
        *pEnv->pbFinishedTopic = (u8)nTopic; /* 0x74414 STRB */
        *pEnv->pnShownTopic = nTopic;        /* 0x74424 STR */
        u16 *const pCol = &pEnv->apResearchCols[nTopic];
        for (i32 b = 0; b < PUFO_FAC_BASE_COUNT; ++b) { /* 0x7442C..0x74444, eight bases */
            pCol[b * PUFO_RESEARCH_TOPIC_COUNT] = 0;
        }
        *pEnv->pdwShortfall = 1; /* 0x74458, a dword store into byte_1870C8 */
        ResearchRebuild_MarkFacFlag(pEnv, nTopic);
        UiLineList_AppendWithSound(pEnv->pLines, PUFO_RESEARCH_LINE_FLAG, (u32)(u8)nTopic,
                                   PUFO_LINE_COLOR_MAX, PUFO_LINE_COLOR_MAX, PUFO_LINE_COLOR_MAX,
                                   pEnv->pfnPlaySample, pEnv->pUser); /* 0x74554 */
        if (Research_TopicNeedsItem(aTopics, pEnv->apObjects, nTopic, nBase) == 0) {
            pT->bActive = 0; /* 0x7456C */
        }
        pEnv->abResearchZero[nTopic] = 1; /* 0x7457C */
        for (i32 i = 0; i < 5; ++i) { /* 0x74584..0x745A8 */
            const u8 nOn = pT->abActivateTopics[i];
            if (nOn != 0) {
                aTopics[nOn].bActive = 1;
            }
        }

        /* 6. 0x745AC..0x74660: unlock one topic out of the range in the next record's head bytes. */
        const u8 *const pbRange = ResearchTopic_RangeBytes(aTopics, nTopic);
        const i32 nLo = pbRange[0];
        const i32 nHi = pbRange[1];
        if (nHi == nLo) {
            continue; /* 0x745B4 RSBS / BEQ */
        }
        u16 nRoll = 0;
        for (i32 nTry = 1; ; ++nTry) { /* 0x745C0..0x74610 */
            nRoll = Game_RandU16((u16)(nHi - nLo + 1));
            const i32 nPick = nLo + (i32)nRoll;
            if (aTopics[nPick].bActive != 1 && aTopics[nPick].bUnlocked != 1) {
                break;
            }
            if (nTry >= PUFO_RESEARCH_UNLOCK_TRIES) {
                break; /* 0x7460C: after 50 tries the original takes what it landed on */
            }
        }
        const i32 nPick = nLo + (i32)nRoll;
        aTopics[nPick].bUnlocked = 1; /* 0x74630 */
        *pEnv->pnShownTopic = nPick;  /* 0x74644 */
        for (i32 b = 0; b < PUFO_FAC_BASE_COUNT; ++b) {
            /* ORIGINAL BUG (0x74648), reproduced: `MOV R0,R7` and R7 still holds
             * &auResearchCol[nTopic] from 0x74428, while the newly unlocked index (R4 = idx + roll)
             * was only ever used for the bUnlocked store and nResearchTopicShown. So this zeroes the
             * same eight slots the finished branch already zeroed, and the topic that just got
             * unlocked keeps whatever allocation it carried -- ResearchScreen_AccumulateAllocated
             * (0x74914) books progress for locked topics too, so a locked topic really can arrive
             * with a non-zero entry. Reproduced, not corrected. */
            pCol[b * PUFO_RESEARCH_TOPIC_COUNT] = 0;
        }
        ResearchRebuild_MarkFacFlag(pEnv, nTopic); /* 0x74668, the second switch */
        if (pEnv->abResearchZero[nPick] == 0) { /* 0x7474C */
            ResearchRebuild_RewriteLine(pEnv, nTopic, nRoll);
        }
        pEnv->abResearchZero[nPick] = 1; /* 0x74798 */
    }

    return nEntryCount; /* 0x74904: the loop leaves the count in R0 */
}

static const char *ResearchUi_Text(const ResearchDrawEnv *pEnv, i32 nRec)
{
    if (pEnv->pTextRecs == NULL) {
        return "";
    }
    return pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec; /* the original's own label pointers */
}

static void ResearchUi_DrawCentered(i32 nCenterX, i32 y, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz); /* 0x73CC8, 0x740FC, 0x7415C, 0x741E0 */
    Text_DrawString(nCenterX - nWidth / 2, y, psz);
}

/* ResearchScreen_Draw @ 0x73a34: the research screen's frame. The header carries the branch map. */
void ResearchScreen_Draw(const ResearchDrawEnv *pEnv)
{
    char szLabel[PUFO_RESEARCH_LABEL_MAX];
    char szNum[52]; /* var_34 @ sp+0x80, the j_itoa buffer */

    if (pEnv == NULL) {
        return;
    }

    if (pEnv->pPad != NULL) {
        PadImage_Blit(pEnv->pPad); /* 0x73A40: *ppScreen, the pad inside aResearchTopics[0] */
    }

    BaseUi_DrawFrameRails(); /* 0x73A44..0x73BE0 */

    if (pEnv->pbPanelsBuilt == NULL || *pEnv->pbPanelsBuilt == 0) {
        return; /* 0x73BE8: no panels, no labels, and not even the close control */
    }

    if (pEnv->pPanelTopic != NULL) {
        UiPanel_DrawText(pEnv->pPanelTopic); /* 0x73BFC */
    }
    if (pEnv->pPanelQty != NULL) {
        UiPanel_DrawText(pEnv->pPanelQty); /* 0x73C08 */
    }
    if (pEnv->pPanelProgress != NULL) {
        UiPanel_DrawText(pEnv->pPanelProgress); /* 0x73C14 */
    }

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 0, 0, 0); /* 0x73C34 */
    pufo_strncpy(szLabel, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_HEADER), sizeof(szLabel));
    Text_DrawString(10, 52, szLabel); /* 0x73C74 */

    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    const i32 nEntries = (pEnv->pbEntryCount != NULL) ? (i32)*pEnv->pbEntryCount : 0;

    pufo_strncpy(szLabel, (pEnv->apSiteRecs != NULL && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT)
                              ? (const char *)pEnv->apSiteRecs[nBase].abName
                              : "",
                  sizeof(szLabel));
    ResearchUi_DrawCentered(120, 13, szLabel); /* 0x73CE0 */

    /* 0x73D00..0x73D60: the two column separators, over the TOPIC panel's row band. */
    i32 nBand = 0;
    if (pEnv->pPanelTopic != NULL) {
        nBand = 16 * pEnv->pPanelTopic->nLineCount;
        if (pEnv->pPanelTopic->nLineCount >= pEnv->pPanelTopic->nStyle) {
            nBand = 16 * pEnv->pPanelTopic->nStyle; /* 0x73D1C */
        }
    }
    SoftSurf_BlitRectProxy(0, 170, 69, 1, nBand + 2, PUFO_RESEARCH_BAR_INDEX,
                           PUFO_RESEARCH_BAR_INDEX, PUFO_RESEARCH_BAR_INDEX); /* 0x73D30 */
    SoftSurf_BlitRectProxy(0, 200, 69, 1, nBand + 2, PUFO_RESEARCH_BAR_INDEX,
                           PUFO_RESEARCH_BAR_INDEX, PUFO_RESEARCH_BAR_INDEX); /* 0x73D54 */

    if (pEnv->pbTopicSelected == NULL || *pEnv->pbTopicSelected == 0) {
        /* 0x73D64: no topic row selected -- the close control, and nothing else. This is the only
         * path in the function that blits unk_186B00. */
        (void)UiControl_BlitActivePad(pEnv->pCtlClose, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlUser);
        return;
    }

    /* 0x73DA0: the detail panel. Only the low byte of the result is tested (0x73DA8). */
    if (!Ui_AnimExpandBlit(pEnv->pAnim, 0, 110, 236, 100, 0x32, 0x96, 0x32, 0x96, 4)) {
        return;
    }

    pufo_strncpy(szLabel, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_SCI_FREE), sizeof(szLabel));
    Text_DrawString(10, 118, szLabel); /* 0x73DE8: "Ученых доступно" */
    pufo_strncpy(szLabel, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_LAB_FREE), sizeof(szLabel));
    Text_DrawString(10, 132, szLabel); /* 0x73E28: "Свободн. мест в лаборатории " */
    pufo_strncpy(szLabel, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_SCI_USED), sizeof(szLabel));
    Text_DrawString(10, 146, szLabel); /* 0x73E64: "Ученых задействовано" */

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x73E8C */
    pufo_strncpy(szLabel, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_ADD), sizeof(szLabel));
    Text_DrawString(40, 164, szLabel); /* 0x73EC8: "Добав." */
    pufo_strncpy(szLabel, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_REMOVE), sizeof(szLabel));
    Text_DrawString(40, 184, szLabel); /* 0x73F04: "Убрать" */

    /* 0x73F24..0x74068: the three numbers, each through j_itoa and drawn at x 195. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 0, 0, 0);
    const bool bBase = (nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT);
    const u16 *pCol = (pEnv->apResearchCols != NULL && bBase)
                          ? pEnv->apResearchCols + PUFO_RESEARCH_TOPIC_COUNT * nBase : NULL;
    const i32 nScientists = (pEnv->pScientistRec != NULL && pCol != NULL)
                                ? Base_GetScientistCount(pEnv->pScientistRec, pCol, nBase, nEntries)
                                : 0;
    (void)snprintf(szNum, sizeof(szNum), "%d", nScientists); /* 0x73F44 */
    Text_DrawString(195, 118, szNum);

    {
        const i32 nCap = (pEnv->abFacilityGrid != NULL)
                             ? Base_CalcLabCap(pEnv->abFacilityGrid, nBase) : 0;
        const i32 nActive = (pCol != NULL) ? Base_CountActiveResearch(pCol, nEntries) : 0;
        (void)snprintf(szNum, sizeof(szNum), "%d", nCap - nActive); /* 0x73FB0: SUB R0,R0,R1 */
    }
    Text_DrawString(195, 132, szNum);

    {
        const i32 nTopic = ManufSelectedProduct(pEnv->pPanelTopic, pEnv->pnTopicRow); /* 0x74028 */
        const u16 nQty = (nTopic >= 0 && nTopic < PUFO_RESEARCH_TOPIC_COUNT && pCol != NULL)
                             ? pCol[nTopic]
                             : 0u;
        (void)snprintf(szNum, sizeof(szNum), "%u", (unsigned)nQty); /* 0x74038 */
    }
    Text_DrawString(195, 146, szNum);

    /* 0x7406C..0x7408C: the two quantity controls, screen-block slots 1876 and 1877. */
    (void)UiControl_BlitActivePad(pEnv->apCtlQty[0], pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlUser);
    (void)UiControl_BlitActivePad(pEnv->apCtlQty[1], pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlUser);

    (void)UiControl_BlitActivePad(pEnv->pCtlCancel, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlUser); /* 0x7409C */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x740BC */
    ResearchUi_DrawCentered(198, 189, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_CANCEL)); /* 0x74114 */

    (void)UiControl_BlitActivePad(pEnv->pCtlStart, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlUser); /* 0x74120 */
    ResearchUi_DrawCentered(198, 167, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_OK)); /* 0x74174 */

    /* 0x74184..0x741A4: the allocate-all control is re-pointed and moved on every frame. Note the
     * order against the manufacturing screen's two buttons: here the handler goes in first. */
    if (pEnv->pCtlAllocAll != NULL) {
        pEnv->pCtlAllocAll->pfnClick = pEnv->pfnAllocAllClick; /* 0x74184 */
        pEnv->pCtlAllocAll->x = PUFO_RESEARCH_ALLOC_X;         /* 0x74190 */
        pEnv->pCtlAllocAll->y = PUFO_RESEARCH_ALLOC_Y;         /* 0x7419C */
        (void)UiControl_BlitActivePad(pEnv->pCtlAllocAll, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                      pEnv->pCtlUser); /* 0x741A4 */
    }

    ResearchUi_DrawCentered(119, 165, ResearchUi_Text(pEnv, PUFO_RESEARCH_TEXT_ALL)); /* 0x741F8 */
}

static const char *BuySellUi_Text(const BuySellDrawEnv *pEnv, i32 nRec)
{
    if (pEnv->pTextRecs == NULL) {
        return "";
    }
    return pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec; /* the original's own label pointers */
}

static void BuySellUi_DrawCentered(i32 nCenterX, i32 y, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz); /* 0x78130, 0x78290, 0x782E8, 0x787C0, 0x78818, 0x78C10 */
    Text_DrawString(nCenterX - nWidth / 2, y, psz);
}

/* apObjects[slot]'s name, or empty when the slot is outside the array. The original derefs whatever
 * the marker names, with no test on the slot. */
static const char *BuySellUi_ObjectName(const BuySellDrawEnv *pEnv, i32 nSlot)
{
    if (pEnv->apObjects == NULL || nSlot < 0 || nSlot >= pEnv->nObjectCount
        || pEnv->apObjects[nSlot] == NULL) {
        return "";
    }
    return (const char *)pEnv->apObjects[nSlot]->szName; /* szName is the record's first byte */
}

/* The "no row under the cursor" tail, shared by both halves through the original's LABEL_65/66/67. */
static void BuySellUi_DrawOk(const BuySellDrawEnv *pEnv, char *szLabel, size_t cchLabel)
{
    (void)UiControl_BlitActivePad(pEnv->pCtlOk, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx, pEnv->pCtlUser);
    pufo_strncpy(szLabel, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_OK), cchLabel); /* 0x78BB0 / 0x78664 */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);            /* LABEL_65, 0x78BC8 */
    BuySellUi_DrawCentered(120, 297, szLabel);                                  /* LABEL_67, 0x78C18 */
}

/* BuySellScreen_Draw @ 0x77e6c: the buy/sell screen's frame. The header carries the branch map. */
void BuySellScreen_Draw(const BuySellDrawEnv *pEnv)
{
    char szLabel[PUFO_BUYSELL_LABEL_MAX];
    char szNum[52]; /* var_34 @ sp+0x50, the j_itoa buffer */
    i32 nBand = 0;

    if (pEnv == NULL) {
        return;
    }

    BaseUi_DrawFrameRails(); /* 0x77E80..0x78014 */

    (void)UiControl_BlitActivePad(pEnv->pCtlClose, pEnv->pfnCtlBlit, pEnv->pfnCtlBlitEx,
                                  pEnv->pCtlUser); /* 0x78028: unconditional, unlike the research frame */

    /* 0x7804C..0x780C8: the funds line. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 120, 255, 120, 10, 10, 10);
    pufo_strncpy(szLabel, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_FUNDS), sizeof(szLabel));
    (void)snprintf(szNum, sizeof(szNum), "%d", (pEnv->pnFunds != NULL) ? *pEnv->pnFunds : 0); /* 0x78090 */
    (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
    (void)pufo_strcat(szLabel, " $", sizeof(szLabel)); /* asc_ABC28, 0x780A8 */
    (void)Text_MeasureWidth(szLabel);                  /* 0x780B4: measured, then thrown away */
    Text_DrawString(10, 28, szLabel);

    /* 0x780E8..0x78150: the selected site's name. */
    const i32 nBase = (pEnv->pnSelectedBaseIdx != NULL) ? (i32)*pEnv->pnSelectedBaseIdx : 0;
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 180, 180, 180, 10, 10, 10);
    pufo_strncpy(szLabel, (pEnv->apSiteRecs != NULL && nBase >= 0 && nBase < PUFO_FAC_BASE_COUNT)
                              ? (const char *)pEnv->apSiteRecs[nBase].abName
                              : "",
                  sizeof(szLabel));
    BuySellUi_DrawCentered(120, 10, szLabel);

    const bool bBuilt = (pEnv->pbPanelsBuilt != NULL && *pEnv->pbPanelsBuilt != 0);
    const i32 nMarker = ManufSelectedProduct(pEnv->pPanelA, pEnv->pnRowCursor); /* 0x78474 / 0x78B54 */

    if (!bBuilt) {
        /* 0x786BC: with the controls gone too there is nothing left to draw. */
        if (pEnv->pbControlsBuilt == NULL || *pEnv->pbControlsBuilt == 0) {
            return;
        }

        /* 0x786E4..0x78768: the total line, in the red pair. */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 255, 120, 120, 10, 10, 10);
        pufo_strncpy(szLabel, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_TOTAL_DOWN), sizeof(szLabel));
        (void)pufo_strcat(szLabel, ":", sizeof(szLabel)); /* asc_AB078, 0x7871C */
        (void)snprintf(szNum, sizeof(szNum), "%d",
                       (pEnv->pnTransTotal != NULL) ? *pEnv->pnTransTotal : 0); /* 0x78730 */
        (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
        (void)pufo_strcat(szLabel, " $", sizeof(szLabel));
        (void)Text_MeasureWidth(szLabel); /* 0x78754: measured, then thrown away */
        Text_DrawString(10, 40, szLabel);

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 180, 180, 250, 10, 10, 10);
        BuySellUi_DrawCentered(210, 54, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_COL_LEFT));  /* 0x787E0 */
        BuySellUi_DrawCentered(155, 54, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_COL_RIGHT)); /* 0x78838 */

        /* ORIGINAL BUG (0x78864..0x78B80), not reproduced. This half runs while bBuySellPanelsBuilt
         * is 0, and BuySellScreen_BuildPanels' teardown (0x77D60..0x77E38) operator delete's all five
         * panels without ever nulling pBuySellPanelA..E -- the last thing it does is the funds
         * rollback at 0x77E50. So the original writes bFlag7073 and bUnk7080 into freed panels here,
         * reads panel B's nLineCount at 0x788E4 and panel A's aux column at 0x78B54, and uses the row
         * marker raw at 0x78B80 with no 2000 subtraction, so an alien row would deref
         * apObjects[2077]. The port null-guards the panels and bounds the marker, which is why an
         * alien row in this half draws nothing where the original would read past the object array. */
        /* 0x78864..0x788A4: the only bFlag7073 writes in this function, and the shared scratch byte. */
        const u8 bNotShort = (u8)((pEnv->pdwShortfall != NULL && *pEnv->pdwShortfall != 1u) ? 1u : 0u);
        if (pEnv->pPanelB != NULL) {
            pEnv->pPanelB->bFlag7073 = bNotShort;
        }
        if (pEnv->pPanelC != NULL) {
            pEnv->pPanelC->bFlag7073 = bNotShort;
        }
        const u8 bScratch = (pEnv->pbPanelFlagScratch != NULL) ? *pEnv->pbPanelFlagScratch : 0u;
        if (pEnv->pPanelB != NULL) {
            pEnv->pPanelB->bUnk7080 = bScratch;
        }
        if (pEnv->pPanelC != NULL) {
            pEnv->pPanelC->bUnk7080 = bScratch;
        }
        if (pEnv->pbPanelFlagScratch != NULL) {
            *pEnv->pbPanelFlagScratch = 0; /* 0x788A4 */
        }
    } else {
        /* 0x78180..0x7819C: the scratch byte only -- no bFlag7073 write on this path. */
        const u8 bScratch = (pEnv->pbPanelFlagScratch != NULL) ? *pEnv->pbPanelFlagScratch : 0u;
        if (pEnv->pPanelB != NULL) {
            pEnv->pPanelB->bUnk7080 = bScratch;
        }
        if (pEnv->pPanelC != NULL) {
            pEnv->pPanelC->bUnk7080 = bScratch;
        }
        if (pEnv->pbPanelFlagScratch != NULL) {
            *pEnv->pbPanelFlagScratch = 0; /* 0x7819C */
        }

        /* 0x781B8..0x78238: the total line, in the blue pair. */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 120, 120, 255, 10, 10, 10);
        pufo_strncpy(szLabel, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_TOTAL_UP), sizeof(szLabel));
        (void)pufo_strcat(szLabel, ":", sizeof(szLabel)); /* asc_AB078, 0x781EC */
        (void)snprintf(szNum, sizeof(szNum), "%d",
                       (pEnv->pnTransTotal != NULL) ? *pEnv->pnTransTotal : 0); /* 0x78200 */
        (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
        (void)pufo_strcat(szLabel, " $", sizeof(szLabel));
        (void)Text_MeasureWidth(szLabel); /* 0x78224: measured, then thrown away */
        Text_DrawString(10, 40, szLabel);

        /* 0x78258..0x78308: the two column captions, on the other sides from the other half. */
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 180, 180, 250, 10, 10, 10);
        BuySellUi_DrawCentered(145, 54, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_COL_LEFT));  /* 0x782B0 */
        BuySellUi_DrawCentered(212, 54, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_COL_RIGHT)); /* 0x78308 */
    }

    /* 0x78314..0x7833C / 0x788AC..0x788D4: the five panels, in the order A, E, C, B, D. */
    if (pEnv->pPanelA != NULL) {
        UiPanel_DrawText(pEnv->pPanelA);
    }
    if (pEnv->pPanelE != NULL) {
        UiPanel_DrawText(pEnv->pPanelE);
    }
    if (pEnv->pPanelC != NULL) {
        UiPanel_DrawText(pEnv->pPanelC);
    }
    if (pEnv->pPanelB != NULL) {
        UiPanel_DrawText(pEnv->pPanelB);
    }
    if (pEnv->pPanelD != NULL) {
        UiPanel_DrawText(pEnv->pPanelD);
    }

    /* The band is 16 * min(nLineCount, nStyle) of panel B, the decrement column. */
    if (pEnv->pPanelB != NULL) {
        nBand = 16 * pEnv->pPanelB->nLineCount;
        if (pEnv->pPanelB->nLineCount >= pEnv->pPanelB->nStyle) {
            nBand = 16 * pEnv->pPanelB->nStyle; /* 0x7836C / 0x78904 */
        }
    }

    if (!bBuilt) {
        SoftSurf_BlitRectProxy(0, 130, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                               PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x78920 */
        SoftSurf_BlitRectProxy(0, 176, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                               PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x78944 */
        SoftSurf_BlitRectProxy(0, 191, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                               PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x78968 */
        SoftSurf_BlitRectProxy(0, 206, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                               PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x7898C */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30); /* 0x789B4 */

        /* 0x789C0..0x78C24: the shortfall panel, on this half only. */
        if (pEnv->pdwShortfall != NULL && *pEnv->pdwShortfall == 1u) {
            const i32 nReason = (pEnv->pnShortfallReason != NULL) ? *pEnv->pnShortfallReason : -1;
            if (nReason >= 0 && nReason <= 3) {
                (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1);
                BuySellUi_DrawCentered(120, 158,
                                       BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_SHORTFALL + nReason));
                return;
            }
            /* 0x789D0 default: break -- reasons 4 and up fall into the cursor block. */
        }

        const bool bRow = (pEnv->pbPreviewOk != NULL && *pEnv->pbPreviewOk != 0
                           && pEnv->pnCursorY != NULL
                           && *pEnv->pnCursorY > PUFO_BUYSELL_CURSOR_Y_OFF
                           && *pEnv->pnCursorY < nBand + PUFO_BUYSELL_CURSOR_Y_OFF);
        if (bRow) {
            if (pEnv->pbCursorOnRow != NULL) {
                *pEnv->pbCursorOnRow = 1; /* 0x78B14 */
            }
            (void)Gfx_PackRgb565Pair(pEnv->pColor, 150, 150, 255, 20, 20, 55); /* 0x78B38 */
            if (nMarker == PUFO_ROW_SOLDIER) {
                Text_DrawString(10, 297, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_SOLDIER)); /* 0x78B74 */
            } else {
                Text_DrawString(10, 297, BuySellUi_ObjectName(pEnv, nMarker)); /* 0x78B80 */
            }
            return;
        }
        if (pEnv->pbCursorOnRow != NULL) {
            *pEnv->pbCursorOnRow = 0; /* 0x78B94 */
        }
        BuySellUi_DrawOk(pEnv, szLabel, sizeof(szLabel));
        return;
    }

    SoftSurf_BlitRectProxy(0, 130, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                           PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x78388 */
    SoftSurf_BlitRectProxy(0, 160, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                           PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x783AC */
    SoftSurf_BlitRectProxy(0, 175, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                           PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x783D0 */
    SoftSurf_BlitRectProxy(0, 190, 69, 1, nBand + 2, PUFO_BUYSELL_SEP_INDEX,
                           PUFO_BUYSELL_SEP_INDEX, PUFO_BUYSELL_SEP_INDEX); /* 0x783F4 */

    const bool bRow = (pEnv->pbPreviewOk != NULL && *pEnv->pbPreviewOk != 0
                       && pEnv->pnCursorY != NULL
                       && *pEnv->pnCursorY > PUFO_BUYSELL_CURSOR_Y_OFF
                       && *pEnv->pnCursorY < nBand + PUFO_BUYSELL_CURSOR_Y_OFF);
    if (!bRow) {
        BuySellUi_DrawOk(pEnv, szLabel, sizeof(szLabel)); /* 0x78654..0x7867C, then LABEL_65 */
        return;
    }

    /* Note unk_161A30 is written only by the other half: every one of its five cross-references
     * (0x78B08, 0x78B14, 0x78B88, 0x78B8C, 0x78B94) sits in the panels-not-built tail. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 150, 150, 255, 20, 20, 55); /* 0x78448 */

    if (nMarker == PUFO_ROW_SOLDIER) {
        /* 0x78478..0x784C8: the soldier index is panel B's aux cell, not panel A's -- panel A only
         * carries the marker. Soldier.szName is at +0xBA. */
        const i32 nRow = (pEnv->pnRowCursor != NULL) ? *pEnv->pnRowCursor : -1;
        const i32 nSol = (pEnv->pPanelB != NULL && nRow >= 0 && nRow < PUFO_PANEL_LINES)
                             ? pEnv->pPanelB->anLineAux[nRow] : -1;
        const Soldier *pSol = (pEnv->apSoldiers != NULL && nSol >= 0 && nSol < pEnv->nSoldierCount)
                                  ? pEnv->apSoldiers[nSol] : NULL;
        Text_DrawString(10, 281, (pSol != NULL) ? pSol->szName : ""); /* 0x784A8 */
        Text_DrawString(10, 297, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_SOL_NAME)); /* 0x784C8 */
    } else if (nMarker > PUFO_ROW_ALIEN_OFFSET) {
        /* 0x784E8..0x78520: an alien row carries its slot plus 2000. */
        Text_DrawString(10, 281, BuySellUi_ObjectName(pEnv, nMarker - PUFO_ROW_ALIEN_OFFSET));
        Text_DrawString(10, 297, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_ALIEN));
    } else if (nMarker >= 0) {
        /* 0x78540..0x785B8: a plain object or craft row, named and priced from its own record. */
        Text_DrawString(10, 281, BuySellUi_ObjectName(pEnv, nMarker));
        pufo_strncpy(szLabel, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_PRICE), sizeof(szLabel));
        const u32 nPrice = (pEnv->apObjects != NULL && nMarker < pEnv->nObjectCount
                            && pEnv->apObjects[nMarker] != NULL)
                               ? pEnv->apObjects[nMarker]->uPrice /* ObjectRecFull.uPrice, +0xBC */
                               : 0u;
        (void)snprintf(szNum, sizeof(szNum), "%u", nPrice); /* 0x785AC */
        (void)pufo_strcat(szLabel, szNum, sizeof(szLabel));
        if (nMarker > PUFO_ROW_CRAFT_FIRST - 1 && nMarker < PUFO_ROW_CRAFT_LAST + 1) {
            /* 0x785D8: the five craft rows get their own caption instead of the price. */
            Text_DrawString(10, 297, BuySellUi_Text(pEnv, PUFO_BUYSELL_TEXT_CRAFT));
        } else {
            Text_DrawString(10, 297, szLabel);
        }
    }
}

/* ---------------------------------------------------- the plane screen's stat rows */

i32 BasePlane_ResetDetailState(const BasePlaneDetailEnv *pEnv)
{
    i32 nRows = 1;                                          /* 0x79D20: the header row is always there */
    i32 i;

    *pEnv->pnCraftCannon  = 0;                              /* 0x79CC0..0x79D1C, the six cleared */
    *pEnv->pnStingray     = 0;
    *pEnv->pnAvalanche    = 0;
    *pEnv->pnLaserCannon  = 0;
    *pEnv->pnPlasmaCannon = 0;
    *pEnv->pnFusionBall   = 0;
    *pEnv->pnRowCount     = 1;

    if (pEnv->apObjects == NULL
        || pEnv->nBaseIdx < 0
        || pEnv->nBaseIdx >= PUFO_FAC_BASE_COUNT) {
        /* Deviation: 0x79D5C dereferences apObjects[i] and indexes auPerBaseVal with
         * 2 * (char)nSelectedBaseIdx with no test at all, so a negative or oversized base index reads
         * outside the record. No shipped path does that; the port reaches the same observable result --
         * six zeroes and one row -- without the out-of-bounds read. */
        return nRows;
    }

    for (i = 0; i < pEnv->nObjectCount; ++i) {              /* 0x79D48: every slot, not just the six */
        const ObjectRecFull *pObj = pEnv->apObjects[i];
        u16 nCount;

        if (pObj == NULL) {
            continue;                                       /* the original would fault at 0x79D5C */
        }
        if (i < PUFO_PLANE_WEAPON_FIRST || i > PUFO_PLANE_WEAPON_LAST) {
            continue;                                       /* the switch's default arm discards the read */
        }
        nCount = pObj->auPerBaseVal[pEnv->nBaseIdx];        /* 0x79D5C LDRH [R3,#0x2A] */
        if (nCount == 0) {
            continue;
        }
        switch (i) {
        case 25: *pEnv->pnCraftCannon  = nCount; break;     /* 0x79DDC */
        case 26: *pEnv->pnStingray     = nCount; break;     /* 0x79DD4 */
        case 27: *pEnv->pnAvalanche    = nCount; break;     /* 0x79DCC */
        case 28: *pEnv->pnLaserCannon  = nCount; break;     /* 0x79DC4 */
        case 29: *pEnv->pnPlasmaCannon = nCount; break;     /* 0x79DBC */
        case 30: *pEnv->pnFusionBall   = nCount; break;     /* 0x79D90 */
        default: break;
        }
        ++nRows;                                           /* 0x79DE4: nPlaneStatRowCount = ++result */
        *pEnv->pnRowCount = nRows;
    }

    return nRows;                                          /* 0x79DFC */
}

/* ---------------------------------------------------- the base-facility status line */

static const char *FacStatus_Text(const BaseFacStatusEnv *pEnv, i32 nRec)
{
    return (const char *)(pEnv->pTextRecs) + PUFO_TEXT_REC_STRIDE * (size_t)nRec;
}

/* The original tests none of these indices -- apObjects[] is dereferenced raw, the grid byte and the
 * topic index are computed with no bound, and aBaseFacFlags is indexed by a byte. The port returns
 * NULL for an index it cannot justify and the caller skips that one append. */
static const char *FacStatus_BaseName(const BaseFacStatusEnv *pEnv, i32 nBaseIdx)
{
    if (pEnv->apSiteRecs == NULL || nBaseIdx < 0 || nBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return NULL;
    }
    return pEnv->apSiteRecs[nBaseIdx].abName;   /* 0x186C24 + 64*i + 54 == 0x186C40 + 64*i + 26 */
}

static const char *FacStatus_ObjectName(const BaseFacStatusEnv *pEnv, i32 nSlot)
{
    if (pEnv->apObjects == NULL
        || nSlot < 0 || nSlot >= pEnv->nObjectCount
        || pEnv->apObjects[nSlot] == NULL) {
        return NULL;
    }
    return pEnv->apObjects[nSlot]->szName;      /* 0x6A6E8: LDR [R3,#0x108], then its first field */
}

static const char *FacStatus_TopicName(const BaseFacStatusEnv *pEnv, i32 nTopic)
{
    if (pEnv->aTopics == NULL || nTopic < 0 || nTopic >= PUFO_RESEARCH_TOPIC_COUNT) {
        return NULL;
    }
    /* 0x6A764 LDRH [R7,#4] then 0x6A770 MUL #0xCD against 0xB38D0: the topic's name record. */
    return FacStatus_Text(pEnv, pEnv->aTopics[nTopic].wGate4);
}

char *BaseFac_BuildStatusMsg(const BaseFacStatusEnv *pEnv, i32 nCase, i32 nBaseIdx,
                             i32 nFacRow, i32 nFacCol)
{
    char *pszMsg = pEnv->pszMsg;
    char *pszExtra = pEnv->pszExtra;
    const char *pszApp;
    i32 nKind;

    pEnv->pTime->bPaused = 1;                    /* 0x6A4D4: GameTime_Pause, ahead of the switch */

    if (nCase < PUFO_FAC_STATUS_CASE_FIRST || nCase > PUFO_FAC_STATUS_CASE_LAST) {
        return NULL;                             /* 0x6A4E0: SUB #0x20 / CMP #0xC / ADDLS PC */
    }

    switch (nCase) {
    case ' ':
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_BASE), pEnv->cchMsg);
        pszApp = FacStatus_BaseName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);
        }
        pufo_strncpy(pszExtra, "Product: ", pEnv->cchExtra); /* aProduct_0 @ 0xAB73C, a real literal */
        pszApp = FacStatus_ObjectName(pEnv, nFacCol);
        if (pszApp != NULL) {
            pufo_strcat(pszExtra, pszApp, pEnv->cchExtra);   /* LABEL_21 + LABEL_64 at 0x6AAA4 */
        }
        return pszExtra;

    case '!':
    case '"':                                    /* 0x6A5A0 and 0x6A610 are the same eleven operations */
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_BASE_SPACED), pEnv->cchMsg);
        pszApp = FacStatus_BaseName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);
        }
        pufo_strncpy(pszExtra, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_PROD), pEnv->cchExtra);
        pszApp = FacStatus_ObjectName(pEnv, nFacCol);
        if (pszApp != NULL) {
            pufo_strcat(pszExtra, pszApp, pEnv->cchExtra);
        }
        return pszExtra;

    case '#':
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_BASE), pEnv->cchMsg);
        pszApp = FacStatus_BaseName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);
        }
        pufo_strncpy(pszExtra, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_PROD), pEnv->cchExtra);
        pszApp = FacStatus_ObjectName(pEnv, nFacCol);
        if (pszApp != NULL) {
            pufo_strcat(pszExtra, pszApp, pEnv->cchExtra);
        }
        return pszExtra;

    case '$':
    case ')':
    case '*':                                    /* 0x6A700, 0x6A998 and 0x6A9C8 all land on LABEL_60 */
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_BASE), pEnv->cchMsg);
        pszApp = FacStatus_BaseName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);
        }
        return pszMsg;

    case '%':
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_RESEARCH), pEnv->cchMsg);
        pszApp = FacStatus_TopicName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);       /* 0x6A77C */
        }
        if (nFacCol != PUFO_FAC_STATUS_RESEARCH_TAG) {
            return pszMsg;                                   /* 0x6A78C: CMP #0x3E8 / BNE default */
        }
        pufo_strncpy(pszExtra, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_RESEARCH), pEnv->cchExtra);
        /* 0x6A7B4 LDRB [R7,#0x22]: the byte one RECORD in -- aTopics[nBaseIdx + 1].abUnk0[0], the same
         * off-by-one-record pairing ResearchScreen_RebuildTopicList reads the topic ranges through. */
        if (pEnv->aTopics != NULL && nBaseIdx >= 0 && nBaseIdx + 1 < PUFO_RESEARCH_TOPIC_COUNT) {
            pszApp = FacStatus_TopicName(pEnv,
                                         nFacRow + pEnv->aTopics[nBaseIdx + 1].abUnk0[0]);
            if (pszApp != NULL) {
                pufo_strcat(pszExtra, pszApp, pEnv->cchExtra);
            }
        }
        return pszExtra;

    case '&':
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_BASE), pEnv->cchMsg);
        pszApp = FacStatus_BaseName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);
        }
        pufo_strncpy(pszExtra, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_MODULE), pEnv->cchExtra);
        nKind = -1;
        if (pEnv->abFacilityGrid != NULL
            && nBaseIdx >= 0 && nBaseIdx < PUFO_FAC_BASE_COUNT
            && nFacRow >= 0 && nFacRow < PUFO_FAC_GRID_FACILITY_ROWS
            && nFacCol >= 0 && nFacCol < PUFO_FAC_GRID_ROW_STRIDE) {
            /* 0x6A858 LDRB [R3,#0x1E0] over a1 = abFacilityGrid: 480 + 6 * (24 * base + row) + col.
             * The grid is eight 144-byte 24x6 blocks from abFacilityGrid + 372, so +480 is row 18 and
             * these are the grid's last six rows. */
            nKind = pEnv->abFacilityGrid[PUFO_FAC_GRID_FACILITY_BASE
                                         + PUFO_FAC_GRID_BASE_STRIDE * (size_t)nBaseIdx
                                         + PUFO_FAC_GRID_ROW_STRIDE * (size_t)nFacRow
                                         + (size_t)nFacCol];
        }
        if (pEnv->paFac != NULL && nKind >= 0 && nKind < PUFO_BASEFAC_COUNT) {
            pufo_strcat(pszExtra, pEnv->paFac[nKind].szName, pEnv->cchExtra); /* 0x6A86C + LABEL_64 */
        }
        return pszExtra;

    case '\'':
        pufo_strncpy(pszExtra, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_CRAFT), pEnv->cchExtra);
        /* The original's Ship_FormatDisplayName builds into szRadarMsg @ 0x186B10 and returns it, so
         * the craft name passes through pszMsg before it lands on pszExtra. */
        (void)Ship_FormatDisplayName(pEnv->apCraft, nFacRow, pEnv->pTextRecs, pszMsg, pEnv->cchMsg);
        pufo_strcat(pszExtra, pszMsg, pEnv->cchExtra);
        if (nFacCol == PUFO_FAC_STATUS_NO_OBJECT) {
            pEnv->pszScratch[0] = '\0';        /* 0x6A8D0: copies from 0x10DA38, all zero -- a clear */
        } else {
            pszApp = FacStatus_ObjectName(pEnv, nFacCol);
            pufo_strncpy(pEnv->pszScratch, pszApp != NULL ? pszApp : "", pEnv->cchScratch);
        }
        return pszExtra;

    case '(':
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_BASE), pEnv->cchMsg);
        pszApp = FacStatus_BaseName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);
        }
        pszApp = FacStatus_ObjectName(pEnv, nFacRow);   /* '(' indexes the object with nFacRow, not nFacCol */
        pufo_strncpy(pEnv->pszScratch, pszApp != NULL ? pszApp : "", pEnv->cchScratch);
        return pszMsg;

    case '+':
        pufo_strncpy(pszExtra, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_CRAFT), pEnv->cchExtra);
        (void)Ship_FormatDisplayName(pEnv->apCraft, nFacRow, pEnv->pTextRecs, pszMsg, pEnv->cchMsg);
        pufo_strcat(pszExtra, pszMsg, pEnv->cchExtra);
        pufo_strncpy(pszMsg, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_BASE), pEnv->cchMsg);
        pszApp = FacStatus_BaseName(pEnv, nBaseIdx);
        if (pszApp != NULL) {
            pufo_strcat(pszMsg, pszApp, pEnv->cchMsg);
        }
        return pszMsg;

    case ',':
        pufo_strncpy(pszExtra, FacStatus_Text(pEnv, PUFO_FAC_STATUS_TEXT_CRAFT), pEnv->cchExtra);
        pufo_strcat(pszExtra, pEnv->pszScratch, pEnv->cchExtra); /* 0x6AAA0: whatever a previous call left */
        return pszExtra;

    default:
        return NULL;
    }
}

/* ---- Ufopaedia ------------------------------------------------------------------ */

/* aUfopaediaCatTopics @ 0xAA830, read straight out of the image. Row 5 is the only row that
 * fills all 22 slots; every other row is terminated by a zero byte, which is what
 * Ufopaedia_NextEntry stops on at 0x9A7A8. IDA's own name, aFghijklmnop, comes from row 0's
 * bytes happening to be ASCII 'f'..'p'. */
const u8 g_aUfopaediaCatTopics[PUFO_UFOPAEDIA_CATEGORIES][PUFO_UFOPAEDIA_CAT_WIDTH] = {
    { 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  97,  98,  99, 100, 101,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {   1,   2,   3,   4,   5,   6,   7,   8,  14,  15,  16,  17,  18,  22,  23,  24,  25,   0,   0,   0,   0,   0 },
    {   9, 113,  10, 114,  11, 115,  12, 116,  13, 117,  21,  19,  20,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  48,  49,  50,  51,  52,  53,  54,   0,   0,   0,   0,   0 },
    {  55,  66,  56,  67,  57,  68,  58,  69,  59,  70,  60,  71,  61,  72,  62,  73,  63,  74,  64,  75,  65,  76 },
    {  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  77,  78,  79,  80,  81,  82,  83,  84,  85,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
    {  86,  87,  88,  89,  90,  91,  92,  93,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 }
};

/* anUfopaediaEntryNum @ 0xAA5F0: research topic number -> ufopaedia entry number. Five topics
 * have no entry (47, 94, 95, 96, 143) and the alien-race topics 118..142 all map onto the five
 * race entries 54..58, which is why the ufopaedia lists each race once per race topic. */
const i32 g_anUfopaediaEntryNum[PUFO_RESEARCH_TOPIC_COUNT] = {
        0, 1001, 1004, 1008, 1007, 1010, 1002, 1005, 1009, 1003, 1006, 1011,
     1013, 1012, 1020, 1021, 1024, 1022, 1014, 1105, 1023, 1018, 1017, 1015,
     1016, 1019, 2026, 2027, 2028, 2029, 2030, 2031, 2032, 2033, 2034, 2035,
     2036, 3000, 3001, 3002, 3003, 3004, 3005, 3013, 3006, 3007, 3014,    0,
     3015, 3016, 3012, 3010, 3008, 3009, 3011,   54,   55,   56,   57,   58,
       59,   60,   61,   62,   63,   64,   65,   66,   67,   68,   69,   70,
       71,   72,   73,   74,   75,   76,   77,   78,   79,   80,   81,   82,
       83,   84,   85,   86,   87,   88,   89,   90,   91,   92,    0,    0,
        0, 2097, 2098, 2099, 2100, 2101,   93,   94,   95,   96,   97,   98,
       99,  100,  101,  102,  103, 1037, 1039, 1049, 1051, 1050,   54,   58,
       57,   55,   56,   54,   58,   57,   55,   56,   54,   58,   57,   55,
       56,   54,   58,   57,   55,   56,   54,   58,   57,   55,   56,    0
};

static UiPanel *Ufopaedia_CatPanelAt(const UfopaediaEnv *pEnv, i32 nCategory)
{
    if (nCategory < 0 || nCategory >= PUFO_UFOPAEDIA_CATEGORIES) {
        return NULL;
    }
    return pEnv->apCatPanels[nCategory];
}

/* g_aUfopaediaCatTopics's cell, the static category order. Out of range reads as the row
 * terminator 0, which is what the table itself uses past the end of every short row. */
static i32 Ufopaedia_TableTopic(i32 nCategory, i32 nEntryIdx)
{
    if (nCategory < 0 || nCategory >= PUFO_UFOPAEDIA_CATEGORIES) {
        return 0;
    }
    if (nEntryIdx < 0 || nEntryIdx >= PUFO_UFOPAEDIA_CAT_WIDTH) {
        return 0;
    }
    return g_aUfopaediaCatTopics[nCategory][nEntryIdx];
}

/* panel->anLineAux[nEntryIdx]: the entry's topic number. The original reaches it as
 * `panel + 4 * (n + 1261)`, and 4 * 1261 = 5044 is exactly anLineAux's offset in the
 * 7092-byte UiPanel. -1 marks "no entry here" and is never a valid topic. */
static i32 Ufopaedia_CatTopic(const UiPanel *pPanel, i32 nEntryIdx)
{
    if (nEntryIdx < 0 || nEntryIdx >= PUFO_PANEL_LINES) {
        return -1;
    }
    return pPanel->anLineAux[nEntryIdx];
}

static i32 Ufopaedia_EntryNum(i32 nTopic)
{
    if (nTopic < 0 || nTopic >= PUFO_RESEARCH_TOPIC_COUNT) {
        return 0;
    }
    return g_anUfopaediaEntryNum[nTopic];
}

static const char *Ufopaedia_TextRec(const UfopaediaEnv *pEnv, i32 nRec)
{
    return (const char *)(pEnv->pTextRecs) + PUFO_TEXT_REC_STRIDE * (size_t)nRec;
}

/* The unlock gate both paging functions share, 0x9A6BC..0x9A6E8:
 *   MOV R0, #0x22 ; MUL R0, R11, R0 ; ADD R0, R0, R4 ; LDRB R0, [R0, #0xD]
 *     -> aResearchTopics[topic].bUnlocked
 *   MOV R0, #0x1C00 ; ORR R0, #0x24 ; ADD R1, R1, R4 ; LDRB R1, [R1, R0]
 *     -> byte at aResearchTopics + topic + 0x1C24, and 0x15EF08 + 0x1C24 = 0x160B2C.
 * Hex-Rays renders the second as aResearchTopics[211].abActivateTopics[topic + 1] because
 * 211 * 34 + 0x1E = 0x1C24 -- one byte late. The ARM is the authority: it is
 * abResearchZero[topic], the mirror ResearchScreen_RebuildTopicList sets at 0x74334 when a
 * topic's bUnlocked becomes 1. */
static bool Ufopaedia_TopicUnlocked(const UfopaediaEnv *pEnv, i32 nTopic)
{
    if (nTopic < 0 || nTopic >= PUFO_RESEARCH_TOPIC_COUNT) {
        return false;
    }
    return pEnv->aTopics[nTopic].bUnlocked != 0 || pEnv->abResearchZero[nTopic] != 0;
}

/* The accept test both paging loops share, 0x9A684..0x9A6B8 / 0x9A7E8..0x9A820. The three
 * comparisons are the original's own: count != 0, index != count, index <= count -- together
 * "index is inside the panel's entries", written out as the ARM writes them. Returns the topic
 * it landed on, or -1 to keep paging. */
static i32 Ufopaedia_TryEntry(UfopaediaEnv *pEnv, UiPanel *pCat, i32 nEntryIdx)
{
    i32 nTopic;
    if (pCat == NULL) {
        return -1;
    }
    if (pCat->nLineCount == 0) {            /* 0x9A684 CMP R1, #0 */
        return -1;
    }
    if (nEntryIdx == pCat->nLineCount) {    /* 0x9A68C CMP R2, R1 */
        return -1;
    }
    if (nEntryIdx > pCat->nLineCount) {     /* 0x9A694 BGT */
        return -1;
    }
    nTopic = Ufopaedia_CatTopic(pCat, nEntryIdx);
    if (!Ufopaedia_TopicUnlocked(pEnv, nTopic)) {
        return -1;
    }
    *pEnv->pnTopic = nTopic;                /* 0x9A6A8 STR R1, [R7] */
    *pEnv->pnEntryNum = Ufopaedia_EntryNum(nTopic); /* 0x9A6B4 LDR R0, [R5,R0,LSL#2] */
    return nTopic;
}

/* The tail both paging functions run before refreshing: the pad is dropped here and rebuilt by
 * Ufopaedia_RefreshEntry, which is why the two flags are set here rather than there. */
static void Ufopaedia_DropPad(UfopaediaEnv *pEnv)
{
    *pEnv->pbPadDirty = 1;      /* 0x9A6F4 / 0x9A85C */
    *pEnv->pbEntryDirty = 0;    /* 0x9A700 / 0x9A868 */
    CloseFreePad(pEnv->ppPad);  /* 0x9A718..0x9A720 / 0x9A880..0x9A888 */
}

void Ufopaedia_PrevEntry(UfopaediaEnv *pEnv)
{
    i32 nEntryIdx = *pEnv->pnEntryIdx;      /* 0x9A618 */
    i32 nCategory  = *pEnv->pnCategory;     /* 0x9A630 */

    for (;;) {
        /* 0x9A640. PrevEntry has no table test at all -- unlike NextEntry it walks the index
         * down to -1 and wraps the category there, starting again at the row's last slot. */
        if (--nEntryIdx == -1) {
            if (nCategory <= 0) {
                nCategory = PUFO_UFOPAEDIA_CATEGORIES - 1; /* 0x9A65C */
            } else {
                --nCategory;                               /* 0x9A658 */
            }
            nEntryIdx = PUFO_UFOPAEDIA_CAT_WIDTH - 1;      /* 0x9A660 */
            *pEnv->pnCategory = nCategory;                 /* 0x9A664 */
        }
        *pEnv->pnEntryIdx = nEntryIdx;                     /* 0x9A66C */
        if (Ufopaedia_TryEntry(pEnv, Ufopaedia_CatPanelAt(pEnv, nCategory), nEntryIdx) >= 0) {
            break;
        }
    }
    Ufopaedia_DropPad(pEnv);
    Ufopaedia_RefreshEntry(pEnv);                          /* 0x9A728 */
}

void Ufopaedia_NextEntry(UfopaediaEnv *pEnv)
{
    i32 nEntryIdx = *pEnv->pnEntryIdx;      /* 0x9A760 */
    i32 nCategory  = *pEnv->pnCategory;     /* 0x9A77C */

    for (;;) {
        /* 0x9A7A8: `++v0 == 22 || table[22 * category + v0] == 0`. The table read uses the OLD
         * category and is short-circuited when the index reaches 22, which only row 5 can do. */
        if (++nEntryIdx == PUFO_UFOPAEDIA_CAT_WIDTH ||
            Ufopaedia_TableTopic(nCategory, nEntryIdx) == 0) {
            if (nCategory >= PUFO_UFOPAEDIA_CATEGORIES - 1) {
                nCategory = 0;                             /* 0x9A7C4 */
            } else {
                ++nCategory;                               /* 0x9A7C0 */
            }
            nEntryIdx = 0;                                 /* 0x9A7C8 */
            *pEnv->pnCategory = nCategory;                 /* 0x9A7CC */
        }
        *pEnv->pnEntryIdx = nEntryIdx;                     /* 0x9A7D4 */
        if (Ufopaedia_TryEntry(pEnv, Ufopaedia_CatPanelAt(pEnv, nCategory), nEntryIdx) >= 0) {
            break;
        }
    }
    Ufopaedia_DropPad(pEnv);
    Ufopaedia_RefreshEntry(pEnv);                          /* 0x9A890 */
}

void Ufopaedia_RefreshEntry(UfopaediaEnv *pEnv)
{
    UiPanel *pText = pEnv->pTextPanel;
    char szDesc[800];                     /* v51 @ 0x9A014's frame, bp-0x320 */
    i32 nLineStart = 0;
    i32 nMaxW = PUFO_UFOPAEDIA_TEXT_MAXW;
    i32 nEntry, nTopic, i;

    if (pText == NULL) {
        return;
    }

    /* 0x9A054: neither the pointer (which stores the row into the panel's own nUnk3032) nor a
     * paging step has settled an entry, so read the category panel's selected row back out. */
    if (*pEnv->pbReselect == 0 && *pEnv->pbPadDirty == 0) {
        UiPanel *pCat = Ufopaedia_CatPanelAt(pEnv, *pEnv->pnCategory); /* 0x9A06C */
        if (pCat != NULL) {
            *pEnv->pnEntryIdx = pCat->nUnk3032;                        /* 0x9A078 */
            nTopic = Ufopaedia_CatTopic(pCat, *pEnv->pnEntryIdx);     /* 0x9A08C */
            *pEnv->pnEntryNum = Ufopaedia_EntryNum(nTopic);           /* 0x9A094 */
            *pEnv->pnTopic = nTopic;                                  /* 0x9A09C */
        }
    }

    /* The layout defaults, 0x9A0B8..0x9A0EC. */
    *pEnv->pbWidePanel = 0;
    pText->nStyle = 7;
    pText->nPosX = 10;
    pText->nPosY = 230;
    nMaxW = PUFO_UFOPAEDIA_TEXT_MAXW;
    nLineStart = 0;
    for (i = 0; i < PUFO_UFOPAEDIA_TEXT_LINES; ++i) {   /* 0x9A0F0: strcpy(line, "") from 0x10DA38 */
        if (pText->apLines[i] != NULL) {
            pText->apLines[i][0] = '\0';
        }
    }
    pText->nUnk3028 = 0;                                /* 0x9A138: the scroll cursor */

    /* 0x9A14C..0x9A1F0. The original is a cascade with a goto: the > 2999 arm first, then
     * `if (v7 <= 1000) goto LABEL_13`, then `v8 = v7 <= 2000`, then `if (v7 < 2000) { wide = 1;
     * maxW = 220; LABEL_13: v8 = v7 <= 2000; }`, then `if (!v8 && v7 < 3000)`. Every path lands
     * in exactly one of the four bands below, so the port writes the bands directly. The two
     * boundaries the cascade makes explicit are worth pinning: entry 1000 and entry 2000 both
     * keep the default layout, and entry 3000 is the alien band, not the tight one. */
    nEntry = *pEnv->pnEntryNum;
    if (nEntry >= PUFO_UFOPAEDIA_BAND_ALIEN_FIRST) {
        *pEnv->pbWidePanel = 1;                          /* 0x9A154 */
        nMaxW = PUFO_UFOPAEDIA_MAXW_ALIEN;               /* 0x9A160 */
        pText->nPosY = 110;                              /* 0x9A16C */
        pText->nStyle = 8;                               /* 0x9A178 */
    } else if (nEntry > PUFO_UFOPAEDIA_BAND_DEFAULT_LAST &&
               nEntry < PUFO_UFOPAEDIA_BAND_WIDE_LAST) {
        *pEnv->pbWidePanel = 1;                          /* 0x9A19C */
        nMaxW = PUFO_UFOPAEDIA_TEXT_MAXW;                /* 0x9A1A0 */
    } else if (nEntry > PUFO_UFOPAEDIA_BAND_WIDE_LAST &&
               nEntry < PUFO_UFOPAEDIA_BAND_TIGHT_LAST) {
        *pEnv->pbWidePanel = 1;                          /* 0x9A1C0 */
        nMaxW = PUFO_UFOPAEDIA_MAXW_TIGHT;               /* 0x9A1CC */
        pText->nPosY = 50;                               /* 0x9A1E0 */
        pText->nStyle = 13;                              /* 0x9A1EC */
    }

    /* Four more bands that only set the flag; 93..97 also indents the text by seven lines so the
     * panel's first rows stay clear of the entry's picture. */
    if (nEntry >= PUFO_UFOPAEDIA_INDENT_FIRST && nEntry <= PUFO_UFOPAEDIA_INDENT_LAST) {
        *pEnv->pbWidePanel = 1;                          /* 0x9A208 */
        nLineStart = PUFO_UFOPAEDIA_INDENT_LINES;        /* 0x9A20C */
    }
    if (nEntry >= PUFO_UFOPAEDIA_FLAG_BAND1_FIRST && nEntry <= PUFO_UFOPAEDIA_FLAG_BAND1_LAST) {
        *pEnv->pbWidePanel = 1;                          /* 0x9A21C: 53 < entry < 76 */
    }
    if (nEntry >= PUFO_UFOPAEDIA_FLAG_BAND2_FIRST && nEntry <= PUFO_UFOPAEDIA_FLAG_BAND2_LAST) {
        *pEnv->pbWidePanel = 1;                          /* 0x9A230: 75 < entry < 85 */
    }
    if (nEntry >= PUFO_UFOPAEDIA_FLAG_BAND3_FIRST && nEntry <= PUFO_UFOPAEDIA_FLAG_BAND3_LAST) {
        *pEnv->pbWidePanel = 1;                          /* 0x9A244: 84 < entry < 93 */
    }

    /* 0x9A24C..0x9A2CC: the description is TEXTS[4*topic + 534] then records 535, 536 and 537
     * appended. 109470 / 109675 / 109880 / 110085 are 205 * 534..537. The trailing space the
     * original appends at 0x9A2FC is the one UiPanel_WrapText appends to its own working copy,
     * so it is not added here. */
    szDesc[0] = '\0';
    nTopic = *pEnv->pnTopic;
    if (nTopic >= 0 && nTopic < PUFO_RESEARCH_TOPIC_COUNT) {
        for (i = 0; i < 4; ++i) {
            (void)pufo_strcat(szDesc,
                              Ufopaedia_TextRec(pEnv, PUFO_UFOPAEDIA_DESC_REC_FIRST + 4 * nTopic + i),
                              sizeof(szDesc));
        }
    }

    /* 0x9A308..0x9A4D0 is a byte-for-byte inline clone of UiPanel_WrapText @ 0x37144: same
     * cursors, same `psz[++v7 - v9] = 0` break, same `i = v7` resume, same trailing flush, same
     * `nLineCount = nLineStart + count` tail. The port calls the shared function. */
    (void)UiPanel_WrapText(pText, (u8)nMaxW, (u8)nLineStart, szDesc);

    /* The pad toggle, 0x9A4F8..0x9A5D8. Paging sets bEntryDirty to 0 (0x9A700 / 0x9A868), so the
     * call it makes lands in the else arm and builds the pad; a refresh that did not come from
     * paging finds the flag set and frees instead. */
    if (*pEnv->pbEntryDirty != 0) {
        *pEnv->pbEntryDirty = 0;                         /* 0x9A5D8 */
        CloseFreePad(pEnv->ppPad);                       /* 0x9A5E4..0x9A5F8 */
    } else {
        PadImage *pPad = (PadImage *)calloc(1, sizeof(PadImage)); /* operator new(0x14), 0x9A508 */
        char szPal[56];                                  /* pszName @ bp-0x458 */
        char szNum[56];                                  /* v49 @ bp-0x420 */
        *pEnv->pbEntryDirty = 1;                         /* 0x9A500 */
        if (pPad != NULL) {
            PadImage_ctor(pPad);                         /* 0x9A514: the ctor's result is stored */
        }
        *pEnv->ppPad = pPad;                             /* 0x9A530 */
        (void)snprintf(szPal, sizeof(szPal), "%d.pal", nEntry); /* 0x9A53C..0x9A564: itoa + ".pal" */
        if (nEntry < 1000) {                             /* 0x9A578: only the sub-1000 entries have art */
            Palette_Load(szPal, PUFO_UFOPAEDIA_PAL_SLOT);        /* 0x9A584 */
            (void)snprintf(szNum, sizeof(szNum), "%d", nEntry); /* 0x9A594: the bare number, no ".pal" */
            if (pPad != NULL) {
                (void)PadImage_Load(pPad, szNum, PUFO_UFOPAEDIA_PAD_MODE); /* 0x9A5A4 */
                pPad->nOriginX = 0;                      /* 0x9A5B0 */
                pPad->nOriginY = 0;                      /* 0x9A5B8 */
            }
        }
        *pEnv->pbPadDirty = 0;                           /* 0x9A5C4 */
    }
}

/* The five race topics, in the order the switch at 0x9AB50 lists them: '7'..';' are 55..59. */
static const i32 g_anRaceTopic[PUFO_UFOPAEDIA_RACE_COUNT] = { 55, 56, 57, 58, 59 };

/* The encounter topics that make a race appear, in the order the original's AND-chains list them,
 * with the fallback last. The 25 topics 118..142 form five families by residue mod 5, and two of
 * the chains are incomplete: Ethereal's never mentions 142 and Muton's never mentions 135 or 140. */
const i32 g_anRaceKnownTopics[PUFO_UFOPAEDIA_RACE_COUNT][6] = {
    { 118, 123, 128, 133,  55, 138 },   /* Sectoid  */
    { 121, 126, 131, 136,  56, 141 },   /* Snakeman */
    { 122, 127, 132, 137,  57,   -1 },  /* Ethereal: the chain stops at 137, the fallback is 57 */
    { 120, 125,  58, 130,   -1,  -1 },  /* Muton: the chain is 120, 125, 58, the fallback 130 */
    { 124, 129, 134, 139,  59, 119 }    /* Floater  */
};

const char *const g_apszUfopaediaRaceName[PUFO_UFOPAEDIA_RACE_COUNT] = {
    "Sectoid", "Snakeman", "Ethereal", "Muton", "Floater"
};  /* aSectoid @ 0xAC610, aSnakeman @ 0xAC618, aEthereal @ 0xAC624, aMuton @ 0xAC630, aFloater @ 0xAC638 */

static i32 Ufopaedia_RaceIndex(i32 nTopic)
{
    i32 r;
    for (r = 0; r < PUFO_UFOPAEDIA_RACE_COUNT; ++r) {
        if (g_anRaceTopic[r] == nTopic) {
            return r;
        }
    }
    return -1;   /* the switch's default arm, 0x9AB70 */
}

/* 0x9AB34..0x9AD68. Category 5 only, and only for the five race topics. Returns the topic id when
 * the race is known, else 0. A known race also marks abResearchZero[topic] = 1, so opening the
 * ufopaedia is what puts a met race into the research mirror -- which is why the paging gate at
 * 0x9A6D4 accepts on that byte. */
static i32 Ufopaedia_RaceOverride(UfopaediaEnv *pEnv, i32 nCategory, i32 nTopic)
{
    i32 r, k;

    if (nCategory != PUFO_UFOPAEDIA_RACE_CATEGORY) {   /* 0x9AB3C: `if (v4 != 5) goto LABEL_45` */
        return 0;
    }
    r = Ufopaedia_RaceIndex(nTopic);
    if (r < 0) {
        return 0;
    }
    for (k = 0; k < 6; ++k) {
        i32 nGate = g_anRaceKnownTopics[r][k];
        if (nGate < 0 || nGate >= PUFO_RESEARCH_TOPIC_COUNT) {
            continue;
        }
        if (pEnv->aTopics[nGate].bUnlocked == 1) {     /* the chain's `!= 1` tests, then the fallback */
            if (nTopic >= 0 && nTopic < PUFO_RESEARCH_TOPIC_COUNT) {
                pEnv->abResearchZero[nTopic] = 1;      /* 0x9AD68 STRB */
            }
            return nTopic;                             /* 0x9AD60: v6 = aUfopaediaCatTopics[5][n] */
        }
    }
    return 0;
}

/* 0x9ADAC: `(aResearchTopics[topic].bUnlocked != 0 && topic != 0) || override != 0`. */
static bool Ufopaedia_SlotShown(const UfopaediaEnv *pEnv, i32 nTopic, i32 nOverride)
{
    if (nOverride != 0) {
        return true;
    }
    if (nTopic == 0) {
        return false;
    }
    if (nTopic < 0 || nTopic >= PUFO_RESEARCH_TOPIC_COUNT) {
        return false;
    }
    return pEnv->aTopics[nTopic].bUnlocked != 0;
}

static UiPanel *Ufopaedia_NewPanel(i32 nLines)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel));   /* operator new(0x1BB4) */

    if (pPanel == NULL) {
        return NULL;
    }
    if (!UiPanel_ctor(pPanel, nLines)) {
        free(pPanel);
        return NULL;
    }
    return pPanel;
}

static void Ufopaedia_InitSelPanel(UiPanel *pPanel, void (*pfnClick)(void))
{
    pPanel->nStyle   = 15;                                  /* 0x9A928 */
    pPanel->nFont    = 24;                                  /* 0x9A93C */
    pPanel->pfnClick = pfnClick;                            /* 0x9A958: Ufopaedia_CacheSelRow */
    pPanel->nPosX    = 120;                                 /* 0x9A96C */
    pPanel->nPosY    = 50;                                  /* 0x9A980 */
    pPanel->nUnk7052 = 5;                                   /* 0x9A994 */
    pPanel->nUnk7056 = 50;                                  /* 0x9A9A8 */
    pPanel->nViewW   = 230;                                 /* 0x9A9B0 */
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);         /* 0x9A9BC */
    UiPanel_SetBytes6to11(pPanel, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14); /* 0x9A9E0 */
    pPanel->bFlag7073 = 1;                                  /* 0x9A9F0 */
    pPanel->bUnk7088  = 0x96;                               /* 0x9AA04: -106 as a signed byte */
    UiPanel_SetBytes3to5(pPanel, 0x32, 0x32, 0xFA);         /* 0x9AA18 */
    pPanel->nUnk7076  = 1;                                  /* 0x9AA28: centred rows */
}

static void Ufopaedia_InitTextPanel(UiPanel *pPanel)
{
    pPanel->nStyle   = 7;                                   /* 0x9AA6C */
    pPanel->nFont    = 12;                                  /* 0x9AA7C */
    pPanel->nPosX    = 5;                                   /* 0x9AA90 */
    pPanel->nPosY    = 230;                                 /* 0x9AA9C */
    pPanel->nUnk7052 = 5;                                   /* 0x9AAA4 */
    pPanel->nUnk7056 = 50;                                  /* 0x9AAB0 */
    pPanel->nViewW   = 230;                                 /* 0x9AAB8 */
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);         /* 0x9AAC4 */
    UiPanel_SetBytes6to11(pPanel, 0xBE, 0x78, 0xF0, 0x14, 0x14, 0x14); /* 0x9AAE4 */
    pPanel->bFlag7072 = 0;                                  /* 0x9AAF8: the only panel that clears it */
}

static void Ufopaedia_InitCatPanel(UiPanel *pPanel, i32 nShown, void (*pfnClick)(void))
{
    i32 nVisible = nShown;

    if (nVisible > PUFO_UFOPAEDIA_CAT_VISIBLE_MAX) {        /* 0x9AE44 */
        nVisible = PUFO_UFOPAEDIA_CAT_VISIBLE_MAX;
    }
    pPanel->nFont    = 16;                                  /* 0x9AE0C */
    pPanel->pfnClick = pfnClick;                            /* 0x9AE1C: Ufopaedia_RefreshEntry */
    pPanel->nPosX    = 120;                                 /* 0x9AE30 */
    pPanel->nStyle   = nVisible;                            /* 0x9AE54 */
    /* 0x9AE58..0x9AE6C: `v12 = 16 * n; if (n & 0x8000000) ++v12; posY = 160 - (v12 >> 1)`. The sign
     * correction is the ARM's arithmetic-shift rounding and never fires for a non-negative count. */
    pPanel->nPosY    = PUFO_UFOPAEDIA_CAT_CENTRE_Y - 8 * nVisible;
    pPanel->nUnk7052 = 5;                                   /* 0x9AE74 */
    pPanel->nUnk7056 = pPanel->nPosY;                       /* 0x9AE80 */
    pPanel->nViewW   = 230;                                 /* 0x9AE88 */
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);         /* 0x9AE98 */
    UiPanel_SetBytes6to11(pPanel, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14); /* 0x9AEB8 */
    pPanel->bFlag7073 = 1;                                  /* 0x9AECC */
    UiPanel_SetBytes3to5(pPanel, 0x32, 0x32, 0xFA);         /* 0x9AEE0 */
    pPanel->nUnk7076  = 1;                                  /* 0x9AEF4 */
    pPanel->bUnk7088  = 0x96;                               /* 0x9AF0C */
}

void Ufopaedia_Open(UfopaediaEnv *pEnv)
{
    i32 c, k;

    if (*pEnv->pbOpen == 0) {                               /* 0x9A8E0 */
        *pEnv->pbOpen = 1;                                  /* 0x9A8E8: left set, so the next call releases */

        pEnv->pSelPanel = Ufopaedia_NewPanel(PUFO_UFOPAEDIA_SEL_LINES);        /* 0x9A8F4 */
        if (pEnv->pSelPanel != NULL) {
            Ufopaedia_InitSelPanel(pEnv->pSelPanel, pEnv->pfnSelClick);
        }
        pEnv->pTextPanel = Ufopaedia_NewPanel(PUFO_UFOPAEDIA_TEXT_LINES_BUILT); /* 0x9AA34 */
        if (pEnv->pTextPanel != NULL) {
            Ufopaedia_InitTextPanel(pEnv->pTextPanel);
        }

        /* Pass 1, 0x9AB04..0x9AF14: count the shown entries, then build each panel with that many
         * line buffers. The panel keeps all of them; only nStyle is clamped to 13. */
        for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
            i32 nShown = 0;
            for (k = 0; k < PUFO_UFOPAEDIA_CAT_WIDTH; ++k) {
                i32 nTopic = g_aUfopaediaCatTopics[c][k];
                if (Ufopaedia_SlotShown(pEnv, nTopic, Ufopaedia_RaceOverride(pEnv, c, nTopic))) {
                    ++nShown;
                }
            }
            pEnv->apCatPanels[c] = Ufopaedia_NewPanel(nShown);   /* 0x9ADD8..0x9AE04 */
            if (pEnv->apCatPanels[c] != NULL) {
                Ufopaedia_InitCatPanel(pEnv->apCatPanels[c], nShown, pEnv->pfnCatClick);
            }
        }

        /* 0x9AF2C..0x9B0BC: the selector's nine rows. The nine sources at 0x105EE6 .. 0x10654E are
         * 205 bytes apart and all sit above 0xAC800, inside .data's zero-filled span, so each copy
         * writes a single NUL: this clears the rows, it does not label them. */
        if (pEnv->pSelPanel != NULL) {
            for (k = 0; k < PUFO_UFOPAEDIA_SEL_LINES; ++k) {
                if (pEnv->pSelPanel->apLines[k] != NULL) {
                    pEnv->pSelPanel->apLines[k][0] = '\0';
                }
            }
        }

        /* Pass 2, 0x9B0C4..0x9B4FC: the same test again, this time writing the names. The row index
         * counts only accepted slots, so the rows are packed. */
        for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {
            UiPanel *pPanel = pEnv->apCatPanels[c];
            i32 nRow = 0;
            if (pPanel == NULL) {
                continue;
            }
            for (k = 0; k < PUFO_UFOPAEDIA_CAT_WIDTH; ++k) {
                i32 nTopic = g_aUfopaediaCatTopics[c][k];
                i32 nOverride = Ufopaedia_RaceOverride(pEnv, c, nTopic);
                i32 nRace;
                if (!Ufopaedia_SlotShown(pEnv, nTopic, nOverride)) {
                    continue;
                }
                if (nRow >= PUFO_PANEL_LINES || pPanel->apLines[nRow] == NULL) {
                    break; /* deviation: the original writes past a panel it under-sized */
                }
                /* 0x9B38C..0x9B3B4: strcpy(row, TEXTS[205 * aResearchTopics[topic].wGate4]) */
                pufo_strncpy((char *)pPanel->apLines[nRow],
                             Ufopaedia_TextRec(pEnv, pEnv->aTopics[nTopic].wGate4),
                             PUFO_PANEL_LINE_BYTES);
                pPanel->anLineAux[nRow] = nTopic;           /* 0x9B3CC */
                nRace = Ufopaedia_RaceIndex(nOverride);     /* 0x9B3D8 */
                if (nRace >= 0) {
                    pufo_strncpy((char *)pPanel->apLines[nRow],
                                 g_apszUfopaediaRaceName[nRace], PUFO_PANEL_LINE_BYTES);
                }
                ++nRow;
            }
        }
        return;                                             /* 0x9B4FC */
    }

    /* The release half, 0x9B50C..0x9B5DC. */
    *pEnv->pbOpen = 0;                                      /* 0x9B50C */

    if (pEnv->pSelPanel != NULL) {
        UiPanel_FreeLines(pEnv->pSelPanel);                 /* 0x9B524 */
        free(pEnv->pSelPanel);                              /* 0x9B52C */
        pEnv->pSelPanel = NULL;
    }
    if (pEnv->pTextPanel != NULL) {
        /* 0x9B544 writes nLineCount = 50 BEFORE the null test at 0x9B550, so a null text panel
         * would fault here. The port moves the write inside the guard. */
        pEnv->pTextPanel->nLineCount = PUFO_UFOPAEDIA_TEXT_LINES_BUILT;
        UiPanel_FreeLines(pEnv->pTextPanel);                /* 0x9B558 */
        free(pEnv->pTextPanel);                             /* 0x9B560 */
        pEnv->pTextPanel = NULL;
    }
    if (pEnv->ppScreenPad != NULL && *pEnv->ppScreenPad != NULL) {
        PadImage_FreeData(*pEnv->ppScreenPad);              /* 0x9B578 */
        free(*pEnv->ppScreenPad);                           /* 0x9B580 */
        *pEnv->ppScreenPad = NULL;
    }
    for (c = 0; c < PUFO_UFOPAEDIA_CATEGORIES; ++c) {       /* 0x9B588..0x9B5AC, slot order 0..8 */
        if (pEnv->apCatPanels[c] != NULL) {
            UiPanel_FreeLines(pEnv->apCatPanels[c]);
            free(pEnv->apCatPanels[c]);
            pEnv->apCatPanels[c] = NULL;
        }
    }
    if (pEnv->apCtl != NULL) {
        /* 0x9B5B8..0x9B5DC: v70 starts at 0x1872E0 and reads slot +2 each step, so the six controls
         * released are 0x1872E8, 0x1872EC, 0x1872F0, 0x1872F4, 0x1872F8 and 0x1872FC. */
        for (k = 0; k < PUFO_UFOPAEDIA_CTL_COUNT; ++k) {
            if (pEnv->apCtl[k] != NULL) {
                UiControl_FreePads(pEnv->apCtl[k]);
                free(pEnv->apCtl[k]);
                pEnv->apCtl[k] = NULL;
            }
        }
    }
    /* ORIGINAL BUG (0x9B5B8..0x9B5DC), not reproduced. The loop above starts at 0x1872E0 and steps by
     * one pointer while reading slot +2, so it covers 0x1872E8..0x1872FC and never 0x1872E4 -- the
     * live entry's PadImage, which every Ufopaedia_RefreshEntry allocates. Closing the ufopaedia after
     * any entry has been shown leaks that pad. The port releases it. */
    CloseFreePad(pEnv->ppPad);
}

/* ------------------------------------------------- the psi screen (geoscape screen 17) */

/* stru_ABAC8 @ 0xABAC8: the two scroll buttons PsiScreen_Init hands to UiControl_Setup at 0x72C54. */
const UiControlDesc g_aPsiScrollDesc[PUFO_PSI_SCROLL_CTLS] = {
    { 0x000A, 0x000A, "up",   "sup",   "", 0 }, /* 0xABAC8 */
    { 0x000A, 0x0022, "down", "sdown", "", 0 }  /* 0xABAE8 */
};

static UiPanel *PsiNewPanel(UiPanel **ppSlot, i32 nLineCount)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel)); /* operator new(0x1BB4), 0x717D0 */

    if (pPanel == NULL) {
        return NULL; /* deviation: the original stores the null ctor result and writes through it */
    }
    if (!UiPanel_ctor(pPanel, nLineCount)) {
        free(pPanel);
        return NULL;
    }
    if (ppSlot != NULL) {
        *ppSlot = pPanel;
    }
    return pPanel;
}

static void PsiFreePanel(UiPanel **ppSlot)
{
    if (*ppSlot != NULL) {
        UiPanel_FreeLines(*ppSlot);
        free(*ppSlot);
        *ppSlot = NULL;
    }
}

/* The style every column gets: nStyle 14 and nFont 16 (0x7181C/0x71828, 0x71934/0x71940,
 * 0x71A08/0x71A14, 0x71AE0/0x71AEC), nPosY and nUnk7056 of 70, and the same two byte triples
 * (0x71884/0x718A8, 0x71988/0x719AC, 0x71A5C/0x71A80, 0x71B34/0x71B54). */
static void PsiInitPanelCommon(UiPanel *pPanel)
{
    pPanel->nStyle   = PUFO_PSI_PANEL_STYLE;
    pPanel->nFont    = PUFO_PSI_PANEL_FONT;
    pPanel->nPosY    = PUFO_PSI_ROW_Y;
    pPanel->nUnk7056 = PUFO_PSI_ROW_Y;
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);
    UiPanel_SetBytes6to11(pPanel, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
}

/* One row's colour across all four columns. bMid is 120 for an assigned row and 240 for a free one;
 * the original passes the third byte of the first triple as -16, which is 240 once stored. */
static void PsiSetRowMeta(const PsiScreenEnv *pEnv, i32 nBase, i32 nRow, u8 bMid)
{
    UiPanel *const apPanels[4] = {
        pEnv->apNamePanel[nBase], pEnv->apStatPanel[nBase],
        pEnv->apDeltaPanel[nBase], pEnv->apCheckPanel[nBase]
    };
    i32 i;

    for (i = 0; i < 4; ++i) {
        if (apPanels[i] != NULL) {
            UiPanel_SetLineMeta(apPanels[i], nRow, PUFO_PSI_ROW_EDGE, bMid, PUFO_PSI_ROW_EDGE,
                                PUFO_PSI_ROW_SECOND, PUFO_PSI_ROW_SECOND, PUFO_PSI_ROW_SECOND);
        }
    }
}

static Soldier *PsiSoldierAt(const PsiScreenEnv *pEnv, i32 nIdx)
{
    if (nIdx < 0 || nIdx >= pEnv->nSoldierCount) {
        return NULL; /* deviation: 0x722DC indexes apSoldiers with the aux value unguarded */
    }
    return pEnv->apSoldiers[nIdx];
}

/* PsiScreen_BuildPanels @ 0x716d4. Build when byte_15EEF0 is clear, release when it is set -- the
 * same one-flag shape as Ufopaedia_Open, and the build arm never clears the flag either. */
void PsiScreen_BuildPanels(PsiScreenEnv *pEnv)
{
    i32 nBaseCount = (i32)*pEnv->pnBaseCount; /* LDRSB [grid,#0x618], read SIGNED */
    i32 anRowCount[PUFO_FAC_BASE_COUNT];
    i32 b, s;

    if (*pEnv->pbBuilt != 0) {
        *pEnv->pnGeoscapeScreen = 0;              /* 0x7212C: the geoscape screen id goes back to 0 */
        *pEnv->pbBuilt = 0;                       /* 0x72134 */
        if (pEnv->pScreenPad != NULL) {
            PadImage_FreeData(pEnv->pScreenPad);  /* 0x72148 */
            free(pEnv->pScreenPad);               /* 0x72150 */
            pEnv->pScreenPad = NULL;
        }
        for (b = 0; b < nBaseCount; ++b) {        /* 0x7216C */
            PsiFreePanel(&pEnv->apNamePanel[b]);  /* 0x7218C */
            PsiFreePanel(&pEnv->apStatPanel[b]);  /* 0x721A8 */
            PsiFreePanel(&pEnv->apDeltaPanel[b]); /* 0x721BC */
            PsiFreePanel(&pEnv->apCheckPanel[b]); /* 0x721E0 */
        }
        for (b = 0; b < PUFO_PSI_SCROLL_CTLS; ++b) { /* 0x72214: v76 = &unk_15EE78, slot +1 twice */
            if (pEnv->apScrollCtl[b] != NULL) {
                UiControl_FreePads(pEnv->apScrollCtl[b]);
                free(pEnv->apScrollCtl[b]);
                pEnv->apScrollCtl[b] = NULL;
            }
        }
        return;
    }

    *pEnv->pbBuilt = 1;                           /* 0x716F0 */

    /* Pass 1, 0x71734..0x71788: how many soldiers each base lists. */
    for (b = 0; b < nBaseCount; ++b) {
        i32 nCount = 0;

        for (s = 0; s < pEnv->nSoldierCount; ++s) {
            const Soldier *pS = pEnv->apSoldiers[s];
            if (pS != NULL && (i32)pS->nKind == b) { /* 0x71768: LDRB +0xB6 against the base index */
                ++nCount;
            }
        }
        anRowCount[b] = nCount;
    }

    /* Pass 2, 0x7178C..0x71B90: four panels per base, each built with that base's own row count. */
    for (b = 0; b < nBaseCount; ++b) {
        UiPanel *pName  = PsiNewPanel(&pEnv->apNamePanel[b], anRowCount[b]);  /* 0x717D0 */
        UiPanel *pStat  = PsiNewPanel(&pEnv->apStatPanel[b], anRowCount[b]);  /* 0x718F8 */
        UiPanel *pDelta = PsiNewPanel(&pEnv->apDeltaPanel[b], anRowCount[b]); /* 0x719D0 */
        UiPanel *pCheck = PsiNewPanel(&pEnv->apCheckPanel[b], anRowCount[b]); /* 0x71AA4 */

        if (pName != NULL) {
            PsiInitPanelCommon(pName);
            pName->pfnClick  = pEnv->pfnSelClick;          /* 0x7183C */
            pName->nPosX     = PUFO_PSI_NAME_X;            /* 0x7184C */
            pName->nUnk7052  = 5;                          /* 0x71860 */
            pName->nViewW    = PUFO_PSI_NAME_VIEWW;        /* 0x71874 */
            pName->bFlag7073 = 1;                          /* 0x718C0: only the name column */
            UiPanel_SetBytes3to5(pName, 0x32, 0x32, 0xFA); /* 0x718D4 */
            pName->bUnk7088  = 0x96;                       /* 0x718F4: -106 as a signed byte */
        }
        if (pStat != NULL) {
            PsiInitPanelCommon(pStat);
            pStat->nPosX    = PUFO_PSI_STAT_X;             /* 0x71950 */
            pStat->nUnk7052 = 175;                         /* 0x71964 */
            pStat->nViewW   = PUFO_PSI_NARROW_VIEWW;       /* 0x71978 */
            pStat->nUnk7076 = 1;                           /* 0x719CC */
        }
        if (pDelta != NULL) {
            PsiInitPanelCommon(pDelta);
            pDelta->nPosX    = PUFO_PSI_DELTA_X;           /* 0x71A24 */
            pDelta->nUnk7052 = 209;                        /* 0x71A38 */
            pDelta->nViewW   = PUFO_PSI_NARROW_VIEWW;      /* 0x71A4C */
            pDelta->nUnk7076 = 1;                          /* 0x71AA0 */
        }
        if (pCheck != NULL) {
            PsiInitPanelCommon(pCheck);
            pCheck->nPosX    = PUFO_PSI_CHECK_X;           /* 0x71AFC */
            pCheck->nUnk7052 = 210;                        /* 0x71B10 */
            pCheck->nViewW   = PUFO_PSI_NARROW_VIEWW;      /* 0x71B24 */
            pCheck->nUnk7076 = 1;                          /* 0x71B68 */
        }
    }

    /* Pass 3, 0x71BA8..0x72118: fill the rows. The count array is rezeroed at 0x71BDC and reused as
     * the row cursor, so rows are packed from 0 in Map.apSoldiers order. */
    for (b = 0; b < nBaseCount; ++b) {
        UiPanel *pName  = pEnv->apNamePanel[b];
        UiPanel *pStat  = pEnv->apStatPanel[b];
        UiPanel *pDelta = pEnv->apDeltaPanel[b];
        UiPanel *pCheck = pEnv->apCheckPanel[b];
        char szText[104];                                  /* v103 in the original's frame */
        i32  nRow = 0;

        if (pName == NULL || pStat == NULL || pDelta == NULL || pCheck == NULL) {
            continue; /* deviation: the original writes through all four without testing them */
        }

        for (s = 0; s < pEnv->nSoldierCount; ++s) {
            Soldier *pS = pEnv->apSoldiers[s];

            if (pS == NULL || (i32)pS->nKind != b) {
                continue;                                  /* 0x71C1C */
            }
            if (nRow >= pName->nLineCount) {
                break; /* deviation: UiPanel_ctor clamps to 500 lines, the original does not */
            }

            (void)UiPanel_SetLineText(pName, nRow, pS->szName); /* 0x71C3C, Soldier +0xBA */
            pName->anLineAux[nRow] = s;                        /* 0x71C6C: the soldier INDEX */

            if (pS->bPsiAssigned != 0) {
                /* 0x71C8C: the label source unk_EEE8A sits above 0xAC800, inside .data's zero-filled
                 * span, so the box row is cleared rather than labelled; the state is in anLineAux. */
                (void)UiPanel_SetLineText(pCheck, nRow, "");
                pCheck->anLineAux[nRow] = 1;                   /* 0x71CEC */
                pS->bPsiActive = 1;                            /* 0x71D00: Soldier +0x1A6 */
                PsiSetRowMeta(pEnv, b, nRow, PUFO_PSI_ROW_ASSIGNED_1); /* 0x71D1C..0x71D9C */
            } else {
                pCheck->anLineAux[nRow] = 0;                   /* 0x71DC4 */
                (void)UiPanel_SetLineText(pCheck, nRow, "");   /* 0x71E14, unk_EEF57 is zero too */
            }

            if (pS->bPsiActive != 0) {
                snprintf(szText, sizeof szText, "%u%s%u",
                         (unsigned)pS->auStat[6], PUFO_PSI_STAT_SEP, (unsigned)pS->auStat[7]);
                (void)UiPanel_SetLineText(pStat, nRow, szText); /* 0x71EB0 */

                if (pS->auStat[6] >= pS->auStat[7]) {
                    (void)UiPanel_SetLineText(pDelta, nRow, PUFO_PSI_DELTA_FULL); /* 0x71FC0 */
                    pDelta->anLineAux[nRow] = 1;                                   /* 0x71FCC */
                } else {
                    i32 nDelta = PUFO_PSI_TRAIN_PCT * (i32)pS->auStat[7] / 100;
                    snprintf(szText, sizeof szText, "%s%d", PUFO_PSI_DELTA_PREFIX, nDelta);
                    (void)UiPanel_SetLineText(pDelta, nRow, szText); /* 0x71F4C */
                    pDelta->anLineAux[nRow] = nDelta;                /* 0x71F7C */
                }
            } else {
                (void)UiPanel_SetLineText(pStat, nRow, "");   /* 0x72000, the 0x10DA38 clear */
                (void)UiPanel_SetLineText(pDelta, nRow, "");  /* 0x7202C */
            }

            /* 0x72034..0x720B0: arm the training countdown. A soldier already at or above their psi
             * energy still gets one day, which is what makes auStat[6] keep climbing past auStat[7]. */
            if (pS->bPsiActive != 0 && pS->bPsiAssigned != 0) {
                if (pS->auStat[6] < pS->auStat[7]) {
                    pS->nPsiTrainDays = (u8)(PUFO_PSI_TRAIN_PCT * (i32)pS->auStat[7] / 100); /* 0x72090 */
                } else {
                    pS->nPsiTrainDays = 1;                                                   /* 0x7209C */
                }
            } else {
                pS->nPsiTrainDays = 0;                                                       /* 0x720A4 */
            }
            ++nRow;                                                        /* 0x720D0 */
        }
    }
}

/* PsiScreen_SelectListed @ 0x71298: the name column's click handler. It toggles the selected row's
 * psi-lab box, and assigning is gated on the base's free psi-lab capacity. */
bool PsiScreen_SelectListed(PsiScreenEnv *pEnv)
{
    i32 nBase = (i32)*pEnv->pnBaseIdx;                    /* byte_1870F4, read SIGNED */
    UiPanel *pName, *pCheck;
    i32 nRow, nAssigned = 0, j;

    if (nBase < 0 || nBase >= PUFO_FAC_BASE_COUNT) {
        return false;
    }
    pName  = pEnv->apNamePanel[nBase];
    pCheck = pEnv->apCheckPanel[nBase];
    if (pName == NULL || pCheck == NULL) {
        return false; /* deviation: 0x712C4 dereferences both without testing */
    }

    nRow = pName->nUnk3032;                               /* 0x712D0: the selected row */
    *pEnv->pnScratchRow = nRow;                           /* 0x712E0: byte_15EE84[68] */
    if (nRow < 0 || nRow >= pCheck->nLineCount) {
        return false; /* deviation: 0x712EC indexes anLineAux unguarded */
    }

    if (pCheck->anLineAux[nRow] != 0) {                   /* already assigned -> free it */
        PsiSetRowMeta(pEnv, nBase, nRow, PUFO_PSI_ROW_FREE_1);   /* 0x714D4..0x71554 */
        pCheck->anLineAux[nRow] = 0;                      /* 0x71578 */
        (void)UiPanel_SetLineText(pCheck, nRow, "");      /* 0x715D8, unk_EEF57 */
        return true;
    }

    /* 0x712F8..0x7131C: the count walks the NAME panel's line count but reads the CHECK panel's aux,
     * and compares against 1 exactly. */
    for (j = 0; j < pName->nLineCount; ++j) {
        if (pCheck->anLineAux[j] == 1) {
            ++nAssigned;
        }
    }

    if (Base_CalcPsiLabCap(pEnv->abFacilityGrid, nBase) < nAssigned + 1) { /* 0x71334 */
        *pEnv->pnActionFail = 1;                          /* 0x7133C */
        *pEnv->pnResidue = 0;                             /* 0x71348 */
        return false;
    }

    PsiSetRowMeta(pEnv, nBase, nRow, PUFO_PSI_ROW_ASSIGNED_1); /* 0x7137C..0x713FC */
    pCheck->anLineAux[nRow] = 1;                          /* 0x71420 */
    (void)UiPanel_SetLineText(pCheck, nRow, "");          /* 0x71480, unk_EEE8A */
    return true;
}

/* PsiScreen_ApplyAssignments @ 0x72250: turn the check column back into game state, refresh each
 * base's psi-lab needed flag, then rebuild. Tail call into BuildPanels at 0x72394. */
void PsiScreen_ApplyAssignments(PsiScreenEnv *pEnv)
{
    i32 nBaseCount = (i32)*pEnv->pnBaseCount;

    for (i32 b = 0; b < nBaseCount; ++b) {
        UiPanel *pName  = pEnv->apNamePanel[b];
        UiPanel *pCheck = pEnv->apCheckPanel[b];
        UiPanel *pDelta = pEnv->apDeltaPanel[b];
        i32 nRows = 0;

        if (pName != NULL && pCheck != NULL && pDelta != NULL) {
            i32 nLines = pName->nLineCount;               /* 0x722AC: BLE skips the body at <= 0 */

            if (nLines > 0) {
                nRows = nLines;
                for (i32 j = 0; j < nLines; ++j) {
                    Soldier *pS = PsiSoldierAt(pEnv, pName->anLineAux[j]);

                    if (pS == NULL) {
                        continue;
                    }
                    if (pCheck->anLineAux[j] == 1) {      /* 0x722E0 */
                        pS->bPsiAssigned = 1;             /* 0x722E8 */
                        pS->nPsiTrainDays = (u8)(i8)pDelta->anLineAux[j]; /* 0x7230C, LDRSB */
                    } else {
                        pS->nPsiTrainDays = 0;            /* 0x72330 */
                        pS->bPsiAssigned = 0;             /* 0x7234C */
                    }
                }
            }
        }

        /* 0x72370. The third argument is never loaded at this call site: the last write to R2 is the
         * inner loop's ADD at 0x7235C, so the budget is the loop's exit value -- the base's whole row
         * count, or 0 when the panel is empty. The other two call sites (0x8BA4C, 0x953CC) pass
         * Base_CountPsiSoldiers instead. Recorded in docs/parity/bugs.tsv. */
        (void)Base_UpdatePsiLabNeeded(pEnv->abFacilityGrid, b, nRows);
    }

    PsiScreen_BuildPanels(pEnv);                          /* 0x72394 */
}

/* PsiScreen_Init @ 0x72b40. The bring-up; the tail call at 0x72C78 is the build arm. */
void PsiScreen_Init(PsiScreenEnv *pEnv)
{
    PadImage *pPad = (PadImage *)calloc(1, sizeof(PadImage)); /* 0x72B4C, operator new(0x14) */
    i32 i;

    if (pPad != NULL) {
        PadImage_ctor(pPad);                              /* 0x72B5C */
    }
    pEnv->pScreenPad = pPad;                              /* 0x72B70: the ctor's result */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_PSI_PAD_NAME, PUFO_PSI_PAD_MODE); /* 0x72B74 */
        pPad->nOriginX = 0;                               /* 0x72B88 */
        pPad->nOriginY = 0;                               /* 0x72B90 */
    }

    if (pEnv->pCtlOk != NULL) {
        pEnv->pCtlOk->x = 1;                              /* 0x72B9C */
        pEnv->pCtlOk->y = 300;                            /* 0x72BAC: 0x12C */
        pEnv->pCtlOk->pfnClick = pEnv->pfnOkClick;       /* 0x72BB4 */
    }
    if (pEnv->pCtlApply != NULL) {
        pEnv->pCtlApply->x = 120;                         /* 0x72BC4: 0x78 */
        pEnv->pCtlApply->y = 300;                         /* 0x72BD0 */
        pEnv->pCtlApply->pfnClick = pEnv->pfnApplyClick; /* 0x72BDC */
    }

    for (i = 0; i < PUFO_PSI_SCROLL_CTLS; ++i) {         /* 0x72BE4..0x72C20, operator new(0x20) */
        UiControl *pCtl = (UiControl *)calloc(1, sizeof(UiControl));

        if (pCtl == NULL) {
            continue;
        }
        UiControl_ctor(pCtl);                             /* 0x72BF0 */
        pEnv->apScrollCtl[i] = pCtl;
    }
    if (pEnv->apScrollCtl[0] != NULL) {
        pEnv->apScrollCtl[0]->pfnClick = pEnv->pfnScrollPrev; /* 0x72C34, +0x0C */
    }
    if (pEnv->apScrollCtl[1] != NULL) {
        pEnv->apScrollCtl[1]->pfnClick = pEnv->pfnScrollNext; /* 0x72C40 */
    }
    for (i = 0; i < PUFO_PSI_SCROLL_CTLS; ++i) {         /* 0x72C44..0x72C64, descriptors 0 and 1 */
        UiControl_Setup(pEnv->apScrollCtl[i], 0, 0, &g_aPsiScrollDesc[i],
                        pEnv->pfnLoadPads, pEnv->pPadUser);
    }

    *pEnv->pbRowSel = 1;                                  /* 0x72C70: byte_15EE84[0] */
    PsiScreen_BuildPanels(pEnv);                          /* 0x72C78 */
}

/* ---- BaseInfoScreen_Init @ 0x57688 ---- */

static const UiControlDesc g_aBaseInfoCtlDesc[2] = {
    { 0x0001, 0x012C, "emptyb", "cemptyb", "", 0 }, /* 0xAB198: the OK control, at (1, 300) */
    { 0x0078, 0x012C, "emptyb", "cemptyb", "", 0 }  /* 0xAB1B8: the panel-mode toggle, at (120, 300) */
};

void BaseInfoScreen_Init(BaseInfoInitEnv *pEnv)
{
    PadImage *pPad;
    i32 i;

    if (pEnv == NULL || pEnv->ppPad == NULL || pEnv->papCtl == NULL) {
        return;
    }

    pPad = (PadImage *)calloc(1, sizeof(PadImage));   /* 0x57694, operator new(0x14) */
    if (pPad != NULL) {
        PadImage_ctor(pPad);                          /* 0x576A4 */
    }
    *pEnv->ppPad = pPad;                              /* 0x576B8: the ctor's result, not the raw new */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_BASEINFO_PAD_NAME, PUFO_BASEINFO_PAD_MODE); /* 0x576BC */
        pPad->nOriginX = 0;                           /* 0x576CC */
        pPad->nOriginY = 0;                           /* 0x576D4 */
    }

    for (i = 0; i < 2; ++i) {                         /* 0x576DC..0x57708, operator new(0x20) */
        UiControl *pCtl = (UiControl *)calloc(1, sizeof(UiControl));

        if (pCtl == NULL) {
            continue;
        }
        UiControl_ctor(pCtl);                         /* 0x576E8 */
        pEnv->papCtl[i] = pCtl;
    }
    if (pEnv->papCtl[0] != NULL) {
        pEnv->papCtl[0]->pfnClick = pEnv->pfnOkClick;     /* 0x5772C, +0x0C */
    }
    if (pEnv->papCtl[1] != NULL) {
        pEnv->papCtl[1]->pfnClick = pEnv->pfnToggleClick; /* 0x5771C */
    }
    if (pEnv->pCtlClose != NULL) {
        pEnv->pCtlClose->x = 224;                        /* 0x57738 */
        pEnv->pCtlClose->y = 1;                          /* 0x57744 */
        pEnv->pCtlClose->pfnClick = pEnv->pfnCloseClick; /* 0x57750 */
    }
    for (i = 0; i < 2; ++i) {                            /* 0x57754..0x57774 */
        UiControl_Setup(pEnv->papCtl[i], 0, 0, &g_aBaseInfoCtlDesc[i],
                        pEnv->pfnLoadPads, pEnv->pPadUser);
    }

    if (pEnv->pnWorkBaseIdx != NULL && pEnv->pnSelectedBaseIdx != NULL) {
        *pEnv->pnWorkBaseIdx = *pEnv->pnSelectedBaseIdx; /* 0x5778C */
    }
    if (pEnv->pbOverlayOpen != NULL) {
        *pEnv->pbOverlayOpen = 0;                        /* 0x57794 */
    }
    if (pEnv->pfnBuildPanels != NULL) {
        pEnv->pfnBuildPanels();                          /* 0x5779C, the tail call */
    }
}

/* ---- BaseInfoScreen_BuildPanels @ 0x557f4 ---- */

/* The money literals at 0xAB1D8..0xAB20B. The craft labels are indexed by the name column's aux,
 * which is Ship.bCraftType; aux 0 is handled too (0x56810) and costs the same 500000 as aux 1.
 * The costs are the table Base_CalcMonthlyUpkeep also uses (0x6A294..0x6A2D4). */
static const char *const g_apzBaseInfoCraftLabel[5] = {
    "500000$", "500000$", "700000$", "600000$", "400000$"
};
static const i32 g_anBaseInfoCraftCost[5] = {
    PUFO_BASEINFO_CRAFT_COST_0, PUFO_BASEINFO_CRAFT_COST_0, PUFO_BASEINFO_CRAFT_COST_2,
    PUFO_BASEINFO_CRAFT_COST_3, PUFO_BASEINFO_CRAFT_COST_4
};

static UiPanel *BaseInfoNewPanel(void)
{
    UiPanel *pPanel = (UiPanel *)calloc(1, sizeof(UiPanel)); /* 0x55828, operator new(0x1BB4) */

    if (pPanel == NULL) {
        return NULL;
    }
    if (!UiPanel_ctor(pPanel, PUFO_BASEINFO_PANEL_LINES)) {  /* 0x55844 */
        free(pPanel);
        return NULL;
    }
    return pPanel;
}

/* The three panels share everything but their x, their click-rect left and their view width
 * (0x558D8..0x559B0 against 0x56188..0x56278 and 0x56288..0x56360). The name column never touches
 * nUnk7076; only the two narrow ones set it, so it stays out of this helper. */
static void BaseInfoPanelCommon(UiPanel *pPanel, i32 nPosX, i32 nClickLeft, i32 nViewW)
{
    pPanel->nStyle = PUFO_BASEINFO_PANEL_STYLE;
    pPanel->nFont = PUFO_BASEINFO_PANEL_FONT;
    pPanel->pfnClick = NULL;
    pPanel->nPosX = nPosX;
    pPanel->nPosY = PUFO_BASEINFO_PANEL_Y;
    pPanel->nUnk7052 = nClickLeft;
    pPanel->nUnk7056 = PUFO_BASEINFO_CLICK_TOP;
    pPanel->nViewW = nViewW;
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);
    UiPanel_SetBytes6to11(pPanel, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
    pPanel->bFlag7073 = 0;
    pPanel->bFlag7072 = 0;
}

static void BaseInfoRowMeta(UiPanel *pPanel, i32 nRow)
{
    UiPanel_SetLineMeta(pPanel, nRow, PUFO_BASEINFO_META_A, PUFO_BASEINFO_META_B,
                        PUFO_BASEINFO_META_ROW, PUFO_BASEINFO_META_C,
                        PUFO_BASEINFO_META_D, PUFO_BASEINFO_META_D);
}

/* Every label on this screen copies from a zero-filled scratch address, so each one is a clear. */
static void BaseInfoClearRow(UiPanel *pPanel, i32 nRow)
{
    (void)UiPanel_SetLineText(pPanel, nRow, "");
}

static void BaseInfoSetNumber(UiPanel *pPanel, i32 nRow, i32 nValue, const char *pszSuffix)
{
    char szLine[PUFO_PANEL_LINE_BYTES];

    (void)snprintf(szLine, sizeof(szLine), "%d%s", nValue, pszSuffix);
    (void)UiPanel_SetLineText(pPanel, nRow, szLine);
}

static i32 BaseInfoPanelAux(UiPanel *pPanel, i32 nRow)
{
    if (pPanel == NULL || nRow < 0 || nRow >= pPanel->nLineCount) {
        return 0;
    }
    return pPanel->anLineAux[nRow];
}

static void BaseInfoReleasePanels(BaseInfoBuildEnv *pEnv)
{
    i32 i;

    *pEnv->pbBuilt = 0;                                  /* 0x56F0C */
    for (i = 0; i < 3; ++i) {                            /* 0x56F18..0x56F6C: 0x158824, 0x158820, 0x158838 */
        UiPanel *pPanel = pEnv->pppPanel[i];

        if (pPanel != NULL) {
            UiPanel_FreeLines(pPanel);                   /* 0x56F24 */
            free(pPanel);                                /* 0x56F2C */
        }
        pEnv->pppPanel[i] = NULL;
    }
    for (i = 0; i < 2; ++i) {                            /* 0x56F74..0x56F90: 0x15882C then 0x158830 */
        UiControl *pCtl = pEnv->papCtl[i];

        if (pCtl != NULL) {
            UiControl_FreePads(pCtl);                    /* 0x56F88 */
            free(pCtl);                                  /* 0x56F90 */
        }
        pEnv->papCtl[i] = NULL;
    }
}

void BaseInfoScreen_BuildPanels(BaseInfoBuildEnv *pEnv)
{
    UiPanel *pNames, *pFree, *pTotals;
    const i32 nBase = (pEnv != NULL && pEnv->pnWorkBaseIdx != NULL) ? (i32)*pEnv->pnWorkBaseIdx : 0;
    const ObjectRecFull *pObjEngineer =                                   /* byte_113C80, slot 112 */
        (pEnv != NULL && pEnv->apObjects != NULL) ? pEnv->apObjects[PUFO_ROW_ENGINEER] : NULL;
    const ObjectRecFull *pObjScientist =                                  /* byte_113C84, slot 113 */
        (pEnv != NULL && pEnv->apObjects != NULL) ? pEnv->apObjects[PUFO_ROW_SCIENTIST] : NULL;
    /* Map_nCraftCount @ 0x114B4C, read as an unsigned byte here (ANDS R3, R0, #0xFF then CMP #0). */
    const i32 nCraftCount = (pEnv != NULL && pEnv->pnCraftCount != NULL)
                                ? (i32)(*pEnv->pnCraftCount & 0xFF) : 0;
    char szName[PUFO_PANEL_LINE_BYTES];
    i32 nShips = 0;
    i32 i;

    if (pEnv == NULL || pEnv->pbBuilt == NULL || pEnv->pppPanel == NULL) {
        return;
    }
    if (*pEnv->pbBuilt != 0) {
        BaseInfoReleasePanels(pEnv);
        return;
    }

    *pEnv->pbBuilt = 1;                                  /* 0x55810 */
    if (pEnv->pbModeFlag != NULL) {
        *pEnv->pbModeFlag = 1;                           /* 0x5581C: byte_AB190, the toggle's flag */
    }

    pEnv->pppPanel[0] = BaseInfoNewPanel();              /* 0x55828: 0x158824, the name column */
    pEnv->pppPanel[1] = BaseInfoNewPanel();              /* 0x5586C: 0x158820, the free column */
    pEnv->pppPanel[2] = BaseInfoNewPanel();              /* 0x558A0: 0x158838, the total column */
    pNames = pEnv->pppPanel[0];
    pFree = pEnv->pppPanel[1];
    pTotals = pEnv->pppPanel[2];
    if (pNames == NULL || pFree == NULL || pTotals == NULL) {
        return; /* deviation: the original dereferences all three without testing them */
    }

    BaseInfoPanelCommon(pNames, PUFO_BASEINFO_NAME_X, PUFO_BASEINFO_NAME_CLICKL,
                        PUFO_BASEINFO_NAME_VIEWW);
    for (i = 0; i < PUFO_BASEINFO_PANEL_LINES; ++i) {    /* 0x559B4..0x55A34: all three panels cleared */
        BaseInfoClearRow(pNames, i);
        BaseInfoClearRow(pFree, i);
        BaseInfoClearRow(pTotals, i);
    }
    for (i = 0; i < 4; ++i) {                            /* 0x55A6C..0x55AD8: rows 0, 5, 12 and 16 */
        static const i32 anMetaRow[4] = { 0, 5, 12, 16 };
        BaseInfoRowMeta(pNames, anMetaRow[i]);
    }

    /* 0x55E28..0x55F20: the base's craft, in apCraft order. Map_nCraftCount is read unsigned here
     * (ANDS R3, R0, #0xFF then CMP #0 / BLS), byte_1870F4 signed (LDRSB at 0x55E6C) and Ship.bHomeBase
     * unsigned (LDRB at 0x55E70). */
    if (pEnv->apCraft != NULL && nCraftCount != 0) {
        for (i = 0; i < nCraftCount; ++i) {
            const Ship *pShip = pEnv->apCraft[i];

            if (pShip == NULL || (i32)(u8)pShip->bHomeBase != nBase) {
                continue;                                /* 0x55E78 */
            }
            {
                const i32 nCraftIdx = Ship_FindIndexById(pEnv->apCraft, nCraftCount,
                                                         (i32)(u8)pShip->bId); /* 0x55E84 */
                const char *pszShip = Ship_FormatDisplayName(pEnv->apCraft, nCraftIdx,
                                                             pEnv->pTextRecs, szName, sizeof(szName));

                (void)UiPanel_SetLineText(pNames, PUFO_BASEINFO_SHIP_ROW + nShips,
                                          (pszShip != NULL) ? pszShip : ""); /* 0x55E94..0x55EB8 */
            }
            UiPanel_SetLineAux(pNames, PUFO_BASEINFO_SHIP_ROW + nShips, (i32)(u8)pShip->bCraftType);
            UiPanel_SetLineAux(pFree, PUFO_BASEINFO_SHIP_ROW + nShips, (i32)(u8)pShip->bId);
            UiPanel_SetLineAux(pTotals, PUFO_BASEINFO_SHIP_ROW + nShips, 1); /* 0x55EE8: the run marker */
            ++nShips;
        }
    }

    BaseInfoClearRow(pNames, nShips + 17);                                 /* 0x55F30 */
    for (i = 0; i < 3; ++i) {                            /* 0x55F44..0x55FB0: rows nShips+18, +23, +25 */
        static const i32 anTailMeta[3] = { 18, 23, 25 };
        BaseInfoRowMeta(pNames, nShips + anTailMeta[i]);
    }
    for (i = 18; i <= 25; ++i) {                        /* 0x55FE4..0x56178: eight more cleared rows */
        BaseInfoClearRow(pNames, nShips + i);
    }

    BaseInfoPanelCommon(pFree, PUFO_BASEINFO_FREE_X, PUFO_BASEINFO_NARROW_CLICKL,
                        PUFO_BASEINFO_NARROW_VIEWW);                       /* 0x56188..0x56278 */
    pFree->nUnk7076 = 1;                                                   /* 0x56270 */
    BaseInfoPanelCommon(pTotals, PUFO_BASEINFO_TOTAL_X, PUFO_BASEINFO_NARROW_CLICKL,
                        PUFO_BASEINFO_NARROW_VIEWW);                       /* 0x56288..0x56360 */
    pTotals->nUnk7076 = 1;                                                 /* 0x56358 */

    /* 0x56364..0x563A8: the soldiers aboard the listed craft. The loop runs while the TOTAL column's
     * aux is 1, and passes the FREE column's aux -- Ship.bId, not the roster index every other
     * Craft_CountSoldiersAboard call site uses. */
    {
        i32 nAboard = 0;

        if (BaseInfoPanelAux(pTotals, PUFO_BASEINFO_SHIP_ROW) == 1) {
            i32 n = 0;

            do {
                nAboard += Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount,
                                                     BaseInfoPanelAux(pFree, PUFO_BASEINFO_SHIP_ROW + n));
                ++n;
            } while (BaseInfoPanelAux(pTotals, PUFO_BASEINFO_SHIP_ROW + n) == 1);
        }
        BaseInfoSetNumber(pFree, 1, nAboard, "");                            /* 0x563B8 */
    }

    /* 0x563FC..0x56670: the five capacity deltas and the two crew ones. */
    {
        const i32 nEntries = (pEnv->pbEntryCount != NULL) ? (i32)*pEnv->pbEntryCount : 0;
        const u16 *pManuf = (pEnv->apManufCol != NULL)
                                ? pEnv->apManufCol + PUFO_PRODUCT_ENTRIES_PER_BASE * nBase : NULL;
        const u16 *pResearch = (pEnv->apResearchCol != NULL)
                                   ? pEnv->apResearchCol + PUFO_RESEARCH_TOPIC_COUNT * nBase : NULL;

        BaseInfoSetNumber(pFree, 2,
                          Base_GetItemQty(pObjEngineer, nBase)
                              - Base_GetEngineerCount(pObjEngineer, pManuf, nBase, nEntries), "");
        BaseInfoSetNumber(pFree, 3,
                          Base_GetItemQty(pObjScientist, nBase)
                              - Base_GetScientistCount(pObjScientist, pResearch, nBase, nEntries), "");
        BaseInfoSetNumber(pFree, 6,
                          Base_CalcLivingQuartersCap(pEnv->abFacilityGrid, nBase)
                              - Base_CalcOccupiedSpace(pEnv->apSoldiers, pEnv->nSoldierCount,
                                                       pEnv->apObjects, pEnv->nObjectCount, nBase), "");
        BaseInfoSetNumber(pFree, 7,
                          Base_CalcStoresCapacity(pEnv->abFacilityGrid, nBase)
                              - Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nBase), "");
        BaseInfoSetNumber(pFree, 8,
                          Base_CalcLabCap(pEnv->abFacilityGrid, nBase)
                              - ((pResearch != NULL) ? Base_CountActiveResearch(pResearch, nEntries) : 0),
                          "");
        BaseInfoSetNumber(pFree, 9,
                          Base_CalcWorkshopCap(pEnv->abFacilityGrid, nBase)
                              - ((pManuf != NULL) ? Base_CountActiveManuf(pManuf, nEntries) : 0), "");
        BaseInfoSetNumber(pFree, 10,
                          Base_CalcHangarCap(pEnv->abFacilityGrid, nBase)
                              - Base_CountInboundTransfers(pEnv->aSites, pEnv->nSiteCount, -1,
                                                           pEnv->apBases, nBase, true)
                              - Base_CountShipsAtBase(pEnv->apCraft, nCraftCount,
                                                      pEnv->apBases, pEnv->nBaseCount,
                                                      pEnv->abGridBytes, nBase), "");
    }

    /* 0x566A4..0x5685C: the craft cost rows. A zero aux is handled here too (0x56810) and the run
     * continues; only the total column's marker ending stops it. */
    {
        i32 nShipTotal = 0;
        i32 n = 0;

        if (BaseInfoPanelAux(pTotals, PUFO_BASEINFO_SHIP_ROW) == 1) {
            for (;;) {
                const i32 nAux = BaseInfoPanelAux(pNames, PUFO_BASEINFO_SHIP_ROW + n);

                if (nAux >= 0 && nAux <= 4) {
                    (void)UiPanel_SetLineText(pFree, PUFO_BASEINFO_SHIP_ROW + n,
                                              g_apzBaseInfoCraftLabel[nAux]);
                    nShipTotal += g_anBaseInfoCraftCost[nAux];
                }
                ++n;
                if (BaseInfoPanelAux(pTotals, PUFO_BASEINFO_SHIP_ROW + n) != 1) {
                    break;
                }
            }
        }

        /* 0x56860..0x568FC: the free column's three unit-price rows. These are fixed literals, not
         * computed -- the free column says what one of each costs, the total column what this base
         * actually holds costs. */
        (void)UiPanel_SetLineText(pFree, n + 19, "20000$");                 /* a20000 @ 0xAB1DC */
        (void)UiPanel_SetLineText(pFree, n + 20, "20000$");                 /* the same literal twice */
        (void)UiPanel_SetLineText(pFree, n + 21, "30000$");                 /* a30000 @ 0xAB1E4 */

        {
            const i32 nSoldiers = Base_CountSoldiers(pEnv->apSoldiers, pEnv->nSoldierCount, nBase);
            const i32 nEngineers = Base_GetItemQty(pObjEngineer, nBase);
            const i32 nScientists = Base_GetItemQty(pObjScientist, nBase);
            i32 nFacTotal = 0;
            i32 nGrand = PUFO_BASEINFO_PRICE_SOLDIER * (nSoldiers + nEngineers)
                       + PUFO_BASEINFO_PRICE_SCIENTIST * nScientists + nShipTotal;

            /* 0x56904..0x569B8: the total column's money breakdown, each row itoa'd and then
             * strcat with the single '$' at asc_AB1D8. */
            BaseInfoSetNumber(pTotals, n + 19, PUFO_BASEINFO_PRICE_SOLDIER * nSoldiers, "$");
            BaseInfoSetNumber(pTotals, n + 20, PUFO_BASEINFO_PRICE_SOLDIER * nEngineers, "$");
            BaseInfoSetNumber(pTotals, n + 21, PUFO_BASEINFO_PRICE_SCIENTIST * nScientists, "$");

            if (pEnv->abFacilityGrid != NULL) {
                const u8 *pType = pEnv->abFacilityGrid + 480 + 144 * nBase; /* 0x56AA4 + 0x174 */
                i32 nCol, nRow;

                for (nCol = 0; nCol < 6; ++nCol) {
                    for (nRow = 0; nRow < 6; ++nRow) {
                        if (pType[nCol + 6 * nRow] != PUFO_BASEINFO_FAC_EMPTY) {
                            nFacTotal += PUFO_BASEINFO_PRICE_CELL;           /* 0x56ABC */
                        }
                    }
                }
            }

            BaseInfoSetNumber(pFree, n + 23, nFacTotal, "$");                /* 0x56AE0 */
            BaseInfoSetNumber(pFree, n + 25, nGrand + nFacTotal, "$");       /* 0x56B28 */

            BaseInfoSetNumber(pTotals, 1, nSoldiers, "");                    /* 0x56B78 */
            BaseInfoSetNumber(pTotals, 2, nEngineers, "");                   /* 0x56BBC */
            BaseInfoSetNumber(pTotals, 3, nScientists, "");                  /* 0x56C00 */
            BaseInfoSetNumber(pTotals, 6, Base_CalcLivingQuartersCap(pEnv->abFacilityGrid, nBase), "");
            BaseInfoSetNumber(pTotals, 7, Base_CalcStoresCapacity(pEnv->abFacilityGrid, nBase), "");
            BaseInfoSetNumber(pTotals, 8, Base_CalcLabCap(pEnv->abFacilityGrid, nBase), "");
            BaseInfoSetNumber(pTotals, 9, Base_CalcWorkshopCap(pEnv->abFacilityGrid, nBase), "");
            BaseInfoSetNumber(pTotals, 10, Base_CalcHangarCap(pEnv->abFacilityGrid, nBase), "");
            BaseInfoSetNumber(pTotals, 13,
                              Base_CountBuiltFacilities(pEnv->abFacilityGrid, nBase), ""); /* 0x56D88 */

            /* 0x56DD4..0x56ED4: rows 14 and 15 are cleared, then cleared again for every cell of this
             * base that is facility kind 4 or kind 5. Both sources are zero-filled scratch, so the
             * scan has no visible effect -- reproduced because it is what the original does. */
            BaseInfoClearRow(pTotals, 14);
            BaseInfoClearRow(pTotals, 15);
            if (pEnv->abFacilityGrid != NULL) {
                const u8 *pType = pEnv->abFacilityGrid + 480 + 144 * nBase;
                i32 nCol, nRow;

                for (nRow = 0; nRow < 6; ++nRow) {
                    for (nCol = 0; nCol < 6; ++nCol) {
                        if (pType[6 * nRow + nCol] == PUFO_BASEINFO_FAC_LIVING) {
                            BaseInfoClearRow(pTotals, 14);
                        }
                        if (pType[6 * nRow + nCol] == PUFO_BASEINFO_FAC_WORKSHOP) {
                            BaseInfoClearRow(pTotals, 15);
                        }
                    }
                }
            }
        }
    }
}

static const char *PsiUi_Text(const PsiScreenDrawEnv *pEnv, i32 nRec)
{
    if (pEnv->pTextRecs == NULL) {
        return "";
    }
    return pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec;
}

static const char *PsiUi_BaseName(const PsiScreenDrawEnv *pEnv, i32 nBaseIdx)
{
    if (pEnv->apSiteRecs == NULL || nBaseIdx < 0 || nBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return ""; /* deviation: 0x725AC and 0x728D8 index the eight records blindly */
    }
    return pEnv->apSiteRecs[nBaseIdx].abName; /* 0x186C24 + 64*i + 54 == 0x186C40 + 64*i + 26 */
}

static void PsiUi_DrawCentered(i32 nCenterX, i32 y, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz);
    Text_DrawString(nCenterX - nWidth / 2, y, psz);
}

/* PsiScreen_Draw @ 0x72398. See the header for the frame, the four captions and the tail. */
void PsiScreen_Draw(const PsiScreenDrawEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pbPanelOpen == NULL || *pEnv->pbPanelOpen == 0) {
        return;                                         /* 0x723B8: nothing draws while the flag is clear */
    }

    if (pEnv->pScreenPad != NULL) {
        PadImage_Blit(pEnv->pScreenPad);                /* 0x723C0 */
    }
    BaseUi_DrawFrameRails();                            /* 0x723C8..0x72558, the shared rails */

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x7258C */
    PsiUi_DrawCentered(120, 13, PsiUi_BaseName(pEnv, (i32)pEnv->nWorkBaseIdx)); /* 0x725F0 */

    for (i32 i = 0; i < 4; ++i) {                       /* 0x72600..0x72624 */
        if (pEnv->apPanels[i] != NULL) {
            UiPanel_DrawText(pEnv->apPanels[i]);
        }
    }
    (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser); /* 0x72630 */
    (void)UiControl_BlitActivePad(pEnv->pCtlApply, pEnv->pfnCtlBlit,
                                  pEnv->pfnCtlBlitEx, pEnv->pCtlUser);  /* 0x7263C */

    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);   /* 0x7265C */
    Text_DrawString(7, 54, PsiUi_Text(pEnv, PUFO_PSI_TEXT_COLUMNS));    /* 0x726A0, TEXTS[1188] */
    PsiUi_DrawCentered(60, 300, PsiUi_Text(pEnv, PUFO_PSI_TEXT_SEL_BASE)); /* 0x726F8, TEXTS[1189] */
    PsiUi_DrawCentered(180, 300, PsiUi_Text(pEnv, PUFO_PSI_TEXT_OK));      /* 0x7274C, TEXTS[1127] */

    const bool bOverlay = (pEnv->pbOverlayOpen != NULL) && *pEnv->pbOverlayOpen != 0;

    if (bOverlay) {
        SoftSurf_BlitSrcRect(0, 0, 240, 320, 0, 50, 119, 200);          /* 0x72790 */
        const i32 nBaseCount = (pEnv->pnBaseCount != NULL) ? (i32)*pEnv->pnBaseCount : 0;
        SoftSurf_BlitRectProxy(0, 208, 0, 32,
                               PUFO_PSI_BASE_ROW_PITCH * nBaseCount + 4, 0, 0, 0); /* 0x727CC */
        if (pEnv->pfnDrawIconStrip != NULL) {
            pEnv->pfnDrawIconStrip(-1, pEnv->pStripUser);              /* 0x727D8: -1 draws no chevron */
        }
        SoftSurf_BlitRectProxy(0, 0, 0, 210,
                               PUFO_PSI_BASE_ROW_PITCH * nBaseCount + 4, 0, 0, 0); /* 0x72810 */

        for (i32 nBase = 0; nBase < nBaseCount; ++nBase) {             /* 0x72828..0x729EC */
            const i32 nY = PUFO_PSI_BASE_ROW_PITCH * nBase;
            SoftSurf_BlitRectProxy(0, 3, nY + 3, 205, 26, 0x46, 0xA0, 0xB3); /* 0x72870 */
            SoftSurf_BlitRectProxy(0, 3, nY + 4, 204, 24, 0x00, 0x6E, 0x77); /* 0x728A4 */
            SoftSurf_BlitRectProxy(0, 5, nY + 5, 201, 22, 0x04, 0x46, 0x4B); /* 0x728D0 */
            PsiUi_DrawCentered(110, nY + 7, PsiUi_BaseName(pEnv, nBase));    /* 0x7291C */

            /* 0x72930: a base with no psi lab gets the white bracket, the same 23-step shape
             * Base_DrawFacilityIconStrip paints for a marked base -- here marking the absence. */
            if (Base_CalcPsiLabCap(pEnv->abFacilityGrid, nBase) == 0) {
                for (i32 k = 0; k < PUFO_PSI_CHEVRON_STEPS; ++k) {
                    SoftSurf_BlitRectProxy(0, 212 + k, nY + 4 + k, 2, 2, 0xFF, 0, 0); /* 0x72968 */
                    SoftSurf_BlitRectProxy(0, 212 + k, nY + 26 - k, 2, 2, 0xFF, 0, 0); /* 0x72994 */
                }
            }
        }
    }

    /* 0x72A0C..0x72B2C. With the overlay closed the compare is skipped and the shortfall panel is
     * reached straight from 0x72760. */
    if (!bOverlay || (i32)pEnv->nWorkBaseIdx == (i32)*pEnv->pnPsiBaseIdx) {
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30); /* 0x72A94 */
        if (pEnv->pnActionFail != NULL && *pEnv->pnActionFail == 1 &&
            pEnv->pnResidue != NULL && *pEnv->pnResidue == 0) {
            (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1); /* 0x72AD4 */
            PsiUi_DrawCentered(120, 158, PsiUi_Text(pEnv, PUFO_PSI_TEXT_NO_ROOM)); /* 0x72B2C */
        }
    } else if (Base_CalcPsiLabCap(pEnv->abFacilityGrid, (i32)pEnv->nWorkBaseIdx) != 0) {
        *pEnv->pnPsiBaseIdx = pEnv->nWorkBaseIdx;                      /* 0x72A28 */
        *pEnv->pbOverlayOpen = 0;                                      /* 0x72A2C */
    }
}

/* ---- BaseInfoScreen_Draw @ 0x56fb8 ---- */

static const char *BaseInfo_Text(const BaseInfoDrawEnv *pEnv, i32 nRec)
{
    if (pEnv->pTextRecs == NULL) {
        return "";
    }
    return pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec;
}

static const char *BaseInfo_SiteName(const BaseInfoDrawEnv *pEnv, i32 nBaseIdx)
{
    if (pEnv->apSiteRecs == NULL || nBaseIdx < 0 || nBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return ""; /* deviation: 0x57244 indexes the eight records blindly */
    }
    return pEnv->apSiteRecs[nBaseIdx].abName; /* byte_186C24 + 64*i + 54 == 0x186C40 + 64*i + 26 */
}

static void BaseInfo_DrawCentered(i32 nCenterX, i32 y, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz);
    Text_DrawString(nCenterX - nWidth / 2, y, psz);
}

/* The original copies the site name into the 60-byte local with a byte-at-a-time loop and no
 * bound (0x57250..0x57264). The bound here is the deviation. */
static void BaseInfo_CopyName(char *pszDst, i32 cchDst, const char *pszSrc)
{
    i32 i = 0;

    while (i + 1 < cchDst && pszSrc[i] != 0) {
        pszDst[i] = pszSrc[i];
        ++i;
    }
    pszDst[i] = 0;
}

void BaseInfoScreen_Draw(const BaseInfoDrawEnv *pEnv)
{
    char szName[60]; /* psz[60] on the original's frame, at sp+18h */
    i32 nBase, nBaseCount, i;

    if (pEnv == NULL || pEnv->pbBuilt == NULL || *pEnv->pbBuilt == 0) {
        return;                                          /* 0x56FCC: nothing draws while the flag is clear */
    }
    nBase = (pEnv->pnWorkBaseIdx != NULL) ? (i32)*pEnv->pnWorkBaseIdx : 0;

    const bool bMode = (pEnv->pbModeFlag != NULL) && *pEnv->pbModeFlag != 0;
    /* The moving bands need the craft count, and the original re-reads it on all four calls
     * (0x57118, 0x57158, 0x57198, 0x571D8) of every iteration. It is pure, so once is enough. */
    const i32 nShips = (!bMode && pEnv->apCraft != NULL)
                           ? Base_CountShipsAtBase(pEnv->apCraft, pEnv->nCraftCount, pEnv->apBases,
                                                   pEnv->nBaseCount, pEnv->abGridBytes, nBase)
                           : 0;

    /* 0x56FE8..0x57210: the grey ramp, 24 steps of 5 pixels. Both colours are bytes, so the right
     * one wraps: -6-5*step runs 250, 245, ... 135 while the left runs 100, 105, ... 215. */
    for (i32 nStep = 0; nStep < PUFO_BASEINFO_RAMP_STEPS; ++nStep) {
        const i32 x = PUFO_BASEINFO_RAMP_STEP * nStep;
        const u8 nLeft = (u8)(PUFO_BASEINFO_RAMP_STEP * (nStep + PUFO_BASEINFO_RAMP_BASE));
        const u8 nRight = (u8)(PUFO_BASEINFO_RAMP_RIGHT0 - PUFO_BASEINFO_RAMP_STEP * nStep);
        i32 nYb, nYc;

        if (bMode) {
            nYb = PUFO_BASEINFO_BAND_B_FIXED;
            nYc = PUFO_BASEINFO_BAND_C_FIXED;
        } else {
            nYb = PUFO_BASEINFO_BAND_B_PITCH * nShips + PUFO_BASEINFO_BAND_B_OFF;
            nYc = PUFO_BASEINFO_BAND_B_PITCH * nShips + PUFO_BASEINFO_BAND_C_OFF;
        }
        SoftSurf_BlitRectProxy(0, x + 1, PUFO_BASEINFO_BAND_A, 5, 1, nLeft, nLeft, nLeft);
        SoftSurf_BlitRectProxy(0, x + 119, PUFO_BASEINFO_BAND_A, 5, 1, nRight, nRight, nRight);
        SoftSurf_BlitRectProxy(0, x + 1, nYb, 5, 1, nLeft, nLeft, nLeft);
        SoftSurf_BlitRectProxy(0, x + 119, nYb, 5, 1, nRight, nRight, nRight);
        SoftSurf_BlitRectProxy(0, x + 1, nYc, 5, 1, nLeft, nLeft, nLeft);
        SoftSurf_BlitRectProxy(0, x + 119, nYc, 5, 1, nRight, nRight, nRight);
    }

    for (i = 0; i < 3; ++i) {                            /* 0x57218..0x57230 */
        if (pEnv->pppPanel != NULL && pEnv->pppPanel[i] != NULL) {
            UiPanel_DrawText(pEnv->pppPanel[i]);
        }
    }

    BaseInfo_CopyName(szName, (i32)sizeof(szName), BaseInfo_SiteName(pEnv, nBase));
    BaseInfo_DrawCentered(120, 13, szName);             /* 0x57284 */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75); /* 0x5729C */
    BaseInfo_DrawCentered(60, 300, BaseInfo_Text(pEnv, PUFO_BASEINFO_TEXT_SELECT));   /* 0x572F0, TEXTS[1112] */
    BaseInfo_DrawCentered(180, 300,                                              /* 0x57330 / 0x57370 */
                          BaseInfo_Text(pEnv, bMode ? PUFO_BASEINFO_TEXT_MONTHLY
                                                    : PUFO_BASEINFO_TEXT_BASE));

    if (pEnv->pbOverlayOpen == NULL || *pEnv->pbOverlayOpen == 0) {
        return;                                          /* 0x57388 */
    }

    nBaseCount = (pEnv->pnBaseCount != NULL) ? (i32)*pEnv->pnBaseCount : 0;
    SoftSurf_BlitRectProxy(0, 208, 0, 32, PUFO_BASEINFO_ROW_PITCH * nBaseCount + 4,
                           0, 0, 0);                                            /* 0x573B4 */
    if (pEnv->pfnDrawIconStrip != NULL) {
        pEnv->pfnDrawIconStrip(-1, pEnv->pStripUser);                          /* 0x573C0: -1 draws none */
    }
    SoftSurf_BlitRectProxy(0, 0, 0, 210, PUFO_BASEINFO_ROW_PITCH * nBaseCount + 4,
                           0, 0, 0);                                           /* 0x573F0 */

    for (i = 0; i < nBaseCount; ++i) {                       /* 0x57408..0x574F0 */
        const i32 nY = PUFO_BASEINFO_ROW_PITCH * i;

        SoftSurf_BlitRectProxy(0, 3, nY + 3, 205, 26, 0x46, 0xA0, 0xB3);      /* 0x57438 */
        SoftSurf_BlitRectProxy(0, 3, nY + 4, 204, 24, 0x00, 0x6E, 0x77);      /* 0x5746C */
        SoftSurf_BlitRectProxy(0, 5, nY + 5, 201, 22, 0x04, 0x46, 0x4B);      /* 0x57498 */
        BaseInfo_DrawCentered(110, nY + 7, BaseInfo_SiteName(pEnv, i));       /* 0x574E4 */
    }

    /* 0x57500..0x57570: the draw itself adopts a base the geoscape moved to, and rebuilds the
     * panels right here -- so the rebuild lands on this frame, not on the next click. */
    if (pEnv->pnAltBaseIdx != NULL && pEnv->pnWorkBaseIdx != NULL
        && (i32)*pEnv->pnAltBaseIdx != (i32)*pEnv->pnWorkBaseIdx) {
        *pEnv->pnWorkBaseIdx = *pEnv->pnAltBaseIdx;                            /* 0x57508 */
        *pEnv->pbOverlayOpen = 0;                                              /* 0x5750C */
        *pEnv->pbBuilt = 0;                                                    /* 0x57514 */
        if (pEnv->pppPanel != NULL) {
            for (i = 0; i < 3; ++i) {
                UiPanel *pPanel = pEnv->pppPanel[i];

                if (pPanel != NULL) {
                    UiPanel_FreeLines(pPanel);                                 /* 0x57520 / 0x57554 / 0x57588 */
                    free(pPanel);
                }
                pEnv->pppPanel[i] = NULL;
            }
        }
        if (pEnv->pfnBuildPanels != NULL) {
            pEnv->pfnBuildPanels();                                            /* 0x575B0 */
        }
    }
}

/* ---- BaseAtScreen_Update @ 0x57808 ---- */

/* The attack strength the defences have to burn through, per craft class (0x5784C..0x578B8). The ARM
 * builds the last two as 2192|8 and 2992|8; a class outside 0..7 leaves the budget at 0. */
static const i32 g_anBaseAtBudget[8] = { 50, 200, 250, 500, 500, 1200, 2200, 3000 };

static const char *BaseAt_Text(const char *pTextRecs, i32 nRec)
{
    if (pTextRecs == NULL) {
        return "";
    }
    return pTextRecs + PUFO_TEXT_REC_STRIDE * nRec;
}

/* One defence pair (0x57BF8..0x57D7C, and the same shape three more times at 0x57DB4, 0x57F64 and
 * 0x5810C). The "ОГОНЬ!!!" row goes first with aux 20, then the roll decides the result row: below
 * nThreshold the defence HITS -- green meta and TEXTS[1116] -- and burns nDamage off the budget; at
 * or above it the defence MISSES -- red meta and TEXTS[1117]. The second row gets aux 20 last, after
 * the label is already in place. */
static void BaseAt_EmitDefencePair(const BaseAtUpdateEnv *pEnv, UiPanel *pPanel, i32 nRow,
                                   i32 nFireRec, i32 nThreshold, i32 nDamage, i32 *pnBudget)
{
    (void)UiPanel_SetLineText(pPanel, nRow, BaseAt_Text(pEnv->pTextRecs, nFireRec)); /* 0x57C08..0x57C54 */
    UiPanel_SetLineAux(pPanel, nRow, PUFO_BASEAT_AUX_DEFENCE);                       /* 0x57C70 */

    if ((i32)Game_RandU16(PUFO_BASEAT_ROLL_MAX) >= nThreshold) {                     /* 0x57C74..0x57C90 */
        UiPanel_SetLineMeta(pPanel, nRow + 1, 255, 50, 50, 20, 0, 0);                /* 0x57D0C */
        (void)UiPanel_SetLineText(pPanel, nRow + 1,
                                  BaseAt_Text(pEnv->pTextRecs, PUFO_BASEAT_TEXT_MISS));   /* 0x57D1C */
    } else {
        UiPanel_SetLineMeta(pPanel, nRow + 1, 50, 255, 50, 0, 20, 0);                /* 0x57CAC */
        (void)UiPanel_SetLineText(pPanel, nRow + 1,
                                  BaseAt_Text(pEnv->pTextRecs, PUFO_BASEAT_TEXT_HIT));    /* 0x57CC0 */
        *pnBudget -= nDamage;                                                        /* 0x57CE4 */
    }
    UiPanel_SetLineAux(pPanel, nRow + 1, PUFO_BASEAT_AUX_DEFENCE);                   /* 0x57D74 */
}

/* The gravity-shield pair (0x582A8..0x583D8): aux 0 then 10, the same meta on both rows, and no
 * budget change -- the shield repels the craft and the defences get their charge back. */
static void BaseAt_EmitGravityPair(UiPanel *pPanel, i32 nRow, const char *pTextRecs)
{
    UiPanel_SetLineAux(pPanel, nRow, PUFO_BASEAT_AUX_GRAVITY_A);                     /* 0x582C4 */
    UiPanel_SetLineAux(pPanel, nRow + 1, PUFO_BASEAT_AUX_GRAVITY_B);                 /* 0x582E0 */
    UiPanel_SetLineMeta(pPanel, nRow, 150, 150, 255, 0, 20, 20);                     /* 0x582FC */
    UiPanel_SetLineMeta(pPanel, nRow + 1, 150, 150, 255, 0, 20, 20);                 /* 0x58320 */
    (void)UiPanel_SetLineText(pPanel, nRow,
                              BaseAt_Text(pTextRecs, PUFO_BASEAT_TEXT_GRAVITY));     /* 0x58330..0x5837C */
    (void)UiPanel_SetLineText(pPanel, nRow + 1,
                              BaseAt_Text(pTextRecs, PUFO_BASEAT_TEXT_RECHARGED));   /* 0x58388..0x583D4 */
}

void BaseAtScreen_Update(i32 nBaseIdx, const BaseAtUpdateEnv *pEnv)
{
    i32 nGravity;
    i32 nMissile;
    i32 nLaser;
    i32 nPlasma;
    i32 nFusion;
    i32 nBudget;
    i32 nCap;
    i32 nRounds;
    i32 nRow;
    i32 bBudgetGone;
    UiPanel *pPanel;
    PadImage *pPad;

    if (pEnv == NULL || pEnv->pbActive == NULL || pEnv->ppPanel == NULL || pEnv->ppPad == NULL
        || pEnv->pnLineLimit == NULL || pEnv->pnLineCap == NULL) {
        return;
    }

    if (*pEnv->pbActive != 0) {
        /* 0x586B4..0x58718: the teardown. nLineCount is restored from the built capacity FIRST, so
         * UiPanel_FreeLines reaches every line buffer -- the draw had left nLineCount at the revealed
         * row count, which is smaller. The original deletes without nulling either slot. */
        *pEnv->pbActive = 0;                                                       /* 0x586B4 */
        pPanel = *pEnv->ppPanel;
        if (pPanel != NULL) {
            pPanel->nLineCount = *pEnv->pnLineCap;                                 /* 0x586D0 */
            UiPanel_FreeLines(pPanel);                                             /* 0x586E4 */
            free(pPanel);                                                          /* 0x586EC */
            *pEnv->ppPanel = NULL;
        }
        pPad = *pEnv->ppPad;
        if (pPad != NULL) {
            PadImage_FreeData(pPad);                                               /* 0x58704 */
            free(pPad);                                                            /* 0x5870C */
            *pEnv->ppPad = NULL;
        }
        if (pEnv->pbSpawnReplacement != NULL) {
            *pEnv->pbSpawnReplacement = 1;                                         /* 0x58718 */
        }
        return;
    }

    *pEnv->pbActive = 1;                                                           /* 0x57838 */
    if (pEnv->pbConfirmShown != NULL) {
        *pEnv->pbConfirmShown = 0;                                                 /* 0x57840 */
    }

    nBudget = 0;
    if (pEnv->pbCraftClass != NULL) {
        const i32 nClass = (i32)(i8)*pEnv->pbCraftClass;                           /* 0x5784C, (char) */

        if (nClass >= 0 && nClass < 8) {
            nBudget = g_anBaseAtBudget[nClass];
        }
    }

    pPad = (PadImage *)calloc(1, sizeof(PadImage));                                /* 0x578BC, new(0x14) */
    if (pPad != NULL) {
        PadImage_ctor(pPad);                                                       /* 0x578CC */
    }
    *pEnv->ppPad = pPad;                                                           /* 0x578E0 */
    if (pPad != NULL) {
        (void)PadImage_Load(pPad, PUFO_BASEAT_PAD_NAME, PUFO_BASEAT_PAD_MODE);     /* 0x578E4 */
        pPad->nOriginX = 0;                                                        /* 0x578F8 */
        pPad->nOriginY = 0;                                                        /* 0x5790C */
    }

    /* 0x57914..0x57A38: one pass over the base's 36 cells. Every kind test also requires the cell to
     * be free (its buildDays byte is 0), so the days test comes first here. The type layer is
     * 372 + 144*base + 108 and the days layer 36*base, both indexed 6*row + col -- the same cell. */
    nGravity = 0;
    nMissile = 0;
    nLaser = 0;
    nPlasma = 0;
    nFusion = 0;
    if (pEnv->abFacilityGrid != NULL && nBaseIdx >= 0 && nBaseIdx < PUFO_FAC_BASE_COUNT) {
        const u8 *const pDays = pEnv->abFacilityGrid + PUFO_GRID_DAYS_STRIDE * nBaseIdx;
        const u8 *const pType = pEnv->abFacilityGrid + PUFO_GRID_FAC_OFF
                                + PUFO_GRID_FAC_STRIDE * nBaseIdx + PUFO_GRID_FAC_TYPE_OFF;

        for (i32 nCell = 0; nCell < PUFO_GRID_CELL_COUNT; ++nCell) {
            if (pDays[nCell] != 0) {
                continue;
            }
            switch (pType[nCell]) {
            case FAC_GRAVITY_SHIELD:      ++nGravity; break;                       /* 0x5796C */
            case FAC_MISSILE_DEFENCES:    ++nMissile; break;                       /* 0x57998 */
            case FAC_LASER_DEFENCE:       ++nLaser; break;                         /* 0x579C4 */
            case FAC_PLASMA_DEFENCE:      ++nPlasma; break;                        /* 0x579F0 */
            case FAC_FUSION_BALL_DEFENCE: ++nFusion; break;                        /* 0x57A1C */
            default: break;
            }
        }
    }

    /* 0x57A80..0x57A9C: the barrage runs (gravity + 1) rounds, and the capacity is exactly the row
     * count that produces -- defence pairs per round times the rounds, plus the gravity pairs, plus
     * the three tail rows. */
    nRounds = nGravity + 1;
    nCap = 2 * ((nMissile + nLaser + nPlasma + nFusion) * nRounds + nGravity) + 3;
    *pEnv->pnLineCap = nCap;

    pPanel = (UiPanel *)calloc(1, sizeof(UiPanel));                                /* 0x57AA8, new(0x1BB4) */
    if (pPanel != NULL) {
        (void)UiPanel_ctor(pPanel, nCap);                                          /* 0x57AB8 */
    }
    *pEnv->ppPanel = pPanel;                                                       /* 0x57ADC */
    if (pPanel == NULL) {
        return;
    }

    pPanel->nStyle = PUFO_BASEAT_PANEL_STYLE;                                      /* 0x57AE4 */
    pPanel->nFont = PUFO_BASEAT_PANEL_FONT;                                        /* 0x57AF4 */
    pPanel->nPosX = PUFO_BASEAT_PANEL_X;                                           /* 0x57B08 */
    pPanel->nPosY = PUFO_BASEAT_PANEL_Y;                                           /* 0x57B18 */
    pPanel->nUnk7052 = PUFO_BASEAT_PANEL_CLICKL;                                   /* 0x57B2C */
    pPanel->nUnk7056 = PUFO_BASEAT_PANEL_CLICKTOP;                                 /* 0x57B40 */
    pPanel->nViewW = PUFO_BASEAT_PANEL_VIEWW;                                      /* 0x57B54 */
    UiPanel_SetBytes0to2(pPanel, 0x64, 0x96, 0x64);                                /* 0x57B64 */
    pPanel->bUnk7088 = (u8)-106;                                                   /* 0x57B78 */
    UiPanel_SetBytes6to11(pPanel, 0xDC, 0xC8, 0xF0, 0x14, 0x14, 0x14);             /* 0x57B9C */
    UiPanel_SetBytes3to5(pPanel, 0x32, 0x32, 0xFA);                                /* 0x57BB0 */
    pPanel->bFlag7073 = 0;                                                         /* 0x57BC4 */
    pPanel->bFlag7072 = 0;                                                         /* 0x57BDC */

    nRow = 0;
    bBudgetGone = 0;
    for (i32 nRound = 0;; ++nRound) {                                              /* 0x57BEC */
        for (i32 i = 0; i < nMissile; ++i) {
            BaseAt_EmitDefencePair(pEnv, pPanel, nRow, PUFO_BASEAT_TEXT_FIRE_MISSILE,
                                   PUFO_BASEAT_THRESHOLD_MISSILE, PUFO_BASEAT_DAMAGE_MISSILE, &nBudget);
            nRow += 2;
            if (nBudget < 1) { bBudgetGone = 1; goto tail; }                       /* 0x57D7C */
        }
        for (i32 i = 0; i < nLaser; ++i) {
            BaseAt_EmitDefencePair(pEnv, pPanel, nRow, PUFO_BASEAT_TEXT_FIRE_LASER,
                                   PUFO_BASEAT_THRESHOLD_LASER, PUFO_BASEAT_DAMAGE_LASER, &nBudget);
            nRow += 2;
            if (nBudget < 1) { bBudgetGone = 1; goto tail; }                       /* 0x57F28 */
        }
        for (i32 i = 0; i < nPlasma; ++i) {
            BaseAt_EmitDefencePair(pEnv, pPanel, nRow, PUFO_BASEAT_TEXT_FIRE_PLASMA,
                                   PUFO_BASEAT_THRESHOLD_PLASMA, PUFO_BASEAT_DAMAGE_PLASMA, &nBudget);
            nRow += 2;
            if (nBudget < 1) { bBudgetGone = 1; goto tail; }                       /* 0x580D0 */
        }
        for (i32 i = 0; i < nFusion; ++i) {
            BaseAt_EmitDefencePair(pEnv, pPanel, nRow, PUFO_BASEAT_TEXT_FIRE_FUSION,
                                   PUFO_BASEAT_THRESHOLD_FUSION, PUFO_BASEAT_DAMAGE_FUSION, &nBudget);
            nRow += 2;
            if (nBudget < 1) { bBudgetGone = 1; goto tail; }                       /* 0x58280 */
        }
        if (nGravity > 0 && nGravity != nRound) {                                  /* 0x582A8 */
            BaseAt_EmitGravityPair(pPanel, nRow, pEnv->pTextRecs);
            nRow += 2;
        }
        if (nRound >= nGravity) {
            break;                                                                 /* 0x583F0, ++v144 >= v10 + 1 */
        }
    }

tail:
    *pEnv->pnLineLimit = nRow + 3;                                                 /* 0x58424 */
    UiPanel_SetLineAux(pPanel, nRow, 0);                                           /* 0x58434 */
    UiPanel_SetLineAux(pPanel, nRow + 1, 0);                                       /* 0x58448 */
    UiPanel_SetLineAux(pPanel, nRow + 2, 0);                                       /* 0x5845C */
    (void)UiPanel_SetLineText(pPanel, nRow, ""); /* 0x58464: 0x10DA38 is zero-filled .data, not a record */

    if (bBudgetGone) {
        (void)UiPanel_SetLineText(pPanel, nRow + 1,
                                  BaseAt_Text(pEnv->pTextRecs, PUFO_BASEAT_TEXT_DESTROYED)); /* 0x584C0 */
        (void)UiPanel_SetLineText(pPanel, nRow + 2,
                                  BaseAt_Text(pEnv->pTextRecs, PUFO_BASEAT_TEXT_THREAT));    /* 0x58518 */
        if (pEnv->pbMissionFollows != NULL) {
            *pEnv->pbMissionFollows = 0;                                           /* 0x58570 */
        }
    } else {
        (void)UiPanel_SetLineText(pPanel, nRow + 1,
                                  BaseAt_Text(pEnv->pTextRecs, PUFO_BASEAT_TEXT_LANDED));   /* 0x585D4 */
        (void)UiPanel_SetLineText(pPanel, nRow + 2,
                                  BaseAt_Text(pEnv->pTextRecs, PUFO_BASEAT_TEXT_PREPARING)); /* 0x58630 */
        if (pEnv->pbMissionFollows != NULL) {
            *pEnv->pbMissionFollows = 1;                                           /* 0x58688 */
        }
    }

    if (pEnv->pnRadarCooldownR != NULL) {
        *pEnv->pnRadarCooldownR = PUFO_RADAR_COOLDOWN_READY;                      /* 0x58698 */
    }
    pPanel->nLineCount = 0;                                                        /* 0x586A4 */
}

/* ---- BaseAtScreen_Draw @ 0x58728 ---- */

void BaseAtScreen_Draw(const BaseAtDrawEnv *pEnv)
{
    char szOk[52];
    UiPanel *pPanel;
    i32 nLimit;

    if (pEnv == NULL || pEnv->pbActive == NULL || *pEnv->pbActive == 0) {
        return;
    }
    if (pEnv->ppPad != NULL && *pEnv->ppPad != NULL) {
        PadImage_Blit(*pEnv->ppPad);                                               /* 0x58744 */
    }
    pPanel = (pEnv->ppPanel != NULL) ? *pEnv->ppPanel : NULL;
    nLimit = (pEnv->pnLineLimit != NULL) ? *pEnv->pnLineLimit : 0;

    /* 0x58758..0x587C0: one row per cooldown. The cooldown is parked at 255 minus the aux of the row
     * about to be shown, so the geoscape's saturating counter has to run that many ticks before the
     * next row appears -- 20 for a defence row, 10 for the gravity pair's second row, 0 for a tail row,
     * which the counter reaches at once. */
    if (pPanel != NULL && pEnv->pnRadarCooldownR != NULL
        && *pEnv->pnRadarCooldownR == PUFO_RADAR_COOLDOWN_READY
        && pPanel->nLineCount < nLimit) {
        *pEnv->pnRadarCooldownR = PUFO_RADAR_COOLDOWN_READY
                                  - UiPanel_GetLineAux(pPanel, pPanel->nLineCount); /* 0x58788 */
        ++pPanel->nLineCount;                                                      /* 0x5879C */
        if (pPanel->nLineCount > pPanel->nStyle) {                                 /* 0x587B4 */
            UiPanel_ScrollNext(pPanel);                                            /* 0x587C0 */
        }
    }

    if (pPanel != NULL) {
        UiPanel_DrawText(pPanel);                                                  /* 0x587CC */
    }
    if (pPanel == NULL || nLimit != pPanel->nLineCount) {                          /* 0x587D8 */
        return;
    }

    if (pEnv->pCtlConfirm != NULL) {
        pEnv->pCtlConfirm->x = (i16)(PUFO_BASEAT_CONFIRM_X - (pEnv->pCtlConfirm->uWidth >> 1)); /* 0x587EC */
        pEnv->pCtlConfirm->y = PUFO_BASEAT_CONFIRM_Y;                              /* 0x587F8 */
    }
    if (pEnv->pbConfirmShown != NULL) {
        *pEnv->pbConfirmShown = 1;                                                 /* 0x5880C */
    }
    if (pEnv->pCtlConfirm != NULL) {
        (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit,
                                      pEnv->pfnCtlBlitEx, pEnv->pCtlUser);         /* 0x58814 */
    }

    snprintf(szOk, sizeof szOk, "%s", BaseAt_Text(pEnv->pTextRecs, PUFO_BASEAT_TEXT_OK)); /* 0x5881C..0x58838 */
    Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);                    /* 0x5884C */
    BaseInfo_DrawCentered(PUFO_BASEAT_CONFIRM_X, PUFO_BASEAT_CONFIRM_Y, szOk);     /* 0x58868..0x5888C */
}

/* ---- BaseAtScreen_CloseThunk @ 0x577c0 ---- */

void BaseAtScreen_CloseThunk(const BaseAtCloseEnv *pEnv)
{
    if (pEnv == NULL) {
        return;
    }
    if (pEnv->pbMissionFollows == NULL || *pEnv->pbMissionFollows == 0) {          /* 0x577C8 */
        (void)StrategList_RemoveAt(pEnv->pStrategList, pEnv->pWorld,
                                   (pEnv->pnStrategIdx != NULL) ? *pEnv->pnStrategIdx : 0,
                                   pEnv->pbSpawnReplacement, pEnv->pSpawnCtx);      /* 0x577D4 */
        if (pEnv->pTime != NULL) {
            pEnv->pTime->bPaused = 0; /* 0x577E4: GameTime_Unpause is a store of 0 to 0x15961C */
        }
    }
    Geoscape_CloseUI(pEnv->pClose);                                                /* 0x577F4, the tail call */
}
