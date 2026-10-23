#!/usr/bin/env python3
"""Part 133: one mutation for each of the 24 strat-ui rows that got a test cite.

Every anchor is a byte string that occurs exactly once in its file -- the address comment on the line is what
makes the otherwise repeated store (`pPanelD->anLineAux[nRow] += 1`, `pbPanelsBuilt = 1`) address-specific. Two
rows share one port body with a twin the port already has (`BuySellScreen_SyncSelection` / `Transfer_CacheSelIndex`
forward to the same cache, and `BuySell_RoleToItemId` / `Transfer_RoleToIconId` are one switch), so those anchors
carry the function signature to land on the right body.

Run from the repo root:  python -I docs/parity/mutations_part133.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

BASE = "src/pufo/game/pufo_baseui.c"
PLANE = "src/pufo/game/pufo_plane.c"

# ---- BaseInfoScreen_OnOk @ 0x557c8: the arming byte at 0x557D0 ----
run_case("0x557C8 OnOk drops the arming byte", "test_baseui", BASE,
         b"pState->bPsiPending = 1;", b"pState->bPsiPending = 0;")

# ---- PsiScreen_OnOk @ 0x716a8: the same eight instructions, its own body ----
run_case("0x716A8 OnOk returns one past the pending base", "test_baseui", BASE,
         b"    return BaseInfoScreen_OnOk(pState);",
         b"    return BaseInfoScreen_OnOk(pState) + 1;")

# ---- ManufScreen_SyncSelection @ 0x6dd24: the store into anLineAux[147] at 0x6DD3C ----
run_case("0x6DD24 stores 0 instead of the selection", "test_baseui", BASE,
         b"*pnLineAux147 = nSel0BD8; /* 0x6DD3C */", b"*pnLineAux147 = 0; /* 0x6DD3C */")

# ---- BuySellScreen_SyncSelection @ 0x76138 and Transfer_CacheSelIndex @ 0x96ab0: one shared cache,
#      two bodies. The signature makes each anchor land on its own function. ----
run_case("0x76138 caches the selection off by one", "test_baseui", BASE,
         b"i32 BuySellScreen_SyncSelection(ScreenScratch *pScratch, i32 nSel0BD8)\n"
         b"{\n    return BaseUi_Cache186BE8(pScratch, nSel0BD8);",
         b"i32 BuySellScreen_SyncSelection(ScreenScratch *pScratch, i32 nSel0BD8)\n"
         b"{\n    return BaseUi_Cache186BE8(pScratch, nSel0BD8 + 1);")

run_case("0x96AB0 caches the selection off by one", "test_baseui", BASE,
         b"i32 Transfer_CacheSelIndex(ScreenScratch *pScratch, i32 nSel0BD8)\n"
         b"{\n    return BaseUi_Cache186BE8(pScratch, nSel0BD8);",
         b"i32 Transfer_CacheSelIndex(ScreenScratch *pScratch, i32 nSel0BD8)\n"
         b"{\n    return BaseUi_Cache186BE8(pScratch, nSel0BD8 + 1);")

# ---- Transfer_RoleToIconId @ 0x96ad8 (one switch with BuySell_RoleToItemId @ 0x76d88): role 3's id ----
run_case("0x96AD8 role 3 gets the wrong item id", "test_baseui", BASE,
         b"case 3: return 118;", b"case 3: return 119;")

# ---- BaseAtScreen_Open @ 0x588cc: y built as ORR #2 at 0x588F4 ----
run_case("0x588CC confirm control lands on the wrong row", "test_baseui", BASE,
         b"pCtl->y = (i16)(0x120 | 2);", b"pCtl->y = (i16)(0x120 | 3);")

# ---- BaseAtScreen_Update @ 0x57808: a cell still under construction is not a defence (days byte, 0x57930
#      side of the scan). The budget burn at 0x57CE4 was the first anchor here and it is INERT: every fixture in
#      TestBaseAtUpdate rolls a miss, so no damage is ever applied -- the record has no check on the burn. ----
run_case("0x57808 a nine-day cell counts as a defence", "test_baseui", BASE,
         b"if (pDays[nCell] != 0) {", b"if (pDays[nCell] > 9) {")

# ---- BaseAtScreen_Draw @ 0x58728: the centring at 0x587EC ----
run_case("0x58728 confirm control is not centred", "test_baseui", BASE,
         b"pEnv->pCtlConfirm->x = (i16)(PUFO_BASEAT_CONFIRM_X - (pEnv->pCtlConfirm->uWidth >> 1));",
         b"pEnv->pCtlConfirm->x = (i16)PUFO_BASEAT_CONFIRM_X;")

# ---- ManufScreen_BuildPanels @ 0x6e9e8: column C keeps the TOPIC index at 0x6EF70 ----
run_case("0x6E9E8 column C keeps the topic index off by one", "test_baseui", BASE,
         b"UiPanel_SetLineAux(pC, nRow, nTopic);", b"UiPanel_SetLineAux(pC, nRow, nTopic + 1);")

# ---- ManufScreen_Init @ 0x710b0: the close control at 0x71118 ----
run_case("0x710B0 close control moves one pixel", "test_baseui", BASE,
         b"pEnv->pCloseCtl->x = 224; /* 0x71118 */", b"pEnv->pCloseCtl->x = 223; /* 0x71118 */")

# ---- PsiScreen_BuildPanels @ 0x716d4: the teardown leaves the built flag at 0 at 0x72134 ----
run_case("0x716D4 teardown leaves the built flag up", "test_baseui", BASE,
         b"*pEnv->pbBuilt = 0;                       /* 0x72134 */",
         b"*pEnv->pbBuilt = 1;                       /* 0x72134 */")

# ---- PsiScreen_SelectListed @ 0x71298: the scratch row at 0x712E0 ----
run_case("0x71298 scratch row is off by one", "test_baseui", BASE,
         b"*pEnv->pnScratchRow = nRow;", b"*pEnv->pnScratchRow = nRow + 1;")

# ---- PsiScreen_ApplyAssignments @ 0x72250: the assignment byte at 0x722E8 ----
run_case("0x72250 the marked soldier is never assigned", "test_baseui", BASE,
         b"pS->bPsiAssigned = 1;", b"pS->bPsiAssigned = 0;")

# ---- PsiScreen_Init @ 0x72b40: the OK control at 0x72BAC ----
run_case("0x72B40 OK control moves one pixel", "test_baseui", BASE,
         b"pEnv->pCtlOk->y = 300;", b"pEnv->pCtlOk->y = 301;")

# ---- TransferScreen_BuildPanels @ 0x96b30 teardown flag (0x97994) vs build flag (0x96B60) ----
run_case("0x96B30 teardown leaves the panels-built flag down", "test_baseui", BASE,
         b"*pEnv->pbPanelsBuilt = 0; /* 0x97994 */", b"*pEnv->pbPanelsBuilt = 1; /* 0x97994 */")

run_case("0x96B30 build never sets the panels-built flag", "test_baseui", BASE,
         b"*pEnv->pbPanelsBuilt = 1; /* 0x96B60 */", b"*pEnv->pbPanelsBuilt = 0; /* 0x96B60 */")

# ---- BuySellScreen_BuildPanels @ 0x76de0: column A's window width at 0x7720C ----
run_case("0x76DE0 column A is one pixel narrower", "test_baseui", BASE,
         b"pA->nViewW = 125;                           /* 0x7720C */",
         b"pA->nViewW = 124;                           /* 0x7720C */")

# ---- BuySellScreen_FillBuyPanel @ 0x75160: the buy column clears its mark at 0x752D8 ----
run_case("0x75160 the row keeps its buy mark", "test_baseui", BASE,
         b"pEnv->pPanelE->anLineAux[nRow] = 0;   /* 0x752D8 */",
         b"pEnv->pPanelE->anLineAux[nRow] = 1;   /* 0x752D8 */")

# ---- BuySellScreen_UpdatePrices @ 0x75878: the refund at 0x75B38 / 0x75ADC ----
run_case("0x75878 the refund is one credit short", "test_baseui", BASE,
         b"*pEnv->pnFunds += nPrice; /* 0x75B38, 0x75ADC */", b"*pEnv->pnFunds += nPrice + 1; /* 0x75B38, 0x75ADC */")

# ---- BuySellScreen_BuildControls @ 0x76160 teardown: the working site's own flags, 0x76C30.
#      The item-quantity memset at 0x76C18 was the first anchor here and it is INERT: shortening it by two
#      bytes only leaves the last slot of auItemQty uncleared and no check reads that slot. ----
run_case("0x76160 the teardown leaves the site flag up", "test_baseui", BASE,
         b"pSite->bFlag0 = 0;                                       /* 0x76C30 */",
         b"pSite->bFlag0 = 1;                                       /* 0x76C30 */")

# ---- BasePlaneScreen_ApplyWeaponSwap @ 0x79748: the cargo removal at 0x79838 ----
run_case("0x79748 takes two of the old weapon out of the craft", "test_plane", PLANE,
         b"InvSel_SubItemQty(pCraft, pOld, 1);              /* 0x79838 */",
         b"InvSel_SubItemQty(pCraft, pOld, 2);              /* 0x79838 */")

# ---- Transfer_ResetSite @ 0x9690c: the ok flag at 0x9691C ----
run_case("0x9690C reset does not arm the transfer", "test_baseui", BASE,
         b"*pEnv->pbTransferOk = 1; /* 0x9691C */", b"*pEnv->pbTransferOk = 0; /* 0x9691C */")

# ---- TransferScreen_ValidateCapacity @ 0x96288: the radar-pulse limit at 0x962F0 ----
run_case("0x96288 the radar-pulse limit doubles", "test_baseui", BASE,
         b"nLimit = *pEnv->pnRadarPulseTimer / 10; /* 0x962F0, signed division */",
         b"nLimit = *pEnv->pnRadarPulseTimer / 5; /* 0x962F0, signed division */")

# ---- Transfer_MoveOneItem @ 0x9602c: the increment column store at 0x961F4 ----
run_case("0x9602C two units go into the increment column", "test_baseui", BASE,
         b"pEnv->pPanelD->anLineAux[nRow] += 1;  /* 0x961F4 */",
         b"pEnv->pPanelD->anLineAux[nRow] += 2;  /* 0x961F4 */")
