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

#endif /* PUFO_BASEUI_H */
