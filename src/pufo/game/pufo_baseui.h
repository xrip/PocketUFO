/* pufo_baseui.h - base-screen scratch-cell helpers.
 *
 * Evidence: Options_SetTab4 @ 0x6b6b0,
 *           Options_SetTab3_AndUiY @ 0x6b674,
 *           Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec,
 *           PlaySample7_SetFlags_Cache0xBD8 @ 0x73328.
 *
 * The original writes scattered globals: the options tab byte `gOptionsScratch[0]` @ 0x159E88, two
 * controls' `y` fields (`pGeoscapeCtlB` @ 0x186BB0 / `pGeoscapeCtlA` @ 0x186BB4), and two cached
 * `nUnk3032` copies (`dword_186BE8` and `nResearchTopicRowCache` @ 0x160C74) plus the `unk_160C80` /
 * `byte_187080` flags. The port carries the last four in a ScreenScratch so the helpers are testable
 * without the global bank. The `nUnk3032` source values differ per caller and are passed in.
 *
 * The tab byte and the two `y` fields are NOT in the bundle: they are reached through pointers, onto
 * the run of bytes the caller owns and onto the two control objects. The tab byte is a byte of a
 * block with other live bytes in it -- `gOptionsScratch[4] @ 0x159E8C` is the "the controls exist"
 * flag the frame and the pointer arm read -- so a copy in a bundle would put a second home on it.
 * The writers are the two tab helpers below plus `OptionsScreen_LayoutButtons` 0x6B6D0,
 * `OptionsScreen_BindSaveSlots` 0x6B4A8, `OptionsScreen_BindLoadSlots` 0x6B594,
 * `SaveGame_Slot_CloseUI` 0x6AFA4, `LoadGame_Slot_InitOptions` 0x6B1A4,
 * `OptionsScreen_ReturnToTitle` 0x6B478 and `Options_SaveAudioSettings` 0x6B8A8. The readers are
 * `Geoscape_DrawScreen` 0x6C68C, `Geoscape_CloseUI` 0x8BCD0 and `Options_SaveAudioSettings` 0x6B7E8.
 * `Options_SetTab3_AndUiY` writes the two `y` fields with `STRH` at 0x6B68C and 0x6B69C, onto the
 * controls themselves -- a copy in a bundle is read by nobody, because the draw reads `y` out of the
 * control (the `LDRSH [R1,#2]` at 0x6C5B4, 0x6CBE0, 0x6CED0).
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
#include "game/pufo_save.h"      /* DatFile, SaveGameInput, LoadGameEnv, the save/load slot handlers */

/* The screen frame rails, shared by the four screens that draw the same 24+24 step border:
 * TransferScreen_Draw (0x97FB8..0x98150), ManufScreen_DrawFrame (0x6F9C4..0x6FB54),
 * ResearchScreen_Draw (0x73A54..0x73BD8) and BasePlaneScreen_Draw (0x7D6AC..0x7D770). The ARM is
 * identical in all four -- same loop bounds (CMP #0xA8 over a step of 7, CMP #0x78 over a step of 5),
 * same x/y pairs, same ramp formulas -- and the three index values of every blit are stored with STRB,
 * so each index is the low byte of the arithmetic: `RSB R5,R3,#0xE6` and `RSB R5,R3,#0xFA` are
 * 230 - 5k and 250 - 5k as bytes. */
#define PUFO_FRAME_RAIL_STEPS     24  /* both loops run 24 times */
#define PUFO_FRAME_RAMP_BASE      20  /* the climbing ramp is 5 * (k + 20), so it starts at 100 */
#define PUFO_FRAME_RAMP_STEP      5   /* the ramp index steps by 5 per rail segment */
#define PUFO_FRAME_RAMP_SIDE    0xE6  /* the descending side rail, 230 down to 115 */
#define PUFO_FRAME_RAMP_EDGE    0xFA  /* the descending top/bottom rails, 250 down to 135 */

/* The rails themselves, in pufo_baseui.c. The hangar draws them too, so they are not file-local. */
void BaseUi_DrawFrameRails(void);

typedef struct ScreenScratch {
    i32 nCache186BE8;  /* dword_186BE8 */
    u8   bFlag160C80;  /* unk_160C80 / bUiRowSelected: a byte global -- STRB at 0x73334, LDRB at 0x88B18 */
    u8  bFlag187080;   /* byte_187080 */
    i32 nCache160C74;  /* unk_160C74 */
} ScreenScratch;

/* Options_SetTab4 @ 0x6b6b0: STRB 4 to gOptionsScratch[0], and nothing else. */
void BaseUi_SetScratch4(i8 *pbTab);

/* Options_SetTab3_AndUiY @ 0x6b674: STRB 3 to gOptionsScratch[0], then STRH 120 to *pGeoscapeCtlB's
 * y (0x6B68C) and STRH 142 to *pGeoscapeCtlA's y (0x6B69C). Those are the two hotspots the
 * quit-confirmation page draws its Да under (ctlB) and its Нет under (ctlA). */
void BaseUi_SetScratch3AndUiY(i8 *pbTab, UiControl *pCtlB, UiControl *pCtlA);

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
 * and hands the options screen the LITERAL mode -1: 0x84EC8 is `MOV R1,#0xFFFFFFFF`
 * before the tail call at 0x84EF4, and the callee (OptionsScreen_OpenWithSound
 * 0x6dd04) stores that R1 and never reads the first argument (0x6DD08). Part 102
 * re-read the function; the old "anLineAux[119]" reading was wrong, and no code
 * ever wrote that scratch word. */
typedef struct GeoscapeScreenState {
    i32 nGeoscapeScreen;   /* nGeoscapeScreen */
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

/* BuySellScreen_ListPanelPrev @ 0x79a20 / ListPanelNext @ 0x79a48: 40 bytes = 8 instructions + 2 pool words,
 * the same two-panel shape as the soldier screen, over pPlaneCrewPanel @ 0x161EDC first and
 * pPlaneCrewStatusPanel @ 0x161A3C second. */
void BuySellScreen_ListPanelPrev(UiPanel *pFirst, UiPanel *pSecond);
void BuySellScreen_ListPanelNext(UiPanel *pFirst, UiPanel *pSecond);

/* ------------------------------------------------- the rest of the panel scroll family
 *
 * Evidence: ManufScreen_PanelPrev @ 0x6dd4c / PanelNext @ 0x6dd94, PsiScreen_PanelPrev @
 * 0x715e8 / PanelNext @ 0x71648, BuySellScreen_PanelPrev @ 0x76088 / PanelNext @ 0x760e0,
 * BuySellScreen_DetailPanelPrev @ 0x79a70 / DetailPanelNext @ 0x79ac8, Transfer_ScrollPrevAll
 * @ 0x967e4 / ScrollNextAll @ 0x9683c.
 *
 * Ten handlers, and every one is the same loop: read the panel pointer out of its global and
 * run the already ported scroll mover on it -- UiPanel_ScrollPrev (DecDwordIfPos_AtPlus0xBD4
 * @ 0x3f040) for the previous button, UiPanel_ScrollNext (IncDwordIfInRange_AtPlus0xBD4
 * @ 0x3f00c) for the next. Nothing else: no redraw, no return value, no wrap. Only the last
 * mover call in each body is a tail call (B rather than BL, with POP {LR} in front of it), which
 * is the sole difference from the calls before it.
 *
 * Records, measured 2026-10-23 by walking each member (instructions + pool words, no two-word
 * MOV in this family): BaseFac pair 16 B = 3 + 1, the only pair that moves ONE panel; the crew
 * pair @ 0x79a20/0x79a48 40 B = 8 + 2; the research pair @ 0x72e30/0x72e68 56 B = 11 + 3; the
 * manuf pair 72 B = 14 + 4; the psi pair 96 B = 19 + 5; the buy/sell pair and the transfer pair
 * and the hangar detail pair all 88 B = 17 + 5. Prev and next of one pair are the same size and
 * the same shape.
 *
 * The panel sets, in the order each handler walks them:
 *
 *   ManufScreen_Panel*          pManufProductPanel @ 0x15EE44, pManufPanelB @ 0x15EE48,
 *                               pManufPanelC @ 0x15EE40, pManufPanelD @ 0x15EE38
 *   PsiScreen_Panel*            g_apPsiNamePanel @ 0x15EED0, g_apPsiStatPanel @ 0x15EE88,
 *                               g_apPsiDeltaPanel @ 0x15EEA8, g_apPsiCheckPanel @ 0x15EE58 -- four
 *                               parallel arrays, one entry per base, all indexed by the SIGNED byte at
 *                               byte_1870F4 (the psi screen's selected base), which the body re-reads
 *                               before every step (0x715F4, 0x71600, 0x71610, 0x71620), so the handler picks
 *                               one entry out of every array for that base. The loops that walk
 *                               them (PsiScreen_ApplyAssignments, 0x72250, ported at
 *                               pufo_baseui.c:12142) bound the
 *                               index by the base count at 0x159D80, at most PUFO_FAC_BASE_COUNT.
 *   BuySellScreen_Panel*        pBuySellPanelA @ 0x161A1C, pBuySellPanelB @ 0x161A00,
 *                               pBuySellPanelC @ 0x161A20, pBuySellPanelD @ 0x160C88,
 *                               pBuySellPanelE @ 0x1615F8
 *   BuySellScreen_DetailPanel*  pPlaneSlotPanel @ 0x161AC8, pPlaneSelRowPanel @ 0x161F00,
 *                               pPlaneCommitRowPanel @ 0x161EEC, pPlaneTakenPanel @ 0x161EF4,
 *                               pPlaneAvailPanel @ 0x161A40
 *   Transfer_Scroll*            the same five globals as BuySellScreen_Panel*, same order
 *
 * BuySellScreen_PanelPrev @ 0x76088 and Transfer_ScrollPrevAll @ 0x967e4 are the same seventeen
 * instructions over the same five globals -- re-measured 2026-10-23 word by word: 5 of their 22 words
 * differ and all five are branch displacements (four BL plus the tail B); the 17 instructions and the
 * 5 pool words are the same. An earlier note here said "eleven instructions", which undercounts the
 * record (88 B = 17 insns + 5 pool). The next pair (0x760e0 / 0x9683c) is the same case. The port
 * keeps one implementation of each and cites both addresses; the pair is on record in
 * docs/parity/renames.tsv.
 *
 * The port takes the panels as arguments instead of reaching through the globals, as the
 * earlier handlers in this family do. PsiScreen_Panel* takes the four panels already resolved
 * for the selected base: the byte read is the caller's, not this function's. */
void ManufScreen_PanelPrev(UiPanel *const apPanels[4]);
void ManufScreen_PanelNext(UiPanel *const apPanels[4]);
void PsiScreen_PanelPrev(UiPanel *const apPanels[4]);
void PsiScreen_PanelNext(UiPanel *const apPanels[4]);
void BuySellScreen_PanelPrev(UiPanel *const apPanels[5]);
void BuySellScreen_PanelNext(UiPanel *const apPanels[5]);
void BuySellScreen_DetailPanelPrev(UiPanel *const apPanels[5]);
void BuySellScreen_DetailPanelNext(UiPanel *const apPanels[5]);

/* BuySell_RoleToItemId @ 0x76d88. The object slot of the item a buy/sell row stands for, from
 * the SIGNED role byte the row carries (BuySellScreen_BuildPanels reads it with LDRSB at
 * 0x77964 and stores the result into the row panel at +0x1380):
 *
 *   0 -> 117, 1 -> 120, 2 -> 121, 3 -> 118, 4 -> 119, anything else -> 255
 *
 * The argument is sign-extended from a byte first (0x76D88..0x76D8C), so a negative role takes
 * the 255 branch too. 117..121 are the five craft-type item slots the buy/sell screen sells, and
 * the shipped names are now known (part 18 fixed the name mapping): 117 "SKYRANGER", 118
 * "INTERCEPTOR", 119 "FIRE STORM", 120 "LIGTHNING" (sic), 121 "AVENGER". So role 0 is the
 * Skyranger, 1 the Lightning, 2 the Avenger, 3 the Interceptor and 4 the Fire Storm.
 *
 * Transfer_RoleToIconId @ 0x96ad8 is the same function byte for byte -- same 88 bytes, same jump
 * table, same 255 fall-through -- so the transfer screen shares this one port. */
i32 BuySell_RoleToItemId(i32 nRole);

/* ------------------------------------------------- the alien race ids
 *
 * Evidence: BuySell_ItemIdToAlienRaceId @ 0x6ad30 (292 B), renamed in the IDB from
 * BuySell_ItemIdToCategory. Its only reader is BuySellScreen_CommitTransaction (0x74bf8, not
 * ported).
 *
 * The old name was wrong and the IDB now says so: the function does NOT read
 * ObjectRec.nCategory @ 0xB8. That field is 0 for every slot the function maps (255 for
 * 145..147), so it could not be the source. What the function returns is the alien RACE id
 * of the item in that object slot, or -1 for any other slot. The comparisons are signed
 * (BGT / BEQ / BNE, no unsigned branch), so a negative slot takes the -1 branch.
 *
 * The race reading is verified two ways. First against the shipped OBJECTS.DAT names
 * (main.dat, 149 records of 208 bytes, the name at record+0x00): 77..81 are the five Sectoid
 * ranks, 82..86 Snakeman, 87..91 Floater, 92..94 Muton, 95..98 Ethereal, 99 Cyberdisk,
 * 100 Chryssalid, 101 Reaper, 102 Silacoid, 103 Celatid, 104 Sectopod, 114 is a sixth Ethereal
 * rank and 144..148 are the five race medics. The function's groups cut exactly along those
 * race boundaries, and 114 shares Ethereal's id with 95..98 while each medic shares its own
 * race's id.
 *
 * Second against the consumer. BuySellScreen_CommitTransaction turns the returned id into a
 * record pointer through a switch (0x74CA0), and the eleven pointers are apObjects[59..69] --
 * Map+0x1F4..0x21C, part of the apObjects array at Map+0x108, not a separate table. Those
 * eleven records are the alien corpses: 59 Sectoid, 60 Snakeman, 61 Floater, 62 Muton,
 * 63 Ethereal, 64 Cyberdisk, 65 Chryssalid, 66 Reaper, 67 Celatid, 68 Silacoid, 69 Sectopod.
 * So the commit moves the bought item into the base and the matching race corpse out of it.
 * Smap_GrantSiteRecovery (0x98b50, ported at pufo_baseui.c:6120 and driven hourly by
 * World_HookGrantSiteRecovery) hard-codes the same pairing per slot instead of going
 * through the race id (77..81 -> 59, 82..86 -> 60, 87..91 -> 61, 92..94 -> 62, 95..98 and 114
 * -> 63, 99 -> 64, 100 -> 65, 101 -> 66, 103 -> 67, 102 -> 68, 104 -> 69), which is what
 * pins the ids to races. Ids 16 and 17 are not used by this map. */
typedef enum PufoAlienRaceId {
    PUFO_ALIEN_RACE_SECTOID    = 9,   /* apObjects[59] "Sectoid corpse" */
    PUFO_ALIEN_RACE_MUTON      = 10,  /* apObjects[62] "Muton corpse" */
    PUFO_ALIEN_RACE_FLOATER    = 11,  /* apObjects[61] "Floater corpse" */
    PUFO_ALIEN_RACE_SNAKEMAN   = 12,  /* apObjects[60] "Snakeman corpse" */
    PUFO_ALIEN_RACE_ETHEREAL   = 13,  /* apObjects[63] "Ethereal corpse" */
    PUFO_ALIEN_RACE_CHRYSSALID = 14,  /* apObjects[65] "Chryssalid corpse" */
    PUFO_ALIEN_RACE_CELATID    = 15,  /* apObjects[67] "Celatid corpse" */
    PUFO_ALIEN_RACE_CYBERDISK  = 18,  /* apObjects[64] "Cyberdisk remains" */
    PUFO_ALIEN_RACE_SILACOID   = 19,  /* apObjects[68] "Silacoid corpse" */
    PUFO_ALIEN_RACE_REAPER     = 20,  /* apObjects[66] "Reaper corpse" */
    PUFO_ALIEN_RACE_SECTOPOD   = 21,  /* apObjects[69] "Sectopod remains" */
    PUFO_ALIEN_RACE_NONE       = -1   /* the slot has no race behind it */
} PufoAlienRaceId;

PufoAlienRaceId BuySell_ItemIdToAlienRaceId(i32 nSlotId);

/* The row-kind marker the buy/sell screen writes into pBuySellPanelA.anLineAux and reads back in
 * every one of its handlers -- see the BuySellScreen_BuildPanels block for how it is produced. The
 * IDB carries the same enum as `PufoBuySellRowMarker`; its two older members are named
 * BUYSELL_ROW_SOLDIER (1000) and BUYSELL_ROW_ALIEN_BIAS (2000), which the IDB rename tool cannot
 * retitle because enum members are not globals. */
typedef enum PufoBuySellRowMarker {
    PUFO_ROW_ENGINEER      = 112, /* the crew slot, Base_GetEngineerCount's item */
    PUFO_ROW_SCIENTIST     = 113, /* and Base_GetScientistCount's */
    PUFO_ROW_CRAFT_FIRST   = 117, /* BuySell_RoleToItemId's five craft slots are 117..121 */
    PUFO_ROW_CRAFT_SECOND  = 118, /* the buy screen offers only 117 Skyranger and 118 Interceptor */
    PUFO_ROW_CRAFT_LAST    = 121,
    PUFO_ROW_SOLDIER       = 1000, /* a soldier row, the one marker that is not an object slot */
    PUFO_ROW_ALIEN_OFFSET  = 2000  /* an alien row carries its object slot plus this */
} PufoBuySellRowMarker;

/* ------------------------------------------------- the screen-id scroll dispatcher
 *
 * Evidence: Geoscape_UiFocusPrev @ 0x8afa0 (596 B) and Geoscape_UiFocusNext @ 0x8b1f4
 * (596 B). Their only callers are Input_OnUp @ 0x118b0 and Input_OnDown @ 0x119b0, and both
 * discard the result, so the port returns void.
 *
 * Each one switches on the screen id at nGeoscapeScreen @ 0x186BC4 and runs the mover over
 * that screen's panel set: UiPanel_ScrollPrev (DecDwordIfPos_AtPlus0xBD4 @ 0x3f040) for the
 * previous button, UiPanel_ScrollNext (IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c) for the next.
 * The sets are the same ones the per-screen handlers in this file already walk, which is why
 * the dispatcher reads as a list of them:
 *
 *   0, 2   no panel at all: set one flag byte and return. Prev sets unk_1864F4, Next sets
 *          unk_1864F6, and BOTH return the address of unk_1864F4 -- the byte Next writes is
 *          not the byte it hands back. Geoscape_SetPanelFlagA/B @ 0x8b448 / 0x8b478 write the
 *          same two bytes. Geoscape_PulseUpdate @ 0x954dc (ported in pufo_geoscape.c) reads them. What the two
 *          bytes mean is not established here.
 *   4      pSolPanelNames @ 0x15975C then pSolPanelCraft @ 0x159DEC  (the soldier screen)
 *   6      the five hangar panels, but only while bPlaneStoresCommitted @ 0x161F28 is set;
 *          then, if bPlaneListPanelsBuilt @ 0x161F24 is set, pPlaneCrewPanel @ 0x161EDC and
 *          pPlaneCrewStatusPanel @ 0x161A3C. With the built flag clear the handler stops
 *          there, so a hangar that has not built its lists scrolls nothing past the five.
 *   7      pBaseFacPanel @ 0x15886C only
 *   8      pResearchTopicPanel @ 0x160C68, pResearchPanelB @ 0x15EEFC, pResearchPanelC @ 0x15EF00
 *   9      pManufProductPanel @ 0x15EE44, pManufPanelB/C/D @ 0x15EE48, 0x15EE40, 0x15EE38
 *   10,11,12  the five buy/sell panels, same order as BuySellScreen_Panel* -- the transfer
 *          screen and both buy/sell variants share one panel set, which is the same fact the
 *          merged Transfer_Scroll* pair records
 *   16     unk_187200 only
 *   17     the four psi arrays at the selected base, same order as PsiScreen_Panel*
 *   other  nothing
 *
 * Deviation: the original hands back a pointer (the mover's result, or &unk_1864F4 for the
 * 0/2 case) and both callers ignore it. The port returns void. */
typedef struct GeoscapeFocusEnv {
    i32 nScreenId;                    /* nGeoscapeScreen @ 0x186BC4 */
    UiPanel *pSolPanelNames;          /* 0x15975C */
    UiPanel *pSolPanelCraft;          /* 0x159DEC */
    u8 bPlaneStoresCommitted;         /* bPlaneStoresCommitted @ 0x161F28 */
    u8 bPlaneListPanelsBuilt;         /* bPlaneListPanelsBuilt @ 0x161F24 */
    UiPanel *apPlanePanels[5];        /* pPlaneSlotPanel 0x161AC8, pPlaneSelRowPanel 0x161F00,
                                       * pPlaneCommitRowPanel 0x161EEC, pPlaneTakenPanel
                                       * 0x161EF4, pPlaneAvailPanel 0x161A40 */
    UiPanel *pPlaneCrewPanel;         /* 0x161EDC */
    UiPanel *pPlaneCrewStatusPanel;   /* 0x161A3C */
    UiPanel *pBaseFacPanel;           /* 0x15886C */
    UiPanel *apResearchPanels[3];     /* pResearchTopicPanel 0x160C68, pResearchPanelB 0x15EEFC,
                                       * pResearchPanelC 0x15EF00 */
    UiPanel *apManufPanels[4];        /* pManufProductPanel 0x15EE44, pManufPanelB 0x15EE48,
                                       * pManufPanelC 0x15EE40, pManufPanelD 0x15EE38 */
    UiPanel *apBuySellPanels[5];      /* pBuySellPanelA..E at 0x161A1C, 0x161A00, 0x161A20,
                                       * 0x160C88, 0x1615F8 */
    UiPanel *apPsiPanels[4];          /* same set as PsiScreen_Panel*, already resolved for
                                       * the base byte_1870F4 selects */
    UiPanel *pScreen16Panel;          /* unk_187200 */
    u8 *pbFocusFlagA;                 /* unk_1864F4, the byte the prev case sets */
    u8 *pbFocusFlagB;                 /* unk_1864F6, the byte the next case sets */
} GeoscapeFocusEnv;

void Geoscape_UiFocusPrev(GeoscapeFocusEnv *pEnv);
void Geoscape_UiFocusNext(GeoscapeFocusEnv *pEnv);

/* ------------------------------------------------- the buy/sell list panels
 *
 * Evidence: BuySellScreen_BuildPanels @ 0x76de0 (4236 B). The branch selector is the byte the
 * IDB now names bBuySellPanelsBuilt @ 0x161A28; BuySellScreen_Draw @ 0x77e6c reads it too.
 *
 * Set -- the teardown branch (0x77D60..0x77E50): clear the flag, release the four screen
 * controls at 0x161A0C..0x161A18 with UiControl_FreePads + delete, release the five panels with
 * UiPanel_FreeLines + delete in the order A, B, E, C, D, and finally roll the funds back: if
 * BuySell_nFundsSnapshot @ 0x161460 differs from nFunds @ 0x1598D8, nFunds takes the snapshot.
 * The screen edits nFunds as the player moves quantities and this is where an uncommitted
 * transaction is undone.
 *
 * Clear -- the build branch, in order:
 *
 *  1. Four UiControl objects (new 0x20 + UiControl_ctor) into 0x161A0C..0x161A18. The first two
 *     get pfnClick = BuySellScreen_PanelPrev / PanelNext before any Setup runs, and all four are
 *     then UiControl_Setup(ctl, 0, 0, stru_ABBA0[i]) -- "up", "down", "bclose", "emptyb".
 *  2. dword_161468[0] = 0 and g_aSoldierIdList[0] = 0: the assigned-ship count and the scratch
 *     soldier-id list Base_RemoveAllSoldiers @ 0x668e8 consumes.
 *  3. Three counting passes over the selected base, all adding into one row total:
 *       soldiers  Soldier.nKind == base AND Soldier.bUnk183 == 255 (not assigned to a craft)
 *       craft     Ship.bHomeBase == base AND Craft_CountSoldiersAboard == 0 AND Ship.bAtBase != 0
 *       objects   slots 1..nObjectCount-1 kept by a per-slot include flag:
 *                   1..69, 105..111, 122..124, 142, 143 when ObjectRecFull.auPerBaseVal[base] != 0
 *                   112 when Base_GetEngineerCount > 0, 113 when Base_GetScientistCount > 0
 *       aliens    77..104, 144..148, 114 when auPerBaseVal[base] != 0
 *  4. Five UiPanel objects (new 0x1BB4 + UiPanel_ctor(row total)), each nStyle 13 and nFont 16,
 *     laid out as the table in the .c comment.
 *  5. Three filling passes in the same order, writing row `nRow` of every panel:
 *       panel A  the name, and anLineAux as the row-kind marker -- 1000 for a soldier,
 *                BuySell_RoleToItemId(Ship.bCraftType) (117..121) for a craft, the slot id for a
 *                plain object, and slot + 2000 for an alien
 *       panel B  "<" (the decrement column), pfnClick BuySellScreen_FillBuyPanel
 *       panel C  ">" (the increment column), pfnClick BuySellScreen_FillSellPanel
 *       panel D  the available count: 1 for a soldier or a craft, auPerBaseVal[base] for an
 *                object, the engineer or scientist count for slots 112 / 113
 *       panel E  the selection flag, 0 for every row here; BuySellScreen_CommitTransaction below
 *                reads it back as "== 1" to find the rows the player marked
 *     Names are copied through an 18-byte buffer and cut to 17 characters when longer; the
 *     craft row is the exception -- its text goes straight into the line as
 *     TEXTS[415 + bCraftType] + "-" + itoa(Ship.bType), with no truncation.
 *  6. A sort pass: for each value of g_aBuySellRowOrder @ 0xAAA04 (the IDB name, 148 dwords read
 *     up to nObjectCount-1 of them) find the panel A row whose anLineAux equals it and swap that
 *     row into the next free position in all five panels. The table lists 1000, 112, 113,
 *     117..121, the plain object slots and the alien slot numbers 77..104 / 144..148 / 114 -- but
 *     alien rows carry slot + 2000, so those 34 entries never match and the aliens keep their
 *     fill order at the end. That is the original's own inconsistency, reproduced.
 *
 * Deviation: the original's name buffer is 17 bytes plus one more at 0x77730, and the copy loop
 * also copies the terminator, so a name of 18 characters or more writes one byte past it before
 * the strlen test cuts the string to 17. The port bounds the copy instead. */
#define PUFO_CRAFT_NAME_TEXT_REC 415 /* TEXTS[415..419] = Skyranger, Lightning, Avenger,
                                      * Interceptor, Fire Storm -- one per CraftType, which is
                                      * 0x77904's 205 * (byte)[2] index and the same role
                                      * BuySell_RoleToItemId maps to object slots 117..121 */

/* Staff allocation pointers are full per-base tables (manufacturing200,
 * research144 words per base), not pre-offset rows. */
typedef struct BuySellBuildEnv {
    u8  *pbPanelsBuilt;             /* bBuySellPanelsBuilt @ 0x161A28 */
    UiControl *apCtl[4];            /* g_aBuySellCtl @ 0x161A0C, 0x161A10, 0x161A14, 0x161A18 */
    UiPanel **ppPanelA;             /* pBuySellPanelA @ 0x161A1C, the name column */
    UiPanel **ppPanelB;             /* pBuySellPanelB @ 0x161A00, the "<" column */
    UiPanel **ppPanelC;             /* pBuySellPanelC @ 0x161A20, the ">" column */
    UiPanel **ppPanelD;             /* pBuySellPanelD @ 0x160C88, the available count */
    UiPanel **ppPanelE;             /* pBuySellPanelE @ 0x1615F8, the pending quantity */
    Soldier *const *apSoldiers;     /* Map + 0x1198 */
    i32 nSoldierCount;              /* nSoldierCount @ 0x115350 */
    Ship *const *apCrafts;          /* Map + 0x10AC */
    i32 nCraftCount;                /* Map_nCraftCount @ 0x114B4C */
    ObjectRecFull *const *apObjects;/* Map + 0x108 */
    i32 nObjectCount;               /* Map_nObjectCount @ 0x114A60 */
    i8 nSelectedBaseIdx;            /* nSelectedBaseIdx @ 0x159D81 */
    i32 *pnAssignedShipCount;       /* dword_161468 */
    i32 *pnSoldierIdCount;          /* g_aSoldierIdList @ 0x160C90, its count dword */
    i32 *pnFunds;                   /* nFunds @ 0x1598D8 */
    const i32 *pnFundsSnapshot;     /* BuySell_nFundsSnapshot @ 0x161460, reached live: the teardown's
                                     * rollback reads the global at the call (0x77E40), not a copy */
    const char *pTextRecs;          /* the loaded TEXTS.DAT records, PUFO_TEXT_REC_STRIDE apart */
    const u16 *apManufCol;          /* the engineer column @ 0x15E19C */
    const u16 *apResearchCol;       /* the scientist column @ 0x16022C */
    i32 nCrewEntries;               /* the crew column entry count byte @ 0x160C4C */
    void (*pfnSyncSelection)(void); /* BuySellScreen_SyncSelection @ 0x76138 */
    void (*pfnFillBuyPanel)(void);  /* BuySellScreen_FillBuyPanel @ 0x75160 */
    void (*pfnFillSellPanel)(void); /* BuySellScreen_FillSellPanel @ 0x75464 */
    void (*pfnPanelPrev)(void);     /* BuySellScreen_PanelPrev @ 0x76088 */
    void (*pfnPanelNext)(void);     /* BuySellScreen_PanelNext @ 0x760e0 */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
} BuySellBuildEnv;

void BuySellScreen_BuildPanels(BuySellBuildEnv *pEnv);

/* ------------------------------------------------- the buy/sell commit
 *
 * Evidence: BuySellScreen_CommitTransaction @ 0x74bf8 (1176 B), wired as the click handler of
 * screen 12 by BuySellScreen_Init @ 0x78c30.
 *
 * Six phases, in order:
 *
 *  1. 0x74C0C..0x74D74. For every object slot 0..nObjectCount-1, apply the pending delta the
 *     screen accumulated in unk_161600 (one dword per slot, cleared by BuySellScreen_Init):
 *     `BaseRec_SubItemQty(base, apObjects[slot], -delta)`, which is `auPerBaseVal[base] += delta`
 *     and `nStoresTotal += delta`. If the slot has an alien race behind it
 *     (BuySell_ItemIdToAlienRaceId), the same slot's race corpse moves the other way:
 *     `BaseRec_AddItemQty(base, apObjects[corpse], -delta)`. The corpse pairing is the switch at
 *     0x74CA0 and it is the one the PufoAlienRaceId block above records.
 *  2. 0x74D88..0x74E64. Walk panel A; a row whose aux is 117..121 (a craft) and whose panel E aux
 *     is 1 (selected) contributes `Ship_FindIndexById(panelC aux)` -- panel C holds Ship.bId on a
 *     craft row -- into the assigned-ship list at dword_161468[1..], with the count in [0].
 *  3. 0x74E70..0x74F14. For each of those craft indices, and for every object slot, move what the
 *     craft carries into the base: `apObjects[slot]->aItemQty[craftIndex]` is added to the base and
 *     subtracted from the craft. So confirming the screen unloads every selected craft.
 *  4. 0x74F38..0x74FB0. Walk panel A again; a row whose aux is 1000 (a soldier) AND whose panel E
 *     aux is 1 contributes its panel C aux -- the soldier's roster id at Soldier + 0x1B8 -- into
 *     the scratch list g_aSoldierIdList[1..], with the count in [0].
 *  5. 0x74FB8..0x75064. For each of those roster ids, find the soldier whose nUnk440 matches and,
 *     when the soldier wears armour (Soldier.nUnitType, the same byte SoldierScreen_EquipSelected
 *     writes), give that armour back to the base: `apObjects[121 + nUnitType]` +1.
 *  6. 0x75070..0x75078. Take the funds snapshot (`BuySell_nFundsSnapshot = nFunds`, the value
 *     BuildPanels' teardown later restores) and tail-call Geoscape_CloseUI on the screen block at
 *     0x1864F8 -- the same block whose two flag bytes the scroll dispatcher sets.
 *
 * Deviation: the original indexes `apObjects[121 + Soldier.nUnitType]` without bounding the byte,
 * and dereferences the panel and array pointers without a null test. The port bounds the armour
 * index to 1..3 and skips null slots. */
typedef struct BuySellCommitEnv {
    const i32 *anSlotDelta;         /* unk_161600, one pending delta per object slot */
    UiPanel *pPanelA;               /* pBuySellPanelA @ 0x161A1C */
    UiPanel *pPanelC;               /* pBuySellPanelC @ 0x161A20 */
    UiPanel *pPanelE;               /* pBuySellPanelE @ 0x1615F8, the selection flag */
    BaseRec *pBase;                 /* apBases[nSelectedBaseIdx] */
    ObjectRecFull *const *apObjects;/* Map + 0x108 */
    i32 nObjectCount;               /* Map_nObjectCount @ 0x114A60 */
    Ship *const *apCrafts;          /* Map + 0x10AC */
    i32 nCraftCount;                /* Map_nCraftCount @ 0x114B4C */
    Soldier *const *apSoldiers;     /* Map + 0x1198 */
    i32 nSoldierCount;              /* nSoldierCount @ 0x115350 */
    i32 *pnAssignedCount;           /* dword_161468[0] */
    i32 *anAssignedList;            /* dword_161468[1..] */
    i32 nAssignedCapacity;
    i32 *pnSoldierIdCount;          /* g_aSoldierIdList[0] @ 0x160C90 */
    i32 *anSoldierIdList;           /* g_aSoldierIdList[1..] */
    i32 nSoldierIdCapacity;
    i32 *pnFundsSnapshot;           /* BuySell_nFundsSnapshot @ 0x161460 */
    i32 nFunds;                     /* nFunds @ 0x1598D8, read only */
    const struct GeoscapeCloseEnv *pClose; /* Geoscape_CloseUI @ 0x8b94c, tail call at 0x75078.
                                            * The full type is declared with the dispatcher. */
} BuySellCommitEnv;

void BuySellScreen_CommitTransaction(BuySellCommitEnv *pEnv);

/* ------------------------------------------------- the buy/sell row editors
 *
 * Evidence: BuySellScreen_FillBuyPanel @ 0x75160 (772 B), the click handler of panel B (the "<"
 * column), and BuySellScreen_FillSellPanel @ 0x75464 (1044 B), the handler of panel C (the ">").
 * Both take the row from their own panel's nUnk3032 and branch on panel A's aux.
 *
 * What each column holds, now settled from both ends:
 *   panel A aux  the row kind -- 1000 soldier, 117..121 craft, slot id plain object, slot+2000 alien
 *   panel D aux  what the base still has to give: auPerBaseVal[base] (or the crew count), moved by
 *                one per unit the player marks
 *   panel E aux  for a soldier or craft row the selection flag (0 or 1); for an alien row the count
 *                of units marked; for a plain object row the money the marks have moved
 *   panel C aux  the soldier's roster id, or Ship.bId
 *
 * ">" (FillSellPanel), per row kind:
 *   soldier  needs panel D aux > 0; highlights the row (100,255,100 / 10,10,50), sets panel E aux
 *            to 1, decrements panel D aux
 *   craft    the same, and it is the 1 CommitTransaction looks for
 *   alien    needs panel D aux > 0; `g_anBuySellSlotDelta[aux - 2000]--`, panel E aux += 1,
 *            panel D aux -= 1
 *   object   a repeat loop (see the deviation note) that while `delta[slot] + auPerBaseVal[base]`
 *            is positive decrements the delta, decrements panel D aux, and either adds the price to
 *            the transaction total unk_161A2C and to nFunds and adds the price to panel E aux, or
 *            for slots 112 / 113 uses the crew getter as the availability test, highlights the row
 *            and sets panel E aux to `-delta[slot]`
 *
 * "<" (FillBuyPanel) undoes it: a soldier or craft row needs panel E aux > 0, highlights the row
 * (240,240,240 / 20,20,20), clears panel E aux and puts one back into panel D aux; an alien row
 * needs panel E aux > 0, then `delta[aux - 2000]++`, panel E aux -= 1, panel D aux += 1; a plain
 * object row repeats while panel E aux > 0, putting one back into panel D aux, incrementing the
 * delta, and either taking the price back out of the transaction total and nFunds and off panel E
 * aux, or for 112 / 113 highlighting the row and stepping panel E aux by one.
 *
 * ORIGINAL BUG, reproduced in both BuySellScreen_FillBuyPanel @ 0x75160 and
 * BuySellScreen_FillSellPanel @ 0x75464: the two repeat loops bound themselves by
 * `nRadarPulseTimer / 10` (0xAC224) -- the radar screen's pulse countdown, which the radar click
 * sets to 250 and the radar range bar draws. Nothing in the buy/sell screen owns it, so how many
 * units one click moves depends on a timer from another screen. Recorded in docs/parity/bugs.tsv. */
#define PUFO_BUYSELL_META_BUY_A 240 /* FillBuyPanel's row highlight, triple one */
#define PUFO_BUYSELL_META_BUY_B  20 /* and triple two */
#define PUFO_BUYSELL_META_SELL_A 100 /* FillSellPanel's highlight: (100,255,100) */
#define PUFO_BUYSELL_META_SELL_G 255
#define PUFO_BUYSELL_META_SELL_B  10 /* and (10,10,50) */
#define PUFO_BUYSELL_META_SELL_BLUE 50

/* Staff allocation pointers are full per-base tables, as in BuySellBuildEnv. */
typedef struct BuySellRowEditEnv {
    i32 *pnRadarPulseTimer;         /* nRadarPulseTimer @ 0xAC224, the repeat bound */
    UiPanel *pPanelA;               /* pBuySellPanelA @ 0x161A1C */
    UiPanel *pPanelB;               /* pBuySellPanelB @ 0x161A00, the "<" column */
    UiPanel *pPanelC;               /* pBuySellPanelC @ 0x161A20, the ">" column */
    UiPanel *pPanelD;               /* pBuySellPanelD @ 0x160C88, the available count */
    UiPanel *pPanelE;               /* pBuySellPanelE @ 0x1615F8, the selection / money column */
    i32 *anSlotDelta;               /* g_anBuySellSlotDelta @ 0x161600 */
    i32 nSlotDeltaCount;
    BaseRec *pBase;                 /* apBases[nSelectedBaseIdx] */
    ObjectRecFull *const *apObjects;/* Map + 0x108 */
    i32 nObjectCount;
    i8 nSelectedBaseIdx;            /* nSelectedBaseIdx @ 0x159D81 */
    i32 *pnTransTotal;              /* unk_161A2C, the transaction total the screen shows */
    i32 *pnFunds;                   /* nFunds @ 0x1598D8 */
    const u16 *apManufCol;          /* the engineer column @ 0x15E19C */
    const u16 *apResearchCol;       /* the scientist column @ 0x16022C */
    i32 nCrewEntries;               /* the crew column entry count byte @ 0x160C4C */
} BuySellRowEditEnv;

void BuySellScreen_FillBuyPanel(BuySellRowEditEnv *pEnv);  /* panel B, the "<" column */
void BuySellScreen_FillSellPanel(BuySellRowEditEnv *pEnv); /* panel C, the ">" column */

/* ------------------------------------------------- the buy screen's capacity gate
 *
 * Evidence: BuySellScreen_ValidateCapacity @ 0x75b88 (1280 B) and BuySellScreen_UpdatePrices
 * @ 0x75878 (784 B) -- the two click handlers BuySellScreen_BuildControls installs into the panel
 * click slot at +0xBCC: UpdatePrices onto pBuySellPanelB (the "<" column, 0x765CC) and
 * ValidateCapacity onto pBuySellPanelC (the ">" column, 0x766EC).
 *
 * Both work on the row the screen has selected: the cached row index at unk_186BE8, whose kind is
 * `pBuySellPanelA.anLineAux[nRow]` (PufoBuySellRowMarker), and both repeat
 * `nRadarPulseTimer / 10 + 1` times -- the same radar-screen timer the two row editors bound
 * themselves by, so the defect recorded in docs/parity/bugs.tsv for 0x75160 and 0x75464 applies to
 * these two handlers too.
 *
 * The record they mutate is the transfer flow's working site: `aSmapSites[nSiteCount]`, the slot
 * Transfer_ResetSite (0x9690c, ported as Transfer_ResetSite) clears and
 * BuySellScreen_ConfirmSiteAndClose commits.
 *
 * ValidateCapacity buys one unit per pass:
 *
 *   117 / 118, the two craft products -- 0x75C28..0x75C84. Budget Base_CalcHangarCap, pressure
 *     Base_CountShipsAtBase + Base_CountInboundTransfers + 1. Over it: shortfall 2.
 *   every other product row -- 0x75C88..0x75D10. Budget Base_CalcStoresCapacity, pressure
 *     Base_CountItemQtyAcrossBases + ObjectRecFull.bCargoSize (0xC6) + Base_CalcStorageUsed.
 *     Over it: shortfall 3.
 *   then, for any product row, `ObjectRecFull.uPrice (0xBC) > (unsigned)nFunds` -- 0x75D18:
 *     shortfall 1.
 *   the pass itself: 117 / 118 also bump the site's signed SmapSite.bUnk518 (0x206); panel D's
 *     anLineAux[nRow] += 1; site.auItemQty[nAux] += 1; nFunds -= uPrice; BuySell_nTransTotal += uPrice.
 *
 *   1000 / 112 / 113 take the other branch, 0x75E08..0x75E64. Budget Base_CalcLivingQuartersCap,
 *     pressure Base_CalcOccupiedSpace + Base_CountOutboundTransferSpace + 1. Over it: shortfall 0.
 *     Then the fixed price -- 40000 a soldier, 50000 an engineer, 60000 a scientist -- against
 *     nFunds, and a shortfall is 1. A soldier bumps unk_159D90 and the site's uRecoveredSoldiers
 *     (0x202) and the pass continues; an engineer bumps auItemQty[112] and a scientist auItemQty[113],
 *     and BOTH return out of the whole function, so one click queues at most one crew member.
 *
 * UpdatePrices is the mirror and sells one unit per pass, with no capacity test at all -- panel D's
 * count is the only guard, and a row whose count is zero just skips the body:
 *
 *   1000 -- 0x75A08..0x75A80: unk_159D90 -= 1, nFunds += 40000, BuySell_nTransTotal -= 40000, panel D
 *     -= 1, site.uRecoveredSoldiers -= 1, continue.
 *   112 -- 0x75B38..0x75B7C: nFunds += 50000, total -= 50000, panel D -= 1, site.auItemQty[112] -= 1,
 *     return. 113 the same at 60000 and auItemQty[113], also a return.
 *   any other row -- 0x75920..0x759C8: panel D -= 1; 117 / 118 also decrement the signed
 *     site.bUnk518; site.auItemQty[nAux] -= 1; nFunds += uPrice; BuySell_nTransTotal -= uPrice.
 *
 * The shortfall flag is written differently per path: the living-quarters and crew-price paths store
 * 1 into byte_1870C8 unconditionally, the hangar, stores and product-price paths store it only when
 * it is still clear -- and the product-price path's reason is the flag's own value afterwards, which
 * is 1 either way because nothing else writes a different one. The reason code always lands in
 * dword_1870B4[0].
 *
 * Deviations: the original bounds nothing -- it indexes aSmapSites[nSiteCount], apObjects[nAux] and
 * the panel arrays blindly -- and returns a residue (the site pointer, the loop limit, or
 * &dword_1870B4) that no caller reads; the port bounds the row, the slot and the site and returns
 * the shortfall code, or PUFO_BUYSELL_SHORTFALL_NONE when every pass completed. */
#define PUFO_BUYSELL_PRICE_SOLDIER   40000 /* 0x9C40, 0x75E80 */
#define PUFO_BUYSELL_PRICE_ENGINEER  50000 /* 0xC350, 0x76010 */
#define PUFO_BUYSELL_PRICE_SCIENTIST 60000 /* 0xEA60, 0x75FA4 */

typedef enum PufoBuySellShortfall {
    PUFO_BUYSELL_SHORTFALL_NONE    = -1, /* the pass completed; the original returns a residue here */
    PUFO_BUYSELL_SHORTFALL_LIVING  = 0,  /* 0x75F90, living quarters full */
    PUFO_BUYSELL_SHORTFALL_FUNDS   = 1,  /* 0x75F64 / 0x75F74, not enough funds */
    PUFO_BUYSELL_SHORTFALL_HANGAR  = 2,  /* 0x75F28, hangar full */
    PUFO_BUYSELL_SHORTFALL_STORES  = 3,  /* 0x75F48, stores full */
    PUFO_BUYSELL_SHORTFALL_CONTAINMENT = 4, /* 0x96618, alien containment full, the transfer screen's
                                             * gate for slots 77..104 and 114 */
    PUFO_BUYSELL_SHORTFALL_TRANSFER = 5  /* 0x96990, the transfer gate blocked */
} PufoBuySellShortfall;

typedef struct BuySellCapacityEnv {
    UiPanel *pPanelA;                /* pBuySellPanelA @ 0x161A1C, the row-kind column */
    UiPanel *pPanelD;                /* pBuySellPanelD @ 0x160C88, the available count */
    i32 *pnRowCursor;                /* unk_186BE8, the cached selected row */
    i32 *pnRadarPulseTimer;          /* nRadarPulseTimer @ 0xAC224, the repeat bound */
    const u8 *abFacilityGrid;        /* gBaseFacGrid @ 0x159768, the four capacity calls */
    const u8 *abGridBytes;           /* 0x159DBC, Base_CountShipsAtBase's extra term */
    SmapSite *aSites;                /* aSmapSites @ 0x162030 */
    i32 nSiteCount;                  /* nSiteCount @ 0x186410, also the working site's index */
    BaseRec *const *apBases;         /* apBases @ 0x114B2C */
    i32 nBaseCount;
    Ship *const *apCraft;            /* apCraft @ 0x114A64 */
    i32 nCraftCount;
    Soldier *const *apSoldiers;      /* apSoldiers @ 0x114B50 */
    i32 nSoldierCount;
    ObjectRecFull *const *apObjects; /* apObjects @ 0x113AC0 */
    i32 nObjectCount;
    i8 nSelectedBaseIdx;             /* nSelectedBaseIdx @ 0x159D81 */
    i32 *pnFunds;                    /* nFunds @ 0x1598D8 */
    i32 *pnTransTotal;               /* BuySell_nTransTotal @ 0x161A2C */
    i32 *pnSoldierHireCount;         /* unk_159D90, saved with the bases and moved by soldier rows */
    u32 *pdwShortfall;               /* byte_1870C8, written as a dword here too */
    i32 *pnResidue;                  /* dword_1870B4[0], the reason code */
} BuySellCapacityEnv;

i32 BuySellScreen_ValidateCapacity(const BuySellCapacityEnv *pEnv); /* the ">" control, buys one */
i32 BuySellScreen_UpdatePrices(const BuySellCapacityEnv *pEnv);     /* the "<" control, sells one */

/* ------------------------------------------------- the buy/sell screen's bring-up
 *
 * Evidence: BuySellScreen_Init @ 0x78c30 (352 B), the tail call of BuySellScreen_OpenBuy @ 0x61794
 * (the BL at 0x617B8) and of BuySellScreen_OpenSell @ 0x617c4 (0x617E8). Both openers write the
 * screen id first -- 11 for buy, 12 for sell -- and then call this with the screen slot at
 * unk_161A08.
 *
 * In the original's order:
 *
 *   1. 0x78C44: `BuySell_nFundsSnapshot = nFunds` -- the value BuySellScreen_BuildPanels' teardown
 *      restores when the player leaves without confirming, taken here at the open as well.
 *   2. 0x78C4C..0x78C70: operator new(0x14) then PadImage_ctor, and the result -- not the raw
 *      allocation -- is what lands in the screen slot.
 *   3. 0x78C7C, 0x78C8C: Palette_Load("buy_sell.pal", 5), then PadImage_Load(pad, "buy_sell", 5);
 *      the pad's nOriginX and nOriginY are cleared at 0x78C98 and 0x78CA4.
 *   4. 0x78CAC..0x78CB4: BuySell_nTransTotal and nSoldierHireCount (0x159D90) are cleared.
 *   5. 0x78CBC..0x78CD4: the close control g_pUiCloseCtl goes to x 224, y 1 (two STRH) with
 *      BuySellScreen_CloseThunk @ 0x75154 as its click.
 *   6. 0x78CE8..0x78CF8: the confirm control unk_186BA4 goes to `x = 120 - (uWidth >> 1)`, y 297 --
 *      the same centring BaseAtScreen_Open @ 0x588cc does.
 *   7. 0x78D08..0x78D44: g_anBuySellSlotDelta is cleared over Map_nObjectCount entries.
 *   8. 0x78D4C..0x78D7C: by the screen id. 11 installs BuySellScreen_ConfirmSiteAndClose @ 0x75090 on
 *      the confirm control and calls BuySellScreen_BuildControls @ 0x76160; 12 installs
 *      BuySellScreen_CommitTransaction @ 0x74bf8 and calls BuySellScreen_BuildPanels @ 0x76de0. The
 *      12 branch is written with LDREQ / STREQ / BLEQ, so the handler store and the call are both
 *      conditional on the id being read a second time.
 *
 * Deviations: the original dereferences the allocation and both control globals with no test; the
 * port skips what failed to allocate or was not supplied. */
#define PUFO_BUYSSELL_PAD_NAME      "buy_sell"     /* aBuySell @ 0xABC2C */
#define PUFO_BUYSSELL_PALETTE_NAME  "buy_sell.pal" /* aBuySellPal @ 0xABC38 */
#define PUFO_BUYSSELL_PALETTE_SLOT  5              /* 0x78C7C: MOV R1,#5 */
#define PUFO_BUYSSELL_SCREEN_BUY    11             /* nGeoscapeScreen for the buy mode */
#define PUFO_BUYSSELL_SCREEN_SELL   12             /* and for the sell mode */

typedef struct BuySellInitEnv {
    PadImage **ppScreen;             /* unk_161A08, the slot both openers pass */
    UiControl *pCloseCtl;            /* g_pUiCloseCtl */
    UiControl *pConfirmCtl;          /* unk_186BA4, the same confirm slot the hangar screen uses */
    void (*pfnCloseClick)(void);     /* BuySellScreen_CloseThunk @ 0x75154 */
    void (*pfnConfirmBuy)(void);     /* BuySellScreen_ConfirmSiteAndClose @ 0x75090 */
    void (*pfnConfirmSell)(void);    /* BuySellScreen_CommitTransaction @ 0x74bf8 */
    void (*pfnBuildControls)(void);  /* BuySellScreen_BuildControls @ 0x76160 */
    void (*pfnBuildPanels)(void);    /* BuySellScreen_BuildPanels @ 0x76de0 */
    i32 nScreenId;                   /* nGeoscapeScreen @ 0x186BC4 */
    const i32 *pnFunds;              /* nFunds @ 0x1598D8 */
    i32 *pnFundsSnapshot;            /* BuySell_nFundsSnapshot @ 0x161460 */
    i32 *pnTransTotal;               /* BuySell_nTransTotal @ 0x161A2C */
    i32 *pnSoldierHireCount;         /* nSoldierHireCount @ 0x159D90 */
    i32 *anSlotDelta;                /* g_anBuySellSlotDelta @ 0x161600 */
    i32 nSlotDeltaCount;             /* Map_nObjectCount @ 0x114A60, the clear length */
} BuySellInitEnv;

void BuySellScreen_Init(const BuySellInitEnv *pEnv);

/* ------------------------------------------------- the buy screen's object block
 *
 * Evidence: BuySellScreen_BuildControls @ 0x76160 (3112 B), the second half of the screen 11 branch
 * of BuySellScreen_Init @ 0x78c30. It is the buy mode's counterpart of BuySellScreen_BuildPanels:
 * the same five panel globals, the same g_aBuySellCtl controls and stru_ABBA0 descriptors, the same
 * g_aBuySellRowOrder sort -- but a different row set (the shop's catalogue, not what the base owns),
 * a different geometry, and the record it writes is the transfer flow's working site.
 *
 * The built flag is unk_161A24, a sibling of bBuySellPanelsBuilt @ 0x161A28: each mode sets its own
 * and each mode's teardown keys off its own.
 *
 * Teardown branch, *pbControlsBuilt != 0 (0x76C08..0x76D70):
 *   1. the working site aSmapSites[nSiteCount] is reset whole -- auItemQty[0..255], bFlag0, bFlag1,
 *      uRecoveredSoldiers, bRecoveredSoldierAssign, bRecoveredCraftCount, anSoldierIds[0..99] and
 *      anCraftIds[0..9]. Note bUnk518 is NOT cleared here, only by the build branch.
 *   2. the flag is cleared, the four controls are UiControl_FreePads'd and deleted, and panels A, B,
 *      E, C, D are freed in that order (UiPanel_FreeLines then delete, 0x76CC4 / 0x76CE4 / 0x76D04 /
 *      0x76D24 / 0x76D44). The control loop starts at &unk_161A08 and reads v69[1], so it covers the
 *      four g_aBuySellCtl slots and not the pad block the slot itself holds.
 *   3. `if (BuySell_nFundsSnapshot != nFunds) nFunds = BuySell_nFundsSnapshot` -- the same rollback
 *      BuildPanels' teardown does.
 *
 * Build branch (0x7619C..0x76BE8):
 *   1. 0x761A8..0x7620C: the working site is opened -- bFlag0 = 8, bFlag1 = apBases[nSelectedBaseIdx]
 *      ->bUnk4 (the base's site type, which is what the two transfer counters match sites against),
 *      bUnk518 = 0, uRecoveredSoldiers = 0, auItemQty[0..255] = 0.
 *   2. 0x76218..0x76298: four controls (operator new 0x20 + UiControl_ctor) into g_aBuySellCtl; [0]
 *      gets BuySellScreen_PanelPrev @ 0x76088 and [1] BuySellScreen_PanelNext @ 0x760e0; all four get
 *      UiControl_Setup(ctl, 0, 0, stru_ABBA0[i]) -- the same four descriptors BuildPanels uses.
 *   3. 0x762A8..0x76334: the include table. A slot gets a row when it is 1..58, 112, 113, 117 or 118
 *      AND ObjectRecFull.uPrice (0xBC) != 0 AND the u16 at +0xBA is 0 (0x76318). In the shipped
 *      OBJECTS.DAT the second test excludes nothing the first has not already excluded -- all 35
 *      records with a non-zero +0xBA have no price -- and 36 slots pass: 1, 4, 7, 8, 10, 14..17, 19,
 *      22, 25..27, 31, 32, 36, 38, 40..48, 52..54, 56, 57, 112, 113, 117, 118. The row count is the
 *      included count + 1, for the soldier row written after slot 113.
 *   4. 0x76348..0x768B4: the five panels, all nStyle 14 and nFont 16 (BuildPanels uses nStyle 13), at
 *      x 10 / 174 / 180 / 196 / 232 with nViewW 125 / 50 / 15 / 15 / 45. Panel A's click is
 *      BuySellScreen_SyncSelection @ 0x76138, panel B's BuySellScreen_UpdatePrices @ 0x75878 and
 *      panel C's BuySellScreen_ValidateCapacity @ 0x75b88 -- the two handlers of the previous unit.
 *   5. 0x768DC..0x76AC4: the fill pass, in slot order, only where the include flag is 1. Panel A gets
 *      the record name cut to 17 and aux the slot id, panels B and C the "<" and ">" literals at
 *      0xABC24 and 0xABC20, panel E aux the price and panel D aux zero. Inside the slot-113 branch an
 *      extra row is written: panel A's text is aTextRecs @ 0xB38D0 + 205*159 = 0xBB823 ("Soldier" in
 *      main.dat, "Солдат" in lang.dat), panel A aux 1000, panel E aux 40000 and panel D aux 0 -- the
 *      soldier the shop sells at exactly the 40000 BuySellScreen_ValidateCapacity charges for one.
 *      The +1 in the row count is unconditional, so when slot 113 itself is excluded (no price) the
 *      soldier row is never written and the last row stays as UiPanel_ctor left it.
 *   6. 0x76AF0..0x76BE8: the same selection sort over g_aBuySellRowOrder, swapping panels A, E, B, C
 *      and D together.
 *
 * Deviations: the original allocates the four controls and the five panels with operator new and
 * deletes the controls in its teardown; the port takes caller-owned controls and frees only the
 * panels it allocated. The original dereferences every allocation without a test. */
/* unk_AAA04 @ 0xAAA04, g_aBuySellRowOrder in the IDB: the row order the selection sort walks. Both
 * buy/sell builders and the hangar's BasePlaneScreen_BuildControls sort with it, and each reads only
 * nObjectCount - 1 of the 148 entries. 124 are non-zero; the rest match nothing. */
#define PUFO_BUYSELL_ORDER_COUNT 148
extern const i32 g_aBuySellRowOrder[PUFO_BUYSELL_ORDER_COUNT];

#define PUFO_BUYSSELL_SITE_FLAG_NEW     8    /* 0x761A8: the working site's bFlag0 while the shop is open */
#define PUFO_TRANSFER_SITE_FLAG_COMMIT  24   /* 0x97DA4: bFlag0 of a site whose transfer has been committed */
#define PUFO_BUYSSELL_SOLDIER_TEXT_REC  159  /* aTextRecs + 205*159 = 0xBB823, "Soldier" */

typedef struct BuySellControlsEnv {
    u8  *pbControlsBuilt;             /* unk_161A24 */
    UiControl *apCtl[4];              /* g_aBuySellCtl @ 0x161A0C */
    UiPanel **ppPanelA;               /* pBuySellPanelA @ 0x161A1C */
    UiPanel **ppPanelB;               /* pBuySellPanelB @ 0x161A00 */
    UiPanel **ppPanelC;               /* pBuySellPanelC @ 0x161A20 */
    UiPanel **ppPanelD;               /* pBuySellPanelD @ 0x160C88 */
    UiPanel **ppPanelE;               /* pBuySellPanelE @ 0x1615F8 */
    SmapSite *aSites;                 /* aSmapSites @ 0x162030 */
    i32 *pnSiteCount;                 /* nSmapSiteCount @ 0x186410, also the working site's index. A pointer,
                                       * not a copy: the original re-reads the global at 0x7619C and 0x76C14,
                                       * and BuySellScreen_ConfirmSiteAndClose changes it INSIDE the close
                                       * chain whose teardown reads it. */
    BaseRec *const *apBases;          /* apBases @ 0x114B2C, for the site's bFlag1 */
    i8 nSelectedBaseIdx;              /* nSelectedBaseIdx @ 0x159D81 */
    ObjectRecFull *const *apObjects;  /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                 /* Map_nObjectCount @ 0x114A60 */
    i32 *pnFunds;                     /* nFunds @ 0x1598D8 */
    const i32 *pnFundsSnapshot;       /* BuySell_nFundsSnapshot @ 0x161460, reached live at the rollback (0x76D60) */
    const char *pTextRecs;            /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    void (*pfnPanelPrev)(void);       /* BuySellScreen_PanelPrev @ 0x76088 */
    void (*pfnPanelNext)(void);       /* BuySellScreen_PanelNext @ 0x760e0 */
    void (*pfnSyncSelection)(void);   /* BuySellScreen_SyncSelection @ 0x76138 */
    void (*pfnUpdatePrices)(void);    /* BuySellScreen_UpdatePrices @ 0x75878 */
    void (*pfnValidateCapacity)(void);/* BuySellScreen_ValidateCapacity @ 0x75b88 */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
} BuySellControlsEnv;

void BuySellScreen_BuildControls(BuySellControlsEnv *pEnv);

/* ------------------------------------------------- the transfer screen's bring-up and reset
 *
 * Evidence: Ui_InitTransfer @ 0x9955c (392 B), the tail call of TransferScreen_Open @ 0x614fc (the
 * BL at 0x61510, with the screen's root array at 0x1870D8), and Transfer_ResetSite @ 0x9690c
 * (420 B), the third of the four controls it wires.
 *
 * Ui_InitTransfer, in the original's order:
 *
 *   1. 0x99568..0x9958C: operator new(0x14) then PadImage_ctor, and the ctor's result -- not the raw
 *      allocation -- goes into ppRoot[0].
 *   2. 0x99590: PadImage_Load(pad, "transfer", 13) -- aTransfer @ 0xAC438, palette slot 13. The pad's
 *      nOriginX and nOriginY are cleared at 0x99598 and 0x995A0.
 *   3. 0x995A4: TransferScreen_BuildPanels runs before the controls exist.
 *   4. 0x995B8: byte_1870F8 = 1, the "this transfer is still possible" flag Transfer_ResetSite
 *      clears when it finds the load blocked.
 *   5. 0x995C0..0x995F0: four controls (operator new 0x20 + UiControl_ctor) into ppRoot[1..4].
 *   6. 0x99604..0x99630: their clicks, in slot order: Transfer_ScrollPrevAll @ 0x967e4 and
 *      Transfer_ScrollNextAll @ 0x9683c -- compared word by word with BuySellScreen_PanelPrev and
 *      BuySellScreen_PanelNext they share the 17 instructions and the same five panel globals, and differ only in
 *      the five branch displacements (four BL plus the tail B); see docs/parity/renames.tsv --
 *      then Transfer_ResetSite and TransferScreen_CommitTransfer (0x97aa4, ported at
 *      pufo_baseui.c:2625).
 *   7. 0x9963C..0x99654: the close control g_pUiCloseCtl goes to x 224, y 1 with
 *      Geoscape_CloseUI_ThunkB @ 0x96020. Same slot and same thunk as the buy/sell screen, but the
 *      buy/sell Init installs BuySellScreen_CloseThunk @ 0x75154 there instead.
 *   8. 0x9965C: the confirm control unk_186BA4 gets that same close thunk -- before UiControl_Setup,
 *      where BuySellScreen_Init installs its confirm handler after the pad and delta clearing.
 *   9. 0x99660..0x99680: UiControl_Setup(ctl, 0, 0, g_aTransferCtlDesc[i]) for the four controls. The reset
 *      and confirm descriptors are (1, 0x12C) and (0x78, 0x12C), both drawing "emptyb" / "cemptyb".
 *  10. 0x99698, 0x996B8: the confirm control goes to `x = 120 - (uWidth >> 1)` and
 *      `y = 28 * nBaseCount + 10`, the base count at 0x159D80 read as a signed byte. The confirm
 *      button sits lower as the player owns more bases; this is the only transfer-screen layout that
 *      depends on the count.
 *
 * Transfer_ResetSite, in the original's order:
 *
 *   1. 0x9691C: byte_1870F8 = 1.
 *   2. 0x96920..0x96974: the gate. The transfer is blocked when
 *      Base_CountItemQtyAcrossBases(abFacilityGrid, nSiteCount, byte_1870F4, 0) is non-zero -- the
 *      cargo the working site aSmapSites[nSiteCount] already carries for the base the psi screen
 *      selected -- or unk_1870D0 or unk_1870F0 is non-zero (both read with LDRSB, so a negative byte
 *      counts), or Smap_SumSiteScores() is positive.
 *   3. 0x96980..0x96990: when blocked, byte_1870F8 = 0, byte_1870C8 = 1 and dword_1870B4[0] = 5 --
 *      the fifth reason code, after the buy screen's 0 (living quarters), 1 (funds), 2 (hangar) and
 *      3 (stores).
 *   4. 0x96994..0x96A04: the pending quantities fold back into the count column. For every row of
 *      panel A: panel D aux += panel E aux, then panel E aux = 0. The loop is guarded by
 *      `nLineCount > 0` at 0x969B0.
 *   5. 0x96A04..0x96AA0: the working site is cleared -- bUnk518, uRecoveredSoldiers,
 *      bRecoveredCraftCount, bRecoveredSoldierAssign and bHasLoot, then auItemQty over
 *      Map_nObjectCount entries and not the full 256. bFlag0 and bFlag1 are left alone, which is what
 *      tells this apart from BuySellScreen_BuildControls' teardown: that one clears the two flags and
 *      all 256 quantities but never bHasLoot.
 *
 * Deviation: the original allocates the pad and the four controls with operator new and dereferences
 * every one of them with no test; the port allocates only the pad and takes caller-owned controls,
 * storing them into ppRoot[1..4] so the rest of the screen finds them where the original put them.
 * Transfer_ResetSite indexes aSmapSites[nSiteCount] blindly; the port bounds it.
 *
 * unk_1870D0 and unk_1870F0 sit either side of the transfer root array at 0x1870D8, and the only
 * references to them anywhere in the binary are the two LDRSB reads in step 2 -- no direct write
 * exists. What they hold is not established, so the port takes them from the env. */
#define PUFO_TRANSFER_PAD_NAME "transfer" /* aTransfer @ 0xAC438 */
#define PUFO_TRANSFER_PAD_MODE 13         /* 0x99590: MOV R2,#0xD */

typedef struct TransferInitEnv {
    void **ppRoot;                  /* 0x1870D8; [0] receives the PadImage, [1..4] the four UiControl* */
    UiControl *apCtl[4];            /* caller-owned, stored into ppRoot[1..4] */
    UiControl *pCloseCtl;           /* g_pUiCloseCtl */
    UiControl *pConfirmCtl;         /* unk_186BA4 */
    void (*pfnCloseClick)(void);    /* Geoscape_CloseUI_ThunkB @ 0x96020 */
    void (*pfnPanelPrev)(void);     /* Transfer_ScrollPrevAll @ 0x967e4 */
    void (*pfnPanelNext)(void);     /* Transfer_ScrollNextAll @ 0x9683c */
    void (*pfnResetSite)(void);     /* Transfer_ResetSite @ 0x9690c */
    void (*pfnCommit)(void);        /* TransferScreen_CommitTransfer (0x97aa4), ported at 2625 */
    void (*pfnBuildPanels)(void);   /* TransferScreen_BuildPanels (0x96b30), ported at 1956; the
                                     * Geoscape_CloseUI transfer arm (0x8BE40) calls it */
    i8 *pbBaseCount;                /* 0x159D80, signed */
    u8 *pbTransferOk;               /* byte_1870F8 */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
} TransferInitEnv;

void Ui_InitTransfer(TransferInitEnv *pEnv);

typedef struct TransferResetEnv {
    SmapSite *aSites;               /* aSmapSites @ 0x162030 */
    i32 nSiteCount;                 /* nSiteCount @ 0x186410, also the working site's index */
    BaseRec *const *apBases;        /* apBases @ 0x114B2C */
    ObjectRecFull *const *apObjects;/* apObjects @ 0x113AC0 */
    i32 nObjectCount;               /* Map_nObjectCount @ 0x114A60, the auItemQty clear length */
    i8 nPendingBase;                /* byte_1870F4, signed: the base the psi screen selected */
    u8 *pbTransferOk;               /* byte_1870F8 */
    i8 *pbScratch1870D0;            /* unk_1870D0, signed */
    i8 *pbScratch1870F0;            /* unk_1870F0, signed */
    u32 *pdwShortfall;              /* byte_1870C8, written as a dword */
    i32 *pnResidue;                 /* dword_1870B4[0], the reason code */
    UiPanel *pPanelA;               /* pBuySellPanelA @ 0x161A1C, the row-count source */
    UiPanel *pPanelD;               /* pBuySellPanelD @ 0x160C88, the count column */
    UiPanel *pPanelE;               /* pBuySellPanelE @ 0x1615F8, the pending quantity */
} TransferResetEnv;

SmapSite *Transfer_ResetSite(TransferResetEnv *pEnv);

/* ------------------------------------------------- the transfer screen's row list
 *
 * Evidence: TransferScreen_BuildPanels @ 0x96b30 (3956 B), called from Ui_InitTransfer @ 0x9955c
 * (0x995A4) and again from Geoscape_CloseUI (0x8b94c, ported at this file:3120) at 0x8BE40, and Transfer_MoveOneItem
 * @ 0x9602c
 * (604 B), the click handler it installs into the decrement column. This is the transfer mode's
 * counterpart of BuySellScreen_BuildPanels @ 0x76de0 -- same five panel globals, same
 * g_aBuySellRowOrder sort, same row model -- with a wider slot filter, a shifted geometry, no price
 * column and no alien row-marker offset.
 *
 * The built flag is unk_1870EC, a third sibling of bBuySellControlsBuilt @ 0x161A24 and
 * bBuySellPanelsBuilt @ 0x161A28.
 *
 * Teardown branch, unk_1870EC != 0 (0x97994..0x97A88): the flag is cleared, byte_1870F8 is cleared,
 * the pad at 0x1870D8 is PadImage_FreeData'd and deleted, the four controls at 0x1870DC..0x1870E8
 * are UiControl_FreePads'd and deleted, and panels A, B, E, C, D are freed in that order. There is
 * no funds rollback here -- the transfer screen spends no money, which is what tells this teardown
 * apart from the two buy/sell ones.
 *
 * Build branch (0x96B5C..0x97988):
 *
 *   1. 0x96B60..0x96BDC: the flag is set and the working site aSmapSites[nSiteCount] is cleared the
 *      same way Transfer_ResetSite does it -- bUnk518, uRecoveredSoldiers, bRecoveredCraftCount,
 *      bRecoveredSoldierAssign, bHasLoot, then auItemQty over Map_nObjectCount entries.
 *   2. 0x96BF0..0x96C3C: count the soldiers at this base that no craft carries (nKind == base and
 *      bUnk183 == 0xFF).
 *   3. 0x96C44..0x96CB4: count the craft homed here with nobody aboard. The fill pass adds a third
 *      test this one does not have -- see the ORIGINAL BUG note in the implementation.
 *   4. 0x96CC4..0x96DC0: the slot filter is 1..69, 77..111, 114, 122..124 and 143..148 with a
 *      non-zero auPerBaseVal[nSelectedBaseIdx], plus 112 and 113 when the base has engineers or
 *      scientists. Against the sell mode that is its stocked and alien filters merged -- the same
 *      slots except 142, which the sell mode counts and this one does not.
 *   5. 0x96DC4..0x9732C: the five panels, all nStyle 13 and nFont 16. Panel A x 10 / viewW 125 with
 *      Transfer_CacheSelIndex @ 0x96ab0 as its click; E x 232 / 190 / 45, no click; B x 172 / 168 /
 *      15 with Transfer_MoveOneItem; C x 188 / 184 / 15 with TransferScreen_ValidateCapacity
 *      (0x96288, ported at pufo_baseui.c:2366); D x 166 / 130 / 50, no click. Against the sell mode the B, C and D
 *      columns are eight pixels further right and A and E are identical.
 *   6. 0x97330..0x974DC: the soldier rows. Panel A gets the name through the same 17-character cut
 *      and aux 1000, panel D aux 1, panel E aux 0, panels B and C the "<" and ">" literals, panel C
 *      aux the roster id (Soldier.nUnk440) and panel B aux the apSoldiers index.
 *   7. 0x974E4..0x976B4: the craft rows, TEXTS[415 + bCraftType] + "-" + itoa(bType), panel A aux
 *      Transfer_RoleToIconId(bCraftType), panel D aux 1, panel E aux 0, panel C aux the craft id.
 *      Panel B aux is left at zero here, where the soldier rows set it.
 *   8. 0x976C0..0x97858: the object rows. Panel A aux is the slot id for every row -- there is no
 *      +2000 alien offset in this screen, so an alien row sorts by its plain slot. Panel E aux is 0
 *      (no price column), and panel D aux is auPerBaseVal[nSelectedBaseIdx], or the engineer or
 *      scientist count for slots 112 and 113.
 *   9. 0x97880..0x97988: the same selection sort over g_aBuySellRowOrder, nObjectCount - 1 passes,
 *      swapping panels A, E, B, C and D together.
 *
 * Transfer_MoveOneItem is the decrement column's handler. It reads the selected row's slot id out of
 * panel A at the row index panel B cached (pBuySellPanelB->nUnk3032), and repeats
 * nRadarPulseTimer / 10 times -- the same timer-bound loop recorded for BuySellScreen_FillBuyPanel
 * and friends. Per iteration: a craft row (aux 117..121) with a pending unit clears panel E aux,
 * raises panel D aux and decrements the site's bRecoveredCraftCount, then stops; any other row with
 * a pending unit raises panel D aux, lowers panel E aux and decrements the site's auItemQty[slot];
 * the soldier row (slot id 1000) is handled once outside the loop, clearing panel E aux, raising
 * panel D aux and decrementing bRecoveredSoldierAssign. All three decrement a site byte with LDRSB
 * and store it back with STRB, so a count already at zero wraps to 255.
 *
 * Deviation: the original allocates the five panels with operator new and deletes the pad and the
 * four controls in its teardown; the port frees only the pad and takes caller-owned controls. The
 * original dereferences every allocation with no test. Transfer_MoveOneItem's return value is
 * whatever R0 happens to hold at the exit -- 517 (the 0x205 offset it just used) after a craft
 * decrement, the row aux when nothing was pending, the site address after a soldier decrement, and
 * the timer quotient at the loop bound; it is a click handler and no caller reads it, so the port
 * returns the timer quotient. */
#define PUFO_TRANSFER_ROW_CRAFT_FIRST 117 /* 0x960E4: the craft-row test is aux > 116 && aux < 122 */
#define PUFO_TRANSFER_ROW_CRAFT_LAST  121

/* Staff allocation pointers are full per-base tables, as in BuySellBuildEnv. */
typedef struct TransferBuildEnv {
    u8  *pbPanelsBuilt;               /* unk_1870EC */
    PadImage **ppPad;                 /* 0x1870D8, the pad Ui_InitTransfer allocated */
    u8 *pbTransferOk;                 /* byte_1870F8, cleared by the teardown */
    UiControl *apCtl[4];              /* 0x1870DC..0x1870E8 */
    SmapSite *aSites;                 /* aSmapSites @ 0x162030 */
    i32 nSiteCount;                   /* nSiteCount @ 0x186410, also the working site's index */
    Soldier *const *apSoldiers;       /* apSoldiers @ 0x114B50 */
    i32 nSoldierCount;                /* nSoldierCount @ 0x115350 */
    Ship *const *apCrafts;            /* apCrafts @ 0x114AE4 */
    i32 nCraftCount;                  /* Map_nCraftCount @ 0x114B4C */
    ObjectRecFull *const *apObjects;  /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                 /* Map_nObjectCount @ 0x114A60 */
    i8 nSelectedBaseIdx;              /* nSelectedBaseIdx @ 0x159D81 */
    const char *pTextRecs;            /* aTextRecs @ 0xB38D0 */
    const u16 *apManufCol;            /* the engineer column @ 0x15E19C */
    const u16 *apResearchCol;         /* the scientist column @ 0x16022C */
    i32 nCrewEntries;                 /* the crew column entry count byte @ 0x160C4C */
    UiPanel **ppPanelA;
    UiPanel **ppPanelB;
    UiPanel **ppPanelC;
    UiPanel **ppPanelD;
    UiPanel **ppPanelE;
    void (*pfnCacheSelIndex)(void);   /* Transfer_CacheSelIndex @ 0x96ab0 */
    void (*pfnMoveOneItem)(void);     /* Transfer_MoveOneItem @ 0x9602c */
    void (*pfnValidateCapacity)(void);/* TransferScreen_ValidateCapacity (0x96288), ported at 2366 */
} TransferBuildEnv;

void TransferScreen_BuildPanels(TransferBuildEnv *pEnv);

typedef struct TransferMoveEnv {
    UiPanel *pPanelA;                /* pBuySellPanelA @ 0x161A1C */
    UiPanel *pPanelB;                /* pBuySellPanelB @ 0x161A00, the row cache lives in nUnk3032 */
    UiPanel *pPanelD;                /* pBuySellPanelD @ 0x160C88, the count column */
    UiPanel *pPanelE;                /* pBuySellPanelE @ 0x1615F8, the pending quantity */
    i32 *pnRowCursor;                /* unk_186BE8, the cached selected row */
    const i32 *pnRadarPulseTimer;    /* nRadarPulseTimer @ 0xAC224, the repeat bound */
    SmapSite *aSites;                /* aSmapSites @ 0x162030 */
    i32 nSiteCount;                  /* nSiteCount @ 0x186410, also the working site's index */
} TransferMoveEnv;

i32 Transfer_MoveOneItem(TransferMoveEnv *pEnv);

/* ------------------------------------------------- the transfer screen's capacity gate
 *
 * Evidence: TransferScreen_ValidateCapacity @ 0x96288 (1372 B), the click handler
 * TransferScreen_BuildPanels installs into the increment column at 0x97168. It is the mirror of
 * BuySellScreen_ValidateCapacity @ 0x75b88: where that one moves units INTO the base and spends
 * money, this one moves units OUT of the base onto the working site and spends none.
 *
 * The selected row's slot id comes from panel A at the index panel C cached
 * (pBuySellPanelC->nUnk3032, 0x962D0) -- panel C, where the buy screen used panel B.
 *
 * Two shapes, decided by that slot id alone:
 *
 *   1. Slot 1000, 112 or 113 (0x96358): the while condition fails on the first test, so the repeat
 *      loop is never entered and the pass runs once. The gate is Base_CalcLivingQuartersCap against
 *      Base_CalcOccupiedSpace + Base_CountOutboundTransferSpace(..., scan ON) + 1, and on shortfall
 *      byte_1870C8 is written UNCONDITIONALLY with reason 0. A soldier row then sets panel E aux to
 *      1 (assigned, not incremented), lowers panel D aux and raises the site's
 *      bRecoveredSoldierAssign. A crew row (112 / 113) instead raises the site's auItemQty[slot] by
 *      one, but only while auPerBaseVal[nSelectedBaseIdx] is strictly above what the site already
 *      holds, and lowers panel D aux and raises panel E aux.
 *   2. Any other slot: the repeat loop, nRadarPulseTimer / 10 + 1 passes -- the fifth handler on
 *      that bound. A craft row (panel A aux 117..121) gates on Base_CountInboundTransfers +
 *      Base_CountShipsAtBase + 1 against Base_CalcHangarCap (reason 2, written only when the flag is
 *      currently clear), then moves at most one craft per click. Any other row first checks that the
 *      base actually has the item: auPerBaseVal[nSelectedBaseIdx] - site.auItemQty[slot] > 0. For an
 *      alien slot (77..104 or 114) it then gates Base_CalcAlienContainmentCap against
 *      Base_CountAmmoItems + 1 -- reason 4, written unconditionally -- and finally gates
 *      Base_CountItemQtyAcrossBases + bCargoSize + Base_CalcStorageUsed against
 *      Base_CalcStoresCapacity (reason 3, written only when clear). On the way through it raises the
 *      site's auItemQty[slot], lowers panel D aux and raises panel E aux.
 *
 * The two base indices are not the same one. Every capacity gate here measures the base the psi
 * screen selected (byte_1870F4, signed), while the "does the base actually have this" test reads
 * auPerBaseVal at nSelectedBaseIdx (0x159D81). The buy screen uses nSelectedBaseIdx for both.
 *
 * Deviation: the original returns &dword_1870B4 on a shortfall and 517 (the 0x205 offset the craft
 * branch just used) after a craft move; the port returns the reason code, matching
 * BuySellScreen_ValidateCapacity. The port bounds the cached row index and the site slot. */
typedef struct TransferCapacityEnv {
    UiPanel *pPanelA;                /* pBuySellPanelA @ 0x161A1C, the row-kind column */
    UiPanel *pPanelC;                /* pBuySellPanelC @ 0x161A20, nUnk3032 holds the selected row */
    UiPanel *pPanelD;                /* pBuySellPanelD @ 0x160C88, the available count */
    UiPanel *pPanelE;                /* pBuySellPanelE @ 0x1615F8, the pending quantity */
    i32 *pnRowCursor;                /* unk_186BE8, the cached selected row */
    const i32 *pnRadarPulseTimer;    /* nRadarPulseTimer @ 0xAC224, the repeat bound */
    const u8 *abFacilityGrid;        /* abFacilityGrid @ 0x159768 */
    Ship *const *apCraft;            /* apCrafts @ 0x114AE4 */
    i32 nCraftCount;                 /* Map_nCraftCount @ 0x114B4C */
    Soldier *const *apSoldiers;      /* apSoldiers @ 0x114B50 */
    i32 nSoldierCount;               /* nSoldierCount @ 0x115350 */
    ObjectRecFull *const *apObjects; /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                /* Map_nObjectCount @ 0x114A60 */
    SmapSite *aSites;                /* aSmapSites @ 0x162030 */
    i32 nSiteCount;                  /* nSiteCount @ 0x186410, also the working site's index */
    BaseRec *const *apBases;         /* apBases @ 0x114B2C */
    i32 nBaseCount;                  /* 0x159D80, the base count Base_CountShipsAtBase walks */
    i8 nSelectedBaseIdx;             /* nSelectedBaseIdx @ 0x159D81, the availability column */
    i8 nTransferBaseIdx;             /* byte_1870F4, the base every capacity gate measures */
    u32 *pdwShortfall;               /* byte_1870C8, written as a dword */
    i32 *pnResidue;                  /* dword_1870B4[0], the reason code */
} TransferCapacityEnv;

i32 TransferScreen_ValidateCapacity(TransferCapacityEnv *pEnv);

/* ------------------------------------------------- the transfer screen's commit
 *
 * Evidence: TransferScreen_CommitTransfer @ 0x97aa4 (1280 B), the confirm control Ui_InitTransfer
 * wires at 0x99608. It writes the queued rows into the working site, unhomes the queued soldiers and
 * craft, and -- only when the transfer actually carries something -- closes the working record and
 * opens a fresh one.
 *
 * The row pass (0x97AF8..0x97D24) walks panel A's rows and acts on two kinds, both gated on panel E
 * aux being exactly 1 (the value the capacity gate assigns):
 *
 *   1. Slot 1000 (0x97B10): panel C aux -- the roster id the row carries -- is written into
 *      aSmapSites[nSmapSiteCount].anSoldierIds[nSoldiersQueued] (0x97B58; the original stores a dead
 *      "+1" there first, then overwrites it with the id). Then every soldier whose nUnk440 equals
 *      that roster id gets Soldier.nKind (0x0B6) set to 255 (0x97BB0, STRB #0xFF): the soldier leaves
 *      the base roster, the mirror of the craft branch's bHomeBase = -1.
 *   2. Slot 117..121 (0x97C18): panel C aux -- the craft id -- goes into anCraftIds[nCraftsQueued]
 *      (0x97C48, same dead "+1" first). Then every craft whose Ship.bId equals that id has its hangar
 *      cell cleared, abFacilityGrid[444 + 144*bHomeBase + 6*bHangarY + bHangarX] (0x97CCC, built as
 *      6 * (24*bHomeBase + bHangarY + 12) + bHangarX over abFacilityGrid + 0x174), and Ship.bHomeBase
 *      set to 255 (0x97CD4).
 *
 * A row whose aux is 1000 but whose panel E aux is not 1 skips the craft test as well: 0x97B10
 * branches to the next row, not to the craft block.
 *
 * The commit gate (0x97D28..0x97D90) runs the whole commit only when at least one of:
 * Base_CountItemQtyAcrossBases with the scan OFF, the site index at the count and the base at
 * byte_1870F4 is non-zero; a soldier or a craft was queued; the working site's auItemQty[112] +
 * auItemQty[113] is non-zero; Smap_SumSiteScores > 0. With nothing pending nothing at all happens.
 *
 * The commit body: bFlag0 = 24 on the record just filled (0x97DA4 -- a completed transfer, where the
 * shop's "open" flag is 8); bFlag1 = apBases[byte_1870F4]->bUnk4 (0x97DBC); panel E aux zeroed over
 * every row (0x97DDC); BaseRec_SubItemQty(apBases[nSelectedBaseIdx], apObjects[i],
 * site.auItemQty[i]) for every object (0x97E5C) -- the pending quantities leave the SELECTED base;
 * nSmapSiteCount raised (0x97E8C); then every object whose auPerBaseVal[nSelectedBaseIdx] exceeds
 * 60000 (0xEA60) sends the base index and the object index to Transfer_WriteErrorDump @ 0x996e4
 * (0x97EE4), the debug dump below; finally the fresh working record at the new count is cleared -- bUnk518,
 * uRecoveredSoldiers, bRecoveredCraftCount, bRecoveredSoldierAssign, bHasLoot and
 * auItemQty[0..nObjectCount-1].
 *
 * Deviation: when the gate skips, the original returns the Smap_SumSiteScores count reinterpreted as
 * a pointer; the port returns NULL. The port bounds the two id arrays (100 and 10 entries) and the
 * bHomeBase used to index the grid; the original trusts all three, so a craft already sitting at
 * bHomeBase 255 would write 24*255 + ... bytes past the facility grid. */
typedef struct TransferCommitEnv {
    UiPanel *pPanelA;                /* pBuySellPanelA @ 0x161A1C, the row-kind column */
    UiPanel *pPanelC;                /* pBuySellPanelC @ 0x161A20, aux holds the roster or craft id */
    UiPanel *pPanelE;                /* pBuySellPanelE @ 0x1615F8, aux 1 marks a queued row */
    SmapSite *aSites;                /* aSmapSites @ 0x162030 */
    i32 *pnSiteCount;                /* nSmapSiteCount @ 0x186410, raised by the commit */
    BaseRec *const *apBases;         /* apBases @ 0x114B2C */
    Soldier *const *apSoldiers;      /* apSoldiers @ 0x114B50 */
    i32 nSoldierCount;               /* nSoldierCount @ 0x115350 */
    Ship *const *apCraft;            /* apCraft @ 0x114A64 */
    i32 nCraftCount;                 /* Map_nCraftCount @ 0x114B4C */
    ObjectRecFull *const *apObjects; /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                /* Map_nObjectCount @ 0x114A60 */
    u8 *abFacilityGrid;              /* abFacilityGrid @ 0x159768, the hangar cell is cleared */
    i8 nSelectedBaseIdx;             /* nSelectedBaseIdx @ 0x159D81, the base the items leave */
    i8 nTransferBaseIdx;             /* byte_1870F4, the destination base */
    const char *pszAppDir;           /* szAppDir @ 0x1883B0, the dump's output directory */
    const u8 *pbBaseCount;           /* nBaseCount @ 0x159D80, read signed there, for the dump */
} TransferCommitEnv;

SmapSite *TransferScreen_CommitTransfer(TransferCommitEnv *pEnv);

/* ------------------------------------------------- the transfer error dump
 *
 * Evidence: Transfer_WriteErrorDump @ 0x996e4 (2,196 B), reached from TransferScreen_CommitTransfer
 * (0x97EE4) and Smap_GrantSiteRecovery (0x99038), both when some base's per-object column passes
 * 60000 (0xEA60). It is a debug dump writer, not a UI display: it opens
 * "<szAppDir>t_error<rand>.txt" and writes a labelled text report, and it changes no game state.
 * The original's first argument is the pointer 0x1870D8 and is never read.
 *
 * The name (0x996EC..0x9977C). strcpy(szPath[40], szAppDir @ 0x1883B0), strcat "t_error",
 * itoa(Game_RandU16(0xE100)) -- 0..57599 -- strcat ".txt", fopen(path, "wb"). The copy is unbounded
 * into a 40-byte stack buffer and the FILE * is never tested: a long directory or a failed open sends
 * NULL to every fwrite and to fclose. The port bounds the copy and returns -1 without writing when
 * the open fails, and writes through CreateFileA / WriteFile because the MSVC CRT deprecates the whole
 * fopen family (see util/pufo_str.h); the bytes on disk are identical.
 *
 * The header block (0x99780..0x99954): "TRANSFERS", "count=" + itoa(nSmapSiteCount),
 * "(basenumber)current base=" + itoa((char)bBaseIdx) -- the argument is sign-extended at 0x99890
 * (`MOV R3,R5,LSL#24 / MOV R5,R3,ASR#24`), so a base index of 0x80 or more prints negative --
 * "problem with obj=" + itoa(nObjSlot). Every line ends with the 2-byte value 2573 written at
 * 0x99784, which is 0x0D 0x0A on the wire: CRLF.
 *
 * One block per site (0x99960..0x99F44), skipped entirely when nSmapSiteCount is not positive. Each
 * block opens with a bare CRLF (0x999E0), so blocks are separated by a blank line:
 *   "transfer N " + itoa(nSiteIdx)                                    -- the label has a trailing space
 *   "(baseidx)destination base=" + itoa((char)site->bFlag1), and when that id resolves through the
 *   base lookup to the very index the dump was called with, "(incoming transfer)" is appended to the
 *   SAME line before its CRLF (0x99AD8..0x99B14)
 *   "timeremain=" + itoa((char)site->bFlag0)
 *   "add_new_ship= " + itoa((char)site->bUnk518)                      -- the label has a trailing space
 *   "add_new_soldiers=" + itoa(site->uRecoveredSoldiers)              -- LDRH, the only unsigned one
 *   "ship_transfer=" + itoa((char)site->bRecoveredCraftCount)
 *   "soldier_transfer=" + itoa((char)site->bRecoveredSoldierAssign)
 *   "(bool)frommiss=" + itoa(site->bHasLoot)                          -- LDRB at 0x99DF0, unsigned
 * then "objN=<auItemQty[N]>" for N in 0..nObjectCount-1, one line each. Six of the nine values are
 * loaded LDRSB and print signed, so a byte of 0x80 or more prints negative; the port matches that
 * with an explicit (i8).
 *
 * The site walk keeps a second counter that adds 371 (0x173, half the 742-byte record) per site and
 * divides it back at 0x99EB4 to index auItemQty, while the site pointer itself advances by 742
 * (`MOV R3,#0x2E4 / ORR R3,#2` at 0x99F1C). The counter is residue, not a different stride.
 *
 * Returns 0 where the original returns fclose's value; no caller reads it.
 *
 * The original calls Base_FindIndexBySiteId @ 0x5a8d0 with the site byte sign-extended at 0x99AC8,
 * then compares its result with bBaseIdx at 0x99AD4. This is the full signed SiteId search, not the
 * byte-masked Base_FindIndexByTypeByte search. The port now sign-extends the same byte before lookup. */
typedef struct TransferDumpEnv {
    const char *pszAppDir;           /* szAppDir @ 0x1883B0 */
    const i32 *pnSiteCount;          /* nSmapSiteCount @ 0x186410 */
    const SmapSite *aSites;          /* aSmapSites @ 0x162030 */
    i32 nObjectCount;                /* Map_nObjectCount @ 0x114A60 */
    BaseRec *const *apBases;         /* apBases @ 0x114B2C, for the site-id lookup */
    i32 nBaseCount;                  /* nBaseCount @ 0x159D80, read signed there */
    i8 bBaseIdx;                     /* a2, sign-extended at 0x99890 */
    i32 nObjSlot;                    /* a3 */
} TransferDumpEnv;

i32 Transfer_WriteErrorDump(const TransferDumpEnv *pEnv);

/* ------------------------------------------------- the transfer screen's frame
 *
 * Evidence: TransferScreen_Draw @ 0x97fa4 (2988 B), reached from Geoscape_Draw (0x8c180, ported in pufo_geoscape.c:6849).
 *
 * Frame (0x97FB8..0x98150). The two side rails run 24 steps: at x 5 and x 234 a 1x7 block at y 2 + 7*k
 * with index 5*(k + 20) (100..215) and a 1x7 at y 152 + 7*k with the ARM's `RSB #0xE6` index 230 - 5*k
 * (230..115). The top and bottom rails run 24 steps of 5: a 5x1 at x 1 + 5*k and at x 119 + 5*k, on
 * y 5 and y 315, climbing 5*(k + 20) on the left half and falling 250 - 5*k (the ARM's `RSB #0xFA`) on
 * the right. Hex-Rays renders the two descending indices as -26 - 5*k and -6 - 5*k; they are byte
 * indices, not negative values, and the port writes the ARM's own expressions.
 *
 * Column separators (0x98178..0x98214). Four 1-pixel vertical lines at x 130, 168, 183 and 198, from
 * y 69, height 16 * min(pPanelB->nLineCount, pPanelB->nStyle) + 2, index 0xB4.
 *
 * Panels (0x98224..0x9828C). Only while bTransferPanelsBuilt: panel B and panel C get bRepeatArmed = 1
 * (the original routes panel C's value through unk_161F0C, setting it to 1 and back to 0 in the
 * process), then the five panels draw through UiPanelMsg_Draw in the order A, E, C, B, D.
 *
 * Hovered row name (0x98290..0x98340). With the destination overlay closed (byte_1870F8 == 0), a base
 * preview armed (bBaseFacPreviewOk) and the map cursor inside the row band (nSmapCursorY in (75,
 * v9 + 75)), the row under the cursor is named at (10, 282): TEXTS[159] "Soldier" for slot 1000,
 * otherwise apObjects[slot] read as its own szName.
 *
 * Labels (0x98388..0x98510). The prev / next controls and the reset control blit, then TEXTS[1264]
 * "Destination Base" centred at (60 - w/2, 300) and TEXTS[1265] "Transfer" at (180 - w/2, 300) in the
 * cyan pair, then TEXTS[1266] "From: " with the source base's name centred at (120 - w/2, 20).
 *
 * Shortfall panel (0x98538..0x986B4). When the shortfall dword byte_1870C8 is 1, reasons 0..5 each
 * animate a 200x70 panel at (20, 130) through Ui_AnimExpandBlit_IfEnabled and draw TEXTS[405],
 * TEXTS[406], TEXTS[407], TEXTS[408], TEXTS[1443] and TEXTS[1444] -- "Not enouth living quarters",
 * "Not enouth money", "Not enough free hangar space", "Not enough storage space", "Not enough
 * containment space" and "Transfer is not complete" (the first two carry the original's typo) --
 * centred at (120 - w/2, 158), and the draw returns there.
 *
 * Closed state (0x986BC..0x986D8). With the overlay closed, TEXTS[1269] "TO: " with the destination
 * base's name centred at (120 - w/2, 32), and the draw returns.
 *
 * Destination overlay (0x986F8..0x98A98). A full-screen 240x320 blit, a 32-wide strip at x 208 and a
 * 210-wide panel, both 28 * nBaseCount + 4 tall, the base-tab strip through
 * Base_DrawFacilityIconStrip (0x6250c) marked at the selected base, then one bevelled row per base
 * with the base's name centred at (110 - w/2, 28*i + 7). The confirm control blits under the list,
 * TEXTS[1267] "Stop Transfer" centred at (120 - w/2, 28 * nBaseCount + 10), and the selection is
 * resolved through Base_HitTestFacilityStrip on the geoscape cursor pair (0x9897C / 0x98974, not a
 * tap position): the result lands in byte_1870F4, the working site's bFlag1 takes that base's type
 * byte, and a result other than the selected base closes the overlay -- which, per the ORIGINAL
 * BUG note below, cannot happen while this screen's own panels are built. Both cursor
 * coordinates are then zeroed, TEXTS[1268] "Select destination base" draws at (120 - w/2, 282) in a
 * pair whose red channel is 250 - 40 * nMapPhase, and the working site's bFlag1 is written once more
 * from byte_1870F4.
 *
 * Deviations: the original builds every label in a 140-byte stack buffer with unbounded copy loops and
 * two strcats; the port bounds each build to the same 140 bytes. It returns register residue (the
 * last callee's value, or nSmapSiteCount on the overlay path); the port returns the site count on the
 * overlay path and 0 on the two early returns. The port null-guards the panels, the controls, the
 * object array and the site array, where the original dereferences whatever the bring-up left there. */
#define PUFO_TRANSFER_SEP_INDEX   0xB4 /* the four column separators */
#define PUFO_TRANSFER_LABEL_DEST_CAP  1264 /* "Destination Base", at (60, 300) */
#define PUFO_TRANSFER_LABEL_XFER_CAP  1265 /* "Transfer", at (180, 300) */
#define PUFO_TRANSFER_LABEL_FROM      1266 /* "From: " plus the source base name, at y 20 */
#define PUFO_TRANSFER_LABEL_STOP      1267 /* "Stop Transfer", under the base list */
#define PUFO_TRANSFER_LABEL_SELECT    1268 /* "Select destination base", at y 282 */
#define PUFO_TRANSFER_LABEL_TO        1269 /* "TO: " plus the destination base name, at y 32 */
#define PUFO_TRANSFER_SHORTFALL_REC   405  /* TEXTS[405..408] are shortfall reasons 0..3 */
#define PUFO_TRANSFER_SHORTFALL_ALIEN 1443 /* reason 4, alien containment */
#define PUFO_TRANSFER_SHORTFALL_BLOCK 1444 /* reason 5, the blocked transfer */
#define PUFO_TRANSFER_LABEL_MAX       140  /* psz @ sp+0x18 in the original */

typedef struct TransferDrawEnv {
    const GfxColorEnv *pColor;         /* pGX @ 0x1139B8, the colour-pair owner */
    UiAnimBlitEnv *pAnim;              /* the geoscape anim root @ 0x1864F8 */
    UiPanel *pPanelA;                  /* pBuySellPanelA @ 0x161A1C */
    UiPanel *pPanelB;                  /* pBuySellPanelB @ 0x161A00 */
    UiPanel *pPanelC;                  /* pBuySellPanelC @ 0x161A20 */
    UiPanel *pPanelD;                  /* pBuySellPanelD @ 0x160C88 */
    UiPanel *pPanelE;                  /* pBuySellPanelE @ 0x1615F8 */
    u8 *pbPanelsBuilt;                 /* bTransferPanelsBuilt @ 0x1870EC */
    i32 *pnRowCursor;                  /* unk_186BE8, the cached selected row */
    ObjectRecFull *const *apObjects;   /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                  /* Map_nObjectCount @ 0x114A60 */
    const char *pTextRecs;             /* aTextRecs @ 0xB38D0, stride 205 */
    BaseSiteRec *apSiteRecs;           /* 0x186C40, eight 64-byte records, name at +26 */
    UiControl *pCtlPrev;               /* *(slot + 12), the prev-base control */
    UiControl *pCtlNext;               /* *(slot + 16), the next-base control */
    UiControl *pCtlReset;              /* g_pUiCloseCtl @ 0x186B00 */
    UiControl *pCtlConfirm;            /* unk_186BA4 @ 0x186BA4 */
    UiControlBlitFn pfnCtlBlit;
    UiControlBlitFn pfnCtlBlitEx;
    void *pCtlUser;
    u8 *pbOverlayOpen;                 /* byte_1870F8 @ 0x1870F8, the destination overlay */
    const u8 *pbPreviewOk;             /* bBaseFacPreviewOk @ 0x18652D */
    i32 *pnCursorX;                    /* nSmapCursorX @ 0x186BD0, zeroed by the overlay */
    i32 *pnCursorY;                    /* nSmapCursorY @ 0x186BD4, zeroed by the overlay */
    const u32 *pdwShortfall;           /* byte_1870C8, read as a dword */
    const i32 *pnResidue;              /* dword_1870B4[0], the shortfall reason */
    i8 *pbTransferBase;                /* byte_1870F4, written by the strip hit-test */
    SmapSite *aSites;                  /* aSmapSites @ 0x162030 */
    const i32 *pnSiteCount;            /* nSmapSiteCount @ 0x186410 */
    BaseRec *const *apBases;           /* apBases @ 0x114B2C */
    const u8 *pbBaseCount;             /* nBaseCount @ 0x159D80, read signed */
    const u8 *abFacilityGrid;          /* abFacilityGrid @ 0x159768 */
    const u8  *pnMapPhase;             /* nMapPhase @ 0x1139EF, drives the label's red channel. The
                                        * 0x98A0C read is LDRB, so the byte cell is the right view. */
    const BaseStripHitEnv *pStripHit;  /* Base_HitTestFacilityStrip's env */
    void (*pfnDrawIconStrip)(i32 nBaseIdx, void *pUser);
    void *pStripUser;
    i8 nSelectedBaseIdx;               /* a2: Geoscape_Draw passes abFacilityGrid + 0x619 (0x93EA8),
                                        * the geoscape's selected base -- the base the transfer
                                        * leaves. It names the "From: " label (0x984C4), marks the
                                        * strip row (0x98744) and gates the overlay close (0x989D4). */
    i8 nDestBaseIdx;                   /* byte_1870F4, the base the transfer arrives at */
} TransferDrawEnv;

i32 TransferScreen_Draw(const TransferDrawEnv *pEnv);

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
 * an argument. Both callees are ported -- the click handler is BaseAtScreen_CloseThunk @ 0x577c0,
 * a tail call to Geoscape_CloseUI @ 0x8b94c, and the update is BaseAtScreen_Update @ 0x57808 -- but
 * each needs its own env (BaseAtCloseEnv / BaseAtUpdateEnv) over the record at 0x158840..0x158867,
 * so the hooks here are thin wrappers the caller supplies. */
typedef struct BaseAtScreenOpenHooks {
    void (*pfnClick)(void);   /* stored into pCtl->pfnClick */
    void (*pfnUpdate)(i32 nMode);
} BaseAtScreenOpenHooks;

void BaseAtScreen_Open(UiControl *pCtl, i32 nMode, const BaseAtScreenOpenHooks *pHooks);

/* ------------------------------------------------- the screen-close dispatcher
 *
 * Evidence: Geoscape_CloseUI @ 0x8b94c (2100 B). Thirteen widget thunks are two instructions
 * each -- `LDR R0, =0x1864F8` and `B Geoscape_CloseUI` -- so they are one tail call written
 * thirteen times, once per widget: BaseInfoScreen_CloseThunk @ 0x557bc,
 * BaseFacScreen_CloseThunkA @ 0x589c0, BaseFacScreen_CloseThunk @ 0x59b54,
 * SoldierScreen_CloseThunk @ 0x64ae4, ManufScreen_CloseThunk @ 0x6e3d4,
 * ResearchScreen_CloseThunk @ 0x72ffc, BuySellScreen_CloseThunk @ 0x75154,
 * NewBaseScreen_CloseThunk @ 0x615b0 (the odd one: it plays sample 17 at 0x615BC first),
 * FundingScreen_CloseThunk @ 0x5abd8, FundingScreen_ExitThunk @ 0x5c078,
 * Geoscape_CloseUI_ThunkA @ 0x838d0, Geoscape_CloseUI_ThunkB @ 0x96020,
 * Geoscape_CloseUI_ThunkC @ 0x9a8c0. Six more call sites reach it from inside a function:
 * ResearchScreen_CancelSelectedAndClose (0x733DC), ManufScreen_ConfirmAndClose (0x6E9C8),
 * BuySellScreen_CommitTransaction (0x75078), SaveGame_Slot_CloseUI (0x6AFB8), the thunk at
 * 0x7513C, and Options_SaveAudioSettings (0x6B7C4 and 0x6B7F0).
 *
 * The last pair is mutual recursion, and the ARM shows the real path is not the one the pseudocode
 * suggests. Case 18 clears gOptionsScratch[0] at 0x8BCFC and then calls Options_SaveAudioSettings. That
 * function opens with its OWN close call at 0x6B7C4, gated on bOptionsScreenOpen @ 0x159E80 -- a
 * global case 18 does not touch -- so while the options screen is open the second pass runs first.
 * It takes the other branch (OptionsScreen_RebuildWithSound, screen 0), and that leaf's
 * OptionsScreen_Init @ 0x6be74 clears gOptionsScratch[4] on its teardown branch. Control returns, this
 * function re-tests gOptionsScratch[4] at 0x6B7D4, the test fails, and the 0x6B7F0 call is never reached.
 * The 0x6B7F0 arm runs only when bOptionsScreenOpen was already clear on entry. Two close passes
 * either way, and the store at 0x8BCFC is what ends the recursion.
 *
 * In execution order:
 *   1. 0x8B954: Sound_Play(17, 4). Sound_Play takes a second argument and every call site in
 *      the image passes 4; the port's leaf models only the sample.
 *   2. 0x8B96C: a DWORD store of 0 into 0x1870C8. IDA names it byte_1870C8 but the store is
 *      STR, and both writers are dword stores as well: BaseFacScreen_TryBuySelected stores 1
 *      at 0x58960 when nFunds is below the facility cost, Ship_TickRangeRecoverConsume stores
 *      1 at 0x4606C when a craft's range is short. It is the "this action failed" flag and
 *      every screen close clears it.
 *   3. 0x8B978 / 0x8B97C: the 0x1864F8 block's two popup bytes (+0x34, +0x35) clear.
 *   4. 0x8B980..0x8B9D0: the game speed is re-applied from nGeoscapeTickSeconds @ 0x187070.
 *      5 -> GameTime_Set5Sec, 60 -> Set1Min, 300 -> Set5Min, 1800 -> SetSpeedIdx3,
 *      3600 -> SetSpeedIdx4 AND THEN SetSpeedIdx5 (0x8B9E0 is `B loc_8B9CC`, the 43200 arm's
 *      call), 43200 -> SetSpeedIdx5 alone, anything else -> nothing at all. The chain's own
 *      constants disagree with the speed table g_aGeoscapeSpeedSec = {5, 60, 300, 1380, 2460,
 *      3600}: 1800 and 43200 are values no speed control ever writes, and every arm that does
 *      fire rewrites the tick (1800 -> 1380, 3600 -> 2460 -> 3600, 43200 -> 3600). Closing a
 *      screen is therefore not a no-op for the speed.
 *   5. 0x8BA00..0x8BA80: for every base, the five needed-flag passes. nBaseCount is loaded
 *      with LDRSB at 0x8BA10 and re-loaded at 0x8BA70 on each iteration, so it is SIGNED and
 *      a count that grows mid-loop extends the loop. The loop counter it leaves in R2 is the
 *      third argument case 18 hands to Options_SaveAudioSettings -- which reads none of its three
 *      arguments, so all three are dead on arrival (see Options_SaveAudioSettings below).
 *   6. 0x8BA94: a switch on nGeoscapeScreen @ 0x186BC4, `SUB R0,#1 / CMP R0,#0x13`. Cases
 *      1..12, 15, 16, 18, 19 and 20 exist; 13, 14 and 17 fall to the default, which returns.
 *
 * The cases, in address order:
 *   1  (0x8BE8C) screen 0; clear bBasePlaceActive (0x8BE9C, dword_1870B4[2]) and THEN
 *      bBasePlaceFail (0x8BEA4, [1]); free the ten nav controls at 0x159888, the two panel
 *      controls at 0x1598B0 and the three find controls at 0x1598B8 -- each one
 *      UiControl_FreePads plus operator delete when non-null, and the slot is left dangling;
 *      then Smap_ClampScrollY.
 *   2  (0x8BE4C) returns untouched while bBaseFacConfirm @ 0x1870AC is set; otherwise
 *      Smap_ClampScrollY, screen 1, clear bBaseFacDone @ 0x187078 (0x8BE70, the +4 of
 *      bGeoscapeDragActive), clear bBaseFacPlaceArmed @ 0x158888 and return.
 *   3  (0x8BDE8) screen 1; rebuild the base-info panels while 0x15883C is set; then free the
 *      pad at 0x158828 with PadImage_FreeData plus delete. IDA types that global as a UiPanel
 *      and the free proves it is a PadImage.
 *   4  (0x8BFC4) screen 1; toggle the soldier screen while 0x159DE0 is set.
 *   5  (0x8BFE4) while bTransferFlag @ 0x159E10 is set, clear it and return; otherwise clear
 *      both popup bytes, clear the flag and set screen 4.
 *   6  (0x8C018) a five-way ladder: bPlaneListPanelsBuilt -> BuildListPanels; else
 *      bPlaneStoresCommitted -> BuildControls; else either stat panel open -> clear both;
 *      else bPlanePanelsBuilt clear -> return; else bPlaneShowCraftDetail clear -> screen 1
 *      and BuildPanels; else clear bPlaneShowCraftDetail. All three build calls are made with
 *      a garbage first argument: 0x8C01C / 0x8C038 leave R0 as the flag byte that just passed
 *      its test, and 0x8C090 is `MOV R0, #1`. None of the three callees reads a1.
 *   7  (0x8BF4C) bBaseFacDone && bBaseFacDialogOpen -> decrement nSelectedBaseIdx (0x8BF74
 *      LDRSB, so a zero index wraps to 255) and toggle the facility screen, screen 1, clear
 *      both popups. Otherwise, if bBaseFacPlaceArmed is clear: dialog closed -> screen 1 and
 *      clear the popups, dialog open -> the toggle path. Otherwise clear bBaseFacPlaceArmed
 *      and clear the popups.
 *   8  (0x8BDB0) bUiRowSelected -> ResearchScreen_StartSelected; else screen 1 and, while
 *      bResearchPanelsBuilt is set, ResearchScreen_BuildPanels.
 *   9  (0x8BD74) bManufProductSelected -> ManufScreen_Draw; else while 0x15EE4C is set,
 *      ManufScreen_BuildPanels and screen 1.
 *   10 (0x8BE28) screen 1; rebuild the transfer panels while bTransferPanelsBuilt is set.
 *   11 (0x8C0BC) set bPopupVisibleB, rebuild the buy/sell controls while its flag is set,
 *      screen 1, free pBuySellScreenPad @ 0x161A08, then clear BOTH popup bytes -- so the 1
 *      stored at 0x8C0C4 exists only for the duration of the build call.
 *   12 (0x8C108) the same shape with BuySellScreen_BuildPanels, then Base_RemoveAllShips and
 *      Base_RemoveAllSoldiers on nSelectedBaseIdx.
 *   15 (0x8BCAC) FundingScreen_Open, then screen 0.
 *   16 (0x8BD2C) 0x187310 set -> Ufopaedia_RefreshEntry; else 0x18730C set -> clear it and
 *      return; else Ufopaedia_Open, screen 0, Smap_ClampScrollY.
 *   18 (0x8BCBC) while gOptionsScratch[4] is set: if gOptionsScratch[0] (read SIGNED, 0x8BCD0 LDRSB)
 *      is non-zero, clear it and call Options_SaveAudioSettings(gOptionsScratch, that byte, the
 *      base-loop counter residue) -- all three arguments unused by the callee; else
 *      OptionsScreen_RebuildWithSound and screen 0. Then,
 *      while byte_159E80 is set, screen 0 and OptionsScreen_Open(-1). Then clear
 *      bGeoscapeDragActive @ 0x187074 and return.
 *   19 (0x8BB38) the monthly economy close, described on the case body below.
 *   20 (0x8BAF8) BaseAtScreen_Open(&0x158840, -1), screen 0, and if 0x158850 is set,
 *      Map_bMissionMode = 1 and Smap_BeginCraftSelect.
 *
 * Five cases share one tail -- 0x8BFF4 is `*v12 = 0` with v12 pointing at a different global
 * in each one (bBaseFacPlaceArmed, bTransferFlag, bPlaneShowCraftDetail, 0x18730C,
 * bGeoscapeDragActive) -- and it is followed by `B def_8BA94`, the jumptable default, so it
 * clears and returns. Hex-Rays renders LABEL_86 inside case 16 as if the other four fell
 * through into it; they do not.
 *
 * Case 19, 0x8BB38..0x8BC9C: free pFundPad @ 0x1595DC when non-null (and only then re-read
 * the base count at 0x8BB60 -- on the null path the count is the base loop's residue); sum
 * Base_CalcMonthlyUpkeep over the bases; then
 * nFunds += bar2 - upkeep + bar1 + bar3 + bar0 + bar4 (0x8BBAC..0x8BBE0, the five dwords at
 * 0x186594..0x1865A4). Below -3000000 (0xFFD23940 at 0x8BBDC, BGE at 0x8BBE8) it is the
 * bankruptcy end: GeoscapeUi_FreeAll(1), EndScreen_Init(0), Map_bTickHold = 1,
 * Map_bBattleEnded = 1, return. Otherwise find the FIRST base whose Base_CalcPsiLabCap is
 * positive, store its index into byte_158834 and byte_1870F4 (both STRB), and open the psi
 * screen at screen 17; with no such base, or no bases at all, screen 0.
 *
 * Deviations:
 *   1. The original reaches every global directly. The port hands them through
 *      GeoscapeCloseEnv, and the callees stay hooks on the same env -- a faithful seam for
 *      the original's indirect calls through the 0x1864F8 block; every one named below is
 *      now ported in the library.
 *   2. The original frees the nav controls and the two pads with operator delete and never
 *      nulls the slot. The port frees through the same leaf and writes NULL into the slot,
 *      because a dangling 0x159888 entry is read again by Geoscape_BuildNavControls and by
 *      every nav handler.
 *   3. The three BasePlaneScreen calls pass a garbage a1 (see case 6). The port passes NULL.
 *   4. Case 19's upkeep loop is gated on the SIGNED base count at 0x8BB74 and 0x8BC30; a
 *      negative count skips both loops, which is also what the psi search does. */
/* The dispatcher reaches screens declared further down this header; the arrays themselves stay
 * with their own definitions. */
typedef struct NavControls NavControls;
typedef struct ResearchStartEnv ResearchStartEnv;
typedef struct ResearchBuildEnv ResearchBuildEnv;
typedef struct ManufDrawEnv ManufDrawEnv;
typedef struct ManufBuildEnv ManufBuildEnv;
typedef struct BaseFacToggleEnv BaseFacToggleEnv;
typedef struct SoldierScreenEnv SoldierScreenEnv;
typedef struct FundingToggleEnv FundingToggleEnv;
typedef struct FundingOpenEnv FundingOpenEnv;
typedef struct OptionsScreenHooks OptionsScreenHooks;
typedef struct SaveAudioEnv SaveAudioEnv;
typedef struct EndScreenInitEnv EndScreenInitEnv;

/* The bankruptcy limit at 0x8BBDC: 0xFFD23940 read as a signed dword. */
#define PUFO_FUNDS_BANKRUPT_LIMIT (-3000000)

typedef struct GeoscapeCloseEnv {
    /* 0x8B954..0x8B9D0, the head. */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void *pSoundUser;
    i32   *pnActionFail;             /* 0x1870C8, IDA's byte_1870C8: stored as a dword */
    u8    *pbPopupA;                 /* bBaseFacPreviewActive @ 0x18652C (the port's older name for it is
                                      * bPopupVisibleA; the IDB names it after the placement preview) */
    u8    *pbPopupB;                 /* bBaseFacPreviewOk @ 0x18652D (older port name bPopupVisibleB) */
    GeoscapeWorld *pWorld;           /* nTickSeconds @ 0x187070 */
    UiControl *const *apSpeedCtl;    /* the six nav speed controls */

    /* 0x8BA00..0x8BA80, the per-base needed-flag pass and its budgets. */
    u8    *abFacilityGrid;           /* abFacilityGrid @ 0x159768 */
    i8    *pnBaseCount;              /* nBaseCount @ 0x159D80, read SIGNED */
    i8    *pnSelectedBaseIdx;        /* nSelectedBaseIdx @ 0x159D81, read SIGNED */
    Ship *const *apCraft;            /* Map + 0x10AC */
    u8    *pnCraftCount;             /* Map_nCraftCount @ 0x114B4C */
    Soldier **apSoldiers;            /* Map + 0x1198; Base_RemoveAllSoldiers writes through it */
    i32    nSoldierCount;
    ObjectRecFull *const *apObjects; /* Map + 0x108 */
    i32    nObjectCount;
    const SmapSite *apSites;         /* 0x162030 */
    i32    nSiteCount;               /* 0x186410 */
    BaseRec *const *apBases;         /* Map + 0x1174, the transfer budgets need it */
    const u16 *apManufCol;           /* 0x15E19C, PUFO_PRODUCT_ENTRIES_PER_BASE per base */
    const u16 *apResearchCol;        /* auResearchCol @ 0x16022C */
    i32    nResearchEntries;

    /* 0x8BA94, the switch. */
    i32   *pnScreenId;               /* nGeoscapeScreen @ 0x186BC4 */
    SmapView *pView;                 /* gSmapCam @ 0x1863E8 */
    const PadImage *pStripPad;       /* 0x186BBC, the strip height Smap_ClampScrollY needs */
    NavControls *pNav;               /* 0x159888 / 0x1598B0 / 0x1598B8, case 1 frees them */
    u8    *pbPlaceActive;            /* bBasePlaceActive @ 0x1870BC, dword_1870B4[2] */
    u8    *pbPlaceFail;              /* bBasePlaceFail @ 0x1870B8, dword_1870B4[1] */
    u8    *pbBaseFacConfirm;         /* bBaseFacConfirm @ 0x1870AC */
    u8    *pbBaseFacDone;            /* bBaseFacDone @ 0x187078 */
    u8    *pbBaseFacArmed;           /* bBaseFacPlaceArmed @ 0x158888 */
    u8    *pbBaseFacDialog;          /* bBaseFacDialogOpen @ 0x158884 */
    u8    *pbDragActive;             /* bGeoscapeDragActive @ 0x187074 */
    u8    *pbBaseInfoBuilt;          /* 0x15883C */
    PadImage **ppBaseInfoPad;        /* 0x158828, freed as a PadImage */
    u8    *pbSoldierOpen;            /* 0x159DE0 */
    u8    *pbTransferFlag;           /* bTransferFlag @ 0x159E10 */
    u8    *pbTransferBuilt;          /* bTransferPanelsBuilt @ 0x1870EC */
    u8    *pbPlaneListBuilt;         /* bPlaneListPanelsBuilt @ 0x161F24 */
    u8    *pbPlaneStores;            /* bPlaneStoresCommitted @ 0x161F28 */
    u8    *pbStatPanelA;             /* bStatPanelAOpen @ 0x161F18 */
    u8    *pbStatPanelB;             /* bStatPanelBOpen @ 0x161F1C */
    u8    *pbPlaneBuilt;             /* bPlanePanelsBuilt @ 0x161F08 */
    u8    *pbPlaneDetail;            /* bPlaneShowCraftDetail @ 0x161F20 */
    u8    *pbResearchBuilt;          /* bResearchPanelsBuilt @ 0x160C7C */
    u8    *pbRowSelected;            /* bUiRowSelected @ 0x160C80 */
    u8    *pbManufSelected;          /* bManufProductSelected @ 0x15EE34 */
    u8    *pbManufBuilt;             /* 0x15EE4C */
    u8    *pbBuySellCtlBuilt;        /* bBuySellControlsBuilt @ 0x161A24 */
    u8    *pbBuySellBuilt;           /* bBuySellPanelsBuilt @ 0x161A28 */
    PadImage **ppBuySellPad;         /* pBuySellScreenPad @ 0x161A08 */
    u8    *pbUfopaediaOpen;          /* 0x18730C */
    u8    *pbUfopaediaDirty;         /* 0x187310 */
    u8    *pbOptionsOpen;            /* 0x159E80 */
    i8    *pnOptionsMode;            /* gOptionsScratch[0], read SIGNED at 0x8BCD0 */
    const u8 *pbOptionsActive;       /* gOptionsScratch[4] */
    i32   *pnFunds;                  /* nFunds @ 0x1598D8 */
    const i32 *anFundBar;            /* g_nFundBar0..4 @ 0x186594..0x1865A4 */
    PadImage **ppFundPad;            /* pFundPad @ 0x1595DC */
    u8    *pnShownBaseIdx;           /* byte_158834, written with STRB at 0x8BC74 */
    i8    *pnPsiBaseIdx;             /* byte_1870F4, read SIGNED by BaseInfoScreen_OnOk */
    u8    *pbTickHold;               /* Map_bTickHold @ 0x113A12 */
    u8    *pbBattleEnded;            /* Map_bBattleEnded @ 0x113A14 */
    u8    *pbMissionMode;            /* Map_bMissionMode @ 0x128AD5 */
    const u8 *pbBaseAtAssault;       /* 0x158850, the flag case 20 tests after BaseAtScreen_Open */
    const i32 *anSoldierIdList;      /* g_aSoldierIdList, the list case 12 hands over */

    /* The ported callees this dispatcher drives. */
    const ResearchStartEnv *pResearchStart;
    ResearchBuildEnv *pResearchBuild;
    ManufDrawEnv *pManufDraw;
    ManufBuildEnv *pManufBuild;
    TransferBuildEnv *pTransferBuild;
    BuySellControlsEnv *pBuySellCtl;
    BuySellBuildEnv *pBuySell;
    const BaseFacToggleEnv *pFacToggle;
    const SoldierScreenEnv *pSoldierScreen;
    FundingToggleEnv *pFundingToggle;
    const FundingOpenEnv *pFundingOpen;
    const OptionsScreenHooks *pOptionsHooks;
    SaveAudioEnv *pSaveAudio;              /* Options_SaveAudioSettings @ 0x6b7ac */
    struct OptionsOpenEnv *pOptionsOpen;   /* OptionsScreen_Open @ 0x6d0ac, called with nMode -1 */
    const BaseClearShipsEnv *pClearShips;
    const SmapCraftSelectEnv *pCraftSelect;

    /* The callees the original reaches indirectly; each is ported and the shell supplies
     * the thunk. */
    void (*pfnBuildBaseInfoPanels)(void *pUser);  /* BaseInfoScreen_BuildPanels (0x557f4) */
    void (*pfnBuildPlaneListPanels)(void *pUser); /* BasePlaneScreen_BuildListPanels (0x7a3f4) */
    void (*pfnBuildPlaneControls)(void *pUser);   /* BasePlaneScreen_BuildControls (0x7b014) */
    void (*pfnBuildPlanePanels)(void *pUser);     /* BasePlaneScreen_BuildPanels (0x7cc8c) */
    void (*pfnUfopaediaRefresh)(void *pUser);     /* Ufopaedia_RefreshEntry (0x9a014) */
    void (*pfnUfopaediaOpen)(void *pUser);        /* Ufopaedia_Open (0x9a8cc) */
    void (*pfnPsiInit)(void *pUser);              /* PsiScreen_Init (0x72b40) */
    void (*pfnBaseAtOpen)(void *pUser);           /* BaseAtScreen_Open (0x588cc) */
    void (*pfnFreeGeoscapeUi)(i32 bFreeWorld, void *pUser); /* GeoscapeUi_FreeAll (0x86d20), ui/ */
    const EndScreenInitEnv *pEndScreen;           /* EndScreen_Init @ 0x388b0 */
    void (*pfnInitEndScreen)(u8 bGoodEnd, void *pUser);
    void *pUser;

    /* The world counts can change after the close env is wired. A NULL pointer keeps standalone
     * callers on the scalar fixture values above. */
    const i32 *pnSoldierCountLive;
    const i32 *pnSiteCountLive;
} GeoscapeCloseEnv;

void Geoscape_CloseUI(const GeoscapeCloseEnv *pEnv);

void BaseInfoScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);
void BaseFacScreen_CloseThunkA(const GeoscapeCloseEnv *pEnv);
void BaseFacScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);
void SoldierScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);
void ManufScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);
void ResearchScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);
void BuySellScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);
void TransferScreen_CloseThunk(const GeoscapeCloseEnv *pEnv); /* Geoscape_CloseUI_ThunkB @ 0x96020 */
void NewBaseScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);

/* OptionsScreen_ReturnToTitle @ 0x6b46c: clears the tab byte (STRB 0 at 0x6B478), closes the
 * geoscape screen, frees every geoscape widget, and re-inits the startup UI. pbTab is the same
 * gOptionsScratch[0] the two tab helpers write. */
typedef struct TitleReturnHooks {
    void (*pfnCloseUi)(void *pUser);              /* Geoscape_CloseUI (0x8b94c) */
    void (*pfnFreeGeoscapeUi)(i32 nMode, void *pUser); /* GeoscapeUi_FreeAll (0x86d20) */
    i32 (*pfnStartupUi)(void *pUser);             /* GameUI_InitStartup (0x38a28) */
    void *pUser;
} TitleReturnHooks;

i32 OptionsScreen_ReturnToTitle(i8 *pbTab, const TitleReturnHooks *pHooks);

/* Five more copies of the same two instructions: FundingScreen_CloseThunk @ 0x5abd8,
 * FundingScreen_ExitThunk @ 0x5c078, Geoscape_CloseUI_ThunkA @ 0x838d0,
 * Geoscape_CloseUI_ThunkB @ 0x96020, Geoscape_CloseUI_ThunkC @ 0x9a8c0. Together with the
 * eight above, thirteen widget handlers all reach the same dispatcher. */
void FundingScreen_CloseThunk(const GeoscapeCloseEnv *pEnv);
void FundingScreen_ExitThunk(const GeoscapeCloseEnv *pEnv);
void Geoscape_CloseUI_ThunkA(const GeoscapeCloseEnv *pEnv);
void Geoscape_CloseUI_ThunkB(const GeoscapeCloseEnv *pEnv);
void Geoscape_CloseUI_ThunkC(const GeoscapeCloseEnv *pEnv);

/* Geoscape_OnMapClick_Thunk @ 0x857e8: a widget callback shim that drops its four
 * arguments and tail-calls Geoscape_OnMapClick (0x8b4a8), which is ported below with its own env. */
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
 * (LDRB R3, [R3,#0xE] at 0x72CE4), the first of the five required-item bytes.
 *
 * The lookup is apObjects[item]->auPerBaseVal[nSelectedBaseIdx]: LDR [R3,#0x108] at 0x72CEC
 * picks the pointer out of the Map-block array at 0x1139B8+0x108, LDRH [R3,#0x2A] at 0x72D00
 * reads the u16. The original tests neither the pointer nor the item index, and reads the base
 * index as the signed byte at 0x159D81; the port takes both as arguments and reproduces the
 * unguarded dereference. */
typedef struct ResearchTopicLive {
    u8  abUnk0[4];    /* +0x00..0x03: the on-disk wUnk0/wUnk2 pair, and the only place the two (lo,hi)
                       * topic ranges exist. ResearchScreen_RebuildTopicList reads them as
                       * aTopics[n + 1].abUnk0 -- [0]/[1] for the unlock roll (0x745AC LDRB [R9,#0x22],
                       * 0x745C0 LDRB [R9,#0x23]) and [2]/[3] for the availability scan (0x74868). The
                       * record being tested is aTopics[n], so the ranges sit one record later: for
                       * n == 143 the four bytes are 0x160228, the gap between the 144-record table and
                       * auResearchCol @ 0x16022C, which nothing in the IDB writes. No other function
                       * reads these four bytes of a record, so the pairing is visible only there. */
    u16 wGate4;   /* +0x4: two meanings on one u16. The weapon fire gate reads it as a value
                  * (pufo_combat.h, FireGate evidence), and ResearchScreen_BuildPanels reads it as the
                  * topic's TEXTS.DAT name record index -- 0x7373C LDRH [R10,#4], 0x73744 MUL by 0xCD
                  * against 0xB38D0. Neither use is a mistake; the name records only the first. */
    u16 wUnk6;
    u16 wTotal;   /* +0x8: the progress divisor, 0x73174 LDRH [R9,#8] */
    u16 wProgress;/* +0xA: the progress numerator, 0x73160 LDRH [R9,#0xA] */
    u8  bActive;  /* +0xC: 1 = the topic is on the screen list (0x73034, 0x730B8) */
    u8  bUnlocked;/* +0xD: read as 1 by the manufacturing row filter (0x6EA30, 0x6EEB0) and as 0 by
                   * Unit_SelectBestFireMode (0x47A10, 0x47B3C), which reaches the same record through
                   * byte_AA968[ObjectRec.uSortKey] -- the topic that unlocks that weapon. The old name
                   * here was bGate13, after the offset the tactical weapon menu read it at. */
    u8  abNeedItems[5];   /* +0x0E..0x12: the five items this topic needs. Research_TopicNeedsItem tests
                           * only the first (0x72CE4 LDRB [R3,#0xE]); ResearchScreen_RebuildTopicList walks
                           * all five (0x747E4 ADD R3,R9,#0xE), treats 254 and 255 as "no item", and for a
                           * real id reads apObjects[id]->auPerBaseVal[base] (0x74848). */
    u8  abPreTopics[5];   /* +0x13..0x17: prerequisite topic numbers. Their bUnlocked is ANDed into a gate
                           * at 0x742AC (record 143's own gate) and 0x747AC (every walked topic). */
    u8  abItemIds[5]; /* +0x18..0x1C: five object slot ids. ManufScreen_BuildPanels (0x6EA44, 0x6EEC8)
                       * reads them with LDRB [R3,#0x18+j], tests abManufRowFlag[id] == 1, and then uses the
                       * same byte as an apObjects index to copy the item's name (0x6EEDC). */
    u8  abActivateTopics[5]; /* +0x1D..0x21: topic numbers switched to bActive = 1 when this topic finishes
                              * (0x74584 ADD R2,R9,#0x1D). Geoscape_Draw reads the first of them at 0x8D4A8
                              * to pick the caption for nResearchFinishedTopic. */
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

/* ResearchScreen_IncSelectedQty @ 0x72ea0 and ResearchScreen_DecSelectedQty @ 0x72f70: the
 * research screen's "+1 / -1" buttons, applied to the topic the topic panel is sitting on.
 *
 * Both reach the topic through the same pair of globals. The topic panel at 0x160C68 carries a
 * topic number per row in its anLineAux (ResearchScreen_RebuildTopicList (0x74270, ported in this file behind ResearchRebuildEnv)
 * fills it),
 * and 0x160C74 caches that panel's nUnk3032 row cursor -- PlaySample7_SetFlags_Cache0xBD8
 * @ 0x73328 is the write, `unk_160C74 = unk_160C68->nUnk3032`, which is why IDA's own name for
 * that handler already says Cache0xBD8. The quantity entry is then auResearchCol[topic +
 * 144*base]: the ARM composes it as MUL base,#0x90, ADD topic, LSL #1, over the 0x15EF08 anchor
 * plus 0x1324 (0x72F0C..0x72F58), and reads and writes that one slot.
 *
 * Inc gates the write on two tests in this order:
 *   1. Base_GetScientistCount > 0                  (0x72EB8) the base has a scientist to spare
 *   2. Base_HasResearchSlotFree(nActiveResearch+1)  (0x72ED4, 0x72EEC) a lab slot is free
 * and only then stores qty + 1 truncated to 16 bits (LSL #16 / LSR #16 at 0x72F50). Dec has no
 * gate beyond the entry being non-zero, and stores qty - 1 the same truncated way -- ADDHI R3,
 * R3, #0xFFFF at 0x72FDC is the ARM's way of writing minus one.
 *
 * The return value is not a status code. On success both leave 4900 in R0, which is the research
 * column's byte offset from the 0x15EF08 anchor, register residue from the address arithmetic
 * (MOV R0,#0x1300 then ORR R0,R0,#0x24). Inc's early exits instead return what the last call
 * left there: the scientist count when it is <= 0, or Base_HasResearchSlotFree's 0 when the lab
 * gate fails. Dec returns 4900 always, including when it stored nothing.
 *
 * Deviations: the original reads the panel pointer, the row cache, the count byte and the base
 * index straight out of globals and derefs the panel without testing it; the port takes them as
 * arguments and refuses (returning 0) when the panel, the column, the base index or the row is
 * missing or out of range. */
#define PUFO_RESEARCH_COL_OFFSET 4900 /* 0x1324: auResearchCol @ 0x16022C minus the
                                       * aResearchTopics anchor @ 0x15EF08. The value both
                                       * quantity buttons return. */
typedef struct ResearchQtyEnv {
    UiPanel             *pTopicPanel;       /* pResearchTopicPanel @ 0x160C68 */
    const i32           *pnTopicRow;        /* nResearchTopicRowCache @ 0x160C74, a cached nUnk3032 */
    u16                 *apResearchCols;    /* auResearchCol @ 0x16022C, 144 u16 per base */
    const u8            *puResearchEntries; /* nResearchEntryCount @ 0x160C4C, byte, shared by every base */
    const ObjectRecFull *pScientistRec;     /* Map_apObjects113 @ 0x113C84, the scientists item */
    u8                  *abFacilityGrid;    /* gBaseFacGrid @ 0x159768; mutable because the start
                                             * button re-runs the two needed passes over it */
    const i8            *pnSelectedBaseIdx; /* nSelectedBaseIdx @ 0x159D81 */
} ResearchQtyEnv;

i32 ResearchScreen_IncSelectedQty(const ResearchQtyEnv *pEnv);
i32 ResearchScreen_DecSelectedQty(const ResearchQtyEnv *pEnv);

/* ResearchScreen_UpdateProgress @ 0x72d14 (284 B): the research screen's allocate button. Its
 * handler pointer is installed by ResearchScreen_Draw (0x73a34) at 0x7417C and 0x74208 onto the
 * control at 0x186B94, positioned at 100,164 -- the two cross-references to this function are
 * those stores, not calls.
 *
 * It reads the base's scientist total first, through Base_GetItemQtyA @ 0x66198, which is
 * Map_apObjects113->auPerBaseVal[base], and returns it when it is <= 0 without touching
 * anything. Otherwise it runs exactly that many iterations of the SAME two gates
 * ResearchScreen_IncSelectedQty uses, incrementing the SAME auResearchCol entry each time they
 * pass. The total never changes inside the loop, so the iteration count is fixed, but the
 * availability half of the gate falls by one on every successful allocation and so does the lab
 * half (it asks for Base_CountActiveResearch + 1, and that sum grows by one per allocation). The
 * button therefore commits as much of the base's spare scientists as the topic can take --
 * min(available, lab capacity - already active) units -- and then keeps looping without storing
 * until the counter reaches the total. The loop re-reads the total at 0x72E20 and compares signed
 * (CMP R9, R0 / BLT), and the function returns that last read. */
i32 ResearchScreen_UpdateProgress(const ResearchQtyEnv *pEnv);

/* ResearchScreen_CancelSelectedAndClose @ 0x73380 (116 B): the research screen's cancel button,
 * installed twice by ResearchScreen_Init (0x749a8) at 0x74AC0 and 0x74BD0. It writes 0 into the
 * same auResearchCol entry the quantity buttons use -- no gate, not even a non-zero test, just
 * MOV R2,#0 / STRH R2 at 0x733D4 -- and then tail-calls Geoscape_CloseUI(&MEMORY[0x1864F8]).
 * Because the close is a tail call it runs whatever the store did, and the port keeps that order
 * even when its own guards skip the store. */
typedef struct ResearchCancelEnv {
    ResearchQtyEnv     qty;   /* the panel, row cache, column and base the entry is found through */
    const GeoscapeCloseEnv *pClose; /* Geoscape_CloseUI @ 0x8b94c, tail-called at 0x733DC */
} ResearchCancelEnv;

void ResearchScreen_CancelSelectedAndClose(const ResearchCancelEnv *pEnv);

/* ResearchScreen_AccumulateAllocated @ 0x74914 (148 B): the hourly research tick, called from
 * Geoscape_TickHourly @ 0x5D30C. For each of the eight base slots it walks the first
 * nResearchEntryCount entries of that base's allocation column and adds every non-zero one into
 * aResearchTopics[i].wProgress -- the topic index is the column index, so all eight bases
 * accumulate into the same 144 topic records. The add is an LDRH / ADD / STRH triple
 * (0x74964..0x7496C) with the result truncated to 16 bits, so it wraps. It ends with a tail call
 * into ResearchScreen_RebuildTopicList (0x74270, ported in this file behind ResearchRebuildEnv), so the rebuild is a hook and this
 * returns what that hook returns.
 *
 * Two ARM quirks worth recording. The inner loop bound is re-read from nResearchEntryCount on
 * every iteration that saw a non-zero entry (LDRBHI at 0x7497C), which cannot change the outcome
 * because nothing here writes that byte. And the outer loop is a fixed eight (MOV R6,#8 / SUB /
 * BHI), not the live base count, so empty base slots are walked too. */
typedef struct ResearchAccumulateEnv {
    ResearchTopicLive *aTopics;           /* aResearchTopics @ 0x15EF08, 144 records of 34 bytes */
    const u16         *apResearchCols;    /* auResearchCol @ 0x16022C, 144 u16 per base */
    const u8          *puResearchEntries; /* nResearchEntryCount @ 0x160C4C, a byte */
    i32 (*pfnRebuild)(void *pUser);       /* ResearchScreen_RebuildTopicList @ 0x74270, ported in this file behind ResearchRebuildEnv */
    void              *pRebuildUser;
} ResearchAccumulateEnv;

i32 ResearchScreen_AccumulateAllocated(const ResearchAccumulateEnv *pEnv);

/* ------------------------------------------------- the manufacturing engineers buttons
 *
 * The manufacturing screen is the mirror of the research one: a product panel whose anLineAux
 * carries a product number per row, a cached copy of that panel's nUnk3032 row cursor, a per-base
 * column, and the same two gates with engineers and workshops in place of scientists and labs. The
 * column here is auManufCol @ 0x15E19C and it holds the ENGINEERS ASSIGNED to each product: the
 * screen prints TEXTS[1180] "Инженеров" at (10,216) and TEXTS[1181] "занято: " with the value at
 * (10,230), and 0x70908 reads it with LDRH (the units column beside it, aManufProductB, is read with
 * LDR at 0x709D0 under TEXTS[1182] "Единиц" + TEXTS[1183] "произвести: "). The names in this file
 * follow those captions: Part 115 found them swapped and moved them.
 * Evidence: ManufScreen_IncEngineers @ 0x6dddc (228 B), ManufScreen_ReduceSelectedQtyToOne
 * @ 0x6dec0 (172 B), ManufScreen_DecEngineers @ 0x6e09c (160 B), ManufScreen_DecUnits @
 * 0x6e330 (164 B), ManufScreen_UpdateProgress @ 0x6df6c (304 B).
 *
 * The entry index is auManufCol[product + 200*base], composed over the 0x159E98 product-block
 * anchor plus 0x4304 (17156), and three of the buttons return that 17156 -- the same kind of
 * address residue the research buttons return as 4900.
 *
 * DecUnits @ 0x6e330 is the one button here that does not read auManufCol. Its floor is the
 * committed-units column auManufCommitted @ 0x15D09C (block + 0x3204, the counter
 * ManufScreen_CommitProduction raises at 0x6F6C0 once per unit it builds): 0x6E398/0x6E3A0 build
 * the address with MOV R11,#0x3200 / ORR R11,R11,#4 and 0x6E3A4 reads it with LDRH, so the order
 * may fall only while it stays strictly above what has already been built, plus one. Part 116
 * corrected this: the comment here and the IDB note both named auManufCol, which this function
 * never reads.
 *
 * All five open by reading bManufQtyGate @ 0x15EE50 and bail out returning that byte when it is
 * non-zero. Worth stating plainly: no code in the binary ever writes a non-zero value to that
 * byte -- the only store to it anywhere is ManufScreen_SelectListed writing 0 at 0x6E970 -- so in
 * the shipped build the gate is dead. The port keeps it because the original has it.
 *
 * ManufScreen_ReduceSelectedQtyToOne is renamed in the IDB from ManufScreen_GetSelectedProductId:
 * it never returns a product id. It decrements the selected product's engineer count one at a time
 * while the value is above 1 (0x6DF30..0x6DF54), which is a clamp to 1. */
#define PUFO_MANUF_COL_OFFSET 17156 /* 0x4304: auManufCol @ 0x15E19C minus the product-block
                                     * anchor @ 0x159E98. The value three buttons return. */
#define PUFO_MANUF_ANCHOR 0x159E98  /* ManufScreen_DecUnits leaves the product-block anchor in
                                     * R0 (ADDGT R2,R0,R2 is the last thing that touches it).
                                     * Register residue; reproduced, not interpreted. */
typedef struct ManufQtyEnv {
    UiPanel             *pProductPanel;   /* pManufProductPanel @ 0x15EE44 */
    const i32           *pnProductRow;    /* nManufProductRowCache @ 0x15EE3C, a cached nUnk3032 */
    u16                 *apManufCols;     /* auManufCol @ 0x15E19C, 200 u16 per base: the engineers
                                           * assigned to each product */
    i32                 *apProductB;      /* aManufProductB @ 0x15B79C, 200 dwords per base: the units
                                           * to produce -- DecUnits compares it against the column below */
    const u16           *apProductC;      /* auManufCommitted @ 0x15D09C, 200 u16 per base: the units
                                           * already built -- DecUnits' floor, read not written */
    const u8            *puManufEntries;  /* nResearchEntryCount @ 0x160C4C, a byte, shared with research */
    const ObjectRecFull *pEngineerRec;    /* Map_apObjects112 @ 0x113C80, the engineers item */
    const u8            *abFacilityGrid;  /* gBaseFacGrid @ 0x159768 */
    const i8            *pnSelectedBaseIdx; /* nSelectedBaseIdx @ 0x159D81 */
    const u8            *pbQtyGate;       /* bManufQtyGate @ 0x15EE50 */
} ManufQtyEnv;

i32 ManufScreen_IncEngineers(const ManufQtyEnv *pEnv);
i32 ManufScreen_DecEngineers(const ManufQtyEnv *pEnv);
i32 ManufScreen_ReduceSelectedQtyToOne(const ManufQtyEnv *pEnv);
i32 ManufScreen_DecUnits(const ManufQtyEnv *pEnv);
i32 ManufScreen_UpdateProgress(const ManufQtyEnv *pEnv);

/* ManufScreen_SelectListed @ 0x6e914 (120 B): the manufacturing product list's row click, the
 * sibling of PlaySample7_SetFlags_Cache0xBD8 @ 0x73328 on the research screen. It plays sample 7,
 * caches the panel's nUnk3032 row cursor into nManufProductRowCache, sets bManufProductSelected,
 * clears bUiAnimStarted and clears bManufQtyGate.
 *
 * Two things it does that the research sibling does not. It also clears the panel's own nUnk3032,
 * and it clears it with a byte store (MOV R2,#0 / STRB R2,[R0,#0xBD8] at 0x6E958) rather than a
 * word store, so only the low byte of the dword cursor is zeroed and the upper 24 bits survive --
 * for any cursor under 256 that is a full clear, and the port reproduces the byte width exactly.
 * And it clears bManufQtyGate -- the clearing side of the pair. The setting side is
 * ManufScreen_DrawFrame @ 0x7019C and its three twins (0x70358, 0x704B4, 0x70610): a component
 * row whose requirement exceeds the base's stock writes 1 (and paints that row red at 0x70194).
 *
 * Deviation: the original leaves the address of bManufQtyGate in R0 (0x6E974, residue from the
 * last LDR); the port returns the cached row, which is what the research sibling returns. */
typedef struct ManufSelectEnv {
    UiPanel *pProductPanel;      /* pManufProductPanel @ 0x15EE44 */
    i32     *pnProductRow;       /* nManufProductRowCache @ 0x15EE3C */
    u8      *pbProductSelected;  /* bManufProductSelected @ 0x15EE34 */
    u8      *pbQtyGate;          /* bManufQtyGate @ 0x15EE50 */
    u8      *pbAnimStarted;      /* bUiAnimStarted @ 0x187080 */
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void *pSampleUser;
} ManufSelectEnv;

i32 ManufScreen_SelectListed(const ManufSelectEnv *pEnv);

/* ------------------------------------------------- clearing a product slot, and the engineer "+"
 *
 * Evidence: ManufScreen_ClearProductSlot @ 0x6f8cc (216 B), ManufScreen_ConfirmAndClose @ 0x6e98c
 * (92 B), ManufScreen_IncUnits @ 0x6e13c (500 B).
 *
 * The product table is three parallel arrays plus the engineers column, all contiguous and all
 * 200 entries per base (see the product chunk note in game/pufo_save.h):
 *
 *     A  u32  @ 0x159E9C   B  u32 @ 0x15B79C (aManufProductB)
 *     C  u16 @ 0x15D09C (auManufCommitted)   auManufCol u16 @ 0x15E19C
 *
 * ClearProductSlot zeroes one index of all four. Its first argument is never read. Products
 * 119..121 -- the three hangar products, the same range ManufScreen_IncUnits splits on --
 * additionally clear the selected base's byte in the eight-byte base grid table at
 * abFacilityGrid + 1620 = 0x159DBC, and they do it by reading it and subtracting that same value
 * back (BaseGrid_GetByte0x654 then BaseGrid_SubByte0x654 with the amount it just returned), so
 * the net effect is "zero it if it was non-zero".
 *
 * Deviation: the original bounds nothing. nProduct is masked to a byte but 200*nBase + 255 runs
 * past the 1600-entry arrays, and apBases[nSelectedBaseIdx] is dereferenced whatever that signed
 * byte holds. The port refuses the four stores outside 0..1599 and the base lookup outside
 * 0..nBaseCount-1.
 *
 * Return value: the original leaves 200*nBase + nProduct in R0 on the bail-out paths and 1620 (the
 * constant residue BaseGrid_SubByte0x654 returns) on the hangar path. All four call sites --
 * ManufScreen_Draw 0x6E48C, ManufScreen_ConfirmAndClose 0x6E9C4, ManufScreen_CommitProduction
 * 0x6F7C4 and 0x6F888, ManufScreen_RefreshBaseTabs 0x710A0 -- overwrite R0 on the next
 * instruction, so the port returns void. */
#define PUFO_MANUF_HANGAR_PRODUCT_FIRST 119 /* the three products the hangar branch covers */
#define PUFO_MANUF_HANGAR_PRODUCT_LAST  121

typedef struct ManufClearEnv {
    UiPanel     *pProductPanel;     /* pManufProductPanel @ 0x15EE44, read by ConfirmAndClose only */
    const i32   *pnProductRow;      /* nManufProductRowCache @ 0x15EE3C, likewise */
    u32         *apProductA;        /* unk_159E9C, 200 u32 per base */
    i32         *apProductB;        /* aManufProductB @ 0x15B79C, 200 u32 per base */
    u16         *apProductC;        /* auManufCommitted @ 0x15D09C, 200 u16 per base */
    u16         *apManufCols;       /* auManufCol @ 0x15E19C, 200 u16 per base */
    u8          *abGridBytes;       /* 0x159DBC = abFacilityGrid + 1620, the eight base bytes the
                                     * hangar products clear. BaseGrid_Get/SubTypeByte take THIS
                                     * table, not the 1524-byte facility grid -- the original hands
                                     * abFacilityGrid to BaseGrid_GetByte0x654 as a first argument
                                     * that function never reads. */
    BaseRec *const *apBases;        /* apBases @ 0x114B2C, read for apBases[base]->bUnk4 */
    i32          nBaseCount;
    const i8    *pnSelectedBaseIdx; /* nSelectedBaseIdx @ 0x159D81 */
} ManufClearEnv;

void ManufScreen_ClearProductSlot(const ManufClearEnv *pEnv, i32 nBaseIndex, i32 nProduct);

/* ------------------------------------------------- the manufacturing refresh at the hourly tick
 *
 * Evidence: ManufScreen_RefreshBaseTabs @ 0x70ebc (500 B), called from Geoscape_TickHourly @
 * 0x5d304 with the product-block anchor 0x159E98.
 *
 * For every base and every product slot it reads auManufCol[200*base + product] (the u16 quantity
 * table at 0x15E19C, reached as anchor + 400*base + 2*product + 17156) and, when it is non-zero,
 * adds it into the u32 accumulator column auProductU32[200*base + product] @ 0x159E9C.
 *
 *   - Products 119..121 -- the three the hangar branch of ManufScreen_ClearProductSlot covers --
 *     are added with no capacity test at all (0x70F54..0x70F6C).
 *   - Every other product is tested first (0x70F70..0x70FCC): Base_CalcStorageUsed for the base,
 *     plus Base_CountItemQtyAcrossBases with the scan ON and the -1 gate, plus that product's own
 *     ObjectRecFull.bCargoSize, against Base_CalcStoresCapacity. Strictly under capacity the
 *     quantity is added (0x70FE8); at or over it, it is not. Both capacity terms are recomputed
 *     for EVERY product, which is what the original does.
 *   - At or over capacity (0x71060): the shortfall dword is raised, message 32 is posted, and the
 *     product slot is cleared through ManufScreen_ClearProductSlot. The message payload is
 *     (32, base, base, product, 0): the callee takes six arguments, so a3 is R2 and a4 is R3 --
 *     both hold the base (0x7107C and 0x71084) -- while a5 is [SP+0], where 0x7108C stores the
 *     product, and a6 is [SP+4], where 0x71078 stores 0. That is the shape all four manufacturing
 *     messages use. Then the function RETURNS: 0x710A4 branches
 *     past the commit call, so one over-budget product ends the whole refresh.
 *
 * When the loop finishes with no over-budget product the original calls ManufScreen_CommitProduction
 * on the way out (0x71044 / 0x71048), and makes the same call when the base count is zero. The port
 * runs that tail call whenever the caller supplies the commit env: the hourly tick hook does, the
 * refresh-only tests leave pCommitEnv NULL. The over-budget path at 0x710A4 branches PAST the call
 * either way -- one over-budget product ends the whole refresh without committing.
 *
 * Deviation: the original walks the quantity and accumulator tables with running pointers and
 * bounds nothing; the port indexes them as 200*nBase + product and refuses when either table is
 * missing from the environment. */
typedef struct ManufRefreshEnv {
    u16              *apManufCols;   /* auManufCol @ 0x15E19C, 200 u16 per base: the quantity read */
    u32              *apProductA;    /* unk_159E9C, 200 u32 per base: the accumulator written */
    ObjectRecFull    **apObjects;    /* Map.apObjects @ Map + 0x108 */
    i32               nObjectCount;  /* Map_nObjectCount @ 0x114A60, u16 semantics */
    u8              *abFacilityGrid; /* abFacilityGrid @ 0x159768 */
    const u8        *pbBaseCount;    /* nBaseCount @ 0x159D80, read signed */
    BaseRec         **apBases;       /* Map.apBases @ Map + 0x1174 */
    SmapSite        *aSites;         /* aSmapSites @ 0x162030 */
    i32               nSiteCount;    /* nSmapSiteCount @ 0x186410 */
    u32             *pdwShortfall;   /* byte_1870C8, written as a dword */
    UiLineList      *pLines;         /* the root message list @ 0x158890 */
    const ManufClearEnv *pClearEnv;  /* ManufScreen_ClearProductSlot's tables */
    const struct ManufCommitEnv *pCommitEnv; /* the 0x71044/0x71048 tail call's env: set it to run
                                              * the commit on the way out, leave it NULL to run the
                                              * refresh alone (the test shape) */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void *pSoundUser;
} ManufRefreshEnv;

void ManufScreen_RefreshBaseTabs(const ManufRefreshEnv *pEnv);

/* ManufScreen_CommitProduction @ 0x6f34c (1408 B) -- turn allocated manufacturing work into
 * finished units, at most one unit per product per call.
 *
 * Called from ManufScreen_RefreshBaseTabs on the way out (0x71044 and 0x71048). Guard: the signed
 * base count at abFacilityGrid + 0x618 <= 0 returns (0x6F368).
 *
 * The four per-product tables, 200 entries per base off the anchor unk_159E98:
 *
 *   auManufCol  @ 0x15E19C  u16  the engineers assigned to this product at this base
 *   apProductA  @ 0x159E9C  u32  the work units accumulated so far
 *   apProductB  @ 0x15B79C  u32  the unit count the player asked for
 *   apProductC  @ 0x15D09C  u16  the units already committed
 *
 * and ObjectRecFull.uManufWorkPerUnit (+0xBA) is the work one unit costs. _rt_sdiv takes the
 * DIVISOR in R0 and the DIVIDEND in R1 -- proved at 0x65BD0/0x65BDC, where
 * Base_UpdateWorkshopNeeded divides the manufacturing total by 50 -- so
 *
 *   nUnits = apProductA[200*base + product] / apObjects[product]->uManufWorkPerUnit
 *
 * is how many units the accumulated work has paid for. The original's own panel percent
 * (ManufScreen_Draw's read at 0x6E704, BuildPanels' at 0x6F060) is
 * `accumulated * 100 / (workPerUnit * target)`, which is the same reading.
 *
 * Per base, per product, in this order:
 *
 *   1. 0x6F3D0: a zero allocation is skipped outright.
 *   2. 0x6F408..0x6F494 funds gate: blocked when (double)nFunds - (double)anObjectPriceInit[product]
 *      * 0.7 < 0.0, the 0.7 being the double 0x3FE6666666666666 assembled at 0x6F440/0x6F448.
 *      Blocked: shortfall dword, message 35.
 *   3. 0x6F4D0..0x6F564 stores gate, SKIPPED for products 119..121: Base_CalcStorageUsed plus
 *      Base_CountItemQtyAcrossBases with the SITE COUNT as the second argument (not the -1 gate
 *      RefreshBaseTabs uses) plus this product's own bCargoSize, blocked when that sum is strictly
 *      ABOVE Base_CalcStoresCapacity (0x6F530 BLE keeps going when equal). Blocked: shortfall,
 *      message 32.
 *   4. 0x6F574..0x6F5C8 component gate: each abManufComponents byte against the base's stock of
 *      apObjects[105] Elerium-115, [106] Alien alloys, [107] UFO power source and [109] UFO
 *      navigation, blocked when stock < requirement. All four are always evaluated -- MOVCC, no
 *      early exit.
 *   5. 0x6F870 blocked tail: clear the product slot, post message 33 ONLY when the shortfall dword
 *      is still zero (so 33 never follows a 35 or a 32), and RETURN from the whole pass. Like
 *      RefreshBaseTabs, one blocked product ends every remaining base and product.
 *   6. 0x6F5E8: produce only while nUnits is strictly above apProductC -- one unit per call.
 *      Products 119..121 take the craft branch: BaseGrid_SubTypeByte with amount 1 on the type byte
 *      of the SELECTED base (nSelectedBaseIdx at abFacilityGrid + 0x619, and apBases[that]->bUnk4
 *      is the type), then Ship_SpawnAtSlot with Ship_ProductSlotToType(product) as the type, homed
 *      at the base being processed. Every other product takes BaseRec_AddItemQty(apBases[base],
 *      apObjects[product], 1). Then apProductC is bumped by one with u16 truncation (0x6F6B8), the
 *      four components are subtracted from those four stocks, and nFunds is converted with
 *      _dtoi((double)nFunds - (double)price * 0.7) (0x6F70C..0x6F788). The conversion applies to
 *      the remaining balance, not the fee alone.
 *   7. 0x6F798 completion, reached whether or not this tick produced: when nUnits == apProductB the
 *      slot is cleared, the shortfall dword is raised and message 34 is posted. This one does NOT
 *      return -- the loop continues.
 *
 * All four messages take the same six-argument payload: (msg, base, base, product, 0).
 *
 * Deviation: with a zero uManufWorkPerUnit the original calls the external COREDLL _rt_sdiv with a
 * zero divisor, and the IDB cannot show what that answers; the port answers zero units. In the
 * shipped OBJECTS.DAT the 114 records with a zero work cost are exactly the priced ones with no
 * components -- items you buy rather than build -- so a queued product never hits it. The port also
 * bounds the selected-base index against the base count, which the original does not. */
typedef struct ManufCommitEnv {
    u16               *apManufCols;   /* auManufCol @ 0x15E19C: the engineers per product */
    u32               *apProductA;    /* unk_159E9C: the accumulated work */
    i32               *apProductB;    /* aManufProductB @ 0x15B79C: the requested unit count */
    u16               *apProductC;    /* auManufCommitted @ 0x15D09C: the units already committed */
    const u32         *pdwPrices;     /* anObjectPriceInit @ 0xAA2C0, indexed by product */
    i32               *pnFunds;       /* nFunds @ 0x1598D8 */
    ObjectRecFull    **apObjects;     /* Map.apObjects @ Map + 0x108 */
    i32                nObjectCount;  /* Map_nObjectCount @ 0x114A60, u16 semantics */
    u8               *abFacilityGrid; /* abFacilityGrid @ 0x159768 */
    const u8          *pbBaseCount;   /* nBaseCount @ 0x159D80, read signed */
    BaseRec          **apBases;       /* Map.apBases @ Map + 0x1174 */
    SmapSite          *aSites;        /* aSmapSites @ 0x162030 */
    i32                nSiteCount;    /* nSmapSiteCount @ 0x186410, passed as-is and NOT as -1 */
    u32              *pdwShortfall;   /* byte_1870C8, written and read back as a dword */
    UiLineList       *pLines;         /* the root message list @ 0x158890 */
    const ManufClearEnv *pClearEnv;   /* ClearProductSlot's tables, plus the selected base index */
    ShipSpawnEnv     *pShipEnv;       /* Ship_SpawnAtSlot's Map block */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void *pSoundUser;
} ManufCommitEnv;

void ManufScreen_CommitProduction(const ManufCommitEnv *pEnv);

/* ManufScreen_ConfirmAndClose @ 0x6e98c (92 B): clear the selected product's slot for the selected
 * base and tail-call the screen-close dispatcher. The product index comes from the panel's aux
 * column at the cached row, exactly as the quantity buttons read it. Its first argument to
 * ClearProductSlot is the 0x159E98 anchor, which that function never reads. The close runs as
 * the same Geoscape_CloseUI hook ResearchScreen_CancelSelectedAndClose uses (the dispatcher is
 * ported at this file:3120 and the shell hands it through the close env). */
void ManufScreen_ConfirmAndClose(const ManufClearEnv *pEnv, const GeoscapeCloseEnv *pClose);

/* ManufScreen_Init @ 0x710b0 (488 B): the tail call of ManufScreen_Open @ 0x614cc.
 *
 * Order, straight from the ARM:
 *   1. 0x710BC..0x7110C: operator new(0x14) + PadImage_ctor; the ctor's result goes into slot 0 of
 *      the screen block; PadImage_Load("manuf", 7) at 0x710E4; then the two origins zeroed. Those
 *      two stores are one dword each at PadImage +0 and +4 (0x710F8 STR R8,[R3] and 0x7110C
 *      STR R8,[R3,#4]); Hex-Rays renders them as ->x and ->uWidth because it types the slot as
 *      UiControl*, which is the wrong struct, not a Hex-Rays bug.
 *   2. 0x710F0..0x71130: the close control (g_pUiCloseCtl) at x 224, y 1, handler CloseThunk.
 *   3. 0x71134..0x7116C: six operator new(0x20) + UiControl_ctor into the six slots the ARM builds
 *      as ppScreen + (0x4F00 | 0x84) + 4*i, i.e. byte offsets 0x4F84..0x4F98 -- array indices
 *      5089..5094, which is what Hex-Rays shows.
 *   4. 0x71184..0x711EC: their handlers in slot order -- PanelPrev, PanelNext, IncEngineers (0x6dddc),
 *      DecEngineers (0x6e09c), IncUnits (0x6e13c), DecUnits (0x6e330). g_aManufCtlDesc gives each slot
 *      its position -- 0 (205,18) "up", 1 (205,38) "down", 2 (10,251), 3 (10,271), 4 (140,251),
 *      5 (140,271) -- so the engineers pair sits under the x 10 column ("Инженеров занято") and the
 *      units pair under the x 140 column ("Единиц произвести").
 *   5. 0x71200..0x71238: two controls that already exist as globals, pGeoscapeCtlA at x 40 y 294 with
 *      handler ManufScreen_Draw, and pGeoscapeCtlB at x 130 y 294 with handler
 *      ManufScreen_ConfirmAndClose. Neither is allocated here.
 *   6. 0x7123C..0x71260: UiControl_Setup(slot[5089+i], 0, 0, &g_aManufCtlDesc[i]). Both bases are zero,
 *      so every control lands exactly on its descriptor offset.
 *   7. 0x71268: tail call ManufScreen_BuildPanels @ 0x6e9e8.
 *
 * The six handlers are installed before the Setup pass. Ui_InitTransfer does it the other way
 * round; nothing reads a control in between, so the difference is not observable, and the port
 * keeps the original's order.
 *
 * g_aManufCtlDesc @ 0xAB9F8 is six 32-byte descriptors. Read against the handler order of step 4 they
 * are the base-tab scroll pair at x 205 y 18/38, the engineers pair at x 10 y 251/271, and the units
 * pair at x 140 y 251/271 (the two pairs were named the other way round until Part 115). All six use
 * the "up"/"sup" or "down"/"sdown" pad pair and mode 0, so every one of them is a press/release button
 * in UiControl_HitTest's mode-0 branch.
 *
 * Deviation: the original allocates the six controls with operator new and stores them into the
 * screen block; the port takes caller-owned controls and installs them into ppRoot[1..6], as the
 * BuySell and Transfer inits do. The original also dereferences every allocation with no test. */
#define PUFO_MANUF_PAD_NAME "manuf" /* aManuf @ 0xABABC */
#define PUFO_MANUF_PAD_MODE 7       /* 0x710DC: MOV R2,#7 */

typedef struct ManufInitEnv {
    void **ppRoot;                  /* the screen block; [0] the PadImage, [1..6] the six controls */
    UiControl *apCtl[6];            /* caller-owned, stored into ppRoot[1..6] */
    UiControl *pCloseCtl;           /* g_pUiCloseCtl */
    UiControl *pDrawCtl;            /* pGeoscapeCtlA */
    UiControl *pConfirmCtl;         /* pGeoscapeCtlB */
    void (*pfnCloseClick)(void);    /* ManufScreen_CloseThunk @ 0x6e3d4 */
    void (*pfnDrawClick)(void);     /* ManufScreen_Draw @ 0x6e3e0 */
    void (*pfnConfirmClick)(void);  /* ManufScreen_ConfirmAndClose @ 0x6e98c */
    void (*pfnPanelPrev)(void);     /* ManufScreen_PanelPrev @ 0x6dd4c */
    void (*pfnPanelNext)(void);     /* ManufScreen_PanelNext @ 0x6dd94 */
    void (*pfnIncQty)(void);        /* ManufScreen_IncEngineers @ 0x6dddc */
    void (*pfnDecQty)(void);        /* ManufScreen_DecEngineers @ 0x6e09c */
    void (*pfnIncUnits)(void);  /* ManufScreen_IncUnits @ 0x6e13c */
    void (*pfnDecUnits)(void);  /* ManufScreen_DecUnits @ 0x6e330 */
    void (*pfnBuildPanels)(void);   /* ManufScreen_BuildPanels @ 0x6e9e8 */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
} ManufInitEnv;

void ManufScreen_Init(ManufInitEnv *pEnv);

/* ManufScreen_BuildPanels @ 0x6e9e8 (2404 B): the manufacturing screen's panel build and teardown,
 * tail-called by ManufScreen_Init @ 0x710b0 (0x71268).
 *
 * Evidence: ManufScreen_BuildPanels @ 0x6e9e8, ManufScreen_Init @ 0x710b0, and the four product
 * columns documented on ManufScreen_CommitProduction below.
 *
 * One flag decides the branch: unk_15EE4C, the manufacturing sibling of bBuySellPanelsBuilt @
 * 0x161A28 and unk_1870EC.
 *
 * Teardown, unk_15EE4C != 0 (0x6F264..0x6F340): the flag is cleared, the pad at unk_159E98[0] is
 * PadImage_FreeData'd and deleted, then the four panels in the order A, C, B, D (0x6F28C, 0x6F2AC,
 * 0x6F2CC, 0x6F2EC -- C before B), then the six click controls at unk_159E98 byte offsets
 * 0x4F84..0x4F98 are UiControl_FreePads'd and deleted. No funds rollback: this screen spends none
 * until CommitProduction runs. Nothing is stored back into any of the ten globals, so the original
 * leaves ten dangling pointers behind.
 *
 * Build, unk_15EE4C == 0 (0x6EA00..0x6F344):
 *
 *   1. 0x6EA0C..0x6EA78: count the rows. Walk research topics 1..nTopicCount-1 (the walk starts at
 *      aResearchTopics + 34, one record in -- the same off-by-one-record start ResearchScreen_
 *      RefreshPanels @ 0x73008 has), and for each topic whose bUnlocked (+0xD) is 1, test the five
 *      object slot ids at +0x18..+0x1C against abManufRowFlag[id] == 1. Each hit is one row. The count
 *      byte is 0x160C4C, read as an unsigned byte and the loop runs only while it is above 1.
 *   2. 0x6EA7C..0x6EE5C: four panels, all operator new(0x1BB4), all UiPanel_ctor(nLineCount) with
 *      that count, all nStyle 13 and nFont 16, all with style bytes 0..2 = 0xB4 and 6..11 =
 *      0xF0,0xF0,0xF0,0x14,0x14,0x14:
 *
 *          A  pManufProductPanel @ 0x15EE44   x 10  y 70  nRectX5    viewW 230  click
 *             ManufScreen_SelectListed @ 0x6e914, bytes 3..5 = 0x32,0x32,0xFA, bSelLineHighlight 1,
 *             bColorKey 0x96
 *          B  pManufPanelB      @ 0x15EE48    x 150 y 70  nRectX155  viewW 30   nUnk7076 2
 *          C  pManufPanelC      @ 0x15EE40    x 187 y 70  nRectX185  viewW 30   nUnk7076 2
 *          D  pManufPanelD      @ 0x15EE38    x 233 y 70  nRectX5    viewW 230  nUnk7076 2, click
 *             ManufScreen_SyncSelection @ 0x6dd24, bSelLineHighlight 1, bColorKey 0x96
 *
 *      Only A gets SetBytes3to5; B and C get neither it nor the two flag bytes. All four share
 *      nPosY 70 and nRectY 70.
 *   3. 0x6EE60..0x6F25C: the fill pass, same walk, same filter, one line per hit in panels A, B, C
 *      and D at the same row index:
 *        - A: the item's own name, copied out of apObjects[id] (the record's szName at +0, reached as
 *          the Map-block pointer array at 0x113AC0), cut to 17 characters when strlen is above 0x11
 *          (0x6EF18 STRBHI at var_64+17). A's aux is the slot id (0x6EF64).
 *        - C's aux is the TOPIC index, not the slot id (0x6EF70 stores R8, the outer loop counter).
 *        - B: auManufCol[200*nSelectedBaseIdx + id] as decimal, or the empty string at 0x10DA38 when
 *          it is zero (0x6EF9C LDRH at anchor + 400*base + 2*id + 17156).
 *        - C's line and D's line both depend on apProductB[200*base + id] (0x6F038 LDR at anchor +
 *          4*(200*base+id) + 4). When it is zero both get the empty string (0x6F1C4). Otherwise:
 *            C = 100 * apProductA / (uManufWorkPerUnit * apProductB) with "%" appended -- the ARM
 *                composes _rt_sdiv with the DIVISOR R0 = workPerUnit * apProductB (0x6F064..0x6F068)
 *                and the DIVIDEND R1 = apProductA * 100 (0x6F070), which is the same reading of the
 *                four columns that ManufScreen_CommitProduction established.
 *            D = itoa(apProductB) + "/" + itoa(apProductA / uManufWorkPerUnit) -- the requested count,
 *                then the units the accumulated work has paid for (0x6F174 LDRH #0xBA, _rt_sdiv at
 *                0x6F178 with the divisor in R0).
 *      There is no sort pass here, unlike the buy/sell and transfer builds: the rows stay in topic
 *      order.
 *
 * abManufRowFlag is a 172-byte 0/1 table in the image (0xAA548..0xAA5F3, closed by the dword table that
 * starts at 0xAA5F4) and ManufScreen_BuildPanels is its only reader anywhere in the binary. It is
 * ported as `g_abManufRowFlag` and audit_tables.py compares it byte for byte. 35 slots
 * are set, and against the shipped OBJECTS.DAT that is exactly the set of slots with a non-zero
 * uManufWorkPerUnit -- all 149 slots checked, no disagreement. So abManufRowFlag is the "this slot can be
 * manufactured" flag, and the row list is: items unlocked by a research topic, restricted to items the
 * game can actually build.
 *
 * Deviations: the original allocates the four panels with operator new and dereferences every
 * allocation, every panel pointer and every apObjects slot with no test; the port allocates the four,
 * tests the object slots, and bounds the row index against the 500-line panel capacity. The original
 * leaves the ten freed globals dangling; the port clears them. Both divisions here can be by zero --
 * the percent divides by `uManufWorkPerUnit * asked-for` and the unit count by `uManufWorkPerUnit` --
 * and the original calls _rt_sdiv either way; the port answers zero, the same policy
 * CommitProduction records. It is unreachable in the shipped data, because every slot abManufRowFlag
 * flags has a non-zero work cost and a row only exists for a flagged slot. The teardown frees the six click
 * controls in the original and only pads them in the port, because the port's controls are
 * caller-owned. */
#define PUFO_MANUF_ROW_NAME_CUT 17 /* 0x6EF18: the name is cut at 17 characters when strlen > 0x11 */

typedef struct ManufBuildEnv {
    u8   *pbPanelsBuilt;              /* unk_15EE4C */
    void **ppRoot;                    /* unk_159E98; [0] the pad, [5089..5094] the six controls */
    UiControl *apCtl[6];              /* the six controls, pads freed by the teardown */
    UiPanel **ppPanelA;               /* pManufProductPanel @ 0x15EE44 */
    UiPanel **ppPanelB;               /* pManufPanelB @ 0x15EE48 */
    UiPanel **ppPanelC;               /* pManufPanelC @ 0x15EE40 */
    UiPanel **ppPanelD;               /* pManufPanelD @ 0x15EE38 */
    const ResearchTopicLive *aTopics; /* aResearchTopics @ 0x15EF08 */
    const u8 *pbTopicCount;           /* nResearchEntryCount @ 0x160C4C */
    ObjectRecFull *const *apObjects;  /* apObjects @ 0x113AC0 */
    i32     nObjectCount;             /* Map_nObjectCount @ 0x114A60 */
    u32   *apProductA;                /* unk_159E9C, 200 u32 per base: accumulated work */
    i32   *apProductB;                /* aManufProductB @ 0x15B79C, 200 dwords per base. The IDB types
                                     * that block int[1600] and every reader reaches it with LDR/STR
                                     * (0x6E228, 0x6E2EC, 0x6E30C, 0x6F7A4, 0x6F8FC), so all seven
                                     * apProductB fields now spell it the way the IDB does. */
    u16   *apManufCols;               /* auManufCol @ 0x15E19C, 200 u16 per base: engineers assigned */
    i8     nSelectedBaseIdx;          /* nSelectedBaseIdx @ 0x159D81 */
    void (*pfnSelectListed)(void);    /* ManufScreen_SelectListed @ 0x6e914 */
    void (*pfnSyncSelection)(void);   /* ManufScreen_SyncSelection @ 0x6dd24 */
} ManufBuildEnv;

void ManufScreen_BuildPanels(ManufBuildEnv *pEnv);

/* ------------------------------------------------- the manufacturing screen's "draw" click
 *
 * Evidence: ManufScreen_Draw @ 0x6e3e0 (1332 B). Installed as the click handler of the shared draw
 * control pGeoscapeCtlA by ManufScreen_Init (LDR R3,=ManufScreen_Draw / STR R3,[R0,#0xC] at 0x71228), and
 * called again from Geoscape_CloseUI case 8 at 0x8BD78 whenever bManufProductSelected is set. Both call
 * sites discard the return value -- the original returns Base_UpdateLabNeeded's loop-counter residue of
 * 6 -- so the port returns void.
 *
 * Where BuildPanels refreshes the whole list, this refreshes ONE row: the row the product panel's
 * cached cursor points at.
 *
 * 1. 0x6E40C..0x6E430: the selected product is pManufProductPanel->anLineAux[nManufProductRowCache],
 *    cached in a local; bManufProductSelected is cleared; panel A's bSelLineHighlight is set; sample 17 is
 *    played. 0x6E428 also loads 4 into R1, but Sound_Play @ 0x9e0c0 takes ONE argument (its body reads
 *    only R0) and never touches R1, so that 4 is register residue, not a second argument.
 * 2. 0x6E478..0x6E48C: when apProductB[200*base + product] is zero the slot is cleared through
 *    ManufScreen_ClearProductSlot. The product is masked to a byte for the call (AND R2,R3,#0xFF at
 *    0x6E484) while the index itself uses the unmasked value.
 * 3. 0x6E4E4..0x6E51C: the seed. apProductA[idx] == 0 AND apProductB[idx] != 0 AND auManufCol[idx] != 0
 *    together store 1 into apProductA[idx] -- a product that has a quantity and allocated engineers but
 *    no accumulated work yet starts with one unit of it. Step 6 reloads apProductA from memory at
 *    0x6E6D4, so a seeded row goes down the percent path with that one unit in it rather than the
 *    empty-string one.
 * 4. 0x6E52C..0x6E5A8: ORIGINAL BUG, reproduced -- the hangar branch, DEAD in the shipped build. The test is on the dword
 *    at panel A offset 0 -- `> 118 && < 122`, plainly meant as "the selected product is one of the three
 *    hangar products 119..121" -- but nothing in the binary writes a product id there. BuildPanels leaves
 *    panel A's style bytes at B4 B4 B4 32 32 FA, so the dword reads as 0x32B4B4B4; UiPanel_ClickRow and
 *    UiScrollList_TrackHold write the row into the dword at offset 12, not offset 0. Recorded in
 *    docs/parity/bugs.tsv. What it would do if it ran: take the selected base's type byte
 *    (apBases[base]->bUnk4, 0x6E5A4), read that base's byte in the eight-byte table at
 *    abFacilityGrid + 1620 = 0x159DBC through BaseGrid_GetByte0x654 (0x6E558), and write back that value
 *    plus the LOW BYTE of apProductB[idx] (LDRB [R3,#0x1904] at 0x6E58C, ADD at 0x6E590) through
 *    BaseGrid_SetByteAtFound (0x6E5A8).
 * 5. 0x6E5EC..0x6E6A0: panel B's line for this row = auManufCol[idx] as decimal, or the empty string at
 *    0x10DA38 when it is zero -- the same column BuildPanels fills, one row instead of the list.
 * 6. 0x6E6D4..0x6E8E0: when apProductA[idx] != 0, panel C gets 100 * apProductA / (uManufWorkPerUnit *
 *    apProductB) with "%" appended -- divisor in R0 at 0x6E708, dividend in R1 at 0x6E714, _rt_sdiv's
 *    order again -- and panel D gets itoa(apProductB) then "/" then itoa(apProductA / uManufWorkPerUnit)
 *    (0x6E7A0, 0x6E7E8, 0x6E83C..0x6E848). Otherwise both get the empty string. The guard here is
 *    apProductA != 0 where BuildPanels guards on apProductB != 0; step 2 has just cleared the slot when
 *    the quantity is zero, so the empty-string path is reached through apProductA being zero.
 * 7. 0x6E8F4 and 0x6E908: Base_UpdateWorkshopNeeded then Base_UpdateLabNeeded on the selected base, the
 *    same pair ResearchScreen_ConfirmAndCache ends with. The originals call Base_CountActiveManuf and
 *    Base_CountActiveResearch inside themselves; the port passes the counts in, taken from
 *    auManufCol[200*base..] and auResearchCol[144*base..] with the shared entry count byte at 0x160C4C.
 *
 * Deviations: the original bounds nothing -- an out-of-range row, base index or product indexes the
 * 1600-entry columns and apBases blindly. The port reads the aux cell only for a row inside the panel,
 * skips steps 2..6 when the composed index is outside 0..1599, and dereferences apBases only for a base
 * inside the array; the two tail passes still run either way. Both divisions can be by zero in the
 * original (the percent by uManufWorkPerUnit * asked-for, the unit count by uManufWorkPerUnit) and the
 * port answers zero, the same policy BuildPanels and CommitProduction record. */
typedef struct ManufDrawEnv {
    UiPanel             *pProductPanel;   /* pManufProductPanel @ 0x15EE44: names, aux = product id */
    UiPanel             *pPanelB;         /* pManufPanelB @ 0x15EE48: the allocated-work column */
    UiPanel             *pPanelC;         /* pManufPanelC @ 0x15EE40: the percent column */
    UiPanel             *pPanelD;         /* pManufPanelD @ 0x15EE38: the asked/produced column */
    const i32           *pnProductRow;    /* nManufProductRowCache @ 0x15EE3C */
    u8                  *pbProductSelected; /* bManufProductSelected @ 0x15EE34 */
    u32                 *apProductA;      /* unk_159E9C: accumulated work */
    i32                 *apProductB;      /* aManufProductB @ 0x15B79C: units asked for */
    u16                 *apManufCols;     /* auManufCol @ 0x15E19C: engineers assigned */
    const u16           *apResearchCols;  /* auResearchCol @ 0x16022C, for the lab pass's active count */
    const u8            *pbEntryCount;    /* nResearchEntryCount @ 0x160C4C, shared by both counts */
    ObjectRecFull *const *apObjects;      /* apObjects @ 0x113AC0, for uManufWorkPerUnit */
    i32                  nObjectCount;    /* Map_nObjectCount @ 0x114A60, u16 semantics */
    BaseRec *const      *apBases;         /* apBases @ 0x114B2C = Map + 0x1174, for bUnk4 */
    i32                  nBaseCount;
    u8                  *abGridBytes;     /* 0x159DBC = abFacilityGrid + 1620, the eight base bytes the
                                           * hangar products live in */
    u8                  *abFacilityGrid;  /* abFacilityGrid @ 0x159768, for the two tail passes */
    const i8            *pnSelectedBaseIdx; /* nSelectedBaseIdx @ 0x159D81 */
    const ManufClearEnv *pClearEnv;       /* ManufScreen_ClearProductSlot's tables */
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void *pSampleUser;
} ManufDrawEnv;

void ManufScreen_Draw(ManufDrawEnv *pEnv);

/* ------------------------------------------------- the manufacturing screen's frame
 *
 * Evidence: ManufScreen_DrawFrame @ 0x6f9a4 (5400 B), called from Geoscape_Draw @ 0x941A0 with the
 * result discarded, so the port returns void.
 *
 * Every label is a TEXTS.DAT record: the ARM builds each address as `LDR R0, =(<addr> - 0xB38D0)`
 * plus `R8 = 0xB38D0`, so each one is aTextRecs + 205*n and the record number is exact. The shipped
 * Russian text behind each index was read out of lang.dat, and it says what the label is for.
 *
 * Frame (0x6F9C4..0x6FB54). 24 steps of 7 down each side rail at x 5 and x 234 (y = 7k + 1 and
 * 7k + 151, index 5*(k+20) climbing, 0xE6 - 5k falling), then 24 steps of 5 along the top and bottom
 * rails at y 5 and y 315 (x = 5k + 1 and 5k + 119, same climbing index, 0xFA - 5k falling). The four
 * constants are the ones the transfer frame uses.
 *
 * Panels built (unk_15EE4C != 0, 0x6FB7C..0x6FD54): the four panels draw through UiPanelMsg_Draw in
 * the order A, B, C, D (0x6FB84, 0x6FB90, 0x6FB9C, 0x6FBA8); the column header TEXTS[1173]
 * "Производство          Инж. Вып. Зак./Гот." at (10, 52) -- the three numeric columns this screen
 * fills are the ones that header names; the selected base's name from the eight 64-byte site records
 * at 0x186C40 (name at +26, 0x6FC14) centred at (120 - w/2, 13); then, only with no product selected
 * and bBaseFacPreviewOk set and the map cursor y strictly inside 70..70+band, the object under the
 * cursor named at (10, 300) -- the row comes from PANEL D's nUnk3032 (0x6FCD8), not from
 * nManufProductRowCache, and the object is apObjects[pManufProductPanel->anLineAux[that row]].
 * The band is 16 * min(nLineCount, nStyle) of panel A (0x6FC78..0x6FC98), and the three column
 * separators at x 130, 152 and 187 are 1 wide and band + 2 tall in index 0xB4 (0x6FD40..).
 *
 * No product selected (0x6FDB4..0x6FDC0): the reset control g_pUiCloseCtl blits and the frame ends in the
 * teal pair.
 *
 * Product selected (0x6FDC4..): the detail panel opens through Ui_AnimExpandBlit(0, 30, 236, 284,
 * index 4, src 0x28/0x2D, key 0xC8, mode 3) at 0x6FE00; when it returns 0 the frame stops at the red
 * pair and then the teal one, with none of the detail drawn. Otherwise: the product's own name centred
 * at (120 - w/2, 45); `uManufWorkPerUnit` + TEXTS[1349] " инженер-часов для производства" at (10, 69);
 * TEXTS[1174] "одной единицы" at (10, 83); and the price line at (10, 97) --
 * `dtoi((double)anObjectPriceInit[product] * 0.7)` then "$". The 0.7 double is the same raw constant
 * used by ManufScreen_CommitProduction at 0x6F440/0x6F448. The display converts the fee itself;
 * commit converts the remaining funds after subtraction.
 *
 * Components (0x7002C..0x7073C). Four cases, one row each, and a case with a zero component draws
 * nothing and does not advance the row counter. The headers TEXTS[1176] "Необх." and TEXTS[1177]
 * "Дост." are centred at (140 - w/2, 114) and (200 - w/2, 114) inside the loop, so they are redrawn
 * per row. Each row is at y = 130 + 14*(rows drawn so far): the item name at x 10, the required amount
 * (ObjectRecFull.abManufComponents[k]) at x 140, and the base's stock of that component at x 200. When
 * the requirement is strictly above the stock the colour pair becomes (20*(nMapPhase+5), 10, 10 /
 * 50, 0, 0) and bManufQtyGate is set to 1 (0x70198 and its three twins) -- the only live writer this
 * gate has outside SelectListed's clear. The four pairings are component[0] against apObjects[105]
 * Elerium-115 with TEXTS[439], component[1] against [106] Alien alloys with TEXTS[505], component[2]
 * against [107] UFO power source with TEXTS[497], component[3] against [109] UFO navigation with
 * TEXTS[498]; slot 108 (Alien surgery) is never a component.
 *
 * Crew and capacity (0x70740..0x709F4): TEXTS[1178] "Инженеров доступно: " + Base_GetEngineerCount at
 * (10, 185); TEXTS[1179] "Свободных мест в мастерских: " + (Base_CalcWorkshopCap -
 * Base_CountActiveManuf) at (10, 199); then the two column captions TEXTS[1180] "Инженеров" at (10,
 * 216) and TEXTS[1182] "Единиц" at (140, 216), and under them TEXTS[1181] "занято: " +
 * auManufCol[200*base + product] at (10, 230) and TEXTS[1183] "произвести: " +
 * aManufProductB[200*base + product] at (140, 230).
 *
 * Buttons (0x709F8..0x70C48): TEXTS[1142] "Добав." drawn twice, at (38, 251) and (170, 251), and
 * TEXTS[1143] "Убрать" twice at (38, 271) and (170, 271) -- one pair per arrow-button column. The last
 * four of the six arrow controls blit (0x70AF8..0x70B14 walks four dword slots starting at
 * unk_159E98[5091]). unk_186B94 is MOVED to (94, 251) and its pfnClick is set to
 * ManufScreen_UpdateProgress; unk_186B90 is moved to (94, 271) and its pfnClick set to
 * ManufScreen_ReduceSelectedQtyToOne -- this function re-points both every frame. TEXTS[1604] "ВСЕ" is
 * centred at (114 - w/2, 252) and TEXTS[1711] at (113 - w/2, 272); 1711 is past the 1650 records
 * TEXTS.DAT loads, so that second label draws an empty string in the shipped build. The draw control
 * pGeoscapeCtlA blits, then TEXTS[1127] "ОК" is centred at (73 - w/2, 295) and TEXTS[1144] "Отмена" at
 * (164 - w/2, 295) under the confirm control pGeoscapeCtlB.
 *
 * Shortfall (0x70DB8..0x70EB4). Only when byte_1870C8 read as a dword is exactly 1: with
 * dword_1870B4[0] == 0 the 200x70 panel opens through Ui_AnimExpandBlit_IfEnabled and TEXTS[1184]
 * "Нет места в хранилищах" is centred at (120 - w/2, 158); with 1 the same panel and TEXTS[1185]
 * "Нет свободных ангаров"; with anything else nothing is drawn. The original returns the value it
 * tested here or at 0x70DB8; the port returns void because Geoscape_Draw discards it.
 *
 * Deviations: the original builds every label into a 104-byte stack buffer with unbounded copy loops
 * and strcats, dereferences all four panels and the object record with no test, and reads the cursor
 * row out of panel D's cursor without bounds. The port bounds each label build at the same 104 bytes,
 * null-guards the panels and the controls, and resolves the selected product through the same bounded
 * aux read the other manufacturing functions use. */
#define PUFO_MANUF_SEP_INDEX      0xB4 /* the three column separators */
#define PUFO_MANUF_LABEL_MAX      104 /* psz @ sp+0x20 in the original */
#define PUFO_MANUF_TEXT_HEADER    1173 /* "Производство          Инж. Вып. Зак./Гот." at (10, 52) */
#define PUFO_MANUF_TEXT_ONE_UNIT  1174 /* "одной единицы" at (10, 83) */
#define PUFO_MANUF_TEXT_PRICE     1175 /* "Цена 1 единицы: " at (10, 97) */
#define PUFO_MANUF_TEXT_REQUIRED  1176 /* "Необх." at (140 - w/2, 114) */
#define PUFO_MANUF_TEXT_AVAILABLE 1177 /* "Дост." at (200 - w/2, 114) */
#define PUFO_MANUF_TEXT_ENG_FREE  1178 /* "Инженеров доступно: " at (10, 185) */
#define PUFO_MANUF_TEXT_WS_FREE   1179 /* "Свободных мест в мастерских: " at (10, 199) */
#define PUFO_MANUF_TEXT_ENG_CAP   1180 /* "Инженеров" at (10, 216) */
#define PUFO_MANUF_TEXT_ENG_USED  1181 /* "занято: " plus auManufCol at (10, 230) */
#define PUFO_MANUF_TEXT_UNIT_CAP  1182 /* "Единиц" at (140, 216) */
#define PUFO_MANUF_TEXT_UNIT_ASK  1183 /* "произвести: " plus aManufProductB at (140, 230) */
#define PUFO_MANUF_TEXT_NO_STORES 1184 /* "Нет места в хранилищах" */
#define PUFO_MANUF_TEXT_NO_HANGAR 1185 /* "Нет свободных ангаров" */
#define PUFO_MANUF_TEXT_OK        1127 /* "ОК" under the draw control, at (73 - w/2, 295) */
#define PUFO_MANUF_TEXT_CANCEL    1144 /* "Отмена" under the confirm control, at (164 - w/2, 295) */
#define PUFO_MANUF_TEXT_ADD       1142 /* "Добав." at (38, 251) and (170, 251) */
#define PUFO_MANUF_TEXT_REMOVE    1143 /* "Убрать" at (38, 271) and (170, 271) */
#define PUFO_MANUF_TEXT_ALL       1604 /* "ВСЕ" at (114 - w/2, 252) */
#define PUFO_MANUF_TEXT_CLAMP_ONE 1711 /* past the 1650 loaded records: the label draws empty */
#define PUFO_MANUF_TEXT_WORK_HOURS 1349 /* " инженер-часов для производства" */
#define PUFO_MANUF_TEXT_COMP_ELERIUM 439 /* "Элерий-115" */
#define PUFO_MANUF_TEXT_COMP_ALLOY   505 /* "Сплав пришельцев" */
#define PUFO_MANUF_TEXT_COMP_POWER   497 /* "Энергоблоки НЛО" */
#define PUFO_MANUF_TEXT_COMP_NAV     498 /* "Навигация НЛО" */
#define PUFO_MANUF_PRICE_FACTOR 0.7    /* 0x3FE66666_66666666 at display 0x6FFBC/0x6FFC4 and commit 0x6F440/0x6F448 */

typedef struct ManufFrameEnv {
    const GfxColorEnv *pColor;          /* pGX @ 0x1139B8, the colour-pair owner */
    UiAnimBlitEnv *pAnim;               /* the geoscape anim root @ 0x1864F8 */
    UiPanel *pPanelA;                   /* pManufProductPanel @ 0x15EE44 */
    UiPanel *pPanelB;                   /* pManufPanelB @ 0x15EE48 */
    UiPanel *pPanelC;                   /* pManufPanelC @ 0x15EE40 */
    UiPanel *pPanelD;                   /* pManufPanelD @ 0x15EE38 */
    u8   *pbPanelsBuilt;                /* unk_15EE4C */
    u8   *pbProductSelected;            /* bManufProductSelected @ 0x15EE34 */
    u8   *pbQtyGate;                    /* bManufQtyGate @ 0x15EE50, set when a component is short */
    const i32 *pnProductRow;            /* nManufProductRowCache @ 0x15EE3C */
    const u8  *pbPreviewOk;             /* bBaseFacPreviewOk @ 0x18652D */
    const i32 *pnCursorY;               /* nSmapCursorY @ 0x186BD4, read signed */
    const i8  *pnSelectedBaseIdx;       /* nSelectedBaseIdx @ 0x159D81 */
    const u8  *pbMapPhase;              /* nMapPhase @ 0x1139EF, the short-component red ramp */
    ObjectRecFull *const *apObjects;    /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                   /* Map_nObjectCount @ 0x114A60, u16 semantics */
    const char *pTextRecs;              /* aTextRecs @ 0xB38D0, stride 205 */
    BaseSiteRec *apSiteRecs;            /* 0x186C40, eight 64-byte records, name at +26 */
    i32   *apProductB;                  /* aManufProductB @ 0x15B79C */
    u16   *apManufCols;                 /* auManufCol @ 0x15E19C */
    const u8 *pbEntryCount;             /* nResearchEntryCount @ 0x160C4C, both crew/capacity counts */
    const u32 *pdwPrices;               /* anObjectPriceInit @ 0xAA2C0 */
    const ObjectRecFull *pEngineerRec;  /* apObjects[112] @ 0x113C80 */
    const u8 *abFacilityGrid;           /* abFacilityGrid @ 0x159768, for Base_CalcWorkshopCap */
    UiControl *apCtl[6];                /* the six arrow controls; the last four blit at 0x70AF8 */
    UiControl *pCtlReset;               /* g_pUiCloseCtl, blitted when no product is selected */
    UiControl *pCtlAllocAll;            /* unk_186B94, moved to (94, 251) and re-pointed every frame */
    UiControl *pCtlClampOne;            /* unk_186B90, moved to (94, 271) and re-pointed every frame */
    UiControl *pCtlDraw;                /* pGeoscapeCtlA, the "ОК" control */
    UiControl *pCtlConfirm;             /* pGeoscapeCtlB, the "Отмена" control */
    UiControlBlitFn pfnCtlBlit;
    UiControlBlitFn pfnCtlBlitEx;
    void *pCtlUser;
    const u32 *pdwShortfall;            /* byte_1870C8, read as a dword */
    const i32 *pnShortfallReason;       /* dword_1870B4[0] */
    void (*pfnAllocAllClick)(void);     /* ManufScreen_UpdateProgress @ 0x6df6c */
    void (*pfnClampOneClick)(void);     /* ManufScreen_ReduceSelectedQtyToOne @ 0x6dec0 */
} ManufFrameEnv;

void ManufScreen_DrawFrame(const ManufFrameEnv *pEnv);

/* ManufScreen_IncUnits @ 0x6e13c (500 B): the manufacturing screen's units "+" button -- the right of
 * the two "+/-" pairs, the one under TEXTS[1182] "Единиц" + TEXTS[1183] "произвести: ", so the number
 * it moves is aManufProductB, the units to produce. Part 115 moved the name from IncEngineers: the
 * engineer gates (available engineers, a free workshop slot) sit on 0x6dddc and 0x6e09c. Like the
 * other four buttons it opens by reading bManufQtyGate and bails returning that byte, and like them
 * the gate is dead in the shipped build. It then splits on the selected product:
 *
 *   product 119..121 (the hangar products) -- the budget is free hangar slots,
 *     Base_CalcHangarCap - Base_CountShipsAtBase. When that is zero it stores 1 into byte_1870C8
 *     (as a dword) and 1 into dword_1870B4[0], re-reads the gate, and then -- because a full
 *     hangar means alloc >= 0 == free -- leaves. Otherwise it raises aManufProductB[idx] by one
 *     while it is below the free-slot count.
 *
 *   every other product -- the budget is Base_CalcStoresCapacity, and the pressure on it is
 *     ObjectRecFull[product].bCargoSize * aManufProductB[idx]  (0x6E214: the object is found by the
 *     PRODUCT index, not the base)  + Base_CalcStorageUsed + Base_CountItemQtyAcrossBases. Below
 *     capacity the units count goes up by one; at or above it, byte_1870C8 = 1 and
 *     dword_1870B4[0] = 0 -- note the residue differs from the hangar path, 1 versus 0.
 *
 * Base_CountItemQtyAcrossBases is called with the site count at 0x186410, the selected base as the
 * site index, and scan-sites = 1.
 *
 * Return value: the gate byte, or 4*idx (hangar path), or the capacity, or &dword_1870B4. It is
 * installed as a click handler (0x711D0, 0x7127C) and nothing reads it, so the port returns void. */
typedef struct ManufUnitsEnv {
    UiPanel              *pProductPanel;   /* pManufProductPanel @ 0x15EE44 */
    const i32            *pnProductRow;    /* nManufProductRowCache @ 0x15EE3C */
    i32                  *apProductB;      /* aManufProductB @ 0x15B79C */
    const u8             *abFacilityGrid;  /* gBaseFacGrid @ 0x159768, for the two capacity calls */
    const u8             *abGridBytes;     /* 0x159DBC, for Base_CountShipsAtBase's extra term */
    const i8             *pnSelectedBaseIdx;
    const u8             *pbQtyGate;       /* bManufQtyGate @ 0x15EE50 */
    Ship *const          *apCraft;         /* apCraft @ 0x114A64 */
    i32                   nCraftCount;
    BaseRec *const       *apBases;         /* apBases @ 0x114B2C */
    i32                   nBaseCount;
    ObjectRecFull *const *apObjects;       /* apObjects @ 0x113AC0: the storage sum and the product */
    i32                   nObjectCount;
    const SmapSite       *aSites;          /* aSmapSites @ 0x162030 */
    i32                   nSiteCount;      /* 0x186410 */
    u32                  *pdwShortfall;    /* byte_1870C8, written as a dword here too */
    i32                  *pnResidue;       /* dword_1870B4[0] */
} ManufUnitsEnv;

void ManufScreen_IncUnits(const ManufUnitsEnv *pEnv);

/* ------------------------------------------------- the buy/sell confirm, and the region funding bonus
 *
 * Evidence: BuySellScreen_ConfirmSiteAndClose @ 0x75090 (196 B), Funding_AddRegionBonus @ 0x7debc
 * (196 B).
 *
 * BuySellScreen_ConfirmSiteAndClose is installed twice as a click handler by BuySellScreen_Init
 * (0x78D58, 0x78D88). It asks whether the transfer flow's working site -- the record AT the site
 * count, the same slot Smap_SumSiteScores and Transfer_ResetSite use -- holds anything at all, and
 * if it does it commits that record as a real site: bFlag0 = 24 and the site count goes up by one.
 * The three tests, in the original's order:
 *
 *   1. Base_CountItemQtyAcrossBases over that site, with scan-sites OFF (0x750B0: a2 = the site
 *      count, a3 = nSelectedBaseIdx, a4 = 0). Cargo volume, not item count.
 *   2. SmapSite.uRecoveredSoldiers (0x202) -- 0x750DC.
 *   3. auItemQty[112] + auItemQty[113] + auItemQty[117] + auItemQty[118] -- 0x750E8..0x75104, four
 *      unsigned halfword loads. 112 and 113 are the two the monthly upkeep prices (20,000 and
 *      30,000 a unit) and Base_CountOutboundTransferSpace's recovery load also use; 117 and 118 are
 *      read nowhere else in the binary.
 *
 * Either way it then copies nFunds into the buy/sell scratch dword at 0x161460 (the only fifteen
 * xrefs that global has are all inside BuySellScreen_*) and tail-calls Geoscape_CloseUI, so the
 * close runs whatever the commit did.
 *
 * Deviation: the original bounds nothing -- it reads aSmapSites[nSiteCount] even when the count is
 * 200, one record past the array. The port refuses the commit there. */
#define PUFO_SITE_FLAG_CONFIRM 24 /* the value bFlag0 takes when a working site becomes a real one.
                                   * The only other value this byte is ever written with is
                                   * Base_DemolishFacility's 0xFF not-assigned marker (0x66D58); no
                                   * reader of 24 was found in the binary. */

typedef struct BuySellConfirmEnv {
    SmapSite             *aSites;      /* aSmapSites @ 0x162030. Not const: the commit writes bFlag0
                                        * into the working record. */
    i32                  *pnSiteCount; /* 0x186410, reached through the 0x1863E8 view block */
    const i8             *pnSelectedBaseIdx;
    BaseRec *const       *apBases;
    ObjectRecFull *const *apObjects;
    i32                   nObjectCount;
    i32                   nFunds;      /* nFunds @ 0x1598D8 */
    i32                  *pnFundsSnapshot; /* unk_161460, the buy/sell scratch */
} BuySellConfirmEnv;

void BuySellScreen_ConfirmSiteAndClose(const BuySellConfirmEnv *pEnv,
                                       const GeoscapeCloseEnv *pClose);

/* Funding_AddRegionBonus @ 0x7debc (196 B): one radar contact raises one funding dword by 2. It is
 * called from RadarScreen_Update three times (0x60B1C, 0x60C50, 0x60CAC), each time with the value
 * Tilemap_GetAt returned for the contact's tile as the region id and unk_161F34 as a first argument
 * it never reads. The return value -- &0x1864F8 on the paths that changed something -- is
 * overwritten by the next instruction every time, so the port returns void.
 *
 * The six dwords are 0x186568, 0x18656C, 0x186570, 0x186574, 0x186578, 0x18657C. FundingScreen_Init
 * (0x5C0FC..0x5C144) is what gives them their meaning: it sums the first five, divides the funding
 * total at 0x1865BC by that sum plus one, multiplies the result by the sixth, adds that back into
 * the total, divides the total by the sum plus one again, and scales each of the first five by that
 * last quotient into 0x186580..0x186590. So [0..4] are the five country scores and [5] is a
 * multiplier on the monthly total -- and [5] is the one dword of the block the save never writes.
 *
 * The 0x80..0x89 id space is the WORLD REGION of a strategy-map tile, not a race. The proof is the
 * globe hover (Geoscape_DrawGlobeHover 0x67914): it prints TEXTS[905 + tile] as the tile's own
 * name, and the ten records 1033..1042 are "Северная Америка", "Южная Америка", "Европа", "Азия",
 * "Австралия", "Африка", "Антарктика", "Арктика", "Канада", "Здесь нельзя строить". 0xFF is the
 * ocean tile with no region behind it. The same byte is the new-base placement cost index
 * (aNewBaseCostByRegion @ 0xAB488, ten dwords indexed by region - 0x80), the base's own region
 * (BaseRec.bRegionId +0x11), and the funding bonus source -- the funding screen groups the ten
 * regions into its five investing countries (TEXTS[1149] "Инвестирование стран").
 *
 * The region -> country mapping is NOT monotonic -- 0x84 feeds the second slot while 0x82 and 0x83
 * feed the third and fourth -- and 0x86, 0x87 and 0x89 give no bonus at all. That is what the
 * compare chain at 0x7DEBC..0x7DF7C says, and it is reproduced as written. */
#define PUFO_FUNDING_SLOT_COUNT 6
#define PUFO_FUNDING_COUNTRY_FIRST 5  /* [0..4] are the five country scores */
#define PUFO_FUNDING_MULT_SLOT  5  /* [5] @ 0x18657C scales the monthly total */
#define PUFO_FUNDING_BONUS      2  /* every hit adds exactly two */
#define PUFO_REGION_ID_FIRST      0x80
#define PUFO_REGION_ID_COUNT      10 /* aNewBaseCostByRegion @ 0xAB488 has one entry per region */
#define PUFO_REGION_ID_NONE       0xFF /* the ocean tile, with no region behind it */

/* The ten strategy-map regions, TEXTS[1033 + (region - 0x80)]. */
typedef enum PufoRegionId {
    PUFO_REGION_NORTH_AMERICA = 0x80, /* TEXTS[1033] "Северная Америка" */
    PUFO_REGION_SOUTH_AMERICA = 0x81, /* TEXTS[1034] "Южная Америка" */
    PUFO_REGION_EUROPE        = 0x82, /* TEXTS[1035] "Европа" */
    PUFO_REGION_ASIA          = 0x83, /* TEXTS[1036] "Азия" */
    PUFO_REGION_AUSTRALIA     = 0x84, /* TEXTS[1037] "Австралия" */
    PUFO_REGION_AFRICA        = 0x85, /* TEXTS[1038] "Африка" */
    PUFO_REGION_ANTARCTICA    = 0x86, /* TEXTS[1039] "Антарктика", no funding arm */
    PUFO_REGION_ARCTIC        = 0x87, /* TEXTS[1040] "Арктика", no funding arm */
    PUFO_REGION_CANADA        = 0x88, /* TEXTS[1041] "Канада", funds with North America */
    PUFO_REGION_NO_BUILD      = 0x89  /* TEXTS[1042] "Здесь нельзя строить" */
} PufoRegionId;

void Funding_AddRegionBonus(i32 *anFundingSlots, i32 nRegionId);

/* ResearchScreen_StartSelected @ 0x73240 (232 B): the research screen's start button. It clears
 * the detail flag, plays sample 17, and then -- only if the selected topic's wProgress is zero AND
 * that topic's allocation for this base is non-zero -- sets wProgress to 1, which is what moves a
 * topic from "allocated" to "running". The two reads are 0x732C8 (LDRH [topic*34 + 0xA]) and
 * 0x732F8 (LDRH of auResearchCol[topic + 144*base]); the store at 0x732D8 is the same wProgress
 * reached through the 0x15EF0C anchor plus 6, the same address.
 *
 * It then refreshes the three panels, re-runs both needed passes (workshops and labs, which is why
 * the manufacturing column has to be handed in too), and tail-calls ResearchScreen_RebuildTopicList
 * (0x74270, ported in this file behind ResearchRebuildEnv), so the rebuild is a hook and this returns what that hook returns. */
typedef struct ResearchStartEnv {
    ResearchQtyEnv qty;                  /* panel, row cache, research column, count, scientists, grid, base */
    ResearchTopicLive *aTopics;          /* aResearchTopics @ 0x15EF08 */
    UiPanel         *pPanelQty;          /* unk_15EEFC, the second of the three research panels */
    UiPanel         *pPanelProgress;     /* unk_15EF00, the third */
    const u16       *apManufCols;        /* auManufCol @ 0x15E19C, for the workshop needed pass */
    u8              *pbTopicSelected;    /* unk_160C80, cleared here */
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void *pSampleUser;
    i32 (*pfnRebuild)(void *pUser);      /* ResearchScreen_RebuildTopicList @ 0x74270, ported in this file behind ResearchRebuildEnv */
    void *pRebuildUser;
} ResearchStartEnv;

i32 ResearchScreen_StartSelected(const ResearchStartEnv *pEnv);


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
 * and hands R4 to OptionsScreen_Open as the mode. The first argument is dropped. Both callees are
 * ported -- OptionsScreen_Init @ 0x6be74 and OptionsScreen_Open @ 0x6d0ac -- but neither reaches this
 * pair directly: OptionsScreenHooks carries them as function pointers because RebuildWithSound and
 * OpenWithSound have no env of their own to hand over. That seam is a design problem, recorded in
 * HANDOFF rather than fixed here. */
typedef struct OptionsScreenHooks {
    void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser);
    void (*pfnInit)(void *pUser);
    void (*pfnOpen)(i32 nMode, void *pUser);
    void *pUser;
} OptionsScreenHooks;

void OptionsScreen_RebuildWithSound(const OptionsScreenHooks *pHooks);
void OptionsScreen_OpenWithSound(i32 nIgnored, i32 nMode, const OptionsScreenHooks *pHooks);

/* Funding_ScoreToBarLevel @ 0x5aa60. Row 0..4 maps to 3, 5, 4, 1, 3 (two rows share 3);
 * direction -2/-1/1/2 means -50000/-25000/+25000/+50000; direction 0 or outside -2..2 returns 0
 * and discards the level. The first argument is the report ROW, not a score: FundingScreen_Init
 * passes 0..4 in the order the five rows are drawn (0x5C39C, 0x5C3E0, 0x5C420, 0x5C460, 0x5C4A0). */
i32 Funding_ScoreToBarLevel(i32 nRow, i32 nDirection);

/* ------------------------------------------------- the five funding countries
 *
 * The funding block is five countries, and the monthly report draws them in this order -- lang.dat
 * TEXTS[1302..1306] are the five row titles: "США", "Евро Альянс", "Азиатский Альянс",
 * "Австралия", "Африка". Each row feeds on one of the five country SCORE slots, and the row order is
 * NOT the slot order: the row -> slot permutation is 0, 2, 3, 1, 4. Both halves agree on it -- the
 * five Funding_DeltaToArrow calls in FundingScreen_DrawExtended (0x5B804, 0x5B82C, 0x5B854, 0x5B87C,
 * 0x5B8A4) read shares 0x186580, 0x186588, 0x18658C, 0x186584, 0x186590 in row order, and the five
 * share stores in FundingScreen_Init (0x5C34C, 0x5C350, 0x5C368, 0x5C374, 0x5C38C) write the same
 * five in the same order. So the SHARE, BAR-TOTAL and ARROW arrays are slot-indexed
 * (0x186580/0x186594/0x1865A8 plus 4*slot) while the BASELINE array is row-indexed
 * (0x1595F0 plus 4*row). */
#define PUFO_FUND_ROW_COUNT 5
#define PUFO_FUNDING_PAD_NAME "funding" /* aFunding @ 0xAB268 */
#define PUFO_FUNDING_PAD_MODE 28        /* 0x5C0B8: MOV R2,#0x1C -- palette slot 28 is funding.pal */

typedef enum FundingRow {
    PUFO_FUND_ROW_USA  = 0, /* TEXTS[1302] "США" */
    PUFO_FUND_ROW_EURO = 1, /* TEXTS[1303] "Евро Альянс" */
    PUFO_FUND_ROW_ASIA = 2, /* TEXTS[1304] "Азиатский Альянс" */
    PUFO_FUND_ROW_AUS  = 3, /* TEXTS[1305] "Австралия" */
    PUFO_FUND_ROW_AFR  = 4  /* TEXTS[1306] "Африка" */
} FundingRow;

extern const i32 g_anFundingRowSlot[PUFO_FUND_ROW_COUNT]; /* {0, 2, 3, 1, 4} */

/* ------------------------------------------------- the monthly funding recompute
 *
 * Evidence: FundingScreen_Init @ 0x5c084 (1256 B), FundingScreen_OpenMonthly @ 0x5d160 (92 B),
 * FundingScreen_ExitThunk @ 0x5c078 (12 B).
 *
 * 1. The pad and the close control. operator new(0x14) + PadImage_ctor into unk_1595DC (the ctor's
 *    result, not the raw allocation), PadImage_Load("funding", 28), origin (0, 0); then the shared
 *    close control g_pUiCloseCtl to x 224, y 1 with FundingScreen_ExitThunk.
 *
 * 2. The money. With P the pending total at unk_1865BC and S the sum of the five country scores:
 *    bonus = (P / (S + 1)) * g_nFundMult, P becomes P + bonus, and the per-score share is
 *    (P + bonus) / (S + 1). Two _rt_sdiv calls (0x5C13C, 0x5C164), both with the divisor in R0.
 *    Each score times that share goes to 0x186580..0x186590.
 *
 * 3. The bases. For each of the nBaseCount bases, the region byte BaseRec.bRegionId (+0x11) selects
 *    slot through the same table Funding_AddRegionBonus uses, and a hit adds 3 * share to that slot
 *    (0x5C1BC..0x5C220). The count is a SIGNED byte (LDRSB at 0x5C188, BLE at 0x5C1B0), so a
 *    negative count runs nothing.
 *
 * 4. The upkeep, by month. unk_159648 is a month counter -- FundingScreen_OpenMonthly increments it
 *    at 0x5D170 before calling this. The table at 0x5C22C..0x5C324 gives one value per row: rows
 *    USA, Euro and Asia share one figure, Australia and Africa each have their own. Month 1..3 gives
 *    -300/-100/-200, month 4 -500/-200/-350, month 5 -700/-500/-300, and BOTH month <= 0 and month
 *    >= 6 give -1100/-800/-300 (the <= 0 case falls into the same block as >= 6, at 0x5C31C). The
 *    value is added to that row's share AND stored into that row's baseline.
 *
 * 5. The bars. Per row: arrow = Funding_DeltaToArrow(share[slot], baseline[row]), then
 *    level = Funding_ScoreToBarLevel(row, arrow), stored into arrow[slot]; if the month is below 3
 *    and the level is negative the level becomes 0 (0x5C3AC..0x5C3BC and its four copies), and the
 *    level is then ADDED to barTotal[slot]. Finally each slot whose bar total went negative has
 *    BOTH its bar total and its arrow zeroed (0x5C4D4..0x5C53C).
 *
 * 6. The pending total is zeroed at 0x5C544. Tact_CheckBattleEnd, StrategList_ProcessEvents and
 *    Smap_BeginCraftSelect are the three writers that put money into it between reports.
 *
 * Deviation: the original indexes apBases with an unchecked byte and dereferences every entry; the
 * port stops at the count it is given and skips a NULL entry. */
/* Every array here is a POINTER into the caller's own copy of the original global it names: the
 * function reads and writes them in place (0x5C168, 0x5C33C, 0x5C3A4, 0x5C3CC), and the same cells are
 * read by FundingScreen_DrawExtended, Geoscape_DrawFundingArm and Geoscape_CloseUI case 19. A by-value
 * copy in the env would give the shell two instances of one global. */
typedef struct FundingInitEnv {
    PadImage         **ppPadSlot;      /* unk_1595DC, the "funding" pad */
    UiControl         *pCloseCtl;      /* g_pUiCloseCtl */
    void (*pfnCloseClick)(void);       /* FundingScreen_ExitThunk @ 0x5c078 */
    i32               *anCountryScore; /* g_nFundScore0..4 @ 0x186568..0x186578, [5] g_nFundMult @ 0x18657C */
    i32              *pnPendingTotal;  /* unk_1865BC */
    i32               *anShare;        /* 0x186580..0x186590, slot-indexed */
    i32               *anBaseline;     /* 0x1595F0..0x159600, row-indexed */
    i32               *anBarTotal;     /* 0x186594..0x1865A4, slot-indexed */
    i32               *anArrow;        /* 0x1865A8..0x1865B8, slot-indexed */
    i32               *pnMonthCounter; /* unk_159648 */
    const BaseRec    *const *apBases;  /* apBases @ 0x114B2C */
    i8                nBaseCount;      /* nBaseCount @ 0x159D80, signed */
} FundingInitEnv;

void FundingScreen_Init(FundingInitEnv *pEnv);

/* FundingScreen_OpenMonthly @ 0x5d160 (92 B): the monthly report opener. It increments the month
 * counter FIRST (0x5D170), so the first report of a new game already runs at month 1, sets the
 * geoscape screen id to 19 (0x5D18C) so Geoscape_CloseUI runs the funding screen's close case, calls
 * FundingScreen_Init, and clears byte_187080 (the shared UI animation flag). nullsub_7 at 0x5D180 is
 * an empty compiler stub and is not ported. */
void FundingScreen_OpenMonthly(FundingInitEnv *pEnv, i32 *pnScreenId, u8 *pbAnimFlag);

/* ------------------------------------------------- the funding screen's own three panels
 *
 * Evidence: FundingScreen_Toggle @ 0x5abe4 (1560 B). It is gated on bFundScreenOpen @ 0x1595EC:
 * set, it closes the screen -- clear the flag, then for each of the three panels
 * (pFundPanelNames @ 0x1595E4, pFundPanelBars @ 0x1595E0, pFundPanelArrows @ 0x1595E8)
 * UiPanel_FreeLines plus operator delete, and for pFundPad (0x1595DC) PadImage_FreeData plus
 * operator delete. Clear, it builds the screen:
 *
 * 1. The flag is set and nFundDisplayTotal @ 0x159604 is zeroed (0x5AC00, 0x5AC0C). That dword is
 *    not a total the recompute left behind -- it is rebuilt here from the five bar values, and
 *    FundingScreen_Draw prints it as the "$" line.
 * 2. The pad: operator new(0x14) + PadImage_ctor, PadImage_Load("funding", 28), origin (0, 0)
 *    (0x5AC14..0x5AC54). Same pad FundingScreen_Init loads; the two do not share an instance.
 * 3. Three 5-line panels, each operator new(0x1BB4) + UiPanel_ctor(5), all nStyle 5 and nFont 16,
 *    all at y 70, at x 10 / 100 / 170. The names panel is the only one that sets nRectX = 5,
 *    nRectY = 70 and nViewW = 230; the bars and arrows panels keep the ctor's 240 / 320. All
 *    three get SetBytes0to2, bColorKey = 0x96, SetBytes6to11(0xC8, 0xB4, 0xF0, 0x14, 0x14, 0x14),
 *    SetBytes3to5(0x32, 0x32, 0xFA) and bSelLineHighlight = 0; the names panel uses SetBytes0to2
 *    (0x64, 0x96, 0x64) and leaves bRowRules at the ctor's 1, while bars and arrows use
 *    (0xB4, 0xB4, 0xB4), clear bRowRules and set bAuxNumbers = 1, and the arrows panel also sets
 *    bAuxPlusSign = 1.
 * 4. The five country names go into the NAMES panel's lines in SLOT order -- TEXTS[1302] США,
 *    TEXTS[1305] Австралия, TEXTS[1303] Евро Альянс, TEXTS[1304] Азиатский Альянс, TEXTS[1306]
 *    Африка (0x5AF5C..0x5B038) -- which is what pins the slots: slot 0 USA, slot 1 Australia,
 *    slot 2 Euro, slot 3 Asia, slot 4 Africa. The bars panel takes g_nFundBar0..4 and the arrows
 *    panel g_nFundArrow0..4 into anLineAux[0..4] (0x5B058..0x5B0E8).
 * 5. A bar that is <= 0 zeroes itself AND its arrow (0x5B0EC..0x5B11C): a country with no bar
 *    shows no arrow either. Then the five (possibly zeroed) bars are summed into nFundDisplayTotal
 *    (0x5B124..0x5B144).
 *
 * Deviations: the original dereferences the ctor result with no null test (0x5AC98 and after) and
 * copies each name byte by byte into a 50-byte line buffer; the port guards the allocation and
 * bounds the copy. */
#define PUFO_FUND_PANEL_LINES 5

/* The country name of each funding slot, in the order the funding screen's own panel draws them. */
extern const i32 g_anFundingSlotText[PUFO_FUND_ROW_COUNT]; /* {1302, 1305, 1303, 1304, 1306} */

typedef struct FundingToggleEnv {
    u8       *pbOpen;        /* bFundScreenOpen @ 0x1595EC: the screen's open flag */
    i32      *pnTotal;       /* nFundDisplayTotal @ 0x159604: the "$" total the draw prints */
    PadImage **ppPad;        /* pFundPad @ 0x1595DC */
    UiPanel **ppPanelNames;  /* pFundPanelNames @ 0x1595E4, the five country names at x 10 */
    UiPanel **ppPanelBars;   /* pFundPanelBars @ 0x1595E0, the bar totals at x 100 */
    UiPanel **ppPanelArrows; /* pFundPanelArrows @ 0x1595E8, the arrow levels at x 170 */
    const u8 *pTextRecs;     /* aTextRecs @ 0xB38D0, for the five names */
    const i32 *anBar;        /* g_nFundBar0..4 @ 0x186594 */
    const i32 *anLevel;      /* g_nFundArrow0..4 @ 0x1865A8 */
} FundingToggleEnv;

void FundingScreen_Toggle(FundingToggleEnv *pEnv);

/* FundingScreen_Open @ 0x5b560 (60 B): the funding screen's opener. FundingScreen_Toggle first,
 * then the shared close control g_pUiCloseCtl to (224, 1) with FundingScreen_CloseThunk @ 0x5abd8 --
 * one of twelve identical Geoscape_CloseUI thunks, so the port takes it as a hook. The control is
 * what the function returns. Note FundingScreen_Init wires the SAME control to
 * FundingScreen_ExitThunk @ 0x5c078; whichever of the two runs last owns the button. */
typedef struct FundingOpenEnv {
    UiControl *pCloseCtl;        /* g_pUiCloseCtl */
    void (*pfnCloseClick)(void); /* FundingScreen_CloseThunk @ 0x5abd8 */
} FundingOpenEnv;

UiControl *FundingScreen_Open(FundingToggleEnv *pToggle, const FundingOpenEnv *pEnv);

/* FundingScreen_Draw @ 0x5b1fc (868 B): gated on the open flag. The pad, the shared frame rails
 * (the same 96 blits TransferScreen_Draw, ManufScreen_DrawFrame and ResearchScreen_Draw carry),
 * the close control's pad, then TEXTS[1149] "Инвестирование стран" centred at y 20 in
 * (210,190,250)/(20,20,20), TEXTS[1150] "Страна Инвест. изм. $" at (10, 50), the three panels in
 * names/bars/arrows order, and finally TEXTS[1151] "Итого: " with the total and a "$" at
 * (10, 160) in (200,180,240)/(20,20,20). The total is the one FundingScreen_Toggle rebuilt --
 * this draw never recomputes it. */
#define PUFO_FUND_TEXT_MONTHLY 1301 /* unk_F4AA1 "Ежемесячный отчет проекта НЛО", the report header */
#define PUFO_FUND_TEXT_ROW0    1302 /* unk_F4B6E; the five row titles are 1302 + row */
#define PUFO_FUND_TEXT_TITLE   1149 /* unk_ED0E9 = aTextRecs + 205*1149 */
#define PUFO_FUND_TEXT_COLUMNS 1150 /* unk_ED1B6 */
#define PUFO_FUND_TEXT_TOTAL   1151 /* unk_ED283 */
#define PUFO_FUND_LABEL_MAX    104  /* the original's char psz[104] */

typedef struct FundingDrawEnv {
    const u8          *pbOpen;      /* bFundScreenOpen @ 0x1595EC: nothing is drawn while closed */
    const u8          *pTextRecs;   /* aTextRecs @ 0xB38D0 */
    const i32         *pnTotal;     /* nFundDisplayTotal @ 0x159604 */
    PadImage          *pPad;        /* pFundPad, blitted first */
    UiPanel           *pPanelNames; /* pFundPanelNames @ 0x1595E4 */
    UiPanel           *pPanelBars;  /* pFundPanelBars @ 0x1595E0 */
    UiPanel           *pPanelArrows;/* pFundPanelArrows @ 0x1595E8 */
    UiControl         *pCloseCtl;   /* g_pUiCloseCtl */
    UiControlBlitFn    pfnCtlBlit;  /* the control's pad blit, injected as everywhere else */
    UiControlBlitFn    pfnCtlBlitEx;
    void              *pCtlUser;
    const GfxColorEnv *pColor;      /* pGX @ 0x1139B8, the colour-pair owner */

    /* Only FundingScreen_DrawExtended reads these. */
    const i32         *anShare;     /* g_nFundShare0..4 @ 0x186580, slot-indexed */
    const i32         *anBaseline;  /* g_nFundBase0..4 @ 0x1595F0, row-indexed */
    const i32         *anBar;       /* g_nFundBar0..4 @ 0x186594, slot-indexed */
    i32                nMonthCounter; /* nMonthCounter @ 0x159648 */
} FundingDrawEnv;

void FundingScreen_Draw(const FundingDrawEnv *pEnv);

/* FundingScreen_DrawExtended @ 0x5b59c (2780 B): the monthly report itself. It has NO open-flag
 * gate -- unlike FundingScreen_Draw it draws whatever it is called -- and it does not read the
 * three panels at all: it re-derives everything from the funding arrays.
 *
 * The colour pair is set BEFORE the pad (240,240,240 / 20,20,20), then the pad, the same frame
 * rails, the close control's pad, and TEXTS[1301] "Ежемесячный отчет проекта НЛО" centred at y 5.
 *
 * Then five rows, in REPORT order (row -> slot 0, 2, 3, 1, 4). For each row:
 *   - the arrow is `Funding_DeltaToArrow(share[slot], baseline[row])`, kept as a signed byte;
 *   - a separate "is this country in the red" flag is `(arrow < 0) ? (bar[slot] < 0) : false`
 *     (0x5B810..0x5B820) -- the bar, not the share, decides it;
 *   - while the month counter is below 3 every negative arrow is drawn as 0 (0x5B8CC..0x5B8F0),
 *     the same clamp FundingScreen_Init applies to the stored level;
 *   - the row title is TEXTS[1302 + row] at (5, 60*row + 20) in (40,240,40), then three commentary
 *     lines at y + 35, + 45 and + 55 in (180,180,240).
 *
 * The commentary is TEXTS[1307..1333], selected by the arrow and by whether the row is Euro or
 * Asia (rows 1 and 2 get their own wording, 0x5BA34..0x5BF24). The third line has a negative and a
 * non-negative variant only for the two negative arrows; 0, +1 and +2 have one line each. A row
 * whose arrow is outside -2..2 copies nothing and redraws the previous row's three lines -- the
 * original's buffers are not cleared between rows, and the port keeps that. */
void FundingScreen_DrawExtended(const FundingDrawEnv *pEnv);

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
 * The soldier screen keeps its roster row in the dword cursor at 0x159E08 (read by
 * SoldierScreen_Draw @ 0x65290, EquipSelected @ 0x619cc, SelectListed @ 0x64a9c and CopyNameToEdit
 * (0x6318c, ported below)). The name-edit handlers resolve the current detail soldier through the
 * separate unk_159760 cursor; CopyNameToEdit also uses this roster cursor to choose its output row.
 * These two handlers move the roster cursor to the next / previous soldier that belongs to the base
 * the screen is showing.
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
 * TextInputDialog (0x632cc), OnPointer (0x872d0), OnPointerUp (0x89450) and CloseUI (0x8b94c) - none
 * of those four are ported, so they are named without a citation on purpose).
 * The original returns the byte_159E10 address; no caller tests it, so the port returns void. */
void Ui_SetTransferFlag(u8 *pbTransferFlag, void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser),
                        void *pUser);

/* ------------------------------------------------- the new-base screen
 *
 * Evidence: NewBaseScreen_Open @ 0x615d0, NewBaseScreen_TryConfirm @ 0x6152c.
 *
 * Open (ARM 0x615D0..0x61658): Sound_Play(17, 4); if the SIGNED base count (0x159D80, LDRSB
 * at 0x615F0) is already 8 it returns without touching anything. Otherwise it positions the
 * frame control (g_pUiCloseCtl) at x=224, y=1 (both STRH, so 16-bit stores), sets the geoscape
 * screen id (0x186BC4) to 2, installs TryConfirm as the confirm control's (unk_186BA4) click
 * handler, calls Smap_ZoomOut twice on the viewport at 0x1863E8 (2 -> 1 -> 0), clears
 * byte_187080 (STRB at 0x6164C), and installs the close thunk on the frame control.
 *
 * TryConfirm (ARM 0x6152C..0x61590): Sound_Play(17, 4). If the placement-pending byte at
 * 0x1870AC is non-zero the funds test is skipped entirely and Geoscape_OnMapClick runs.
 * Otherwise cost = aNewBaseCostByRegion[byte_159DF4 - 128] (the ARM composes it as
 * `ADD R3, R0, R3, LSL#2` + `LDR R3,[R3,#-0x200]` at 0x61554..0x61568, so the byte is the region
 * id 0x80..0x89 and the table index is region - 0x80), and funds (unk_1598D8) - cost >= 0 also runs
 * OnMapClick. On shortfall it stores 1 into 0x1870C8 and 0 into dword_1870B4 and returns
 * dword_1870B4 (0); OnMapClick's result is returned otherwise.
 *
 * Width notes the source only shows in ARM: 0x1870AC is read as a BYTE (LDRB at 0x6153C);
 * the failure store into 0x1870C8 is a DWORD store (STRMI at 0x61574) while almost every reader
 * in the image loads it as a byte, so the port stores a byte (deviation, unobservable).
 * The one reader that does not is Geoscape_OnPointerUp: 0x89584 loads it as a DWORD and tests it
 * against 1, and its three clears (0x895C4 / 0x895D4 / 0x89600) are dword stores. That is why
 * GeoscapePointerEnv carries it as `u32 *pdwShortfall`.
 * byte_159DF4 is the pending region id 0x80..0x89 (0xFF marks "no pending placement" in the pointer
 * handlers); the original indexes the 10-entry cost table unguarded, so a region outside 0x80..0x89
 * reads adjacent image bytes. The shipped paths reach the ten regions only; the port treats
 * out-of-range as cost 0 (deviation, documented in pufo_baseui.c). */
#define PUFO_NEW_BASE_COST_COUNT 10
extern const i32 g_anNewBaseCostByRegion[PUFO_NEW_BASE_COST_COUNT]; /* aNewBaseCostByRegion @ 0xAB488 */

typedef struct NewBaseOpenEnv {
    void (*pfnPlaySample)(u32 nSample, u32 nArg, void *pUser);
    UiControl *pCtlFrame;          /* g_pUiCloseCtl (GEOUI_CTL_186B00) */
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
    u8 bPendingRegionId;      /* byte_159DF4: 0xFF = none, else the region id 0x80..0x89 */
    i32 *pnFunds;         /* unk_1598D8 */
    u8 *pbFlag1870C8;     /* 0x1870C8, the GeoscapeState low-supply flag */
    i32 *pnFlag1870B4;    /* dword_1870B4 */
    i32 (*pfnMapClick)(void *pUser); /* Geoscape_OnMapClick(&MEMORY[0x1864F8]), ported at this file:7242 */
    void *pUser;
} NewBaseConfirmEnv;

i32 NewBaseScreen_TryConfirm(u8 bPlacementPending, const NewBaseConfirmEnv *pEnv);

/* BaseInfoScreen_TogglePanelMode @ 0x55754 (104 B): the base-info screen's panel-mode button. It
 * toggles one byte and writes a scroll cursor into all three of the screen's panels.
 *
 * The two numbers are not the same, and that is the whole trap. When the flag was ZERO it becomes 1
 * and the cursor written is 0; when it was non-zero it becomes 0 and the cursor written is 16. The
 * ANDS at 0x55764 sets the flags, and nothing between it and the MOVEQ at 0x55784 -- two MOVs and
 * two conditional stores, none of which touch the flags -- changes them, so the MOVEQ answers the
 * flag's ORIGINAL value, not the toggled one. Hex-Rays shows it as a separate `if (v0) v1 = 0;`
 * after the branch for the same reason.
 *
 * The cursor field is UiPanel.nUnk3028 @ +0xBD4, the same scroll cursor the panel paging helpers
 * read. The three panels are the base-info screen's own (unk_158820 / unk_158824 / unk_158838, all
 * built by BaseInfoScreen_BuildPanels (0x557f4, ported at this file:12420), and the stores go in the order
 * 0x158824, 0x158820, 0x158838. Installed as a click handler (0x57714, 0x577BC); the original
 * leaves the third panel pointer in R0 and nothing reads it, so the port returns void. */
#define PUFO_BASEINFO_CURSOR_ON  16 /* the cursor written when the mode flag was already set */

typedef struct BaseInfoToggleEnv {
    u8      *pbModeFlag;  /* byte_AB190 @ 0xAB190 */
    UiPanel *pPanelA;     /* unk_158820 */
    UiPanel *pPanelB;     /* unk_158824, written first */
    UiPanel *pPanelC;     /* unk_158838, written last */
} BaseInfoToggleEnv;

void BaseInfoScreen_TogglePanelMode(const BaseInfoToggleEnv *pEnv);

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
 * ResearchScreen_Open (0x616ac, ported at this file:9756) and SoldierScreen_Toggle (0x64af0,
 * ported) are installed through the env like the rest, so all fifteen handlers arrive as plain
 * click thunks -- the same seam Geoscape_InitUI uses for its root controls.
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

/* ------------------------------------------------- two geoscape widget handlers
 *
 * Evidence: Base_EnterBuildMode @ 0x84e20 (124 B) and Smap_SetWaypoint @ 0x85440 (132 B). Both are
 * installed as widget handlers -- Base_EnterBuildMode by Geoscape_InitUI (0x865A8, 0x86640),
 * Smap_SetWaypoint by Geoscape_Draw (0x8F938, 0x8FD2C) -- and neither return value is read, so
 * both are void here. The originals leave a pointer in R0 (Base_EnterBuildMode returns the control
 * it parked, Smap_SetWaypoint returns GameTime_Pause's leftover clock address 0x159618); the port
 * drops them, the same policy it applies to UiLineList_AppendWithSound.
 *
 * Base_EnterBuildMode plays sample 17, re-runs the workshop and lab needed passes for the selected
 * base, sets the geoscape screen id to 1, rebuilds the fifteen nav controls, and then parks the
 * control at 0x186BB4 at 1000,1000 -- off every 240x320 surface, which is how the original hides a
 * widget it has no slot for rather than freeing it.
 *
 * Smap_SetWaypoint reads colour slot 0 of message line 0, and that value IS the ship index: it
 * stores it into both 0x159E74 and 0x187054, clears that craft's waypoint/intercept assignment,
 * drops message line 0, re-inserts it as flag 11 carrying the ship index, and pauses the clock. */
typedef struct BaseBuildModeEnv {
    NavControls     *pNavOut;             /* the fifteen nav-control pointers Geoscape_BuildNavControls fills */
    const NavCtlEnv *pNavEnv;             /* their wiring */
    u8              *abFacilityGrid;      /* gBaseFacGrid @ 0x159768 */
    const u16       *apManufCols;         /* auManufCol @ 0x15E19C, the workshop pass's budget source */
    const u16       *apResearchCols;      /* auResearchCol @ 0x16022C, the lab pass's budget source */
    const u8        *puResearchEntries;   /* nResearchEntryCount @ 0x160C4C, a byte */
    const i8        *pnSelectedBaseIdx;   /* nSelectedBaseIdx @ 0x159D81 */
    i32             *pnScreenId;          /* nGeoscapeScreen @ 0x186BC4 */
    UiControl       *pParkCtl;            /* pGeoscapeCtlA, moved to 1000,1000 */
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void *pSampleUser;
} BaseBuildModeEnv;

void Base_EnterBuildMode(const BaseBuildModeEnv *pEnv);

typedef struct SmapWaypointEnv {
    UiLineList           *pLines;            /* the root message list @ 0x158890 */
    i32                  *pnMsgShipIdx;    /* nMsgSelectedShipIdx @ 0x159E74, an apCraft index */
    i32                  *pnSelectedShipIdx; /* 0x187054 */
    const ShipRemovalEnv *pShipEnv;          /* Ship_ClearWaypointIntercept's env */
    GameTime             *pTime;             /* GameTime_Pause is one store to bPaused */
} SmapWaypointEnv;

void Smap_SetWaypoint(const SmapWaypointEnv *pEnv);

/* ------------------------------------------------- dismissing a selected map target
 *
 * Evidence: Smap_CancelTarget @ 0x84254 (152 B). Installed as a click handler by Geoscape_Draw.
 *
 * It reads the target out of colour SLOT 1 of message line 0 (0x84268) -- Smap_SetWaypoint puts the
 * ship index in slot 0, so the two handlers read different slots of the same line -- and the ship
 * out of the selected-ship dword, and hands both to Ship_LaunchIntercept. Then it drops the
 * selection: bMapSelectionFlag, 0x187098, message line 0, and finally 0x18708C and 0x187090.
 *
 * The clock test at 0x842A4 is on `LDRB R0,[R4,#0xD48]` with R4 = 0x158890, which is the root
 * panel's line-list count byte (200 flags + 200 * 16 record bytes = 3400 = 0xD48), read AFTER the
 * removal. So the clock is un-paused only when no message line is left standing. Renamed in the IDB
 * to gUiRootPanel_nLineCount. */
typedef struct SmapCancelTargetEnv {
    UiLineList           *pLines;            /* the root message list @ 0x158890 */
    i32                  *pnSelectedShipIdx; /* 0x187054 */
    u8                   *pbSelectionFlag;   /* bMapSelectionFlag @ 0x159E84 */
    u8                   *pbFlag187098;
    u8                   *pbFlag18708C;
    u8                   *pbFlag187090;
    GameTime             *pTime;
    const ShipRemovalEnv *pShipEnv;          /* Ship_LaunchIntercept's env */
} SmapCancelTargetEnv;

void Smap_CancelTarget(const SmapCancelTargetEnv *pEnv);

/* ------------------------------------------------- the two "keep the course" selection answers
 *
 * Evidence: Smap_ConfirmSelected @ 0x845d8 (92 B), Smap_ConfirmSelectedAlt @ 0x853e4 (92 B). Both
 * are installed as click handlers by Geoscape_Draw, and both are the same eight instructions: read
 * the ship index out of colour SLOT 0 of message line 0, answer it with Ship_ClearWaypointIntercept,
 * then run Smap_ClearSelection @ 0x84648. The craft keeps the course it was already on and its
 * proximity timer is parked at 0xFFFF; the dismiss half of the pair, Smap_DismissSelected @ 0x84428,
 * is already ported in game/pufo_strateg.c and sends the craft home instead.
 *
 * Smap_ConfirmSelectedAlt is the same body with the tail reordered: it clears bMapSelectionFlag at
 * 0x85420 BEFORE the line-count test at 0x8542C, where the other one clears it after. Nothing reads
 * the flag in between, so the two orders cannot be told apart from the outside -- recorded because
 * the difference is real in the binary and it is the only difference. */
typedef struct SmapSelectedEnv {
    UiLineList           *pLines;            /* the root message list @ 0x158890 */
    const ShipRemovalEnv *pShipEnv;          /* the ship call's env */
    GameTime             *pTime;
    u8                   *pbSelectionFlag;   /* bMapSelectionFlag @ 0x159E84 */
} SmapSelectedEnv;

void Smap_ConfirmSelected(const SmapSelectedEnv *pEnv);      /* 0x845d8 */
void Smap_ConfirmSelectedAlt(const SmapSelectedEnv *pEnv);   /* 0x853e4 */

/* ------------------------------------------------- confirming a map target for a craft
 *
 * Evidence: Smap_ConfirmCraftTarget @ 0x842ec (316 B). Installed as a click handler by Geoscape_Draw.
 *
 * Four steps.
 *
 *   1. The ship index is colour SLOT 0 of message line 0, and it is stored into the selected-ship
 *      dword 0x187054 (0x84310) before anything else uses it.
 *   2. If the craft is intercepting AND the item it chases is nType 5 (a waypoint), that waypoint is
 *      removed from the strateg list and bIntercepting is cleared (0x84374). A waypoint is one-shot,
 *      so it is deleted rather than left pointing at a craft that no longer wants it. If the old
 *      target was a real contact, or the craft was not intercepting, bIntercepting is NOT cleared --
 *      the clear sits inside the type-5 branch, not after it.
 *   3. A fresh waypoint is added: nType 5, bActive 1, bCraftClass 0, nId -1, bVisible 1, at the
 *      pending click in 0x186414 / 0x186418. Those two dwords are 0x2C and 0x30 past the viewport
 *      block at 0x1863E8 but they are outside its 36 bytes, and each has exactly one xref in the IDB
 *      -- this function. Their writer is Smap_HitTestStrategSelect @ 0x82be8, the flag-27 pick-a-cell
 *      tail at 0x83784 / 0x83788, so the port takes them as inputs and the app has to wire the two
 *      environments onto the same storage. StrategList_AddItem scales a type-5 position out of the current
 *      zoom mode, so the viewport's nMode is passed too.
 *   4. The new item's index is GetCount() - 1 (0x843D0), the ship index is read out of the message
 *      line a SECOND time (0x843E0, after the removal has not yet happened but after the list
 *      changed), and Ship_LaunchIntercept sends the craft to it. Then the usual tail: drop line 0,
 *      unpause if the root line-list count is now zero, clear bMapSelectionFlag.
 *
 * StrategList_RemoveAt ignores its first register argument entirely (0x82AB0 loads the Map context
 * itself), which is why the ARM's `MOV R0, R6` before that call is dead. */
typedef struct SmapConfirmTargetEnv {
    UiLineList           *pLines;             /* the root message list @ 0x158890 */
    i32                  *pnSelectedShipIdx;  /* 0x187054 */
    PtrArray             *pStrategList;       /* 0x1864F8 + 0x3C */
    i32                  *pnSeed;             /* 0x18640C, StrategItem_ctor's seed counter */
    const SmapView       *pView;              /* 0x1863E8; its nMode scales a type-5 position */
    const i32            *pnPickX;            /* 0x186414, the pending click */
    const i32            *pnPickY;            /* 0x186418 */
    const ShipRemovalEnv *pShipEnv;           /* the removal and the launch */
    GameTime             *pTime;
    u8                   *pbSelectionFlag;    /* bMapSelectionFlag @ 0x159E84 */
} SmapConfirmTargetEnv;

void Smap_ConfirmCraftTarget(const SmapConfirmTargetEnv *pEnv);

/* ------------------------------------------------- putting a map target on the pending order
 *
 * Evidence: Smap_SelectMapTarget @ 0x84484 (340 B). Installed as a click handler by Geoscape_Draw.
 *
 * 1. 0x84498: bMapSelectionFlag is SET here. This is the flag Geoscape_DrawScreen tests at 0x6C180,
 *    which closes the geoscape UI and draws nothing at all on the next frame.
 * 2. 0x844A4: the ship index is colour SLOT 0 of message line 0, stored into the selected-ship dword
 *    0x187054 at 0x844C4. Every later use in this function re-reads that stored dword, not the line.
 * 3. 0x844D0..0x84514: if the craft has bIntercepting and the item at its nStrategIdx is nType 5 (a
 *    waypoint), the waypoint is removed and bIntercepting is cleared -- the same shape as
 *    Smap_ConfirmCraftTarget's step 2, and again the clear sits INSIDE the type-5 branch.
 * 4. 0x84520..0x84544: with the Cydonia topic researched (aResearchTopics[28].bUnlocked) and the
 *    craft's bCraftType == 2 -- the Avenger, the constant the shipped data confirms -- arm
 *    bFinalMissionCtlArmed. Note the ship is re-fetched through the stored dword here (0x844E8,
 *    0x84518), not through the local.
 * 5. 0x84554..0x84588: the line colour is read a THIRD time, a flag-27 line is inserted at index 1
 *    carrying it, line 0 is removed, and the clock is paused. Unlike the sibling handlers there is no
 *    unpause here at all: this is the arm that PAUSES the geoscape.
 * 6. 0x84598..0x845A4: the colour is read a FOURTH time -- after the insert and the removal -- and
 *    written to both 0x187054 and 0x159E74. After the removal the line at index 0 IS the inserted
 *    flag-27 line, so what lands in those two slots is the value the insert just carried.
 *
 * Deviation: the original indexes apCraft with the line colour with no bounds test (0x844BC); the port
 * checks the index against the craft count and skips the two ship-dependent steps when it is out of
 * range. Not a ledger row -- the shipped panel only ever carries a valid index. */
typedef struct SmapSelectTargetEnv {
    UiLineList           *pLines;             /* the root message list @ 0x158890 */
    i32                  *pnSelectedShipIdx;  /* 0x187054 */
    i32                  *pnSlot159E74;       /* 0x159E74, written with the same value at 0x845A4 */
    PtrArray             *pStrategList;       /* 0x1864F8 + 0x3C */
    const ShipRemovalEnv *pShipEnv;           /* apCraft / nCraftCount, for the waypoint removal */
    const ResearchTopicLive *aTopics;         /* aResearchTopics @ 0x15EF08 */
    GameTime             *pTime;
    u8                   *pbSelectionFlag;    /* bMapSelectionFlag @ 0x159E84 */
    u8                   *pbFinalMissionCtl;  /* bFinalMissionCtlArmed @ 0x187068 */
} SmapSelectTargetEnv;

void Smap_SelectMapTarget(const SmapSelectTargetEnv *pEnv);

/* ------------------------------------------------- answering the message line under the map
 *
 * Evidence: Smap_ResolveInterceptUI @ 0x84964 (340 B). Installed as a click handler by Geoscape_Draw.
 *
 * 1. 0x84968: GameTime_Set5Sec -- the geoscape goes back onto a five-second tick and sample 17 plays.
 *    0x84984: byte_18709C (the "intercept resolved" byte the two pointer dispatchers read) is cleared.
 * 2. 0x84988: flag 27 on message line 0 -- the pick-a-cell line Smap_HitTestStrategSelect and
 *    OptionsScreen_OnConfirm both append -- sends line 0's colour slot 0 to Ship_RetargetTravel, i.e.
 *    the craft is sent home along a fresh travel leg.
 * 3. 0x849B4: the flag is read a SECOND time and tested against 13. On 13: insert a flag-11 line at
 *    index 1 carrying line 0's colour SLOT 1 masked to a byte (0x849DC), remove line 0 (0x849FC), so
 *    the flag-11 line lands at index 0; set byte_18709C to 1 (0x84A04); GameTime_Pause; and return.
 *    The branch jumps 0x84A10 -> 0x84A9C, which is past the armed-byte clear at 0x84A94, so this path
 *    leaves bFinalMissionCtlArmed alone.
 * 4. Any other flag: clear 0x187090, 0x18708C, bMapSelectionFlag and 0x187098 (0x84A20..0x84A3C) --
 *    the same four bytes Smap_CancelTarget drops, in a different order and before the rest instead of
 *    after it. Then three dismiss tests, each re-reading line 0 (0x84A44, 0x84A58, 0x84A6C) against
 *    23, 21 and 22 and calling Smap_DismissSelected on a match. **Each of those calls removes line 0
 *    itself**, so the three tests are not three tests of one flag: a match shifts the list and the
 *    next test sees the line that was underneath. Then 0x84A80 removes line 0 AGAIN, unpauses only if
 *    the list is now empty (0x84A84 reads the count byte at 0x158890+0xD48 after the removal), and
 *    clears bFinalMissionCtlArmed.
 *
 * ORIGINAL BUG (0x84A80): that tail removal is unconditional. When one of 23/21/22 matched,
 * Smap_DismissSelected already dropped the line, so the tail drops the NEXT message instead -- an
 * unrelated line is eaten and the clock can unpause a step earlier than the messages warrant. */
typedef struct SmapResolveInterceptEnv {
    UiLineList           *pLines;            /* the root message list @ 0x158890 */
    u8                   *pbInterceptDone;   /* byte_18709C */
    u8                   *pbFlag187090;
    u8                   *pbFlag18708C;
    u8                   *pbFlag187098;
    u8                   *pbSelectionFlag;   /* bMapSelectionFlag @ 0x159E84 */
    u8                   *pbFinalMissionCtl; /* bFinalMissionCtlArmed @ 0x187068 */
    GeoscapeWorld        *pWorld;            /* GameTime_Set5Sec's tick dword */
    UiControl *const     *apSpeedCtl;        /* the six nav speed controls */
    const ShipRemovalEnv *pShipEnv;          /* Ship_RetargetTravel and Smap_DismissSelected */
    GameTime             *pTime;
} SmapResolveInterceptEnv;

void Smap_ResolveInterceptUI(const SmapResolveInterceptEnv *pEnv);

/* ------------------------------------------------- the map pointer release
 *
 * Evidence: Smap_OnPointerUp @ 0x84688 (732 B). Installed as a click handler by Geoscape_Draw.
 *
 * Two arms on bMapSelectionFlag, and both end with RemoveLine(0) plus the unpause-if-the-list-is-empty
 * tail at 0x84944..0x84954.
 *
 *   Set (0x8491C): the target is colour slot 0 of message line 0 and the craft is the selected-ship
 *   dword, and Ship_LaunchIntercept sends the craft out to it. Then the flag is cleared.
 *
 *   Clear (0x8469C): GameTime_Set5Sec, then the view is recentred on the strateg item that colour slot 0
 *   names. Both GetLineColor calls read SLOT 0 (0x846DC and 0x84700), so it is ONE item whose nX and nY are
 *   used -- not the pair the two reads look like.
 *     - v3 = nMapW / 2 - 120 and v4 = nMapH / 2 - 160. 0x846B4..0x846D8 is MOVS / ADDMI #1 / ASR #1,
 *       and ADDMI fires only on a NEGATIVE value: for a non-negative extent nothing is added and the
 *       half is plain `n >> 1`, while the +1 only makes a negative extent round toward zero the way C
 *       division does. It is NOT a ceiling -- an earlier reading of this triple got that wrong.
 *     - Zoom unscale: mode 1 multiplies both coordinates by 1/1.333 and mode 2 by 1/1.694 -- the exact
 *       reciprocals of the STRATEG_SCALE_MODE1/2 pair StrategList_AddItem scales OUT with (pool words
 *       0x3FE80189_5077D685 and 0x3FE2E3E4_31916105). Mode 0 leaves them alone.
 *     - nScrollY = nY - v4 - 160, and when nY - 160 is negative it becomes -v4 instead. The condition is
 *       the N flag of the `ADDS R2, R0, R5` at 0x84838, which is nY - 160 because v4 cancels -- and nY is
 *       the ZOOM-SCALED coordinate, so a target that unscales below 160 takes this arm (mode 2 turns 250
 *       into 147 and 147 < 160).
 *     - nScrollX = nX - v3 - 120, unconditionally.
 *     - Then a second test on the strip height. With uHeight = [0x186BBC]->uHeight, if the value just
 *       stored PLUS v4 exceeds uHeight + nMapH - 320, nScrollY is REPLACED by uHeight - v4 + nMapH - 320
 *       (0x84874..0x84884). That sum is nY - 160 on the normal path and 0 on the negative one.
 *     - Crosshair: nSmapCrosshairY = nY - nScrollY(final) - v4, wrapped once by nMapH when greater;
 *       nSmapCrosshairX = 120, or 120 - nMapW when nMapW is below 120. The ARM reaches the 120 as
 *       (v3 + 120) - v3 at 0x84860 / 0x84880, which is why the pseudocode looks arbitrary.
 *       bSmapCrosshairPending = 1 and nRadarCooldownR = 0.
 *
 * Geoscape_Draw @ 0x8C69C is the only reader of the crosshair triple. While bSmapCrosshairPending is set
 * and nRadarCooldownR < 5 it blits a 1-pixel vertical line at x = nSmapCrosshairX + 3 (height
 * 320 - stripHeight) and a 240 x 1 horizontal line at y = nSmapCrosshairY + 3, both in the 50/200 colour
 * pair; otherwise it clears the flag and parks the cooldown at 0xFF. Renamed in the IDB accordingly. */
typedef struct SmapPointerUpEnv {
    u8                   *pbSelectionFlag;    /* bMapSelectionFlag @ 0x159E84, the arm selector */
    UiLineList           *pLines;             /* the root message list @ 0x158890 */
    i32                  *pnSelectedShipIdx;  /* 0x187054 */
    SmapView             *pView;              /* 0x1863E8 */
    PtrArray             *pStrategList;       /* 0x1864F8 + 0x3C */
    const PadImage       *pStripPad;          /* [0x186BBC], its uHeight is the strip */
    i32                  *pnCrosshairX;       /* nSmapCrosshairX @ 0x186BC8 */
    i32                  *pnCrosshairY;       /* nSmapCrosshairY @ 0x186BCC */
    u8                   *pbCrosshairPending; /* bSmapCrosshairPending @ 0x1870B0 */
    i32                  *pnCooldownR;        /* nRadarCooldownR @ 0xAC218 */
    GeoscapeWorld        *pWorld;             /* GameTime_Set5Sec's tick dword */
    UiControl *const     *apSpeedCtl;         /* the six nav speed controls */
    const ShipRemovalEnv *pShipEnv;           /* Ship_LaunchIntercept */
    GameTime             *pTime;
} SmapPointerUpEnv;

void Smap_OnPointerUp(const SmapPointerUpEnv *pEnv);

/* ------------------------------------------------- delivering what a mission brought back
 *
 * Evidence: Smap_GrantSiteRecovery @ 0x98b50 (2572 B), one caller, Geoscape_TickHourly @ 0x5d1bc.
 * It runs on the hourly geoscape tick over the smap site array: it ages every site's countdown
 * byte, delivers the sites whose countdown reached zero, and then removes the delivered records.
 *
 * Phase 1 (0x98B68..0x98BAC): every site with bFlag0 > 0 gets bFlag0 - 1. That is the countdown
 * phase 2 tests for zero, so a site is delivered a fixed number of ticks after it is queued.
 *
 * Phase 2 (0x98BB0..0x994C4), per site with bFlag0 == 0:
 *
 *   1. 0x98BE0..0x98C24: find the base whose BaseRec.bUnk4 (@ +4) equals site->bFlag1, scanning
 *      apBases while the index stays below nBaseCount. A miss leaves the index AT nBaseCount and
 *      the rest of the body then runs through apBases[nBaseCount] (deviation).
 *   2. 0x98C30..0x98CF0: the stores gate. With bHasLoot set, the site's own cargo volume
 *      (Base_CountItemQtyAcrossBases with the scan ON and this site as the gate site) plus what
 *      the base already holds (Base_CalcStorageUsed) is tested against Base_CalcStoresCapacity;
 *      over budget raises the shortfall dword and posts message 42. With bHasLoot clear the
 *      shortfall is raised unconditionally and message 41 is posted -- the item loop runs either
 *      way, it just has nothing to add.
 *   3. 0x98D00..0x99058: the item loop over every object slot. Slots 77..104, 114 and 144..148
 *      are the alien slots and carry one extra gate: each non-zero quantity raises a running
 *      counter held against Base_CalcAlienContainmentCap for the base, and the counter is NOT
 *      reset per site -- it accumulates over the whole call, so a later site sees what earlier
 *      sites already put into containment. Over cap: shortfall dword set, message 36, and the
 *      quantity MOVES to that alien's corpse slot (Smap_CorpseSlotForAlien, the two-switch
 *      structure at 0x98DBC..0x98F98) with the source slot zeroed. Everything else, and every
 *      alien that still fits, takes the normal path: the quantity is added to the base except
 *      for slots 117..121 (the new-craft counts, delivered by step 5 instead), a debug dump
 *      (Transfer_WriteErrorDump @ 0x996e4, the transfer error dump above) fires with the base index
 *      and the slot when the base's column for the slot passes 60000, and
 *      Base_DistributeRecoveredItems runs after EVERY slot -- empty slot or not, alien slot or not.
 *   4. 0x9905C..0x99330: uRecoveredSoldiers calls of Soldier_CreateAndClassify (port:
 *      Unit_CreateAndClassify) with the base index as nKind. Then per recovered craft: the craft
 *      whose Ship.bId matches site->anCraftIds[i] gets Ship.bHomeBase = the base index
 *      unconditionally, and a 5x5 scan of the base's facility grid looks for a hangar cell --
 *      facType cell == FAC_HANGAR, buildDays cell == 0, and layer2 tested at fac + 79 + 6*row + col
 *      (0x99164..0x99174, the 6*row from `ADD R4, R4, #6` at 0x99210). On a match bHangarX = col + 1,
 *      bHangarY = row + 1, and the craft glyph (33 + bCraftType) is stamped into layer2 at
 *      fac + 79 + 6*row + col -- the tested cell and the stamped cell are the same one. The one
 *      difference from Ship_SpawnAtSlot's scan is the unknown craft type: here it CONTINUES the scan,
 *      there it returns unstamped.
 *   5. 0x99340..0x993CC: for i in 0..4, site->auItemQty[117 + i] new craft of type
 *      {0, 3, 4, 1, 2} -- the five slots carry the shipped craft names and the map lands each
 *      name on its own CraftType. Ship_ProductSlotToType @ 0x65a6c answers that same map from the
 *      slot id, so the port calls it instead of carrying a second copy. Every spawn homes the
 *      craft at the same base.
 *   6. 0x993DC..0x99468: for i < bRecoveredSoldierAssign, the roster soldier whose nUnk440 equals
 *      site->anSoldierIds[i] gets Soldier.nKind = the base index.
 *   7. 0x99484..0x994A0: bFlag0 = -1 marks the site delivered, then the three needed passes run
 *      with the budgets the original computes inside them: ammo items; occupied space plus
 *      outbound transfer space (the -1 gate); storage used plus item quantity across bases (the
 *      -1 gate).
 *
 * Phase 3 (0x994C8..0x99558): every site marked -1 is dropped by shifting the records after it
 * down one slot, and nSmapSiteCount is stored back with them removed. The copy length is
 * 371*(count - index) WORDS, one record more than the records still inside the count -- that
 * extra record is what ends the loop, because the unused slot after the last live site carries
 * bFlag0 == 0 and shifts in. A run of delivered sites collapses in one pass for the same reason:
 * the index is re-tested after every shift.
 *
 * Deviations: the original bounds nothing here. A base-lookup miss continues through
 * apBases[nBaseCount] -- the port marks the site delivered and skips the delivery. A craft- or
 * soldier-lookup miss writes through apCraft[Map_nCraftCount] / apSoldiers[nSoldierCount] -- the
 * port skips that craft or soldier. The compaction's inner loop is guarded only by the copy, so
 * a site array that is entirely delivered would spin the count into negative territory -- the
 * port also bounds the loop by the count. The original returns the viewport-mode global at
 * 0x1863E8 (register residue, no caller reads it); the port returns void. */
typedef struct SmapGrantRecoveryEnv {
    SmapSite        *aSites;             /* aSmapSites @ 0x162030 */
    i32             *pnSiteCount;        /* nSmapSiteCount @ 0x186410, read and stored back */
    u8              *abFacilityGrid;     /* abFacilityGrid @ 0x159768 */
    const u8        *pbBaseCount;        /* nBaseCount @ 0x159D80, read signed */
    BaseRec         **apBases;           /* Map.apBases @ Map + 0x1174 */
    Ship            **apCraft;           /* Map.apCraft @ Map + 0x10AC */
    const u8        *pnCraftCount;       /* Map_nCraftCount @ 0x114B4C, read unsigned */
    Soldier         **apSoldiers;        /* Map.apSoldiers @ Map + 0x1198 */
    const i32       *pnSoldierCount;     /* nSoldierCount @ 0x115350 (u16 semantics, as everywhere) */
    ObjectRecFull   **apObjects;         /* Map.apObjects @ Map + 0x108 */
    i32              nObjectCount;       /* Map_nObjectCount @ 0x114A60 */
    UiLineList      *pLines;             /* the root message list @ 0x158890 */
    u32             *pdwShortfall;       /* byte_1870C8, written as a dword */
    UnitCreateEnv   *pUnitEnv;           /* Unit_CreateAndClassify's Map block */
    ShipSpawnEnv    *pShipEnv;           /* Ship_SpawnAtSlot's Map block */
    void (*pfnPlaySample)(i32 nSample, void *pUser); /* the message seam */
    void *pSoundUser;
    const char      *pszAppDir;          /* szAppDir @ 0x1883B0, the dump's output directory */
} SmapGrantRecoveryEnv;

void Smap_GrantSiteRecovery(const SmapGrantRecoveryEnv *pEnv);

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
    i32          *pnMsgShipIdx;        /* nMsgSelectedShipIdx @ 0x159E74, an apCraft index */
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

/* ------------------------------------------------- the select test under the pointer
 *
 * Evidence: Smap_HitTestStrategSelect @ 0x82be8 (3,088 B). Two call sites, both in the geoscape
 * press dispatcher (0x87C10 and 0x87C94, the same arguments twice on one press), each passing the
 * viewport at 0x1863E8 with the cursor pair read LDRSH from 0x186BD0 and nSmapCursorY @ 0x186BD4,
 * and each masking the answer with `ANDS R3, R0, #0xFF`. The release half never calls it.
 *
 * ## What it does
 *
 *   1. 0x82C18: a click in the zoom strip is not a map click. The gate is strict --
 *      `nClickY > 320 - pStripPad->uHeight` -- so a click exactly on the strip edge still scans.
 *   2. The status panel's line count is cleared (0x82C3C) and a 16x16 window of cells around the
 *      click is walked, dx and dy over -8..7, each cell resolved by the same screen-to-cell formula
 *      Smap_GetTileAtScreen uses (0x82C84 / 0x82CAC), one wrap per axis.
 *   3. Every strateg item is tested against every cell. Before the comparison the item's stored
 *      coordinates are scaled by the zoom level: PUFO_SMAP_ITEM_SCALE_L1 at level 1 (0x82D48),
 *      PUFO_SMAP_ITEM_SCALE_L2 at level 2 (0x82DD0), unscaled at level 0 or any other mode.
 *   4. A coordinate match builds one panel line per item, and which line it is depends on the type
 *      group. The item's list index goes into anLineAux beside the text (0x8306C / 0x83230).
 *
 *      nType 0 or 3 (0x82E6C). With bActive set the line needs bVisible too, and its text is TEXTS
 *      record 1145 (0x82F9C) -- overwritten from the first character by record 1146 when the type
 *      is 3 (0x82FE4) -- with itoa(nSiteId) appended (0x8301C / 0x83028). With bActive clear and no
 *      map selection in progress the line is a base name instead: the first base whose BaseRec.bUnk4
 *      byte, loaded SIGNED (LDRSB at 0x82ED4), equals nSiteId, named from byte_186C24 + 64 * i + 54,
 *      the name byte of the per-base info record the port keeps as BaseSiteRec @ 0x186C40 -- abName
 *      at +26 is the same byte (0x186C24 + 54 == 0x186C40 + 26).
 *
 *      nType 1 or 4 (0x830B4). With bActive set, again gated on bVisible, the text is TEXTS record
 *      1147 plus itoa(nDisplayId). With bActive clear, bVisible set and no map selection in progress
 *      the line is Ship_FormatDisplayName of the first craft whose Ship.bId byte (LDRB at 0x83130,
 *      unsigned) equals nDisplayId. That search is the same walk Ship_FindIndexByStrategType does,
 *      and the original really does carry it twice in one function: inline here and as a call at
 *      0x836A0. The port uses the ported function for both.
 *
 *   5. 0x83248 / 0x83250: a coordinate match sets the "something was under the pointer" answer and
 *      re-stores the line count even when the gates above added no line. An invisible contact, or
 *      one blocked by an in-progress map selection, is therefore selectable in the dispatcher's
 *      sense while contributing nothing to the panel.
 *   6. One line: the same six (nType, bActive) branches the status panel's own click handler runs
 *      (Geoscape_OnRadarListClick @ 0x854c4), in a different order and each one first clearing the
 *      panel's nUnk3032 -- so the appended line carries 0 in colour slot 0 where the click handler
 *      puts the clicked row. The type-1 inactive branch is the odd one out: it stores the ship index
 *      into 0x187054 and 0x159E74 first, appends flag 11 with that index in colour slot 0, and does
 *      NOT drop line 0.
 *   7. More than one line: the panel becomes a multi-select box -- nPosX 120, nPosY and nRectY
 *      both 120 - (nLineCount / 2) * nFont, nRectX 20, nViewW 200, bSelLineHighlight clear -- the clock
 *      pauses, byte_18709C[4] goes up and Sound_Play(7) plays.
 *   8. The tail (0x83708..0x837BC) runs whatever the line count was. When message line 0 carries
 *      flag 27 (the pick-a-target-cell prompt Smap_ConfirmCraftTarget's chain installs), nothing
 *      was selectable, and the click is above the strip, it pauses, clears bMapSelectionFlag,
 *      writes the clicked cell into 0x186414 and 0x186418 -- the pending click
 *      Smap_ConfirmCraftTarget @ 0x842ec reads (its writer is the flag-27 tail at 0x83784 / 0x83788,
 *      ported in Smap_HitTestStrategSelect at this file:7151) -- drops line 0 and re-inserts it as flag 12 with the selected ship index in colour
 *      slot 0, and clears nUnk3032.
 *
 * Deviations:
 *   1. Both searches are bounded by their counts. When nothing matches, the original walks one slot
 *      past the block it is indexing -- byte_186C24 + 64 * nBaseCount + 54, or apCraft[nCraftCount],
 *      a stale or null pointer -- and still adds a line. The port adds the line with an empty text.
 *   2. PtrArray_Get results are null-checked; the original dereferences them.
 *   3. The flag-27 gate re-reads nSmapCursorY from the global the caller had just read LDRSH from.
 *      Nothing writes it in between, so the port tests its nClickY parameter.
 *   4. The three label strings are TEXTS records 1145, 1146 and 1147 (0xECDB5, 0xECE82, 0xECF4F --
 *      aTextRecs @ 0xB38D0 + 205 * n, renamed in the IDB from unk_* names). Nothing in the image
 *      writes them and the binary's static table holds them empty, so with no TEXTS.DAT loaded the
 *      line text is just the number. */
#define PUFO_SMAP_HIT_SCAN_HALF 8   /* the scan window is dx, dy over -8..7 */
#define PUFO_SMAP_HIT_TEXT_MAX  56  /* the original's v62 build buffer */

/* The three label prefixes, as TEXTS record numbers. Each address is aTextRecs @ 0xB38D0 plus
 * 205 * n, which is what the original's own `LDR R2, =(unk_... - 0xB38D0); ADD R1, R0, R2` pairs
 * compute (0x80690, 0x808B0, 0x807E4). Renamed in the IDB to aTextRec1145/1146/1147. */
#define PUFO_TEXT_REC_SITE_LABEL   1145 /* 0xECDB5, the nType 0 line prefix */
#define PUFO_TEXT_REC_ALIEN_LABEL  1146 /* 0xECE82, the nType 3 prefix, written over the first */
#define PUFO_TEXT_REC_CRAFT_LABEL  1147 /* 0xECF4F, the nType 1 / 4 prefix */

typedef struct SmapHitEnv {
    const PadImage *pStripPad;             /* apPad[GEOUI_PAD_186BBC] @ 0x186BBC, the zoom strip */
    UiPanel        *pStatusPanel;          /* unk_18641C, the panel the lines are built in */
    UiLineList     *pLines;                /* the root message list @ 0x158890 */
    PtrArray       *pStrategList;          /* the strateg list @ 0x186534 */
    const u8       *pTextRecs;             /* aTextRecs @ 0xB38D0, for records 1145..1147 */
    BaseRec *const *apBases;               /* Map.apBases @ Map + 0x1174 */
    const i8       *pnBaseCount;           /* nBaseCount @ 0x159D80, read SIGNED (LDRSB at 0x82EE8) */
    const BaseSiteRec *apSiteRecs;         /* byte_186C24 == 0x186C40, 64-byte records; the name is
                                            * abName at +26, the byte 0x186C24 + 54 names */
    Ship *const *apCraft;                  /* Map.apCraft @ Map + 0x10AC */
    const u8       *pnCraftCount;          /* Map_nCraftCount @ 0x114B4C, read unsigned */
    u8             *pbSelectionFlag;       /* bMapSelectionFlag @ 0x159E84, cleared by the tail */
    i32            *pnPendingPickX;        /* 0x186414, written by the flag-27 tail */
    i32            *pnPendingPickY;        /* 0x186418 */
    i32            *pnScreenId;            /* nGeoscapeScreen @ 0x186BC4 */
    i32            *pnSelectedStrategIdx;  /* 0x186BE4 */
    i32            *pnSelectedShipIdx;     /* 0x187054 */
    i32            *pnMsgShipIdx;        /* nMsgSelectedShipIdx @ 0x159E74, an apCraft index */
    UiPanel *const *ppOptPanelShips;       /* the cell of pOptPanelShips @ 0x159E18, bSelLineHighlight cleared at
                                            * 0x83494. A cell and not a copy because Open runs at 0x83480
                                            * just before: the open half just put the names panel there, the
                                            * close half just took it out, and the original's load at 0x83494
                                            * answers whichever of those left the global. */
    GameTime       *pTime;                 /* 0x159618; bPaused is the byte GameTime_Pause writes */
    u8             *pbFlag18708C;
    u8             *pbFlag187090;
    u8             *pbFlag187098;
    u8             *pbFlag1870A0;          /* byte_18709C[4] */
    i32            *pnRadarPulseTimer;     /* nRadarPulseTimer @ 0xAC224 */
    const OptionsScreenHooks *pOptionsHooks; /* the type-0 inactive branch */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void *pSoundUser;
} SmapHitEnv;

u8 Smap_HitTestStrategSelect(const SmapView *pView, const SmapHitEnv *pEnv,
                             i32 nClickX, i32 nClickY);

/* ------------------------------------------------- the base-facility screen's bring-up
 *
 * Evidence: BaseFacScreen_Init @ 0x5a198. Two callers: BaseFacScreen_Open @ 0x61868 and
 * Geoscape_OnMapClick @ 0x8B654 (the screen-2 branch with no pending confirm).
 *
 * In the original's order:
 *
 *   1. the close control (g_pUiCloseCtl) gets BaseFacScreen_CloseThunkA as its click handler FIRST
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
 *      BaseFacScreen_TryBuySelected @ 0x58910 (ported at this file:7481); it enters here as a
 *      hook because the shell supplies the thunk.
 *
 * Deviations: the original allocates with operator new and stores into fixed .bss slots; the port
 * takes caller-owned controls. The port's handler slot is `void (*)(void)`, so the panel pair
 * arrives as the env's own thunks rather than as BaseFacScreen_PanelPrev itself. The Toggle hook
 * returns nothing, so the port returns 0 where the original returns Toggle's register. */
#define PUFO_BASEFAC_PANEL_DESC_COUNT 2

typedef struct BaseFacInitEnv {
    UiControl *pCloseCtl;        /* g_pUiCloseCtl, the control that closes the screen */
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
    void (*pfnToggle)(void *pUser); /* BaseFacScreen_Toggle (0x589ec), ported at this file:7372 */
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
 * Screen 2 - the pending facility, gated on the pending region (0x159DF4) not being 0xFF:
 *
 *   - With bBaseFacConfirm (0x1870AC) set, the pending placement is committed: apBases[0] gets
 *     nUnk8 / nUnk12 = the pending coordinates (0x159D94 / 0x159D98), bRegionId = the region and
 *     bUnk16 = 1; the aBaseFacilities record gets bType = region - 0x80 (the ARM is `SUB R0,R1,#0x80`
 *     at 0x8B544; Hex-Rays renders it as `+ 0x80` and the two spellings agree for every byte value),
 *     so the stored value is the region index 0..9 -- and its abName field (the
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

    u8   bPendingRegionId;             /* 0x159DF4, the region awaiting placement */
    u8   *pbConfirm;               /* bBaseFacConfirm @ 0x1870AC */
    u8   *pbFacDone;               /* bBaseFacDone @ 0x187078 */
    i32   nPlaceX;                 /* 0x159D94 */
    i32   nPlaceY;                 /* 0x159D98 */
    BaseRec **apBases;             /* 0x114B2C */
    BaseSiteRec *apSiteRecs;       /* 0x186C40, eight 64-byte records */
    PtrArray *pStrategList;        /* strategList @ 0x186534 */
    const u8 *pTextRecs;           /* TEXTS, for the cost label */
    i8   *paRegionBuildCount;             /* g_anRegionBuildCount @ 0x18655D, indexed by region - 0x80 */
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
 *      nPosY 234, nRectX 5, nRectY 234, nViewW 230, aStyleRgb[0..2] = B4 B4 B4,
 *      aStyleRgb[3..5] = 46 96 AA, aStyleRgb[6..11] = 44 F4 FC 04 46 4B, bColorKey 0x96, bRowRules 0,
 *      bSelLineHighlight 1, nUnk7076 1.
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
 * panel->nUnk3032 (the store at 0x58998, gated by 0x5898C). Neither index is bounds-checked: an out-of-range sel reads anLineAux
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
 *      nPosX 55, nPosY 102, nRectX46, nRectY102, nViewW 167, aStyleRgb[0..2] 44 F4 FC,
 *      aStyleRgb[3..5] 32 A0 BD, aStyleRgb[6..11] C8 F4 FC 04 46 4B, bSelLineHighlight 1, bColorKey 0xFF.
 *   3. panel B @ 0x159758 (4 lines): nStyle 4, nFont 18, nPosX 190, nPosY 102, nRectX46,
 *      nRectY102, nViewW 167, aStyleRgb[0..2] 44 F4 FC, aStyleRgb[3..5] 32 32 FA,
 *      aStyleRgb[6..11] 44 F4 FC 04 46 4B, bRowRules 0. No click handler.
 *   4. n = how many of apSoldiers[0..count) have the base byte at +0xB6 equal to nSelectedBaseIdx
 *      (0x64BA4..0x64CC4; the count is the u16 at unk_115350).
 *   5. panel C @ 0x15975C (n lines): nStyle 13, nFont 18, +3020 = SoldierScreen_SelectListed,
 *      nPosX 10, nPosY 70, nRectX2, nRectY70, nViewW 234, aStyleRgb[0..2] B4 B4 B4,
 *      aStyleRgb[3..5] 32 32 FA, aStyleRgb[6..11] F0 F0 F0 14 14 14, bSelLineHighlight 1, bColorKey 0x96.
 *   6. panel D @ 0x159DEC (n lines): nStyle 13, nFont 18, nPosX 230, nPosY 70, nRectX5,
 *      nRectY70, nViewW 40, aStyleRgb[0..2] B4 B4 B4, aStyleRgb[6..11] F0 F0 F0 14 14 14,
 *      bSelLineHighlight 0, nUnk7076 2, bColorKey 0x96. No click handler.
 *   7. the row pass (0x65018..0x651A0) walks the roster again; for every soldier on the selected
 *      base, panel C gets szName (+0xBA) and anLineAux = the ROSTER index, panel D gets
 *      Soldier_FormatDisplayName's string, and the row colours come from the craft byte at +0xB7:
 *      254 gives (250,150,150 / 20,20,20) and 255 gives (150,150,250 / 20,20,20) on both panels.
 *      Any other value leaves the row colours at the ctor defaults.
 *   8. g_pUiCloseCtl's +12 handler becomes SoldierScreen_CloseThunk -- the same control slot
 *      BaseFacScreen_Init uses for the build dialog.
 *
 * Deviations: the port owns only the five heap objects; the controls stay caller-owned. The panel
 * line-count stores are guarded where the original stores through the pointers unchecked. */
typedef struct SoldierScreenEnv {
    u8      *pbOpen;              /* 0x159DE0, the screen-open byte */
    i32     *pnScreenId;          /* nGeoscapeScreen @ 0x186BC4 */
    Soldier *const *apSoldiers;   /* Map + 0x1198 */
    const i32 *pnSoldierCount;    /* unk_115350: the original reads this global as a u16; the shell
                                   * owns one i32 cell (PufoWorldState.nSoldierCount) and this env
                                   * points at that live cell. It is a live pointer, not a copy:
                                   * SoldierScreenEnv lives in the world, not in the thunk that fills
                                   * it (World_SoldierToggleThunk). */
    const i8 *pnSelectedBaseIdx;  /* nSelectedBaseIdx @ 0x159D81 */
    UiPanel **ppPanelEquip;       /* 0x159DF0, panel A */
    UiPanel **ppPanelStores;      /* 0x159758, panel B */
    UiPanel **ppPanelNames;       /* 0x15975C, panel C */
    UiPanel **ppPanelCraft;       /* 0x159DEC, panel D */
    PadImage **ppPad;             /* unk_1598D4 */
    const char *pszPadName;       /* the pad loaded at mode 8 ("soldier"); NULL skips the load */
    UiControl *pCloseCtl;         /* g_pUiCloseCtl */
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
#define PUFO_SOLDIER_EQUIP_OBJ_BASE 121 /* apObjects[121 + nUnitType]; 122 "Personal armour",
                                         * 123 "Power suit", 124 "Fly suit" (IDB
                                         * Map_apObjects122..124). 121 itself is "AVENGER", the
                                         * fifth craft item, which nUnitType 0 never reaches. */

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

/* --- SoldierScreen_Draw @ 0x65290 (716 B) : the soldier screen's pad, lists and frame ---
 *
 * Reached three times from Geoscape_Draw (0x8c180, ported in pufo_geoscape.c:6849) at 0x92DD0 / 0x92F38 / 0x93094, plus a
 * data reference from the handler table entry at 0x18A2B0.
 *
 * SCREEN 5 (0x652A4): while nGeoscapeScreen is 5 this is not a draw at all - it returns
 * SoldierScreen_DrawDetail(nSoldierCursor[0]), the soldier readout that SoldierScreen_SelectListed
 * (0x64a9c) arms by putting 5 into the screen id. The port keeps it behind pfnTextInput because the
 * readout needs the full SoldierDetailEnv (font, pads, colour env, keyboard controls) that this
 * screen's own env does not carry; the caller that has those assembles the env and calls it directly.
 *
 * CLOSED (0x652CC): while the screen-open byte at 0x159DE0 is zero nothing is drawn.
 *
 * OPEN (0x652D4..0x65538): the "soldier" pad, then the two list panels through UiPanelMsg_Draw, then
 * the frame in three parts:
 *   - the header bevel (0x65318..0x65390): a 98x22 box whose top edge (97x1 at 69,23) and right edge
 *     (1x22 at 166,23) use index 0xB4 and whose bottom edge (97x1 at 69,44) and left edge (1x21 at
 *     69,24) use index 0x64;
 *   - the two side rails (0x6539C..0x6546C): 24 steps, at x 2 and x 235. Each step draws a 1x8 at
 *     y 5 + 7*k with index 5*(k + 20) - 100, 105, ... 215 - and a 1x6 at y 174 + 6*k with index
 *     0xE6 - 5*k - 230, 225, ... 115 - so the ramp climbs down the upper half and falls down the lower.
 *     The climbing blocks are 8 tall on a 7 step, so each one repaints the previous block's last row;
 *     the falling blocks are 6 on a 6 and never overlap. The two halves meet cleanly at 173 / 174.
 *   - the top and bottom rails (0x6547C..0x65538): 24 steps of 5. Each draws a 5x1 at x 1 + 5*k and a
 *     5x1 at x 119 + 5*k, on y 5 and on y 315, with 5*(k + 20) on the left half and 0xFA - 5*k
 *     (250, 245, ... 135) on the right. The halves meet at x 119..120, where the climbing block of the
 *     last step is drawn after the falling block of the first one and wins.
 *
 * Hex-Rays renders the two descending indices as -26 - 5*k and -6 - 5*k. They are not negative values:
 * the ARM computes 0xE6 - 5*k (0x653E8 `RSB R5, R3, #0xE6`) and 0xFA - 5*k (0x654E0 `RSB R4, R3, #0xFA`)
 * and stores all three colour arguments with STRB into the unsigned __int8 slots SoftSurf_BlitRectProxy
 * reads, so the byte is what matters. The port writes the ARM's own expressions.
 *
 * Deviations: the original reaches the pad, both panels and the cursor through the fixed context block
 * at 0x159768 (the pad is *(ctx + 364) = 0x1598D4); the port takes them from the env. The original
 * returns its context pointer and Geoscape_Draw discards it; the port returns the dialog hook's value on
 * the screen-5 path and 0 elsewhere. The port null-guards the pad and both panels, where the original
 * dereferences whatever SoldierScreen_Toggle left there. */
#define PUFO_SOL_SCREEN_TEXT_INPUT 5 /* the screen id that turns this draw into the name-edit dialog */
#define PUFO_SOL_BEVEL_LIGHT 0xB4    /* the header box's top and right edges */
#define PUFO_SOL_BEVEL_DARK  0x64    /*  and its bottom and left edges */
#define PUFO_SOL_RAMP_BASE   20      /* the climbing ramp is 5 * (k + 20), so it starts at 100 */
#define PUFO_SOL_RAMP_STEP   5       /* and gains 5 per step, in both ramp directions */
#define PUFO_SOL_RAMP_SIDE   0xE6    /* the ARM's RSB #0xE6: the descending side-ramp index */
#define PUFO_SOL_RAMP_EDGE   0xFA    /* the ARM's RSB #0xFA: the descending top/bottom ramp index */

typedef struct SoldierDrawEnv {
    i32      *pnScreenId;   /* nGeoscapeScreen @ 0x186BC4 */
    u8       *pbOpen;       /* 0x159DE0, the screen-open byte */
    i32      *pnCursor;     /* nSoldierCursor @ 0x159E08 */
    PadImage *pPad;         /* *(ctx + 364) = 0x1598D4, the "soldier" pad */
    UiPanel  *pPanelNames;  /* pSolPanelNames @ 0x15975C */
    UiPanel  *pPanelCraft;  /* pSolPanelCraft @ 0x159DEC */
    const GfxColorEnv *pColor; /* the pair both panels' UiPanelMsg_Draw (0x652E0, 0x652EC) packs */
    i32 (*pfnTextInput)(i32 nCursor, void *pUser); /* the SoldierScreen_DrawDetail (0x632cc) seam */
    void *pTextInputUser;
} SoldierDrawEnv;

i32 SoldierScreen_Draw(const SoldierDrawEnv *pEnv);

/* --- SoldierScreen_DrawEquipPanels @ 0x62d78 (960 B) : the soldier screen's transfer panels
 *
 * Reached once, from SoldierScreen_DrawDetail (0x6468C) behind bTransferFlag @ 0x159E10. It fills the
 * equip panel (pSolPanelEquip @ 0x159DF0) and the stores panel (pSolPanelStores @ 0x159758) and draws
 * both under a three-rect frame. The IDB called it Ui_DrawTextInputDialog until the body was read: it
 * has nothing to do with text input.
 *
 * Row count (0x62D8C..0x62E28). The ARM keeps a row cursor in R4 starting at 1 and a global row count
 * nEquipRowCount @ 0xab710 (initialised to 1 in .data, right before the keyboard's own "Accept" /
 * "BackSpace" literals) that it resets to 1 at 0x62DB8. For each of apObjects[122], [123], [124] --
 * the three armour objects the original holds as globals at 0x113CA8, 0x113CAC and 0x113CB0 -- it
 * reads auPerBaseVal[nSelectedBaseIdx] (ObjectRecFull + 0x2A, 2*base) and, when that is non-zero,
 * records the stock in a global (0x159D84, 0x159D88, 0x159D8C) and raises the cursor. The count is
 * only ever assigned from the cursor and the cursor only ever moves on a non-zero stock, so the two
 * end up equal: rows = 1 + (how many of the three the base actually stocks). All four globals are
 * private to this function -- no other function in the IDB reads them -- so the port keeps locals.
 *
 * Rows (0x62E2C..0x6302C). Row 0 is always present: the equip panel gets TEXTS[93] "None" and the
 * stores panel gets the address 0x10DA38, which is aTextRecs + 205*1800 -- past the 1650 records
 * TEXTS.DAT fills, so in the shipped build it is the zero-filled .data and the label is an empty
 * string. anLineAux[0] = 0. Rows 1..3 follow in the same order as the stock pass, each with the armour
 * name (TEXTS[94] "Personal Armour", [95] "Power Armour", [96] "Fly armour") on the equip side,
 * j_itoa(stock) on the stores side, and anLineAux = 1, 2, 3 -- the values SoldierScreen_EquipSelected
 * (0x619cc) switches on. The stock capture tests the quantity with != 0 and the row emission with > 0,
 * so a negative quantity is recorded but never drawn; the port keeps both tests.
 *
 * Frame and draw (0x63040..0x63118). Both panels get nLineCount = the row count, then three nested
 * rects -- (45,75) 170 x (18*rows+29) in 0x6E/0x77, (46,76) 168 x (18*rows+27) in 0x30/0xE0/0xE8,
 * (47,77) 166 x (18*rows+25) in 0x6E/0x77 -- where the height multiplies the SIGNED BYTE of the row
 * count (the ARM's LDRSB), and then UiPanelMsg_Draw on the equip panel and on the stores panel.
 *
 * Deviations: the original reaches the panels, the base index and the three objects through fixed
 * addresses; the port takes them from the env. The original copies every label with an unbounded
 * LDRSB/STRB loop into the 50-byte line buffers; the port goes through UiPanel_SetLineText, which bounds
 * the copy -- and because that helper also bounds by nLineCount, the port stores nLineCount before the
 * rows instead of after them. Nothing reads either panel in between, so the frame and the row set come
 * out the same. The stock capture's != 0 and the row emission's > 0 can only differ for a negative
 * quantity, and auPerBaseVal is a u16, so in the shipped data they always agree. */
#define PUFO_SOLPAN_OBJ_FIRST 122 /* the three armour objects, IDB Map_apObjects122..124 */
#define PUFO_SOLPAN_OBJ_LAST  124
#define PUFO_SOLPAN_FRAME_X   45  /* the outer of the three nested frame rects */
#define PUFO_SOLPAN_FRAME_Y   75
#define PUFO_SOLPAN_FRAME_W   170
#define PUFO_SOLPAN_ROW_H     18  /* each row adds 18 to the frame height, over 29 / 27 / 25 */

typedef struct SolEquipPanelEnv {
    ObjectRecFull *const *apObjects; /* Map + 0x108 */
    const i8 *pnSelectedBaseIdx;     /* nSelectedBaseIdx @ 0x159D81 */
    UiPanel *pPanelEquip;            /* pSolPanelEquip @ 0x159DF0 */
    UiPanel *pPanelStores;           /* pSolPanelStores @ 0x159758 */
    const char *pTextRecs;           /* TEXTS.DAT records, PUFO_TEXT_REC_STRIDE apart */
    const GfxColorEnv *pColor;       /* the pair the two UiPanelMsg_Draw calls pack (0x63108, 0x63118) */
} SolEquipPanelEnv;

i32 SoldierScreen_DrawEquipPanels(const SolEquipPanelEnv *pEnv);

/* --- SoldierScreen_DrawDetail @ 0x632cc (6096 B) : the selected soldier's read-out, the transfer
 * panels and the name-entry keyboard
 *
 * The IDB called this Ui_TextInputDialog. That name fits only the last 1,100 bytes: the body is the
 * whole soldier detail view. SoldierScreen_Draw (0x652B0) calls it while nGeoscapeScreen is 5, the id
 * SoldierScreen_SelectListed (0x64a9c) puts there, with nSoldierCursor @ 0x159E08 as its argument. It
 * stores that argument into unk_159760 (0x632F4), which is the copy SoldierScreen_CopyNameToEdit
 * (0x6318c), SoldierScreen_AppendNameGlyph (0x63208) and Map_BackspaceSelectedName (0x63138) read.
 *
 * Soldier = apSoldiers[nCursor] (0x63448: Map + 0x1198 + 4*arg). Every field below is that soldier's.
 *
 * 1. Header frame (0x632E0..0x63404). 24 steps of 5 draw a 5x1 at x = 3+5k and another at x = 115+5k,
 *    on y 5 and on y 31, with 5*(k+20) (100..215) on the left half and the byte 0xFA-5*k (250..135) on
 *    the right; then a 1x27 at (3,5) and at (235,5) in 0x96. Hex-Rays shows the descending half as
 *    -6-5*k: the ARM computes 0xFA-5*k and stores it through STRB, so the byte is what matters.
 *
 * 2. Name (0x63428..0x63628). Colour pair (240,240,100)/(30,30,30); the rank icon is the basebits
 *    glyph bRankType + fontSys_nBaseBitsSprBase + 42 at (5,7); the name (szName @ +0xBA) is centred at
 *    (160 - w/2, 10). Before the name, and only when uMissions @ +0xB8 is above 3, the two class
 *    insignia blit: cs0/cs1/cs2 are the three PadImages Assets_LoadStartup allocates at 0x186C30,
 *    0x186C34 and 0x186C38 and loads from the archive members "cs0", "cs1", "cs2" (0x387F4..0x38898,
 *    right after "class.pal" into palette slot 0x12). The ARM sets pad->nOriginX = 33, pad->nOriginY = 2,
 *    blits, adds 34 to nOriginX and blits again, so the two slots are x 33 and x 67 at y 2. Which pad
 *    goes in which slot is Soldier.nClass @ +0x1AC (0x63490..0x63614): 0 -> cs0/cs0, 1 -> cs1/cs1,
 *    2 -> cs2/cs2, 3 -> cs0/cs1, 4 -> cs1/cs2, 5 -> cs0/cs2, anything else draws nothing. The pads are
 *    left with nOriginX = 67 behind.
 *
 * 3. Armour (0x63668..0x63720). Cyan pair; TEXTS[90] "Armour" centred at (104 - w/2, 38). The value is
 *    TEXTS[93 + nUnitType] -- "None", "Personal Armour", "Power Armour", "Fly armour" -- centred at
 *    (188 - w/2, 38), and only for nUnitType 0..3: the ARM's switch has no default arm, so a unit type
 *    of 4 or more (every alien type) draws no armour value at all.
 *
 * 4. Rank / craft / record (0x63724..0x63b90). A 10-step ramp first (0x6374C..0x63808): 4x1 at x =
 *    143+5k and at x = 188+5k, on y 37 and on y 55, in 8*k+100 (100..172) and the byte 0xAC-8*k
 *    (172..108; Hex-Rays' -76-8*k). Then cyan labels: TEXTS[123] "Rank:" and TEXTS[124] "Craft:"
 *    centred at x 35 on y 61 and 75, TEXTS[125] "Missions:" and TEXTS[126] "Kills:" centred at x 170 on
 *    the same two lines. When bUnk183 == 254 -- the value Base_TransferTacticalLoot @ 0x68344 leaves on
 *    a craft's crew when it credits them, together with the days in bUnk247 @ +0xF7 (40*nUnk436/100,
 *    0x68610, and Geoscape's daily tick counts it down) -- TEXTS[127]
 *    "Wound Recovery" is centred at (120 - w/2, 89) and the day count drawn at (207, 89) in the
 *    (240,240,240) pair. Then the white pair: the rank name aszTextE[20*bRankType] at (63, 61); the
 *    craft at (63, 75) as TEXTS[137]/[138]/[139] ("Skyranger-", "Lightning-", "Avenger-") with
 *    itoa(apCraft[bUnk183]->bType) appended, or TEXTS[93] "None" when bUnk183 >= 0xFE; then
 *    itoa(uMissions) at (207, 61) and itoa(uKills @ +0xFA) at (207, 75).
 *
 * 5. Stat bars (0x63bb0..0x64214). Nine rows, each: its own colour pair, the label at (10, y), the
 *    value itoa(auStat[k]) at (210, y+4), and a bar of two rects -- (9, y+16) (val>>1)+2 x 8 and
 *    (10, y+17) (val>>1) x 6. The rows and their auStat slots are Time [0] at y 110, Stamina [9] at 132,
 *    Bravery [2] at 154, Reaction [3] at 176, Firing Accuracy [4] at 198, Throwing Accuracy [5] at 220,
 *    Strength [8] at 242, and -- only when bPsiActive @ +0x1A6 is set -- Psi Strength [7] at 264 and Psi
 *    Skill [6] at 286. auStat[1] is never shown here.
 *
 * 6. Panel frame (0x64420..0x64664). A colour pair is set at 0x64420 and no text follows it before the
 *    next one, so it changes nothing visible; the port still makes the call because it rewrites the
 *    palette. Then three ramp groups: 24 steps of 5 at y 61 and y 108 (5x1, x 3+5k and 115+5k), 9 rows
 *    at y 134+22k each with 24 steps of 5 (5x1, no bottom edge), and 24 steps of 6 at x 5 and x 232
 *    (1x6, y 58+6k and 174+6k). The rising colour is 5*(k+20), the falling one the byte 0xFA-5*k or
 *    0xE6-5*k.
 *
 * 7. Transfer panels (0x64678): SoldierScreen_DrawEquipPanels, behind bTransferFlag.
 *
 * 8. Name keyboard (0x64694..0x64a78), behind byte_186B88 -- the flag SoldierScreen_CopyNameToEdit
 *    (0x6318c) arms. Ui_AnimExpandBlit over the geoscape anim root 0x1864F8 at (0,70) 236x250, index 4,
 *    source 0x46/0x4B, key 0xC8, mode 3; while it is still expanding it returns 0 and the whole
 *    keyboard, including the return value, stays at 0. Once it finishes: the two pads blit (unk_186BA8
 *    is the left one -- Geoscape_OnPointer gives it Map_BackspaceSelectedName as its click handler at
 *    0x88244 -- and unk_186B9C the right one, which gets SoldierScreen_CopyNameToEdit at 0x8824C), then
 *    the red pair (255,55,55) and "Windows-1251 encode" centred at (120, 65), then the cyan pair and
 *    "BackSpace" at (60 - w/2, 301) and "Accept" at (180 - w/2, 301). Those three literals are plain
 *    .data strings at 0xab728, 0xab71c and 0xab714, not TEXTS records. A one-cell box then frames the
 *    space key at (12, 80): two horizontals of measure(" ") on y 80 and 93, a 1x13 left edge and a
 *    1x14 right edge, all in index 0xFA -- in 0/0 when the selected character (unk_159DE8) is a space
 *    AND 0x1864F8+0x35 is set, otherwise in 0x14/0x14. Finally the grid: 16 rows of 13 on y 80..279 and
 *    16 columns of 14 on x 12..226, the character code counting up from 32, so the 256 cells cover
 *    32..255 and then wrap through (char) to 0..31. A cell whose character measures zero gets a grey
 *    (150,150,120) '-' first, then its own character in (250,250,20) -- which draws nothing -- and the
 *    selected character, when 0x1864F8+0x35 is set, in (250,0,0) instead.
 *
 * Return: 0 when the keyboard is not armed or is still expanding, otherwise whatever the last
 * Text_DrawString returned. SoldierScreen_Draw hands that straight back to Geoscape_Draw.
 *
 * Deviations: the original reaches the soldier array, the craft array, the pads, the font and every
 * flag through fixed addresses and never bounds apSoldiers[nCursor] or apCraft[bUnk183]; the port takes
 * them from the env and returns 0 for a null env or a soldier it cannot resolve. The original builds
 * the craft string in a 140-byte stack buffer that it only ever initialises through the switch, so a
 * soldier aboard a craft whose bCraftType is 3 or 4 -- the two the game names Interceptor and Fire
 * Storm (TEXTS[418], TEXTS[419]) -- prints that uninitialised buffer; the port writes "None" instead
 * and the difference is in the ledger. The original's itoa scratch buffer is the global unk_159DD0; the
 * port uses locals. */
#define PUFO_SOLDET_ICON_BASE   42  /* the rank icon is basebits glyph bRankType + nBaseBitsBase + 42 */
#define PUFO_SOLDET_CLASS_X0    33  /* the two class-insignia slots, 33 then 33+34 */
#define PUFO_SOLDET_CLASS_X1    67
#define PUFO_SOLDET_CLASS_Y      2
#define PUFO_SOLDET_CLASS_MAX     5  /* nClass above this draws no insignia at all */
#define PUFO_SOLDET_MISSION_GATE  3  /* the insignia pair needs uMissions strictly above this */
#define PUFO_SOLDET_BAR_SHIFT     1  /* every bar is (value >> 1) wide, the frame one wider */
#define PUFO_SOLDET_KB_ROWS      16  /* 16 rows of 13 and 16 columns of 14, 256 cells from char 32 */
#define PUFO_SOLDET_KB_COLS      16
#define PUFO_SOLDET_KB_ROW_H     13
#define PUFO_SOLDET_KB_COL_W     14
#define PUFO_SOLDET_KB_X0        12
#define PUFO_SOLDET_KB_Y0        80
#define PUFO_SOLDET_KB_FIRST    32  /* the first character code the grid draws */
#define PUFO_SOLDET_STAT_ROWS     9 /* 7 stat rows always, plus Psi Strength and Psi Skill under bPsiActive */
#define PUFO_SOLDET_BAR_LABEL_X  10 /* every stat label is left-aligned here, every value at x 210 */
#define PUFO_SOLDET_BAR_VALUE_X 210
#define PUFO_SOLDET_BAR_X0        9 /* the bar frame rect, and the fill one pixel in from it */
/* The keyboard's three captions and its two markers are plain .data literals in the original, not
 * TEXTS records, so the Russian build shows them in English exactly as the English one does. */
#define PUFO_SOLDET_KB_ENCODE    "Windows-1251 encode" /* aWindows1251Enc @ 0xab728 */
#define PUFO_SOLDET_KB_BACKSPACE "BackSpace"           /* aBackspace @ 0xab71c */
#define PUFO_SOLDET_KB_ACCEPT    "Accept"              /* aAccept @ 0xab714 */
#define PUFO_SOLDET_KB_DASH      "-"                   /* asc_AB340 @ 0xab340, the unprintable-cell marker */
#define PUFO_SOLDET_KB_SPACE     " "                   /* asc_AADB8 @ 0xaadb8, the width the key box measures */

/* The TEXTS.DAT records this draw labels itself with, by record number. The English values are the
 * shipped main.dat copy; lang.dat carries the Russian one and wins the lookup at run time. */
typedef enum SoldierDetailText {
    PUFO_SOLDET_TEXT_ARMOUR    = 90,  /* "Armour" */
    PUFO_SOLDET_TEXT_NONE      = 93,  /* "None" -- also the equip panel's row 0 */
    PUFO_SOLDET_TEXT_ARMOUR_1  = 94,  /* "Personal Armour" */
    PUFO_SOLDET_TEXT_ARMOUR_2  = 95,  /* "Power Armour" */
    PUFO_SOLDET_TEXT_ARMOUR_3  = 96,  /* "Fly armour" */
    PUFO_SOLDET_TEXT_RANK      = 123, /* "Rank:" */
    PUFO_SOLDET_TEXT_CRAFT     = 124, /* "Craft:" */
    PUFO_SOLDET_TEXT_MISSIONS  = 125, /* "Missions:" */
    PUFO_SOLDET_TEXT_KILLS     = 126, /* "Kills:" */
    PUFO_SOLDET_TEXT_WOUND     = 127, /* "Wound Recovery" */
    PUFO_SOLDET_TEXT_TIME      = 128, /* "Time"        -> auStat[0] */
    PUFO_SOLDET_TEXT_STAMINA   = 129, /* "Stamina"     -> auStat[9] */
    PUFO_SOLDET_TEXT_BRAVERY   = 130, /* "Bravery"     -> auStat[2] */
    PUFO_SOLDET_TEXT_REACTION  = 131, /* "Reaction"    -> auStat[3] */
    PUFO_SOLDET_TEXT_FIRING    = 132, /* "Firing Accuracy"   -> auStat[4] */
    PUFO_SOLDET_TEXT_THROWING  = 133, /* "Throwing Accuracy" -> auStat[5] */
    PUFO_SOLDET_TEXT_STRENGTH  = 134, /* "Strength"    -> auStat[8] */
    PUFO_SOLDET_TEXT_PSI_STR   = 135, /* "Psi Strength" -> auStat[7] */
    PUFO_SOLDET_TEXT_PSI_SKL   = 136, /* "Psi Skill"    -> auStat[6] */
    PUFO_SOLDET_TEXT_CRAFT_0   = 137, /* "Skyranger-"  for Ship.bCraftType 0 */
    PUFO_SOLDET_TEXT_CRAFT_1   = 138, /* "Lightning-"  for 1 */
    PUFO_SOLDET_TEXT_CRAFT_2   = 139  /* "Avenger-"    for 2 */
} SoldierDetailText;

typedef struct SoldierDetailEnv {
    Soldier *const *apSoldiers;   /* apSoldiers @ Map + 0x1198 */
    const i32 *pnSoldierCount;    /* nSoldierCount @ 0x115350: the original reads the global as a u16;
                                   * the shell keeps one i32 for it (as in SoldierEquipEnv), and every
                                   * read here is a loop bound, so the two are the same count */
    Ship *const *apCraft;         /* Map + 0x10AC */
    i32 nCraftCount;              /* Map_nCraftCount @ 0x114B4C, the bound the original never applies */
    const char *pTextRecs;        /* TEXTS.DAT records, PUFO_TEXT_REC_STRIDE apart */
    const u8 *aszRankNames;       /* aszTextE @ 0xb2200, PUFO_TXT_E_COUNT x PUFO_TXT_E_STRIDE */
    PadImage *const *apClassPads; /* cs0/cs1/cs2 @ 0x186C30, 0x186C34, 0x186C38 */
    const GfxColorEnv *pColor;    /* the pair Gfx_PackRgb565Pair writes */
    UiAnimBlitEnv *pAnim;         /* the geoscape anim root @ 0x1864F8 */
    UiControl *pCtlBackspace;     /* unk_186BA8, the keyboard's left pad */
    UiControl *pCtlAccept;        /* unk_186B9C, its right pad */
    UiControlBlitFn pfnCtlBlit;
    UiControlBlitFn pfnCtlBlitEx;
    void *pCtlUser;
    const u8 *pbPreviewOk;        /* 0x1864F8 + 0x35, the byte the keyboard highlights on */
    const u8 *pbNameEditActive;   /* byte_186B88, armed by SoldierScreen_CopyNameToEdit */
    const u8 *pnNameEditChar;     /* unk_159DE8, the character the grid highlights */
    i32 *pnCursorCopy;            /* unk_159760, where this function stores its argument */
    const u8 *pbTransferFlag;     /* bTransferFlag @ 0x159E10 */
    SolEquipPanelEnv EquipPanels; /* the transfer panels behind that flag */
} SoldierDetailEnv;

i32 SoldierScreen_DrawDetail(const SoldierDetailEnv *pEnv, i32 nCursor);

/* Soldier name keyboard input @ 0x63138 / 0x6318c / 0x63208. The original resolves the soldier
 * through unk_159760, then resolves the roster row separately through nSoldierCursor. The caller
 * supplies those same live values for the Win64 input path. pfnMeasureWidth is required and is
 * Text_MeasureWidth in the game; tests may supply a deterministic width. */
typedef struct SoldierNameEditEnv {
    Soldier *pSoldier;             /* apSoldiers[nSoldierDetailCursor] via unk_159760 @ 0x159760 */
    char *pszEditLine;             /* pSolPanelNames->apLines[nSoldierCursor] @ 0x159E08 */
    u8 *pbNameEditActive;          /* bNameEditActive @ 0x186B88 */
    u8 *pnNameEditChar;            /* low byte of nNameEditChar @ 0x159DE8 */
    i32 (*pfnMeasureWidth)(const char *psz);
} SoldierNameEditEnv;

void Map_BackspaceSelectedName(const SoldierNameEditEnv *pEnv);
void SoldierScreen_CopyNameToEdit(const SoldierNameEditEnv *pEnv);
void SoldierScreen_AppendNameGlyph(const SoldierNameEditEnv *pEnv, i32 nX, i32 nY);

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
 *    the pending region, Base_CreateNamedEntry runs at the pending coordinates,
 *    aNewBaseCostByRegion[region - 0x80] comes off nFunds, BaseFacScreen_Toggle closes the dialog,
 *    and bFacPreviewActive / nGeoscapeScreen = 1 / new-base mode / bFacPreviewOk are cleared.
 *
 * Deviations: the port returns 1 when it committed and 0 otherwise where the original returns
 * register residue; it bounds the panel selection, the panel index and the pending region where the
 * original indexes anLineAux, aBaseFacFlags and aNewBaseCostByRegion unchecked; and it scores only a
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
    u8 nPendingRegionId;                /* byte_159DF4, the pending region id */
    const i32 *paNewBaseCost;       /* g_anNewBaseCostByRegion, indexed by nPendingRegionId - 0x80 */
    u8 nPhaseByte;                  /* Map + 0x37, the cycling outline colour */
    u32 *pdwShortfall;              /* byte_1870C8, written as a dword */
    i32 *pnResidue;                 /* dword_1870B4[0] */
    i32 *pnScreenId;                /* nGeoscapeScreen @ 0x186BC4 */
    const u8 *pTextRecs;            /* the TEXTS blob, for the new-base label */
    char *pszLabel;                 /* szRadarMsg @ 0x186B10 */
    size_t cchLabel;
    i8 *paRegionBuildCount;                /* g_anRegionBuildCount @ 0x18655D, indexed by region - 0x80 */
    const BaseCreateEnv *pCreateEnv;/* Base_CreateNamedEntry's env */
    const BaseFacToggleEnv *pToggleEnv; /* the closing BaseFacScreen_Toggle */
    i32 nPlaceX;                    /* unk_159D94 */
    i32 nPlaceY;                    /* unk_159D98 */
} FacOverlayEnv;

i32 BaseFacScreen_DrawPlacementOverlay(const FacOverlayEnv *pEnv);

/* --- BaseFacScreen_DrawBuildDialog @ 0x59b60 (1592 B) : the build dialog's whole frame ---
 *
 * Nothing is drawn while bBaseFacDialogOpen (0x158884) is zero (0x59B6C). The two branches then
 * split on bBaseFacPlaceArmed (0x158888, 0x59B84), the byte BaseFacScreen_TryBuySelected sets when
 * a purchase is affordable.
 *
 * Armed (0x59B84..0x59EF8). The funds line goes first: the label unk_BB5BC, the funds value and a
 * "$" (asc_AB1D8), drawn at (5, 20) in the blue pair 130,130,250 / 20,20,90 (0x59BAC..0x59C1C). The
 * panel frame is Ui_AnimExpandBlit(0, 228, 236, 85, idx 4, src 0x46/0x4B, key 0xC8, mode 0)
 * (0x59C60). In the green pair 50,255,50 / 0,20,0 the selected record's name is measured and drawn
 * centred at y = 236 (0x59C80..0x59D0C), then the three row labels unk_B7B47, unk_B7C14 and
 * unk_B7CE1 at (6, 257), (6, 275), (6, 293). In the blue pair 50,150,255 / 0,0,20 the record's
 * uCost goes at (185, 257), the literal "50000" (a50000 @ 0xAB260) at (185, 293), and
 * "<days> <unk_BB422>" — days being aFacBuildSpec[record.bFlag21].nBuildDays — at (185, 275)
 * (0x59D8C..0x59E78). The title unk_B7DAE is drawn at (6, 4) in 68,244,252 / 4,70,75 (0x59EA4), the
 * dialog's pad control (*g_pUiCloseCtl) blits (0x59ECC), the red pair 220,80,80 / 50,30,30 is set up
 * (0x59EF4) and BaseFacScreen_DrawPlacementOverlay runs (0x59EF8).
 *
 * Not armed (0x59F98..0x5A118). bBaseFacOverlayShown (0x15888C) is cleared (0x59FAC), the same panel
 * frame runs (0x59FE0), the panel's own rows draw through UiPanelMsg_Draw (0x59FEC), the title is
 * unk_ED01C at (6, 4) in the cyan pair (0x5A00C), the funds line repeats in the blue pair
 * (0x5A078..0x5A0E4), the pad control blits (0x5A0F0) and the red pair is set up (0x5A118).
 *
 * Shared tail (0x5A138..0x5A188). When the shortfall dword byte_1870C8 is 1 and dword_1870B4[0] is
 * 0 — the state an unaffordable purchase leaves behind in BaseFacScreen_TryBuySelected and in the
 * overlay's commit — a 200x70 panel animates at (20, 130) through Ui_AnimExpandBlit_IfEnabled (its
 * first argument is the grid pointer, never read) and the shortfall message unk_C7DEE is drawn
 * centred at y = 158.
 *
 * Centring is `120 - w/2` with the ARM's negative fix-up (MOVS / ADDMI / ASR#1 at 0x59CEC and
 * 0x5A168): a negative width gets +1 before the arithmetic shift.
 *
 * Deviations: the original builds every string in a 56-byte stack buffer (psz at sp+0x18) with
 * unbounded copy loops and three strcats, so a long label overflows the frame; the port bounds each
 * build to the same 56 bytes (bugs.tsv). It indexes anLineAux[nBaseFacPanelSel], aBaseFacFlags[kind]
 * and aFacBuildSpec[bFlag21] unchecked; the port skips the record rows when any of them is out of
 * range. UiControl_BlitActivePad takes one argument in the original and calls PadImage_Blit /
 * PadImage_BlitEx(pPad, 0) itself; the port keeps its existing injected-blit seam. The original
 * returns register residue (the last callee's value); the port returns 1 when it drew the dialog and
 * 0 when the dialog is closed. The eight unk_0xBxxxx/0xCxxxx/0xEDxxx labels are runtime-loaded
 * buffers the original reads directly; the port takes them through the env. */
#define PUFO_BUILD_LABEL_MAX 56 /* psz @ sp+0x18 in the original */

typedef struct BuildDialogEnv {
    const GfxColorEnv *pColor;      /* pGX @ 0x1139B8, the colour-pair owner */
    UiAnimBlitEnv *pAnim;           /* the geoscape anim root @ 0x1864F8 */
    UiPanel *pPanel;                /* pBaseFacPanel @ 0x15886C */
    i32 nPanelSel;                  /* nBaseFacPanelSel @ 0x158868 */
    const BaseFacFlag *paFac;       /* aBaseFacFlags @ 0x186E48 */
    i32 *pnFunds;                   /* nFunds @ 0x1598D8 */
    UiControl *pCtl;                /* *g_pUiCloseCtl, the dialog's pad control */
    UiControlBlitFn pfnCtlBlit;     /* PadImage_Blit */
    UiControlBlitFn pfnCtlBlitEx;   /* PadImage_BlitEx(pPad, 0) */
    void *pCtlUser;
    u8 *pbDialogOpen;               /* bBaseFacDialogOpen @ 0x158884 */
    u8 *pbPlaceArmed;               /* bBaseFacPlaceArmed @ 0x158888 */
    u8 *pbOverlayShown;             /* bBaseFacOverlayShown @ 0x15888C */
    const u32 *pdwShortfall;        /* byte_1870C8, read as a dword */
    const i32 *pnResidue;           /* dword_1870B4[0] */
    const char *pszFundsPrefix;     /* unk_BB5BC */
    const char *pszRowCost;         /* unk_B7B47 */
    const char *pszRowBuild;        /* unk_B7C14 */
    const char *pszRowNewBase;      /* unk_B7CE1 */
    const char *pszDaysSuffix;      /* unk_BB422 */
    const char *pszTitleArmed;      /* unk_B7DAE */
    const char *pszTitleList;       /* unk_ED01C */
    const char *pszShortfall;       /* unk_C7DEE */
    const FacOverlayEnv *pOverlayEnv; /* BaseFacScreen_DrawPlacementOverlay's env */
} BuildDialogEnv;

i32 BaseFacScreen_DrawBuildDialog(const BuildDialogEnv *pEnv);

/* --- BaseFacScreen_DrawNameGrid @ 0x61c8c (1,840 B) : the base-facility screen's 6x6 cell grid ---
 *
 * Reached twice from Geoscape_Draw (0x922B4 and 0x92D34), the two states of the build overlay. It
 * draws nothing but glyphs and changes no state.
 *
 * The backdrop (0x61CA4..0x61CC8). 36 blank sprites: for row 0..5 and column 0..5,
 * Font_DrawGlyph(32*column + nSmapOriginX, 36 + 32*row + nSmapOriginY, nBaseBitsSprBase).
 *
 * The name pass (0x61CEC..0x620E8) is a `do { } while (v27 < 2)` -- TWO passes over the same 6x6 cells,
 * and the layer byte address is built from v27: 0x61D14 `ADD R3,R2,R3,LSL#2` (R2 = v27, R3 = base) then
 * two `MUL R2,R3,#6` around `+ row` and `+ 0x3E`, giving grid + 372 + 144*base + 36*v27 + 6*row +
 * column. So pass 0 reads fac[nBase].tileA (+0, the stamp layer) and pass 1 reads fac[nBase].tileB
 * (+36); layer2 (+72) is NOT a name pass at all -- its only reader here is the craft pass (0x621B8).
 * A zero layer byte skips the cell (0x61D58). buildDays[nBase][row][column] (grid + 36*base + 6*row +
 * column) then picks the sprite: a finished cell, or any cell in pass 1, gets the NAME sprite
 * nBaseBitsSprBase + <layer byte>, while an unfinished cell in pass 0 gets the blank sprite instead
 * (0x61D74 / 0x61D84). The two layers name the two sprite families: the tileA stamp is 1/2/3 or
 * 9..12, and the tileB byte is the aFacBuildSpec tileB stamp, 17..32 for the sixteen named facility
 * kinds, so pass 1 is the pass that draws a facility's own name glyph.
 *
 * A cell with build days left then consults fac[nBase].tileA (0x61E00) -- the tile stamp, 1/2/3 for
 * the single-cell facilities of the three stamp groups and 9,10,11,12 for the hangar's four quadrants
 * (BaseFac_SetCellBuildState writes those four) -- and draws the "under construction" sprite
 * nBaseBitsSprBase + 4, 5, 6, 13, 14, 15 or 16. Stamps 9, 10 and 11 stop there (goto LABEL_31);
 * stamp 12 packs (150,150,255 / 0,0,255) and draws the remaining days centred at
 * (x - w/2, y - 8), and stamps 1, 2 and 3 pack the same pair and draw it at (x - w/2 + 16, y + 7).
 * Any other stamp draws nothing further. The width comes from Text_MeasureWidth and the number is
 * itoa'd TWICE (0x62054 and 0x62074, and 0x61E94 and 0x61EB4) into the same 4-byte scratch at
 * sp+0xC -- exactly enough for "255".
 *
 * The craft pass (0x620F0..0x623B0). For row 0..5 and column 0..5, at
 * x = 32*column - 16 + nSmapOriginX and y = 19 + 32*row + nSmapOriginY, it first requires
 * fac[nBase].tileA[row][column] == 12 -- the hangar's bottom-right quadrant (0x62138) -- and then
 * walks every craft (Map_nCraftCount read as a byte at 0x6214C, `BLS` skips zero) whose Ship.bHangarX
 * is the column, Ship.bHangarY the row, Ship.bAtBase non-zero and Ship.bHomeBase the base. The
 * sprite id is fac[nBase].layer2[row][column] + nBaseBitsSprBase -- 33..37, the five craft glyphs --
 * and the layer2 value also picks the vertical nudge: 33 and 35 draw one pixel HIGH, 34 one pixel
 * LOW, 36 unshifted and 37 two pixels HIGH; any other value draws nothing.
 *
 * Deviation: the original reaches that layer2 byte through `tileA.cell[6 * v27][6 * row + column]`
 * where v27 is the name pass's loop counter left at 2, so the row index is 12 and the flat offset
 * 6*12 = 72 lands exactly on layer2. The port reads layer2 directly. The original bounds nothing:
 * nBase indexes the eight fac blocks and the eight buildDays blocks unchecked, and a craft pointer
 * array of Map_nCraftCount entries is walked without a null test. The port bounds the base at 8 and
 * skips null craft. The return value is register residue -- a craft-loop counter, or the address
 * v29 + 372 when no cell matched -- and no caller reads it, so the port returns void. */
typedef struct FacNameGridEnv {
    const u8 *abFacilityGrid;       /* abFacilityGrid @ 0x159768 */
    Ship *const *apCraft;           /* apCraft @ 0x114A64 */
    const u8 *pnCraftCount;         /* Map_nCraftCount @ 0x114B4C, read as a byte */
    const i32 *pnOriginX;           /* nSmapOriginX @ 0x159DC4 */
    const i32 *pnOriginY;           /* nSmapOriginY @ 0x159DC8 */
    const GfxColorEnv *pColor;      /* pGX @ 0x1139B8, the days label's colour pair */
    i8 nBaseIdx;                    /* a2, the base whose blocks are drawn */
} FacNameGridEnv;

void BaseFacScreen_DrawNameGrid(const FacNameGridEnv *pEnv);

/* --- Soldier_FormatDisplayName @ 0x6654c (384 B) : the soldier screen's craft column ---
 *
 * Called from SoldierScreen_Toggle (0x64AF0), BasePlaneScreen_FillCrewPanel (0x7A188) and
 * BasePlaneScreen_BuildListPanels (0x7A3F4). The soldier's craft byte is Soldier.bUnk183 @ +0xB7.
 *
 *   0xFF (0x66560): aTextRec93, and the function returns without looking at the 0xFE branch.
 *   0xFE (0x66694): aTextRec140, reached because the craft branch is skipped by `BCS` at 0x665AC.
 *   below 0xFE (0x665AC): apCraft[byte], whose Ship.bCraftType @ +0x02 picks aTextRec137, 138 or 139
 *   for types 0, 1 and 2, then Ship.bType @ +0x00 is appended as a number. Types 3 and 4 branch
 *   straight past the name and the number and write nothing at all: the original hands back the
 *   shared scratch string exactly as the previous call left it.
 *
 * The result is the shared scratch at 0x186B10 (szRadarMsg, 108 bytes), which the original fills
 * with unbounded copy loops and one strcat (bugs.tsv). The port writes into the caller's buffer with
 * the bound the caller supplies, and returns an empty string where the original left stale text. */
char *Soldier_FormatDisplayName(const Soldier *pSol, Ship *const *apCraft, const u8 *pTextRecs,
                                char *pszOut, size_t cchOut);

/* ------------------------------------------------- the options screen's audio tab
 *
 * Evidence: OptionsScreen_Init @ 0x6be74 (760 B), OptionsScreen_LayoutButtons @ 0x6b6c4,
 * OptionsScreen_BindSaveSlots @ 0x6b49c, OptionsScreen_BindLoadSlots @ 0x6b588,
 * OptionsScreen_RefreshVolumeHighlight @ 0x6b8fc, and the twelve radio handlers
 * OptionsScreen_SetMusicVol0..5 @ 0x6ba14 / 0x6ba70 / 0x6babc / 0x6bb18 / 0x6bb74 / 0x6bbd0 and
 * OptionsScreen_SetSfxVol0..5 @ 0x6bc1c / 0x6bc80 / 0x6bce4 / 0x6bd48 / 0x6bdac / 0x6be10.
 *
 * ## The control block
 *
 * The screen owns nineteen controls through .bss pointer slots: seven "list" rows at
 * 0x159E20..0x159E38, a two-slot gap, then six music radio buttons at 0x159E44..0x159E58 and six
 * sfx radio buttons at 0x159E5C..0x159E70. Two more controls live in the geoscape control block:
 * pGeoscapeCtlA (the "save" hotspot) and pGeoscapeCtlB (the "return to title" hotspot). The tab byte is
 * the scratch byte the helpers above already model (gOptionsScratch[0], ScreenScratch.nLineAux145):
 * 0 audio, 1 save, 2 load, 3 and 4 the two pen rows. A different byte at 0x159E8C is the "the
 * controls exist" flag OptionsScreen_Init branches on.
 *
 * ## OptionsScreen_Init @ 0x6be74
 *
 * Two modes off that flag. Set (0x6C0FC..0x6C15C): clear it, then UiControl_FreePads plus operator
 * delete over the seven list rows and the twelve radio buttons. Clear (0x6BE90..0x6C0B8): set it,
 * allocate and ctor seven list rows and position them with the seven identical records at 0xAB750
 * ("firebtn"/"cfirebtn", offset 1/229, mode 0); allocate and ctor twelve radio buttons; install
 * Options_SaveAudioSettings (0x6b7ac) on pGeoscapeCtlA and OptionsScreen_ReturnToTitle (0x6b46c) on
 * pGeoscapeCtlB; position the twelve with the twelve identical records at 0xAB830
 * ("initg"/"cinitg"/"cinitg", offset 1/229, mode 0). Then the two position loops: music buttons get
 * y = 107 and x = 46 + 25*i, sfx buttons get y = 147 and x = 46 + 25*i, i = 0..5 (the y store lands
 * at +2 and the x store at +0, so both survive). Then the twelve +12 handler slots get the twelve
 * radio handlers in slot order, and RefreshVolumeHighlight and LayoutButtons run last.
 *
 * Both descriptor tables are uniform repeats of one record, so the port keeps one record each.
 *
 * ## The radio handlers
 *
 * Each clears its group (OptionsScreen_ClearMusicButtons / OptionsScreen_ClearSfxButtons), selects
 * its own button with nState = 2, writes the volume global, and mutes only at step 0: Map_bMusicOff /
 * Map_bSoundOff become 1 for step 0 and 0 for steps 1..5. Music ends with Sound_ApplyMusicVolume;
 * sfx calls Sound_ApplySfxVolume and then plays Sound_Play(17,4) TWICE -- a real BL at 0x6BC64 and a
 * tail `B Sound_Play` at 0x6BC74 after the POP, with the same two immediates loaded again at
 * 0x6BC68/0x6BC6C (verified on the bytes: 0x6BC5C..0x6BC74 is E3A01004 E3A00011 EB00C915 E3A01004
 * E3A00011 E8BD4000 EA00C911 in all six members). Two clicks' worth of sound per click is the
 * original's, not an artefact of reading it twice. Four music handlers (0, 2, 3, 4) store the volume
 * before clearing the group and reach their button through the 0x159E44 + 4*n array; the other two
 * (1, 5) clear first and name the pointer directly. The order is not observable — the group clear only
 * zeroes nState and the selected button is written after it — so the port uses one body for all six.
 *
 * The level the appliers get is the original's own arithmetic, which Sound_ApplyMusicVolume @ 0x9E470
 * / 0x9E488 reads back out of the globals: 4 * (muted ? 0 : 12 * step + 4) - 1, muted yielding -1.
 * pufo_options.c carries the same formula for the startup options screen's copy of the state; the
 * duplication is a candidate cleanup, not this unit.
 *
 * ## OptionsScreen_RefreshVolumeHighlight @ 0x6b8fc
 *
 * Re-selects both groups from the current state: a muted group resets its step to 0 and selects
 * button 0, an unmuted one selects the button of its stored step. The original indexes the 0x159E44 /
 * 0x159E5C arrays with that step and tests nothing; the port clamps it into 0..5 first.
 *
 * ## The three tab binders
 *
 * LayoutButtons (0x6b6c4) is the audio tab: scratch byte 0; the first four list rows get
 * OptionsScreen_BindSaveSlots, OptionsScreen_BindLoadSlots, Options_SetTab3_AndUiY (0x6b674) and
 * Options_SetTab4 (0x6b6b0); those four are centred at x = 120 - (uWidth >> 1) with y = 90 + 25*i.
 * pGeoscapeCtlA then gets x = 120 - (its own uWidth >> 1) and y = 198, and pGeoscapeCtlB gets
 * x = 120 - (pGeoscapeCtlA's uWidth >> 1): the second hotspot is centred on the FIRST control's width,
 * because the ARM reloads R1 from the same R0 (pGeoscapeCtlA) for the uWidth read at 0x6B764 and only
 * then switches the store target to pGeoscapeCtlB at 0x6B778.
 *
 * BindSaveSlots (0x6b49c) and BindLoadSlots (0x6b588) are the same shape over all seven rows:
 * scratch byte 1 or 2, the seven SaveGame_SlotN_CloseUI / LoadGame_SlotN_InitOptions handlers,
 * x = 120 - (uWidth >> 1), y = 40 + 22*i, pGeoscapeCtlA->y = 215, and SaveSlot_FormatList over the main
 * menu's slot context. The fourteen slot handlers are ported below; they arrive here as the caller's
 * thunks because the click slot takes no arguments.
 *
 * Deviations: the original allocates every control with operator new(0x20) into fixed .bss slots and
 * tears them down with UiControl_FreePads plus operator delete; the port takes caller-owned controls,
 * so its teardown branch runs UiControl_FreePads and leaves the storage to the caller. The port's
 * click slot is `void (*)(void)`, so the twelve radio handlers, the fourteen slot handlers,
 * OptionsScreen_ReturnToTitle and Options_SaveAudioSettings (0x6b7ac, ported above) arrive as the
 * caller's thunks over the caller's own env. The original returns the applier's register from the
 * radio handlers; the port returns 0, which nothing reads. */
#define PUFO_OPTIONS_LIST_COUNT 7
#define PUFO_OPTIONS_VOL_COUNT  6

/* The original keeps all nineteen in ONE array -- `aOptionsListCtl @ 0x159E20`, 21 slots through
 * 0x159E73 -- laid out 0..6 the seven menu rows, 7 and 8 the overlay's prev/next pair (the two
 * OptionsScreen_Open allocates at 0x159E3C / 0x159E40), 9..14 the six music buttons (that is why
 * `PUFO_GEOOPT_VOL_CONTROL0` is 9), 15..20 the six sound buttons. `Geoscape_DrawScreen` 0x6C888 and
 * `Geoscape_OnPointer` 0x89160 index the flat array; `OptionsScreen_Init` owns only the 19, which is
 * why the struct below is split and the shell keeps the flat view beside it. */
#define PUFO_OPTIONS_CTL_COUNT 21

typedef struct OptionsScreenCtl {
    UiControl *apList[PUFO_OPTIONS_LIST_COUNT]; /* 0x159E20..0x159E38 */
    UiControl *apMusic[PUFO_OPTIONS_VOL_COUNT]; /* 0x159E44..0x159E58 */
    UiControl *apSfx[PUFO_OPTIONS_VOL_COUNT];   /* 0x159E5C..0x159E70 */
} OptionsScreenCtl;

typedef struct OptionsVolumeEnv {
    OptionsScreenCtl *pCtl;
    i32 *pnMusicVol;  /* Map_nMusicVolume @ 0x128A98 */
    u8  *pbMusicOff;  /* Map_bMusicOff @ 0x128A92 */
    i32 *pnSfxVol;    /* Map_nSoundVolume @ 0x128A94 */
    u8  *pbSfxOff;    /* Map_bSoundOff @ 0x128A93 */
} OptionsVolumeEnv;

/* The shared body behind the twelve handlers. nStep is the step the handler names. */
i32 OptionsScreen_SetMusicVol(OptionsVolumeEnv *pEnv, i32 nStep);
i32 OptionsScreen_SetSfxVol(OptionsVolumeEnv *pEnv, i32 nStep);

i32 OptionsScreen_SetMusicVol0(OptionsVolumeEnv *pEnv); /* 0x6ba14 */
i32 OptionsScreen_SetMusicVol1(OptionsVolumeEnv *pEnv); /* 0x6ba70 */
i32 OptionsScreen_SetMusicVol2(OptionsVolumeEnv *pEnv); /* 0x6babc */
i32 OptionsScreen_SetMusicVol3(OptionsVolumeEnv *pEnv); /* 0x6bb18 */
i32 OptionsScreen_SetMusicVol4(OptionsVolumeEnv *pEnv); /* 0x6bb74 */
i32 OptionsScreen_SetMusicVol5(OptionsVolumeEnv *pEnv); /* 0x6bbd0 */
i32 OptionsScreen_SetSfxVol0(OptionsVolumeEnv *pEnv);   /* 0x6bc1c */
i32 OptionsScreen_SetSfxVol1(OptionsVolumeEnv *pEnv);   /* 0x6bc80 */
i32 OptionsScreen_SetSfxVol2(OptionsVolumeEnv *pEnv);   /* 0x6bce4 */
i32 OptionsScreen_SetSfxVol3(OptionsVolumeEnv *pEnv);   /* 0x6bd48 */
i32 OptionsScreen_SetSfxVol4(OptionsVolumeEnv *pEnv);   /* 0x6bdac */
i32 OptionsScreen_SetSfxVol5(OptionsVolumeEnv *pEnv);   /* 0x6be10 */

i32 OptionsScreen_RefreshVolumeHighlight(OptionsVolumeEnv *pEnv); /* 0x6b8fc */

typedef struct OptionsTabEnv {
    OptionsScreenCtl *pCtl;
    i8               *pbTab;   /* gOptionsScratch[0] @ 0x159E88, the tab byte */
    UiControl *pSaveCtl;   /* pGeoscapeCtlA, the "save" hotspot */
    UiControl *pBackCtl;   /* pGeoscapeCtlB, the "return to title" hotspot */
    /* The four handlers LayoutButtons installs on the first four rows. They land in a control's
     * click slot, so they are `void (*)(void)` thunks over the caller's own env. */
    void (*pfnBindSaveSlots)(void); /* OptionsScreen_BindSaveSlots (0x6b49c) */
    void (*pfnBindLoadSlots)(void); /* OptionsScreen_BindLoadSlots (0x6b588) */
    void (*pfnSetTab3)(void);       /* Options_SetTab3_AndUiY (0x6b674) */
    void (*pfnSetTab4)(void);       /* Options_SetTab4 (0x6b6b0) */
    void (*apfnSaveSlot[PUFO_OPTIONS_LIST_COUNT])(void); /* SaveGame_SlotN_CloseUI (0x6af80..0x6b148) */
    void (*apfnLoadSlot[PUFO_OPTIONS_LIST_COUNT])(void); /* LoadGame_SlotN_InitOptions (0x6b194..0x6b404) */
    void (*pfnFormatSlots)(void *pUser);   /* SaveSlot_FormatList (0x45904), reads the save files */
    void *pUser;
} OptionsTabEnv;

UiControl *OptionsScreen_LayoutButtons(OptionsTabEnv *pEnv);  /* 0x6b6c4 */
void OptionsScreen_BindSaveSlots(OptionsTabEnv *pEnv);        /* 0x6b49c */
void OptionsScreen_BindLoadSlots(OptionsTabEnv *pEnv);        /* 0x6b588 */

/* ------------------------------------------------- Options_SaveAudioSettings @ 0x6b7ac (336 B)
 *
 * The "save" hotspot's click handler: OptionsScreen_Init installs it on pGeoscapeCtlA and
 * OptionsScreen_Open does the same at 0x6D24C. Geoscape_CloseUI case 18 calls it at 0x8BD00, and it
 * calls back into the close, so the two are mutually recursive.
 *
 * ## The body
 *
 * 1. 0x6B7C0: while bOptionsScreenOpen @ 0x159E80 is set, Geoscape_CloseUI(&0x1864F8).
 * 2. 0x6B7D4: while gOptionsScratch[4] (the controls-exist flag) is set:
 *    - 0x6B7E8: if gOptionsScratch[0] -- loaded LDRSB and sign-extended by 0x6B7E8..0x6B7D8 -- is
 *      non-zero:
 *      - 0x6B808: if it is exactly 4, the pen row Options_SetTab4 @ 0x6b6b0 selects, write the four
 *        audio settings to the registry in this order: soundvolume = itoa(Map_nSoundVolume),
 *        musicvolume = itoa(Map_nMusicVolume), musicoff = "1" when Map_bMusicOff is set else "0",
 *        soundoff likewise for Map_bSoundOff. The "1" and the "0" are the shared strings at 0xA62A8
 *        and 0xA62A4, and every value is stored as a REG_SZ string, the numbers included.
 *      - 0x6B8A8: gOptionsScratch[0] = 0, then OptionsScreen_LayoutButtons @ 0x6b6c4, which writes 0 to
 *        the same byte again at 0x6B6D0.
 *    - 0x6B7F0: otherwise Geoscape_CloseUI(&0x1864F8) again.
 * 3. 0x6B8BC: if bMapSelectionFlag @ 0x159E84 is clear, store 0 to 0x15961C -- GameTime.bPaused -- so
 *    a close with no pending map selection resumes the clock. Then bMapSelectionFlag = 0.
 *
 * ## Why the recursion ends
 *
 * Geoscape_CloseUI case 18 clears gOptionsScratch[0] at 0x8BCFC before calling this function, so the pass
 * started at 0x6B7C4 takes the 0x6B7F0 branch test rather than another call. That pass runs
 * OptionsScreen_RebuildWithSound @ 0x6cf00, whose OptionsScreen_Init @ 0x6be74 clears gOptionsScratch[4]
 * on its teardown branch -- so this function's own test at 0x6B7D4 fails and 0x6B7F0 is not reached at
 * all. The 0x6B7F0 arm only runs when bOptionsScreenOpen was already clear on entry.
 *
 * ## The three arguments are dead
 *
 * The ARM overwrites R0 with Map_nSoundVolume at 0x6B824 before its first call, and R1 and R2 are
 * never read anywhere in the function. Geoscape_CloseUI's a1 (the gOptionsScratch block), a2 (the signed
 * byte it read at 0x8BCD0) and a3 (the base-loop counter residue from 0x8BA70) all reach a callee that
 * ignores them. The port takes one env instead of three arguments.
 *
 * Deviation: the original dereferences every global it touches; the port skips a step whose pointer is
 * NULL. */
typedef struct SaveAudioEnv {
    i8   *pbTab;             /* gOptionsScratch[0], the options tab byte */
    u8   *pbControlsExist;   /* gOptionsScratch[4] */
    u8   *pbOptionsOpen;     /* bOptionsScreenOpen @ 0x159E80 */
    i32  *pnSfxVol;          /* Map_nSoundVolume @ 0x128A94 */
    i32  *pnMusicVol;        /* Map_nMusicVolume @ 0x128A98 */
    u8   *pbSfxOff;          /* Map_bSoundOff @ 0x128A93 */
    u8   *pbMusicOff;        /* Map_bMusicOff @ 0x128A92 */
    u8   *pbSelectionFlag;   /* bMapSelectionFlag @ 0x159E84 */
    GameTime *pTime;         /* 0x159618; bPaused is the byte at 0x15961C */
    const GeoscapeCloseEnv *pClose;  /* the close this function calls back into */
    OptionsTabEnv *pTabEnv;          /* the state LayoutButtons lays out */
} SaveAudioEnv;

i32 Options_SaveAudioSettings(SaveAudioEnv *pEnv); /* 0x6b7ac */

/* ------------------------------------------------- OptionsScreen_Open @ 0x6d0ac (3,160 B)
 *
 * The options screen's overlay half. OptionsScreen_OpenWithSound @ 0x6dd04 tail-calls it with the mode
 * it kept in R4, and Geoscape_CloseUI case 18 calls it with -1 at 0x8BD18. It is a toggle: the same
 * global decides which half runs.
 *
 * ## The close half (bOptionsScreenOpen set, 0x6DC30..0x6DCF4)
 *
 * 0x6DC38 clears nGeoscapeScreen @ 0x186BC4 and 0x6DC40 clears bOptionsScreenOpen. That second store
 * is what ends the Geoscape_CloseUI / Options_SaveAudioSettings recursion: case 18 re-reads the flag at
 * 0x8BD10 *after* the callee returns, so an inner OptionsScreen_Open(-1) makes the outer one skip its
 * own options-open arm. Then four panels are released through UiPanel_FreeLines + operator delete in
 * slot order 0x159E18, 0x159E78, 0x159E7C, 0x159E1C, and the two prev/next controls at 0x159E3C /
 * 0x159E40 through UiControl_FreePads + operator delete. Nothing else -- the seven list rows and the
 * twelve radio buttons belong to OptionsScreen_Init, not to this half.
 *
 * ## The open half (clear on entry)
 *
 * 1. 0x6D0C8: bOptionsScreenOpen = 1. 0x6D0DC: bUiAnimStarted @ 0x187080 = 0.
 * 2. The line count. nMode is sign-extended (0x6D0D4 / 0x6D0E0). If it is -1 both counts are simply
 *    Map_nCraftCount. Otherwise:
 *    - 0x6D104..0x6D144: walk the base pointer array (0x114B2C) for the first index whose BaseRec.bUnk4,
 *      loaded LDRSB, equals nMode. The bound is the SIGNED byte at 0x159D80 and a count <= 0 skips the
 *      walk; a miss leaves the index equal to the count.
 *    - 0x6D16C..0x6D190: count the craft whose Ship.bHomeBase equals that index. That count is the line
 *      count of all four panels, and the index is kept for the loop below.
 * 3. 0x6D1A8: bMapSelectionFlag = 0. 0x6D1B0..0x6D1F4: two new UiControl into 0x159E3C / 0x159E40,
 *    OptionsScreen_PanelPrev / OptionsScreen_PanelNext installed at +0xC, then UiControl_Setup over the
 *    two identical descriptors at stru_AB9B0 (0x20 bytes each).
 * 4. 0x6D240: pGeoscapeCtlA gets Options_SaveAudioSettings at +0xC, x = 120 - (uWidth >> 1), y = 179.
 *    OptionsScreen_LayoutButtons puts the same control at y = 198 -- the two tabs disagree.
 * 5. Four panels, each operator new(0x1BB4) + UiPanel_ctor(count), all with nStyle 6 and nFont 16:
 *      0x159E18 names  nPosX 5,   nRectX 5,   nViewW 230, bSelLineHighlight 1, bColorKey 100,
 *                     bytes0-2 (0A,50,5A), bytes3-5 (44,F4,FC), bytes6-11 (44,F4,FC,04,46,4B),
 *                     pfnClick = OptionsScreen_OnConfirm
 *      0x159E78 stores nPosX 115, nRectX 155, nViewW 30,  nUnk7076 1
 *      0x159E7C region nPosX 175, nRectX 185, nViewW 30,  nUnk7076 1
 *      0x159E1C cargo  nPosX 233, nRectX 5,   nViewW 230, nUnk7076 2, bColorKey 150 (written -106)
 * 6. 0x6D648..0x6DC18: the craft loop. With nMode == -1 the line index IS the craft index and nothing
 *    is filtered; otherwise a craft whose Ship.bHomeBase differs from the found index is skipped. Per
 *    line written:
 *    - names: Ship_FormatDisplayName, and the line's aux dword = the craft index.
 *    - stores: the aux dword starts at -1. uRangeRemain == uRangeMax AND nFuel == uFuelMax ->
 *      TEXTS[151] and aux = 0. uRangeRemain != uRangeMax -> TEXTS[150]. nFuel != uFuelMax -> TEXTS[149].
 *      bAtBase == 0 -> TEXTS[152] and aux back to -1.
 *    - region: apBases[Ship.bHomeBase]; a switch on BaseRec.bRegionId picks TEXTS 1165, 1166, 1167,
 *      1036, 1168, 1169, 1170, 1171, 1172 for 0x80..0x88, anything else leaves the buffer empty; then
 *      "-", then itoa of the SIGNED BaseRec.bUnk16.
 *    - cargo: Craft_CountSoldiersAboard, " \", Craft_CountWeaponSlotsCarried, " \", then "1" when
 *      Ship.bWeaponsHot or Ship.bIntercepting, plus "2" when both, appended as one string.
 *    The loop re-reads Map_nCraftCount on every iteration (0x6DC08) and returns as soon as the index
 *    reaches it.
 *
 * ## What the ARM showed that the pseudocode did not
 *
 * - The base-count bound is the signed byte at 0x159D80, reached as [abFacilityGrid,#0x618]. R0 is
 *   abFacilityGrid there because the LDREQ of 0x1139B8 at 0x6D0F0 is conditional on nMode == -1 and did
 *   not run. Hex-Rays renders the bound as a bare global and the walk as `*((_DWORD *)j + 1117)`, which
 *   hides that both walks are the same 0x1139B8-relative block with different offsets: 0x1174 for the
 *   base pointers, 0x10AC for the craft pointers.
 * - The four stores labels are TEXTS records 149, 150, 151 and 152 (aTextRecs + 205 * n) and the nine
 *   region labels are TEXTS 1165, 1166, 1167, 1036, 1168, 1169, 1170, 1171 and 1172 -- the switch is on
 *   BaseRec.bRegionId, the same region byte FundingScreen_Init groups into its five countries. All
 *   thirteen are zero-filled in the image and filled from the text table at load, so with no data file
 *   the labels are empty. Renamed in the IDB to aTextRecNNN. Read out of the shipped lang.dat they are
 *   149 "Ремонт", 150 "Заправка", 151 "Готов", 152 "На миссии", 1036 "Азия", 1165 "С.Аме", 1166 "Ю.Аме",
 *   1167 "Евро", 1168 "Авст", 1169 "Афр.", 1170 "Анта", 1171 "Аркт.", 1172 "Кан" -- four craft states
 *   and nine region abbreviations, which is what the two columns show.
 * - 0x6D71C `LDR R1,[R8,R2,LSL#2]!` pre-increments R8 to &apCraft[n] and every later Ship field read
 *   goes through it, which is why the pseudocode has two names for one pointer.
 *
 * Deviations: the original writes through the panel pointers with no null test after operator new; the
 * port skips a panel whose allocation or ctor failed. The original's 50-byte line buffers are filled by
 * unbounded copy loops; the port uses the bounded helpers. */
#define PUFO_OPTIONS_PANEL_COUNT 4

typedef struct OptionsOpenEnv {
    u8   *pbOptionsOpen;      /* bOptionsScreenOpen @ 0x159E80, the toggle */
    u8   *pbAnimStarted;      /* bUiAnimStarted @ 0x187080 */
    u8   *pbSelectionFlag;    /* bMapSelectionFlag @ 0x159E84 */
    i32  *pnScreenId;         /* nGeoscapeScreen @ 0x186BC4, cleared by the close half */
    const i8   *pnBaseCount;  /* the signed byte at 0x159D80 */
    const u8   *pnCraftCount; /* Map_nCraftCount @ 0x114B4C */
    BaseRec *const *apBases;  /* 0x114B2C */
    Ship *const *apCraft;     /* 0x114A64 */
    Soldier *const *apSoldiers;
    i32 nSoldierCount;
    ObjectRecFull *const *apObjects;
    i32 nObjectCount;
    UiControl **apPairCtl;    /* aOptionsListCtl + 0x1C: the prev/next pair @ 0x159E3C / 0x159E40 */
    UiPanel **ppPanelName;    /* 0x159E18 */
    UiPanel **ppPanelStores;  /* 0x159E78 */
    UiPanel **ppPanelRegion;  /* 0x159E7C */
    UiPanel **ppPanelCargo;   /* 0x159E1C */
    UiControl *pSaveCtl;      /* pGeoscapeCtlA @ 0x186BB4 */
    const u8 *pTextRecs;      /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    void (*pfnPanelPrev)(void);   /* OptionsScreen_PanelPrev (0x6d01c) over the four panels */
    void (*pfnPanelNext)(void);   /* OptionsScreen_PanelNext (0x6d064) */
    void (*pfnOnConfirm)(void);   /* OptionsScreen_OnConfirm (0x6cf18) over an OptionsConfirmEnv, as a
                                   * thunk: the original installs the function itself at 0x6D2E8 and the
                                   * click slot takes no arguments */
    void (*pfnSaveAudio)(void);   /* Options_SaveAudioSettings (0x6b7ac) over a SaveAudioEnv */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
    void *pUser;
} OptionsOpenEnv;

void OptionsScreen_Open(OptionsOpenEnv *pEnv, i32 nMode);

/* The research topic the final-mission button waits on. 0x6CFA8 and 0x8453C both read it as
 * LDRB [aResearchTopics,#0x3B8] -- 28 records of 34 bytes in. RESEARCH.DAT record 28 is named by
 * TEXTS[448] "Цидония или разорение" (the Cydonia topic, research cost 6000). */
#define PUFO_RESEARCH_TOPIC_CYDONIA 28

/* OptionsScreen_OnConfirm @ 0x6cf18 (260 B): the name panel's click handler, installed at 0x6D2E8 by
 * OptionsScreen_Open. Confirm the row the panel has selected.
 *
 * 1. 0x6CF3C..0x6CF50: the row is the NAME panel's nUnk3032 (what UiPanel_ClickRow set), and the craft
 *    it stands for is pPanelName->anLineAux[nUnk3032] -- OptionsScreen_Open wrote the craft index into
 *    that aux dword. Both selection globals come from it: nMsgShipIdx @ 0x159E74 and
 *    nSelectedShipIdx @ 0x187054.
 * 2. 0x6CF5C..0x6CF6C: the gate is the STORES panel's aux dword at the SAME index. OptionsScreen_Open
 *    leaves it 0 only when uRangeRemain == uRangeMax AND nFuel == uFuelMax AND bAtBase != 0, so a
 *    non-zero aux means "not ready" and the confirm does nothing at all: no flag, no sound, no line, no
 *    pause. The original returns the stores-panel pointer here (0x6CFF0).
 * 3. 0x6CF78: bMapSelectionFlag = 1. 0x6CF84: Sound_Play(7, 4) -- sample 7, the same second argument 4
 *    every other caller passes.
 * 4. 0x6CFA4: UiPanel_AppendLineWithSound over the root message list with flag 27 and the selected
 *    craft index as its colour dword. The index is re-read LDRB at 0x6CF8C, so a -1 selection appends
 *    255. Flag 27 is what Smap_ResolveInterceptUI (0x84964, ported at this file:5944) answers with Ship_RetargetTravel:
 *    picking a row arms a travel order for that craft.
 * 5. 0x6CFA8..0x6CFE0: aResearchTopics[28].bUnlocked AND apCraft[nSelectedShipIdx]->bCraftType == 2
 *    (LDRB [R3,#2]) arms byte_187068. Both constants are verified against the shipped data: TEXTS[415 +
 *    2] is "Мститель", the Avenger, and topic 28 is the Cydonia topic above. Geoscape_Draw @ 0x90908
 *    blits the pad of pFinalMissionCtl @ 0x186B8C and draws TEXTS[1236] "Финальная миссия!" centred on
 *    unk_186BA4 while the byte is set, and Geoscape_OnPointer @ 0x87590 / Geoscape_OnPointerUp @ 0x8971c
 *    hit-test that control before anything else in the default arm. So confirming a row arms the
 *    final-mission button for a ready Avenger once Cydonia is researched.
 * 6. 0x6CFE4: GameTime_Pause, on both arms of the topic test.
 *
 * Deviation: the original indexes both aux arrays and the craft array with no bound test; the port
 * checks the row against PUFO_PANEL_LINES and skips a null craft slot. */
typedef struct OptionsConfirmEnv {
    UiPanel **ppPanelName;            /* 0x159E18, the panel whose click installed this */
    UiPanel **ppPanelStores;          /* 0x159E78, the readiness gate */
    i32      *pnMsgShipIdx;         /* nMsgShipIdx @ 0x159E74 */
    i32      *pnSelectedShipIdx;      /* nSelectedShipIdx @ 0x187054 */
    u8       *pbSelectionFlag;        /* bMapSelectionFlag @ 0x159E84 */
    u8       *pbFinalMissionCtl;      /* byte_187068, arms the final-mission control */
    Ship *const *apCraft;             /* 0x114A64 */
    const ResearchTopicLive *aTopics; /* aResearchTopics @ 0x15EF08 */
    UiLineList *pLines;               /* the root message list @ 0x158890 */
    GameTime *pTime;                  /* 0x159618; bPaused is the byte at 0x15961C */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void *pUser;
} OptionsConfirmEnv;

void OptionsScreen_OnConfirm(OptionsConfirmEnv *pEnv);

typedef struct OptionsInitEnv {
    OptionsScreenCtl *pCtl;
    OptionsVolumeEnv *pVolEnv;  /* the state RefreshVolumeHighlight re-selects from */
    OptionsTabEnv    *pTabEnv;  /* the state LayoutButtons lays out */
    u8 *pbControlsExist;        /* gOptionsScratch[4] @ 0x159E8C */
    UiControl *pSaveCtl;        /* pGeoscapeCtlA */
    UiControl *pBackCtl;        /* pGeoscapeCtlB */
    void (*pfnSaveAudio)(void);          /* Options_SaveAudioSettings (0x6b7ac), as a thunk over a
                                          * SaveAudioEnv: the click slot takes no arguments */
    void (*pfnReturnToTitle)(void);      /* OptionsScreen_ReturnToTitle (0x6b46c), as a thunk */
    void (*apfnMusicVol[PUFO_OPTIONS_VOL_COUNT])(void); /* the twelve radio handlers, as thunks */
    void (*apfnSfxVol[PUFO_OPTIONS_VOL_COUNT])(void);
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
    void *pUser;
} OptionsInitEnv;

void OptionsScreen_Init(OptionsInitEnv *pEnv); /* 0x6be74 */

/* ------------------------------------------------- the options screen's save and load slots
 *
 * Evidence: SaveGame_Slot0..6_CloseUI @ 0x6af80 / 0x6afcc / 0x6b018 / 0x6b064 / 0x6b0b0 / 0x6b0fc / 0x6b148
 * and LoadGame_Slot0..6_InitOptions @ 0x6b194 / 0x6b1fc / 0x6b264 / 0x6b2cc / 0x6b334 / 0x6b39c / 0x6b404.
 * Fourteen copies (76 B and 104 B) of two bodies that differ only in the slot number, installed on the
 * seven rows by OptionsScreen_BindSaveSlots and OptionsScreen_BindLoadSlots.
 *
 * SaveGame_SlotN_CloseUI: MainMenu_nListMode = 4, SaveGame(&MainMenu_pSlotCtx, N), the tab byte back
 * to 0, MainMenu_nListMode = 1, then Geoscape_CloseUI over the 0x1864F8 control block as a tail call.
 * Mode 4 is what MainMenu_Draw (0x1dbb8) has no layout for — the start screen drops its rows while
 * the file is being written — and mode 1 is the "second four rows" menu it comes back to.
 *
 * LoadGame_SlotN_InitOptions: tab byte 0, nGeoscapeScreen = 0, MainMenu_nListMode = 3 (the loading
 * layout), the "controls exist" flag set, OptionsScreen_Init — which therefore takes its teardown
 * branch and releases the options controls before the load replaces the world — then
 * LoadGame(&MainMenu_pSlotCtx, N). A zero LOW BYTE of the result puts the list mode back to 1 and
 * returns 1; any other value is returned unchanged. The test is on the low byte, not the value, so a
 * LoadGame that returned 256 would read as a failure; the port reproduces the byte test.
 *
 * Deviations: the original reaches both drivers through the fixed slot context at 0x156938; the port
 * takes the caller's DatFile and its SaveGameInput / LoadGameEnv and writes the handler's slot into
 * pIn->nSlot / pLoadEnv->nSlot before the driver runs. Geoscape_CloseUI (0x8b94c, ported at this
 * file:3120) is reached as a hook here; it runs this screen's close case. The original's return
 * register is Geoscape_CloseUI's; the port returns 0. */
typedef struct SaveSlotEnv {
    DatFile        *pDat;       /* the slot file the driver opens */
    SaveGameInput  *pIn;        /* nSlot is set to this handler's slot before SaveGame runs */
    LoadGameEnv    *pLoadEnv;   /* nSlot is set to this handler's slot before LoadGame runs */
    i32            *pnListMode;      /* MainMenu_nListMode @ 0x157188 */
    i8             *pbTab;           /* gOptionsScratch[0] @ 0x159E88, the tab byte */
    u8             *pbControlsExist; /* gOptionsScratch[4] */
    i32            *pnGeoscapeScreen;/* nGeoscapeScreen @ 0x186BC4 */
    OptionsInitEnv *pOptionsInit;    /* re-initialised before the load */
    const struct GeoscapeCloseEnv *pClose; /* Geoscape_CloseUI @ 0x8b94c, tail call at 0x6AFB8 */
} SaveSlotEnv;

i32 SaveGame_Slot_CloseUI(SaveSlotEnv *pEnv, i32 nSlot);
i32 LoadGame_Slot_InitOptions(SaveSlotEnv *pEnv, i32 nSlot);

void SaveGame_Slot0_CloseUI(SaveSlotEnv *pEnv); /* 0x6af80 */
void SaveGame_Slot1_CloseUI(SaveSlotEnv *pEnv); /* 0x6afcc */
void SaveGame_Slot2_CloseUI(SaveSlotEnv *pEnv); /* 0x6b018 */
void SaveGame_Slot3_CloseUI(SaveSlotEnv *pEnv); /* 0x6b064 */
void SaveGame_Slot4_CloseUI(SaveSlotEnv *pEnv); /* 0x6b0b0 */
void SaveGame_Slot5_CloseUI(SaveSlotEnv *pEnv); /* 0x6b0fc */
void SaveGame_Slot6_CloseUI(SaveSlotEnv *pEnv); /* 0x6b148 */

i32 LoadGame_Slot0_InitOptions(SaveSlotEnv *pEnv); /* 0x6b194 */
i32 LoadGame_Slot1_InitOptions(SaveSlotEnv *pEnv); /* 0x6b1fc */
i32 LoadGame_Slot2_InitOptions(SaveSlotEnv *pEnv); /* 0x6b264 */
i32 LoadGame_Slot3_InitOptions(SaveSlotEnv *pEnv); /* 0x6b2cc */
i32 LoadGame_Slot4_InitOptions(SaveSlotEnv *pEnv); /* 0x6b334 */
i32 LoadGame_Slot5_InitOptions(SaveSlotEnv *pEnv); /* 0x6b39c */
i32 LoadGame_Slot6_InitOptions(SaveSlotEnv *pEnv); /* 0x6b404 */

/* ------------------------------------------------- three openers that also reset the clock
 *
 * Evidence: ResearchScreen_Open @ 0x616ac (232 B), Ui_OpenBaseInfo @ 0x84AF8 (256 B),
 * Geoscape_PrepScreenOpen @ 0x85194 (292 B).
 *
 * These are not the six plain openers above. All three run the same four-step prologue and then
 * tail-call their screen's Init:
 *
 *   1. Clear the shared message scratch. The ARM is a strcpy loop -- LDRSB / STRB / CMP #0 / BNE --
 *      reading from 0x10DA38, and 0x10DA38 is all zero in the image, so exactly one NUL lands at
 *      szRadarMsg @ 0x186B10 (dword_186B08 + 8). Nothing in the binary ever fills that source, so
 *      this is a clear and not a message write.
 *   2. Clear the second scratch the same way, from 0x10DA39 into unk_186420. Same all-zero source,
 *      same single NUL.
 *   3. Walk the research "active" bytes and promote every entry marked 1 to 2.
 *   4. Clear the shortfall dword (byte_1870C8, written with a 4-byte store) and run
 *      GameTime_Set5Sec, which puts the geoscape back on a five-second tick.
 *
 * The research walk is the same loop in all three, and the ARM spells out two details a source
 * reading would miss. The bound is the byte at 0x160C4C -- byte_160BBC + 144 -- and it is RE-READ
 * only after a store happened (0x6173C / 0x84B9C / 0x85234 are MOVEQ / LDRBEQ), so a walk that
 * changes nothing runs on the count it loaded at the start. And the loop is a do/while: with a
 * non-zero count the first entry is always examined. ResearchScreen_Open's copy reaches the same
 * result through `LDRB R3,[R0,R1]!` with writeback, which looks like a running pointer but is not:
 * the back edge at 0x61750 targets 0x6171C, which recomputes R0 from the base every iteration.
 *
 * Ui_OpenBaseInfo and Geoscape_PrepScreenOpen also store the walk's index into dword_186B08 -- 0
 * before the walk and the index reached after it. That global is the compiler's spilled local (see
 * ui/pufo_geouimanage.h) and it is the dword immediately below szRadarMsg, so the port keeps the
 * counter in a real local.
 *
 * The tails are where they differ:
 *   ResearchScreen_Open     -- sample 17 first, screen id 8 written BEFORE the Init, then POP and
 *                              the tail call B ResearchScreen_Init at 0x61778, with the topic array
 *                              at 0x15EF08 in R0.
 *   Ui_OpenBaseInfo         -- sample 17 first, screen id 16 written BEFORE the Init, then POP and
 *                              the tail call B Ui_InitBaseInfo at 0x84BD4, with the ufopaedia's
 *                              screen block g_pUfopaediaScreenPad @ 0x1872E0 in R0.
 *   Geoscape_PrepScreenOpen -- no sample, no screen id. It clears the clock pause byte first
 *                              (0x8519C, a STRB at 0x15961C), and after the prologue it removes
 *                              line 0 of the root message list (0x8525C) and then pauses the clock
 *                              when that line's flag is above 30 AND below 50 -- two separate
 *                              UiPanel_GetLineFlag calls, 0x85270 and 0x85284, not one value tested
 *                              twice.
 *
 * Deviation: the original bounds the walk only by the count byte, which is a byte and so could ask
 * for up to 255 of the 144 active entries. The port stops at PUFO_RESEARCH_TOPIC_COUNT. */
typedef struct ScreenResetOpenEnv {
    GeoscapeScreenState *pState;      /* nGeoscapeScreen @ 0x186BC4 */
    char     *pszMsg;                 /* szRadarMsg @ 0x186B10 */
    size_t    cchMsg;
    char     *pszScratch;             /* unk_186420 */
    size_t    cchScratch;
    u8       *abResearchActive;       /* byte_160BBC, 144 bytes */
    const u8 *pbResearchCount;        /* 0x160C4C = byte_160BBC + 144 */
    u32      *pdwShortfall;           /* byte_1870C8, written as a dword */
    GameTime *pTime;                  /* 0x159618; bPaused is the byte at 0x15961C */
    GeoscapeWorld *pWorld;            /* GameTime_Set5Sec's tick dword */
    UiControl *const *apSpeedCtl;     /* the six nav speed controls */
    UiLineList *pLines;               /* the root message list @ 0x158890 */
    ScreenInitHooks init;             /* the tail call into the screen's Init */
} ScreenResetOpenEnv;

void ResearchScreen_Open(const ScreenResetOpenEnv *pEnv);
void Ui_OpenBaseInfo(const ScreenResetOpenEnv *pEnv);
void Geoscape_PrepScreenOpen(const ScreenResetOpenEnv *pEnv);

/* ------------------------------------------------- the three siblings of that prologue
 *
 * Evidence: Geoscape_PrepScreenOpenSfx @ 0x852b8 (300 B), Ui_OpenBaseInfoAlt @ 0x84d18 (264 B),
 * Ui_OpenResearch @ 0x84bf8 (288 B).
 *
 * They run the same prologue as the three above, and the differences are all in what surrounds it.
 *
 *   Geoscape_PrepScreenOpenSfx -- the clock pause byte is cleared first (0x852C8), then
 *                                Sound_Play(17, 4) at 0x852D0, then the prologue, then the same
 *                                remove-line-0 / flag-in-(30,50) tail. It is the ONLY one of the six
 *                                that never calls GameTime_Set5Sec: 0x8537C goes straight from the
 *                                shortfall store to UiPanel_RemoveLine, so this opener leaves the
 *                                tick interval exactly as it found it.
 *   Ui_OpenBaseInfoAlt        -- sets byte_18706C to 1 (0x84D1C), calls the whole
 *                                Geoscape_PrepScreenOpen (0x84D24), and only then runs the prologue a
 *                                second time, so the message clears, the research walk, the shortfall
 *                                store, the line-0 removal and the clock dance all happen twice and
 *                                the 5-second tick is written twice. Screen id 16, tail call
 *                                Ui_InitBaseInfo(g_pUfopaediaScreenPad).
 *   Ui_OpenResearch           -- runs the prologue WITHOUT GameTime_Set5Sec (0x84CA4 goes from the
 *                                shortfall store straight to the call), then calls
 *                                Geoscape_PrepScreenOpen (0x84CBC), then GameTime_Set5Sec (0x84CC0),
 *                                then Geoscape_BuildNavControls(abFacilityGrid) (0x84CC8), then parks
 *                                the control at pGeoscapeCtlA at (1000, 1000) with two STRH of 0x3E8,
 *                                then screen id 8, then the tail call ResearchScreen_Init(0x15EF08).
 *
 * byte_18706C is the ufopaedia "reselect pending" flag: Ui_OpenBaseInfoAlt sets it on the way in,
 * Ufopaedia_RefreshEntry reads it at 0x9A02C, and Ui_InitBaseInfo consumes it at 0x9DEF4 -- when it
 * is non-zero the ufopaedia category at unk_160C60 is remapped onto a page and index and the flag is
 * cleared at 0x9E084. The port passes the byte through the env. */
typedef struct ScreenResetOpen2Env {
    ScreenResetOpenEnv base;
    NavControls        *pNavOut;          /* the fifteen nav-control pointers Ui_OpenResearch rebuilds */
    const NavCtlEnv    *pNavEnv;          /* their wiring */
    UiControl          *pParkCtl;         /* pGeoscapeCtlA, parked at (1000, 1000) */
    u8                 *pbUfopaediaReselect; /* byte_18706C */
} ScreenResetOpen2Env;

void Geoscape_PrepScreenOpenSfx(const ScreenResetOpenEnv *pEnv);
void Ui_OpenBaseInfoAlt(const ScreenResetOpen2Env *pEnv);
void Ui_OpenResearch(const ScreenResetOpen2Env *pEnv);

/* ResearchScreen_Init @ 0x749a8 (592 B): the research screen's bring-up, called from
 * ResearchScreen_Open's tail (0x615D0 -> 0x74B00 region) and from Ui_OpenResearch's chain.
 *
 * Order, straight from the ARM.
 *   1. 0x749B4..0x74A00: operator new(0x14) + PadImage_ctor into slot 0 of the screen block,
 *      PadImage_Load("Research", 6) at 0x749DC, then the two origins zeroed (STR R11,[R3] and
 *      STR R11,[R3,#4] at 0x749EC / 0x74A00 -- PadImage +0 and +4, not a UiControl field).
 *   2. 0x74A04..0x74A34: four operator new(0x20) + UiControl_ctor into ppScreen + 0x1D48 + 4*i,
 *      i.e. array indices 1874..1877. Four controls, not the manufacturing screen's six: research
 *      has no engineers pair.
 *   3. 0x74A4C..0x74A94: their handlers in slot order -- ResearchScreen_PanelPrev (0x72e30),
 *      PanelNext (0x72e68), IncSelectedQty (0x72ea0), DecSelectedQty (0x72f70).
 *   4. 0x74AA0..0x74AB8: the shared close control g_pUiCloseCtl moved to (224, 1) and pointed at
 *      ResearchScreen_CloseThunk (0x72ffc). Same position the manufacturing bring-up gives it.
 *   5. 0x74AC4 / 0x74AD0: the two shared bottom controls get their research handlers --
 *      pGeoscapeCtlA -> ResearchScreen_CancelSelectedAndClose (0x73380), pGeoscapeCtlB ->
 *      ResearchScreen_StartSelected (0x73240). Note the swap against the manufacturing screen,
 *      which points BB4 at its OK button and BB0 at its cancel: these two are generic geoscape
 *      controls and each screen re-points them. Their POSITIONS are not set here.
 *   6. 0x74AD4..0x74AF8: UiControl_Setup(slot[1874+i], 0, 0, &g_aResearchCtlDesc[i]).
 *   7. 0x74B00: ResearchScreen_RebuildTopicList (0x74270) with the screen block as its argument.
 *   8. 0x74B08..0x74B8C: for topic i in 0..nResearchEntryCount-1, if aResearchTopics[i].bActive is
 *      zero then auResearchCol[nSelectedBaseIdx * 144 + i] = 0. The base index is LDRSB of
 *      abFacilityGrid + 0x619 and is sign-extended by the usual LSL #24 / ASR #24 pair, so a
 *      negative index reaches the column from below; the count byte is re-read from memory on
 *      every iteration.
 *   9. 0x74B98..0x74BB4: NOW the two bottom controls are positioned -- pGeoscapeCtlA to (165, 188)
 *      and pGeoscapeCtlB to (165, 166). They sit one above the other in the same column.
 *  10. 0x74BB8: ResearchScreen_BuildPanels (0x733f4), then the tail call
 *      ResearchScreen_RefreshPanels (0x73008) -- the only one of the four research screen entry
 *      points that refreshes the panels itself rather than leaving it to a handler.
 *
 * g_aResearchCtlDesc @ 0xABB10 is four 32-byte descriptors: the topic-list scroll pair at x 205
 * y 18/38 (the same offsets the manufacturing tab pair uses) and the quantity pair at x 10
 * y 163/183. All four use the "up"/"sup" or "down"/"sdown" pad pair and mode 0.
 *
 * Deviation: the original allocates the pad and the four controls with operator new and derefs
 * every allocation with no test; the port takes caller-owned controls and installs them into
 * ppRoot[1..4], as the manufacturing, buy/sell and transfer inits do. */
#define PUFO_RESEARCH_PAD_NAME  "Research" /* aResearch_0 @ 0xABB90 */
#define PUFO_RESEARCH_PAD_MODE  6          /* 0x749DC: MOV R2,#6 */
#define PUFO_RESEARCH_CLOSE_X   224        /* 0x74AA0: STRH #0xE0 */
#define PUFO_RESEARCH_CLOSE_Y   1          /* 0x74AAC */
#define PUFO_RESEARCH_BOTTOM_X  165        /* 0x74B98 and 0x74BAC: STRH #0xA5 into both */
#define PUFO_RESEARCH_CANCEL_Y  188        /* 0x74BA0: STRH #0xBC, into pGeoscapeCtlA */
#define PUFO_RESEARCH_START_Y   166        /* 0x74BB4: STRH #0xA6, into pGeoscapeCtlB */

typedef struct ResearchInitEnv {
    void **ppRoot;                  /* the screen block; [0] the PadImage, [1..4] the four controls */
    UiControl *apCtl[4];            /* caller-owned, stored into ppRoot[1..4] */
    UiControl *pCloseCtl;           /* g_pUiCloseCtl */
    UiControl *pCancelCtl;          /* pGeoscapeCtlA, handler CancelSelectedAndClose, moved to (165,188) */
    UiControl *pStartCtl;           /* pGeoscapeCtlB, handler StartSelected, moved to (165,166) */
    void (*pfnCloseClick)(void);    /* ResearchScreen_CloseThunk @ 0x72ffc */
    void (*pfnCancelClick)(void);   /* ResearchScreen_CancelSelectedAndClose @ 0x73380 */
    void (*pfnStartClick)(void);    /* ResearchScreen_StartSelected @ 0x73240 */
    void (*pfnPanelPrev)(void);     /* ResearchScreen_PanelPrev @ 0x72e30 */
    void (*pfnPanelNext)(void);     /* ResearchScreen_PanelNext @ 0x72e68 */
    void (*pfnIncQty)(void);        /* ResearchScreen_IncSelectedQty @ 0x72ea0 */
    void (*pfnDecQty)(void);        /* ResearchScreen_DecSelectedQty @ 0x72f70 */
    void (*pfnRebuildTopicList)(void); /* ResearchScreen_RebuildTopicList @ 0x74270, ported in this file behind ResearchRebuildEnv */
    void (*pfnBuildPanels)(void);   /* ResearchScreen_BuildPanels @ 0x733f4 */
    ResearchTopicLive *aTopics;     /* aResearchTopics @ 0x15EF08 */
    const u8 *pbEntryCount;         /* nResearchEntryCount @ 0x160C4C, read with LDRB */
    u16 *apResearchCols;            /* auResearchCol @ 0x16022C, 144 u16 per base */
    const i8 *pnSelectedBaseIdx;    /* nSelectedBaseIdx @ 0x159D81, read with LDRSB */
    UiPanel *pPanelTopic;           /* unk_160C68, the first of the three research panels */
    UiPanel *pPanelQty;             /* unk_15EEFC */
    UiPanel *pPanelProgress;        /* unk_15EF00 */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
} ResearchInitEnv;

void ResearchScreen_Init(ResearchInitEnv *pEnv);

/* ResearchScreen_BuildPanels @ 0x733f4 (1600 B): build the research screen's three panels, or tear
 * them down, depending on byte_160C7C. The twin of ManufScreen_BuildPanels, with three differences.
 *
 * The filter is bActive, not bUnlocked plus abManufRowFlag. Manufacturing lists the items an unlocked
 * topic can produce; research lists the topics themselves, and `bActive` is already the "this topic is
 * on the screen list" flag that ResearchScreen_RebuildTopicList maintains. There is no object array
 * here at all: the row label is TEXTS[aTopics[nTopic].wGate4] read straight out of the text table.
 *
 * **The u16 at topic +0x4 is the topic's name record index.** 0x7373C `LDRH R3,[R10,#4]`, 0x73744
 * `MOV R2,#0xCD` / `MUL R2,R3,R2`, 0x73748 `ADD R1,R2,R0` with R0 = 0xB38D0 -- aTextRecs + 205 *
 * topic[+4]. That is the same field the weapon fire gate reads (pufo_combat.h), so +0x4 carries two
 * meanings and neither is a mistake: the record is read as a u16 by the fire gate and as a TEXTS index
 * here.
 *
 * **The percentage is computed the other way round from ResearchScreen_RefreshPanels.** Here it is
 * 100 * (wProgress / wTotal) -- 0x73834 LDRH R1,[R10,#0xA] as the dividend, 0x73850 LDRH R0,[R10,#8]
 * as the divisor, _rt_sdiv, then 0x73870 MUL R0,R3,#0x64 on the QUOTIENT. RefreshPanels at 0x73168
 * multiplies FIRST (MUL R1,R2,#0x64) and divides after. A topic at one quarter done therefore reads
 * "0%" from this function and "25%" from that one. Reproduced, recorded in docs/parity/bugs.tsv.
 *
 * **The screen block is the topic table.** ResearchScreen_Init's ppScreen is 0x15EF08, so the screen's
 * pad object lives in aResearchTopics[0]'s first four bytes, the four controls at 0x15EF08 + 0x1D48 =
 * 0x160C50..0x160C5C, and pResearchTopicPanel @ 0x160C68 is exactly one 34-byte record past the end of
 * the 144-record table. That is why the teardown reaches them as `*(void **)aResearchTopics[0].abUnk0`
 * and as `&aResearchTopics[220].abUnkF[1] + 4*i` -- 220 * 34 + 0x10 = 0x1D48 -- and not through named
 * globals.
 *
 * Build path, 0x73410..0x73940: byte_160C7C = 1; aResearchTopics[0].bActive = 1 (topic 0 is always on
 * the list); three UiPanel_ctor(144) allocations. All three share nStyle 15, nFont 16, nPosY 70,
 * nRectY 70, nViewW and the two style byte groups (bytes 0..2 = B4 B4 B4, bytes 6..11 = F0 F0 F0
 * 14 14 14); only panel A gets bytes 3..5 = 32 32 FA, bSelLineHighlight, bColorKey = 0x94 and the click handler
 * PlaySample7_SetFlags_Cache0xBD8 (0x73328), which is what caches the row cursor into 0x160C74 that
 * ResearchScreen_IncSelectedQty / DecSelectedQty read. Panel A is x 10 width 230 with nRectX 5;
 * panel B x 197 width 30 with nRectX 175; panel C x 233 width 30 with nRectX 205 -- B and C are the
 * two narrow numeric columns, both right-aligned (nUnk7076 = 2).
 *
 * Fill pass, 0x73700..0x73914: topics 1..nResearchEntryCount-1, bActive == 1 only. Panel A gets the
 * name and anLineAux[row] = the topic number; panel B gets itoa(auResearchCol[144*base + topic]) or the
 * empty string at 0x10DA38; panel C gets the percentage plus "%" or the empty string. The row counter
 * advances only for an active topic, and all three panels end with nLineCount = that counter.
 *
 * Teardown, 0x73944..0x73A28: byte_160C7C = 0; all three nLineCount = 144; the pad's data freed and the
 * pad deleted; the three panels freed in A, B, C order (UiPanel_FreeLines then delete each); then the
 * four controls' pads freed and the controls deleted.
 *
 * Deviations: the original divides by wTotal with no test, so a topic with wProgress != 0 and wTotal == 0
 * divides by zero -- the port skips the store, the same policy as RefreshPanels. The original also
 * derefs every allocation and the panel line buffers with no test. */
#define PUFO_RESEARCH_PANEL_STYLE 15  /* 0x73464 / 0x73594 / 0x73668: all three panels */
#define PUFO_RESEARCH_PANEL_FONT  16  /* 0x73478 / 0x735A8 / 0x73678 */
#define PUFO_RESEARCH_PANEL_POSY  70  /* 0x734B4 / 0x735CC / 0x7369C, and nRectY at the same value */

typedef struct ResearchBuildEnv {
    u8   *pbPanelsBuilt;              /* byte_160C7C */
    void **ppRoot;                    /* 0x15EF08, the topic table itself: [0] the pad, [1..4] the controls */
    UiControl *apCtl[4];              /* the four controls, pads freed by the teardown */
    UiPanel **ppPanelTopic;           /* pResearchTopicPanel @ 0x160C68 */
    UiPanel **ppPanelB;               /* pResearchPanelB @ 0x15EEFC */
    UiPanel **ppPanelC;               /* pResearchPanelC @ 0x15EF00 */
    ResearchTopicLive *aTopics;       /* aResearchTopics @ 0x15EF08 */
    const u8 *pbEntryCount;           /* nResearchEntryCount @ 0x160C4C */
    const u16 *apResearchCols;        /* auResearchCol @ 0x16022C, 144 u16 per base */
    const i8 *pnSelectedBaseIdx;      /* nSelectedBaseIdx @ 0x159D81 */
    const char *pTextRecs;            /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    void (*pfnPanelClick)(void);      /* PlaySample7_SetFlags_Cache0xBD8 @ 0x73328 */
} ResearchBuildEnv;

void ResearchScreen_BuildPanels(ResearchBuildEnv *pEnv);

/* ------------------------------------------------- the research screen's rebuild pass
 *
 * ResearchScreen_RebuildTopicList @ 0x74270 (1700 B): the pass that decides which topics are on the
 * research screen, which facility kinds finished research switches on, and which topic unlocks next.
 * All three callers pass the topic table itself -- ResearchScreen_Init (0x74B00),
 * ResearchScreen_StartSelected (0x73310) and ResearchScreen_AccumulateAllocated (0x749A0, the hourly
 * tick) -- and the first block proves it: 0x742E4 writes `*(result + 4875) = 1`, and
 * 4875 = 143 * 34 + 13 is aResearchTopics[143].bUnlocked.
 *
 * 1. 0x74294, the last record's own gate. When aResearchTopics[143].bUnlocked is zero, AND the
 *    bUnlocked of the five prerequisite topics listed at record 143 +0x13..0x17; when the product is
 *    exactly 1, unlock record 143. The product is a byte multiply (AND R2,R2,#0xFF after each MUL),
 *    so a prerequisite holding 2 gives 2 and the == 1 test fails.
 *
 * 2. 0x74314, the walk: `for (n = 1; n < nResearchEntryCount; ++n)`. Topic 0 is skipped -- it is the
 *    screen block, not a topic.
 *
 * 3. 0x74334: bUnlocked == 1 marks abResearchZero[n] = 1 and, when Research_TopicNeedsItem says the
 *    topic still needs an item it no longer has, drops bActive to 0 (0x74360).
 *
 * 4. 0x74364, the item gate: it runs unless (bUnlocked == 0 && wTotal > 0), and when it fails the
 *    whole iteration is skipped (0x74388 BEQ loc_748EC). So an in-progress topic is never dropped for
 *    a missing item, while an unstarted one with no progress is.
 *
 * 5. 0x74394 splits on wTotal > wProgress.
 *
 *    IN PROGRESS (0x747A0). A gate is rebuilt from the record's own groups:
 *      - 0x747AC: AND the bUnlocked of the five prerequisites at +0x13..0x17 into the gate, and ADD the
 *        same five bytes into an accumulator. The original adds the topic NUMBERS, not any count; the
 *        accumulator only ever feeds the `!= 0` test at 0x748E0, so this cannot change an outcome, but
 *        the arithmetic is reproduced.
 *      - 0x747E4: walk the five needed-item bytes at +0x0E..0x12. 254 and 255 clear the gate (the "no
 *        item" sentinels) and are still added to the accumulator; any other non-zero value that is
 *        <= Map_nObjectCount and whose apObjects[id]->auPerBaseVal[base] is zero clears the gate AND
 *        sets bActive = 0 (0x74854, 0x74858). An id above Map_nObjectCount is skipped entirely.
 *      - 0x74868: the second range pair, again from the NEXT record -- abUnk0[2] as lo2 and abUnk0[3]
 *        as hi2. lo2 + hi2 + accumulator must be non-zero, and when lo2 != hi2 some topic in
 *        [lo2, hi2] must already be unlocked or already marked in abResearchZero, or the gate is
 *        cleared (0x74894..0x748C8).
 *      - 0x748D0: bActive = 1 only when the gate survived and that sum is non-zero.
 *
 *    FINISHED (0x743A4): when the topic still needs its item, BaseRec_SubItemQty takes one back
 *    (0x743E8) and wProgress goes to zero (0x743F8). Either way bUnlocked = 1 (0x7440C); n lands in
 *    nResearchFinishedTopic as a byte (0x74414) and in nResearchTopicShown as a dword (0x74424);
 *    auResearchCol[n + 144*base] is zeroed for all eight bases (0x7442C..0x74444); the shortfall dword
 *    is set to 1 (0x74458, a dword store into byte_1870C8); the facility flag for this topic's kind is
 *    set; a message line is appended with flag 37 and colours n / 0xFF / 0xFF / 0xFF (0x74554);
 *    bActive is dropped when the item is missing (0x7456C); abResearchZero[n] = 1; and the five topics
 *    at +0x1D..0x21 are switched to bActive = 1 (0x74584..0x745A8).
 *
 * 6. 0x745AC, the unlock roll, using the FIRST range pair of the next record (abUnk0[0], abUnk0[1]).
 *    When hi != lo, roll Game_RandU16(hi - lo + 1) and retry while the landing topic is already active
 *    or already unlocked, up to 50 tries -- after 50 tries the original accepts whatever it landed on
 *    (0x7460C). That topic gets bUnlocked = 1 (0x74630) and becomes nResearchTopicShown (0x74644). The
 *    column-clear that follows (0x74648) runs over the ORIGINAL topic's eight entries a second time,
 *    not the newly unlocked one's: it repeats step 5's stores and changes nothing.
 *
 * 7. 0x7474C, after the roll: when abResearchZero[lo + roll] is zero the line appended in step 5 is
 *    REWRITTEN in place -- UiPanel_SetLineColors @ 0x5a450 overwrites line (nLineCount - 1) with flag
 *    37 and colours n / roll / 0xE8 / 0 (0x74780) -- and abResearchZero[lo + roll] is then set
 *    unconditionally (0x74798).
 *
 * 8. 0x74460 and 0x74668, the two switches. Both switch on the topic number itself with cases 37..53
 *    and write 1 into aBaseFacFlags[k].bFlag20 for the k in g_anResearchTopicFacKind -- a jumptable
 *    permutation, not an identity: topic 43 is FAC_MISSILE_DEFENCES, topic 51 is FAC_MIND_SHIELD, and
 *    so on. Case 10 (topic 47) has no slot, and FAC_HANGAR is the one kind no topic ever writes. Both
 *    switches fall through to their common tail, so the Append and the SetLine run for every walked
 *    topic, not only for the mapped ones. LoadGame @ 0x4539C carries the same 17-case table over the
 *    same 0x186E48 base with the same -37 offset and the same unmapped case 10, which is what pins the
 *    permutation down.
 *
 * Deviations: the original derefs the topic table, the object pointers, the count byte and the line
 * list with no test, and reaches 0x160228 (the gap before auResearchCol) for the range pair of topic
 * 143 -- nothing in the IDB writes those four bytes, so the port reads them as zero, which makes
 * lo == hi and skips the roll for the last topic. The port also returns the entry count where the
 * original returns its own argument (the 0x15EF08 anchor) when the count is 0 or 1: the callers expect
 * an i32 and the port cannot carry a pointer in it. */
#define PUFO_RESEARCH_FAC_TOPIC_BASE 37  /* 0x74460 / 0x74668: both switches are on (nTopic - 37) */
#define PUFO_RESEARCH_LINE_FLAG       37 /* 0x74550 MOV R1,#0x25 and 0x74774 MOV R2,#0x25 */
#define PUFO_RESEARCH_LINE_COLOR2   0xE8u /* 0x74770 MOV R3,#0xE8: the third colour slot of the
                                          * rewritten line. The appended line's slots 1..3 are all 0xFF
                                          * (0x74540, 0x74548). */
#define PUFO_RESEARCH_UNLOCK_TRIES   50  /* 0x7460C CMP R4,#0x32: then the roll is accepted as-is */

typedef struct ResearchRebuildEnv {
    ResearchTopicLive *aTopics;        /* aResearchTopics @ 0x15EF08, the original's own argument */
    const u8 *pbEntryCount;            /* nResearchEntryCount @ 0x160C4C, read with LDRB */
    u16 *apResearchCols;               /* auResearchCol @ 0x16022C, 144 u16 per base */
    u8 *abResearchZero;                /* abResearchZero @ 0x160B2C */
    BaseFacFlag *paFac;                /* aBaseFacFlags @ 0x186E48 */
    u32 *pdwShortfall;                 /* byte_1870C8, written as a dword at 0x74458 */
    UiLineList *pLines;                /* the root message list @ 0x158890 */
    ObjectRecFull *const *apObjects;   /* apObjects @ Map+0x108 */
    const u16 *pnObjectCount;          /* Map_nObjectCount @ Map+0x10A8 */
    BaseRec *const *apBases;           /* apBases @ Map+0x1174 */
    const i8 *pnSelectedBaseIdx;       /* nSelectedBaseIdx @ 0x159D81 */
    u8 *pbFinishedTopic;               /* nResearchFinishedTopic @ 0x15EEF8 */
    i32 *pnShownTopic;                 /* nResearchTopicShown @ 0x160C60 */
    void (*pfnPlaySample)(i32 nSample, void *pUser); /* UiLineList_AppendWithSound's seam */
    void *pUser;
    const u8 *pFinalTopicRange;       /* optional 0x160228 range bytes for topic 143 */
} ResearchRebuildEnv;

i32 ResearchScreen_RebuildTopicList(const ResearchRebuildEnv *pEnv);

/* ResearchScreen_Draw @ 0x73a34 (2108 B): the research screen's frame. Its only caller is
 * Geoscape_Draw's screen-id-8 case (0x94114: R0 = 0x15EF08, the topic table itself), and the result
 * is overwritten by `MOV R0,#0` at 0x94124, so the port returns void.
 *
 * Order, straight from the ARM.
 *   1. 0x73A3C: `LDR R0,[R0]` then PadImage_Blit -- the screen pad pointer lives in the first four
 *      bytes of aResearchTopics[0], because the screen block IS the topic table (ResearchScreen_Init
 *      stores the PadImage there). This is the one screen frame of the three that blits its pad;
 *      ManufScreen_DrawFrame opens straight into the rails.
 *   2. 0x73A44..0x73B10 / 0x73B14..0x73BE0: the shared frame rails, byte-identical to the transfer
 *      and manufacturing screens (see PUFO_FRAME_RAMP_*).
 *   3. 0x73BE8: byte_160C7C == 0 ends the function there -- no panels, no labels, and not even the
 *      close control. That byte is what ResearchScreen_BuildPanels (0x733f4) sets and clears.
 *   4. 0x73BFC..0x73C14: the three panels' text, in the order topic / B / C.
 *   5. 0x73C34..0x73CE0: white pair, TEXTS[1138] "Исследование  Учен. Вып." at (10, 52), then the
 *      selected site's 64-byte name from 0x186C5A + 64 * nSelectedBaseIdx, centred at (120 - w/2, 13).
 *   6. 0x73D00..0x73D60: the two column separators, height 16 * min(nLineCount, nStyle) + 2 of the
 *      TOPIC panel, at x 170 and x 200, y 69, width 1, index 0xB4.
 *   7. 0x73D64: unk_160C80 == 0 -- no topic row selected -- blits the shared close control
 *      g_pUiCloseCtl and returns. With a row selected this function never blits the close control;
 *      that is the original's shape, not an omission. unk_160C80 is set by panel A's click handler
 *      PlaySample7_SetFlags_Cache0xBD8 (0x73328) and cleared by ResearchScreen_StartSelected.
 *   8. 0x73DA0: Ui_AnimExpandBlit over the geoscape anim root 0x1864F8 at (0, 110) 236x100, index
 *      0x32, source (0x96, 0x32), key 0x96, mode 4. Only the low byte of the result is tested
 *      (0x73DA8), and a false result leaves through the same tail as step 7 without blitting
 *      anything else.
 *   9. 0x73DBC..0x73E64: the three read-out labels -- TEXTS[1139] "Ученых доступно" at (10, 118),
 *      TEXTS[1140] "Свободн. мест в лаборатории" at (10, 132), TEXTS[1141] "Ученых задействовано"
 *      at (10, 146).
 *  10. 0x73E8C..0x73F04: the teal pair 68,244,252 / 4,70,75, then TEXTS[1142] "Добав." at (40, 164)
 *      and TEXTS[1143] "Убрать" at (40, 184) -- the captions of the two quantity controls.
 *  11. 0x73F24..0x74068: white pair, then the three numbers, each built through j_itoa and drawn at
 *      x 195: Base_GetScientistCount at y 118, Base_CalcLabCap - Base_CountActiveResearch at y 132,
 *      and auResearchCol[panel A's anLineAux[nResearchTopicRowCache] + 144 * base] at y 146. The row
 *      index is the cached cursor 0x160C74, written by panel A's click handler.
 *  12. 0x7406C..0x7408C: two UiControl_BlitActivePad calls. The pointers come from
 *      `&aResearchTopics[0].wTotal + 1874` advanced by two u16 slots, i.e. the screen block's control
 *      slots 1876 and 1877 at 0x160C58 and 0x160C5C -- the IncSelectedQty and DecSelectedQty controls
 *      ResearchScreen_Init installed.
 *  13. 0x7409C..0x74114: pGeoscapeCtlA (the cancel control, which Init parked at (165, 188)), the teal
 *      pair again, then TEXTS[1144] "Отмена" centred at (198 - w/2, 189).
 *  14. 0x74120..0x74174: pGeoscapeCtlB (the start control, (165, 166)), then TEXTS[1127] "ОК" centred at
 *      (198 - w/2, 167). Note the caption pair is swapped against the controls' own order: "ОК" sits
 *      under the upper control, "Отмена" under the lower one.
 *  15. 0x74184..0x741A4: unk_186B94 is re-pointed at ResearchScreen_UpdateProgress (0x72d14) and moved
 *      to (100, 164) on every frame, then blitted.
 *  16. 0x741B4..0x741F8: TEXTS[1604] "ВСЕ" centred at (119 - w/2, 165), and that Text_DrawString is
 *      the value the original returns.
 *
 * The nine labels are direct pointers into aTextRecs (stride 205): 0xEC81A, 0xEC8E7, 0xEC9B4,
 * 0xECA81, 0xECB4E, 0xECC1B, 0xECCE8, 0xEBF4B and 0x103D44 are records 1138, 1139, 1140, 1141, 1142,
 * 1143, 1144, 1127 and 1604. 1127/1142/1143/1144/1604 are the same records the manufacturing frame
 * uses for its own buttons; only the positions differ.
 *
 * Deviations: the original copies every label into a 104-byte stack buffer with an unbounded
 * byte-at-a-time loop, builds the three numbers through a second 52-byte buffer, derefs the three
 * panels, the four controls, the three shared controls and the colour root with no test, and indexes
 * anLineAux with the cached row cursor without bounds. The port bounds the label buffer, null-guards
 * the panels and controls, and clamps the row read the same way ResearchScreen_IncSelectedQty does. */
#define PUFO_RESEARCH_TEXT_HEADER     1138 /* "Исследование                         Учен. Вып." at (10, 52) */
#define PUFO_RESEARCH_TEXT_SCI_FREE   1139 /* "Ученых доступно" at (10, 118) */
#define PUFO_RESEARCH_TEXT_LAB_FREE   1140 /* "Свободн. мест в лаборатории " at (10, 132) */
#define PUFO_RESEARCH_TEXT_SCI_USED   1141 /* "Ученых задействовано" at (10, 146) */
#define PUFO_RESEARCH_TEXT_ADD        1142 /* "Добав." at (40, 164) */
#define PUFO_RESEARCH_TEXT_REMOVE     1143 /* "Убрать" at (40, 184) */
#define PUFO_RESEARCH_TEXT_CANCEL     1144 /* "Отмена" at (198 - w/2, 189) */
#define PUFO_RESEARCH_TEXT_OK         1127 /* "ОК" at (198 - w/2, 167) */
#define PUFO_RESEARCH_TEXT_ALL        1604 /* "ВСЕ" at (119 - w/2, 165) */
#define PUFO_RESEARCH_BAR_INDEX       0xB4 /* the two column separators, 0x73D30 / 0x73D54 */
#define PUFO_RESEARCH_LABEL_MAX       104  /* psz @ sp+0x18 in the original */
#define PUFO_RESEARCH_ALLOC_X         100  /* 0x74190: unk_186B94->x, re-written every frame */
#define PUFO_RESEARCH_ALLOC_Y         164  /* 0x7419C: unk_186B94->y, re-written every frame */

typedef struct ResearchDrawEnv {
    const PadImage    *pPad;              /* *ppScreen, the PadImage stored in aResearchTopics[0] */
    const GfxColorEnv *pColor;            /* pGX @ 0x1139B8, the colour-pair owner */
    UiAnimBlitEnv     *pAnim;             /* the geoscape anim root @ 0x1864F8 */
    UiPanel           *pPanelTopic;       /* pResearchTopicPanel @ 0x160C68 */
    UiPanel           *pPanelQty;         /* pResearchPanelB @ 0x15EEFC */
    UiPanel           *pPanelProgress;    /* pResearchPanelC @ 0x15EF00 */
    u8                *pbPanelsBuilt;     /* byte_160C7C, the BuildPanels flag */
    const u8          *pbTopicSelected;   /* unk_160C80, set by panel A's click handler */
    const i32         *pnTopicRow;        /* nResearchTopicRowCache @ 0x160C74, a cached nUnk3032 */
    const i8          *pnSelectedBaseIdx; /* nSelectedBaseIdx @ 0x159D81, read with LDRSB */
    const char        *pTextRecs;         /* aTextRecs @ 0xB38D0, stride 205 */
    BaseSiteRec       *apSiteRecs;        /* 0x186C40, eight 64-byte records, name at +26 */
    const u16         *apResearchCols;    /* auResearchCol @ 0x16022C, 144 u16 per base */
    const u8          *pbEntryCount;      /* nResearchEntryCount @ 0x160C4C */
    const ObjectRecFull *pScientistRec;   /* apObjects[113] @ 0x113C84 */
    const u8          *abFacilityGrid;    /* abFacilityGrid @ 0x159768, for Base_CalcLabCap */
    UiControl         *apCtlQty[2];       /* slots 1876/1877 @ 0x160C58 (IncSelectedQty) and
                                           * 0x160C5C (DecSelectedQty), blitted in that order */
    UiControl         *pCtlCancel;        /* pGeoscapeCtlA, parked at (165, 188) by Init */
    UiControl         *pCtlStart;         /* pGeoscapeCtlB, parked at (165, 166) by Init */
    UiControl         *pCtlAllocAll;      /* unk_186B94, moved to (100, 164) and re-pointed here */
    UiControl         *pCtlClose;         /* g_pUiCloseCtl, blitted only when no row is selected */
    void (*pfnAllocAllClick)(void);       /* ResearchScreen_UpdateProgress @ 0x72d14 */
    UiControlBlitFn    pfnCtlBlit;
    UiControlBlitFn    pfnCtlBlitEx;
    void              *pCtlUser;
} ResearchDrawEnv;

void ResearchScreen_Draw(const ResearchDrawEnv *pEnv);

/* BuySellScreen_Draw @ 0x77e6c (3524 B): the buy/sell screen's frame, and the biggest of the four
 * screen frames. Called from Geoscape_Draw's screen-id-10/11/12 case (0x93E10); the result is register
 * residue and the caller discards it, so the port returns void.
 *
 * The frame splits on bBuySellPanelsBuilt @ 0x161A28 -- the same flag BuySellScreen_BuildPanels sets
 * and clears -- and the two halves are NOT the same drawing. They share the rails, the close control,
 * the funds line, the site name, the five panel draws, the four column separators and the cursor block,
 * but they differ in the two column captions' x, in the separators' x, in which total line is used and
 * in its colour, in whether the shortfall panel exists, and in how much of the cursor block runs.
 *
 * Common head, 0x77E80..0x78150. The shared rails. Then UiControl_BlitActivePad(g_pUiCloseCtl) -- the
 * close control, unconditionally, which is the opposite of the research frame. Then the pair
 * 120,255,120 / 10,10,10 and TEXTS[156] + itoa(nFunds) + " $" at (10, 28); Text_MeasureWidth runs at
 * 0x780B4 and its result is thrown away. Then the pair 180,180,180 / 10,10,10 and the selected site's
 * 64-byte name centred at (120 - w/2, 10).
 *
 * Panels not built, 0x786BC..0x78B94. bBuySellControlsBuilt @ 0x161A24 == 0 ends the function here.
 * Otherwise the pair 255,120,120 / 10,10,10 and TEXTS[157] + ":" + itoa(BuySell_nTransTotal) + " $" at
 * (10, 40); then the pair 180,180,250 / 10,10,10 with TEXTS[92] centred at (210 - w/2, 54) and
 * TEXTS[158] centred at (155 - w/2, 54). Then panels B and C get bSelLineHighlight = (dword at byte_1870C8 != 1)
 * -- the only place in this function that writes that flag -- and both get bRepeatArmed = unk_161F0C, which
 * is then zeroed. Five UiPanelMsg_Draw in the order A, E, C, B, D. Band = 16 * min(panelB->nLineCount,
 * panelB->nStyle); four 1-pixel separators at x 130, 176, 191, 206, y 69, height band + 2, index 0xB4.
 * The pair 220,80,80 / 50,30,30, and if the shortfall dword is 1 a switch on dword_1870B4[0] runs
 * Ui_AnimExpandBlit_IfEnabled(grid, 20, 130, 200, 70, 1) and draws TEXTS[405 + reason] centred at
 * (120 - w/2, 158) and returns. Reasons 4 and up fall through to the cursor block.
 *
 * Panels built, 0x78180..0x78420. Panels B and C get bRepeatArmed = unk_161F0C (no bSelLineHighlight write here),
 * then the scratch byte is zeroed. The pair 120,120,255 / 10,10,10 and TEXTS[411] + ":" +
 * itoa(BuySell_nTransTotal) + " $" at (10, 40); then 180,180,250 / 10,10,10 with TEXTS[92] centred at
 * (145 - w/2, 54) and TEXTS[158] centred at (212 - w/2, 54) -- the two captions swap sides against the
 * other half. The same five panel draws, the same band, separators at x 130, 160, 175, 190. No shortfall
 * panel at all on this path.
 *
 * The cursor block, both halves. Gate: bBaseFacPreviewOk != 0 and 75 < nSmapCursorY < band + 75. Inside
 * it the pair 150,150,255 / 20,20,55. The row marker is
 * panelA->anLineAux[unk_186BE8], and it decodes three ways:
 *   == 1000 (PUFO_ROW_SOLDIER) -- the built half draws apSoldiers[panelB->anLineAux[cursor]]->szName
 *     (Soldier.szName is +0xBA) at (10, 281) and TEXTS[412] at (10, 297); the not-built half draws only
 *     TEXTS[159] at (10, 297).
 *   > 2000 (an alien row, PUFO_ROW_ALIEN_OFFSET) -- apObjects[marker - 2000]->szName at (10, 281) and
 *     TEXTS[1605] at (10, 297).
 *   anything else -- apObjects[marker]->szName at (10, 281), then TEXTS[413] + itoa(apObjects[marker]
 *     ->uPrice) built into the label buffer, drawn at (10, 297) unless the marker is 117..121, where
 *     TEXTS[414] is drawn instead.
 * Outside the gate unk_186BA4 is blitted, the label buffer takes TEXTS[1127], the pair 68,244,252 /
 * 4,70,75 is set, and the label draws centred at (120 - w/2, 297). unk_161A30 is written only by the
 * panels-not-built half -- 1 inside its gate at 0x78B14, 0 outside it at 0x78B94; all five of its
 * cross-references sit in that tail, so the built half leaves the flag alone.
 *
 * The ten labels are direct aTextRecs pointers (stride 205): 0xBB5BC, 0xBB689, 0xBB756, 0xB827C,
 * 0xC81EF, 0xC82BC, 0xC8389, 0xC8456, 0x103E11 and 0xEBF4B are records 156, 157, 158, 92, 411, 412,
 * 413, 414, 1605 and 1127; the four shortfall captions are 405..408, the same records the transfer
 * screen uses for the same four reasons.
 *
 * Deviations: the original builds every label into a 56-byte stack buffer with unbounded copy loops and
 * up to three strcats, derefs all five panels, both controls, the object and soldier pointers and the
 * colour root with no test, and indexes anLineAux with the cached cursor without bounds. The port
 * bounds the label buffer, null-guards the panels and controls, and clamps the marker and the soldier
 * index. It also drops the three Text_MeasureWidth calls whose results the original discards, and it
 * does not follow the original into the freed panels: the panels-not-built half writes bSelLineHighlight and
 * bRepeatArmed into pBuySellPanelB/C and reads panel A's aux column after BuySellScreen_BuildPanels'
 * teardown has deleted all five (0x78864, recorded in docs/parity/bugs.tsv). */
#define PUFO_BUYSELL_TEXT_FUNDS      156 /* TEXTS[156] + itoa(nFunds) + " $" at (10, 28) */
#define PUFO_BUYSELL_TEXT_TOTAL_DOWN 157 /* the panels-not-built total line, 255,120,120 */
#define PUFO_BUYSELL_TEXT_TOTAL_UP   411 /* the panels-built total line, 120,120,255 */
#define PUFO_BUYSELL_TEXT_COL_LEFT    92 /* centred at (210, 54) / (145, 54) */
#define PUFO_BUYSELL_TEXT_COL_RIGHT  158 /* centred at (155, 54) / (212, 54) */
#define PUFO_BUYSELL_TEXT_SOLDIER    159 /* the not-built cursor line for a soldier row, at (10, 297) */
#define PUFO_BUYSELL_TEXT_SOL_NAME   412 /* under a soldier name, at (10, 297) */
#define PUFO_BUYSELL_TEXT_PRICE      413 /* plus itoa(ObjectRecFull.uPrice), at (10, 297) */
#define PUFO_BUYSELL_TEXT_CRAFT      414 /* rows 117..121, at (10, 297) */
#define PUFO_BUYSELL_TEXT_ALIEN     1605 /* under an alien name, at (10, 297) */
#define PUFO_BUYSELL_TEXT_OK        1127 /* centred at (120 - w/2, 297) */
#define PUFO_BUYSELL_TEXT_SHORTFALL  405 /* reasons 0..3, centred at (120 - w/2, 158) */
#define PUFO_BUYSELL_SEP_INDEX       0xB4
#define PUFO_BUYSELL_LABEL_MAX        56 /* psz @ sp+0x18 in the original */
#define PUFO_BUYSELL_CURSOR_Y_OFF     75 /* the cursor gate is 75 < nSmapCursorY < band + 75 */

typedef struct BuySellDrawEnv {
    const GfxColorEnv *pColor;          /* pGX @ 0x1139B8 */
    UiPanel  *pPanelA;                  /* pBuySellPanelA @ 0x161A1C, the name column */
    UiPanel  *pPanelB;                  /* pBuySellPanelB @ 0x161A00, the "<" column */
    UiPanel  *pPanelC;                  /* pBuySellPanelC @ 0x161A20, the ">" column */
    UiPanel  *pPanelD;                  /* pBuySellPanelD @ 0x160C88, the count column */
    UiPanel  *pPanelE;                  /* pBuySellPanelE @ 0x1615F8, the pending quantity */
    const u8 *pbPanelsBuilt;            /* bBuySellPanelsBuilt @ 0x161A28 */
    const u8 *pbControlsBuilt;          /* bBuySellControlsBuilt @ 0x161A24 */
    u8       *pbCursorOnRow;            /* unk_161A30, 1 inside the cursor gate, 0 outside */
    u8       *pbPanelFlagScratch;       /* unk_161F0C, copied into panels B/C then zeroed */
    const i32 *pnRowCursor;             /* unk_186BE8, the cached row cursor */
    const i8  *pnSelectedBaseIdx;       /* nSelectedBaseIdx @ 0x159D81 */
    const char *pTextRecs;              /* aTextRecs @ 0xB38D0, stride 205 */
    BaseSiteRec *apSiteRecs;            /* 0x186C40, name at +26 */
    ObjectRecFull *const *apObjects;    /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                   /* Map_nObjectCount @ 0x114A60 */
    Soldier *const *apSoldiers;         /* Map + 0x1198 @ 0x114B50 */
    i32 nSoldierCount;                  /* nSoldierCount @ 0x115350 */
    const i32 *pnFunds;                 /* nFunds @ 0x1598D8 */
    const i32 *pnTransTotal;            /* BuySell_nTransTotal @ 0x161A2C */
    const u8  *pbPreviewOk;             /* bBaseFacPreviewOk @ 0x18652D */
    const i32 *pnCursorY;               /* nSmapCursorY @ 0x186BD4 */
    const u32 *pdwShortfall;            /* byte_1870C8, read as a dword */
    const i32 *pnShortfallReason;       /* dword_1870B4[0] */
    UiAnimBlitEnv *pAnim;               /* Ui_AnimExpandBlit_IfEnabled's progress pair */
    UiControl *pCtlClose;               /* g_pUiCloseCtl, blitted unconditionally */
    UiControl *pCtlOk;                  /* unk_186BA4, blitted when the cursor is not on a row */
    UiControlBlitFn pfnCtlBlit;
    UiControlBlitFn pfnCtlBlitEx;
    void *pCtlUser;
} BuySellDrawEnv;

void BuySellScreen_Draw(const BuySellDrawEnv *pEnv);

/* ------------------------------------------------- the plane screen's stat rows
 *
 * Evidence: BasePlane_ResetDetailState @ 0x79cb8 (328 B), reached from 0x7baf8 --
 * BasePlaneScreen_DrawCraftDetail, ported at pufo_plane.c:1833. It rebuilds the six
 * craft-weapon rows of the plane screen's stat panel for the currently selected base.
 *
 * 1. 0x79CC0..0x79D1C: the six per-weapon counts and nPlaneStatRowCount are cleared, and the row count
 *    starts at 1 rather than 0 -- the panel always has its header row, which is what
 *    BasePlaneScreen_DrawStatLabel @ 0x79e00 sizes its three nested rects with
 *    (20 * (nPlaneStatRowCount + 1), then 20 * n + 18 and 20 * n + 16).
 * 2. 0x79D30..0x79DE8: the loop runs over EVERY object slot, 0..Map_nObjectCount - 1, and reads
 *    apObjects[i]->auPerBaseVal[nSelectedBaseIdx] (the LDRH at +0x2A) for each of them. Only slots
 *    25..30 have a switch arm; every other slot's read is evaluated and discarded, so the observable
 *    effect comes from those six alone. They are the six craft weapons in the shipped OBJECTS.DAT:
 *    25 Craft cannon, 26 Stingray launcher, 27 Avalanche launcher, 28 Laser cannon, 29 Plasma cannon,
 *    30 Fusion ball launcher.
 * 3. A slot whose per-base count is non-zero stores that count into its own global and bumps the row
 *    count, so the panel lists exactly the weapons the selected base actually holds.
 *
 * The six globals are not contiguous -- they sit in the gaps between the plane screen's panel pointers
 * at 0x161ED0..0x161EFC (pPlaneCraftStatusPanel, pPlaneCrewPanel, nPlaneCrewQuota, pPlaneCommitRowPanel,
 * pPlaneTakenPanel, pPlaneCraftPanel), which is why the IDB carries them separately. They are dwords,
 * not words: every store is a STR and every read an LDR, including the six gates in Geoscape_OnPointer
 * (0x88674..0x88730) and Geoscape_OnPointerUp (0x8a38c..0x8a460), where a row's control is hit-tested
 * only when that row's count is non-zero. The value stored is the u16 read at +0x2A widened.
 *
 * auPerBaseVal's slot count is PUFO_FAC_BASE_COUNT, already defined in pufo_geoscape.h. */
#define PUFO_PLANE_WEAPON_FIRST   25 /* Craft cannon */
#define PUFO_PLANE_WEAPON_LAST    30 /* Fusion ball launcher */

typedef struct BasePlaneDetailEnv {
    ObjectRecFull *const *apObjects; /* the Map-block array at 0x113AC0 = 0x1139B8 + 66 dwords */
    i32             nObjectCount;    /* Map_nObjectCount @ 0x114A60, the loop bound */
    i32             nBaseIdx;        /* nSelectedBaseIdx @ 0x159D81, a signed byte in the original */
    i32            *pnCraftCannon;   /* nPlaneStatCraftCannon @ 0x161ED8, slot 25 */
    i32            *pnStingray;      /* nPlaneStatStingray @ 0x161EE8, slot 26 */
    i32            *pnAvalanche;     /* nPlaneStatAvalanche @ 0x161EF8, slot 27 */
    i32            *pnLaserCannon;   /* nPlaneStatLaserCannon @ 0x161EF0, slot 28 */
    i32            *pnPlasmaCannon;  /* nPlaneStatPlasmaCannon @ 0x161ABC, slot 29 */
    i32            *pnFusionBall;    /* nPlaneStatFusionBall @ 0x161ED4, slot 30 */
    i32            *pnRowCount;      /* nPlaneStatRowCount @ 0x161A38 */
} BasePlaneDetailEnv;

/* Returns the row count, which is also the original's return value (1 with nothing matched). */
i32 BasePlane_ResetDetailState(const BasePlaneDetailEnv *pEnv);

/* ------------------------------------------------- the base-facility status line
 *
 * Evidence: BaseFac_BuildStatusMsg @ 0x6a4bc (1,520 B), reached only from 0x8C82C inside the
 * geoscape dispatcher Geoscape_Draw @ 0x8c180 (ported in pufo_geoscape.c:6849). That call site supplies every
 * argument out of line 0 of the root message list @ 0x158890: nCase is UiLineList_GetFlag(line 0)
 * sign-extended from a byte (0x8C818), nBaseIdx is that line's colour dword slot 2 (0x8C7C4),
 * nFacRow is slot 0 (0x8C7EC) and nFacCol is slot 1 (0x8C7D8); a1 is abFacilityGrid (0x8C790 loads it
 * into R7). The dispatcher then switches on the SAME flag byte again at 0x8C83C and draws the two
 * lines this function leaves behind.
 *
 * The switch is on the case byte itself, 0x20..0x2C -- thirteen arms, `SUB R0, R3, #0x20 /
 * CMP R0, #0xC / ADDLS PC, PC, R0, LSL #2` at 0x6A4E0, with R3 sign-extended from the char. Anything
 * else leaves both buffers untouched. GameTime_Pause runs on EVERY arm including the default (0x6A4D4,
 * before the switch).
 *
 * Two output buffers, both fixed global scratch:
 *   pszMsg    -- szRadarMsg @ 0x186B10 (dword_186B08 + 8), the first line
 *   pszExtra  -- unk_186420, the second line
 * and a third, szScratch186488 @ 0x186488, which two arms fill and one arm reads back: the original
 * uses it to carry a name from one status line to the next, so ',' depends on what a previous call
 * left there.
 *
 * Six of the label prefixes are TEXTS records, not literals -- 0xF650E / 0xF690F / 0xF69DC / 0xF6AA9 /
 * 0xF6B76 / 0xF6C43 all sit past .data's initialised span (929,136 virtual bytes, 26,624 raw, so
 * everything from 0xAC800 up is zero-filled at load) and each is an exact multiple of 205 past
 * aTextRecs: records 1334, 1339, 1340, 1341, 1342 and 1343. Out of lang.dat they read "База: " (Base: ),
 * "База : " (the same word with the space before the colon), "Производство: " (Production: ),
 * "Исследование: " (Research: ), "Модуль: " (Module: ) and "Корабль: " (Craft: ).
 *
 * The arms, in case order:
 *   ' '  pszMsg = 1334 + base name;      pszExtra = "Product: " + apObjects[nFacCol]->szName
 *   '!'  pszMsg = 1339 + base name;      pszExtra = 1340 + apObjects[nFacCol]->szName
 *   '"'  identical to '!'
 *   '#'  pszMsg = 1334 + base name;      pszExtra = 1340 + apObjects[nFacCol]->szName
 *   '$'  pszMsg = 1334 + base name;      pszExtra untouched
 *   '%'  pszMsg = 1341 + TEXTS[aTopics[nBaseIdx].wGate4]; pszExtra only when nFacCol == 1000, and then
 *        it is 1341 + TEXTS[aTopics[nFacRow + aTopics[nBaseIdx + 1].abUnk0[0]].wGate4] -- the topic
 *        index is read one RECORD in, at +0x22, the same off-by-one-record pairing
 *        ResearchScreen_RebuildTopicList uses.
 *   '&'  pszMsg = 1334 + base name;      pszExtra = 1342 + aBaseFacFlags[kind].szName, where kind is
 *        abFacilityGrid[480 + 6 * (24 * nBaseIdx + nFacRow) + nFacCol]. The grid is eight 144-byte
 *        24x6 blocks starting at abFacilityGrid + 372, so +480 is row 18 and nFacRow 0..5 selects the
 *        grid's last six rows.
 *   '\'' pszExtra = 1343 + Ship_FormatDisplayName(nFacRow); szScratch = nFacCol == 255 ? "" :
 *        apObjects[nFacCol]->szName. The empty case copies from 0x10DA38, which is inside .data's
 *        zero-filled span -- the same all-zero source the three screen openers use to clear these
 *        buffers, so it is a clear and not a message.
 *   '('  pszMsg = 1334 + base name;      szScratch = apObjects[nFacRow]->szName
 *   ')'  identical to '*' and to '$': pszMsg = 1334 + base name, pszExtra untouched
 *   '*'  as ')'
 *   '+'  pszExtra = 1343 + Ship_FormatDisplayName(nFacRow); pszMsg = 1334 + base name
 *   ','  pszExtra = 1343 + szScratch186488
 *
 * Deviation: every copy here is an unbounded strcpy/strcat loop into fixed scratch buffers, and the
 * index arithmetic is untested -- apObjects[nFacCol] and apObjects[nFacRow] are dereferenced with no
 * bound, abFacilityGrid's facility byte is read with no bound, and aBaseFacFlags[kind] is indexed by a
 * byte that can exceed the 17 records. The port bounds every copy by the buffer sizes the env carries
 * and skips a dereference whose index is out of range, reaching the same observable text for the values
 * the shipped game actually produces. */
#define PUFO_FAC_STATUS_CASE_FIRST   0x20 /* ' ', the lowest case the jumptable covers */
#define PUFO_FAC_STATUS_CASE_LAST    0x2C /* ',', the highest */
#define PUFO_FAC_STATUS_TEXT_BASE 1334 /* "База: " Base: */
#define PUFO_FAC_STATUS_TEXT_BASE_SPACED 1339 /* "База : " the same word, space before the colon */
#define PUFO_FAC_STATUS_TEXT_PROD 1340 /* "Производство: " Production: */
#define PUFO_FAC_STATUS_TEXT_RESEARCH 1341 /* "Исследование: " Research: */
#define PUFO_FAC_STATUS_TEXT_MODULE 1342 /* "Модуль: " Module: */
#define PUFO_FAC_STATUS_TEXT_CRAFT 1343 /* "Корабль: " Craft: */
#define PUFO_FAC_STATUS_RESEARCH_TAG 1000 /* the '%' arm's second line only runs for this nFacCol */
#define PUFO_FAC_STATUS_NO_OBJECT    255  /* the '\'' arm's "no object" sentinel */
/* The base name the original reaches as byte_186C24 + 64 * i + 54 is the port's BaseSiteRec.abName:
 * 0x186C24 + 54 == 0x186C40 + 26, the same byte of the same 64-byte record. */
#define PUFO_FAC_GRID_ROW_STRIDE     6    /* the 24x6 facility grid's row stride */
#define PUFO_FAC_GRID_BASE_STRIDE    144  /* 24 rows of 6 */
#define PUFO_FAC_GRID_FACILITY_BASE  480  /* abFacilityGrid + 480 is row 18 of the grid */
#define PUFO_FAC_GRID_FACILITY_ROWS  6    /* rows 18..23; a row past 5 would leave the base's block */

typedef struct BaseFacStatusEnv {
    GameTime             *pTime;        /* GameTime_Pause @ 0x5d6ac, on every arm including the default */
    const u8             *abFacilityGrid; /* abFacilityGrid @ 0x159768, the '&' arm's facility-kind bytes */
    const BaseSiteRec    *apSiteRecs;   /* 0x186C40, eight 64-byte records; the name is abName at +26 */
    ObjectRecFull *const *apObjects;    /* the Map-block array at 0x113AC0 = 0x1139B8 + 66 dwords */
    i32                   nObjectCount; /* Map_nObjectCount @ 0x114A60, the port's bound on those indices */
    const BaseFacFlag    *paFac;        /* aBaseFacFlags @ 0x186E48, PUFO_BASEFAC_COUNT records */
    const ResearchTopicLive *aTopics;   /* aResearchTopics @ 0x15EF08, 144 records of 34 bytes */
    Ship *const          *apCraft;      /* Ship_FormatDisplayName's craft array */
    const u8             *pTextRecs;    /* aTextRecs @ 0xB38D0, stride PUFO_TEXT_REC_STRIDE */
    char                 *pszMsg;       /* szRadarMsg @ 0x186B10 */
    size_t                cchMsg;
    char                 *pszExtra;     /* unk_186420 */
    size_t                cchExtra;
    char                 *pszScratch;   /* szScratch186488 @ 0x186488 */
    size_t                cchScratch;
} BaseFacStatusEnv;

/* Returns the line the arm last wrote, or NULL when the case is outside 0x20..0x2C. The original
 * returns strcat's destination for most arms and a stray one-past-the-source pointer for '\'' and '(',
 * and the only caller drops it. */
char *BaseFac_BuildStatusMsg(const BaseFacStatusEnv *pEnv, i32 nCase, i32 nBaseIdx,
                             i32 nFacRow, i32 nFacCol);

/* ---- Ufopaedia: the in-game encyclopedia -------------------------------------------
 *
 * Evidence: Ufopaedia_RefreshEntry @ 0x9a014, Ufopaedia_PrevEntry @ 0x9a60c,
 *           Ufopaedia_NextEntry @ 0x9a754.
 *
 * The screen is nine category panels plus one 50-line text panel. Paging walks the
 * category panels and lands on a research topic; that topic's ufopaedia entry number
 * then selects the text layout and the pad asset.
 *
 * Two tables drive it, both read straight out of the image:
 *
 *   aUfopaediaCatTopics @ 0xAA830 -- 9 rows of 22 research topic numbers, the category
 *     order. IDA names it aFghijklmnop after the first row's bytes, which happen to be
 *     ASCII 'f'..'p' (102..112). Row 5 is the only row that fills all 22 slots; every
 *     other row ends at a zero byte, and Ufopaedia_NextEntry stops on that zero
 *     (0x9A7A8). Ufopaedia_PrevEntry has no such test -- it walks down to -1 and wraps
 *     the category there (0x9A640), then relies on the panel's own line count.
 *   anUfopaediaEntryNum @ 0xAA5F0 -- 144 dwords, research topic number -> entry number.
 *     Five topics map to 0 (47, 94, 95, 96, 143) and the alien-race topics 118..142 map
 *     onto the five race entries 54..58.
 *
 * Entry numbers are not sequential per category: topics 55..76 map to 54..75, topics
 * 77..93 to 76..92, topics 102..112 to 93..103, and the rest to 1001..1051, 2026..2036,
 * 2097..2101 and 3000..3016. Ufopaedia_RefreshEntry branches on those ranges.
 *
 * The category panels reuse UiPanel's own storage for their entry list: apLines[n] holds
 * the entry name and anLineAux[n] holds its topic number. That is why both paging
 * functions read the topic as `panel + 4 * (n + 1261)` -- 4 * 1261 = 5044 = anLineAux's
 * offset in the 7092-byte layout. nLineCount is the entry count and nUnk3032 the row the
 * pointer last selected. */

#define PUFO_UFOPAEDIA_CATEGORIES   9   /* g_apUfopaediaCatPanels[0..8] @ 0x1871D0 */
#define PUFO_UFOPAEDIA_CAT_WIDTH    22  /* aUfopaediaCatTopics's row length; 0x9A7A8's wrap test */
#define PUFO_UFOPAEDIA_TEXT_LINES   50  /* the text panel's line capacity, from 0x9A0F0's clear loop */
#define PUFO_UFOPAEDIA_TEXT_MAXW    220 /* the default wrap width (0x9A0D4) */
#define PUFO_UFOPAEDIA_MAXW_ALIEN   200 /* entry > 2999 (0x9A160) */
#define PUFO_UFOPAEDIA_MAXW_TIGHT   180 /* 2000 < entry < 3000 (0x9A1CC) */

/* Ufopaedia_RefreshEntry's layout bands, keyed on the entry number. The bands are what the
 * ARM branches on (0x9A14C..0x9A1F0); what each band's entries depict is not recorded in the
 * IDB, so they are named after the layout they select, not after their subject matter. */
#define PUFO_UFOPAEDIA_BAND_ALIEN_FIRST  3000 /* entry > 2999: 200 px, style 8, panel at y 110 */
#define PUFO_UFOPAEDIA_BAND_DEFAULT_LAST 1000 /* entry <= 1000: 220 px, style 7, panel at y 230 */
#define PUFO_UFOPAEDIA_BAND_WIDE_LAST    2000 /* 1000 < entry < 2000: 220 px, wide flag set */
#define PUFO_UFOPAEDIA_BAND_TIGHT_LAST   3000 /* 2000 < entry < 3000: 180 px, style 13, y 50 */
#define PUFO_UFOPAEDIA_INDENT_FIRST        93 /* 93..97 also start the text at line 7 (0x9A20C) */
#define PUFO_UFOPAEDIA_INDENT_LAST         97
#define PUFO_UFOPAEDIA_INDENT_LINES         7 /* the line the indented band starts writing at */
/* Three further bands that only set the wide flag (0x9A21C, 0x9A230, 0x9A244). Entries 54..58
 * are the five alien races -- that much the IDB states outright, through the race-name
 * overwrite in the screen's opener -- but what the rest of 54..92 depicts is not recorded
 * anywhere, so the bands carry no subject name here. */
#define PUFO_UFOPAEDIA_FLAG_BAND1_FIRST    54 /* 53 < entry < 76 */
#define PUFO_UFOPAEDIA_FLAG_BAND1_LAST     75
#define PUFO_UFOPAEDIA_FLAG_BAND2_FIRST    76 /* 75 < entry < 85 */
#define PUFO_UFOPAEDIA_FLAG_BAND2_LAST     84
#define PUFO_UFOPAEDIA_FLAG_BAND3_FIRST    85 /* 84 < entry < 93 */
#define PUFO_UFOPAEDIA_FLAG_BAND3_LAST     92
#define PUFO_UFOPAEDIA_RACE_ENTRY_FIRST    54 /* Sectoid, Snakeman, Ethereal, Muton, Floater */
#define PUFO_UFOPAEDIA_RACE_ENTRY_LAST     58

#define PUFO_UFOPAEDIA_DESC_REC_FIRST 534 /* TEXTS[4*topic + 534..537], the four description records */
#define PUFO_UFOPAEDIA_PAL_SLOT        17 /* Palette_Load's slot for "<entry>.pal" (0x9A584) */
#define PUFO_UFOPAEDIA_PAD_MODE        17 /* PadImage_Load's palette slot (0x9A5A4) */

typedef struct UfopaediaEnv {
    UiPanel   *apCatPanels[PUFO_UFOPAEDIA_CATEGORIES]; /* g_apUfopaediaCatPanels @ 0x1871D0 */
    UiPanel   *pTextPanel;      /* g_pUfopaediaTextPanel @ 0x187200, 50 lines */
    i32       *pnEntryIdx;      /* 0x1871F4, the entry slot inside the current category */
    i32       *pnCategory;      /* g_nUfopaediaCategory @ 0x1871F8, 0..8 */
    i32       *pnTopic;         /* g_nUfopaediaTopic @ 0x187300 */
    i32       *pnEntryNum;      /* g_nUfopaediaEntryNum @ 0x1872D8 */
    u8        *pbWidePanel;     /* g_bUfopaediaWidePanel @ 0x1871FC, shared with the geoscape pointer's 16-mode panel set */
    u8        *pbPadDirty;      /* g_bUfopaediaPadDirty @ 0x187304 */
    u8        *pbEntryDirty;    /* bUfopaediaEntryDirty @ 0x187310 */
    u8        *pbReselect;      /* Ufopaedia_bReselectPending @ 0x18706C */
    PadImage **ppPad;           /* g_pUfopaediaPad @ 0x1872E4 */
    const ResearchTopicLive *aTopics;      /* aResearchTopics @ 0x15EF08 */
    u8        *abResearchZero;  /* abResearchZero @ 0x160B2C, the unlock mirror. Not const:
                                 * Ufopaedia_Open writes it at 0x9AD68 / 0x9B344. */
    const u8  *pTextRecs;       /* aTextRecs @ 0xB38D0, stride PUFO_TEXT_REC_STRIDE */

    /* The three fields only Ufopaedia_Open @ 0x9a8cc uses. */
    UiPanel   *pSelPanel;       /* g_pUfopaediaSelPanel @ 0x187204, the 9-line category selector */
    PadImage  **ppScreenPad;    /* g_pUfopaediaScreenPad @ 0x1872E0, the screen's own pad */
    UiControl **apCtl;          /* 0x1872E8..0x1872FC, the six controls the teardown releases */
    u8        *pbOpen;          /* unk_187308, the build/teardown toggle */
    void (*pfnSelClick)(void);  /* 0x9A958 installs Ufopaedia_CacheSelRow @ 0x99fd0 */
    void (*pfnCatClick)(void);  /* 0x9AE1C installs Ufopaedia_RefreshEntry @ 0x9a014 */
} UfopaediaEnv;

/* 0x9A014. Re-picks the current entry when neither the pointer nor a paging step has already
 * chosen one, lays the text panel out for the entry number, word-wraps the four description
 * records into it, and creates or frees the entry's pad.
 *
 * The wrap loop at 0x9A308..0x9A4D0 is a byte-for-byte inline clone of UiPanel_WrapText
 * @ 0x37144 -- same cursors, same `psz[++v7 - v9] = 0` break, same trailing space appended to
 * the working copy, same `nLineCount = nLineStart + count` tail. The port calls the shared
 * function instead of duplicating it; the trailing space UiPanel_WrapText appends is the one
 * space 0x9A2FC appends here. */
void Ufopaedia_RefreshEntry(UfopaediaEnv *pEnv);

/* 0x9A60c / 0x9A754. Page to the previous / next entry, wrapping the entry index into the
 * neighbouring category and skipping entries whose research topic is not unlocked. Both end by
 * dropping the current pad and calling Ufopaedia_RefreshEntry. */
void Ufopaedia_PrevEntry(UfopaediaEnv *pEnv);
void Ufopaedia_NextEntry(UfopaediaEnv *pEnv);

/* ---- Ui_DrawBaseInfo @ 0x9b5f4 (10,196 B): the ufopaedia screen draw -------------
 *
 * Reached from the geoscape draw dispatch (code xref 0x941B4, screen table entry at
 * 0x18AC80) while the ufopaedia screen is up. The draw has four layers:
 *
 *  1. The screen pad (ppScreenPad[0]) is always blitted first.
 *  2. When the entry is NOT dirty, a dashed frame is drawn instead of the entry: 24 vertical
 *     1x7 dashes at x 5 and 234 (y 1,8,..,162 and 152,..,313) and 24 horizontal 5x1 dashes at
 *     y 5 and 315 (x 1,6,..,116 and 120,..,235). The dash colour ramps with the ITERATION i:
 *     ((i + 20) * 5) on the near pair and (230 - 5*i) vertically / (250 - 5*i) horizontally on
 *     the far pair, and the same byte is used as index, mid and low -- the dashes fade along
 *     the frame.
 *  3. The close control (pCloseCtl) is always blitted. Then three gates: screen not built ->
 *     return; ufopaedia not open -> draw the selector panel and return; entry not dirty -> the
 *     category-panel gate (if the current category panel has lines, draw the selector panel,
 *     else return).
 *  4. Entry dirty: the screen is filled (BlitSrcRect 0,0,240,320 idx 0x64 mid 0x64 low 0xC8
 *     key 0x50), a solid 5px frame is drawn, the entry number is NORMALISED into a category
 *     plus a sub-index, and a dispatch draws that entry kind. The normalisation writes both
 *     *pnEntryNum and *pnSub:
 *      - 54..75  -> category 3 (sub untouched)
 *      - > 0xBB7 (3000 and up) -> category 2, sub = n - 3000
 *      - 1001..1999 -> category 1, sub = n - 1000
 *      - 2001..2999 -> category 26
 *      - 76..84 -> category 4
 *     Categories 1,2,3,4,26,85..91,92 and 93..103 each have their own drawing block; anything
 *     else falls through to the close-control tail.
 *
 * The entry blocks read the object record apObjects[sub] (the ufopaedia "entry data" IS the
 * object record; the original copies its first 0xD0 bytes into two scratch blocks and mutates
 * the copies -- the port keeps the copies as locals and names the fields it reads). The
 * category-3 block reads aBaseFacFlags[sub].uCost and aFacBuildSpec; the 93..103 block fills
 * the text panel rows through UiPanel_SetBytes6to11.
 *
 * Deviations recorded in the parity notes: the scratch copies are locals (nothing else in the
 * binary reads 0x187100 / 0x187208 -- both have only this function's xrefs); the original's
 * unguarded apObjects[sub] / aBaseFacFlags[sub] indexing is bounds-checked by the port. */
typedef struct UiBaseInfoDrawEnv {
    PadImage  **ppScreenPad;     /* g_pUfopaediaScreenPad @ 0x1872E0: [0] the screen pad, [1] g_pUfopaediaPad @ 0x1872E4 */
    UiControl *pCloseCtl;        /* g_pUiCloseCtl */
    UiControlBlitFn pfnCtlBlit;  /* the close control's pad blit, injected as everywhere else */
    UiControlBlitFn pfnCtlBlitEx;
    void           *pCtlUser;
    u8        *pbEntryDirty;     /* bUfopaediaEntryDirty @ 0x187310 */
    u8        *pbScreenBuilt;    /* g_bUfopaediaScreenBuilt @ 0x187308 */
    u8        *pbUfopaediaOpen;  /* bUfopaediaOpen @ 0x18730C */
    i32       *pnEntryNum;       /* g_nUfopaediaEntryNum @ 0x1872D8, normalised in place */
    i32       *pnSub;            /* g_nUfopaediaEntrySub @ 0x1872DC, derived by the normalisation */
    i32       *pnTopic;          /* g_nUfopaediaTopic @ 0x187300 */
    i32       *pnCategory;       /* g_nUfopaediaCategory @ 0x1871F8 */
    UiPanel   **apCatPanels;     /* g_apUfopaediaCatPanels @ 0x1871D0 */
    UiPanel   *pSelPanel;        /* g_pUfopaediaSelPanel @ 0x187204 */
    UiPanel   *pTextPanel;       /* g_pUfopaediaTextPanel @ 0x187200 */
    const ResearchTopicLive *aTopics;  /* aResearchTopics @ 0x15EF08; wGate4 is the name record */
    const char *pTextRecs;       /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    const BaseFacFlag *paFac;    /* aBaseFacFlags @ 0x186E48, 28-byte records */
    const ObjectRecFull *const *apObjects; /* apObjects @ Map+0x108: the entry data */
    i32        nObjectCount;   /* Map_nObjectCount @ 0x114A60, the apObjects bound */
    const FacBuildSpec *paBuildSpec;       /* aFacBuildSpec, 18 records */
    PufoFont  *pFont;            /* fontSys: nBigObsSprBase / fontSys_nBaseBitsSprBase */
    i32       *pnGlyphPalBase;   /* Map_nGlyphPalBase @ Map+0x4A3C */
    const GfxColorEnv *pColor;   /* the pairs Gfx_PackRgb565Pair writes */
} UiBaseInfoDrawEnv;

void Ui_DrawBaseInfo(UiBaseInfoDrawEnv *pEnv);

/* ---- Ufopaedia_Open @ 0x9a8cc: the builder and the teardown ----------------------
 *
 * One function does both, on a toggle. unk_187308 clear builds the screen and LEAVES THE FLAG SET;
 * the build path returns from inside its last loop (0x9B4FC), so the release code below it only
 * runs on the next call, which clears the flag first. Calling it twice is open-then-close.
 *
 * Build: a 9-line selector panel, a 50-line text panel, then nine category panels. Each category
 * panel is constructed with the FULL number of shown entries -- the ufopaedia pages through all of
 * them -- but nStyle, the visible row count, is clamped to 13 (0x9AE44) and the panel is centred at
 * posY = 160 - 8 * visible (0x9AE58..0x9AE6C).
 *
 * Which entries are shown is a two-part test, evaluated twice (a counting pass at 0x9AB04 that
 * sizes the panel, then a filling pass at 0x9B0C4 that writes the names):
 *   (aResearchTopics[topic].bUnlocked != 0 && topic != 0) || raceOverride != 0
 * The override is non-zero only in category 5, only for the five race topics 55..59, and only when
 * the race is "known" -- any of a hand-written list of its encounter topics is unlocked. A known
 * race also writes abResearchZero[topic] = 1 (0x9AD68 / 0x9B344), so opening the ufopaedia is what
 * puts a met race into the research mirror; that is why the paging gate accepts on that byte.
 *
 * The name written into the row is TEXTS[aResearchTopics[topic].wGate4], and for the five race rows
 * it is then overwritten with a literal race name (0x9B3D8..0x9B4E0). */

#define PUFO_UFOPAEDIA_RACE_CATEGORY     5   /* 0x9AB3C: only this category has the race override */
#define PUFO_UFOPAEDIA_RACE_COUNT        5   /* topics 55..59 */
#define PUFO_UFOPAEDIA_RACE_TOPIC_FIRST 55   /* Sectoid, Snakeman, Ethereal, Muton, Floater */
#define PUFO_UFOPAEDIA_SEL_LINES         9   /* UiPanel_ctor's line count for the selector */
#define PUFO_UFOPAEDIA_SEL_REC_FIRST     1646 /* the selector's nine labels, TEXTS 1646..1654 (0x105EE6
                                               * .. 0x10654E). 1646..1649 read "не исп." in lang.dat;
                                               * 1650..1654 are past what TEXTS.DAT fills, so empty. */
#define PUFO_UFOPAEDIA_TEXT_LINES_BUILT 50   /* and for the text panel */
#define PUFO_UFOPAEDIA_CAT_VISIBLE_MAX  13   /* 0x9AE44 clamps nStyle, not the panel's line count */
#define PUFO_UFOPAEDIA_CAT_CENTRE_Y    160   /* posY = 160 - 8 * visible rows */
#define PUFO_UFOPAEDIA_CTL_COUNT         6   /* 0x1872E8..0x1872FC */

/* The encounter topics that make each race appear, in the order the original's AND-chains list
 * them, fallback last. The five families are 118..142 by residue mod 5, and two of the chains are
 * incomplete: Ethereal never mentions 142 and Muton never mentions 135 or 140. -1 is an empty slot. */
extern const i32 g_anRaceKnownTopics[PUFO_UFOPAEDIA_RACE_COUNT][6];
extern const char *const g_apszUfopaediaRaceName[PUFO_UFOPAEDIA_RACE_COUNT];

/* 0x9A8CC. Builds the screen when the toggle is clear, releases it when it is set. */
void Ufopaedia_Open(UfopaediaEnv *pEnv);

extern const u8  g_aUfopaediaCatTopics[PUFO_UFOPAEDIA_CATEGORIES][PUFO_UFOPAEDIA_CAT_WIDTH];
extern const i32 g_anUfopaediaEntryNum[PUFO_RESEARCH_TOPIC_COUNT];

/* ---- Ui_InitBaseInfo @ 0x9ddc8 (744 B): the base-info screen's bring-up ------------------
 *
 * Evidence: the ARM at 0x9DDC8..0x9E088. Two callers reach it, both with the screen block
 * g_pUfopaediaScreenPad @ 0x1872E0 as the argument: Ui_OpenBaseInfo tails into it at 0x84BD4 and
 * Ui_OpenBaseInfoAlt at 0x84DF8.
 *
 * The block is a UiControl* array shared with the ufopaedia, and this function writes only part
 * of it: [0] the screen's own pad, [2..7] the six controls at 0x1872E8..0x1872FC that
 * Ufopaedia_Open's teardown releases. Slot 1 -- g_pUfopaediaPad @ 0x1872E4, the current entry's
 * pad -- is never touched here; Ufopaedia_RefreshEntry owns it.
 *
 * 1. 0x9DDD4..0x9DE14: operator new(0x14) + PadImage_ctor into [0], PadImage_Load("baseinfo", 14),
 *    then the pad's two origins zeroed. The two stores are STR R7,[R3] and STR R7,[R3,#4] through
 *    the pad pointer -- PadImage +0/+4, the same pair the research bring-up clears at 0x749EC.
 * 2. 0x9DE18..0x9DE48: six operator new(0x20) + UiControl_ctor, stored at ppRoot + 8 stepping by 4,
 *    i.e. slots 2..7.
 * 3. 0x9DE5C..0x9DE98: their handlers in slot order -- UiPanel_ScrollPrev (0x99f88) and
 *    UiPanel_ScrollNext (0x99f78) on the text panel, Ufopaedia_PrevEntry (0x9a60c) and
 *    Ufopaedia_NextEntry (0x9a754), then UiPanel_ScrollPrevSel (0x99fb4) and
 *    UiPanel_ScrollNextSel (0x99f98) on the category panel of the current category.
 * 4. 0x9DE9C..0x9DEC0: UiControl_Setup(slot 2+i, 0, 0, &g_aUiBaseInfoCtlDesc[i]). The loop walks the
 *    slots with ADD R4,R4,#4 and the descriptors with ADD R5,R5,#0x20, so each control gets its own
 *    record; both bases are zero (MOV R1,#0 / MOV R2,#0).
 * 5. 0x9DED0..0x9DEE8: the shared close control g_pUiCloseCtl to (224, 1) with Geoscape_CloseUI_ThunkC
 *    -- the same corner the research and manufacturing bring-ups give it.
 * 6. 0x9DEEC: Ufopaedia_Open, which builds the nine category panels, the selector and the text panel.
 * 7. 0x9DEF4..0x9E084: the reselect. When Ufopaedia_bReselectPending (0x18706C) is set --
 *    Ui_OpenBaseInfoAlt sets it on the way in -- the ufopaedia is re-pointed at the topic the
 *    research screen last showed:
 *      0x9DF18: a 25-case switch on nResearchTopicShown - 0x76. Five arms fold the 25 alien-encounter
 *        topics onto the five race topics, but the jump table sends cases 17 and 22 (135 and 140, the
 *        two Muton encounters the race chain also omits) to the default arm, which leaves the topic
 *        alone. Topics outside 118..142 take the default arm too (ADDLS is an unsigned test).
 *      0x9DFE0..0x9E01C: a linear walk of aUfopaediaCatTopics, 22 cells per category, 198 cells in
 *        all, comparing with LDRB. Zeros are ordinary cells for this walk: topic 0 lands on the
 *        first zero of row 0.
 *      0x9E020..0x9E084: category (STRB, a byte), pad dirty = 1, entry dirty = 0, entry index,
 *        entry number = anUfopaediaEntryNum[topic], topic, bUfopaediaOpen (0x18730C) = 1, then
 *        Ufopaedia_RefreshEntry and the pending flag cleared. Setting pad dirty is what keeps
 *        RefreshEntry's own re-pick at 0x9A054 from overwriting the values just written.
 *
 * g_aUiBaseInfoCtlDesc @ 0xAC548: six 32-byte records. The panel scroll pair sits at x 142/172 y 210,
 * the ufopaedia page pair at x 10/40 y 210, and the category selector pair at x 200 y 18/38. All six
 * use mode 0 and the third pad name is empty.
 *
 * The pad name and mode are the same "baseinfo" / 14 the base roster overlay loads (see
 * BaseInfoScreen_Init @ 0x57688), and the six controls are the same six Ufopaedia_Open releases.
 *
 * Deviation: the original allocates the pad and the six controls with operator new and derefs every
 * allocation with no test; the port takes caller-owned controls and installs them into ppRoot[2..7],
 * as the research, manufacturing, buy/sell and transfer inits do. The pad stays port-allocated
 * because Ufopaedia_Open's teardown releases exactly that slot. */
#define PUFO_UIBASEINFO_CLOSE_X 224        /* 0x9DED0: STRH #0xE0 */
#define PUFO_UIBASEINFO_CLOSE_Y 1          /* 0x9DEDC: STRH #1 */
#define PUFO_ALIEN_TOPIC_FIRST  118        /* 0x9DF18: SUB R0,R2,#0x76, switch 25 cases */
#define PUFO_ALIEN_TOPIC_LAST   142        /* 0x76 + 0x18 */
#define PUFO_ALIEN_TOPIC_NOFOLD 135        /* jump-table case 17: the default arm */
#define PUFO_ALIEN_TOPIC_NOFOLD2 140       /* jump-table case 22: the default arm */
/* The walk's failure state. Five research topics -- 47, 94, 95, 96 and 143 -- are in no category
 * row, and the original walks off the end of the table: it leaves the category counter at 9 and the
 * index at 22 and then reads aUfopaediaCatTopics[9][22], the byte at 0xAA90C, which belongs to the
 * table that follows. The port keeps the same counters and names that byte instead of reading out of
 * bounds. Recorded in docs/parity/bugs.tsv. */
#define PUFO_UFOPAEDIA_MISS_CATEGORY  9
#define PUFO_UFOPAEDIA_MISS_ENTRY_IDX 22
#define PUFO_UFOPAEDIA_MISS_TOPIC     10   /* the byte at 0xAA90C = aUfopaediaCatTopics[9][22] */

typedef struct UiBaseInfoInitEnv {
    void      **ppRoot;             /* g_pUfopaediaScreenPad @ 0x1872E0: [0] the pad, [2..7] the controls */
    UiControl *apCtl[PUFO_UFOPAEDIA_CTL_COUNT]; /* caller-owned, stored into ppRoot[2..7] */
    UiControl *pCloseCtl;           /* g_pUiCloseCtl */
    UfopaediaEnv *pUfopaedia;       /* the state step 6 builds and step 7 re-points */
    i32 *pnShownTopic;              /* nResearchTopicShown @ 0x160C60 */
    u8  *pbUfopaediaOpen;           /* bUfopaediaOpen @ 0x18730C, the "a page is up" flag */
    void (*pfnPanelPrev)(void);     /* UiPanel_ScrollPrev @ 0x99f88 */
    void (*pfnPanelNext)(void);     /* UiPanel_ScrollNext @ 0x99f78 */
    void (*pfnEntryPrev)(void);     /* Ufopaedia_PrevEntry @ 0x9a60c */
    void (*pfnEntryNext)(void);     /* Ufopaedia_NextEntry @ 0x9a754 */
    void (*pfnSelPrev)(void);       /* UiPanel_ScrollPrevSel @ 0x99fb4 */
    void (*pfnSelNext)(void);       /* UiPanel_ScrollNextSel @ 0x99f98 */
    void (*pfnCloseClick)(void);    /* Geoscape_CloseUI_ThunkC @ 0x9a8c0 */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pPadUser;
} UiBaseInfoInitEnv;

void Ui_InitBaseInfo(UiBaseInfoInitEnv *pEnv);

/* ------------------------------------------------- the psi screen (geoscape screen 17)
 *
 * Evidence: PsiScreen_Init @ 0x72b40, PsiScreen_BuildPanels @ 0x716d4,
 *           PsiScreen_SelectListed @ 0x71298, PsiScreen_ApplyAssignments @ 0x72250.
 *
 * ## Four parallel per-base columns
 *
 * The screen lists the soldiers stationed at the selected base (byte_1870F4, read SIGNED) in four
 * columns that scroll together through the already ported PsiScreen_PanelPrev/Next. Each column is
 * its own UiPanel and each column is an array of PUFO_FAC_BASE_COUNT entries indexed by base:
 *
 *   unk_15EED0 @ 0x15EED0   the soldier name,   nPosX 10, nRectX   5, nViewW 230
 *   unk_15EE88 @ 0x15EE88   "auStat[6]\auStat[7]", nPosX 154, nRectX 175, nViewW 10
 *   unk_15EEA8 @ 0x15EEA8   the training delta, nPosX 184, nRectX 209, nViewW 10
 *   unk_15EE58 @ 0x15EE58   the assignment box, nPosX 215, nRectX 210, nViewW 10
 *
 * The middle two are not separate globals in the image: they are the dword runs at byte_15EE84+4 and
 * byte_15EE84+36. That same 72-byte block also holds the row-selected flag at +0 (byte_15EE84[0], set
 * by PsiScreen_Init at 0x72C70 and cleared by the screen-17 branch of Geoscape_OnPointerUp at
 * 0x890C4) and a scratch row index at +68 that PsiScreen_SelectListed uses to hand one row to its
 * four SetLineMeta calls. The port keeps four named arrays plus the two scalars.
 *
 * All four share nStyle 14 (14 visible rows), nFont 16, nPosY / nRectY of 70 and the same
 * SetBytes0to2(B4,B4,B4) and SetBytes6to11(F0,F0,F0,14,14,14). Only the name column gets
 * SetBytes3to5(32,32,FA), bSelLineHighlight = 1 and bColorKey = 0x96 (0x718C0..0x718F4); only the three
 * narrow columns get nUnk7076 = 1 (0x719CC, 0x71AA0, 0x71B68). The name column is also the only one
 * with a click handler (0x7183C, PsiScreen_SelectListed).
 *
 * ## What the rows carry
 *
 * anLineAux is the row payload and each column uses it for something different:
 *   name    the SOLDIER INDEX into Map.apSoldiers (0x71C6C), not a base-relative slot
 *   check   1 when the soldier is assigned to this base's psi lab, 0 otherwise
 *   delta   the training total: 30 * auStat[7] / 100, or 1 when auStat[6] is already >= auStat[7]
 *   stat    unused
 *
 * PsiScreen_ApplyAssignments is what turns the check column back into game state, and the delta
 * column is what it feeds into Soldier.nPsiTrainDays @ 0x1A7 (read with LDRSB at 0x7230C).
 *
 * ## The row colours
 *
 * UiPanel_SetLineMeta's first triple marks the row: (240, 120, 240) when assigned, (240, 240, 240)
 * when not; the second triple is (10, 10, 10) either way. The original passes the third byte of the
 * first triple as -16, which is 240 once it is stored as a byte. BuildPanels applies the assigned
 * colour only, on the rows it fills (0x71D1C..0x71D9C); SelectListed applies whichever colour the
 * row is moving to, on all four columns (0x7137C..0x713FC or 0x714D4..0x71554).
 *
 * ## BuildPanels is a build/teardown toggle, like Ufopaedia_Open
 *
 * byte_15EEF0 clear means build: it is set at 0x716F0 and never cleared by the build arm. Set means
 * release: nGeoscapeScreen @ 0x186BC4 goes to 0 at 0x7212C, the flag clears at 0x72134, and the
 * screen pad, the four panels of every base and the two scroll controls are freed. The same flag is
 * what Base_HitTestFacilityStrip @ 0x671b0 and the geoscape pointer handlers test to disable the
 * base-tab strip while the screen is open.
 *
 * Building runs two passes over the roster. The counting pass (0x71734) sizes each base's four
 * panels to the number of soldiers whose Soldier.nKind equals that base; the filling pass (0x71BD8)
 * rezeroes the same array as a row counter and writes the rows. So a base's panel holds exactly its
 * own roster, and the row order is Map.apSoldiers order.
 *
 * ## The third argument at 0x72370
 *
 * Base_UpdatePsiLabNeeded takes its budget as its third argument, and its two other call sites load
 * Base_CountPsiSoldiers into R2 first (Geoscape_CloseUI 0x8BA4C, Game_StartNew 0x953CC). This call
 * site never writes R2: the last write is the inner loop's `ADD R2, R2, #1` at 0x7235C, so the
 * budget is the loop's exit value -- the base's whole row count, or 0 when the panel is empty. The
 * port reproduces the residue and records it in docs/parity/bugs.tsv. */
#define PUFO_PSI_PAD_NAME          "Psi"  /* aPsi @ 0xABB0C, loaded at 0x72B74 */
#define PUFO_PSI_PAD_MODE           19    /* the palette slot PadImage_Load takes */
#define PUFO_PSI_PANEL_STYLE        14    /* nStyle: 14 visible rows */
#define PUFO_PSI_PANEL_FONT         16
#define PUFO_PSI_ROW_Y              70    /* nPosY and nRectY on all four columns */
#define PUFO_PSI_NAME_X             10
#define PUFO_PSI_STAT_X            154
#define PUFO_PSI_DELTA_X           184
#define PUFO_PSI_CHECK_X           215
#define PUFO_PSI_NAME_VIEWW        230
#define PUFO_PSI_NARROW_VIEWW       10
#define PUFO_PSI_STAT_SEP         "\\"    /* asc_AB9F0 @ 0xAB9F0 */
#define PUFO_PSI_DELTA_PREFIX     "+"     /* asc_AAF90 @ 0xAAF90 */
#define PUFO_PSI_DELTA_FULL       "+1"    /* unk_ABB08 @ 0xABB08 */
#define PUFO_PSI_TRAIN_PCT          30    /* 0x71F7C and 0x72090: 30% of auStat[7] */
#define PUFO_PSI_ROW_ASSIGNED_1    120    /* the assigned row's middle colour */
#define PUFO_PSI_ROW_FREE_1        240    /* and the unassigned row's */
#define PUFO_PSI_ROW_EDGE          240    /* -16 as the original passes it, stored as a byte */
#define PUFO_PSI_ROW_SECOND        10     /* the second triple, both colours */
#define PUFO_PSI_SCROLL_CTLS        2

typedef struct PsiScreenEnv {
    UiPanel   *apNamePanel[PUFO_FAC_BASE_COUNT];   /* unk_15EED0 @ 0x15EED0 */
    UiPanel   *apStatPanel[PUFO_FAC_BASE_COUNT];   /* unk_15EE88 @ 0x15EE88 */
    UiPanel   *apDeltaPanel[PUFO_FAC_BASE_COUNT];  /* unk_15EEA8 @ 0x15EEA8 */
    UiPanel   *apCheckPanel[PUFO_FAC_BASE_COUNT];  /* unk_15EE58 @ 0x15EE58 */
    PadImage  *pScreenPad;                         /* unk_15EE78 */
    UiControl *apScrollCtl[PUFO_PSI_SCROLL_CTLS];  /* 0x15EE7C prev, 0x15EE80 next */
    u8        *pbBuilt;                            /* byte_15EEF0 */
    u8        *pbRowSel;                           /* byte_15EE84[0] */
    i32       *pnScratchRow;                       /* byte_15EE84[68] */
    i8        *pnBaseIdx;                          /* byte_1870F4, read SIGNED */
    const i8  *pnBaseCount;                        /* nBaseCount @ 0x159D80, read SIGNED */
    u8        *abFacilityGrid;                     /* abFacilityGrid @ 0x159768 */
    const u8   *pTextRecs;                         /* aTextRecs @ 0xB38D0, the check column's labels */
    Soldier  **apSoldiers;                         /* Map.apSoldiers @ 0x114B50 */
    i32        nSoldierCount;                      /* nSoldierCount @ 0x115350 */
    i32       *pnActionFail;                       /* dwActionFail @ 0x1870C8 */
    i32       *pnResidue;                          /* dword_1870B4[0], the shortfall reason */
    i32       *pnGeoscapeScreen;                   /* nGeoscapeScreen @ 0x186BC4 */
    UiControl *pCtlOk;                             /* unk_186BA4, x 1 y 300 */
    UiControl *pCtlApply;                          /* unk_186BAC, x 120 y 300 */
    void   (*pfnOkClick)(void);                    /* PsiScreen_OnOk @ 0x716a8 */
    void   (*pfnApplyClick)(void);                 /* PsiScreen_ApplyAssignments @ 0x72250 */
    void   (*pfnSelClick)(void);                   /* PsiScreen_SelectListed @ 0x71298 */
    void   (*pfnScrollPrev)(void);                 /* PsiScreen_PanelPrev @ 0x715e8, into 0x15EE7C */
    void   (*pfnScrollNext)(void);                 /* PsiScreen_PanelNext @ 0x71648, into 0x15EE80 */
    bool   (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                          const char *psz2, void *pUser);
    void    *pPadUser;
} PsiScreenEnv;

/* stru_ABAC8 @ 0xABAC8, the two scroll buttons PsiScreen_Init sets up. */
extern const UiControlDesc g_aPsiScrollDesc[PUFO_PSI_SCROLL_CTLS];

/* 0x716d4. Builds the four columns when byte_15EEF0 is clear, releases them when it is set. */
void PsiScreen_BuildPanels(PsiScreenEnv *pEnv);

/* 0x71298. Toggles the selected row's psi-lab assignment. False when the base has no free psi-lab
 * capacity for one more soldier; that path sets dwActionFail and clears dword_1870B4[0]. */
bool PsiScreen_SelectListed(PsiScreenEnv *pEnv);

/* 0x72250. Writes the check column into Soldier.bPsiAssigned / nPsiTrainDays per base, refreshes
 * each base's psi-lab needed flag, then rebuilds the panels. */
void PsiScreen_ApplyAssignments(PsiScreenEnv *pEnv);

/* 0x72b40. The screen's bring-up: the "Psi" pad, the OK and Apply buttons, the two scroll controls,
 * then the build arm of PsiScreen_BuildPanels. */
void PsiScreen_Init(PsiScreenEnv *pEnv);

/* PsiScreen_Draw @ 0x72398 (1,960 B). Runs only while g_bPsiPanelOpen is set.
 *
 * The frame is the shared ramped rails (BaseUi_DrawFrameRails): the same 24 steps of 7 on the sides and
 * 24 steps of 5 on the top and bottom that TransferScreen_Draw, ManufScreen_DrawFrame and
 * ResearchScreen_Draw each carry. Then the base name of byte_158834 centred at (120, 13), the four
 * columns of the base a2 selects, and the two geoscape controls.
 *
 * Four captions, all TEXTS records at aTextRecs @ 0xB38D0 -- an earlier IDB note called them scratch
 * buffers nothing writes, which is wrong: the shipped lang.dat has text in every one.
 *   TEXTS[1188] "Имя: Пси навык/сила: Доб.навык: Обуч" at (7, 54)      -- the four column captions
 *   TEXTS[1189] "Выберете базу" centred at (60, 300)
 *   TEXTS[1127] "ОК" centred at (180, 300)                              -- the same record the
 *                                                                        manuf/research/buy-sell frames use
 *   TEXTS[1190] "Нет места в Пси-лабораториях" centred at (120, 158)   -- the shortfall panel
 *
 * Overlay (byte_1870F8): the full-screen blend, the 32-wide strip at x 208, Base_DrawFacilityIconStrip
 * with the mark argument -1 (no chevron), the 210-wide panel, then one bevelled row per base with its
 * name centred at (110, 28*i + 7). A base whose Base_CalcPsiLabCap is 0 gets the 23-step white chevron
 * at x 212..234 -- the same bracket the strip drawer paints for a marked base, here marking the ABSENCE
 * of a psi lab.
 *
 * Tail. With the overlay closed the shortfall panel runs unconditionally (0x72760 branches straight to
 * 0x72A94, skipping the base compare). With it open: byte_158834 == byte_1870F4 runs the same panel,
 * otherwise a non-zero Base_CalcPsiLabCap(byte_158834) adopts that base -- byte_1870F4 takes it and
 * byte_1870F8 clears. */
#define PUFO_PSI_TEXT_ASSIGNED   1186  /* unk_EEE8A "Обучение", the assigned check-column row */
#define PUFO_PSI_TEXT_UNASSIGNED 1187  /* unk_EEF57 "Нет", the unassigned one */
#define PUFO_PSI_TEXT_COLUMNS    1188  /* the four column captions, at (7, 54) */
#define PUFO_PSI_TEXT_SEL_BASE   1189  /* "Выберете базу", centred at (60, 300) */
#define PUFO_PSI_TEXT_OK         1127  /* "ОК", centred at (180, 300) */
#define PUFO_PSI_TEXT_NO_ROOM    1190  /* the shortfall caption, centred at (120, 158) */
#define PUFO_PSI_BASE_ROW_PITCH   28   /* one base row per 28 pixels in the overlay list */
#define PUFO_PSI_CHEVRON_STEPS    23   /* the white bracket at x 212..234 */

typedef struct PsiScreenDrawEnv {
    const GfxColorEnv *pColor;             /* pGX @ 0x1139B8, the colour-pair owner */
    UiAnimBlitEnv *pAnim;                  /* the geoscape anim root, for the shortfall panel */
    const u8 *pbPanelOpen;                 /* g_bPsiPanelOpen @ 0x15EEF0, the gate */
    PadImage *pScreenPad;                  /* g_pPsiScreenPad @ 0x15EE78 */
    UiPanel *apPanels[4];                  /* the four columns of nBaseIdx, in PsiScreenEnv order */
    i8 nBaseIdx;                           /* a2: byte_1870F4, LDRSB at Geoscape_Draw 0x94290 */
    i8 nWorkBaseIdx;                       /* byte_158834, the base the geoscape screens work on */
    i8 *pnPsiBaseIdx;                      /* byte_1870F4, written when a base is adopted */
    u8 *pbOverlayOpen;                     /* byte_1870F8, cleared when a base is adopted */
    UiControl *pCtlConfirm;                /* pGeoscapeCtlConfirm @ 0x186BA4 */
    UiControl *pCtlApply;                  /* pGeoscapeCtlApply @ 0x186BAC */
    UiControlBlitFn pfnCtlBlit;
    UiControlBlitFn pfnCtlBlitEx;
    void *pCtlUser;
    const char *pTextRecs;                 /* aTextRecs @ 0xB38D0, stride 205 */
    const BaseSiteRec *apSiteRecs;         /* 0x186C40, eight 64-byte records; the name is abName at +26 */
    const i8 *pnBaseCount;                 /* nBaseCount @ 0x159D80, read SIGNED */
    const u8 *abFacilityGrid;              /* abFacilityGrid @ 0x159768 */
    const i32 *pnActionFail;               /* dwActionFail @ 0x1870C8 */
    const i32 *pnResidue;                  /* dword_1870B4[0], the shortfall reason */
    void (*pfnDrawIconStrip)(i32 nBaseIdx, void *pUser);
    void *pStripUser;
} PsiScreenDrawEnv;

void PsiScreen_Draw(const PsiScreenDrawEnv *pEnv);

/* ------------------------------------------------- the base info screen
 *
 * BaseInfoScreen_Init @ 0x57688 (312 B) and BaseInfoScreen_BuildPanels @ 0x557f4 (6,084 B).
 *
 * Init brings up the screen's own pad and two controls, then tail-calls BuildPanels:
 *   - a PadImage at 0x158828 (IDA's pBaseInfoPad), loaded "baseinfo" at mode 14 with both origins 0
 *     (0x57694..0x576D4).
 *   - two UiControls at 0x15882C and 0x158830 (0x576DC..0x57708). Their clicks are BaseInfoScreen_OnOk
 *     (0x557C8) on the FIRST and BaseInfoScreen_TogglePanelMode (0x55754) on the second (0x5771C /
 *     0x5772C) -- the order the release arm of BuildPanels then frees them in.
 *   - the shared geoscape close control g_pUiCloseCtl moved to (224, 1) with BaseInfoScreen_CloseThunk
 *     (0x57738..0x57750).
 *   - UiControl_Setup over the two 32-byte descriptors at 0xAB198: (1, 300) and (120, 300), both with
 *     the pad names "emptyb" and "cemptyb" and an empty third (0x57754..0x57774).
 *   - byte_1870F4 takes nSelectedBaseIdx and byte_1870F8 is cleared (0x5778C / 0x57794), so the base
 *     info screen always works on the base the geoscape has selected, and the base-select overlay
 *     starts closed.
 *
 * BuildPanels is the same build/release toggle on bBaseInfoPanelsBuilt @ 0x15883C the psi and ufopaedia
 * screens use. The release arm (0x56F0C..0x56F90) frees the three panels and the two controls at
 * 0x15882C / 0x158830 -- and NOT the pad at 0x158828, which Geoscape_CloseUI @ 0x8BE00 releases. The
 * build arm sets the flag, sets byte_AB190 (the panel-mode flag BaseInfoScreen_TogglePanelMode flips),
 * and builds three 42-line panels.
 *
 * The three panels, all 42 lines, all nStyle 16, nFont 16, nPosY 40, click top 70, no click handler,
 * bytes 0..2 = (B4,B4,B4), bytes 6..11 = (F0,F0,F0,14,14,14), bRowRules and bSelLineHighlight cleared:
 *   - 0x158824, the NAME column: x 10, click left 5, nViewW 200, nUnk7076 left at the ctor's 0.
 *   - 0x158820, the FREE column: x 130, click left 210, nViewW 10, nUnk7076 1.
 *   - 0x158838, the TOTAL column: x 190, click left 210, nViewW 10, nUnk7076 1.
 *
 * All 42 rows of all three panels are first filled from 0x10DA38, and that one really is a clear: it
 * is TEXTS record 1800, past the 1650 records lang.dat loads, so the slot is never written. The fixed
 * captions are strings, not clears. Every one is 0xB38D0 + 205*n with n inside the loaded range, i.e. a
 * lang.dat TEXTS.DAT record -- all 1650 strings, in record order, are in src/pufo/asset/pufo_texts_lang.h.
 * In row order (with the literal the ARM uses, and the shipped text):
 *   0  0x104C7B 1623 "Персонал Работают Всего"      6  0x10507C 1628 "Жилые"
 *   1  0x104D48 1624 "Солдаты"                      7  0x105149 1629 "Хранилища"
 *   2  0x104E15 1625 "Инженеры"                     8  0x105216 1630 "Лаборатории"
 *   3  0x104EE2 1626 "Ученые"                       9  0x1052E3 1631 "Мастерские"
 *   4  szEmptyString                                10 0x1053B0 1632 "Ангары"
 *   5  0x104FAF 1627 "Места Занято Всего"           11 szEmptyString
 *   12 0x10547D 1633 "Обнаружение и защита"         13 0x10554A 1634 "Мощность защиты"
 *   14 0x105617 1635 "Близкое обнаружение"          15 0x1056E4 1636 "Дальнее обнаружение"
 *   16 0x1057B1 1637 "Корабли Обслуживание"
 * and the eight rows after the ship list, nShips+18..nShips+25: 0x10587E 1638 "Персонал Зарплата Итого",
 * 0x10594B 1639 "Солдаты", 0x105A18 1640 "Инженеры", 0x105AE5 1641 "Ученые", szEmptyString, 0x105BB2 1642
 * "Обслуж. базы", szEmptyString, 0x105C7F 1643 "Итого". The meta
 * rows 0, 5, 12, 16 and nShips+18, +23, +25 are the four group headers plus the three tail ones.
 * The only non-TEXTS strings here are the money literals at 0xAB1D8..0xAB20B.
 *
 * Rows 0, 5, 12 and 16 of the name column, and rows nShips+18, nShips+23 and nShips+25, get
 * UiPanel_SetLineMeta(200, 150, -106, 50, 10, 10).
 *
 * The ship list starts at row 17 in all three panels. For every craft in apCraft order whose
 * Ship.bHomeBase equals byte_1870F4 (the base byte read SIGNED at 0x55E6C, the ship byte unsigned at
 * 0x55E70), in order: the name column gets Ship_FormatDisplayName of Ship_FindIndexById(Ship.bId),
 * the name column's aux takes Ship.bCraftType, the free column's aux takes Ship.bId, and the total
 * column's aux is set to 1 -- that 1 is the run marker the three later loops test.
 *
 * The free column's fixed rows: 1 = the soldiers aboard those craft, summed with
 * Craft_CountSoldiersAboard over the marked run (0x5638C). This is the only call site in the binary
 * that passes the craft ID rather than the roster index -- the aux it reads is Ship.bId. 2 and 3 =
 * engineers and scientists not already allocated; 6..9 = living quarters, stores, labs and workshops,
 * each capacity minus what it holds; 10 = Base_CalcHangarCap minus Base_CountInboundTransfers AND
 * Base_CountShipsAtBase (0x56644), the inbound call with nSiteIdx -1 so pending sites are not counted.
 *
 * The ship cost rows (0x566A4..0x5685C) read the name column's aux: 0 and 1 both write "500000$" and
 * add 500000, 2 writes "700000$", 3 "600000$", 4 "400000$", and any other value writes nothing and adds
 * nothing. A zero aux is NOT the end of the list -- 0x56810 handles it and 0x56858 loops on; only the
 * total column's aux ceasing to be 1 ends the run. Hex-Rays renders that zero case as a `break` out of
 * the loop, which is wrong.
 *
 * The money total (0x56958): 20000 * (soldiers + engineers) + 30000 * scientists + the ship total,
 * plus 50000 for every one of the base's 36 facility cells whose type byte is not 255 (0x56A8C..
 * 0x56AC0). The free column shows the facility part alone at nShips+23 and the whole at nShips+25,
 * both with the trailing '$' from asc_AB1D8; the total column shows 20000*soldiers, 20000*engineers
 * and 30000*scientists at nShips+19..21, each with the '$'. Rows 1, 2, 3, 6, 7, 8, 9, 10 and 13 of the
 * total column are the raw counts -- soldiers, engineers, scientists, and the five capacities -- and
 * rows 14 and 15 hold TEXTS[1644] "нет", rewritten to TEXTS[1645] "да" for every cell of the base that is
 * facility kind 4 (FAC_SHORT_RANGE_RADAR) or kind 5 (FAC_LONG_RANGE_RADAR) (0x56E20..0x56ED4): those two
 * rows are the answers to the name column's "Близкое обнаружение" / "Дальнее обнаружение". */
#define PUFO_BASEINFO_PAD_NAME      "baseinfo"  /* aBaseinfo @ 0xAB20C */
#define PUFO_BASEINFO_PAD_MODE      14
#define PUFO_BASEINFO_PANEL_LINES   42   /* every panel is built with 42 lines (0x55844) */
#define PUFO_BASEINFO_PANEL_STYLE   16   /* and shows 16 of them */
#define PUFO_BASEINFO_PANEL_FONT    16
#define PUFO_BASEINFO_PANEL_Y       40
#define PUFO_BASEINFO_CLICK_TOP     70   /* nRectY, the same for all three */
#define PUFO_BASEINFO_NAME_X        10
#define PUFO_BASEINFO_FREE_X        130
#define PUFO_BASEINFO_TOTAL_X       190
#define PUFO_BASEINFO_NAME_CLICKL   5    /* nRectX of the name column */
#define PUFO_BASEINFO_NARROW_CLICKL 210  /* and of the two narrow ones */
#define PUFO_BASEINFO_NAME_VIEWW    200
#define PUFO_BASEINFO_NARROW_VIEWW  10
#define PUFO_BASEINFO_SHIP_ROW      17   /* the first ship row, in all three panels */
#define PUFO_BASEINFO_META_A        200  /* the four UiPanel_SetLineMeta arguments */
#define PUFO_BASEINFO_META_B        150
#define PUFO_BASEINFO_META_C        50
#define PUFO_BASEINFO_META_D        10
#define PUFO_BASEINFO_META_ROW      0x96 /* -106 as the byte the original stores */
#define PUFO_BASEINFO_CRAFT_COST_0  500000 /* the aux 0 and 1 cost, 0x56838 */
#define PUFO_BASEINFO_CRAFT_COST_2  700000
#define PUFO_BASEINFO_CRAFT_COST_3  600000
#define PUFO_BASEINFO_CRAFT_COST_4  400000
#define PUFO_BASEINFO_PRICE_SOLDIER 20000  /* 0x56958 / a20000 @ 0xAB1DC */
#define PUFO_BASEINFO_PRICE_SCIENTIST 30000 /* a30000 @ 0xAB1E4 */
#define PUFO_BASEINFO_PRICE_CELL    50000  /* one built facility cell, 0x56A88 */
#define PUFO_BASEINFO_FAC_EMPTY     0xFF   /* the type byte that means "no facility here" */
/* The two kinds the total column's radar rows test are FacilityKind 4 and 5, FAC_SHORT_RANGE_RADAR and
 * FAC_LONG_RANGE_RADAR from game/pufo_geoscape.h -- there is no base-info name for them. */

typedef struct BaseInfoInitEnv {
    PadImage  **ppPad;              /* pBaseInfoPad @ 0x158828 */
    UiControl **papCtl;             /* 0x15882C (OnOk) then 0x158830 (the panel-mode toggle) */
    UiControl *pCtlClose;           /* g_pUiCloseCtl, the shared geoscape close control */
    void (*pfnOkClick)(void);       /* BaseInfoScreen_OnOk @ 0x557c8 */
    void (*pfnToggleClick)(void);   /* BaseInfoScreen_TogglePanelMode @ 0x55754 */
    void (*pfnCloseClick)(void);    /* BaseInfoScreen_CloseThunk @ 0x557bc */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser);
    void *pPadUser;
    i8   *pnWorkBaseIdx;            /* byte_1870F4 @ 0x1870F4 */
    const i8 *pnSelectedBaseIdx;    /* nSelectedBaseIdx @ 0x159D81 */
    u8   *pbOverlayOpen;            /* byte_1870F8 @ 0x1870F8 */
    void (*pfnBuildPanels)(void);   /* BaseInfoScreen_BuildPanels @ 0x557f4 */
} BaseInfoInitEnv;

void BaseInfoScreen_Init(BaseInfoInitEnv *pEnv);

typedef struct BaseInfoBuildEnv {
    u8      *pbBuilt;               /* bBaseInfoPanelsBuilt @ 0x15883C */
    u8      *pbModeFlag;            /* byte_AB190 @ 0xAB190, set by the build arm */
    UiPanel **pppPanel;             /* [0] 0x158824 names, [1] 0x158820 free, [2] 0x158838 totals */
    UiControl **papCtl;             /* 0x15882C / 0x158830, the two controls the release arm frees */
    Ship *const *apCraft;           /* apCraft @ 0x114A64 */
    const u8 *pnCraftCount;         /* Map_nCraftCount @ 0x114B4C, read UNSIGNED (0x55E38) */
    const i8 *pnWorkBaseIdx;        /* byte_1870F4, read SIGNED (0x55E6C) */
    const u8 *pTextRecs;            /* aTextRecs @ 0xB38D0, for Ship_FormatDisplayName */
    Soldier *const *apSoldiers;     /* apSoldiers @ 0x114B50 */
    i32  nSoldierCount;
    ObjectRecFull *const *apObjects; /* apObjects @ 0x113AC0 */
    i32  nObjectCount;
    const u16 *apManufCol;          /* auManufCol @ 0x15E19C, PUFO_PRODUCT_ENTRIES_PER_BASE per base */
    const u16 *apResearchCol;       /* auResearchCol @ 0x16022C, PUFO_RESEARCH_TOPIC_COUNT per base */
    const u8 *pbEntryCount;         /* 0x160C4C, the shared entry count byte */
    BaseRec *const *apBases;        /* apBases @ 0x114B2C */
    i32  nBaseCount;
    const SmapSite *aSites;         /* aSmapSites @ 0x162030 */
    i32  nSiteCount;
    const u8 *abFacilityGrid;       /* gBaseFacGrid @ 0x159768 */
    const u8 *abGridBytes;          /* 0x159DBC, Base_CountShipsAtBase's extra term */
} BaseInfoBuildEnv;

void BaseInfoScreen_BuildPanels(BaseInfoBuildEnv *pEnv);

/* BaseInfoScreen_Draw @ 0x56fb8 (1,744 B). Everything sits behind bBaseInfoPanelsBuilt.
 *
 * 1. The grey ramp. 24 steps of 5 pixels, x 0..115, each step drawing a 5x1 rect at x+1 in the
 *    left colour and another at x+119 in the right colour, at three y bands. The colours are the
 *    RGB triples (5*(step+20), same, same) on the left and (-6-5*step, same, same) on the right,
 *    both as BYTES: the left ramps 100..215 and the right 250..135. With bBaseInfoPanelMode set
 *    the bands are y 35, 113 and 225; with it clear they are y 35, 18*shipsAtBase+59 and
 *    18*shipsAtBase+169, where shipsAtBase is Base_CountShipsAtBase for byte_1870F4 -- the
 *    original re-reads it on every one of the four calls (0x57118, 0x57158, 0x57198, 0x571D8).
 * 2. UiPanelMsg_Draw on the three columns, in the build order.
 * 3. The base name from the 64-byte site record at byte_186C24 + 64*base + 54, copied into a
 *    60-byte local and centred at (120, 13). Then Gfx_PackRgb565Pair(68, 244, 252, 4, 70, 75) --
 *    the same caption pair the psi frame packs -- and TEXTS[1112] "Выбор базы" centred at (60, 300).
 * 4. The toggle label at (180, 300): TEXTS[1113] "Месячный расход" when the mode flag is set,
 *    TEXTS[1114] "База" when it is clear. All three are real lang.dat records, not scratch.
 * 5. Overlay (byte_1870F8): a 32-wide erase strip at x 208 of height 28*nBaseCount + 4,
 *    Base_DrawFacilityIconStrip with the mark -1, the 210-wide panel, then one bevelled row per
 *    base with its name centred at (110, 28*i + 7). No bracket here, unlike the psi frame.
 * 6. Still inside the overlay: byte_158834 against byte_1870F4. Different, the draw ADOPTS it --
 *    byte_1870F4 = byte_158834, byte_1870F8 = 0, bBaseInfoPanelsBuilt = 0, the three panels freed
 *    with UiPanel_FreeLines plus delete, and BaseInfoScreen_BuildPanels() called again. So the
 *    rebuild happens on the NEXT frame's draw, not on the click.
 */
#define PUFO_BASEINFO_TEXT_SELECT   1112 /* "Выбор базы", centred at (60, 300) */
#define PUFO_BASEINFO_TEXT_MONTHLY  1113 /* "Месячный расход", centred at (180, 300) in panel mode */
#define PUFO_BASEINFO_TEXT_BASE     1114 /* "База", centred at (180, 300) outside panel mode */
#define PUFO_BASEINFO_RAMP_STEPS    24   /* x 0, 5, ... 115 */
#define PUFO_BASEINFO_RAMP_STEP     5
#define PUFO_BASEINFO_RAMP_BASE     20   /* the left colour is 5*(step+20) */
#define PUFO_BASEINFO_RAMP_RIGHT0   -6   /* and the right one is -6-5*step */
#define PUFO_BASEINFO_BAND_A        35
#define PUFO_BASEINFO_BAND_B_FIXED  113
#define PUFO_BASEINFO_BAND_C_FIXED  225
#define PUFO_BASEINFO_BAND_B_PITCH  18   /* the moving bands are 18*shipsAtBase + 59 / + 169 */
#define PUFO_BASEINFO_BAND_B_OFF    59
#define PUFO_BASEINFO_BAND_C_OFF    169
#define PUFO_BASEINFO_ROW_PITCH     28   /* one base row per 28 pixels in the overlay list */

typedef struct BaseInfoDrawEnv {
    const GfxColorEnv *pColor;      /* pGX @ 0x1139B8, the colour-pair owner */
    u8      *pbBuilt;               /* bBaseInfoPanelsBuilt @ 0x15883C, cleared by the adopt arm */
    const u8 *pbModeFlag;           /* bBaseInfoPanelMode @ 0xAB190 */
    UiPanel **pppPanel;             /* the three columns, in build order */
    i8      *pnWorkBaseIdx;         /* byte_1870F4, written by the adopt arm */
    const i8 *pnAltBaseIdx;         /* byte_158834, the base the adopt arm copies from */
    u8      *pbOverlayOpen;         /* byte_1870F8, cleared by the adopt arm */
    const BaseSiteRec *apSiteRecs;  /* byte_186C40, eight 64-byte records; abName at +26 */
    const char *pTextRecs;          /* aTextRecs @ 0xB38D0, stride 205 */
    const i8 *pnBaseCount;          /* nBaseCount @ 0x159D80, read SIGNED as in the psi frame */
    Ship *const *apCraft;           /* Base_CountShipsAtBase's inputs, for the moving bands */
    i32  nCraftCount;
    BaseRec *const *apBases;
    i32  nBaseCount;
    const u8 *abGridBytes;          /* 0x159DBC, Base_CountShipsAtBase's extra term */
    void (*pfnDrawIconStrip)(i32 nMark, void *pUser);
    void *pStripUser;
    void (*pfnBuildPanels)(void);   /* BaseInfoScreen_BuildPanels @ 0x557f4 */
} BaseInfoDrawEnv;

void BaseInfoScreen_Draw(const BaseInfoDrawEnv *pEnv);

/* ------------------------------------------------- the base-attack screen
 *
 * Evidence: BaseAtScreen_Update @ 0x57808 (3,872 B), BaseAtScreen_Draw @ 0x58728 (420 B),
 * BaseAtScreen_CloseThunk @ 0x577c0 (72 B).
 *
 * This is the report a base gets when a mission site lands on it. Geoscape_SpawnMissionSite fills
 * the block at 0x158840..0x158849 from the strateg item (0x7EF50..0x7EF70): nBaseAtClass = bCraftClass
 * (+2, read SIGNED), nBaseAtStrategIdx = the item's creation serial at +0x34, byte_158848 =
 * nTargetIdx (+0x84), nAssaultBaseRace = bRace (+0x70); it pauses the game, writes the screen id 20
 * to 0x186BC4, and tail-calls BaseAtScreen_Open with the base index read SIGNED from the same +0x84.
 * Geoscape_CloseUI case 20 (0x8BAF8) is the teardown: the same opener with nMode -1, which now takes
 * the release arm.
 *
 * Update builds the panel as a scripted battle log.
 *
 * 1. Budget. The attacking craft's class sets the strength the defences have to burn through:
 *    50, 200, 250, 500, 500, 1200, 2200, 3000 for class 0..7 and 0 for anything else. The ARM builds
 *    the last two as 2192|8 and 2992|8 (0x578A8..0x578B4).
 * 2. The base's 6x6 grid is scanned once. Every cell that is FREE (its buildDays byte is 0) and of
 *    facility kind 12..16 is counted: gravity shield, missile, laser, plasma and fusion-ball defence.
 *    The type layer is reached as 372 + 144*base + 108 and the days layer as 36*base, both with the
 *    same 6*row + col cell index -- Hex-Rays renders the type reads through the buildDays array type,
 *    which is why the decompile shows two different strides for one cell.
 * 3. The log runs (gravity + 1) rounds. Each round emits one PAIR of rows per defence cell and then,
 *    in every round except the last, one gravity-shield pair. The panel capacity
 *    2*(defenceCells*(gravity + 1) + gravity) + 3 is exactly the row count that structure produces.
 * 4. A defence pair is rolled with Game_RandU16(100). BELOW the kind's threshold the defence HITS:
 *    green meta (50, 255, 50 / 0, 20, 0), TEXTS[1116] "ПОПАДАНИЕ!!!", and the budget drops by the
 *    kind's damage. AT or above the threshold it MISSES: red meta (255, 50, 50 / 20, 0, 0) and
 *    TEXTS[1117] "ПРОМАХ". Thresholds 50/60/70/80 and damages 500/600/900/1500 for
 *    missile/laser/plasma/fusion -- the better the defence, the more often it misses and the harder
 *    it hits. Both rows of the pair get aux 20, which is the delay the draw waits before the next row.
 * 5. The gravity pair is TEXTS[1121] "Гравитаци. щит отбросил НЛО" and TEXTS[1122] "Все сисст. защ.
 *    перезаряжены!!!" with meta (150, 150, 255 / 0, 20, 20) on both rows and aux 0 then 10. It burns
 *    no budget.
 * 6. The tail is three rows with aux 0: an EMPTY label (the original copies it from 0x10DA38, which
 *    is zero-filled .data and not a TEXTS record), then either TEXTS[1125] "НЛО высадилось рядом с
 *    базой" plus TEXTS[1126] "Подготовка к защите" with bBaseAtMissionFollows = 1 -- the budget
 *    survived, the aliens got out and the tactical mission follows -- or TEXTS[1123] "НЛО уничтожен"
 *    plus TEXTS[1124] "Угроза захвата снята" with the flag clear, when the budget reached 0 or less.
 *    nBaseAtLineLimit is the row count, nRadarCooldownR goes to 255 and nLineCount to 0, so the
 *    reveal starts again from the first row.
 *
 * Draw reveals one row per cooldown. With nRadarCooldownR at 255 and rows still hidden it parks the
 * cooldown at 255 - aux of the row it is about to show, shows that row, and scrolls once the shown
 * count passes nStyle (15). When every row is shown it centres the shared confirm control on x 120 at
 * y 290, sets bBaseAtConfirmShown -- the byte Geoscape_OnPointer (0x89414) and Geoscape_OnPointerUp
 * (0x8AF70) gate on -- blits its pad, and draws TEXTS[1127] "ОК" centred at (120, 290) in the same
 * caption colour pair the base-info and psi frames pack.
 *
 * CloseThunk: with bBaseAtMissionFollows clear the contact is over, so the strateg item is removed
 * (nBaseAtStrategIdx is the index handed to StrategList_RemoveAt) and the game is unpaused; either way
 * it tail-calls Geoscape_CloseUI, whose case 20 runs Update's release arm.
 *
 * Deviations:
 *   1. A null environment, panel, pad or counter is refused; the original dereferences all of them.
 *   2. The base index is bounded to the eight grid slots. The original computes 36*(4*base+3) and
 *      144*base + 480 without a test, so its -1 call would read grid - 36 and grid + 336 if it ever
 *      reached the build arm.
 *   3. The original writes the panel's nLineCount from nBaseAtLineCap BEFORE testing the panel for
 *      null (0x586D0), and blits the pad and calls UiPanelMsg_Draw without a test.
 *   4. The original's panel capacity is not bounded, and the row count it emits can reach 723 with
 *      18 gravity cells and 18 defence cells -- past the 500 lines a UiPanel holds. UiPanel_ctor's
 *      clamp and the line setters' bounds stop that here; the overflow is recorded as an original bug.
 *   5. The labels are copied through a 152-byte stack buffer in the original (var_98); the port hands
 *      the record straight to UiPanel_SetLineText, which bounds the copy to the 50-byte line.
 */
#define PUFO_BASEAT_PAD_NAME          "baseat" /* aBaseat @ 0xAB218 */
#define PUFO_BASEAT_PAD_MODE          30
#define PUFO_BASEAT_PANEL_STYLE       15   /* nStyle, and the scroll threshold in the draw */
#define PUFO_BASEAT_PANEL_FONT        16
#define PUFO_BASEAT_PANEL_X           10
#define PUFO_BASEAT_PANEL_Y           10
#define PUFO_BASEAT_PANEL_CLICKL      5    /* nRectX */
#define PUFO_BASEAT_PANEL_CLICKTOP    70   /* nRectY */
#define PUFO_BASEAT_PANEL_VIEWW       230
#define PUFO_BASEAT_AUX_DEFENCE       20   /* both rows of a defence pair */
#define PUFO_BASEAT_AUX_GRAVITY_A     0    /* the gravity pair's first row */
#define PUFO_BASEAT_AUX_GRAVITY_B     10   /* and its second */
#define PUFO_BASEAT_ROLL_MAX          100  /* Game_RandU16(0x64) per pair */
#define PUFO_BASEAT_THRESHOLD_MISSILE 50   /* at or above it the defence misses */
#define PUFO_BASEAT_THRESHOLD_LASER   60
#define PUFO_BASEAT_THRESHOLD_PLASMA  70
#define PUFO_BASEAT_THRESHOLD_FUSION  80
#define PUFO_BASEAT_DAMAGE_MISSILE    500
#define PUFO_BASEAT_DAMAGE_LASER      600
#define PUFO_BASEAT_DAMAGE_PLASMA     900
#define PUFO_BASEAT_DAMAGE_FUSION     1500
#define PUFO_BASEAT_TEXT_FIRE_MISSILE 1115 /* "Ракетная защ..ОГОНЬ!!!" */
#define PUFO_BASEAT_TEXT_HIT          1116 /* "ПОПАДАНИЕ!!!" */
#define PUFO_BASEAT_TEXT_MISS         1117 /* "ПРОМАХ" */
#define PUFO_BASEAT_TEXT_FIRE_LASER   1118 /* "Лазерная защ..ОГОНЬ!!!" */
#define PUFO_BASEAT_TEXT_FIRE_PLASMA  1119 /* "Плазменная защ..ОГОНЬ!!!" */
#define PUFO_BASEAT_TEXT_FIRE_FUSION  1120 /* "Термоядерная защита.ОГОНЬ!!!" */
#define PUFO_BASEAT_TEXT_GRAVITY      1121 /* "Гравитаци. щит отбросил НЛО" */
#define PUFO_BASEAT_TEXT_RECHARGED    1122 /* "Все сисст. защ. перезаряжены!!!" */
#define PUFO_BASEAT_TEXT_DESTROYED    1123 /* "НЛО уничтожен" */
#define PUFO_BASEAT_TEXT_THREAT       1124 /* "Угроза захвата снята" */
#define PUFO_BASEAT_TEXT_LANDED       1125 /* "НЛО высадилось рядом с базой" */
#define PUFO_BASEAT_TEXT_PREPARING    1126 /* "Подготовка к защите" */
#define PUFO_BASEAT_TEXT_OK           1127 /* aTextOk @ 0xEBF4B, "ОК" */
#define PUFO_BASEAT_CONFIRM_X         120
#define PUFO_BASEAT_CONFIRM_Y         290

typedef struct BaseAtUpdateEnv {
    u8      *pbActive;              /* bBaseAtActive @ 0x158864, the build/release gate */
    u8      *pbConfirmShown;        /* bBaseAtConfirmShown @ 0x158854, cleared by the build arm */
    u8      *pbMissionFollows;      /* bBaseAtMissionFollows @ 0x158850, the tail's result flag */
    PadImage **ppPad;               /* pBaseAtPad @ 0x15884C */
    UiPanel **ppPanel;              /* pBaseAtPanel @ 0x15885C */
    i32     *pnLineLimit;           /* nBaseAtLineLimit @ 0x158858, the row count to reveal */
    i32     *pnLineCap;             /* nBaseAtLineCap @ 0x158860, the constructed line count */
    const u8 *pbCraftClass;         /* nBaseAtClass @ 0x158840, read SIGNED for the switch */
    const u8 *abFacilityGrid;       /* gBaseFacGrid @ 0x159768 */
    const char *pTextRecs;          /* aTextRecs @ 0xB38D0, stride 205 */
    u8      *pbSpawnReplacement;    /* bSpawnReplacementOnce @ 0x162028, set by the release arm */
    i32     *pnRadarCooldownR;      /* nRadarCooldownR @ 0xAC218, set to 255 by the tail */
} BaseAtUpdateEnv;

void BaseAtScreen_Update(i32 nBaseIdx, const BaseAtUpdateEnv *pEnv);

typedef struct BaseAtDrawEnv {
    const GfxColorEnv *pColor;      /* pGX @ 0x1139B8, the colour-pair owner */
    u8      *pbActive;              /* bBaseAtActive, the gate */
    PadImage **ppPad;               /* pBaseAtPad, blitted first */
    UiPanel **ppPanel;              /* pBaseAtPanel */
    i32     *pnLineLimit;           /* nBaseAtLineLimit, the reveal target */
    i32     *pnRadarCooldownR;      /* nRadarCooldownR, the pacing counter */
    u8      *pbConfirmShown;        /* bBaseAtConfirmShown, set when every row is shown */
    UiControl *pCtlConfirm;         /* unk_186BA4 @ 0x186BA4, the shared confirm control */
    UiControlBlitFn pfnCtlBlit;     /* the pad blits UiControl_BlitActivePad picks between */
    UiControlBlitFn pfnCtlBlitEx;
    void *pCtlUser;
    const char *pTextRecs;          /* TEXTS[1127] "ОК" */
} BaseAtDrawEnv;

void BaseAtScreen_Draw(const BaseAtDrawEnv *pEnv);

typedef struct BaseAtCloseEnv {
    const u8 *pbMissionFollows;     /* bBaseAtMissionFollows @ 0x158850 */
    PtrArray *pStrategList;         /* the strateg list, 0x1864F8 + 0x3C */
    GeoscapeWorld *pWorld;
    const i32 *pnStrategIdx;        /* nBaseAtStrategIdx @ 0x158844 */
    u8      *pbSpawnReplacement;    /* bSpawnReplacementOnce, StrategList_RemoveAt's replacement roll */
    const StrategSpawnCtx *pSpawnCtx;
    GameTime *pTime;                /* 0x159618; bPaused is the byte at 0x15961C it clears */
    const GeoscapeCloseEnv *pClose;
} BaseAtCloseEnv;

void BaseAtScreen_CloseThunk(const BaseAtCloseEnv *pEnv);

#endif /* PUFO_BASEUI_H */
