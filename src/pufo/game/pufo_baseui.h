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
#include "ui/pufo_control.h" /* UiControl, whose nState these handlers clear */
#include "util/pufo_ptrarray.h" /* PtrArray, for StrategList_RegisterAtexit */
#include "game/pufo_objectrec.h" /* ObjectRecFull, for Research_TopicNeedsItem */
#include "game/pufo_unit.h"      /* Soldier, the array the Find* handlers walk */
#include "game/pufo_clock.h"     /* GameTime, whose pause byte the radar click sets */
#include "game/pufo_geoscape.h"  /* HangarPlaceEnv / BaseRec / BaseSiteRec, the map-click chain */
#include "game/pufo_ship.h"      /* Ship, the craft EquipSelected's gate reads */
#include "game/pufo_strateg.h"   /* SmapView/SmapZoomSet/SmapWorldSize, NewBaseScreen_Open's zoom pair */
#include "render/pufo_render.h"  /* PadImage, SoldierScreen_Toggle's "soldier" pad */
#include "ui/pufo_linelist.h"    /* UiLineList, the root message list the radar click edits */

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

typedef struct GeoscapeScreenHooks {
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void (*pfnOptionsRebuildWithSound)(void *pUser); /* OptionsScreen_RebuildWithSound */
    void (*pfnOptionsOpenWithSound)(i32 nArg, void *pUser); /* OptionsScreen_OpenWithSound */
    void (*pfnFundingOpen)(void *pUser);             /* FundingScreen_Open */
    void *pUser;
} GeoscapeScreenHooks;

void Ui_OpenScreen18(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks);
void Ui_CloseScreen18(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks);
void Ui_OpenFunding(GeoscapeScreenState *pState, const GeoscapeScreenHooks *pHooks);

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
 * (0x8b94c) case 19 fills it from Base_CalcPsiLabCap @ 0x6598c -- and 0x158834 is the
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
void BasePlaneScreen_Open(GeoscapeScreenState *pState, const ScreenInitHooks *pHooks);

/* BaseAtScreen_Open @ 0x588cc. It does NOT play a sample and does NOT set the screen id --
 * it positions one UiControl and installs its click handler, then tail-calls the screen's
 * own update.
 *
 * The ARM (0x588CC..0x58908) pins the details:
 *   LDRH [R3,#4] / LSR #1 / RSB R1, R1, #0x78 / STRH [R3]   -> x = 120 - (uWidth >> 1),
 *     stored as a 16-bit value. The control is centred on x = 120.
 *   MOV R3, #0x120 / ORR R3, R3, #2 / STRH [R1,#2]          -> y = 290, built as 0x120|2.
 *   STR [R1,#0xC]                                           -> pfnClick = the close thunk.
 *   MOV R0, R1 at 0x588D0                                   -> the FIRST argument is dropped;
 *     the mode is the second argument, a char.
 *
 * The control pointer is held at 0x186BA4 (LDR R3, [R2]), so the port takes the control as
 * an argument. Both callees are unported and are named without a citation: the click handler
 * is BaseAtScreen_CloseThunk (0x577c0), a tail call to Geoscape_CloseUI (0x8b94c), and the
 * update is BaseAtScreen_Update (0x57808). */
typedef struct BaseAtScreenOpenHooks {
    void (*pfnClick)(void);   /* stored into pCtl->pfnClick */
    void (*pfnUpdate)(i32 nMode);
} BaseAtScreenOpenHooks;

void BaseAtScreen_Open(UiControl *pCtl, i32 nMode, const BaseAtScreenOpenHooks *pHooks);

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
 * Geoscape_CloseUI (0x8b94c) is not ported (~40 unported callees), so it stays a hook. */
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
    void (*pfnCloseUi)(void *pUser);              /* Geoscape_CloseUI (0x8b94c) */
    void (*pfnFreeGeoscapeUi)(i32 nMode, void *pUser); /* GeoscapeUi_FreeAll (0x86d20) */
    i32 (*pfnStartupUi)(void *pUser);             /* GameUI_InitStartup (0x38a28) */
    void *pUser;
} TitleReturnHooks;

i32 OptionsScreen_ReturnToTitle(ScreenScratch *pScratch, const TitleReturnHooks *pHooks);

/* Five more copies of the same close tail call: FundingScreen_CloseThunk @ 0x5abd8,
 * FundingScreen_ExitThunk @ 0x5c078, Geoscape_CloseUI_ThunkA @ 0x838d0,
 * Geoscape_CloseUI_ThunkB @ 0x96020, Geoscape_CloseUI_ThunkC @ 0x9a8c0. Together with the
 * seven above, twelve widget handlers all reach the same dispatcher. */
void FundingScreen_CloseThunk(const GeoscapeCloseHooks *pHooks);
void FundingScreen_ExitThunk(const GeoscapeCloseHooks *pHooks);
void Geoscape_CloseUI_ThunkA(const GeoscapeCloseHooks *pHooks);
void Geoscape_CloseUI_ThunkB(const GeoscapeCloseHooks *pHooks);
void Geoscape_CloseUI_ThunkC(const GeoscapeCloseHooks *pHooks);

/* Geoscape_OnMapClick_Thunk @ 0x857e8: a widget callback shim that drops its four
 * arguments and tail-calls Geoscape_OnMapClick (0x8b4a8), which is not ported. */
typedef struct MapClickHooks {
    i32 (*pfnMapClick)(void *pUser); /* Geoscape_OnMapClick (0x8b4a8) */
    void *pUser;
} MapClickHooks;

i32 Geoscape_OnMapClick_Thunk(const MapClickHooks *pHooks);

/* ResearchScreen_PanelPrev @ 0x72e30 / PanelNext @ 0x72e68: the paging shape again, but
 * THREE panels -- unk_160C68, then unk_15EEFC, then unk_15EF00, in that order. The last
 * call is the one the original returns. */
void ResearchScreen_PanelPrev(UiPanel *pFirst, UiPanel *pSecond, UiPanel *pThird);
void ResearchScreen_PanelNext(UiPanel *pFirst, UiPanel *pSecond, UiPanel *pThird);

/* Research_TopicNeedsItem @ 0x72c8c: the research screen's per-topic gate. One question:
 * does this topic's required item exist at the base the screen is showing?
 *
 * The gate (ARM 0x72C8C..0x72CB8): topics 55..65 and 118..142 need an item; everything else
 * does not; 135 and 140 are pulled out of the second range by name.
 *
 * The record read is the IN-MEMORY one at 0x15EF08 + 34*topic (MUL R3, R0, #34 at 0x72CC8),
 * not the on-disk field order pufo_save.h writes. The required item id is the byte at +14
 * (LDRB R3, [R3,#0xE] at 0x72CE4); the rest of the in-memory layout stays a gap until the
 * screen chain reads more of it.
 *
 * The lookup is apObjects[item]->auPerBaseVal[nSelectedBaseIdx]: LDR [R3,#0x108] at 0x72CEC
 * picks the pointer out of the Map-block array at 0x1139B8+0x108, LDRH [R3,#0x2A] at 0x72D00
 * reads the u16. The original tests neither the pointer nor the item index, and reads the base
 * index as the signed byte at 0x159D81; the port takes both as arguments and reproduces the
 * unguarded dereference. */
typedef struct ResearchTopicLive {
    u8  abUnk0[4];
    u16 wGate4;   /* +0x4: the weapon fire gate reads it (pufo_combat.h, FireGate evidence) */
    u16 wUnk6;
    u16 wTotal;   /* +0x8: the progress divisor, 0x73174 LDRH [R9,#8] */
    u16 wProgress;/* +0xA: the progress numerator, 0x73160 LDRH [R9,#0xA] */
    u8  bActive;  /* +0xC: 1 = the topic is on the screen list (0x73034, 0x730B8) */
    u8  bGate13;  /* +0xD: the weapon menu gate reads it (pufo_tactui.h, 0x17078..0x17094) */
    u8  nItemId;  /* +0xE: the item this topic needs (0x72CE4) */
    u8  abUnkF[19];
} ResearchTopicLive;
_Static_assert(sizeof(ResearchTopicLive) == 34, "the in-memory topic record is 34 bytes");

i32 Research_TopicNeedsItem(const ResearchTopicLive *aTopics,
                            ObjectRecFull *const *apObjects, i32 nTopic, i8 nBaseIdx);

/* ResearchScreen_RefreshPanels @ 0x73008: rebuild the three research panels' line counts and
 * the two text columns, for the base the screen is showing.
 *
 * Pass 1 (0x73028..0x7304C): count the topics 1..nTopicCount-1 whose bActive is 1 (topic 0 is
 * skipped — the walk starts at 0x15EF2A, one record in). That count becomes nLineCount of all
 * three panels (0x73070/0x7307C/0x73084, the unk_160C68 / unk_15EEFC / unk_15EF00 trio the
 * paging handlers page).
 *
 * Pass 2 (0x730B0..0x73234): walk the same topics; for each active one, line index v6 advances
 * by one. The quantity column entry is u16 at 0x15EF08 + 4900 + 288*base + 2*topic — the ARM
 * composes it as MUL base,#0x90 then ADD topic then LSL #1 (0x730C0..0x730E4), so the entry
 * index is the topic number itself and the Hex-Rays "+4" is register residue from the 0xBE4
 * apLines offset, not a header. A non-zero entry is itoa'd into the unk_15EEFC line; a zero one
 * gets the empty string at 0x10DA38. The progress line (unk_15EF00) is 100*wProgress/wTotal
 * (rt_sdiv at 0x73184) with "%" appended when wProgress is non-zero, else the empty string.
 *
 * Deviations: the original reads the count byte at 0x160C4C and the base index at 0x159D81
 * through the 0x15EF08 anchor; the port takes the count and the selected base's 144-entry
 * column as arguments. The original tests only the NUMERATOR (0x73160 tests wProgress) and
 * divides by wTotal untested — a topic with wProgress != 0 and wTotal == 0 divides by zero; the
 * port also skips the progress store when wTotal is 0 (same policy as the craft-class band in
 * Smap_BeginCraftSelect). Returns the topic count. */
i32 ResearchScreen_RefreshPanels(const ResearchTopicLive *aTopics, i32 nTopicCount,
                                 const u16 *apResearchQty, UiPanel *pPanelCount,
                                 UiPanel *pPanelQty, UiPanel *pPanelProgress);

/* OptionsScreen_ClearMusicButtons @ 0x6b994 and OptionsScreen_ClearSfxButtons @ 0x6b9d4:
 * six widget pointers each, held in gUiRootPanel.anLineAux[128..133] (music) and
 * anLineAux[134..139] (sfx). The handler writes zero into byte +10 of every one of them,
 * which is UiControl.nState -- the same selected/radio byte the bank widgets use. So this
 * is "deselect the whole radio group", not a field wipe.
 *
 * The value returned is the SIXTH widget pointer itself (0x6B9C4 loads it before the last
 * store), not a status code. */
UiControl *OptionsScreen_ClearMusicButtons(UiControl *apMusic[6]);
UiControl *OptionsScreen_ClearSfxButtons(UiControl *apSfx[6]);

/* StrategList_RegisterAtexit @ 0x838a0 is `return atexit(StrategList_Clear);` -- the
 * original registers its own cleanup with the C runtime, because its strategic list is a
 * global and its clear function takes no arguments. The port's StrategList_Clear takes the
 * list, so the port registers a thunk that clears the list handed in here.
 *
 * Deliberately NOT ported: BaseGrid_RegisterAtexit (0x614b4) and its two thunks
 * BaseGrid_Atexit_nullsub5 (0x614a8) / BaseGrid_Atexit_nullsub6 (0x614c0). They register
 * nullsub_5 and nullsub_6, empty compiler-generated stubs, so they carry no behaviour. */
int StrategList_RegisterAtexit(PtrArray *pList);

/* OptionsScreen_RebuildWithSound @ 0x6cf00 and OptionsScreen_OpenWithSound @ 0x6dd04: play
 * sample 7, then tail-call the options screen's own builder. Both pass Sound_Play a second
 * argument, 4 (MOV R1, #4 at 0x6CF08 and 0x6DD10), the same as the geoscape openers.
 *
 * 0x6dd04 takes two arguments and keeps only the second: it saves R1 into R4 before the sample
 * and hands R4 to OptionsScreen_Open as the mode. The first argument is dropped. Both callees,
 * OptionsScreen_Init (0x6be74) and OptionsScreen_Open (0x6d0ac), are not ported, so they are
 * hooks and are named without a citation. */
typedef struct OptionsScreenHooks {
    void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser);
    void (*pfnInit)(void *pUser);
    void (*pfnOpen)(i32 nMode, void *pUser);
    void *pUser;
} OptionsScreenHooks;

void OptionsScreen_RebuildWithSound(const OptionsScreenHooks *pHooks);
void OptionsScreen_OpenWithSound(i32 nIgnored, i32 nMode, const OptionsScreenHooks *pHooks);

/* Funding_ScoreToBarLevel @ 0x5aa60. Score 0..4 maps to 3, 5, 4, 1, 3 (two scores share 3);
 * direction -2/-1/1/2 means -50000/-25000/+25000/+50000; direction 0 or outside -2..2 returns 0
 * and discards the level. */
i32 Funding_ScoreToBarLevel(i32 nScore, i32 nDirection);

/* Funding_DeltaToArrow @ 0x5ab34. Which funding arrow to draw, -2..2, against thresholds derived
 * from the total (total/8 and total/2, integer division). A negative total moves the bands, so the
 * same delta gets a different arrow for a base in the red. */
i32 Funding_DeltaToArrow(i32 nDelta, i32 nTotal);

/* OptionsScreen_PanelPrev @ 0x6d01c / OptionsScreen_PanelNext @ 0x6d064. The options screen has
 * four scrolling lists and its prev/next buttons move all four cursors at once. Both handlers touch
 * the same four panels in the same order. */
void OptionsScreen_PanelPrev(struct UiPanel *const apPanels[4]);
void OptionsScreen_PanelNext(struct UiPanel *const apPanels[4]);

/* Ufopaedia_CacheSelRow @ 0x99fd0. A one-shot cache: while the flag is zero it stores the list's
 * current selected row and raises the flag; once raised it does nothing, so the cached row is never
 * refreshed by this path. Returns whether it cached. */
bool Ufopaedia_CacheSelRow(u8 *pbFlag, i32 *pnCachedRow, i32 nPanelSelectedRow);

/* ------------------------------------------------- the soldier-screen cursor movers
 *
 * Evidence: SoldierScreen_FindNextAtBase @ 0x6192c, SoldierScreen_FindPrevAtBase @ 0x618c4.
 *
 * The soldier screen keeps its selected soldier in the dword cursor at 0x159E08 (read by
 * SoldierScreen_Draw @ 0x65290, EquipSelected @ 0x619cc, CopyNameToEdit @ 0x6318c and
 * SelectListed @ 0x64a9c). These two handlers move it to the next / previous soldier that
 * belongs to the base the screen is showing.
 *
 * FindNext (ARM 0x61930..0x619A4): nIdx = cursor + 1; if nIdx >= the u16 soldier count
 * (unk_115350, LDRH at 0x61940) it returns nIdx with the cursor UNCHANGED. Otherwise it walks
 * the soldier pointer array (Map + 0x1198, reached as `LDR R11,[R1,#0x1198]` at 0x61964) and
 * compares each soldier's base byte at +0xB6 (Soldier.nKind, LDRB) against the selected base
 * index (nSelectedBaseIdx @ 0x159D81, loaded SIGNED with LDRSB at 0x61960). On a match it
 * stores the cursor and returns the index; running off the end returns the count itself.
 *
 * FindPrev mirrors it downward: cursor - 1, walk down, return -1 when nothing matches. It never
 * reads the soldier count — the walk is bounded by zero, so the port drops the count argument.
 *
 * The compare is `CMP R11, R2` between a 0..255 byte and a sign-extended -128..127 byte: a
 * negative selected index matches nothing, and the port reproduces that by widening both to
 * i32. The original tests neither the count nor the pointers it walks. Deviation: the port
 * keeps the unguarded dereference (the shipped arrays are dense), so a NULL entry is a crash
 * in both binaries. */
i32 SoldierScreen_FindNextAtBase(i32 *pnCursor, Soldier *const *apSoldiers, u16 nSoldierCount,
                                 i8 nSelectedBaseIdx);
i32 SoldierScreen_FindPrevAtBase(i32 *pnCursor, Soldier *const *apSoldiers, i8 nSelectedBaseIdx);

/* Ui_SetTransferFlag @ 0x619a8: Sound_Play(17, 4) then byte_159E10 = 1 (STRB at 0x619C0).
 * The transfer flag gates the soldier-transfer UI (read by EquipSelected @ 0x619cc,
 * TextInputDialog @ 0x632cc, OnPointer @ 0x872d0, OnPointerUp @ 0x89450 and CloseUI @ 0x8b94c).
 * The original returns the byte_159E10 address; no caller tests it, so the port returns void. */
void Ui_SetTransferFlag(u8 *pbTransferFlag, void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser),
                        void *pUser);

/* ------------------------------------------------- the new-base screen
 *
 * Evidence: NewBaseScreen_Open @ 0x615d0, NewBaseScreen_TryConfirm @ 0x6152c.
 *
 * Open (ARM 0x615D0..0x61658): Sound_Play(17, 4); if the SIGNED base count (0x159D80, LDRSB
 * at 0x615F0) is already 8 it returns without touching anything. Otherwise it positions the
 * frame control (unk_186B00) at x=224, y=1 (both STRH, so 16-bit stores), sets the geoscape
 * screen id (0x186BC4) to 2, installs TryConfirm as the confirm control's (unk_186BA4) click
 * handler, calls Smap_ZoomOut twice on the viewport at 0x1863E8 (2 -> 1 -> 0), clears
 * byte_187080 (STRB at 0x6164C), and installs the close thunk on the frame control.
 *
 * TryConfirm (ARM 0x6152C..0x61590): Sound_Play(17, 4). If the placement-pending byte at
 * 0x1870AC is non-zero the funds test is skipped entirely and Geoscape_OnMapClick runs.
 * Otherwise cost = aNewBaseCostByRace[byte_159DF4 - 128] (the ARM composes it as
 * `ADD R3, R0, R3, LSL#2` + `LDR R3,[R3,#-0x200]` at 0x61554..0x61568, so the byte is
 * 128+kind and the table index is kind), and funds (unk_1598D8) - cost >= 0 also runs
 * OnMapClick. On shortfall it stores 1 into 0x1870C8 and 0 into dword_1870B4 and returns
 * dword_1870B4 (0); OnMapClick's result is returned otherwise.
 *
 * Width notes the source only shows in ARM: 0x1870AC is read as a BYTE (LDRB at 0x6153C);
 * the failure store into 0x1870C8 is a DWORD store (STRMI at 0x61574) while every reader in
 * the image loads it as a byte, so the port stores a byte (deviation, unobservable).
 * byte_159DF4 is 128+kind (0xFF marks "no pending placement" in the pointer handlers); the
 * original indexes the 10-entry cost table unguarded, so a kind outside 0..9 reads adjacent
 * image bytes. The shipped paths reach kinds 0..9 only; the port treats out-of-range as cost 0
 * (deviation, documented in pufo_baseui.c). */
#define PUFO_NEW_BASE_COST_COUNT 10
extern const i32 g_anNewBaseCostByKind[PUFO_NEW_BASE_COST_COUNT]; /* aNewBaseCostByRace @ 0xAB488 */

typedef struct NewBaseOpenEnv {
    void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser);
    UiControl *pCtlFrame;          /* unk_186B00 (GEOUI_CTL_186B00) */
    UiControl *pCtlConfirm;        /* unk_186BA4 (GEOUI_CTL_186BA4) */
    SmapView *pView;               /* gSmapCam @ 0x1863E8 */
    const SmapZoomSet *pZoomSets;  /* the three zoom layer sets */
    SmapWorldSize *pWorldOut;      /* the u16 pair at 0x186BC0 */
    i8 *pbBaseCount;               /* 0x159D80, signed */
    u8 *pbFlag187080;              /* unk_187080 */
    void (*pfnTryConfirm)(void);   /* NewBaseScreen_TryConfirm, installed at +0xC */
    void (*pfnCloseThunk)(void);   /* NewBaseScreen_CloseThunk @ 0x615b0, installed at +0xC */
    void *pUser;
} NewBaseOpenEnv;

void NewBaseScreen_Open(GeoscapeScreenState *pState, const NewBaseOpenEnv *pEnv);

typedef struct NewBaseConfirmEnv {
    void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser);
    u8 bPendingKind;      /* byte_159DF4: 0xFF = none, else 128+kind */
    i32 *pnFunds;         /* unk_1598D8 */
    u8 *pbFlag1870C8;     /* 0x1870C8, the GeoscapeState low-supply flag */
    i32 *pnFlag1870B4;    /* dword_1870B4 */
    i32 (*pfnMapClick)(void *pUser); /* Geoscape_OnMapClick(&MEMORY[0x1864F8]), not ported */
    void *pUser;
} NewBaseConfirmEnv;

i32 NewBaseScreen_TryConfirm(u8 bPlacementPending, const NewBaseConfirmEnv *pEnv);

/* ------------------------------------------------- the geoscape nav controls
 *
 * Evidence: Geoscape_BuildNavControls @ 0x67b1c. Callers: Game_StartNew @ 0x9542C,
 * Base_EnterBuildMode @ 0x84E6C, Ui_OpenResearch @ 0x84CBC.
 *
 * Three groups of heap controls, each: operator new(0x20) + UiControl_ctor for all of them,
 * then the pfnClick stores, then UiControl_Setup(ctl, 0, 0, desc) for all of them.
 *
 *   1. ten nav buttons at 0x159888..0x1598AC (Map + 0x120): NewBaseScreen_Open,
 *      BaseInfoScreen_Open, SoldierScreen_Toggle, BasePlaneScreen_Open, BaseFacScreen_Open,
 *      ResearchScreen_Open, ManufScreen_Open, TransferScreen_Open, BuySellScreen_OpenBuy,
 *      BuySellScreen_OpenSell. Descriptors stru_AB4B0: x = 1 for the first five and 120 for
 *      the second five, y = 229, 247, 265, 283, 301 (step 18), pads "emptyb"/"cemptyb", mode 0.
 *   2. two soldier-list page buttons at 0x1598B0/B4: SoldierScreen_PanelPrev / PanelNext.
 *      stru_AB5F0: (210, 20) "up"/"sup" and (210, 40) "down"/"sdown".
 *   3. three soldier-find buttons at 0x1598B8..C0: SoldierScreen_FindNextAtBase,
 *      SoldierScreen_FindPrevAtBase, Ui_SetTransferFlag. stru_AB630: (35, 37) "bnexts"/"bnext",
 *      (3, 37) "bprevs"/"bprev", (70, 37) "emptyb1"/"cemptyb1".
 *
 * ResearchScreen_Open (0x616ac) is still unported, and SoldierScreen_Toggle (0x64af0) is ported but
 * installed through the env like the rest, so all fifteen handlers arrive as plain click thunks --
 * the same seam Geoscape_InitUI uses for its root controls.
 *
 * Deviations: the original stores a NULL from a failed operator new and then dereferences the
 * first slot anyway (0x67B74); the port guards every store and reports the failure through the
 * return value. The original never frees these fifteen controls (no code outside this function
 * touches the slots), so a second call leaks the first set; the port hands the pointers to the
 * caller in NavControls so the caller can release them. */
#define PUFO_NAV_CTLS 10
#define PUFO_NAV_PANEL_CTLS 2
#define PUFO_NAV_FIND_CTLS 3

typedef struct NavControls {
    UiControl *apNav[PUFO_NAV_CTLS];         /* 0x159888..0x1598AC */
    UiControl *apPanel[PUFO_NAV_PANEL_CTLS]; /* 0x1598B0/B4 */
    UiControl *apFind[PUFO_NAV_FIND_CTLS];   /* 0x1598B8..0x1598C0 */
} NavControls;

typedef struct NavCtlEnv {
    void (*apfnNavClick[PUFO_NAV_CTLS])(void);
    void (*apfnPanelClick[PUFO_NAV_PANEL_CTLS])(void);
    void (*apfnFindClick[PUFO_NAV_FIND_CTLS])(void);
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser);
    void *pLoadPadsUser;
} NavCtlEnv;

extern const UiControlDesc g_aGeodescNav[PUFO_NAV_CTLS];         /* stru_AB4B0 @ 0xAB4B0 */
extern const UiControlDesc g_aGeodescPanel[PUFO_NAV_PANEL_CTLS]; /* stru_AB5F0 @ 0xAB5F0 */
extern const UiControlDesc g_aGeodescFind[PUFO_NAV_FIND_CTLS];   /* stru_AB630 @ 0xAB630 */

bool Geoscape_BuildNavControls(NavControls *pOut, const NavCtlEnv *pEnv);

/* ------------------------------------------------- the status-panel (radar list) click
 *
 * Evidence: Geoscape_OnRadarListClick @ 0x854c4, installed as the status panel's pfnClick by
 * Geoscape_InitUI @ 0x857f4 (see the header comment in ui/pufo_geoiinit.h).
 *
 * The row index the panel selected (pStatusPanel->nUnk3032) indexes anLineAux, and THAT value
 * is the strateg item index: the panel lists strateg items and the aux column carries their
 * list position. The index is stored at 0x186BE4 (Hex-Rays renders it as the dword at
 * word_186BD4[8]), byte_18709C[4] is cleared, and the clock is unpaused before the dispatch.
 * Then five (nType, bActive) cases and one tail case, each of which pauses the clock
 * (GameTime_Pause @ 0x5d6ac is a plain store of 1 to 0x15961C) and edits the root message list
 * at 0x158890 through the UiLineList leaves:
 *
 *   type 1, active   : drop line 0, insert at 0 with flag 10 and the item index as colour slot 0
 *   type 0, inactive : screen id 18, OptionsScreen_OpenWithSound(item->nSiteId)
 *   type 1, inactive : 0x187054 and 0x159E74 = Ship_FindIndexByStrategType(item index),
 *                      append flag 11 with the ship index, radar pulse timer 250
 *   type 4, active   : flag 0x18708C = 1, drop line 0, append flag 20 (row index, item index)
 *   type 3, active   : flag 0x187090 = 1, drop line 0, append flag 19, timer 250
 *   type 0, active   : flag 0x187098 = 1, drop line 0, append flag 18, timer 250, return 250
 *
 * Deviations: the original re-reads the item through PtrArray_Get in every test and never
 * bounds it - an out-of-range index dereferences whatever comes back; the port reads the item
 * once and returns 0 when the list has no such entry. The original's non-250 return value is
 * the item POINTER (register residue); nothing reads it (the widget callback type is void), so
 * the port returns 0 there. */
typedef struct RadarClickEnv {
    UiLineList   *pLines;                /* the root message list @ 0x158890 */
    UiPanel      *pStatusPanel;          /* unk_18641C, the panel whose row was clicked */
    PtrArray     *pStrategList;          /* the strateg list @ 0x186534 */
    i32          *pnSelectedStrategIdx;  /* 0x186BE4 */
    GameTime     *pTime;                 /* 0x159618; bPaused is the byte at 0x15961C */
    GeoscapeWorld *pWorld;               /* Ship_FindIndexByStrategType's craft array */
    i32          *pnSelectedShipIdx;     /* 0x187054 */
    i32          *pnStrategIndex;        /* 0x159E74 */
    i32          *pnScreenId;            /* nGeoscapeScreen @ 0x186BC4 */
    u8           *pbFlag1870A0;          /* byte_18709C[4] */
    u8           *pbFlag18708C;
    u8           *pbFlag187090;
    u8           *pbFlag187098;
    i32          *pnRadarPulseTimer;     /* nRadarPulseTimer @ 0xAC224 */
    const OptionsScreenHooks *pOptionsHooks; /* the type-0 inactive branch */
    void (*pfnPlaySample)(i32 nSample, void *pUser); /* UiLineList_AppendWithSound's seam */
    void *pSoundUser;
} RadarClickEnv;

i32 Geoscape_OnRadarListClick(const RadarClickEnv *pEnv);

/* ------------------------------------------------- the base-facility screen's bring-up
 *
 * Evidence: BaseFacScreen_Init @ 0x5a198. Two callers: BaseFacScreen_Open @ 0x61868 and
 * Geoscape_OnMapClick @ 0x8B654 (the screen-2 branch with no pending confirm).
 *
 * In the original's order:
 *
 *   1. the close control (unk_186B00) gets BaseFacScreen_CloseThunkA as its click handler FIRST
 *      (0x5A1C4) and BaseFacScreen_CloseThunk at the END (0x5A254). In the IDB both are separate
 *      functions and both are one tail call to Geoscape_CloseUI, so the same handler is installed
 *      twice with the whole allocation between them. Then x = 224 and y = 229 (0x5A260, 0x5A26C).
 *   2. 0x15887C and 0x158880 are cleared, then two controls are allocated (operator new 0x20 +
 *      UiControl_ctor) into the pointer slots at 0x158874 and 0x158878 (0x5A1CC..0x5A200).
 *   3. the first control's +12 handler slot gets BaseFacScreen_PanelPrev and the second's gets
 *      BaseFacScreen_PanelNext (0x5A214, 0x5A220) - the pair that scrolls the panel the original
 *      keeps at unk_15886C.
 *   4. both controls are then positioned with the two descriptors at 0xAB220 (the "up"/"sup" and
 *      "down"/"sdown" pads, offsets 11/252 and 11/277) at base (0, 0) (0x5A224..0x5A244).
 *   5. 0x158888 is cleared and BaseFacScreen_Toggle (0x589ec) runs last as a tail call, and its
 *      result is this function's return value. Toggle builds the panel and installs
 *      BaseFacScreen_TryBuySelected; it is not ported yet, so it enters here as a hook.
 *
 * Deviations: the original allocates with operator new and stores into fixed .bss slots; the port
 * takes caller-owned controls. The port's handler slot is `void (*)(void)`, so the panel pair
 * arrives as the env's own thunks rather than as BaseFacScreen_PanelPrev itself. The Toggle hook
 * returns nothing, so the port returns 0 where the original returns Toggle's register. */
#define PUFO_BASEFAC_PANEL_DESC_COUNT 2

typedef struct BaseFacInitEnv {
    UiControl *pCloseCtl;        /* unk_186B00, the control that closes the screen */
    UiControl *pUpCtl;           /* the control the original allocates into 0x158874 */
    UiControl *pDownCtl;         /* ... and 0x158878 */
    i32  *pnUnk15887C;           /* 0x15887C, cleared before the allocation */
    i32  *pnUnk158880;           /* 0x158880, cleared before the allocation */
    u8   *pbArmed;               /* bBaseFacPlaceArmed @ 0x158888, a byte (STRB at 0x5A274) */
    void (*pfnPanelPrev)(void);  /* the original stores BaseFacScreen_PanelPrev @ 0x589cc */
    void (*pfnPanelNext)(void);  /* ... and BaseFacScreen_PanelNext @ 0x589dc */
    void (*pfnCloseThunk)(void); /* the original stores BaseFacScreen_CloseThunkA / CloseThunk */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
    void (*pfnToggle)(void *pUser); /* BaseFacScreen_Toggle (0x589ec), not ported yet */
    void *pToggleUser;
} BaseFacInitEnv;

i32 BaseFacScreen_Init(const BaseFacInitEnv *pEnv);

/* ------------------------------------------------- a click on the geoscape map
 *
 * Evidence: Geoscape_OnMapClick @ 0x8b4a8 (1184 B). It is installed as the map click handler and
 * tail-called by NewBaseScreen_TryConfirm @ 0x6152c; nothing reads its return value.
 *
 * It branches on the screen id (nGeoscapeScreen @ 0x186BC4) and does nothing for any other id.
 *
 * Screen 1 - the hangar placement, gated on bBasePlaceActive (0x1870BC):
 *
 *   - `Base_CanPlaceHangarAt(grid, nSmapTapX, nSmapTapY, bBasePlaceActive)` decides. A zero sets
 *     bBasePlaceFail (0x1870B8) and stops.
 *   - A non-zero result clears the tapped cells. Which cells depends on the tileA byte already
 *     there: 9..12 are the hangar's four quadrants, so the whole 2 by 2 footprint goes; anything
 *     else clears the single cell. Both paths clear tileA, tileB, layer2 and buildDays and write
 *     the 0xFF facType sentinel.
 *   - The 2 by 2 path is a three-iteration loop (0x8B6FC..0x8B7B8) whose index arithmetic is worth
 *     spelling out: the four stores per iteration land at 36*i + 6*nY + nX + {0, 1, 6, 7} inside
 *     the base's 144-byte block, so i walks the three 36-byte layers tileA, tileB and layer2 and
 *     {0,1,6,7} is the 2x2 footprint within each. buildDays is cleared separately at +0, +6, +1
 *     and +7 - the same four cells of the days lane.
 *
 * Screen 2 - the pending facility, gated on the pending kind (0x159DF4) not being 0xFF:
 *
 *   - With bBaseFacConfirm (0x1870AC) set, the pending placement is committed: apBases[0] gets
 *     nUnk8 / nUnk12 = the pending coordinates (0x159D94 / 0x159D98), bUnk17 = the kind and
 *     bUnk16 = 1; the aBaseFacilities record gets bType = kind + 0x80 and its abName field (the
 *     label at 0x186C5A) gets the string BaseFac_FormatCostLabel builds. Then the strateg list is
 *     searched for the entry whose nSiteId equals apBases[0]->bUnk4 and that entry's nX / nY are
 *     set to the pending coordinates. The screen id becomes 1, bBaseFacConfirm and bBaseFacDone
 *     (0x187078) are cleared.
 *   - Without the confirm flag, nSelectedBaseIdx takes the base COUNT (0x159D80, signed), the
 *     screen id becomes 7, and BaseFacScreen_Init runs as a tail call.
 *
 * Deviations: the strateg search has no bound - on a miss the index equals the count and the
 * original writes nX / nY past the last entry. The port skips that write. The original returns
 * register residue and, in two branches, the address of a flag; the port returns a status
 * instead (see the .c). */
typedef struct MapClickEnv {
    u8   *abFacilityGrid;          /* gBaseFacGrid @ 0x159768 */
    i32  *pnScreenId;              /* nGeoscapeScreen @ 0x186BC4 */
    u8   *pbPlaceActive;           /* bBasePlaceActive @ 0x1870BC */
    u8   *pbPlaceFail;             /* bBasePlaceFail @ 0x1870B8 */
    i32   nTapX;                   /* nSmapTapX @ 0x186B7C */
    i32   nTapY;                   /* nSmapTapY @ 0x186B80 */
    const HangarPlaceEnv *pHangar; /* the placement test's own env (grid, base, rosters) */

    u8   bPendingKind;             /* 0x159DF4, the facility kind awaiting placement */
    u8   *pbConfirm;               /* bBaseFacConfirm @ 0x1870AC */
    u8   *pbFacDone;               /* bBaseFacDone @ 0x187078 */
    i32   nPlaceX;                 /* 0x159D94 */
    i32   nPlaceY;                 /* 0x159D98 */
    BaseRec **apBases;             /* 0x114B2C */
    BaseSiteRec *apSiteRecs;       /* 0x186C40, eight 64-byte records */
    PtrArray *pStrategList;        /* strategList @ 0x186534 */
    const u8 *pTextRecs;           /* TEXTS, for the cost label */
    i8   *abKindTable;             /* abBaseKindTable @ 0x1864DD */
    char *pszLabelScratch;         /* szRadarMsg @ 0x186B10, where the label is built */
    i32   cchLabelScratch;
    i8   *pbBaseCount;             /* 0x159D80, signed */
    i8   *pnSelectedBaseIdx;       /* 0x159D81 */
    const BaseFacInitEnv *pFacInit; /* the screen-7 branch */
} MapClickEnv;

i32 Geoscape_OnMapClick(const MapClickEnv *pEnv);

/* --- BaseFacScreen_Toggle @ 0x589ec (1060 B) : the build dialog's open/close switch ---
 *
 * One byte decides the direction: unk_158884 is 1 while the dialog exists.
 *
 * CLOSE (0x58D98..0x58E04): unk_158884 = 0; the two panel controls at unk_158874 and unk_158878
 * (the pair BaseFacScreen_Init allocated) are released with UiControl_FreePads + operator delete;
 * the panel's nLineCount is raised back by 1 (0x58DE4, see step 5); then the UiPanel at unk_15886C
 * has its lines freed and the panel itself deleted.
 *
 * BUILD (0x58A10..0x58D74):
 *   1. nCount = how many of aBaseFacFlags[0..16] have bFlag20 set (0x58A1C..0x58A30).
 *   2. operator new(0x1BB4) + UiPanel_ctor(nCount) -> unk_15886C. The nStyle store at 0x58A7C
 *      dereferences the result even when the allocation failed (bugs.tsv).
 *   3. nStyle 5, nFont 16, the +3020 click handler = BaseFacScreen_TryBuySelected, nPosX 120,
 *      nPosY 234, nUnk7052 5, nUnk7056 234, nViewW 230, gap_0[0..2] = B4 B4 B4,
 *      gap_0[3..5] = 46 96 AA, gap_0[6..11] = 44 F4 FC 04 46 4B, bUnk7088 0x96, bFlag7072 0,
 *      bFlag7073 1, nUnk7076 1.
 *   4. rows (0x58BAC..0x58C0C): aBaseFacFlags[1..16] in order, 16 iterations. Each record with
 *      bFlag20 set gets its szName copied into the next apLines slot and anLineAux = the record
 *      number, which is the facility kind BaseFacScreen_TryBuySelected looks up. A 17th row is
 *      then written UNCONDITIONALLY from aBaseFacFlags[17] -- 0x187024, four bytes before pRdr,
 *      which is a UiControl pointer slot and not a facility record (bugs.tsv). Its aux is 0.
 *   5. nLineCount -= 1 (0x58C68). The count loop includes record 0 while the row loop starts at
 *      record 1, so with record 0 flagged the two agree and the ctor allocated exactly the rows
 *      written; the close branch adds the one back before freeing.
 *   6. With byte_187074[4] (the new-base mode) set: unk_158868 = the row count, unk_158888 = 1,
 *      and every cell of the selected base is wiped -- buildDays, tileA, tileB and layer2 to 0 and
 *      facType to 0xFF (0x58C9C..0x58D74; the `6*(24*base + row + 18) + col` index is the facType
 *      array at grid + 0x174 + 144*base + 108).
 *
 * Deviations: the original allocates the controls and the panel with operator new into fixed .bss
 * slots; the port owns only the panel and takes caller-owned controls, so it frees their pads but
 * not the objects. The port returns early on a failed allocation where the original dereferences
 * the null panel. */
#define PUFO_BASEFAC_PANEL_CTL_COUNT 2

typedef struct BaseFacToggleEnv {
    u8   *pbOpen;                  /* unk_158884: 1 while the dialog is built */
    UiControl *apPanelCtl[PUFO_BASEFAC_PANEL_CTL_COUNT]; /* unk_158874, unk_158878 */
    UiPanel **ppPanel;             /* unk_15886C */
    i32  *pnPanelSel;              /* unk_158868 */
    u8   *pbArmed;                 /* unk_158888 */
    const u8 *pbNewBaseMode;       /* byte_187074[4] */
    u8   *abFacilityGrid;          /* abFacilityGrid @ 0x159768 */
    const i8 *pnSelectedBaseIdx;   /* nSelectedBaseIdx @ 0x159D81 */
    const BaseFacFlag *paFac;      /* aBaseFacFlags @ 0x186E48, PUFO_BASEFAC_COUNT records */
    const char *pszLastRowName;    /* the 4 bytes at 0x187024, the 17th row's "name" */
    void (*pfnBuyThunk)(void);     /* installed as the panel's +3020 handler */
} BaseFacToggleEnv;

void BaseFacScreen_Toggle(const BaseFacToggleEnv *pEnv);

/* --- BaseFacScreen_TryBuySelected @ 0x58910 (176 B) : the build dialog's OK ---
 *
 * sel = panel->nUnk3032; kind = panel->anLineAux[sel]; cost = aBaseFacFlags[kind].uCost (+24).
 * nFunds - cost < 0 (0x58958, SUBS then the MI stores): byte_1870C8 = 1 as a DWORD (every reader
 * loads a byte) and dword_1870B4[0] = 0, and the original returns &dword_1870B4. Otherwise
 * unk_158888 = 1 (0x5897C, arming the placement) and, when byte_187074[4] == 0, unk_158868 takes
 * panel->nUnk3032 (0x58994). Neither index is bounds-checked: an out-of-range sel reads anLineAux
 * past the array and an out-of-range kind reads aBaseFacFlags past the table (bugs.tsv).
 *
 * Deviation: the port returns 1 when the purchase is affordable and 0 on the shortfall, where the
 * original hands back the panel pointer or &dword_1870B4. */
typedef struct BaseFacBuyEnv {
    UiPanel *pPanel;               /* unk_15886C */
    const BaseFacFlag *paFac;      /* aBaseFacFlags @ 0x186E48 */
    const i32 *pnFunds;            /* nFunds @ 0x1598D8 */
    u8   *pbArmed;                 /* unk_158888 */
    const u8 *pbNewBaseMode;       /* byte_187074[4] */
    i32  *pnPanelSel;              /* unk_158868 */
    u32  *pdwShortfall;            /* byte_1870C8, written as a dword */
    i32  *pnResidue;               /* dword_1870B4[0] */
} BaseFacBuyEnv;

i32 BaseFacScreen_TryBuySelected(const BaseFacBuyEnv *pEnv);

/* --- SoldierScreen_Toggle @ 0x64af0 (1952 B) : the soldier screen's open/close switch ---
 *
 * Sound_Play(17), then nGeoscapeScreen = 4 (0x64B08) whatever the direction. The byte at 0x159DE0
 * decides which way to go.
 *
 * CLOSE (0x651BC..0x65288): screen id back to 1, the flag cleared, then the two fixed 4-line
 * panels get nLineCount = 4 again (0x651E4 / 0x651EC -- stored through the panel pointers with no
 * null test, bugs.tsv) and all five objects are released: the 0x159DF0 and 0x159758 panels, the
 * PadImage at unk_1598D4 (PadImage_FreeData + delete), and the 0x15975C and 0x159DEC panels.
 *
 * OPEN (0x64B20..0x651A0):
 *   1. the flag is set; a PadImage (operator new 0x14 + PadImage_ctor) is loaded with the pad
 *      "soldier" at mode 8 into unk_1598D4, with nOriginX / nOriginY 0.
 *   2. panel A @ 0x159DF0 (4 lines): nStyle 4, nFont 18, +3020 = SoldierScreen_EquipSelected,
 *      nPosX 55, nPosY 102, +7052 46, +7056 102, nViewW 167, gap_0[0..2] 44 F4 FC,
 *      gap_0[3..5] 32 A0 BD, gap_0[6..11] C8 F4 FC 04 46 4B, bFlag7073 1, bUnk7088 0xFF.
 *   3. panel B @ 0x159758 (4 lines): nStyle 4, nFont 18, nPosX 190, nPosY 102, +7052 46,
 *      +7056 102, nViewW 167, gap_0[0..2] 44 F4 FC, gap_0[3..5] 32 32 FA,
 *      gap_0[6..11] 44 F4 FC 04 46 4B, bFlag7072 0. No click handler.
 *   4. n = how many of apSoldiers[0..count) have the base byte at +0xB6 equal to nSelectedBaseIdx
 *      (0x64BA4..0x64CC4; the count is the u16 at unk_115350).
 *   5. panel C @ 0x15975C (n lines): nStyle 13, nFont 18, +3020 = SoldierScreen_SelectListed,
 *      nPosX 10, nPosY 70, +7052 2, +7056 70, nViewW 234, gap_0[0..2] B4 B4 B4,
 *      gap_0[3..5] 32 32 FA, gap_0[6..11] F0 F0 F0 14 14 14, bFlag7073 1, bUnk7088 0x96.
 *   6. panel D @ 0x159DEC (n lines): nStyle 13, nFont 18, nPosX 230, nPosY 70, +7052 5,
 *      +7056 70, nViewW 40, gap_0[0..2] B4 B4 B4, gap_0[6..11] F0 F0 F0 14 14 14,
 *      bFlag7073 0, nUnk7076 2, bUnk7088 0x96. No click handler.
 *   7. the row pass (0x65018..0x651A0) walks the roster again; for every soldier on the selected
 *      base, panel C gets szName (+0xBA) and anLineAux = the ROSTER index, panel D gets
 *      Soldier_FormatDisplayName's string, and the row colours come from the craft byte at +0xB7:
 *      254 gives (250,150,150 / 20,20,20) and 255 gives (150,150,250 / 20,20,20) on both panels.
 *      Any other value leaves the row colours at the ctor defaults.
 *   8. unk_186B00's +12 handler becomes SoldierScreen_CloseThunk -- the same control slot
 *      BaseFacScreen_Init uses for the build dialog.
 *
 * Deviations: the port owns only the five heap objects; the controls stay caller-owned. The panel
 * line-count stores are guarded where the original stores through the pointers unchecked. */
typedef struct SoldierScreenEnv {
    u8      *pbOpen;              /* 0x159DE0, the screen-open byte */
    i32     *pnScreenId;          /* nGeoscapeScreen @ 0x186BC4 */
    Soldier *const *apSoldiers;   /* Map + 0x1198 */
    const u16 *pnSoldierCount;    /* unk_115350 */
    const i8 *pnSelectedBaseIdx;  /* nSelectedBaseIdx @ 0x159D81 */
    UiPanel **ppPanelEquip;       /* 0x159DF0, panel A */
    UiPanel **ppPanelStores;      /* 0x159758, panel B */
    UiPanel **ppPanelNames;       /* 0x15975C, panel C */
    UiPanel **ppPanelCraft;       /* 0x159DEC, panel D */
    PadImage **ppPad;             /* unk_1598D4 */
    const char *pszPadName;       /* the pad loaded at mode 8 ("soldier"); NULL skips the load */
    UiControl *pCloseCtl;         /* unk_186B00 */
    void (*pfnCloseSoldierThunk)(void); /* SoldierScreen_CloseThunk @ 0x64ae4, stored at +12 */
    void (*pfnEquipThunk)(void);  /* SoldierScreen_EquipSelected @ 0x619cc */
    void (*pfnSelectThunk)(void); /* SoldierScreen_SelectListed @ 0x64a9c */
    const char *(*pfnFormatName)(void *pUser, Soldier *pSol); /* Soldier_FormatDisplayName (0x6654c) */
    void *pFormatUser;
} SoldierScreenEnv;

void SoldierScreen_Toggle(const SoldierScreenEnv *pEnv);

/* --- SoldierScreen_SelectListed @ 0x64a9c (72 B) : panel C's click ---
 *
 * nGeoscapeScreen = 5 (0x64AA8) and nSoldierCursor @ 0x159E08 takes
 * panel(0x15975C)->anLineAux[panel->nUnk3032] (0x64AD0) -- the roster index the row pass stored.
 * The original returns &nSoldierCursor; the port returns the cursor value. */
typedef struct SoldierSelectEnv {
    UiPanel *pPanel;     /* 0x15975C */
    i32     *pnCursor;   /* nSoldierCursor @ 0x159E08 */
    i32     *pnScreenId; /* nGeoscapeScreen @ 0x186BC4 */
} SoldierSelectEnv;

i32 SoldierScreen_SelectListed(const SoldierSelectEnv *pEnv);

/* --- SoldierScreen_EquipSelected @ 0x619cc (448 B) : panel A's click, the four equip rows ---
 *
 * Sound_Play(17) (0x619D8); the soldier is apSoldiers[nSoldierCursor] (0x619F4).
 *
 * The gate (0x61A1C) is `craft byte >= 0xFE || apCraft[craft byte].bAtBase != 0`, where the craft
 * byte is Soldier.bUnk183 @ +0xB7 (254/255 mean "no craft") and bAtBase is Ship +0x06. A soldier
 * riding a craft that is off-base is left alone: the handler returns the soldier pointer and
 * changes nothing.
 *
 * The row is pSolPanelEquip->anLineAux[pSolPanelEquip->nUnk3032] (0x61A44). The equipment is
 * apObjects[121 + nUnitType] -- nUnitType 0 means nothing equipped, and rows 1..3 select objects
 * 122, 123 and 124, the three the original keeps as globals at 0x113CA8, 0x113CAC and 0x113CB0.
 *
 *   row 0 (0x61B30): with nUnitType non-zero, BaseRec_AddItemQty puts apObjects[121 + nUnitType]
 *      back into the base stores (0x61B60) and nUnitType goes to 0 (0x61B74).
 *   rows 1..3 (0x61AF0 / 0x61AE0 / 0x61A6C): nUnitType takes the row number; with the old type
 *      non-zero the new item leaves the base (BaseRec_SubItemQty at 0x61A80) and the old one comes
 *      back (BaseRec_AddItemQty at 0x61AC4); with the old type zero only the new item leaves
 *      (0x61B04).
 *   any other row: nothing but the tail.
 *
 * The tail (0x61B78) clears bTransferFlag @ 0x159E10 and returns &bTransferFlag. The base is
 * apBases[nSelectedBaseIdx] in all four calls (index 1117 dwords = Map + 0x1174).
 *
 * Deviations: the port returns 1 when the body ran and 0 when the gate refused (the original hands
 * back a pointer); it bounds the cursor, the base index and the old nUnitType to the 121..124 group
 * where the original indexes apSoldiers, apBases and apObjects unchecked; and it reads the row
 * through UiPanel_GetLineAux, which yields 0 for a selection outside the panel where the original
 * reads anLineAux at whatever nUnk3032 holds. */
#define PUFO_SOLDIER_EQUIP_ROWS   4
#define PUFO_SOLDIER_EQUIP_OBJ_BASE 121 /* apObjects[121 + nUnitType]; 122..124 are the three items */

typedef struct SoldierEquipEnv {
    Soldier *const *apSoldiers;      /* apSoldiers @ Map + 0x1198 */
    const u16 *pnSoldierCount;       /* nSoldierCount @ 0x115350 */
    Ship *const *apCraft;            /* Map + 0x10AC */
    BaseRec *const *apBases;         /* apBases @ Map + 0x1174 */
    ObjectRecFull *const *apObjects; /* Map + 0x108 */
    const i32 *pnCursor;             /* nSoldierCursor @ 0x159E08 */
    const i8 *pnSelectedBaseIdx;     /* nSelectedBaseIdx @ 0x159D81 */
    UiPanel *pPanelEquip;            /* pSolPanelEquip @ 0x159DF0 */
    u8 *pbTransferFlag;              /* bTransferFlag @ 0x159E10 */
} SoldierEquipEnv;

i32 SoldierScreen_EquipSelected(const SoldierEquipEnv *pEnv);

/* --- BaseFacScreen_DrawPlacementOverlay @ 0x58e10 (3396 B) : the placement preview and its commit
 *
 * The selected facility is `pBaseFacPanel->anLineAux[nBaseFacPanelSel]` (0x58E20) and its grid type
 * is `aBaseFacFlags[kind].bFlag21` (0x58E34, LDRSB record + 0x15 — InitUI stores the record index
 * there, so it is the kind). Type 11 is the hangar and it makes the footprint 2x2; the original
 * keeps that as the byte 32 or 0 and adds it to every size and bound below.
 *
 * 1. nSmapTapX / nSmapTapY take the pending pair qword_15887C (0x58E58, 0x58E60). With
 *    bFacPreviewActive (unk_18652C) set they come from the cursor instead:
 *    `tapX = (nCursorX - nOriginX) / 32` and `tapY = (nCursorY - nOriginY - 36) / 32`; the ARM adds
 *    31 to a negative delta before the ASR#5 (0x58EAC..0x58EC8), which is C's truncate-toward-zero
 *    division.
 * 2. A tap outside 0..5 skips the clamp geometry (0x58EE4, 0x58EEC) but still reaches the rest.
 * 3. When a 2x2 would hang off the last row or column the outline is drawn clamped and in the
 *    invalid colour (nIdx = 10*(Map+0x37 + 15), nColorMid = nColorLow = 0): the bottom edge line at
 *    y = nOriginY + 196, the right edge line at x = nOriginX + 160, and both at the corner.
 * 4. The preview block (0x5917C) runs when bFacOverlayShown (unk_15888C) or bFacPreviewActive is
 *    set and the cursor lies inside `0 < dx < 192 - span` and `36 < dy < 227 - span`; it sets
 *    bFacOverlayShown and scores the tap:
 *
 *        footprint = span ? tileA[r][c] + tileA[r+1][c] + tileA[r+1][c+1] + tileA[r][c+1]
 *                         : tileA[r][c]
 *
 *    With an empty footprint the four sides are walked, and a neighbour counts only when its
 *    buildDays byte is 0 (nothing under construction there):
 *
 *        up    (r > 0):           tileA[r-1][c];   span: tileA[r-1][c+1]
 *        down  (r < 5 - span/32): tileA[r+1][c];   span: tileA[r+2][c], tileA[r+2][c+1]
 *        left  (c > 0):           tileA[r][c-1];   span: tileA[r+1][c-1]
 *        right (c < 5 - span/32): tileA[r][c+1];   span: tileA[r][c+2], tileA[r+1][c+2]
 *
 *    The score is a byte (every accumulate is a STRB) and new-base mode (byte_187074[4]) forces it
 *    to 1. Hex-Rays mis-renders two of these against the ARM: the "up" guard is the ROW (0x592C8
 *    `CMP R1,#0`), not the column, and the two span reads below it are +384 = tileA[r+2][c] and
 *    +385 = tileA[r+2][c+1]. The outline is then four rects — nIdx 0 with nColorLow at the phase
 *    colour when the score is non-zero, the other way round when it is zero.
 * 5. The commit (0x599A0..0x59B38) needs a non-zero score, bFacPreviewOk (unk_18652D) set (a zero
 *    score clears it), the tap equal to the pending pair, and bFacPreviewActive. The cost is
 *    `aBaseFacFlags[kind].uCost`; `nFunds - cost < 0` sets the shortfall dword and stops. Otherwise
 *    `BaseFac_SetCellBuildState(grid, nSelectedBaseIdx, bFlag21, tapX, tapY, 0)` writes the facility
 *    and nFunds drops by the cost. In new-base mode the label is BaseFac_FormatCostLabel for
 *    bPendingBaseKind, Base_CreateNamedEntry runs at the pending coordinates,
 *    aNewBaseCostByRace[kind - 0x80] comes off nFunds, BaseFacScreen_Toggle closes the dialog, and
 *    bFacPreviewActive / nGeoscapeScreen = 1 / new-base mode / bFacPreviewOk are cleared.
 *
 * Deviations: the port returns 1 when it committed and 0 otherwise where the original returns
 * register residue; it bounds the panel selection, the kind index and the pending kind where the
 * original indexes anLineAux, aBaseFacFlags and aNewBaseCostByRace unchecked; and it scores only a
 * tap inside 0..5, which is every tap the in-bounds cursor test can produce. */
#define PUFO_FAC_OVERLAY_SPAN 32 /* the byte the original uses for "this facility is 2x2" */

typedef struct FacOverlayEnv {
    u8 *abFacilityGrid;             /* gBaseFacGrid @ 0x159768 */
    UiPanel *pPanel;                /* pBaseFacPanel @ 0x15886C */
    i32 nPanelSel;                  /* nBaseFacPanelSel @ 0x158868 */
    const BaseFacFlag *paFac;       /* aBaseFacFlags @ 0x186E48 */
    i32 *pnFunds;                   /* nFunds @ 0x1598D8 */
    i8 nSelectedBaseIdx;            /* nSelectedBaseIdx @ 0x159D81 */
    i32 *pnTapX;                    /* nSmapTapX @ 0x186B7C */
    i32 *pnTapY;                    /* nSmapTapY @ 0x186B80 */
    i32 nPendingX;                  /* qword_15887C */
    i32 nPendingY;                  /* qword_15887C + 4 */
    i32 nOriginX;                   /* unk_159DC4 */
    i32 nOriginY;                   /* unk_159DC8 */
    i16 nCursorX;                   /* word_186BD0 */
    i16 nCursorY;                   /* word_186BD4 */
    u8 *pbPreviewActive;            /* unk_18652C */
    u8 *pbPreviewOk;                /* unk_18652D */
    u8 *pbOverlayShown;             /* unk_15888C */
    u8 *pbNewBaseMode;              /* byte_187074[4] */
    u8 nPendingKind;                /* bPendingBaseKind @ 0x159DF4 */
    const i32 *paNewBaseCost;       /* g_anNewBaseCostByKind, indexed by nPendingKind - 0x80 */
    u8 nPhaseByte;                  /* Map + 0x37, the cycling outline colour */
    u32 *pdwShortfall;              /* byte_1870C8, written as a dword */
    i32 *pnResidue;                 /* dword_1870B4[0] */
    i32 *pnScreenId;                /* nGeoscapeScreen @ 0x186BC4 */
    const u8 *pTextRecs;            /* the TEXTS blob, for the new-base label */
    char *pszLabel;                 /* szRadarMsg @ 0x186B10 */
    size_t cchLabel;
    i8 *abKindTable;                /* abBaseKindTable @ 0x1864DD */
    const BaseCreateEnv *pCreateEnv;/* Base_CreateNamedEntry's env */
    const BaseFacToggleEnv *pToggleEnv; /* the closing BaseFacScreen_Toggle */
    i32 nPlaceX;                    /* unk_159D94 */
    i32 nPlaceY;                    /* unk_159D98 */
} FacOverlayEnv;

i32 BaseFacScreen_DrawPlacementOverlay(const FacOverlayEnv *pEnv);

#endif /* PUFO_BASEUI_H */
