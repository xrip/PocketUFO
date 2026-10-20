/* pufo_geoiinit.h - Geoscape_InitUI @ 0x857f4: the geoscape bring-up.
 *
 * Evidence: Geoscape_InitUI @ 0x857f4 (5420 bytes), fully decompiled with the ARM
 * checked at every store the decompiler rendered through a guessed type.
 *
 * This is the other half of the geoscape UI lifecycle: it builds exactly the objects
 * GeoscapeUi_FreeAll @ 0x86d20 releases (see pufo_geouimanage.h), and it is reached
 * from three places: the start menu's new-game button (MenuCtl0_OnClick @ 0x1a09c with
 * mode 0), LoadGame @ 0x44bbc's teardown paths (mode 1), and the escape path
 * Tact_Shutdown(0) + Geoscape_InitUI(1).
 *
 * What it does, in the original's own order:
 *
 *   1. Sets the nine region counters at 0x18655D to 1 (aRegionBuildCount in pufo_save.h),
 *      runs Font_Init, clears the screen id (nGeoscapeScreen @ 0x186BC4), sets the
 *      geoscape flag byte (Map_bGeoscapeUi @ Map+0x44, a STRB), and clears the two root
 *      flags at 0x18652C / 0x18652D (byte gates read by BaseFacScreen_DrawPlacementOverlay
 *      @ 0x58E7C and BasePlaneScreen_Draw @ 0x7D3D0).
 *   2. Creates the thirteen pad singletons (GeoscapeUiState.apPad), the world-size block
 *      (0x186BC0: a pointer to a u16 pair, 800 x 400), and the ten clock digit pads
 *      "b0".."b9" (apClockDigitPads @ 0x186BF0).
 *   3. Fills the seventeen facility records at aBaseFacFlags @ 0x186E48: name from a text
 *      record, bFlag21 = the record index, and the initial cost. The costs are the
 *      literal pool values; facility 10's cost renders in IDA as `LDR R2, =unk_13D620`
 *      but the literal is the number 0x13D620 = 1300000 - BaseFacScreen_TryBuySelected
 *      @ 0x58910 reads the cost field as a plain number in its afford test.
 *   4. Loads the four geoscape sprite groups through Font_LoadSpr, capturing each group's
 *      first frame index before the load: font -> fontSys_nGlyphBase (+0x28), basebits ->
 *      +0x2C, mapbits -> +0x34, bigobs -> nBigObsSprBase (+0x26), then Font_BuildIndex.
 *   5. Loads 23 palettes, then the 24th (earth100.pal) later with the earth pads.
 *   6. Creates the thirteen control singletons (apCtl) and positions them with the
 *      descriptors at 0xAC078 (g_aGeodescSingle), in the original's own singleton order:
 *      slot i gets descriptor g_anGeodescSingleSlot[i].
 *   7. Creates the thirteen root controls (apRootCtl), stores the click handlers, then
 *      positions them with the descriptors at 0xABED8 (g_aGeodescRoot). The handler slots
 *      are the root block's own dwords: [0] Smap_OnZoomOut (0x1864F8), [1] Smap_OnZoomIn
 *      (0x1864FC), [2..7] the six speed handlers (apGeoscapeSpeedCtl @ 0x186500),
 *      [8] Ui_OpenScreen18 (0x186518), [9] Ui_CloseScreen18 (0x18651C),
 *      [10] Base_EnterBuildMode (0x186520), [11] Ui_OpenBaseInfo (0x186524),
 *      [12] Ui_OpenFunding (0x186528). The 186B8C singleton gets
 *      Smap_BeginCraftSelectBtn (0x865D0). The port takes all fourteen as function
 *      pointers because UiControl.pfnClick is a no-argument pointer and the handlers
 *      need environments.
 *   8. Publishes the three zoom layer sets (SmapZoomSet) and sets the viewport to level 0
 *      with the level-0 grid's size (0x1863F4 / 0x1863F8 are gSmapCam.nMapW / nMapH).
 *   9. Only when the low byte of nMode is zero: reads research.dat into the live topic
 *      array, zeroes the u16 at +6 of every topic (ARM `STRH R4,[R1,#6]` - Hex-Rays
 *      renders this as wProgress through its save-order ResearchTopic type; in the
 *      in-memory order it is ResearchTopicLive.wUnk6), clears the viewport scroll and
 *      drag deltas, and runs Game_StartNew through the pfnStartNew hook.
 *  10. GameTime_Decode; per base Base_ClearFacilityGridRow into the site-state array;
 *      the initial object prices from dword_AA2C0 into ObjectRecFull.uPrice (slots
 *      1..nObjectCount-1); object names, text records 1447..1594 into slots 1..148 (148
 *      copies, not nObjectCount -- 0x86A64 loads apObjects[counter+1], so slot 0 keeps the
 *      name OBJECTS.DAT carries at record+0x00 and slot j gets record 1446+j);
 *      apObjects[1].uFirePower = 110 (`STRH R3,[R0,#0xB6]`);
 *      unk_159DC4 = 10; GameTime_Set5Sec; StartTimers_D.
 *  11. Builds the status line panel (unk_18641C): UiPanel_ctor(20), nStyle 13, nFont 16,
 *      the three SetBytes groups, nUnk7076 1, bFlag7073 1, pfnClick Geoscape_OnRadarListClick
 *      (hooked), bUnk7088 100.
 *  12. Reads the 56 mission-site records: 24 bytes from "<i>.cts" into unk_1865C0, then the
 *      name from text record 1655+i into +4. With lang.dat present only 1650 text records
 *      are loaded (Assets_LoadStartup @ 0x384e0 takes the lang copy), so those 56 records
 *      are zero in the binary and the names come out empty - the original's own behaviour.
 *  13. Clears the clock pause byte (0x15961C = apLines[106] = GameTime.bPaused), sets the
 *      save format version (nSaveFmtVer @ 0x18655C) to 3, and runs the sound bring-up:
 *      Sound_InitAndLoadSamples("tact"), Sound_LoadAndPlayMusic(2), and the two volume
 *      applies from the options.
 *
 * Deviations from the original, all deliberate:
 *   - The world-size block: the original allocates 0x4E204 bytes (800*400 + the u16 pair)
 *     and memsets it; the port allocates just the SmapWorldSize pair. Nothing in the
 *     mapped code reads the 320000-byte tail - the tile grid lives in the m100 pad.
 *   - The name copies are bounded to the destination field (the original copies until NUL
 *     with no bound; the shipped names fit).
 *   - research.dat is read capped at the topic array size (the shipped file is exactly
 *     144 * 34 = 4896 bytes).
 *   - The original's `dword_186B08` loop counters are the compiler's spilled locals (see
 *     pufo_geouimanage.h); the port uses real locals.
 *   - The original stores the research entry count with STRB (a byte); the port writes the
 *     u16 the port family carries at 0x160C4C, which is the same value at startup.
 */
#ifndef PUFO_GEOIINIT_H
#define PUFO_GEOIINIT_H

#include "asset/pufo_asset.h"
#include "audio/pufo_audio.h"
#include "game/pufo_baseui.h"   /* ResearchTopicLive */
#include "game/pufo_clock.h"
#include "game/pufo_font.h"
#include "game/pufo_geoscape.h" /* GeoscapeWorld, GameTime_Set5Sec, Base_ClearFacilityGridRow */
#include "game/pufo_objectrec.h"
#include "game/pufo_options.h"
#include "game/pufo_strateg.h"  /* SmapView, SmapZoomSet, SmapWorldSize */
#include "pufo_common.h"
#include "render/pufo_render.h"
#include "ui/pufo_control.h"
#include "ui/pufo_geouimanage.h"
#include "ui/pufo_panel.h"

/* The 28-byte facility record at aBaseFacFlags @ 0x186E48 (BaseFacFlag, PUFO_BASEFAC_COUNT) now
 * lives in game/pufo_geoscape.h, which this header includes: the map click and the build dialog
 * index it too. */

/* MissionSiteRec and PUFO_MISSION_SITE_COUNT moved to game/pufo_geoscape.h: the flag-0x10 status
 * panel (0x90574) indexes the same 0x1865C0 table, and this header already includes that one. */

/* Everything Geoscape_InitUI reaches outside its own allocations. */
typedef struct GeoscapeInitEnv {
    /* The UI state InitUI fills (the same struct GeoscapeUi_FreeAll releases). */
    GeoscapeUiState *pUi;
    SmapView        *pView;      /* gSmapCam @ 0x1863E8 */
    SmapZoomSet     *pSetsOut;   /* three sets: the layer globals the zoom code swaps */
    /* The world glyph record behind 0x186BC0 (0x4E204 = 4 + 800*400 bytes) is allocated here and
     * published as pUi->pWorldSize, pointing at its header pair; GeoscapeUi_FreeAll releases it. */
    i32             *pnScreenMode;   /* nGeoscapeScreen @ 0x186BC4 */
    u8              *pbGeoscapeUi;   /* Map_bGeoscapeUi @ Map+0x44 */
    u8              *pbRoot34;       /* unk_18652C */
    u8              *pbRoot35;       /* unk_18652D */

    PufoFont *pFont;             /* fontSys @ 0x158268 */

    AssetReader  *pRdr;          /* pRdr @ 0x187028 */
    const u8     *pTextRecs;     /* aTextRecs @ 0xB38D0, stride PUFO_TEXT_REC_STRIDE */

    GameTime         *pTime;     /* nGameTimeSec @ 0x159618; bPaused is 0x15961C */
    ResearchTopicLive *apTopics; /* the live topic array @ 0x15EF0C */
    u16              *puResearchEntries; /* 0x160C4C, the shared entry count */

    BaseFacFlag *paFacFlags;     /* aBaseFacFlags @ 0x186E48 */

    ObjectRecFull *const *apObjects; /* Map.apObjects @ 0x113AC0 */
    i32           nObjectCount;      /* Map_nObjectCount @ 0x114A60 */
    i32           *pnGlyphPalBase;   /* Map_nGlyphPalBase @ 0x1183F4 */

    u8            *abFacilityGrid;   /* gBaseFacGrid @ 0x159768 */
    GeoscapeWorld *pWorld;           /* apSiteState is the byte_161F38 array */
    i8            *pbBaseCount;      /* nBaseCount @ 0x159D80, signed */
    i32           *pnSmapOriginX;    /* nSmapOriginX @ 0x159DC4, set to 10 at 0x86AAC */

    MissionSiteRec *paMissionSites;  /* unk_1865C0, 56 records */

    u8 *aRegionBuildCount;   /* the nine region counters @ 0x18655D, set to 1 */
    i8 *pbSaveVersion; /* nSaveFmtVer @ 0x18655C, set to 3 */

    const PufoOptions *pOptions; /* the two volume applies read these */

    /* The fourteen click handlers, in the root block's own slot order (see the header
     * comment). UiControl.pfnClick takes no arguments, so the app wires these as thunks
     * over its own environments. */
    void (*apfnRootClick[PUFO_GEOUI_ROOT_CTLS])(void);
    void (*pfnCraftSelectClick)(void); /* Smap_BeginCraftSelectBtn @ 0x84634 -> 186B8C */
    void (*pfnRadarClick)(void);       /* Geoscape_OnRadarListClick @ 0x854c4 (ported, pufo_baseui.c) */

    /* Game_StartNew @ 0x94554 is ported below; the mode-0 branch still calls through this
     * hook because the shell owns the StartNewEnv it needs. */
    void (*pfnStartNew)(void *pUser);
    void *pHookUser;

    /* UiControl_Setup's pad loader injection (it reads the asset archives). */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pLoadPadsUser;
} GeoscapeInitEnv;

/* Geoscape_InitUI @ 0x857f4. nMode is tested as its low byte: zero runs the new-game
 * branch (research.dat load, viewport clear, Game_StartNew); any other value skips it
 * and leaves the loaded world alone. */
void Geoscape_InitUI(const GeoscapeInitEnv *pEnv, i32 nMode);

/* ------------------------------------------------- the new game
 *
 * Evidence: Game_StartNew @ 0x94554 (3,976 B), called from Geoscape_InitUI's mode-0
 * branch @ 0x86974 with the 0x1864F8 block in R0.
 *
 * The order, with the original's addresses:
 *   1. 0x94584: every object record's per-base column (+42..+56, eight u16) and item
 *      quantities (+58..+156, aItemQty[0..49] of 52) are zeroed, for nObjectCount records.
 *   2. 0x945D4: placement-pending 0x1870AC = 1; the strateg id counters 0x186544 /
 *      0x186548 / 0x18654C = 1; the five craft-type counters 0x18653C..40 = 0.
 *   3. 0x94620: while the SIGNED base count is above zero, each base's radar byte
 *      (0x162008) gets Base_CountEmptyFacilityCells.
 *   4. 0x94650: ten bytes at 0x18655D = 1 -- the nine counters plus 0x186566 (bUiFrameStyle);
 *      serialised); the strateg count byte 0x186530 = 0.
 *   5. 0x94680: the economy dwords of the 0x1864F8 block: 0x186568..0x186590 = 0,
 *      0x186594 = 1625000, 0x186598 = 383000, 0x18659C = 0x14B4C8, 0x1865A0 = 1668000,
 *      0x1865A4 = 968000, 0x1865A8..0x1865BC = 0. The third one is the interesting one:
 *      0x9469C is `LDR R3, =unk_14B4C8` and 0x946A4 stores R3, so the pool entry IDA names
 *      after the zero buffer that happens to live at 0x14B4C8 puts the value 1357832 into a
 *      slot its neighbours fill with round money. Nothing in the image ever dereferences it,
 *      and Geoscape_CloseUI case 19 (0x8BBAC..0x8BBE0) adds all five slots as plain ints into
 *      nFunds. The port therefore keeps it as an i32, not a pointer -- the earlier
 *      `void *pvFundTable` was both the wrong width for a 32-bit block and never read.
 *      Whether the original meant it as an address is unresolved; the stored value is not.
 *   6. 0x94700: month counter 0x159648 = 0, base count 0x159D80 = 0, selected base 0,
 *      GameTime_Reset on the clock at 0x159618.
 *   7. 0x94730: the research mirror: the 144 bytes at 0x160B2C are zeroed and
 *      byte_160BBC[i] = aResearchTopics[i].bActive, for the count byte at 0x160C4C.
 *   8. 0x94798: eight bases: the 144-entry research column (0x16022C) is zeroed, and for
 *      every object the u16 product column (0x15D09C) entries [i] and [i + 2176] and the
 *      u32 column (0x159E9C) entries [i] and [i + 1600] are zeroed -- the +2176 / +1600
 *      stores reach into the NEXT base, exactly as Base_CreateNamedEntry's clear does.
 *   9. 0x947EC: the site count's low byte (0x186410) = 0; all 200 SmapSite records at
 *      0x162030 have bFlag0/bFlag1/bUnk518/bRecoveredSoldierAssign/uRecoveredSoldiers/
 *      bRecoveredCraftCount cleared and auItemQty[0..nObjectCount-1] zeroed.
 *  10. 0x9485C: the facility grid reset, eight bases: fac layers 0/1/2 (grid + 0x174 +
 *      144*b + {0,36,72} + cell) = 0, layer 3 (+108) = -1, and the 36 buildDays bytes
 *      (grid + 36*b) = 0. The ARM walks it as four overlapping byte layers per cell; the
 *      net stores are what matters and they are reproduced per layer.
 *  11. 0x94964: Base_CreateNamedEntry(-6000, -6000, 0, "new") -- the starting base.
 *  12. 0x94984: the 17 facility records: names copied from text records
 *      {106..118, 463, 120, 121, 122} (the same records InitUI used), bFlag20 from
 *      g_abFacFlag20Init, bFlag21 = i, uCost from g_aFacCostInit.
 *  13. 0x94DC0: ten BaseFac_SetCellBuildState calls place the starter facilities.
 *  14. 0x94E34: four starter items at base 0 (objects 43/68/69/70, deltas 1/20/15/10);
 *      selected base = 0; funds = 4147000; soldier count = 0 (u16); craft count = 0 (byte).
 *  15. 0x94E60: Ship_SpawnAtSlot(0) then uRangeRemain = uRangeMax and twenty cargo adds;
 *      Ship_SpawnAtSlot(3) twice, each with uRangeRemain = uRangeMax, nFuel = uFuelMax,
 *      two cargo adds, weapons 25/26 and ammo bytes 10/3.
 *  16. 0x95310: eight soldiers via Unit_CreateAndClassify(0), each then pinned to
 *      nUnitType 0 (bUnk248 from the byte table at 0xABED0, {0,30,45,60} read at 2*type,
 *      so 0 here), nKind 0, bUnk183/uMissions/uKills/bPsiActive/bRankType/bUnk247 = 0.
 *  17. 0x953A4: per base, the five Base_Update*Needed passes with the same budgets the
 *      LoadGame port lifts (CalcOccupiedSpace + OutboundTransferSpace, CalcStorageUsed +
 *      ItemQtyAcrossBases, CountPsiSoldiers, CountActiveResearch, CountActiveManuf).
 *  18. 0x95400: Base_GrantStarterItems(base 0, bGrant 0); nAlienPoolAddend (word_1472C8)
 *      = 0 (STRH); nullsub_7 carries no behaviour and is not ported.
 *  19. 0x95418: screen id 1; Geoscape_BuildNavControls; the research panel control
 *      (pGeoscapeCtlA) parked at (1000, 1000) as two STRH stores; screen id 2; the confirm
 *      control (unk_186BA4) gets NewBaseScreen_TryConfirm; Smap_ZoomOut twice;
 *      byte_187080 = 0.
 *  20. 0x9542C: return StrategList_MaybeSpawnCraft_Process(list, ..., bSpawn 1). The
 *      original's first three arguments are unused inside it (the list is the hardcoded
 *      0x1864F8 + 0x3C); the port passes its own strateg list and spawn context.
 *
 * Deviations: the radar byte is stored as the port's u8 (the original stores dwords at
 * 0x162008 -- the same choice LoadGame's port already makes); the nullsub_7 call is
 * dropped; the original's unused first arguments (0x1864F8 for GrantStarterItems, the
 * MaybeSpawnCraft a1..a3) are replaced by the port's real state pointers. */
/* 0x186568..0x186590, in address order: the five race funding scores, the monthly multiplier, then
 * the five per-race money shares FundingScreen_Init (0x5C0FC..0x5C144) recomputes from the scores.
 * Funding_AddRegionBonus raises one of the first five (or the multiplier, for a no-race tile). */
#define PUFO_ECON_RACE_SCORE_FIRST 0
#define PUFO_ECON_RACE_SCORE_LAST  4
#define PUFO_ECON_RACE_MULT        5 /* 0x18657C, the one dword of the block the save never writes */
#define PUFO_ECON_RACE_SHARE_FIRST 6
#define PUFO_ECON_RACE_SHARE_LAST  10

/* StartNew's initial value of the third bar, 0x9469C / 0x946A4. See note 5 above. */
#define PUFO_FUND_BAR2_INIT 0x14B4C8

typedef struct GeoscapeEconomy {
    i32  anFundingBlock[11]; /* 0x186568..0x186590, see the index constants above */
    /* g_nFundBar0..4 @ 0x186594..0x1865A4: the five funding bars the funding screen draws and
     * the monthly close (Geoscape_CloseUI case 19) adds into nFunds. */
    i32  anFundBar[5];       /* 0x186594, 0x186598, 0x18659C, 0x1865A0, 0x1865A4 */
    i32  aZero44_49[6];      /* 0x1865A8..0x1865BC */
} GeoscapeEconomy;

/* The eight soldiers Game_StartNew creates (0x95310..0x9538C). apSoldiers must have room
 * for them and pnSoldierCount ends at 8. */
#define PUFO_STARTNEW_SOLDIER_COUNT 8

typedef struct StartNewEnv {
    /* Map block and rosters. */
    ObjectRecFull *const *apObjects;  /* Map + 0x108 */
    u16   nObjectCount;               /* Map_nObjectCount */
    Soldier **apSoldiers;             /* Map + 0x1198 */
    i32   *pnSoldierCount;            /* unk_115350 @ Map + 0x1998; the original's field is a
                                       * u16, the port holds it as i32 with u16 semantics, as
                                       * LoadGameEnv and UnitCreateEnv do */
    Ship  **apCraft;                  /* Map + 0x10AC */
    u8    *pbCraftCount;              /* Map + 0x1194 */
    BaseRec *const *apBases;          /* Map + 0x1174 */
    i8    *pbBaseCount;               /* 0x159D80, signed */
    i8    *pnSelectedBaseIdx;         /* 0x159D81 */
    i32   *pnFunds;                   /* nFunds @ 0x1598D8 */
    i32   *pnMonthCounter;            /* unk_159648 */
    u16   *pnAlienPoolAddend;         /* nAlienPoolAddend @ 0x1472c8 */
    GameTime *pTime;                  /* the clock at 0x159618 */

    /* Grid and the per-base economy columns. */
    u8    *abFacilityGrid;            /* gBaseFacGrid @ 0x159768, 1524 bytes */
    u16   *pProductU16Col;            /* unk_15D09C, 200 u16 per base */
    u32   *pProductU32Col;            /* unk_159E9C, 200 dwords per base */
    u16   *pManufCol;                 /* 0x15E19C, Base_CountActiveManuf's column */
    u16   *pResearchCol;              /* unk_16022C, 144 u16 per base */
    u8    *abResearchZero;            /* unk_160B2C, 144 bytes */
    u8    *abResearchActive;          /* byte_160BBC, 144 bytes */
    u8     bResearchCount;            /* 0x160C4C */
    ResearchTopicLive *paTopics;      /* aResearchTopics @ 0x15EF08 */

    /* Sites and the spawn pass. */
    SmapSite *apSites;                /* 0x162030, 200 records */
    i32   *pnSiteCount;               /* 0x186410; only its low byte is stored */
    i32   *apRadarPct;                /* unk_162008: eight dwords, the same block Geoscape_TickHourly's
                                   * empty-cell pass writes */
    PtrArray *pStrategList;           /* the list MaybeSpawnCraft works on */
    const StrategSpawnCtx *pSpawnCtx; /* its roll tables, difficulty and hooks */

    /* The 0x1864F8 block's flags and economy. */
    u8    *pbPlacementPending;        /* 0x1870AC */
    i32   *pnStrategCount;            /* 0x186530 = the block's +0x38 dword. It is the mirror of the
                                       * strateg list count: StrategList_AddItem refreshes it off
                                       * PtrArray_GetCount at 0x82710, StrategList_RemoveAt at 0x82B34,
                                       * Ship_RemoveWithCargo loops on it at 0x368D4/0x3691C, and
                                       * Save_WriteStrateg uses it as its record-loop bound at 0x3FE28.
                                       * Game_StartNew zeroes it at 0x94680 (MOV R8,#0 at 0x94560). */
    u8    *abTypeCounters;            /* 0x18653C..0x186540, five bytes */
    i32   *pnNextDisplayId;           /* 0x186544 */
    i32   *pnNextSiteId;              /* 0x186548 */
    i32   *pnMissionId;               /* 0x18654C */
    u8    *paRegionBuildCount;        /* g_anRegionBuildCount @ 0x18655D: the nine region counters plus the
                                       * tenth byte the IDB still names bUiFrameStyle (0x186566). Game_StartNew
                                       * sets all ten to 1 (0x94650..0x94680), so the first label a region gets
                                       * already reads 1 (BaseFac_FormatCostLabel 0x6A46C). */
    GeoscapeEconomy *pEconomy;

    /* Facility table. */
    BaseFacFlag *paFacFlags;          /* aBaseFacFlags @ 0x186E48 */
    const u8 *pTextRecs;              /* aTextRecs @ 0xB38D0 */

    /* Geoscape UI and viewport. */
    GeoscapeScreenState *pScreen;     /* the screen id at 0x186BC4 */
    UiControl *pCtlResearchPanel;     /* pGeoscapeCtlA, parked at (1000, 1000) */
    UiControl *pCtlConfirm;           /* unk_186BA4, TryConfirm installed */
    SmapView *pView;                  /* 0x1863E8 */
    const SmapZoomSet *pZoomSets;
    SmapWorldSize *pWorldOut;         /* the u16 pair at 0x186BC0 */
    u8    *pbFlag187080;              /* byte_187080 */
    void (*pfnTryConfirmClick)(void); /* the widget thunk for TryConfirm */
    NavControls *pNav;                /* out: the fifteen nav controls */
    const NavCtlEnv *pNavEnv;         /* their fifteen click thunks */

    /* The ported callees' own environments. */
    UnitCreateEnv *pUnitEnv;
    ShipSpawnEnv  *pShipEnv;
    BaseCreateEnv *pCreateEnv;
} StartNewEnv;

i32 Game_StartNew(const StartNewEnv *pEnv);

#endif /* PUFO_GEOIINIT_H */
