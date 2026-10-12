/* pufo_baseui.h - base-screen scratch-cell helpers.
 *
 * Evidence: Set_byte_159E88_4 @ 0x6b6b0,
 *           Set_byte_159E88_3_SetUiY @ 0x6b674,
 *           Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec,
 *           PlaySample7_SetFlags_Cache0xBD8 @ 0x73328.
 *
 * The original writes scattered globals: `gUiRootPanel.anLineAux[145]`, two
 * controls' `y` fields (`dword_186BB0`/`dword_186BB4`), and two cached
 * `nUnk3032` copies (`dword_186BE8` and `unk_160C74`) plus the `unk_160C80` /
 * `byte_187080` flags. The port carries them in a ScreenScratch so the helpers
 * are testable without the global bank. The `nUnk3032` source values differ per
 * caller and are passed in.
 */
#ifndef PUFO_BASEUI_H
#define PUFO_BASEUI_H

#include "pufo_common.h"
#include "ui/pufo_panel.h"   /* UiPanel, the list these handlers page through */

typedef struct ScreenScratch {
    u8  nLineAux145;   /* gUiRootPanel.anLineAux[145] low byte (0x159E88+145) */
    i32 nControl0Y;    /* dword_186BB0->y */
    i32 nControl1Y;    /* dword_186BB4->y */
    i32 nCache186BE8;  /* dword_186BE8 */
    bool bFlag160C80;  /* unk_160C80 */
    u8  bFlag187080;   /* byte_187080 */
    i32 nCache160C74;  /* unk_160C74 */
} ScreenScratch;

/* Set_byte_159E88_4 @ 0x6b6b0: sets the panel scratch byte to 4. */
void BaseUi_SetScratch4(ScreenScratch *pScratch);

/* Set_byte_159E88_3_SetUiY @ 0x6b674: sets the panel scratch byte to 3 and the
 * two controls' y to 120 and 142. */
void BaseUi_SetScratch3AndUiY(ScreenScratch *pScratch);

/* Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec: copies the source object's
 * nUnk3032 into dword_186BE8 and returns it. */
i32 BaseUi_Cache186BE8(ScreenScratch *pScratch, i32 nUnk3032);

/* PlaySample7_SetFlags_Cache0xBD8 @ 0x73328: plays sample 7, sets unk_160C80 to 1
 * and byte_187080 to 0, then caches the source object's nUnk3032 into unk_160C74
 * and returns it. */
i32 BaseUi_PlaySample7Cache160C74(ScreenScratch *pScratch, i32 nUnk3032,
                                  void (*pfnPlaySample)(u32 nSample, void *pUser),
                                  void *pUser);

/* Geoscape screen openers. Evidence: Ui_OpenScreen18 @ 0x84e9c,
 * Ui_CloseScreen18 @ 0x84ecc, Ui_OpenFunding @ 0x85164.
 *
 * Each plays sample 17, sets the geoscape screen id, then calls the matching
 * screen function. `Ui_CloseScreen18` sets the SAME id (18) as the open function
 * and passes the panel scratch value to the options screen. */
typedef struct GeoscapeScreenState {
    i32 nGeoscapeScreen;   /* nGeoscapeScreen */
    i32 nLineAux119;       /* gUiRootPanel.anLineAux[119], the options-screen arg */
} GeoscapeScreenState;

typedef struct GeoscapeHooks {
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void (*pfnOptionsRebuildWithSound)(void *pUser); /* OptionsScreen_RebuildWithSound */
    void (*pfnOptionsOpenWithSound)(i32 nArg, void *pUser); /* OptionsScreen_OpenWithSound */
    void (*pfnFundingOpen)(void *pUser);             /* FundingScreen_Open */
    void *pUser;
} GeoscapeHooks;

void Ui_OpenScreen18(GeoscapeScreenState *pState, const GeoscapeHooks *pHooks);
void Ui_CloseScreen18(GeoscapeScreenState *pState, const GeoscapeHooks *pHooks);
void Ui_OpenFunding(GeoscapeScreenState *pState, const GeoscapeHooks *pHooks);

/* ------------------------------------------------- strategic screen list paging
 *
 * Four screen handlers that are one-liners over the scroll movers already ported in
 * pufo_panel.c (`UiPanel_ScrollNext` = IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c,
 * `UiPanel_ScrollPrev` = DecDwordIfPos_AtPlus0xBD4 @ 0x3f040). They differ only in
 * which panel they point at:
 *
 *   BaseFacScreen_PanelPrev @ 0x589cc / PanelNext @ 0x589dc
 *       the single panel the original keeps the pointer to at unk_15886C.
 *   SoldierScreen_PanelPrev @ 0x61874 / PanelNext @ 0x6189c
 *       TWO panels, the pointers held in gUiRootPanel.apLines[186] and
 *       gUiRootPanel.anLineAux[106]. Both are moved, in that order, and the second
 *       call is the one the original returns.
 *
 * The port takes the panels as arguments instead of reaching through the globals. */
void BaseFacScreen_PanelPrev(UiPanel *pPanel);
void BaseFacScreen_PanelNext(UiPanel *pPanel);
void SoldierScreen_PanelPrev(UiPanel *pFirst, UiPanel *pSecond);
void SoldierScreen_PanelNext(UiPanel *pFirst, UiPanel *pSecond);

/* BuySellScreen_ListPanelPrev @ 0x79a20 / ListPanelNext @ 0x79a48: the same two-panel
 * shape as the soldier screen, over unk_161EDC first and unk_161A3C second. */
void BuySellScreen_ListPanelPrev(UiPanel *pFirst, UiPanel *pSecond);
void BuySellScreen_ListPanelNext(UiPanel *pFirst, UiPanel *pSecond);

/* ------------------------------------------------- screen selection caches
 *
 * Evidence: BuySellScreen_SyncSelection @ 0x76138, Transfer_CacheSelIndex @ 0x96ab0,
 * ManufScreen_SyncSelection @ 0x6dd24.
 *
 * All three are the same seven instructions: load a screen-object pointer, read the
 * dword at +0xBD8 (IDA names the field nUnk3032, and 3032 == 0xBD8), store it into one
 * global, and return it. 0x76138 and 0x96ab0 are byte for byte identical and both write
 * dword_186BE8, the cache BaseUi_Cache186BE8 already owns, so they forward to it.
 * 0x6dd24 reads unk_15EE38 instead of unk_161A1C and writes
 * gUiRootPanel.anLineAux[147] (0x159E90), which the port takes as a pointer.
 *
 * Nothing here clamps or validates the index. The original stores whatever the control
 * holds. */
i32 BuySellScreen_SyncSelection(ScreenScratch *pScratch, i32 nSel0BD8);
i32 Transfer_CacheSelIndex(ScreenScratch *pScratch, i32 nSel0BD8);
i32 ManufScreen_SyncSelection(i32 *pnLineAux147, i32 nSel0BD8);

/* ------------------------------------------------- the base info screen's OK button
 *
 * Evidence: BaseInfoScreen_OnOk @ 0x557c8.
 *
 * Three state changes: set byte_1870F8 to 1, copy the SIGNED byte at byte_1870F4 into
 * byte_158834, and return that signed byte (0x557DC uses LDRSB, so a negative index
 * stays negative). 0x1870F4 is the base the psi screen selected -- Geoscape_CloseUI
 * @ 0x8b94c case 19 fills it from Base_CalcPsiLabCap @ 0x6598c -- and 0x158834 is the
 * base the geoscape screens work on.
 *
 * The two writers of 0x158834 disagree in width: 0x557E0 stores a byte, case 19 of
 * Geoscape_CloseUI stores a dword. */
typedef struct BaseInfoOkState {
    signed char nPendingBase; /* byte_1870F4, the base the psi screen selected */
    u8  bPsiPending;          /* byte_1870F8 */
    signed char nCurrentBase; /* byte_158834, the base the geoscape screens act on */
} BaseInfoOkState;

signed char BaseInfoScreen_OnOk(BaseInfoOkState *pState);

/* PsiScreen_OnOk @ 0x716a8 is byte for byte the same eight instructions as
 * BaseInfoScreen_OnOk @ 0x557c8: same globals, same widths, same return. The port keeps
 * one implementation and two names. */
signed char PsiScreen_OnOk(BaseInfoOkState *pState);

/* ------------------------------------------------- geoscape screen openers, second batch
 *
 * Evidence: ManufScreen_Open @ 0x614cc, TransferScreen_Open @ 0x614fc,
 * BaseInfoScreen_Open @ 0x6167c, BuySellScreen_OpenBuy @ 0x61794,
 * BuySellScreen_OpenSell @ 0x617c4, BaseFacScreen_Open @ 0x61844.
 *
 * All six are Sound_Play(17, 4), the screen's Init, and the screen id at 0x186BC4. Two
 * facts the source only shows in the disassembly:
 *
 *   1. Sound_Play takes a second argument, 4 (0x614D4, 0x61504, ...). The port's sample
 *      hook carries it.
 *   2. The order of Init and the id is NOT uniform. Manuf, Transfer and BaseInfo write
 *      the id AFTER the Init (0x614EC, 0x6151C, 0x6169C). BuySell and BaseFac write it
 *      BEFORE (0x617AC, 0x6185C). An Init that reaches the close dispatcher therefore
 *      sees a different screen id depending on which opener ran.
 *
 * The screen slot each Init receives is a global: unk_159E98 (anLineAux[149]) for
 * Manuf, 0x1870D8 for Transfer, unk_158828 for BaseInfo, unk_161A08 for both BuySell
 * modes. The port passes it through the hooks instead of owning it. */
typedef struct ScreenInitHooks {
    void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser);
    void (*pfnInit)(void *pSlot, void *pUser);
    void *pSlot;
    void *pUser;
} ScreenInitHooks;

void ManufScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks);
void TransferScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks);
void BaseInfoScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks);
void BuySellScreen_OpenBuy(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks);
void BuySellScreen_OpenSell(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks);
void BaseFacScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks);

/* ------------------------------------------------- the eight screen close thunks
 *
 * Evidence: BaseInfoScreen_CloseThunk @ 0x557bc, BaseFacScreen_CloseThunkA @ 0x589c0,
 * BaseFacScreen_CloseThunk @ 0x59b54, SoldierScreen_CloseThunk @ 0x64ae4,
 * ManufScreen_CloseThunk @ 0x6e3d4, ResearchScreen_CloseThunk @ 0x72ffc,
 * BuySellScreen_CloseThunk @ 0x75154, NewBaseScreen_CloseThunk @ 0x615b0.
 *
 * Seven of them are two instructions: `LDR R0, =0x1864F8` and `B Geoscape_CloseUI`. They
 * are not seven behaviours; they are one tail call with one argument, written seven
 * times so each widget has its own function pointer. NewBaseScreen_CloseThunk is the odd
 * one: it plays sample 17 first and then makes the same call.
 *
 * Geoscape_CloseUI @ 0x8b94c is not ported (~40 unported callees), so it stays a hook. */
typedef struct GeoscapeCloseHooks {
    void (*pfnCloseUi)(void *pUser);   /* Geoscape_CloseUI(&MEMORY[0x1864F8]) */
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void *pUser;
} GeoscapeCloseHooks;

void BaseInfoScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);
void BaseFacScreen_CloseThunkA(const GeoscapeCloseHooks *pHooks);
void BaseFacScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);
void SoldierScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);
void ManufScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);
void ResearchScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);
void BuySellScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);
void NewBaseScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);

/* OptionsScreen_ReturnToTitle @ 0x6b46c: clears the panel scratch byte, closes the
 * geoscape screen, frees every geoscape widget, and re-inits the startup UI. The scratch
 * byte is the same ScreenScratch field the base-screen helpers set to 3 and 4. */
typedef struct TitleReturnHooks {
    void (*pfnCloseUi)(void *pUser);              /* Geoscape_CloseUI @ 0x8b94c */
    void (*pfnFreeGeoscapeUi)(i32 nMode, void *pUser); /* GeoscapeUi_FreeAll @ 0x86d20 */
    i32 (*pfnStartupUi)(void *pUser);             /* GameUI_InitStartup @ 0x38a28 */
    void *pUser;
} TitleReturnHooks;

i32 OptionsScreen_ReturnToTitle(ScreenScratch *pScratch, const TitleReturnHooks *pHooks);

#endif /* PUFO_BASEUI_H */
