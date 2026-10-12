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

#endif /* PUFO_BASEUI_H */
