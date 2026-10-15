/* pufo_baseui.h - base-screen scratch-cell helpers.
 *
 * Evidence: Set_byte_159E88_4 @ 0x6b6b0,
 *           Set_byte_159E88_3_SetUiY @ 0x6b674,
 *           Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec,
 *           PlaySample7_SetFlags_Cache0xBD8 @ 0x73328.
 *
 * The original writes scattered globals: `gUiRootPanel.anLineAux[145]`, two
 * controls' `y` fields (`dword_186BB0`/`dword_186BB4`), and two cached
 * `nUnk3032` copies (`dword_186BE8` and `nResearchTopicRowCache` @ 0x160C74) plus the `unk_160C80` /
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
#include "game/pufo_save.h"      /* DatFile, SaveGameInput, LoadGameEnv, the save/load slot handlers */

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
 * mover call in each body is a tail call (B rather than BL), which is the sole difference from
 * the calls before it.
 *
 * The panel sets, in the order each handler walks them:
 *
 *   ManufScreen_Panel*          pManufProductPanel @ 0x15EE44, unk_15EE48, unk_15EE40,
 *                               unk_15EE38
 *   PsiScreen_Panel*            unk_15EED0, unk_15EE88, unk_15EEA8, unk_15EE58 -- four parallel
 *                               arrays, one entry per base, all indexed by the SIGNED byte at
 *                               unk_1870F4 (the psi screen's selected base), so the handler picks
 *                               one entry out of every array for that base. The loops that walk
 *                               them (PsiScreen_ApplyAssignments, 0x72250, not ported) bound the
 *                               index by the base count at 0x159D80, at most PUFO_FAC_BASE_COUNT.
 *   BuySellScreen_Panel*        unk_161A1C, unk_161A00, unk_161A20, unk_160C88, unk_1615F8
 *   BuySellScreen_DetailPanel*  pPlaneSlotPanel @ 0x161AC8, pPlaneSelRowPanel @ 0x161F00,
 *                               pPlaneCommitRowPanel @ 0x161EEC, pPlaneTakenPanel @ 0x161EF4,
 *                               pPlaneAvailPanel @ 0x161A40
 *   Transfer_Scroll*            the same five globals as BuySellScreen_Panel*, same order
 *
 * BuySellScreen_PanelPrev @ 0x76088 and Transfer_ScrollPrevAll @ 0x967e4 are the same eleven
 * instructions over the same five globals -- only the BL displacements differ -- and the next
 * pair (0x760e0 / 0x9683c) likewise. The port keeps one implementation of each and cites both
 * addresses; the pair is on record in docs/parity/renames.tsv.
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
 * Smap_GrantSiteRecovery (0x98b50, not ported) hard-codes the same pairing per slot instead of going
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
 *          same two bytes. Geoscape_PulseUpdate (0x954dc, not ported) reads them. What the two
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
 * IDB now names bBuySellPanelsBuilt @ 0x161A28; BuySellScreen_Draw (0x77e6c, not ported) reads it
 * too.
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
    i32 nFundsSnapshot;             /* BuySell_nFundsSnapshot @ 0x161460 */
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
    void (*pfnCloseUi)(void);       /* Geoscape_CloseUI (0x8b94c, not ported) */
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
 * Transfer_ResetSite (0x9690c, not ported) clears and BuySellScreen_ConfirmSiteAndClose commits.
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
 *   5. 0x78CBC..0x78CD4: the close control unk_186B00 goes to x 224, y 1 (two STRH) with
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
    UiControl *pCloseCtl;            /* unk_186B00 */
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
    i32 nSiteCount;                   /* nSiteCount @ 0x186410, also the working site's index */
    BaseRec *const *apBases;          /* apBases @ 0x114B2C, for the site's bFlag1 */
    i8 nSelectedBaseIdx;              /* nSelectedBaseIdx @ 0x159D81 */
    ObjectRecFull *const *apObjects;  /* apObjects @ 0x113AC0 */
    i32 nObjectCount;                 /* Map_nObjectCount @ 0x114A60 */
    i32 *pnFunds;                     /* nFunds @ 0x1598D8 */
    i32 nFundsSnapshot;               /* BuySell_nFundsSnapshot @ 0x161460 */
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
 *      Transfer_ScrollNextAll @ 0x9683c -- both byte-identical to BuySellScreen_PanelPrev and
 *      BuySellScreen_PanelNext over the same five panel globals, see docs/parity/renames.tsv --
 *      then Transfer_ResetSite and TransferScreen_CommitTransfer (0x97aa4, not ported).
 *   7. 0x9963C..0x99654: the close control unk_186B00 goes to x 224, y 1 with
 *      Geoscape_CloseUI_ThunkB @ 0x96020. Same slot and same thunk as the buy/sell screen, but the
 *      buy/sell Init installs BuySellScreen_CloseThunk @ 0x75154 there instead.
 *   8. 0x9965C: the confirm control unk_186BA4 gets that same close thunk -- before UiControl_Setup,
 *      where BuySellScreen_Init installs its confirm handler after the pad and delta clearing.
 *   9. 0x99660..0x99680: UiControl_Setup(ctl, 0, 0, stru_AC3B8[i]) for the four controls. The reset
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
    UiControl *pCloseCtl;           /* unk_186B00 */
    UiControl *pConfirmCtl;         /* unk_186BA4 */
    void (*pfnCloseClick)(void);    /* Geoscape_CloseUI_ThunkB @ 0x96020 */
    void (*pfnPanelPrev)(void);     /* Transfer_ScrollPrevAll @ 0x967e4 */
    void (*pfnPanelNext)(void);     /* Transfer_ScrollNextAll @ 0x9683c */
    void (*pfnResetSite)(void);     /* Transfer_ResetSite @ 0x9690c */
    void (*pfnCommit)(void);        /* TransferScreen_CommitTransfer (0x97aa4), not ported */
    void (*pfnBuildPanels)(void);   /* TransferScreen_BuildPanels (0x96b30), not ported */
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
 * (0x995A4) and again from Geoscape_CloseUI (0x8b94c, not ported) at 0x8BE40, and Transfer_MoveOneItem
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
 *      (0x96288, not ported); D x 166 / 130 / 50, no click. Against the sell mode the B, C and D
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
    void (*pfnValidateCapacity)(void);/* TransferScreen_ValidateCapacity (0x96288), not ported */
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
 * 60000 (0xEA60) reports through Transfer_WriteErrorDump (0x996e4, not ported -- a debug dump writer
 * that opens t_error<rand>.txt, not a UI display) with the base index and the object index
 * (0x97EE4); finally the fresh working record at the new count is cleared -- bUnk518,
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
    void (*pfnShowTError)(i32 nBaseIdx, i32 nObjectIdx, void *pUser);
    void *pUser;
} TransferCommitEnv;

SmapSite *TransferScreen_CommitTransfer(TransferCommitEnv *pEnv);

/* ------------------------------------------------- the transfer screen's frame
 *
 * Evidence: TransferScreen_Draw @ 0x97fa4 (2988 B), reached from Geoscape_Draw (0x8c180, not ported).
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
 * Panels (0x98224..0x9828C). Only while bTransferPanelsBuilt: panel B and panel C get bUnk7080 = 1
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
#define PUFO_TRANSFER_RAMP_BASE   20  /* the climbing ramp is 5 * (k + 20), so it starts at 100 */
#define PUFO_TRANSFER_RAMP_STEP   5
#define PUFO_TRANSFER_RAMP_SIDE   0xE6 /* the ARM's RSB #0xE6: the descending side-ramp index */
#define PUFO_TRANSFER_RAMP_EDGE   0xFA /* the ARM's RSB #0xFA: the descending top/bottom ramp index */
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
    UiControl *pCtlReset;              /* unk_186B00 @ 0x186B00 */
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
    const i32 *pnMapPhase;             /* nMapPhase @ 0x1139EF, drives the label's red channel */
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

/* ResearchScreen_IncSelectedQty @ 0x72ea0 and ResearchScreen_DecSelectedQty @ 0x72f70: the
 * research screen's "+1 / -1" buttons, applied to the topic the topic panel is sitting on.
 *
 * Both reach the topic through the same pair of globals. The topic panel at 0x160C68 carries a
 * topic number per row in its anLineAux (ResearchScreen_RebuildTopicList (0x74270, not ported)
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
    const u16           *puResearchEntries; /* nResearchEntryCount @ 0x160C4C, shared by every base */
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
    GeoscapeCloseHooks close; /* Geoscape_CloseUI (&MEMORY[0x1864F8]), not ported */
} ResearchCancelEnv;

void ResearchScreen_CancelSelectedAndClose(const ResearchCancelEnv *pEnv);

/* ResearchScreen_AccumulateAllocated @ 0x74914 (148 B): the hourly research tick, called from
 * Geoscape_TickHourly @ 0x5D30C. For each of the eight base slots it walks the first
 * nResearchEntryCount entries of that base's allocation column and adds every non-zero one into
 * aResearchTopics[i].wProgress -- the topic index is the column index, so all eight bases
 * accumulate into the same 144 topic records. The add is an LDRH / ADD / STRH triple
 * (0x74964..0x7496C) with the result truncated to 16 bits, so it wraps. It ends with a tail call
 * into ResearchScreen_RebuildTopicList (0x74270, not ported), so the rebuild is a hook and this
 * returns what that hook returns.
 *
 * Two ARM quirks worth recording. The inner loop bound is re-read from nResearchEntryCount on
 * every iteration that saw a non-zero entry (LDRBHI at 0x7497C), which cannot change the outcome
 * because nothing here writes that byte. And the outer loop is a fixed eight (MOV R6,#8 / SUB /
 * BHI), not the live base count, so empty base slots are walked too. */
typedef struct ResearchAccumulateEnv {
    ResearchTopicLive *aTopics;           /* aResearchTopics @ 0x15EF08, 144 records of 34 bytes */
    const u16         *apResearchCols;    /* auResearchCol @ 0x16022C, 144 u16 per base */
    const u16         *puResearchEntries; /* nResearchEntryCount @ 0x160C4C */
    i32 (*pfnRebuild)(void *pUser);       /* ResearchScreen_RebuildTopicList (0x74270), not ported */
    void              *pRebuildUser;
} ResearchAccumulateEnv;

i32 ResearchScreen_AccumulateAllocated(const ResearchAccumulateEnv *pEnv);

/* ------------------------------------------------- the manufacturing quantity buttons
 *
 * The manufacturing screen is the mirror of the research one: a product panel whose anLineAux
 * carries a product number per row, a cached copy of that panel's nUnk3032 row cursor, a per-base
 * quantity column, and the same two gates with engineers and workshops in place of scientists and
 * labs. Evidence: ManufScreen_IncSelectedQty @ 0x6dddc (228 B), ManufScreen_ReduceSelectedQtyToOne
 * @ 0x6dec0 (172 B), ManufScreen_DecSelectedQty @ 0x6e09c (160 B), ManufScreen_DecEngineers @
 * 0x6e330 (164 B), ManufScreen_UpdateProgress @ 0x6df6c (304 B).
 *
 * The entry index is auManufCol[product + 200*base], composed over the 0x159E98 product-block
 * anchor plus 0x4304 (17156), and three of the buttons return that 17156 -- the same kind of
 * address residue the research buttons return as 4900.
 *
 * All five open by reading bManufQtyGate @ 0x15EE50 and bail out returning that byte when it is
 * non-zero. Worth stating plainly: no code in the binary ever writes a non-zero value to that
 * byte -- the only store to it anywhere is ManufScreen_SelectListed writing 0 at 0x6E970 -- so in
 * the shipped build the gate is dead. The port keeps it because the original has it.
 *
 * ManufScreen_ReduceSelectedQtyToOne is renamed in the IDB from ManufScreen_GetSelectedProductId:
 * it never returns a product id. It decrements the selected product's quantity one at a time while
 * the value is above 1 (0x6DF30..0x6DF54), which is a clamp to 1. */
#define PUFO_MANUF_COL_OFFSET 17156 /* 0x4304: auManufCol @ 0x15E19C minus the product-block
                                     * anchor @ 0x159E98. The value three buttons return. */
#define PUFO_MANUF_ANCHOR 0x159E98  /* ManufScreen_DecEngineers leaves the product-block anchor in
                                     * R0 (ADDGT R2,R0,R2 is the last thing that touches it).
                                     * Register residue; reproduced, not interpreted. */
typedef struct ManufQtyEnv {
    UiPanel             *pProductPanel;   /* pManufProductPanel @ 0x15EE44 */
    const i32           *pnProductRow;    /* nManufProductRowCache @ 0x15EE3C, a cached nUnk3032 */
    u16                 *apManufCols;     /* auManufCol @ 0x15E19C, 200 u16 per base */
    i32                 *apProductB;      /* aManufProductB @ 0x15B79C, 200 u32 per base */
    const u16           *puManufEntries;  /* nResearchEntryCount @ 0x160C4C, shared with research */
    const ObjectRecFull *pEngineerRec;    /* Map_apObjects112 @ 0x113C80, the engineers item */
    const u8            *abFacilityGrid;  /* gBaseFacGrid @ 0x159768 */
    const i8            *pnSelectedBaseIdx; /* nSelectedBaseIdx @ 0x159D81 */
    const u8            *pbQtyGate;       /* bManufQtyGate @ 0x15EE50 */
} ManufQtyEnv;

i32 ManufScreen_IncSelectedQty(const ManufQtyEnv *pEnv);
i32 ManufScreen_DecSelectedQty(const ManufQtyEnv *pEnv);
i32 ManufScreen_ReduceSelectedQtyToOne(const ManufQtyEnv *pEnv);
i32 ManufScreen_DecEngineers(const ManufQtyEnv *pEnv);
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
 * And it clears bManufQtyGate, which is the only write that address ever gets.
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
 * (92 B), ManufScreen_IncEngineers @ 0x6e13c (500 B).
 *
 * The product table is three parallel arrays plus the quantity column, all contiguous and all
 * 200 entries per base (see the product chunk note in game/pufo_save.h):
 *
 *     A  u32  @ 0x159E9C   B  u32 @ 0x15B79C (aManufProductB)
 *     C  u16 @ 0x15D09C    quantity column u16 @ 0x15E19C (auManufCol)
 *
 * ClearProductSlot zeroes one index of all four. Its first argument is never read. Products
 * 119..121 -- the three hangar products, the same range ManufScreen_IncEngineers splits on --
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
    u16         *apProductC;        /* unk_15D09C, 200 u16 per base */
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
 * (0x6f34c, not ported) on the way out, and makes the same call when the base count is zero. The
 * port leaves that call out and records it here.
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
 *   auManufCol  @ 0x15E19C  u16  the work units allocated to this product at this base
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
 * (ManufScreen_Draw 0x6E704 and ManufScreen_BuildPanels 0x6F060, both not ported) is
 * `accumulated * 100 / (workPerUnit * target)`, which is the same reading.
 *
 * Per base, per product, in this order:
 *
 *   1. 0x6F3D0: a zero allocation is skipped outright.
 *   2. 0x6F408..0x6F494 funds gate: blocked when (double)nFunds - (double)anObjectPriceInit[product]
 *      * 1.4 < 0.0, the 1.4 being the double 0x3FE6666666666666 assembled at 0x6F440/0x6F448.
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
 *      four components are subtracted from those four stocks, and nFunds is cut by price * 1.4
 *      through _dtoi (0x6F70C..0x6F788).
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
    u16               *apManufCols;   /* auManufCol @ 0x15E19C: the allocated work per product */
    u32               *apProductA;    /* unk_159E9C: the accumulated work */
    i32               *apProductB;    /* aManufProductB @ 0x15B79C: the requested unit count */
    u16               *apProductC;    /* unk_15D09C: the units already committed */
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
 * ClearProductSlot is the 0x159E98 anchor, which that function never reads. Geoscape_CloseUI is
 * not ported, so the close is the same hook ResearchScreen_CancelSelectedAndClose uses. */
void ManufScreen_ConfirmAndClose(const ManufClearEnv *pEnv, const GeoscapeCloseHooks *pHooks);

/* ManufScreen_IncEngineers @ 0x6e13c (500 B): the manufacturing screen's engineer "+" button. Like
 * the other four buttons it opens by reading bManufQtyGate and bails returning that byte, and like
 * them the gate is dead in the shipped build. It then splits on the selected product:
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
 *     capacity the quantity goes up by one; at or above it, byte_1870C8 = 1 and
 *     dword_1870B4[0] = 0 -- note the residue differs from the hangar path, 1 versus 0.
 *
 * Base_CountItemQtyAcrossBases is called with the site count at 0x186410, the selected base as the
 * site index, and scan-sites = 1.
 *
 * Return value: the gate byte, or 4*idx (hangar path), or the capacity, or &dword_1870B4. It is
 * installed as a click handler (0x711D0, 0x7127C) and nothing reads it, so the port returns void. */
typedef struct ManufEngineerEnv {
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
} ManufEngineerEnv;

void ManufScreen_IncEngineers(const ManufEngineerEnv *pEnv);

/* ------------------------------------------------- the buy/sell confirm, and the race funding bonus
 *
 * Evidence: BuySellScreen_ConfirmSiteAndClose @ 0x75090 (196 B), Funding_AddRaceBonus @ 0x7debc
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
                                       const GeoscapeCloseHooks *pHooks);

/* Funding_AddRaceBonus @ 0x7debc (196 B): one radar contact raises one funding dword by 2. It is
 * called from RadarScreen_Update three times (0x60B1C, 0x60C50, 0x60CAC), each time with the value
 * Tilemap_GetAt returned for the contact's tile as the race id and unk_161F34 as a first argument it
 * never reads. The return value -- &0x1864F8 on the paths that changed something -- is overwritten
 * by the next instruction every time, so the port returns void.
 *
 * The six dwords are 0x186568, 0x18656C, 0x186570, 0x186574, 0x186578, 0x18657C. FundingScreen_Init
 * (0x5C0FC..0x5C144) is what gives them their meaning: it sums the first five, divides the funding
 * total at 0x1865BC by that sum plus one, multiplies the result by the sixth, adds that back into
 * the total, divides the total by the sum plus one again, and scales each of the first five by that
 * last quotient into 0x186580..0x186590. So [0..4] are five race scores and [5] is a multiplier on
 * the monthly total -- and [5] is the one dword of the block the save never writes.
 *
 * The race ids are 0x80..0x89: aNewBaseCostByRace @ 0xAB488 is ten dwords indexed by (race - 0x80),
 * and 0xFF is the "no race" tile value. The mapping is NOT monotonic -- 0x84 feeds the second slot
 * while 0x82 and 0x83 feed the third and fourth -- and 0x86, 0x87 and 0x89 give no bonus at all.
 * That is what the compare chain at 0x7DEBC..0x7DF7C says, and it is reproduced as written. */
#define PUFO_FUNDING_SLOT_COUNT 6
#define PUFO_FUNDING_RACE_FIRST 5  /* [0..4] are the five race scores */
#define PUFO_FUNDING_MULT_SLOT  5  /* [5] @ 0x18657C scales the monthly total */
#define PUFO_FUNDING_BONUS      2  /* every hit adds exactly two */
#define PUFO_RACE_ID_FIRST      0x80
#define PUFO_RACE_ID_COUNT      10 /* aNewBaseCostByRace @ 0xAB488 has one entry per id */
#define PUFO_RACE_ID_NONE       0xFF /* the tile value with no race behind it */

void Funding_AddRaceBonus(i32 *anFundingSlots, i32 nRaceId);

/* ResearchScreen_StartSelected @ 0x73240 (232 B): the research screen's start button. It clears
 * the detail flag, plays sample 17, and then -- only if the selected topic's wProgress is zero AND
 * that topic's allocation for this base is non-zero -- sets wProgress to 1, which is what moves a
 * topic from "allocated" to "running". The two reads are 0x732C8 (LDRH [topic*34 + 0xA]) and
 * 0x732F8 (LDRH of auResearchCol[topic + 144*base]); the store at 0x732D8 is the same wProgress
 * reached through the 0x15EF0C anchor plus 6, the same address.
 *
 * It then refreshes the three panels, re-runs both needed passes (workshops and labs, which is why
 * the manufacturing column has to be handed in too), and tail-calls ResearchScreen_RebuildTopicList
 * (0x74270, not ported), so the rebuild is a hook and this returns what that hook returns. */
typedef struct ResearchStartEnv {
    ResearchQtyEnv qty;                  /* panel, row cache, research column, count, scientists, grid, base */
    ResearchTopicLive *aTopics;          /* aResearchTopics @ 0x15EF08 */
    UiPanel         *pPanelQty;          /* unk_15EEFC, the second of the three research panels */
    UiPanel         *pPanelProgress;     /* unk_15EF00, the third */
    const u16       *apManufCols;        /* auManufCol @ 0x15E19C, for the workshop needed pass */
    u8              *pbTopicSelected;    /* unk_160C80, cleared here */
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void *pSampleUser;
    i32 (*pfnRebuild)(void *pUser);      /* ResearchScreen_RebuildTopicList (0x74270), not ported */
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
 * SoldierScreen_Draw @ 0x65290, EquipSelected @ 0x619cc, SelectListed @ 0x64a9c and CopyNameToEdit
 * (0x6318c, not ported)). These two handlers move it to the next / previous soldier that
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
 * built by BaseInfoScreen_BuildPanels (0x557f4, not ported), and the stores go in the order
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
    const u16       *puResearchEntries;   /* nResearchEntryCount @ 0x160C4C */
    const i8        *pnSelectedBaseIdx;   /* nSelectedBaseIdx @ 0x159D81 */
    i32             *pnScreenId;          /* nGeoscapeScreen @ 0x186BC4 */
    UiControl       *pParkCtl;            /* unk_186BB4, moved to 1000,1000 */
    void (*pfnPlaySample)(u32 nSample, void *pUser);
    void *pSampleUser;
} BaseBuildModeEnv;

void Base_EnterBuildMode(const BaseBuildModeEnv *pEnv);

typedef struct SmapWaypointEnv {
    UiLineList           *pLines;            /* the root message list @ 0x158890 */
    i32                  *pnStrategIndex;    /* 0x159E74 */
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
 *      -- this function. Their writer is in the unported geoscape pointer path, so the port takes them
 *      as inputs rather than guessing. StrategList_AddItem scales a type-5 position out of the current
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
 *      (Transfer_WriteErrorDump (0x996e4, not ported)) fires when the base's column for the slot
 *      passes 60000, and Base_DistributeRecoveredItems runs after EVERY slot -- empty slot or
 *      not, alien slot or not.
 *   4. 0x9905C..0x99330: uRecoveredSoldiers calls of Soldier_CreateAndClassify (port:
 *      Unit_CreateAndClassify) with the base index as nKind. Then per recovered craft: the craft
 *      whose Ship.bId matches site->anCraftIds[i] gets Ship.bHomeBase = the base index
 *      unconditionally, and a 5x5 scan of the base's facility grid looks for a hangar cell --
 *      facType cell == FAC_HANGAR, buildDays cell == 0, and layer2 tested at fac + 79 + row + col.
 *      On a match bHangarX = col + 1, bHangarY = row + 1, and the craft glyph (33 + bCraftType)
 *      is stamped into layer2 at fac + 79 + 6*row + col. Two differences from Ship_SpawnAtSlot's
 *      own scan: the layer2 test here follows the row (79 + row + col, not 79 + col), and a craft
 *      type outside 0..4 CONTINUES the scan instead of returning unstamped.
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
 * Reached three times from Geoscape_Draw (0x8c180, not ported) at 0x92DD0 / 0x92F38 / 0x93094, plus a
 * data reference from the handler table entry at 0x18A2B0.
 *
 * SCREEN 5 (0x652A4): while nGeoscapeScreen is 5 this is not a draw at all - it returns
 * Ui_TextInputDialog(nSoldierCursor[0]), the name-edit dialog that SoldierScreen_SelectListed (0x64a9c)
 * arms by putting 5 into the screen id. Ui_TextInputDialog (0x632cc) is not ported and enters as a hook.
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
    i32 (*pfnTextInput)(i32 nCursor, void *pUser); /* Ui_TextInputDialog (0x632cc), not ported */
    void *pTextInputUser;
} SoldierDrawEnv;

i32 SoldierScreen_Draw(const SoldierDrawEnv *pEnv);

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
 * dialog's pad control (*unk_186B00) blits (0x59ECC), the red pair 220,80,80 / 50,30,30 is set up
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
    UiControl *pCtl;                /* *unk_186B00, the dialog's pad control */
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
 * unk_186BB4 (the "save" hotspot) and unk_186BB0 (the "return to title" hotspot). The tab byte is
 * the scratch byte the helpers above already model (byte_159E88[0], ScreenScratch.nLineAux145):
 * 0 audio, 1 save, 2 load, 3 and 4 the two pen rows. A different byte at 0x159E8C is the "the
 * controls exist" flag OptionsScreen_Init branches on.
 *
 * ## OptionsScreen_Init @ 0x6be74
 *
 * Two modes off that flag. Set (0x6C0FC..0x6C15C): clear it, then UiControl_FreePads plus operator
 * delete over the seven list rows and the twelve radio buttons. Clear (0x6BE90..0x6C0B8): set it,
 * allocate and ctor seven list rows and position them with the seven identical records at 0xAB750
 * ("firebtn"/"cfirebtn", offset 1/229, mode 0); allocate and ctor twelve radio buttons; install
 * Options_SaveAudioSettings (0x6b7ac) on unk_186BB4 and OptionsScreen_ReturnToTitle (0x6b46c) on
 * unk_186BB0; position the twelve with the twelve identical records at 0xAB830
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
 * sfx calls Sound_ApplySfxVolume and then tail-calls Sound_Play(17) so the player hears the new
 * level. Four music handlers (0, 2, 3, 4) store the volume before clearing the group and reach their
 * button through the 0x159E44 + 4*n array; the other two (1, 5) clear first and name the pointer
 * directly. The order is not observable — the group clear only zeroes nState and the selected button
 * is written after it — so the port uses one body for all six.
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
 * OptionsScreen_BindSaveSlots, OptionsScreen_BindLoadSlots, Set_byte_159E88_3_SetUiY (0x6b674) and
 * Set_byte_159E88_4 (0x6b6b0); those four are centred at x = 120 - (uWidth >> 1) with y = 90 + 25*i.
 * unk_186BB4 then gets x = 120 - (its own uWidth >> 1) and y = 198, and unk_186BB0 gets
 * x = 120 - (unk_186BB4's uWidth >> 1): the second hotspot is centred on the FIRST control's width,
 * because the ARM reloads R1 from the same R0 (unk_186BB4) for the uWidth read at 0x6B764 and only
 * then switches the store target to unk_186BB0 at 0x6B778.
 *
 * BindSaveSlots (0x6b49c) and BindLoadSlots (0x6b588) are the same shape over all seven rows:
 * scratch byte 1 or 2, the seven SaveGame_SlotN_CloseUI / LoadGame_SlotN_InitOptions handlers,
 * x = 120 - (uWidth >> 1), y = 40 + 22*i, unk_186BB4->y = 215, and SaveSlot_FormatList over the main
 * menu's slot context. The fourteen slot handlers are ported below; they arrive here as the caller's
 * thunks because the click slot takes no arguments.
 *
 * Deviations: the original allocates every control with operator new(0x20) into fixed .bss slots and
 * tears them down with UiControl_FreePads plus operator delete; the port takes caller-owned controls,
 * so its teardown branch runs UiControl_FreePads and leaves the storage to the caller. The port's
 * click slot is `void (*)(void)`, so the twelve radio handlers, the fourteen slot handlers and
 * OptionsScreen_ReturnToTitle arrive as the caller's thunks; Options_SaveAudioSettings (0x6b7ac) is
 * not ported and arrives the same way. The original returns the applier's register from the radio
 * handlers; the port returns 0, which nothing reads. */
#define PUFO_OPTIONS_LIST_COUNT 7
#define PUFO_OPTIONS_VOL_COUNT  6

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
    ScreenScratch    *pScratch;
    UiControl *pSaveCtl;   /* unk_186BB4, the "save" hotspot */
    UiControl *pBackCtl;   /* unk_186BB0, the "return to title" hotspot */
    /* The four handlers LayoutButtons installs on the first four rows. They land in a control's
     * click slot, so they are `void (*)(void)` thunks over the caller's own env. */
    void (*pfnBindSaveSlots)(void); /* OptionsScreen_BindSaveSlots (0x6b49c) */
    void (*pfnBindLoadSlots)(void); /* OptionsScreen_BindLoadSlots (0x6b588) */
    void (*pfnSetTab3)(void);       /* Set_byte_159E88_3_SetUiY (0x6b674) */
    void (*pfnSetTab4)(void);       /* Set_byte_159E88_4 (0x6b6b0) */
    void (*apfnSaveSlot[PUFO_OPTIONS_LIST_COUNT])(void); /* SaveGame_SlotN_CloseUI (0x6af80..0x6b148) */
    void (*apfnLoadSlot[PUFO_OPTIONS_LIST_COUNT])(void); /* LoadGame_SlotN_InitOptions (0x6b194..0x6b404) */
    void (*pfnFormatSlots)(void *pUser);   /* SaveSlot_FormatList (0x45904), reads the save files */
    void *pUser;
} OptionsTabEnv;

UiControl *OptionsScreen_LayoutButtons(OptionsTabEnv *pEnv);  /* 0x6b6c4 */
void OptionsScreen_BindSaveSlots(OptionsTabEnv *pEnv);        /* 0x6b49c */
void OptionsScreen_BindLoadSlots(OptionsTabEnv *pEnv);        /* 0x6b588 */

typedef struct OptionsInitEnv {
    OptionsScreenCtl *pCtl;
    OptionsVolumeEnv *pVolEnv;  /* the state RefreshVolumeHighlight re-selects from */
    OptionsTabEnv    *pTabEnv;  /* the state LayoutButtons lays out */
    u8 *pbControlsExist;        /* byte_159E88[4] @ 0x159E8C */
    UiControl *pSaveCtl;        /* unk_186BB4 */
    UiControl *pBackCtl;        /* unk_186BB0 */
    void (*pfnSaveAudio)(void);          /* Options_SaveAudioSettings (0x6b7ac), not ported */
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
 * pIn->nSlot / pLoadEnv->nSlot before the driver runs. Geoscape_CloseUI (0x8b94c) is not ported and
 * enters as a hook. The original's return register is Geoscape_CloseUI's; the port returns 0. */
typedef struct SaveSlotEnv {
    DatFile        *pDat;       /* the slot file the driver opens */
    SaveGameInput  *pIn;        /* nSlot is set to this handler's slot before SaveGame runs */
    LoadGameEnv    *pLoadEnv;   /* nSlot is set to this handler's slot before LoadGame runs */
    i32            *pnListMode;      /* MainMenu_nListMode @ 0x157188 */
    ScreenScratch  *pScratch;        /* byte_159E88[0], the tab byte */
    u8             *pbControlsExist; /* byte_159E88[4] */
    i32            *pnGeoscapeScreen;/* nGeoscapeScreen @ 0x186BC4 */
    OptionsInitEnv *pOptionsInit;    /* re-initialised before the load */
    void (*pfnCloseUi)(void *pUser); /* Geoscape_CloseUI (0x8b94c), not ported */
    void *pCloseUser;
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
 *   ResearchScreen_Open     -- sample 17 first, screen id 8 written BEFORE the Init, tail call
 *                              ResearchScreen_Init (0x615d0, not ported) with the topic array at
 *                              0x15EF08.
 *   Ui_OpenBaseInfo         -- sample 17 first, screen id 16 written BEFORE the Init, tail call
 *                              Ui_InitBaseInfo (not ported) with the root at unk_1872E0.
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
 *                                Ui_InitBaseInfo(unk_1872E0).
 *   Ui_OpenResearch           -- runs the prologue WITHOUT GameTime_Set5Sec (0x84CA4 goes from the
 *                                shortfall store straight to the call), then calls
 *                                Geoscape_PrepScreenOpen (0x84CBC), then GameTime_Set5Sec (0x84CC0),
 *                                then Geoscape_BuildNavControls(abFacilityGrid) (0x84CC8), then parks
 *                                the control at unk_186BB4 at (1000, 1000) with two STRH of 0x3E8,
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
    UiControl          *pParkCtl;         /* unk_186BB4, parked at (1000, 1000) */
    u8                 *pbUfopaediaReselect; /* byte_18706C */
} ScreenResetOpen2Env;

void Geoscape_PrepScreenOpenSfx(const ScreenResetOpenEnv *pEnv);
void Ui_OpenBaseInfoAlt(const ScreenResetOpen2Env *pEnv);
void Ui_OpenResearch(const ScreenResetOpen2Env *pEnv);

#endif /* PUFO_BASEUI_H */
