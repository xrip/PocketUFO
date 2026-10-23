/* test_tactui.c - checks the tactical action-mode / UI-screen transitions.
 *
 * The filter-pad mapping is the interesting case: the selection number does NOT
 * match the pad's address order, so these tests pin the exact measured mapping.
 */
#include "game/pufo_tactui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

/* Clip-ammo-cap lookup for TactItem_TryReloadClip; the caller passes the cap. */
static u8 ClipCap(u16 nAmmoTypeId, void *pUser)
{
    (void)nAmmoTypeId;
    return *(const u8 *)pUser;
}

/* --- recording hooks ------------------------------------------------------ */

typedef struct Log {
    i32 nSamples;
    i32 nLastSample;
    i32 nStatus;
    i32 nLastStatus;
    /* Every pushed record, in order, so a two-message refusal can be pinned by index
     * and not just by count. */
    i32 aStatus[8];
    i32 nCursorFlags;
    i32 nLastFlags;
} Log;

static Log g_log;

static void OnPlay(i32 nSample, void *pUser)
{
    (void)pUser;
    g_log.nSamples += 1;
    g_log.nLastSample = nSample;
}

static void OnStatus(i32 nIdx, void *pUser)
{
    (void)pUser;
    if (g_log.nStatus < (i32)(sizeof(g_log.aStatus) / sizeof(g_log.aStatus[0]))) {
        g_log.aStatus[g_log.nStatus] = nIdx;
    }
    g_log.nStatus += 1;
    g_log.nLastStatus = nIdx;
}

static void OnCursorFlags(u8 nFlags, void *pUser)
{
    (void)pUser;
    g_log.nCursorFlags += 1;
    g_log.nLastFlags = nFlags;
}

static const TactUiHooks g_hooks = {
    OnPlay, NULL, OnStatus, OnCursorFlags, NULL
};

static void LogReset(void)
{
    memset(&g_log, 0, sizeof(g_log));
}

static void UiReset(TactUiState *pUi)
{
    memset(pUi, 0, sizeof(*pUi));
    /* Give the cost table a known shape so the fire-mode tests are meaningful. */
    for (i32 i = 0; i < PUFO_ACTION_MODE_COUNT; ++i) {
        pUi->anActionCost[i] = 10 + i;
    }
}

/* --- tests ---------------------------------------------------------------- */

/* --- the seven inventory filter buttons ---------------------------------------------------------------
 * The filter row is seven separate ARM functions, not one function with an argument. A byte census over all
 * seven 128-byte bodies shows they are identical except two immediates each -- the MOV that feeds one pad
 * store, and the MOV that feeds the selection byte unk_1183DE:
 *
 *   Inv_SetFilterAll @ 0x17410  lights unk_AC9DA (the All pad),  stores unk_1183DE = 0xFF at 0x1747C
 *   Inv_SetFilter0   @ 0x17490  lights unk_AC9FA,                stores unk_1183DE = 0 at 0x174FC
 *   Inv_SetFilter1   @ 0x17610  lights unk_ACA5A,                stores unk_1183DE = 1 at 0x1767C
 *   Inv_SetFilter2   @ 0x17690  lights unk_ACA7A,                stores unk_1183DE = 2 at 0x176FC
 *   Inv_SetFilter3   @ 0x17510  lights unk_ACA1A,                stores unk_1183DE = 3 at 0x1757C
 *   Inv_SetFilter4   @ 0x17590  lights unk_ACA3A,                stores unk_1183DE = 4 at 0x175FC
 *   Inv_SetFilter5   @ 0x17710  lights pBank + 0x2AA (the X pad), stores unk_1183DE = 5 at 0x1777C
 *
 * All seven first zero the seven pad globals and byte_1153DA[0], play sample 17, and end in a tail
 * `B Inv_BuildVisibleList` -- so the original hands back the rebuild's value, while the port returns true
 * (no port path reads it: World_TactFilterCall discards it). The port implements the family once, as
 * Ui_SetInvFilter, and its pad order matches those globals in address order: abFilterPad[0..4] is
 * unk_AC9FA, unk_ACA1A, unk_ACA3A, unk_ACA5A, unk_ACA7A, bFilterPadX is pBank + 0x2AA and bFilterPadAll is
 * unk_AC9DA. The loop below is the check for all seven addresses: which pad ends lit, and what byte the
 * filter leaves in the selection index.
 */
static void TestFilterMapping(void)
{
    TactUiState ui;
    /* The measured mapping from filter number to pad, in pad address order:
     *   pad[0] <- filter 0     pad[1] <- filter 3
     *   pad[2] <- filter 4     pad[3] <- filter 1
     *   pad[4] <- filter 2     X pad  <- filter 5 */
    static const i32 aExpectPad[6] = { 0, 3, 4, 1, 2, -1 /* X */ };

    printf("Ui_SetInvFilter -- number-to-pad mapping\n");
    for (i32 nFilter = 0; nFilter < 6; ++nFilter) {
        i32 nOn = -1;

        UiReset(&ui);
        LogReset();
        /* A sentinel count: the rebuild runs before the pads are touched, so a
         * filter change always clears it. */
        ui.nInvListCount = 7;
        Check(Ui_SetInvFilter(&ui, nFilter, &g_hooks), "  filter applied");
        for (i32 p = 0; p < PUFO_INV_FILTER_PADS; ++p) {
            if (ui.abFilterPad[p] != 0) {
                nOn = p;
            }
        }
        if (aExpectPad[nFilter] == -1) {
            CheckEqI32(ui.bFilterPadX, 1, "  filter 5 turns on the X pad");
            CheckEqI32(nOn, -1, "  filter 5 leaves the numbered pads clear");
        } else {
            CheckEqI32(nOn, aExpectPad[nFilter], "  filter turns on the measured pad");
            CheckEqI32(ui.bFilterPadX, 0, "  X pad stays clear");
        }
        CheckEqI32(ui.bFilterPadAll, 0, "  All pad stays clear");
        CheckEqI32(ui.nInvListSel, nFilter, "  selection index is the filter number");
        CheckEqI32(ui.nInvFilterType, 0, "  scroll cursor reset");
        CheckEqI32(g_log.nSamples, 1, "  played one sample");
        CheckEqI32(g_log.nLastSample, 17, "  sample 17");
        CheckEqI32(ui.nInvListCount, 0, "  rebuilt the visible list (no unit)");
    }

    /* All: the All pad, and a selection index of -1. */
    UiReset(&ui);
    Check(Ui_SetInvFilter(&ui, PUFO_INV_FILTER_ALL, &g_hooks), "  All applied");
    CheckEqI32(ui.bFilterPadAll, 1, "  All pad set");
    CheckEqI32(ui.bFilterPadX, 0, "  X pad clear");
    /* nInvListSel is a u8, so the original's `= -1` stores 0xFF, i.e. 255. */
    CheckEqI32(ui.nInvListSel, 255, "  selection index is 255 (byte -1)");

    /* Only one pad may be set. */
    UiReset(&ui);
    ui.abFilterPad[0] = 1;
    ui.abFilterPad[2] = 1;
    ui.bFilterPadX = 1;
    ui.bFilterPadAll = 1;
    Check(Ui_SetInvFilter(&ui, 1, &g_hooks), "  re-filter applied");
    {
        i32 nSet = 0;

        for (i32 p = 0; p < PUFO_INV_FILTER_PADS; ++p) {
            nSet += ui.abFilterPad[p] != 0 ? 1 : 0;
        }
        nSet += ui.bFilterPadX != 0 ? 1 : 0;
        nSet += ui.bFilterPadAll != 0 ? 1 : 0;
        CheckEqI32(nSet, 1, "  exactly one pad is set afterwards");
    }

    /* Out-of-range values are refused without touching anything. */
    UiReset(&ui);
    ui.nActionMode = 99;
    Check(!Ui_SetInvFilter(&ui, 6, &g_hooks), "  filter 6 refused");
    Check(!Ui_SetInvFilter(&ui, -2, &g_hooks), "  filter -2 refused");
    CheckEqI32(ui.nActionMode, 99, "  refused filter changed nothing");
}

static void TestScroll(void)
{
    TactUiState ui;

    printf("Ui_InvScrollUp / Ui_InvScrollDown\n");
    UiReset(&ui);
    ui.nInvListCount = 10;
    ui.nInvFilterType = 5;
    Ui_InvScrollUp(&ui);
    CheckEqI32(ui.nInvFilterType, 4, "  scroll up decrements");
    ui.nInvFilterType = 0;
    Ui_InvScrollUp(&ui);
    CheckEqI32(ui.nInvFilterType, 0, "  scroll up stops at 0");

    /* The guard is `(u32)filter + 4 < count`. */
    ui.nInvListCount = 10;
    ui.nInvFilterType = 5;
    Ui_InvScrollDown(&ui);
    CheckEqI32(ui.nInvFilterType, 6, "  5 + 4 < 10 -> increments");
    ui.nInvFilterType = 6;
    Ui_InvScrollDown(&ui);
    CheckEqI32(ui.nInvFilterType, 6, "  6 + 4 < 10 is false -> stops");

    /* No sample and no list rebuild, unlike the filter setters. */
    UiReset(&ui);
    ui.nInvListCount = 10;
    ui.nInvFilterType = 3;
    LogReset();
    Ui_InvScrollDown(&ui);
    CheckEqI32(g_log.nSamples, 0, "  scroll plays no sample");
    CheckEqI32(ui.nInvListCount, 10, "  scroll does not rebuild the list");
}

/* --- Inv_BuildVisibleList @ 0x2d580 ---------------------------------------- */

/* One cell, five items over four objects, chosen so every rule fires:
 *
 *   item0 obj0 key 30 cat 1 ammo 3   first copy of obj0
 *   item1 obj1 key 10 cat 2 ammo 7   first copy of obj1
 *   item2 obj0 key 30 cat 1 ammo 9   merge: larger ammo REPLACES item0
 *   item3 obj1 key 10 cat 2 ammo 7   merge: equal ammo keeps item1
 *   item4 obj2 key 20 cat 1 ammo 1   bTwoHanded == 2 -> never listed
 *   item5 obj3 key  5 cat 3 ammo 4   a third object
 *
 * With the "all" filter the merged list is obj0(c2) obj1(c2) obj3(c1) and the
 * sort by uSortKey puts it in the order obj3(5) obj1(10) obj0(30). */
static ObjectRec g_aObj[4];
static TactItem  g_aItem[6];
static MapCell   g_Cell;
static Soldier   g_Unit;

static void ListReset(void)
{
    i32 i;

    memset(g_aObj, 0, sizeof(g_aObj));
    memset(g_aItem, 0, sizeof(g_aItem));
    memset(&g_Cell, 0, sizeof(g_Cell));
    memset(&g_Unit, 0, sizeof(g_Unit));

    g_aObj[0].uSortKey = 30; g_aObj[0].nCategory = 1;
    g_aObj[1].uSortKey = 10; g_aObj[1].nCategory = 2;
    g_aObj[2].uSortKey = 20; g_aObj[2].nCategory = 1; g_aObj[2].bTwoHanded = 2;
    g_aObj[3].uSortKey = 5;  g_aObj[3].nCategory = 3;

    for (i = 0; i < 6; ++i) {
        g_aItem[i].pNext = (i + 1 < 6) ? &g_aItem[i + 1] : NULL;
    }
    /* Two items share obj0 and two share obj1; obj2 is the two-handed one. */
    g_aItem[0].pObj = &g_aObj[0];
    g_aItem[1].pObj = &g_aObj[1];
    g_aItem[2].pObj = &g_aObj[0];
    g_aItem[3].pObj = &g_aObj[1];
    g_aItem[4].pObj = &g_aObj[2];
    g_aItem[5].pObj = &g_aObj[3];
    g_aItem[0].nAmmo = 3;
    g_aItem[1].nAmmo = 7;
    g_aItem[2].nAmmo = 9;
    g_aItem[3].nAmmo = 7;
    g_aItem[4].nAmmo = 1;
    g_aItem[5].nAmmo = 4;

    g_Cell.pItemList = g_aItem;
    g_Unit.pCell = &g_Cell;
    g_Unit.nUnitType = 0;
}

static void TestBuildVisibleList(void)
{
    TactUiState ui;
    TactUiHooks hooks = g_hooks;

    printf("Inv_BuildVisibleList -- merge, filter, sort\n");
    ListReset();
    hooks.pSelUnit = &g_Unit;

    /* The "all" filter: merge by ObjectRec, keep the larger ammo, sort ascending. */
    UiReset(&ui);
    ui.nInvListSel = 0xFF;
    Inv_BuildVisibleList(&ui, &g_Unit);
    CheckEqI32(ui.nInvListCount, 3, "  three distinct objects survive");
    Check(ui.apInvList[0]->pObj == &g_aObj[3], "  slot 0 is the key-5 object");
    Check(ui.apInvList[1]->pObj == &g_aObj[1], "  slot 1 is the key-10 object");
    Check(ui.apInvList[2]->pObj == &g_aObj[0], "  slot 2 is the key-30 object");
    CheckEqI32(ui.anInvCount[0], 1, "  key 5 has one copy");
    CheckEqI32(ui.anInvCount[1], 2, "  key 10 has two copies");
    CheckEqI32(ui.anInvCount[2], 2, "  key 30 has two copies");
    Check(ui.apInvList[2] == &g_aItem[2], "  the larger-ammo copy replaced the first");
    Check(ui.apInvList[1] == &g_aItem[1], "  an equal ammo keeps the first copy");
    Check(ui.apInvList[0] != &g_aItem[4] && ui.apInvList[1] != &g_aItem[4] &&
          ui.apInvList[2] != &g_aItem[4], "  the two-handed-mode-2 item is never listed");

    /* A numbered filter keeps only the matching ObjectRec.nCategory. */
    ui.nInvListSel = 1;
    Inv_BuildVisibleList(&ui, &g_Unit);
    CheckEqI32(ui.nInvListCount, 1, "  category 1 leaves one entry");
    Check(ui.apInvList[0]->pObj == &g_aObj[0], "  the entry is the category-1 object");
    CheckEqI32(ui.anInvCount[0], 2, "  its count is still merged");

    ui.nInvListSel = 2;
    Inv_BuildVisibleList(&ui, &g_Unit);
    CheckEqI32(ui.nInvListCount, 1, "  category 2 leaves one entry");
    Check(ui.apInvList[0]->pObj == &g_aObj[1], "  and it is the category-2 object");

    /* A category nothing holds empties the list. */
    ui.nInvListSel = 4;
    Inv_BuildVisibleList(&ui, &g_Unit);
    CheckEqI32(ui.nInvListCount, 0, "  an unmatched category empties the list");

    /* The count is cleared before the walk, so a NULL unit cannot leave a stale
     * list behind (the original would fault here instead). */
    ui.nInvListSel = 0xFF;
    ui.nInvListCount = 9;
    Inv_BuildVisibleList(&ui, NULL);
    CheckEqI32(ui.nInvListCount, 0, "  NULL unit clears the count first");

    /* An empty cell clears the count and does nothing else. */
    g_Cell.pItemList = NULL;
    ui.nInvListCount = 9;
    Inv_BuildVisibleList(&ui, &g_Unit);
    CheckEqI32(ui.nInvListCount, 0, "  an empty cell leaves an empty list");

    /* A filter handler reaches the same code through the hooks. */
    g_Cell.pItemList = g_aItem;
    UiReset(&ui);
    Check(Ui_SetInvFilter(&ui, PUFO_INV_FILTER_ALL, &hooks), "  All filter applied");
    CheckEqI32(ui.nInvListCount, 3, "  the filter rebuilt the list through the hooks");
    Check(ui.apInvList[0]->pObj == &g_aObj[3], "  and sorted it");
}

static void TestRadio(void)
{
    TactUiState ui;

    printf("Ui_SelectRadio\n");
    for (i32 r = 0; r < 4; ++r) {
        UiReset(&ui);
        LogReset();
        Check(Ui_SelectRadio(&ui, r, &g_hooks), "  radio selected");
        {
            i32 nSet = 0;

            for (i32 i = 0; i < 4; ++i) {
                nSet += ui.abRadio[i] != 0 ? 1 : 0;
            }
            CheckEqI32(nSet, 1, "  exactly one radio is on");
        }
        CheckEqI32(ui.abRadio[r], 1, "  the chosen radio is the one lit");
        CheckEqI32(g_log.nSamples, 1, "  played one sample");
        /* Each handler's own MOV R0,#0x11 -- 0x16E0C, 0x16E48, 0x16E84, 0x16EC0 -- before Sound_Play. */
        CheckEqI32(g_log.nLastSample, 17, "  ... and it is sample 17, the 0x11 literal");
    }

    /* One row per handler: the four bank handlers are separate functions, each with its own one-hot
     * store order -- UiBank_SelectRadio0_Play11 @ 0x16e04 lights option 0 (MOV R1,#1 at 0x16E18, the four
     * STRB at 0x16E1C..0x16E34), ...2 @ 0x16e7c lights option 2 (0x16E94..0x16EAC) and ...3 @ 0x16eb8
     * lights option 3 (0x16ED0..0x16EE8). All four write the same four bytes g_bRadioOpt0..3, reached as
     * pBank + a fixed offset (LDR R0,=pBank at 0x16E14, 0x16E8C, 0x16EC8). */
    static const u8 aWantOneHot[4][4] = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };
    static const char *const apszOneHot[4] = {
        "  radio 0's handler lights 1,0,0,0 @ 0x16e04",
        "  radio 1's handler lights 0,1,0,0",
        "  radio 2's handler lights 0,0,1,0 @ 0x16e7c",
        "  radio 3's handler lights 0,0,0,1 @ 0x16eb8"
    };
    for (i32 r = 0; r < 4; ++r) {
        i32 i;
        bool bBad = false;

        UiReset(&ui);
        for (i = 0; i < 4; ++i) {
            ui.abRadio[i] = 1;      /* everything on: a missing clear shows up here */
        }
        LogReset();
        (void)Ui_SelectRadio(&ui, r, &g_hooks);
        for (i = 0; i < 4; ++i) {
            if (ui.abRadio[i] != aWantOneHot[r][i]) {
                bBad = true;
            }
        }
        Check(!bBad, apszOneHot[r]);
        CheckEqI32(g_log.nSamples, 1, "  ... and plays exactly one sample");
    }

    UiReset(&ui);
    Check(!Ui_SelectRadio(&ui, 4, &g_hooks), "  radio 4 refused");
    Check(!Ui_SelectRadio(&ui, -1, &g_hooks), "  radio -1 refused");
}

static void TestScreensAndLayers(void)
{
    TactUiState ui;

    printf("Ui_OpenCommandScreen / OpenScreen7 / OpenScreen15\n");
    UiReset(&ui);
    ui.bUnk1508C = 7;
    LogReset();
    Ui_OpenCommandScreen(&ui, &g_hooks);
    CheckEqI32(ui.nUiScreen, 4, "  command screen is 4");
    CheckEqI32(ui.bUnk1508C, 0, "  the 0x1508C byte is cleared");
    CheckEqI32(g_log.nLastSample, 16, "  command screen plays sample 16");

    UiReset(&ui);
    Ui_OpenScreen7(&ui, &g_hooks);
    CheckEqI32(ui.nUiScreen, 7, "  screen 7 set");
    CheckEqI32(g_log.nLastSample, 14, "  screen 7 plays sample 14");

    UiReset(&ui);
    Ui_OpenScreen15(&ui, &g_hooks);
    CheckEqI32(ui.nUiScreen, 15, "  screen 15 set");
    CheckEqI32(g_log.nLastSample, 15, "  screen 15 plays sample 15");

    printf("Ui_LayerUp / Ui_LayerDown -- bounds 1..4\n");
    UiReset(&ui);
    ui.nLayerScratch = 2;
    LogReset();
    Ui_LayerUp(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 3, "  layer up 2 -> 3");
    CheckEqI32(g_log.nLastFlags, 3, "  cursor flags follow the layer");
    CheckEqI32(g_log.nLastSample, 17, "  layer change plays sample 17");

    ui.nLayerScratch = 4;
    Ui_LayerUp(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 4, "  layer up refuses at 4");

    ui.nLayerScratch = 3;
    Ui_LayerDown(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 2, "  layer down 3 -> 2");

    /* The lower bound is 1, so layer 0 is unreachable through this path. */
    ui.nLayerScratch = 1;
    Ui_LayerDown(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 1, "  layer down refuses at 1 (0 is unreachable)");
}

static void TestFireModes(void)
{
    TactUiState    ui;
    FireModeWeapon w;

    printf("Ui_SetFireMode -- empty weapon refusal\n");
    /* An empty weapon with a real capacity is refused. */
    UiReset(&ui);
    memset(&w, 0, sizeof(w));
    w.nAmmo = 0;
    w.bAmmoCap = 10;
    LogReset();
    Check(!Ui_SetFireMode(&ui, PUFO_ACTION_MODE_AIMED, &w, &g_hooks),
          "  empty weapon refused");
    CheckEqI32(g_log.nStatus, 1, "  pushed the out-of-ammo message");
    CheckEqI32(g_log.nLastStatus, PUFO_TXT_OUT_OF_AMMO,
               "  the record is 6, byte_B3D9E = 1230/205");
    CheckEqI32(ui.nActionMode, 0, "  mode unchanged (was 0)");

    /* Capacity 255 means unlimited, so an empty weapon is allowed. */
    UiReset(&ui);
    w.bAmmoCap = 255;
    Check(Ui_SetFireMode(&ui, PUFO_ACTION_MODE_AIMED, &w, &g_hooks),
          "  unlimited weapon allowed while empty");
    CheckEqI32(ui.nActionMode, PUFO_ACTION_MODE_AIMED, "  aimed mode set");
    CheckEqI32(ui.nUnk40, 1, "  aimed writes 1");

    /* Loaded weapons always work, and each mode writes its own pair. */
    UiReset(&ui);
    w.nAmmo = 5;
    w.bAmmoCap = 10;
    Check(Ui_SetFireMode(&ui, PUFO_ACTION_MODE_SNAP, &w, &g_hooks), "  snap accepted");
    CheckEqI32(ui.nActionMode, PUFO_ACTION_MODE_SNAP, "  snap mode set");
    CheckEqI32(ui.nUnk40, 1, "  snap writes 1");

    Check(Ui_SetFireMode(&ui, PUFO_ACTION_MODE_AUTO, &w, &g_hooks), "  auto accepted");
    CheckEqI32(ui.nActionMode, PUFO_ACTION_MODE_AUTO, "  auto mode set");
    CheckEqI32(ui.nUnk40, 3, "  auto writes 3");

    /* NULL weapon is the port's documented deviation: refuse, do not crash. */
    UiReset(&ui);
    LogReset();
    Check(!Ui_SetFireMode(&ui, PUFO_ACTION_MODE_SNAP, NULL, &g_hooks),
          "  NULL weapon refused instead of dereferenced");
    CheckEqI32(g_log.nStatus, 1, "  NULL weapon reports out of ammo");

    /* A mode that is not one of the three is refused. */
    UiReset(&ui);
    Check(!Ui_SetFireMode(&ui, 5, &w, &g_hooks), "  mode 5 refused by SetFireMode");
}

static void TestTryFireMode(void)
{
    TactUiState ui;

    printf("Ui_TryFireMode -- TU affordability\n");
    /* Exactly enough TU: accepted, latch 4. */
    UiReset(&ui);
    LogReset();
    Check(Ui_TryFireMode(&ui, 4, (u16)ui.anActionCost[4], &g_hooks),
          "  exactly enough TU accepted");
    CheckEqI32(ui.nActionMode, 4, "  mode set");
    CheckEqI32(ui.bActionLatch, PUFO_ACTION_LATCH_FIRE, "  latch is 4");
    CheckEqI32(ui.nUnk40, 0, "  nUnk40 cleared");
    CheckEqI32(ui.nUiScreen, 0, "  screen cleared");

    /* One TU short: refused, mode falls back to 12. */
    UiReset(&ui);
    LogReset();
    Check(!Ui_TryFireMode(&ui, 5, (u16)(ui.anActionCost[5] - 1), &g_hooks),
          "  one TU short refused");
    CheckEqI32(ui.nActionMode, PUFO_ACTION_MODE_NONE, "  mode falls back to 12");
    CheckEqI32(ui.bActionLatch, PUFO_ACTION_LATCH_NONE, "  latch cleared");
    CheckEqI32(g_log.nStatus, 1, "  out-of-TU message pushed");

    /* The requested mode is written before the test, so a later reader sees it. */
    UiReset(&ui);
    (void)Ui_TryFireMode(&ui, 10, 0, &g_hooks);
    CheckEqI32(ui.anActionCost[10], 20, "  cost table consulted for mode 10");

    UiReset(&ui);
    Check(!Ui_TryFireMode(&ui, 99, 999, &g_hooks), "  mode 99 refused");
}

static void TestActionCosts(void)
{
    ActionCostInput in;
    TactUiState    ui;

    printf("Ui_FillActionCosts\n");
    memset(&in, 0, sizeof(in));
    /* The soft-double product resolves to auStat4 * pct * 0.01. */
    in.auStat4 = 50;
    in.abCostPctPacked[0] = 80;
    in.abFlagByMode[0] = 60;
    in.auStat0 = 200;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    /* 50 * 80 * 0.01 = 40. Cost = 200 * 60 / 100 = 120. */
    CheckEqI32(ui.anActionEnabled[0], 40, "  mode 0 enabled = auStat4 * pct * 0.01 @ 0x26104");
    CheckEqI32(ui.anActionCost[0], 120, "  mode 0 cost = auStat0 * pct2 / 100 @ 0x26104");

    /* Kneeling raises the price by a quarter: 40 + 10 = 50. */
    in.bFlags = 1;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[0], 50, "  kneeling adds a quarter");

    /* Weapon types 13 and 35 disable modes 0..2 entirely. */
    in.bFlags = 0;
    in.uSortKey = 13;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[0], 0, "  sort key 13 disables mode 0");

    /* Mode 7 uses the packed byte at index 2 as its percentage. */
    in.abFlagByMode[2] = 44;
    in.auStat0 = 100;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[7], 1, "  mode 7 enabled for sort key 13");
    CheckEqI32(ui.anActionCost[7], 44, "  mode 7 cost uses abFlagByMode[2]");

    /* Mode 4/10 enable only for sort key 24; mode 5 likewise. */
    memset(&in, 0, sizeof(in));
    in.auStat0 = 100;
    in.uSortKey = 24;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[4], 1, "  mode 4 enabled for sort key 24");
    CheckEqI32(ui.anActionEnabled[10], 1, "  mode 10 enabled for sort key 24");
    CheckEqI32(ui.anActionCost[4], 34, "  mode 4 cost 34%");
    CheckEqI32(ui.anActionCost[5], 50, "  mode 5 cost 50%");

    /* Mode 3 needs category 3, bUnk8 clear and a sort key other than 14. */
    memset(&in, 0, sizeof(in));
    in.auStat0 = 100;
    in.nCategory = 3;
    in.bWeaponUnk8 = 0;
    in.uSortKey = 20;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[3], 1, "  mode 3 enabled");
    CheckEqI32(ui.anActionCost[3], 20, "  mode 3 cost 20%");

    in.uSortKey = 14;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[3], 0, "  sort key 14 disables mode 3");

    /* Mode 11: an operative non-human is refused. */
    memset(&in, 0, sizeof(in));
    in.auStat0 = 100;
    in.auStat5 = 30;
    in.uSortKey = 0;
    in.nUnitType = 5;
    in.bOperative = true;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[11], 0, "  operative non-human gets no mode 11");

    in.nUnitType = 0;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[11], 30, "  a human keeps mode 11 enabled");

    /* The tables must stay in step with the readers. */
    CheckEqI32(PUFO_ACTION_MODE_COUNT, 12, "  twelve action modes");
    CheckEqI32(PUFO_ACTION_MODE_NONE, 12, "  the failure mode is 12");
}

/* The bank mirror at the tail of Ui_FillActionCosts: 0x26404 `ADD R3,R10,#0x400` with
 * `LDR R10,=pBank` (pBank @ 0xAC6D0), 0x2640c `ADD R3,R3,R8,LSL#5` (the 0x20 UiControl stride),
 * 0x2641c `STRB R2,[R3,#9]` with R2 = 0/1 taken from the enabled value. Widget 32 + m, byte
 * bEnabled @ UiControl+9 -- the twelve pads of the screen-3 roster span. It matters because
 * UiControl_HitTest (0x1A3F0) returns false the moment bEnabled is 0. */
static void TestActionCostBankMirror(void)
{
    static UiControl aPads[PUFO_ACTION_MODE_COUNT];
    ActionCostInput  in;
    TactUiState      ui;
    bool             bMirror = true;
    i32              nEnabled = 0;

    printf("Ui_FillActionCosts bank mirror (0x2641C)\n");
    memset(aPads, 0, sizeof(aPads));
    memset(&in, 0, sizeof(in));
    in.auStat0   = 100;
    in.auStat5   = 30;          /* mode 11's enabled value, a number rather than 1 */
    in.nCategory = 3;
    in.uSortKey  = 1;           /* modes 0..2 need a percentage, and this object has none */
    in.pActionPads = &aPads[0];
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);

    for (i32 m = 0; m < PUFO_ACTION_MODE_COUNT; ++m) {
        if (aPads[m].bEnabled != (u8)(ui.anActionEnabled[m] != 0)) { bMirror = false; }
        if (aPads[m].bEnabled != 0) { ++nEnabled; }
    }
    Check(bMirror, "  every pad's bEnabled byte matches its enabled entry, all twelve");
    CheckEqI32(nEnabled, 2, "  the two offered modes (3 and 11) are the two enabled pads");
    CheckEqI32(aPads[11].bEnabled, 1,
               "  mode 11's pad gets 1, not its enabled value 30 (MOV R2,#0 / MOVNE R2,#1)");
    CheckEqI32(ui.anActionEnabled[11], 30, "  the enabled entry itself keeps 30");

    /* The mirror is a write, not an OR: a refill for a weapon that offers nothing clears
     * pads the previous fill had enabled. */
    memset(aPads, 0, sizeof(aPads));
    for (i32 m = 0; m < PUFO_ACTION_MODE_COUNT; ++m) { aPads[m].bEnabled = 1; }
    in.auStat4 = 30;            /* modes 0..2 stay at 0 because the percentage bytes are 0 */
    in.nCategory = 0;
    in.auStat5 = 0;             /* mode 11's enabled value, so 11 is off too */
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    nEnabled = 0;
    for (i32 m = 0; m < PUFO_ACTION_MODE_COUNT; ++m) {
        if (aPads[m].bEnabled != 0) { ++nEnabled; }
    }
    CheckEqI32(nEnabled, 0, "  a refill disables pads the earlier fill had enabled");

    /* No bank to mirror into: the two tables still fill. The original always has the global. */
    in.pActionPads = NULL;
    in.uSortKey = 24;
    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[4], 1, "  the enabled table fills with no bank passed");
    CheckEqI32(ui.anActionCost[4], 34,
               "  and the cost table too (0x263F0/0x26400 precede the byte store)");
}


/* --- the hand action menus -------------------------------------------------
 *
 * The research gate is injected, so the test controls it with one flag. */
static bool g_bGateAllow;

static bool GateWeaponHasMenu(u16 uSortKey, void *pUser)
{
    (void)uSortKey;
    (void)pUser;
    return g_bGateAllow;
}

static void TestHandMenu(void)
{
    ActionCostInput in;
    HandMenuCtx     ctx;
    TactUiState     ui;

    printf("Ui_OpenHandMenu\n");

    /* The fixture is the one TestActionCosts uses: mode 0 is offered at 40 and
     * costs 200 * 60 / 100 = 120 TU. */
    memset(&in, 0, sizeof(in));
    in.auStat4 = 50;
    in.abCostPctPacked[0] = 80;
    in.abFlagByMode[0] = 60;
    in.auStat0 = 200;

    UiReset(&ui);
    Ui_FillActionCosts(&in, &ui);
    CheckEqI32(ui.anActionEnabled[0], 40, "  fixture: mode 0 is offered");
    CheckEqI32(ui.anActionCost[0], 120, "  fixture: mode 0 costs 120");

    g_bGateAllow = true;

    /* The affordable case: 120 TU is exactly what the unit has. */
    memset(&ctx, 0, sizeof(ctx));
    ctx.pCost = &in;
    ctx.uTu = 120;
    ctx.uSortKey = 1;
    UiReset(&ui);
    LogReset();
    Check(Ui_OpenHandMenu(&ui, &ctx, GateWeaponHasMenu, &g_hooks, NULL),
          "  an affordable action opens the menu");
    CheckEqI32(ui.nActionMode, 12, "  opens with nActionMode 12");
    CheckEqI32((i32)ui.nUiScreen, 3, "  opens with nUiScreen 3");
    CheckEqI32(g_log.nSamples, 1, "  plays one sample");
    CheckEqI32(g_log.nLastSample, 17, "  the sample is 17");
    CheckEqI32(g_log.nStatus, 0, "  and pushes no status message");

    /* One TU short: the scan finds nothing and the screen stays closed. */
    ctx.uTu = 119;
    UiReset(&ui);
    LogReset();
    Check(!Ui_OpenHandMenu(&ui, &ctx, GateWeaponHasMenu, &g_hooks, NULL),
          "  an unaffordable action refuses");
    CheckEqI32((i32)ui.nUiScreen, 0, "  refusing leaves the screen closed");
    CheckEqI32(ui.nActionMode, 0, "  and leaves nActionMode alone");
    CheckEqI32(g_log.nStatus, 1, "  refusing pushes one status message");
    CheckEqI32(g_log.nLastStatus, 1, "  the record is 1, not enough time");

    /* The action latch blocks the function before any cost is computed. */
    ctx.uTu = 120;
    ctx.bActionLatch = 3;
    UiReset(&ui);
    LogReset();
    Check(!Ui_OpenHandMenu(&ui, &ctx, GateWeaponHasMenu, &g_hooks, NULL),
          "  an armed action latch blocks the menu");
    CheckEqI32(g_log.nSamples, 1, "  the sample still plays");
    CheckEqI32(g_log.nStatus, 0, "  but no status message is pushed");
    CheckEqI32(ui.anActionCost[0], 10, "  and the cost table is left unfilled");
    ctx.bActionLatch = 0;

    /* The research gate. */
    g_bGateAllow = false;
    UiReset(&ui);
    LogReset();
    Check(!Ui_OpenHandMenu(&ui, &ctx, GateWeaponHasMenu, &g_hooks, NULL),
          "  a weapon whose research record is not cleared refuses");
    CheckEqI32(g_log.nStatus, 0, "  the gate refusal pushes nothing");
    g_bGateAllow = true;

    /* Sort key 24 needs the unit byte at Soldier+0x1A6. */
    ctx.uSortKey = 24;
    ctx.bByte1A6 = 0;
    UiReset(&ui);
    LogReset();
    Check(!Ui_OpenHandMenu(&ui, &ctx, GateWeaponHasMenu, &g_hooks, NULL),
          "  sort key 24 without the unit byte refuses");
    CheckEqI32(g_log.nStatus, 0, "  and pushes nothing");
    ctx.bByte1A6 = 1;
    UiReset(&ui);
    Check(Ui_OpenHandMenu(&ui, &ctx, GateWeaponHasMenu, &g_hooks, NULL),
          "  sort key 24 with the unit byte opens");
    ctx.uSortKey = 1;

    /* A missing gate is a refusal, not a crash. */
    UiReset(&ui);
    Check(!Ui_OpenHandMenu(&ui, &ctx, NULL, &g_hooks, NULL),
          "  a missing research gate refuses");

    /* Plenty of TU still opens; the scan is a yes/no, not a cheapest-mode search. */
    ctx.uTu = 4000;
    UiReset(&ui);
    LogReset();
    Check(Ui_OpenHandMenu(&ui, &ctx, GateWeaponHasMenu, &g_hooks, NULL),
          "  plenty of TU opens too");
    CheckEqI32(g_log.nStatus, 0, "  with no status message");
}

static void TestSubModes(void)
{
    TactUiState ui;
    TactItem   weapon;
    Soldier    unit;

    printf("Ui_SetSubMode -- writes bUnk8/bUnk7 and charges TU\n");
    for (i32 m = 0; m <= 8; ++m) {
        UiReset(&ui);
        memset(&weapon, 0, sizeof(weapon));
        memset(&unit, 0, sizeof(unit));
        ui.nActionMode = 4;          /* cost 14 from UiReset's table */
        ui.anActionCost[4] = 20;
        unit.uTu = 50;
        LogReset();
        Check(Ui_SetSubMode(&ui, &weapon, &unit, m, &g_hooks), "  sub-mode applied");
        CheckEqI32(weapon.bUnk8, 1, "  weapon marked sub-mode active");
        CheckEqI32(weapon.bUnk7, m, "  sub-mode index recorded");
        CheckEqI32(unit.uTu, 30, "  TU charged by the action mode's cost");
        CheckEqI32(g_log.nSamples, 1, "  played one sample");
    }

    /* The TU subtract wraps instead of clamping at zero. */
    UiReset(&ui);
    memset(&weapon, 0, sizeof(weapon));
    memset(&unit, 0, sizeof(unit));
    ui.nActionMode = 0;
    ui.anActionCost[0] = 100;
    unit.uTu = 5;
    (void)Ui_SetSubMode(&ui, &weapon, &unit, 1, &g_hooks);
    CheckEqI32(unit.uTu, 5 - 100 + 65536, "  TU wraps down rather than clamping");

    /* Out-of-range and NULL inputs are refused. */
    UiReset(&ui);
    Check(!Ui_SetSubMode(&ui, &weapon, &unit, 9, &g_hooks), "  sub-mode 9 refused");
    Check(!Ui_SetSubMode(&ui, &weapon, &unit, -1, &g_hooks), "  sub-mode -1 refused");
    Check(!Ui_SetSubMode(&ui, NULL, &unit, 1, &g_hooks), "  NULL weapon refused");
    Check(!Ui_SetSubMode(&ui, &weapon, NULL, 1, &g_hooks), "  NULL unit refused");
}

static void TestOptionToggles(void)
{
    TactUiState ui;

    printf("Ui_ToggleOption -- per-option coupling rules\n");

    /* SCROLL is a plain toggle, but turning it OFF also clears SHOW. */
    UiReset(&ui);
    Check(Ui_ToggleOption(&ui, TACT_TOGGLE_SCROLL, &g_hooks), "  scroll toggled on");
    CheckEqI32(ui.bScrollByPen, 1, "  scroll byte set");
    CheckEqI32(ui.abTogglePad[TACT_TOGGLE_SCROLL], 1, "  scroll pad lit");
    Check(Ui_ToggleOption(&ui, TACT_TOGGLE_SCROLL, &g_hooks), "  scroll toggled off");
    CheckEqI32(ui.bScrollByPen, 0, "  scroll byte cleared");

    UiReset(&ui);
    ui.bScrollByPen = 1;
    ui.bShowBorder = 1;
    ui.abTogglePad[TACT_TOGGLE_SHOW] = 1;
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_SCROLL, &g_hooks);
    CheckEqI32(ui.bScrollByPen, 0, "  scroll cleared");
    CheckEqI32(ui.bShowBorder, 0, "  turning scroll off also clears show");
    CheckEqI32(ui.abTogglePad[TACT_TOGGLE_SHOW], 0, "  show's pad cleared too");

    /* SHOW cannot be on unless SCROLL is on. It recomputes rather than flipping. */
    UiReset(&ui);
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_SHOW, &g_hooks);
    CheckEqI32(ui.bShowBorder, 0, "  show stays off while scroll is off");

    UiReset(&ui);
    ui.bScrollByPen = 1;
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_SHOW, &g_hooks);
    CheckEqI32(ui.bShowBorder, 1, "  show turns on when scroll is on");
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_SHOW, &g_hooks);
    CheckEqI32(ui.bShowBorder, 0, "  show turns back off");

    /* RUN is a plain toggle. */
    UiReset(&ui);
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_RUN, &g_hooks);
    CheckEqI32(ui.bRunByDblClick, 1, "  run toggled on");
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_RUN, &g_hooks);
    CheckEqI32(ui.bRunByDblClick, 0, "  run toggled off");

    /* TURN and MENU are mutually exclusive. */
    UiReset(&ui);
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_MENU, &g_hooks);
    CheckEqI32(ui.bMenuByPen, 1, "  menu on");
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_TURN, &g_hooks);
    CheckEqI32(ui.bTurnByPen, 1, "  turn on");
    CheckEqI32(ui.bMenuByPen, 0, "  turn clears menu");
    CheckEqI32(ui.abTogglePad[TACT_TOGGLE_MENU], 0, "  menu pad cleared");

    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_MENU, &g_hooks);
    CheckEqI32(ui.bMenuByPen, 1, "  menu back on");
    CheckEqI32(ui.bTurnByPen, 0, "  menu clears turn");

    /* Each toggle plays the sample. */
    UiReset(&ui);
    LogReset();
    (void)Ui_ToggleOption(&ui, TACT_TOGGLE_RUN, &g_hooks);
    CheckEqI32(g_log.nLastSample, 17, "  toggles play sample 17");

    UiReset(&ui);
    Check(!Ui_ToggleOption(&ui, 99, &g_hooks), "  unknown toggle refused");
}

static void TestScreenHelpers(void)
{
    TactUiState ui;

    printf("Screen helpers\n");
    UiReset(&ui);
    ui.nUiScreen = 9;
    Ui_ClearScreen(&ui);
    CheckEqI32(ui.nUiScreen, 0, "  ClearScreen zeroes the screen");

    UiReset(&ui);
    ui.nUiScreen = 9;
    LogReset();
    Ui_Samples16_14_ClearScreen(&ui, &g_hooks);
    CheckEqI32(ui.nUiScreen, 0, "  samples 16+14 then clear");
    CheckEqI32(g_log.nSamples, 2, "  two samples played");

    UiReset(&ui);
    LogReset();
    Ui_Screen7_Samples16_14(&ui, &g_hooks);
    CheckEqI32(ui.nUiScreen, 7, "  screen 7 paired with samples");

    UiReset(&ui);
    Ui_Samples16_7_SetDeployFlag(&ui, &g_hooks);
    CheckEqI32(ui.bDeployPanelOpen, 1, "  deploy flag set");
    CheckEqI32(ui.nUiScreen, 0, "  screen cleared");
    Ui_Samples16_7_ClearDeployFlag(&ui, &g_hooks);
    CheckEqI32(ui.bDeployPanelOpen, 0, "  deploy flag cleared");

    UiReset(&ui);
    ui.bUnk1508C = 5;
    Ui_OpenCommandScreenFlagged(&ui, &g_hooks);
    CheckEqI32(ui.nUiScreen, 4, "  flagged command screen is 4");
    CheckEqI32(ui.bSlot80Low, 1, "  the slot-8 low byte is set");
    CheckEqI32(ui.bUnk1508C, 5, "  flagged variant does NOT clear 0x1508C");

    UiReset(&ui);
    ui.nLayerScratch = 3;
    LogReset();
    Ui_IncLayer(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 4, "  IncLayer 3 -> 4");
    CheckEqI32(g_log.nCursorFlags, 0, "  IncLayer does NOT call SetCursorFlags");
    Ui_IncLayer(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 4, "  IncLayer refuses at 4");
    ui.nLayerScratch = 2;
    Ui_DecLayer(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 1, "  DecLayer 2 -> 1");
    Ui_DecLayer(&ui, &g_hooks);
    CheckEqI32(ui.nLayerScratch, 1, "  DecLayer refuses at 1");
}

/* --- action confirmations -------------------------------------------------- */

static void TestConfirmUseItem(void)
{
    TactUiState ui;
    Soldier    unit;
    TactItem   weapon;

    printf("Ui_ConfirmUseItem -- action mode 3\n");
    UiReset(&ui);
    ui.anActionCost[3] = 40;
    memset(&unit, 0, sizeof(unit));
    memset(&weapon, 0, sizeof(weapon));
    unit.uTu = 50;
    LogReset();

    /* Sort key 14 takes the instant-use path: TU is spent and the weapon marked. */
    CheckEqI32(Ui_ConfirmUseItem(&ui, &unit, &weapon, 14, &g_hooks), 0,
               "  sort key 14 -> screen 0");
    CheckEqI32(ui.nActionMode, 3, "  mode 3");
    CheckEqI32(unit.uTu, 10, "  the mode-3 cost was deducted");
    CheckEqI32(weapon.bUnk8, 1, "  the weapon is marked");
    CheckEqI32(g_log.nLastSample, 17, "  sample 17");

    /* Any other sort key opens screen 5 without spending TU. */
    UiReset(&ui);
    ui.anActionCost[3] = 40;
    memset(&unit, 0, sizeof(unit));
    memset(&weapon, 0, sizeof(weapon));
    unit.uTu = 50;
    CheckEqI32(Ui_ConfirmUseItem(&ui, &unit, &weapon, 20, &g_hooks), 5,
               "  other sort key -> screen 5");
    CheckEqI32(unit.uTu, 50, "  no TU spent on the sub-screen path");
    CheckEqI32(weapon.bUnk8, 0, "  weapon not marked yet");

    /* Exactly enough TU is accepted (the comparison allows equality). */
    UiReset(&ui);
    ui.anActionCost[3] = 40;
    memset(&unit, 0, sizeof(unit));
    unit.uTu = 40;
    CheckEqI32(Ui_ConfirmUseItem(&ui, &unit, &weapon, 20, &g_hooks), 5,
               "  exactly enough TU is accepted");

    /* One TU short: refused, mode falls back to 12, screen cleared. */
    UiReset(&ui);
    ui.anActionCost[3] = 40;
    memset(&unit, 0, sizeof(unit));
    unit.uTu = 39;
    ui.nUiScreen = 9;
    LogReset();
    CheckEqI32(Ui_ConfirmUseItem(&ui, &unit, &weapon, 14, &g_hooks), 12,
               "  too little TU -> mode 12");
    CheckEqI32(ui.nActionMode, 12, "  fallback mode stored");
    CheckEqI32(ui.nUiScreen, 0, "  screen cleared");
    CheckEqI32(unit.uTu, 39, "  TU untouched");
    CheckEqI32(weapon.bUnk8, 0, "  weapon untouched");
    CheckEqI32(g_log.nStatus, 1, "  the out-of-TU message was pushed");

    CheckEqI32(Ui_ConfirmUseItem(NULL, &unit, &weapon, 14, &g_hooks), -1,
               "  NULL state -> -1");
    CheckEqI32(Ui_ConfirmUseItem(&ui, NULL, &weapon, 14, &g_hooks), -1,
               "  NULL unit -> -1");
}

/* --- fire burst confirmation ---------------------------------------------- */

static i32 g_nFaceBurst;

static void BurstFace(Soldier *pUnit, i32 nX, i32 nY, void *pUser)
{
    (void)pUnit;
    (void)nX;
    (void)nY;
    (void)pUser;
    g_nFaceBurst += 1;
}

static void TestConfirmFireBurst(void)
{
    TactUiState ui;
    TactState   queue;
    Soldier     unit;
    TactItem    weapon;
    MapCell     aCells[2];
    MapCell    *apTargets[2];
    i32         nCount;

    printf("Ui_ConfirmFireBurst -- clears markers then enqueues\n");
    memset(aCells, 0, sizeof(aCells));
    MapCell_Init(&aCells[0], 7, 8, 1);
    MapCell_Init(&aCells[1], 9, 10, 2);
    aCells[0].bFlags29 = 0x40;
    aCells[1].bFlags29 = 0x40;
    apTargets[0] = &aCells[0];
    apTargets[1] = &aCells[1];
    nCount = 2;

    UiReset(&ui);
    ui.nActionMode = 1;          /* single-shot */
    ui.anActionCost[1] = 15;
    ui.anActionEnabled[1] = 1;
    TactState_ctor(&queue);
    memset(&unit, 0, sizeof(unit));
    memset(&weapon, 0, sizeof(weapon));
    unit.uTu = 50;
    g_nFaceBurst = 0;
    LogReset();

    Check(Ui_ConfirmFireBurst(&ui, &queue, &unit, &weapon, apTargets, &nCount,
                              10, 10, BurstFace, NULL, &g_hooks),
          "  burst confirmed");
    CheckEqI32(g_log.nLastSample, 16, "  sample 16");
    CheckEqI32(ui.bActionLatch, 0, "  latch cleared");
    CheckEqI32(nCount, 0, "  the multi-target count was zeroed");
    CheckEqI32((i32)(aCells[0].bFlags29 & 0x40), 0, "  marker cleared on cell 0");
    CheckEqI32((i32)(aCells[1].bFlags29 & 0x40), 0, "  marker cleared on cell 1");
    CheckEqI32(g_nFaceBurst, 1, "  faced the first target once");
    /* One shot -> 2 entries + clear-aim, and the queue was kicked. */
    CheckEqI32(queue.nQueueLen, 3, "  one shot queued");
    CheckEqI32(queue.aQueue[0].nCmd, TACTCMD_FIRE_SHOT, "  shot entry first");
    CheckEqI32(queue.aQueue[0].nX, 7, "  aimed at the FIRST target's cell");
    CheckEqI32(queue.aQueue[0].nY, 8, "  from the first target");
    CheckEqI32(queue.aQueue[0].nTuCost, 15, "  the mode's cost was charged");

    /* A zero target count is the original's NULL dereference; the port refuses but
     * still clears the latch and publishes the count. */
    UiReset(&ui);
    ui.nActionMode = 1;
    ui.anActionCost[1] = 15;
    TactState_ctor(&queue);
    unit.uTu = 50;
    nCount = 0;
    g_nFaceBurst = 0;
    Check(!Ui_ConfirmFireBurst(&ui, &queue, &unit, &weapon, apTargets, &nCount,
                               10, 10, BurstFace, NULL, &g_hooks),
          "  zero targets refused instead of dereferencing NULL");
    CheckEqI32(queue.nQueueLen, 0, "  nothing queued");
    CheckEqI32(g_nFaceBurst, 0, "  nothing faced");

    /* Not enough TU: the refusal is the CALLEE's own work in the original -- 0x4B0D8 returns
     * StatusMsg_Push(aTextRec1_OutOfTime, 0) -- and 0x1884C kicks the queue anyway. The queue is
     * pre-filled here so that "the kick ran" is observable: KickQueue only touches the cursor and the
     * active flag when the queue is non-empty. */
    UiReset(&ui);
    ui.nActionMode = 1;
    ui.anActionCost[1] = 99;
    TactState_ctor(&queue);
    queue.nQueueLen = 1;
    queue.nQueueCursor = 7;
    queue.bQueueActive = 0;
    unit.uTu = 5;
    nCount = 1;
    g_nFaceBurst = 0;
    LogReset();
    Check(!Ui_ConfirmFireBurst(&ui, &queue, &unit, &weapon, apTargets, &nCount,
                               10, 10, BurstFace, NULL, &g_hooks),
          "  insufficient TU refuses the enqueue");
    CheckEqI32(queue.nQueueLen, 1, "  no entry was added by the refused burst");
    CheckEqI32(g_nFaceBurst, 0, "  the unit was never faced -- the gate is before FaceToward (0x4B0D4)");
    CheckEqI32(g_log.nStatus, 1, "  one status message pushed");
    CheckEqI32(g_log.aStatus[0], PUFO_TXT_OUT_OF_TU, "  it is record 1, aTextRec1_OutOfTime");
    CheckEqI32(queue.bQueueActive, 1, "  and the queue was still kicked (0x1884C)");
    CheckEqI32(queue.nQueueCursor, 0, "  with the cursor reset");

    Check(!Ui_ConfirmFireBurst(NULL, &queue, &unit, &weapon, apTargets, &nCount,
                               10, 10, BurstFace, NULL, &g_hooks),
          "  NULL state refused");
    Check(!Ui_ConfirmFireBurst(&ui, NULL, &unit, &weapon, apTargets, &nCount,
                               10, 10, BurstFace, NULL, &g_hooks),
          "  NULL queue refused");
}

/* --- combat-menu item queries ---------------------------------------------- */

static void TestItemQueries(void)
{
    static const struct { i32 nId; i32 nSlot; i32 nDelta; } aCases[] = {
        /* id, hand slot, equip delta -- transcribed from the two decompilations. */
        { 14,  1, -1 },
        { 15, -1, -1 },
        { 16,  0, -1 },
        { 17, -1, -2 },
        { 18,  1, -1 },
        { 19,  2, -2 },
        { 36, -2, -3 },
        { 37, -1,  1 },
        { 38, -1, -3 },
        { 39,  1, -1 },
        /* Unlisted ids inside each range default differently, so pin both sides. */
        {  0,  0,  0 },
        { 13,  0,  0 },
        { 20,  0,  0 },
        { 35,  0,  0 },
        { 40,  0,  0 }
    };

    printf("TactItem_GetHandSlot / TactItem_GetEquipDelta\n");
    for (size_t i = 0; i < sizeof(aCases) / sizeof(aCases[0]); ++i) {
        char sz[64];

        snprintf(sz, sizeof(sz), "  id %d -> slot %d", (int)aCases[i].nId,
                 (int)aCases[i].nSlot);
        CheckEqI32(TactItem_GetHandSlot(aCases[i].nId), aCases[i].nSlot, sz);

        snprintf(sz, sizeof(sz), "  id %d -> delta %d", (int)aCases[i].nId,
                 (int)aCases[i].nDelta);
        CheckEqI32(TactItem_GetEquipDelta(aCases[i].nId), aCases[i].nDelta, sz);
    }

    /* The negative ids are outside the modelled space; both must default, not
     * crash. Action ids are unsigned in use, so these only guard the API. */
    CheckEqI32(TactItem_GetHandSlot(-1), 0, "  TactItem_GetHandSlot @ 0x3e620 maps a negative id to the right hand");
    CheckEqI32(TactItem_GetEquipDelta(-1), 0, "  TactItem_GetEquipDelta @ 0x3e69c maps a negative id to 0");

    /* Exhaustive agreement check: for every id in a wide range, the two functions
     * must return one of their declared values, never something else. This catches a
     * mis-transcribed branch without duplicating the table above. */
    {
        bool bAllKnownSlots = true;
        bool bAllKnownDeltas = true;

        for (i32 nId = 0; nId < 64; ++nId) {
            i32 nSlot = TactItem_GetHandSlot(nId);
            i32 nDelta = TactItem_GetEquipDelta(nId);

            if (nSlot != 0 && nSlot != 1 && nSlot != 2 && nSlot != -1 && nSlot != -2) {
                bAllKnownSlots = false;
            }
            if (nDelta != 0 && nDelta != -1 && nDelta != -2 && nDelta != -3
                && nDelta != 1) {
                bAllKnownDeltas = false;
            }
        }
        Check(bAllKnownSlots, "  every id yields a declared hand slot");
        Check(bAllKnownDeltas, "  every id yields a declared equip delta");
    }
}

/* --- TactAction_ConfirmSpecial9 / Tact_BeginMultiTarget -------------------- */

static i32 g_nCursorSets;
static i32 g_nCursorX;
static i32 g_nCursorY;

static void H_SetCursor(i32 nX, i32 nY, void *pUser)
{
    (void)pUser;
    g_nCursorSets += 1;
    g_nCursorX = nX;
    g_nCursorY = nY;
}

/* TactAction_ConfirmMeleeMode6 @ 0x17bb8. One layer-0 grid; the actor stands at (2,2,0)
 * facing east, so the candidate cell is (3,2,0). */
#define MELEE_DIM 8
static MapCell g_aMeleeGrid[MELEE_DIM][MELEE_DIM];
static Soldier g_aMeleeUnits[2];
static TactItem g_MeleeItem;
static Mc1Rec  g_MeleeWall;

static MapCell *MeleeLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nLayer != 0 || nX < 0 || nX >= MELEE_DIM || nY < 0 || nY >= MELEE_DIM) {
        return NULL;
    }
    return &g_aMeleeGrid[nY][nX];
}

static void MeleeReset(void)
{
    for (i32 y = 0; y < MELEE_DIM; ++y) {
        for (i32 x = 0; x < MELEE_DIM; ++x) {
            MapCell_Init(&g_aMeleeGrid[y][x], x, y, 0);
            g_aMeleeGrid[y][x].pUnit = NULL;
        }
    }
    memset(g_aMeleeUnits, 0, sizeof(g_aMeleeUnits));
    memset(&g_MeleeWall, 0, sizeof(g_MeleeWall));
    g_aMeleeUnits[0].pCell   = &g_aMeleeGrid[2][2];   /* the actor at (2,2,0) */
    g_aMeleeUnits[0].bFacing = 2;                      /* east: aDirDeltaXY[2] = (+1, 0) */
    g_aMeleeUnits[0].uTu     = 40;
    g_aMeleeUnits[1].bStatus = 0;
    g_aMeleeUnits[1].nUnitType = 0;                    /* small */
}

static void TestConfirmMeleeMode6(void)
{
    TactUiState ui;
    TactState   queue;
    MapGeom     geom = { 1, 1 };

    printf("Ui_ConfirmMeleeMode6 -- mode 6, the paralyzer attack\n");

    /* Accepted: the candidate cell's occupant becomes the target, the melee triple takes
     * the CANDIDATE coordinates, and command 18 is queued at the mode-6 cost. */
    MeleeReset();
    g_aMeleeGrid[2][3].pUnit = &g_aMeleeUnits[1];
    UiReset(&ui);
    TactState_ctor(&queue);
    LogReset();
    Check(Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], &g_MeleeItem,
                               &geom, MeleeLookup, NULL, &g_hooks),
          "  a clear step onto an occupied cell is accepted");
    CheckEqI32(g_log.nLastSample, 17, "  sample 17");
    CheckEqI32(ui.nActionMode, PUFO_ACTION_MODE_MELEE, "  mode 6 committed before any gate");
    CheckEqI32(ui.nFireTgtX, 3, "  melee x is the candidate cell, not the actor's");
    CheckEqI32(ui.nFireTgtY, 2, "  candidate y");
    CheckEqI32(ui.nFireTgtZ, 0, "  candidate layer");
    Check(ui.pMeleeTarget == &g_aMeleeUnits[1], "  Map_pMeleeTarget holds the occupant");
    CheckEqI32(queue.nQueueLen, 1, "  one command-18 entry");
    CheckEqI32(queue.aQueue[0].nCmd, TACTCMD_ACTION18, "  command 18");
    Check(queue.aQueue[0].pTarget == &g_aMeleeUnits[1], "  target is the parked unit");
    CheckEqI32(queue.aQueue[0].nMode, 6, "  the mode travels too");
    CheckEqI32(queue.aQueue[0].nTuCost, 16, "  anActionCost[6] was charged");
    CheckEqI32((i32)queue.bQueueActive, 1, "  the queue was kicked");
    CheckEqI32(g_log.nStatus, 0, "  no refusal message");

    /* Refusal 1: the candidate cell is empty. */
    MeleeReset();
    UiReset(&ui);
    ui.pMeleeTarget = &g_aMeleeUnits[1];
    TactState_ctor(&queue);
    LogReset();
    Check(!Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], NULL, &geom,
                                MeleeLookup, NULL, &g_hooks),
          "  an empty candidate cell is refused");
    CheckEqI32(g_log.nStatus, 2, "  the refusal is a two-line message");
    CheckEqI32(g_log.aStatus[0], PUFO_TXT_MELEE_NEED_TARGET, "  record 35");
    CheckEqI32(g_log.aStatus[1], PUFO_TXT_MELEE_NEED_TARGET_2, "  record 36");
    CheckEqI32(queue.nQueueLen, 0, "  nothing queued");
    Check(ui.pMeleeTarget == NULL, "  the scratch is overwritten with the candidate's NULL");

    /* Refusal 2: the occupant is large. The scratch is still written first (0x17C64). */
    MeleeReset();
    g_aMeleeGrid[2][3].pUnit = &g_aMeleeUnits[1];
    g_aMeleeUnits[1].nUnitType = 4;                    /* a large type */
    UiReset(&ui);
    TactState_ctor(&queue);
    LogReset();
    Check(!Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], NULL, &geom,
                                MeleeLookup, NULL, &g_hooks),
          "  a large occupant is refused");
    Check(ui.pMeleeTarget == &g_aMeleeUnits[1], "  the scratch was stored before the test");
    CheckEqI32(g_log.nStatus, 2, "  the same two-line refusal");

    /* Refusal 3: the wall probe says the step is blocked, so the candidate cell is never
     * even looked up and the scratch keeps its previous value. Unit_CanStepExitDir probes
     * from the ALREADY stepped-to coordinate, so for facing 2 its single wall-A probe
     * lands on (4,2,0) -- one past the candidate cell, not on it. */
    MeleeReset();
    g_MeleeWall.bTransparent = 0;      /* not a blank wall */
    g_MeleeWall.bBlocksMove  = 0;      /* the flag test passes, so the sprite decides */
    g_MeleeWall.gap_2f[0]    = 1;      /* and it is a real wall */
    g_aMeleeGrid[2][4].pWallSprA = &g_MeleeWall;
    g_aMeleeGrid[2][3].pUnit = &g_aMeleeUnits[1];
    UiReset(&ui);
    ui.pMeleeTarget = &g_aMeleeUnits[1];
    TactState_ctor(&queue);
    LogReset();
    Check(!Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], NULL, &geom,
                                MeleeLookup, NULL, &g_hooks),
          "  a blocked exit direction is refused");
    Check(ui.pMeleeTarget == &g_aMeleeUnits[1], "  gate 1 short-circuits before the lookup");
    CheckEqI32(g_log.nStatus, 2, "  the same two-line refusal");
    CheckEqI32(ui.nFireTgtX, 0, "  the melee triple is untouched");

    /* The enqueue's own TU gate: the function still reports success (its gates passed),
     * the triple is still written, and the shared out-of-TU record is pushed. */
    MeleeReset();
    g_aMeleeGrid[2][3].pUnit = &g_aMeleeUnits[1];
    g_aMeleeUnits[0].uTu = 3;
    UiReset(&ui);
    TactState_ctor(&queue);
    LogReset();
    Check(Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], NULL, &geom,
                               MeleeLookup, NULL, &g_hooks),
          "  the gates pass even when the unit cannot pay");
    CheckEqI32(ui.nFireTgtX, 3, "  the triple is still written");
    CheckEqI32(g_log.nStatus, 1, "  the out-of-TU message was pushed");
    CheckEqI32(g_log.nLastStatus, PUFO_TXT_OUT_OF_TU, "  record 1");
    CheckEqI32(queue.nQueueLen, 0, "  nothing queued");
    /* TactState_KickQueue @ 0x4f334 is a no-op on an empty queue -- it only resets the
     * cursor and the active flag when nQueueLen is non-zero. */
    CheckEqI32((i32)queue.bQueueActive, 0, "  the kick runs but does nothing on an empty queue");

    MeleeReset();
    UiReset(&ui);
    TactState_ctor(&queue);
    Check(!Ui_ConfirmMeleeMode6(NULL, &queue, &g_aMeleeUnits[0], NULL, &geom,
                                MeleeLookup, NULL, &g_hooks),
          "  NULL state refused");
    Check(!Ui_ConfirmMeleeMode6(&ui, NULL, &g_aMeleeUnits[0], NULL, &geom,
                                MeleeLookup, NULL, &g_hooks),
          "  NULL queue refused");
    Check(!Ui_ConfirmMeleeMode6(&ui, &queue, NULL, NULL, &geom,
                                MeleeLookup, NULL, &g_hooks),
          "  NULL unit refused");
    Check(!Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], NULL, NULL,
                                MeleeLookup, NULL, &g_hooks),
          "  NULL geometry refused");
    Check(!Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], NULL, &geom,
                                NULL, NULL, &g_hooks),
          "  NULL lookup refused");
    g_aMeleeUnits[0].pCell = NULL;
    Check(!Ui_ConfirmMeleeMode6(&ui, &queue, &g_aMeleeUnits[0], NULL, &geom,
                                MeleeLookup, NULL, &g_hooks),
          "  a unit with no cell is refused instead of faulting");
}

static void TestConfirmSpecial9(void)
{
    TactUiState ui;
    TactState   queue;
    Soldier     unit;
    TactItem    weapon;
    MapCell     cell;

    printf("Ui_ConfirmSpecial9 -- mode 9 refusal guard\n");
    UiReset(&ui);
    memset(&unit, 0, sizeof(unit));
    memset(&weapon, 0, sizeof(weapon));
    MapCell_Init(&cell, 12, 34, 2);

    /* The guard needs ALL FOUR: not kneel/primed, full HP, full morale,
     * and spent TU. Set them all. */
    unit.bKneelOrPrimed = 0;
    unit.auStat[1] = 50;
    unit.uHp = 50;
    unit.bMorale = 100;
    unit.uTuSpent = 5;
    ui.nUiScreen = 9;
    TactState_ctor(&queue);
    LogReset();
    Check(!Ui_ConfirmSpecial9(&ui, &queue, &unit, &weapon, &cell, &g_hooks),
          "  full HP + morale + spent TU is refused");
    CheckEqI32(ui.nActionMode, 9, "  mode 9 still set");
    CheckEqI32(ui.nUiScreen, 0, "  screen cleared");
    CheckEqI32(g_log.nStatus, 2, "  TWO status messages pushed");
    CheckEqI32(g_log.nLastSample, 17, "  sample 17");
    /* 0x182D8 / 0x182F0 name unk_B419F and unk_B426C: records 11 and 12, NOT the
     * out-of-ammo record. The first push is 11, the second 12. */
    CheckEqI32(g_log.aStatus[0], PUFO_TXT_MEDKIT_NOT_NEEDED, "  first record is 11");
    CheckEqI32(g_log.aStatus[1], PUFO_TXT_MEDKIT_NOT_NEEDED_2, "  second record is 12");
    CheckEqI32(queue.nQueueLen, 0, "  the refusal queues nothing");

    /* Break each condition in turn: any one is enough to allow the action. */
    #define SPECIAL9_CASE(label, setup)                                          \
        do {                                                                     \
            unit.bKneelOrPrimed = 0;                                             \
            unit.uHp = 50;                                                       \
            unit.auStat[1] = 50;                                                 \
            unit.bMorale = 100;                                                  \
            unit.uTuSpent = 5;                                                   \
            unit.uTu = 40;                                                       \
            setup;                                                               \
            UiReset(&ui);                                                        \
            ui.nUiScreen = 9;                                                    \
            ui.anActionCost[9] = 25;                                             \
            TactState_ctor(&queue);                                              \
            LogReset();                                                          \
            Check(Ui_ConfirmSpecial9(&ui, &queue, &unit, &weapon, &cell,          \
                                     &g_hooks),                                  \
                  label);                                                        \
            CheckEqI32(ui.nFireTgtX, 12, "  target x is the unit's own cell");    \
            CheckEqI32(ui.nFireTgtY, 34, "  target y is the unit's own cell");    \
            CheckEqI32(ui.nFireTgtZ, 2, "  target z is the unit's own cell");     \
            CheckEqI32(g_log.nStatus, 0, "  no status messages");                 \
            /* 0x18364 enqueues cmd 18 with the unit as BOTH actor and target,     \
             * at the mode-9 cost, and 0x18370 kicks the queue. */                 \
            CheckEqI32(queue.nQueueLen, 1, "  one command-18 entry queued");      \
            CheckEqI32(queue.aQueue[0].nCmd, TACTCMD_ACTION18, "  command 18");   \
            Check(queue.aQueue[0].pUnit == &unit, "  actor is the selected unit"); \
            Check(queue.aQueue[0].pTarget == &unit, "  target is the SAME unit");  \
            CheckEqI32(queue.aQueue[0].nTuCost, 25, "  charged the mode-9 cost");  \
            CheckEqI32((i32)queue.bQueueActive, 1, "  the queue was kicked");      \
        } while (0)

    SPECIAL9_CASE("  kneel/primed breaks the guard", unit.bKneelOrPrimed = 1);
    SPECIAL9_CASE("  wounded breaks the guard", unit.uHp = 49);
    SPECIAL9_CASE("  low morale breaks the guard", unit.bMorale = 99);
    SPECIAL9_CASE("  no TU spent breaks the guard", unit.uTuSpent = 0);

    #undef SPECIAL9_CASE

    /* The enqueue's own TU gate still applies: too little TU pushes the shared
     * "no free time" record 1 and queues nothing, but the kick still runs. */
    unit.bKneelOrPrimed = 1;
    unit.uTu = 5;
    UiReset(&ui);
    ui.anActionCost[9] = 25;
    TactState_ctor(&queue);
    LogReset();
    Check(Ui_ConfirmSpecial9(&ui, &queue, &unit, &weapon, &cell, &g_hooks),
          "  the guard passed but the enqueue could not pay");
    CheckEqI32(g_log.nStatus, 1, "  the out-of-TU message was pushed");
    CheckEqI32(g_log.nLastStatus, PUFO_TXT_OUT_OF_TU, "  the record is 1");
    CheckEqI32(queue.nQueueLen, 0, "  nothing queued");

    /* The target triple is only written on the accepted path. Re-arm all four guard
     * conditions -- the previous case left the unit kneeling. */
    unit.bKneelOrPrimed = 0;
    unit.uHp = 50;
    unit.auStat[1] = 50;
    unit.bMorale = 100;
    unit.uTuSpent = 5;
    unit.uTu = 40;
    UiReset(&ui);
    ui.nFireTgtX = 777;
    TactState_ctor(&queue);
    (void)Ui_ConfirmSpecial9(&ui, &queue, &unit, &weapon, &cell, &g_hooks);
    CheckEqI32(ui.nFireTgtX, 777, "  refused path does not touch the target");

    TactState_ctor(&queue);
    Check(!Ui_ConfirmSpecial9(NULL, &queue, &unit, &weapon, &cell, &g_hooks),
          "  NULL state refused");
    Check(!Ui_ConfirmSpecial9(&ui, NULL, &unit, &weapon, &cell, &g_hooks),
          "  NULL queue refused");
    Check(!Ui_ConfirmSpecial9(&ui, &queue, NULL, &weapon, &cell, &g_hooks),
          "  NULL unit refused");
    Check(!Ui_ConfirmSpecial9(&ui, &queue, &unit, &weapon, NULL, &g_hooks),
          "  NULL cell refused");
}

static void TestBeginMultiTarget(void)
{
    TactUiState    ui;
    FireModeWeapon w;
    i32            nCount;

    printf("Ui_BeginMultiTarget -- mode 7 and latch 3\n");
    UiReset(&ui);
    memset(&w, 0, sizeof(w));
    w.nAmmo = 5;
    w.bAmmoCap = 10;
    nCount = 99;
    g_nCursorSets = 0;
    LogReset();

    CheckEqI32(Ui_BeginMultiTarget(&ui, &w, &nCount, 7, 8, H_SetCursor, NULL,
                                   &g_hooks),
               PUFO_ACTION_LATCH_TARGETING, "  returns latch 3");
    CheckEqI32(ui.nActionMode, 7, "  mode 7 set");
    CheckEqI32(ui.bActionLatch, 3, "  latch is 3, NOT 4");
    CheckEqI32(nCount, 0, "  the multi-target count was zeroed");
    CheckEqI32(g_nCursorSets, 1, "  the cursor was parked");
    CheckEqI32(g_nCursorX, 7, "  at the passed x");
    CheckEqI32(g_nCursorY, 8, "  at the passed y");
    CheckEqI32(g_log.nLastSample, 17, "  sample 17");

    /* Empty weapon with a real capacity: refused with the out-of-ammo message. */
    UiReset(&ui);
    w.nAmmo = 0;
    w.bAmmoCap = 10;
    LogReset();
    g_nCursorSets = 0;
    CheckEqI32(Ui_BeginMultiTarget(&ui, &w, &nCount, 7, 8, H_SetCursor, NULL,
                                   &g_hooks),
               -1, "  empty weapon refused");
    CheckEqI32(g_log.nStatus, 1, "  the out-of-ammo message was pushed");
    CheckEqI32(g_nCursorSets, 0, "  the cursor was NOT parked on refusal");

    /* Capacity 255 means unlimited, so an empty weapon is allowed. */
    UiReset(&ui);
    w.bAmmoCap = 255;
    Check(Ui_BeginMultiTarget(&ui, &w, &nCount, 1, 2, H_SetCursor, NULL, &g_hooks)
              == PUFO_ACTION_LATCH_TARGETING,
          "  unlimited weapon allowed while empty");

    CheckEqI32(Ui_BeginMultiTarget(NULL, &w, &nCount, 1, 2, H_SetCursor, NULL,
                                   &g_hooks),
               -1, "  NULL state -> -1");
    CheckEqI32(Ui_BeginMultiTarget(&ui, &w, NULL, 1, 2, H_SetCursor, NULL, &g_hooks),
               -1, "  NULL count -> -1");
    CheckEqI32(Ui_BeginMultiTarget(&ui, NULL, &nCount, 1, 2, H_SetCursor, NULL,
                                   &g_hooks),
               -1, "  NULL weapon -> -1");

    /* The two latch values are distinct: 3 means targeting, 4 means fire. */
    CheckEqI32(PUFO_ACTION_LATCH_TARGETING, 3, "  targeting latch is 3");
    CheckEqI32(PUFO_ACTION_LATCH_FIRE, 4, "  fire latch is 4 (distinct)");
}

/* --- Tact_MapTypeToGlyphIdx @ 0x46664 -------------------------------------- */

static void TestMapTypeToGlyphIdx(void)
{
    /* The four-byte probe table the original reads at gap_86272[22..25]. */
    static const u8 k_aProbe[4] = { 10, 11, 12, 13 };

    printf("Tact_MapTypeToGlyphIdx\n");

    /* types 0..3 -> type + 5 */
    for (i32 t = 0; t <= 3; ++t) {
        CheckEqI32(Tact_MapTypeToGlyphIdx((u8)t, k_aProbe, 4), t + 5,
                   "  type 0..3 -> type + 5");
    }

    /* types 4..8 -> 18 */
    for (i32 t = 4; t <= 8; ++t) {
        CheckEqI32(Tact_MapTypeToGlyphIdx((u8)t, k_aProbe, 4), 18,
                   "  type 4..8 -> 18");
    }

    /* The two specials, checked BEFORE the probe despite being above 9. */
    CheckEqI32(Tact_MapTypeToGlyphIdx(16, k_aProbe, 4), 14, "  type 16 -> 14");
    CheckEqI32(Tact_MapTypeToGlyphIdx(17, k_aProbe, 4), 13, "  type 17 -> 13");

    /* The probe: a match at index i gives i + 9. These types are >= 9 so they reach
     * the probe. */
    CheckEqI32(Tact_MapTypeToGlyphIdx(10, k_aProbe, 4), 9, "  probe hit at index 0 -> 9");
    CheckEqI32(Tact_MapTypeToGlyphIdx(12, k_aProbe, 4), 11, "  probe hit at index 2 -> 11");
    CheckEqI32(Tact_MapTypeToGlyphIdx(13, k_aProbe, 4), 12, "  probe hit at index 3 -> 12");

    /* No match falls back to type + 5. */
    CheckEqI32(Tact_MapTypeToGlyphIdx(20, k_aProbe, 4), 25, "  no probe match -> type + 5");
    CheckEqI32(Tact_MapTypeToGlyphIdx(255, k_aProbe, 4), 260,
               "  type 255 -> 260 (the +5 is not truncated to a u8)");

    /* A duplicate probe entry resolves to the FIRST match, because the scan stops. */
    {
        static const u8 k_aDup[4] = { 10, 10, 10, 10 };

        CheckEqI32(Tact_MapTypeToGlyphIdx(10, k_aDup, 4), 9,
                   "  duplicates resolve to the first index");
    }

    /* A NULL or empty probe table skips the scan and falls through. */
    CheckEqI32(Tact_MapTypeToGlyphIdx(20, NULL, 4), 25, "  NULL probe table -> fallback");
    CheckEqI32(Tact_MapTypeToGlyphIdx(20, k_aProbe, 0), 25, "  zero-length probe -> fallback");

    CheckEqI32(PUFO_GLYPH_PROBE_COUNT, 4, "  the probe table has four entries");
}

static void TestTryReloadClip(void)
{
    TactItem item;
    Soldier owner;
    u8 nClipCap = 10;

    memset(&item, 0, sizeof(item));
    memset(&owner, 0, sizeof(owner));

    /* Empty weapon with a real cap and a spare clip: one clip is consumed and
     * the magazine is refilled from the clip object's cap. */
    item.nAmmo = 0;
    owner.bHumanoidInv = 2;
    Check(TactItem_TryReloadClip(&item, 12, 7, &owner, ClipCap, &nClipCap),
          "reload succeeds from a spare clip");
    CheckEqI32(item.nAmmo, 10, "magazine refilled from the clip cap");
    CheckEqI32(owner.bHumanoidInv, 1, "one clip consumed");

    /* No spare clip: the weapon stays empty and is not loaded. */
    item.nAmmo = 0;
    owner.bHumanoidInv = 0;
    Check(!TactItem_TryReloadClip(&item, 12, 7, &owner, ClipCap, &nClipCap),
          "no clip means no reload");
    CheckEqI32(item.nAmmo, 0, "magazine stays empty");

    /* The clip object may refill to zero; then the weapon is still empty. */
    nClipCap = 0;
    item.nAmmo = 0;
    owner.bHumanoidInv = 1;
    Check(!TactItem_TryReloadClip(&item, 12, 7, &owner, ClipCap, &nClipCap),
          "an empty clip refills nothing");
    CheckEqI32(owner.bHumanoidInv, 0, "the empty clip was still consumed");

    /* An unlimited weapon (cap 255) always reports loaded, even empty. */
    item.nAmmo = 0;
    Check(TactItem_TryReloadClip(&item, 255, 7, &owner, ClipCap, &nClipCap),
          "unlimited weapon always loaded");
    CheckEqI32(owner.bHumanoidInv, 0, "unlimited weapon consumes no clip");

    /* A non-empty weapon is loaded without touching the clip count. */
    item.nAmmo = 3;
    owner.bHumanoidInv = 5;
    Check(TactItem_TryReloadClip(&item, 12, 7, &owner, ClipCap, &nClipCap),
          "loaded weapon reports loaded");
    CheckEqI32(owner.bHumanoidInv, 5, "loaded weapon consumes no clip");

    Check(!TactItem_TryReloadClip(NULL, 12, 7, &owner, ClipCap, &nClipCap),
          "NULL item refused");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestFilterMapping();    TestScroll();
    TestBuildVisibleList();
    TestRadio();
    TestScreensAndLayers();
    TestFireModes();
    TestTryFireMode();
    TestActionCosts();
    TestActionCostBankMirror();
    TestHandMenu();
    TestSubModes();
    TestOptionToggles();
    TestScreenHelpers();
    TestConfirmUseItem();
    TestConfirmFireBurst();
    TestItemQueries();
    TestConfirmMeleeMode6();
    TestConfirmSpecial9();
    TestBeginMultiTarget();
    TestMapTypeToGlyphIdx();
    TestTryReloadClip();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
