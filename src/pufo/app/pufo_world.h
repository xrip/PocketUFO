/* pufo_world.h - the shell's geoscape state: the one block of world state the new-game
 * path builds (Geoscape_InitUI @ 0x857f4 mode 0 + Game_StartNew @ 0x94554) and that the
 * geoscape screens will read.
 *
 * The original keeps this state in the Map block and the 0x1864F8 root; the port gives it
 * to the shell as one struct so every env the ported functions need can point at it.
 */
#ifndef PUFO_WORLD_H
#define PUFO_WORLD_H

#include "game/pufo_baseui.h"   /* GeoscapeScreenState, NavControls, ResearchTopicLive */
#include "game/pufo_boot.h"     /* BootAssets */
#include "game/pufo_combat.h"   /* UnitRosters, the VisCtx roster view */
#include "game/pufo_clock.h"    /* GameTime */
#include "game/pufo_font.h"
#include "game/pufo_geopointer.h" /* GeoscapePointerEnv, the geoscape pointer dispatcher's env */
#include "game/pufo_geoscape.h"
#include "game/pufo_objectrec.h"
#include "game/pufo_options.h"
#include "game/pufo_save.h"
#include "game/pufo_ship.h"
#include "game/pufo_tactinit.h" /* TactInitEnv/TactInitHooks/TactWeaponSel + the callee envs the block below fills */
#include "game/pufo_tactui.h"   /* TactUiState, the inventory drop state Tact_EquipTeamFromCraft drives */
#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "ui/pufo_control.h"
#include "ui/pufo_geoiinit.h"
#include "ui/pufo_geouimanage.h"
#include "util/pufo_ptrarray.h"

#define PUFO_SOLDIER_CAPACITY 512 /* Map + 0x1198 read by size (2048 bytes of pointers) */

/* One owner for every env the geoscape frame reaches, plus its globals. See the block above
 * GeoscapeFrameGlobals in game/pufo_geoscape.h for the evidence and for what the wiring covers; this
 * header holds the definition because BaseFacStatusEnv and GeoscapeGlobeHoverEnv are complete only
 * here (pufo_geoscape.h is the one pufo_baseui.h includes). */
typedef struct GeoscapeFrameState {
    GeoscapeDrawEnv        draw;      /* the dispatcher's arm wiring, 0x8C1B4..0x8C20C */
    GeoscapeMapViewEnv     map;       /* 0x8C2B0..0x8C690 */
    GeoscapeCrosshairEnv   cross;     /* 0x8C69C..0x8C760 */
    GeoscapeRadarArmEnv    radar;     /* 0x8C214..0x8C29C, only while the radar screen is open */
    GeoscapeStatusPanelEnv panel;     /* 0x8C760..0x91DB0, chain tail included */
    GeoscapeGlobeHoverEnv  hover;     /* the call at 0x91D8C, inside the chain tail */
    GeoscapeFrameEndEnv    frameEnd;  /* 0x944B8..0x94544 */
    /* Case 1, the placement screen (0x91DC8..0x92ADC) and its two grid callees. The id lands here from
     * Geoscape_CloseUI case 2 (0x8BE68) and leaves through case 1 (0x8BE94). */
    GeoscapePlacementArmEnv placement;    /* the arm itself */
    GeoscapeScreenArmEnv    placementCtl; /* its control blits */
    FacNameGridEnv          facNameGrid;  /* BaseFacScreen_DrawNameGrid @ 0x61c80 */
    BaseStripDrawEnv        facStrip;     /* Base_DrawFacilityIconStrip @ 0x626dc, cases 1 and 7 */
    /* Case 4 and 5, the soldier screens (0x92DAC, 0x92EE4). Case 4 is where nav control slot 2
     * (SoldierScreen_Toggle @ 0x64af0, installed at 0x67B8C) puts the geoscape; case 5 is where
     * SoldierScreen_SelectListed @ 0x64a9c puts it (0x64AA8 writes 5). Both arms end in the same
     * SoldierScreen_Draw @ 0x65290. */
    SoldierDrawEnv           solDraw;     /* 0x65290, the draw both soldier arms call */
    GeoscapeSoldierArmEnv    solArm;      /* the caption colours, the close control, the transfer gate */
    GeoscapeScreenArmEnv     solPanelCtl; /* apSoldierPanelCtl @ 0x1598B0/B4, the two page buttons, case 4 */
    GeoscapeScreenArmEnv     solFindCtl;  /* apSoldierFindCtl @ 0x1598B8..C0, the three find buttons, case 5 */
    /* Case 7, the build screen (0x92AF0), opened by BaseFacScreen_Open @ 0x61844 (nav slot 4, 0x67BA4).
     * It calls the same two grid callees as case 1, then the build dialog. */
    GeoscapeBaseFacArmEnv    baseFac;     /* its pad, the two block controls, the armed gate */
    BuildDialogEnv           facDialog;   /* BaseFacScreen_DrawBuildDialog @ 0x59b60 */
    FacOverlayEnv            facOverlay;  /* its call at 0x59EF8: BaseFacScreen_DrawPlacementOverlay @ 0x58e10 */
    /* Case 20 (0x944B0..0x944B4), the base-attack report. It is the only screen id that falls into the
     * frame end instead of branching to it, and BaseAtScreen_Draw is inside that fall-through. */
    BaseAtDrawEnv            baseAtDraw;  /* BaseAtScreen_Draw @ 0x58728, over the shell's base-at record */
    BaseFacStatusEnv       fac;       /* the panel head's BuildStatusMsg env, 0x8C82C */
    UiAnimBlitEnv          anim;      /* the 0x1864F8 anim root: 0x187080, 0x187058, 0x186566 */
    GeoscapeFrameGlobals   g;
} GeoscapeFrameState;

typedef struct PufoWorldState {
    /* Rosters. */
    Soldier *apSoldiers[PUFO_SOLDIER_CAPACITY]; /* Map + 0x1198 */
    i32      nSoldierCount;                     /* unk_115350 @ Map + 0x1998 (u16 semantics) */
    i32      nNextSoldierId;                    /* unk_186554 */
    Ship    *apCraft[PUFO_FAC_BASE_COUNT];      /* Map + 0x10AC */
    u8       bCraftCount;                       /* Map + 0x1194 */
    i32      nNextCraftId;                      /* 0x186550 */
    BaseRec *apBases[PUFO_FAC_BASE_COUNT];      /* Map + 0x1174 */
    i8       nBaseCount;                        /* 0x159D80, signed */
    i8       nSelectedBase;                     /* 0x159D81 */

    /* The 0x1864F8 root's flags, ids and economy. */
    i32     nFunds;                             /* nFunds @ 0x1598D8 */
    i32     nMonthCounter;                      /* unk_159648 */
    u16     nAlienPoolAddend;                   /* 0x1472c8 */
    GeoscapeEconomy economy;                    /* 0x186568..0x1865BC */
    i32     anFundTable[16];                    /* the 0x14B4C8 zero buffer */
    /* The funding block outside the 0x1864F8 root, which the monthly report needs and nothing else
     * in the shell held: the five per-country upkeep baselines FundingScreen_Init writes (0x1595F0..
     * 0x159600), the display total (0x159604, zeroed by FundingScreen_Toggle at 0x5AC0C), the screen's
     * open byte (0x1595EC, which FundingScreen_Init does NOT set) and the funding pad (0x1595DC, the
     * pad FundingScreen_Init allocates and Geoscape_CloseUI case 19 frees). */
    i32     anFundBaseline[PUFO_FUND_ROW_COUNT];
    i32     nFundDisplayTotal;
    u8      bFundScreenOpen;
    PadImage *pFundPad;
    u8      bPlacementPending;                  /* 0x1870AC */
    u8      bStrategCount;                      /* 0x186530 */
    u8      abTypeCounters[PUFO_CRAFT_TYPE_COUNT]; /* 0x18653C..0x186540 */
    i32     nNextDisplayId;                     /* 0x186544 */
    i32     nNextSiteId;                        /* 0x186548 */
    i32     nMissionId;                         /* 0x18654C */
    i32     nCreateNextSiteId;                  /* dword_186558 */
    /* g_anRegionBuildCount @ 0x18655D: one signed counter per region 0x80..0x88, and the tenth
     * byte is 0x186566 (IDA's bUiFrameStyle), which StartNew's ten-byte sweep also sets. InitUI
     * writes the nine counters only; the save writes the nine only (0x3F304). */
    u8      aRegionBuildCount[10];
    i8      nSaveVersion;                       /* nSaveFmtVer @ 0x18655C */

    /* The menu/load-path flags LoadGame writes through its env. */
    u8      bTactFlag;                          /* Map.bTactFlag @ 0x1139B8 */
    u8      bGameState;                         /* unk_15821C */
    u16     nUiScreen;                          /* Map_nUiScreen */
    i32     nListMode;                          /* MainMenu_nListMode @ 0x157188 */
    u8      bMissionCount;                      /* byte_138E8C */
    u8      bCraftSelectAssaultFlag;            /* 0x1870A8 */

    /* Grid and the per-base columns. */
    u8  abFacilityGrid[PUFO_GRID_SIZE];         /* gBaseFacGrid @ 0x159768 */
    u8  abGridHangarBytes[PUFO_FAC_BASE_COUNT]; /* 0x159DBC = abFacilityGrid + 1620 in the image: the
                                                 * eight base bytes the three hangar products clear */
    u16 auProductU16[4096];                     /* unk_15D09C, 200 per base + the +2176 spill */
    u32 auProductU32[3600];                     /* unk_159E9C, 200 per base + the +1600 spill */
    u16 auManufCol[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT]; /* 0x15E19C */
    u16 auResearchCol[PUFO_RESEARCH_TOPIC_COUNT * PUFO_FAC_BASE_COUNT];  /* auResearchCol @ 0x16022C */
    u8  abResearchZero[PUFO_RESEARCH_TOPIC_COUNT];   /* unk_160B2C */
    u8  abResearchActive[PUFO_RESEARCH_TOPIC_COUNT]; /* byte_160BBC */
    u16 nResearchEntries;                       /* 0x160C4C, the shared entry count */
    ResearchTopicLive aTopics[PUFO_RESEARCH_TOPIC_COUNT]; /* aResearchTopics @ 0x15EF08 */

    /* Sites and the strateg list. */
    SmapSite *apSites;                          /* 0x162030, PUFO_SMAP_SITE_COUNT records */
    i32       nSiteCount;                       /* 0x186410 */
    i32       abRadarPct[PUFO_FAC_BASE_COUNT];  /* unk_162008, eight dwords: the radar percent and the
                                   * hourly empty-cell counts in one block, as geo.apEmptyFacilityCells */
    u8        abSiteState[PUFO_FAC_BASE_COUNT]; /* byte_161F38, one byte per base */
    PtrArray  strategList;                      /* 0x186534 */
    i32       nStrategSeed;                     /* 0x18640C */
    BaseSiteRec aSiteRecs[PUFO_FAC_BASE_COUNT]; /* 0x186C40 */

    /* Geoscape UI and viewport. */
    GeoscapeUiState   ui;
    GeoscapeScreenState screen;
    SmapView          view;                     /* gSmapCam @ 0x1863E8 */
    SmapZoomSet       sets[3];
    u8                bGeoscapeUi;              /* Map_bGeoscapeUi @ Map + 0x44 */
    u8                bRoot34;                  /* bBaseFacPreviewActive @ 0x18652C: the aimed placement byte. The
                                     * pointer press sets it (0x87F44), the release clears it (0x89E14), the overlay
                                     * commit clears it (0x59B30) and InitUI clears it (0x85860). */
    u8                bRoot35;                  /* bBaseFacPreviewOk @ 0x18652D: set by the case-7 release (0x89E84),
                                     * cleared by the overlay on a zero score (0x599B0) and at its commit (0x59B34). */
    i32               nSmapOriginX;               /* nSmapOriginX @ 0x159DC4, written 10 by Geoscape_InitUI
                                     * (0x86AAC) and read by the placement overlay, the name grid and the
                                     * placement arm */
    i32               nSmapOriginY;               /* nSmapOriginY @ 0x159DC8: read in the same three places,
                                     * and NOTHING in the image writes it -- the .data value 0 it starts
                                     * with is the value it keeps */
    /* Map_nGlyphPalBase @ 0x1183F4 is ONE global, and the font reads it directly
     * (Font_DrawGlyph picks palette slot (Map_nGlyphPalBase + nPal), Text_DrawString temporarily
     * forces it to 2). The shell therefore does not keep its own copy: every env that the original
     * writes it through is wired to g_nGlyphPalSlotBase, the port's single instance of it. */
    BaseFacFlag       aFac[PUFO_BASEFAC_COUNT]; /* aBaseFacFlags @ 0x186E48 */
    MissionSiteRec    aMissionSites[PUFO_MISSION_SITE_COUNT]; /* unk_1865C0 */
    UiControl         ctlResearchPanel;         /* pGeoscapeCtlA -- see the separate issue: this is a second
                                                 * object, while the original reaches the control InitUI
                                                 * allocated through the global pointer */
    u8                bUiAnimStarted;           /* bUiAnimStarted @ 0x187080: the anim root's
                                                 * "the panel animation already started" byte */
    UiLineList        msgLines;                 /* the root message list @ 0x158890 */
    i32               nSelectedStrategIdx;      /* 0x186BE4 */
    i32               nSelectedShipIdx;         /* 0x187054 */
    i32               nMsgShipIdx;            /* nMsgSelectedShipIdx @ 0x159E74, an apCraft index */
    i32               nRadarPulseTimer;         /* nRadarPulseTimer @ 0xAC224 */
    /* The three radar counters Geoscape_PulseUpdate saturates on every screen (0x95B80..0x95BCC).
     * nRadarCooldownR @ 0xAC218 is already the shell's frame cell pFrame.g.nTapCooldown -- one latch,
     * two names -- so only the other three live here. */
    i32               nRadarCooldownL;          /* nRadarCooldownL @ 0xAC21C */
    i32               nRadarMsgTimer;           /* nRadarMsgTimer @ 0xAC220 */
    /* The four held-direction bytes: bGeoscapeScrollUp / Right / Down / Left @ 0x1864F4..0x1864F7,
     * written by Geoscape_UiFocusPrev/Next (0x8b00c / 0x8b260) and Geoscape_SetPanelFlagA/B
     * (0x8b448 / 0x8b478), consumed and cleared by the pulse (0x95714..0x95880). The panel-scroll
     * dispatcher's 0 / 2 arm writes them, so they are the map-scroll state. */
    u8                abScrollFlags[4];
    /* The geoscape event pass (StrategList_ProcessEvents @ 0x7fcb0) and the intercept pair its
     * arrival dispatch writes. */
    u8                bSpawnOnce;               /* 0x162028, the one-shot replacement-craft byte */
    u8                bMapSelectionFlag;        /* bMapSelectionFlag @ 0x159E84 */
    i32               nInterceptShipIdx;        /* nInterceptShipIdx @ 0x15967C */
    i32               nInterceptStrategIdx;     /* nInterceptStrategIdx @ 0x159680 */
    /* The four panel gates the original keeps in one twelve-byte run at 0x18709C: byte 0 is
     * bInterceptDone, byte 4 (0x1870A0) is the status panel's multi-select gate, and 0x18708C /
     * 0x187090 / 0x187098 are the crash / site / alien-base panel gates the radar click sets. */
    u8                bInterceptDone;           /* bInterceptDone @ 0x18709C */
    u8                bFlag1870A0;              /* byte_18709C[4] @ 0x1870A0 */
    u8                bFlag18708C;              /* 0x18708C, bSmapUfoCrashPanel */
    u8                bFlag187090;              /* 0x187090, bSmapSitePanel */
    u8                bFlag187098;              /* 0x187098, bSmapAlienBasePanel */
    OptionsScreenHooks optionsHooks;            /* the radar click's options branch */
    NavControls       nav;
    NavCtlEnv         navEnv;

    /* The geoscape map click (Geoscape_OnMapClick @ 0x8b4a8) and the base-facility screen it can
     * open. The tap coordinates stay zero until Geoscape_OnPointer (0x872d0) is ported. */
    i32          nSmapTapX;             /* nSmapTapX @ 0x186B7C */
    i32          nSmapTapY;             /* nSmapTapY @ 0x186B80 */
    u8           bPlaceActive;          /* bBasePlaceActive @ 0x1870BC */
    u8           bPlaceFail;            /* bBasePlaceFail @ 0x1870B8 */
    u8           bFacDone;              /* bBaseFacDone @ 0x187078, byte 4 of the twelve bytes IDA
                                         * lumps at 0x187074. The geoscape pointer dispatcher's
                                         * pbNewBaseMode is the same byte (0x874E4), so one field
                                         * serves both. */
    u8           bPendingRegionId;  /* bPendingBaseKind @ 0x159DF4: 0xFF is the "nothing pending"
                                         * sentinel, but the image's .data value is 0 and the port
                                         * keeps it (see World_Init) */
    i32          nPlaceX;               /* 0x159D94 */
    i32          nPlaceY;               /* 0x159D98 */
    char         szLabelScratch[108];   /* szRadarMsg @ 0x186B10 */
    UiControl    ctlFacUp;              /* the control the original allocates at 0x158874 */
    UiControl    ctlFacDown;            /* ... and 0x158878 */
    i32          nUnk15887C;            /* 0x15887C */
    i32          nUnk158880;            /* 0x158880 */
    u8           bArmedPlacement;       /* bBaseFacPlaceArmed @ 0x158888, a byte */
    u8           bFacDialogOpen;        /* unk_158884: 1 while the build dialog exists */
    u8           bFacOverlayShown;      /* bBaseFacOverlayShown @ 0x15888C, written by the dialog (0x59FAC)
                                         * and by the placement overlay (0x591F0) */
    UiControl   *apFacPanelCtl[2];      /* the two block controls, 0x158874 / 0x158878, as the arm's array */
    UiPanel     *pFacPanel;             /* unk_15886C, the dialog's UiPanel */
    i32          nPanelSel;             /* unk_158868 */
    u8           abLastRowName[4];      /* the 0x187024 slot the dialog's 17th row reads */
    u32          nShortfall1870C8;      /* byte_1870C8, written as a dword */
    i32          nResidue1870B4;        /* dword_1870B4[0] */
    /* The base-attack record, the run at 0x158840..0x158864. Its one writer is the assault branch of
     * Geoscape_SpawnMissionSite (nPhase 2 with subtype 6, 0x7EF4C..0x7EF80): the class byte from the
     * attacking item's +2, the strateg index from its +0x34 seed, the base index and the race from +0x84
     * and +0x70. Its readers are the three screen callees -- BaseAtScreen_Update @ 0x57808 builds the
     * panel and the pad into the two pointer slots, BaseAtScreen_Draw @ 0x58728 reveals the rows through
     * the shared radar cooldown, BaseAtScreen_CloseThunk @ 0x577c0 hands nBaseAtStrategIdx to
     * StrategList_RemoveAt. Geoscape_CloseUI case 20 (0x8BAF4) is the teardown that calls the same opener
     * with -1, which runs Update's release arm and frees both. */
    i8           nBaseAtClass;          /* 0x158840, the attacking craft's size class, read SIGNED */
    i32          nBaseAtStrategIdx;     /* 0x158844 = the item's nSeed (+0x34), not its list index */
    i8           nCraftHomeBase;        /* 0x158848 = the item's nTargetIdx, the base index, read SIGNED
                                         * (LDRSB) by the defence and teardown paths that follow */
    i8           nAssaultBaseRace;      /* 0x158849 = the item's bRace (+0x70): the sprite set the
                                         * landing craft selects through */
    PadImage    *pBaseAtPad;            /* 0x15884C, allocated and released by Update */
    u8           bBaseAtMissionFollows; /* 0x158850: the budget survived, so a tactical mission follows */
    u8           bBaseAtConfirmShown;   /* 0x158854: every row is revealed and the OK control is drawn */
    i32          nBaseAtLineLimit;      /* 0x158858, the row count the draw has to reveal */
    UiPanel     *pBaseAtPanel;          /* 0x15885C, allocated and released by Update */
    i32          nBaseAtLineCap;        /* 0x158860, the built line count, restored before the lines go */
    u8           bBaseAtActive;         /* 0x158864, the build/release gate */
    /* The Map block's mission header. Map is the context block at 0x1139B8, so Map+0x1511A is
     * 0x128AD2, and these are the cells the GEOSCAPE side writes before a tactical mission
     * starts: Geoscape_CloseUI case 20 sets Map_bMissionMode (0x8BB20) and hands over to
     * Smap_BeginCraftSelect @ 0x838dc, whose Xcom-assault arm fills the type, the four sprite
     * sets and the three geometry bytes; the two bankruptcy paths set the tick/battle pair
     * (0x8BC04, 0x84238). The tactical half of the block -- the cell grid, the rosters' live
     * fields, the TactState region -- is NOT here: see the craft-select env below for what is
     * still a seam and why. A save game restores these through the tgamedata chunk, and
     * Save_ReadTGameData @ 0x44248 is not ported yet, so a loaded game rebuilds them from the
     * mission-follow call, not from the file. */
    i32          nMapMissionTierRaw;    /* Map_nMissionTierRaw @ Map+0x1510C (0x128AC4), the tier budget */
    u8           nMapTerrainVariant;    /* Map_nTerrainVariant @ Map+0x15111 */
    u8           bMapUnk15112;          /* Map+0x15112, the third byte of the same dword */
    u8           nMapCraftIndex;        /* Map_nCraftIndex @ Map+0x15113 */
    u8           nMapMapLayers;         /* Map_nMapLayers @ Map+0x15114 */
    u8           bMapUnk15115;          /* Map_bUnk15115 @ Map+0x15115, the stealth-budget scale */
    u8           abMapSpriteSet[4];     /* Map_abSpriteSet @ Map+0x15116..19; 23 means "no set" */
    u8           nMapMissionType;       /* Map_nMissionType @ Map+0x1511A (0x128AD2) */
    u8           bMapMissionMode;       /* Map_bMissionMode @ 0x128AD5 = Map+0x1511D */
    u8           bMapUnk1511E;          /* unk_128AD6 @ Map+0x1511E */
    u8           bMapSpawnAlt;          /* Map_bSpawnAlt @ Map+0x15148 */
    u8           bMapTickHold;          /* Map_bTickHold @ 0x113A12 = Map+0x5A */
    u8           bMapBattleEnded;       /* Map_bBattleEnded @ 0x113A14 = Map+0x5C. Map_InitAndLoadRegistry
                                         * (0x226A8) clears it at startup, which is why App_InitStartupUi
                                         * now points its MapInitEnv at this byte instead of at NULL. */
    u8           abMapTileTypes[36];    /* aMissionTileTypes @ 0x159D5C: the 6x6 tactical tile-type grid
                                         * Smap_InitCraftSelectState @ 0x61b94 builds from the facility grid */
    u8           bScratch187084;        /* unk_187084: the craft selector writes 0 here (0x841FC) and
                                         * nothing in the whole image reads it */
    /* The tactical mission the launch hand-over builds (Tact_InitMission @ 0x3c550, run by
     * World_InitTactical through the craft-select seam). These are the Map-block tactical cells
     * the block above does not already own, plus the storage the mission brings up: the original
     * keeps them in the Map block and in malloc'd blocks its globals point at (apCells slots,
     * appMapTactical, appMapCtx, the showship blob), and the shell mirrors that ownership here.
     * bTactFlag (Map+0x00), nUiScreen (Map+0x3A), bMapTickHold/bMapBattleEnded, bMapSpawnAlt,
     * bMissionCount (unk_138E8C @ Map+0x254D4) and the mission-header bytes above are reused as-is.
     * Docs/parity/game-core.md Unit 108 records the storage decisions and the deviations. */
    bool          bTactLive;            /* not an original cell: the shell's own "the mission is up"
                                         * flag, set by World_InitTactical, cleared by World_FreeTactical */
    PufoFont     *pFont;                /* fontSys @ 0x158268: World_NewGame's font, kept for the tact
                                         * env, the alien sprite loader and the terrain sprite hook */
    const PufoOptions *pOptions;        /* the shell's option block: its volume steps feed the tact tail's
                                         * two Sound_Apply*Volume calls, and its gamma / speed fields are
                                         * the registry-load model of the two cells below */
    GameMap      *pTactMap;             /* the mission's live map context, malloc'd like the original's
                                         * heap block whose slot Tact_Shutdown frees. Its nMissionType/
                                         * nMapLayers mirror the world fields above at launch -- same
                                         * original cells, two struct views (the aCells duplicate is the
                                         * recorded separate issue) */
    MapCell      *apTactCells[14400];   /* the original pointer table Map+0x15338: Map_BuildMissionTerrain
                                         * mallocs every cell, the teardown frees every slot (the shape
                                         * Tact_Shutdown's Shutdown_FreeCells uses) */
    MapCell      *pTactDeployCell;      /* Tact_pDeployCell @ 0x157204, written by Tact_SetupCraftCargo */
    TactState    *pTactState;           /* pMapTactical @ 0x157198: malloc(0x111E8), the block whose slot
                                         * Tact_Shutdown frees */
    RmpData       sTactRmp;             /* pRmpMissionData @ 0x15881C. Inline: TactBuf11408_ctor fills it
                                         * and Rmp_FreeNodes releases its nodes (the original frees the
                                         * block itself -- deviation recorded in Unit 108) */
    void         *pTactShowShip;        /* dword_156A78: the showship.dat blob, malloc'd to its header size */
    PadImage     *pTactBriefPad;        /* unk_15692C: the briefing pad Tact_InitMission drops without
                                         * freeing (deviation 3); the shell releases it at teardown */
    u8            abTactLightmap[PUFO_TACTINIT_LIGHTMAP_LEN]; /* unk_156AB8 */
    TactWeaponSel sTactWeaponSel;       /* unk_10DB90: apObjects[1], the constant 10, anAmmoTypeId[0] */
    TactItem      sMapSentinel138EA0;   /* unk_138EA0 @ Map+0x254E8: 0x18 bytes, the same size as this
                                         * record. Tact_InitMission stores an apObjects entry at +0 (its
                                         * pObj) and 0x7D00 at +0x14 (the IDB's unk_138EB4 @ Map+0x254FC);
                                         * Unit_ResetBattleState parks its address in two backpack slots,
                                         * and InvDropCtx.pSentinel points at it */
    TactItem      sTactItem156A88;      /* TactItem_156A88 @ 0x156A88: pObj = apObjects[PUFO_SHOT_OBJ_VARIANT3] */
    TactItem      sTactItem156910;      /* TactItem_156910 @ 0x156910: pObj = apObjects[PUFO_SHOT_OBJ_VARIANT2] */
    TactItem      sTactItem156A58;      /* TactItem_156A58 @ 0x156A58: pObj = apObjects[141] */
    LosTables     sTactLoft;            /* lofttemps.dat + unitloft.dat, filled by Tact_LoadLoftData */
    VisRay        aTactVisRays[PUFO_VIS_RAY_COUNT];      /* unk_B2278, 120 records */
    u8            abTactVisSteps[PUFO_VIS_TABLE_BYTES];  /* byte_AD720 */
    TactItem     *apTactItems[2048];    /* Map.apTactItems @ Map+0x23438: the block up to Map+0x25438
                                         * is 2048 pointer slots in the original */
    i32           nTactItemCount;       /* Map_nTactItemCount @ Map+0x1513C */
    i32           nMapFlobjsWide;       /* the dword at Map+0x118, Save_AllocFlobjs's count-width flag */
    Soldier      *apTactHidden[30];     /* apHiddenSoldiers @ Map+0x25438 (0x25438..0x254B0 = 30 slots) */
    i32           nMapEnemyReserveCount;/* Map_nEnemyReserveCount @ Map+0x15144, the Xcom-base alien park */
    u8            bMapEnemyTurnActive;  /* Map_bEnemyTurnActive @ 0x113A13 = Map+0x5B: set by GameTick_Step 0x27364 when the slot-8 byte is set,
                                         * cleared at the phase wrap 0x274B4 and by Tact_CheckBattleEnd 0x3523C */
    u8            bMapMissionVictory;   /* Map_bMissionVictory @ Map+0x150D4 (was unk_128A8C): Tact_CheckBattleEnd writes it at 0x350F0 (won),
                                         * 0x35070 (lost) and 0x351CC (aborted base defence); the debrief caption and the tally gates read it */
    i32           nMapMissionOutcome;   /* Map_nMissionOutcome @ Map+0x1A1C: 0 won / 1 lost / 2 aborted (0x350F4, 0x3507C, 0x3510C); the original
                                         * has no reader anywhere -- the dead store is reproduced */
    i32           nMapDebriefLine;      /* Map_nDebriefLine @ Map+0x150D0: the debrief's line cursor, shared by the label and value panels */
    char          aMapMissionCaption[30]; /* Map_szMissionCaption @ Map+0x15094: the 0x1E-byte "Victory"/"Defeat" caption (0x353C4, 0x35400) */
    char          aMapMissionScore[30];   /* Map_szMissionScore @ Map+0x150B2: the debrief score as text (0x3654C) */
    u8            bMapPromotionReport;  /* Map_bPromotionReport @ 0x1153D8 = Map+0x1A20: set when the promotion report has rows; the debrief cancel
                                         * sends the debrief to screen 19 when it is set (0x162B8) */
    i32           nMapDeployedHwps;     /* unk_128AF8 @ Map+0x15140, the running hardware-soldier count */
    i32           aMapScatterMid[576];  /* unk_1556D0: the phase-5 scatter tables */
    i32           aMapScatterLast[576]; /* unk_155FD0, its pair; the 6x6 tile scan bounds them at 36 x 16 */
    i32           nMapScatterCount;     /* unk_157200 */
    i32           aTactCargoSplit[160]; /* unk_156CD0, one entry per object: objects.dat ships 160 records */
    i32           aTactCargoWork[480];  /* unk_157208, PUFO_CARGO_WORK_PER_OBJ per object */
    u16           nMapObjectCountU16;   /* TactCargoEnv wants Map_nObjectCount @ Map+0x10A8 as a u16* */
    GameClock     gameClock;            /* unk_128AB4 @ Map+0x150FC: the live speed cell Timer_StartBySpeed
                                         * and StartTimers_BySpeed read. PufoOptions.nGameSpeed models the
                                         * registry-loaded startup value; World_NewGame/World_LoadSlot copy it
                                         * in, and GameSpeed_Set writes it during a mission */
    u8            bMapUnk1139B9;        /* unk_1139B9 @ Map+0x01: 1 for a new mission, 0 with no deployed team */
    u8            bMapPlayerInput;      /* unk_1139F0 @ Map+0x38 */
    u8            bMapFire0;            /* unk_113A08 @ Map+0x50 (Map+0x51 is tactMap.bFireStatus1) */
    u8            bMapFire2;            /* byte_113A0A @ Map+0x52 */
    i32           nMapUiSlot1;          /* dword_113A1C @ Map+0x64 = apUiSlotsA[1] */
    u8            bMapSweepArmed;       /* Map_bSweepArmed @ Map+0x68 = LOBYTE(apUiSlotsA[2]). This byte also
                                         * backs MapInitEnv.pbSlot2Byte0 now: one cell, one owner */
    u8            bMapSlot8Byte1;       /* unk_113A39 @ Map+0x81 = BYTE1(apUiSlotsA[8]): read by
                                         * GameTick_Step 0x27364/0x274B4 and the TickActiveUnit gate
                                         * at 0x275CC, cleared at the phase wrap 0x275CC */
    u8            bMapSlot8Byte2;       /* unk_113A3A @ Map+0x82 */
    u8            bMapSlot8Byte3;       /* unk_113A3B @ Map+0x83 */
    i32           nMapUiSlot9;          /* unk_113A3C @ Map+0x84 = apUiSlotsA[9] */
    i32           nMapUiSlot10;         /* unk_113A40 @ Map+0x88 = apUiSlotsA[10], init sets 10 */
    i32           nMapUiSlot11;         /* unk_113A44 @ Map+0x8C = apUiSlotsA[11] */
    u8            bMapAltOutcomePath;   /* unk_1153D9 @ Map+0x1A21 */
    u8            bMapCraftMapX;        /* Map_nCraftMapX @ Map+0x15139 (0x128AF1): who reads it is listed
                                         * in the pufo_tactctl.h correction note */
    u8            bMapCraftMapY;        /* Map_nCraftMapY @ Map+0x1513A (0x128AF2) */
    i32           nMapShotOrigX;        /* Map_nFireOrigX @ Map+0x14054: the ShotTrace nOrigX cell, the
                                         * first sample tile of the last traced arc */
    i32           nMapShotOrigY;        /* Map_nFireOrigY @ Map+0x14058 */
    i32           nMapSpawnTileX;       /* Map_nSpawnTileX @ Map+0x1514C, the random module tile the */
    i32           nMapSpawnTileY;       /* builder picks on the empty-layers path (Map+0x15150) */
    u8            bMapDrawIsoLayer;     /* Map_bDrawIsoLayer @ Map+0x254B0: 0 through init, 1 at the tail */
    u8            bMapLightEnabled;     /* gap_86301[1] @ Map+0x254B1, Map_RefreshCellMeta's gate */
    i32           nMapUnk138E6C;        /* unk_138E6C @ Map+0x254B4: rand(0x28) + 10 */
    i32           nMapUnk138E70;        /* unk_138E70 @ Map+0x254B8: rand(0x14) */
    u8            bMapFrameScratch;     /* unk_138E74 @ Map+0x254BC: GameTick_OnTimer clears it at
                                         * 0x275E8 at the end of every tactical tick */
    i32           nMapGamma;            /* Map_nGamma @ Map+0x15100: the live cell; PufoOptions.nGamma is
                                         * the registry-load model, copied in by World_NewGame /
                                         * World_LoadSlot (Map_InitAndLoadRegistry @ 0x22648 writes that cell) */
    InvStatHooks  tactInvHooks;         /* Unit_RecalcInventoryStat's hooks, filled over pAssets->apObjects */
    InvHitState   sTactHit;             /* the equip/drag hit state @ 0x0040/0x4A28.. */
    InvDropCtx    sTactDrop;            /* the inventory drop Tact_EquipTeamFromCraft drives */
    InvDropHooks  sTactDropHooks;       /* its hooks, filled by World_InitTactical */
    TactUiState   sTactUi;              /* TactUiState @ the Map ui cells -- its nUiScreen/bActionLatch are
                                         * the same cells world nUiScreen / tactMap.bActionLatch model (the
                                         * recorded multi-view issue) */
    SpotCtx       sTactSpot;            /* VisCtx's spotting state. bBusy/bQueueActive/pSelUnit are copies
                                         * of the tact state (TactCtx272.bBusy, TactState.bQueueActive,
                                         * pMap->pSelUnit); World_TactTickActiveUnit refreshes them every
                                         * tick. pfnFocusOnCell is wired since Part 50: MapView_FocusOnCell
                                         * over tactEnv.focus/cursor, the walker's own camera call */
    i32           nMapTrajFront1036;    /* pCtxBase.nTrajFront0[1036]: the one dword Unit_SpotCellOccupant
                                         * writes when it spots something */
    Soldier      *pTactScratchUnit;     /* the anActionPhaseSave0[120] slot the visibility scan excludes */
    UnitRosters   sTactRosters;         /* the three rosters as Unit_ScanVisibleUnits wants them. The counts
                                         * are the launch values; live counts are the runtime unit's wiring */
    TactInitEnv   tactEnv;              /* the persistent env World_InitTactical refills every launch */

    /* --- the tactical walker (Tact_TickActiveUnit @ 0x54130), wired to the live frame ---
     * The three queue processors take their contexts as arguments; World_TactTickActiveUnit rebuilds
     * them from pTactMap/tactEnv on every tick, which mirrors the original re-reading the Map block
     * inside each processor. These are the pieces that persist between ticks. */
    StatusHooks      sTactStatusHooks;  /* the busy gate every tact StatusMsg_Push reads (TactCtx272.bBusy) */
    MoveActionWiring sTactMoveWiring;   /* the move FSM's real callbacks over aTextRecs + the vis ctx */
    TurnHooks        sTactTurnHooks;    /* Tact_ProcessActionPhase @ 0x4d950, driven from the walker */
    ActionPhaseSave  sTactPhaseSave;    /* anActionPhaseSave0..2 @ Map+0x15078 */
    TickHooks        sTactTickHooks;    /* the walker's five world calls */
    FireGate         sTactFireGate;     /* aResearchTopics[g_abSlotTopicGate[uSortKey]].wGate4 @ 0x15EF08 + 4 */
    ActionCostTable  sTactActionCosts;  /* pCtxBase.nTrajFront0[1040..1135], filled by Unit_SelectBestFireMode */
    i32              nMapActionMode;    /* the action mode Unit_SelectBestFireMode writes */
    ShotHooks        sTactShotHooks;    /* Combat_ResolveShot @ 0x2f12c: the area-fire arm on the world state;
                                         * pfnComputeFov stays NULL (its argument list is still open,
                                         * pufo_combat.h) */
    ShotOutcome      sTactShotOut;      /* scratch Combat_ResolveShot writes its result into */
    /* The soldier screen (SoldierScreen_Toggle @ 0x64af0), opened by nav control slot 2. */
    u8        bSolScreenOpen;           /* 0x159DE0 */
    UiPanel  *pSolPanelEquip;           /* 0x159DF0 */
    UiPanel  *pSolPanelStores;          /* 0x159758 */
    UiPanel  *pSolPanelNames;           /* 0x15975C */
    UiPanel  *pSolPanelCraft;           /* 0x159DEC */
    PadImage *pSolPad;                  /* unk_1598D4 */
    i32       nSoldierCursor;           /* nSoldierCursor @ 0x159E08 */
    u8        bTransferFlag;            /* bTransferFlag @ 0x159E10, cleared by EquipSelected */
    HangarPlaceEnv hangarEnv;           /* the placement test's env */
    BaseFacInitEnv facInitEnv;          /* the screen-7 branch's env */
    BaseFacToggleEnv facToggleEnv;      /* the build dialog's open/close switch */
    /* Base_CreateNamedEntry @ 0x66930's env. Game_StartNew creates the starting base through it (0x94964)
     * and the placement overlay's commit creates every later one (0x59AFC), so it lives on the state: the
     * original reads its Map blocks at the call, and the three economy columns are offset to the base the
     * call is about to create -- see World_WireCreateEnv. */
    BaseCreateEnv createEnv;            /* the shell's copy of the blocks Base_CreateNamedEntry reads */
    SoldierScreenEnv solEnv;            /* the soldier screen's open/close switch */
    NewBaseOpenEnv newBaseOpen;         /* NewBaseScreen_Open @ 0x615d0, nav slot 0's handler */
    ScreenInitHooks facOpenHooks;       /* BaseFacScreen_Open @ 0x61844's hooks, nav slot 4's handler */

    /* The geoscape frame (Geoscape_Draw @ 0x8C180) and its pointer dispatcher (0x872d0 / 0x89450),
     * wired over this state by World_WireGeoscapeFrame. See pufo_geoscape.h for which screens the
     * wiring covers. */
    GeoscapeFrameState frame;           /* the draw envs and the frame's own globals */
    GeoscapePointerEnv ptrEnv;          /* the pointer dispatcher's env, on the same objects */
    /* The funding screen's two draw arms (Geoscape_Draw case 15 at 0x94304 and case 19 at 0x94314) and
     * the monthly opener the clock calls. Case 19 is the screen FundingScreen_OpenMonthly puts the
     * geoscape into, and Geoscape_DrawFundingArm dereferences its arm env with no test, so the shell
     * has to own it as soon as the clock can run a month. */
    FundingInitEnv       fundingInit;
    FundingDrawEnv       fundingDraw;
    GeoscapeFundingArmEnv fundingArm;
    /* The frame's loose globals the shell owns: the radar screen gate, the two research walk
     * indexes, the three globe-sweep counters the hover panel advances, the anim progress byte,
     * and the two scratch lines the status panel writes. */
    u8        bRadarScreenActive;       /* bRadarScreenActive @ 0x159678 */
    u8        nFinishedTopic;           /* nResearchFinishedTopic @ 0x15EEF8 */
    i32       nShownTopic;              /* nResearchTopicShown @ 0x160C60 */
    i32       nGlobeScanX;              /* nGlobeScanX @ 0x159E00 */
    i32       nGlobeSweepV;             /* nGlobeSweepV @ 0x159DFC */
    i32       nGlobeSweepH;             /* nGlobeSweepH @ 0x159DF8 */
    i8        cUiAnimProgress;          /* cUiAnimProgress @ 0x187058 */
    u8        bFinalMissionCtlArmed;    /* bFinalMissionCtlArmed @ 0x187068: arms the final-mission
                                         * control for one press (0x8759C), cleared on release (0x89CD4) */
    i32       nActionPhaseSave112;      /* Map.anActionPhaseSave0[112] @ 0x128AA0, the frame end's cell */
    char      szUiScratchStr[104];      /* szUiScratchStr @ 0x186420: the status panel's pszExtra,
                                         * the second line of the panel arms */
    char      szScratch[85];            /* szScratch186488 @ 0x186488: BuildStatusMsg's scratch line */
    /* The tick chain's own state block (Geoscape_TickShips / TickHourly / TickBases @
     * 0x5d4e4 / 0x5d1bc / 0x5d3bc, driven by Geoscape_AdvanceTime @ 0x5ce40). Its `time` and `world`
     * members ARE the shell's clock and GeoscapeWorld, so nothing else in the shell keeps a copy, and
     * its pointer members point back into this struct: World_WireGeoscapeClock does the wiring. */
    GeoscapeState     geo;
    GeoscapeHooks     geoHooks;      /* the eleven callees the ticks reach */
    GeoscapeTickBundle geoBundle;    /* the user pointer Geoscape_AdvanceTime's hooks carry */
    GeoscapeClockHooks geoClock;     /* the four hooks, built once by Geoscape_BuildClockHooks */
    /* Geoscape_CloseUI @ 0x8b94c, the dispatcher every screen close tail-calls, wired over this state by
     * World_WireGeoscapeClock. It is what moves nGeoscapeScreen: case 2 (the screen Game_StartNew leaves a
     * new game on) goes to 1, case 1 to 0, and only at 0 does the pulse advance the clock. */
    GeoscapeCloseEnv  closeEnv;
    const UiControlHitCtx *pHitCtx;  /* the WorldHooks' hit context, wired for the pulse's forced clicks */
    const u8    *pnMapPhase;         /* nMapPhase @ 0x1139EF (Map + 0x37): the 0xC352 anim counter the
                                      * WndProc wraps at 7 (0xA0988/0xA098C) and Save_ReadTGameData
                                      * restores (0x444E0). The caller owns it; the placement arm's cursor
                                      * colour 10*(nMapPhase+15) reads it (0x92470). */
    i32         *pnAnimPhase;        /* nUiSlot0AnimPhase @ 0x113A18 (Map + 0x60), the shell's ONE cell
                                      * for the slot UiControl_HitTest clears and Geoscape_PulseUpdate's
                                      * animation-phase gate counts (0x95644..0x95654, cleared at 0x95B78) */

    AssetReader       rdr;                      /* pRdr @ 0x187028 */

    /* Save-load state (LoadGame @ 0x44bbc). The chunk states the readers fill, plus the
     * 17152-byte product block (A u32, B u32, C u16, facility->item map). */
    SaveBasesState    saveBases;      /* includes the base count byte and the type-byte array */
    SaveResearchState saveResearch;
    SaveSmapState     saveSmap;       /* the 200 site records, on-disk order */
    StrategSaveState  saveStrateg;
    SaveTimeRecord    saveTime;
    u8                abProductBlock[17152];
    u32               nTactVerOut;    /* write-only residue, handed out by the port */
    SmapWorldSize     loadWorldSize;  /* 0x186BC0 on the load path (InitUI owns it on the new-game path) */

    /* The callees' own environments, kept here so their by-value state survives a spawn. */
    ShipCtorEnv ctorEnv;
    StrategSpawnCtx spawnCtx;   /* the spawn hooks' wiring; the event pass reaches it back */
    /* The removal pair and the mission-launch selector, built by World_WireMissionSeams over
     * this state. Smap_BeginCraftSelect @ 0x838dc reaches the ship env through its lookup and
     * removal calls (0x83968, 0x83F7C, 0x84428) and the demolish env at 0x84208. */
    ShipRemovalEnv    shipEnv;
    BaseDemolishEnv   demolishEnv;
    SmapCraftSelectEnv craftEnv;
    BootAssets *pAssets; /* set by World_NewGame; the StartNew hook reaches it through here */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser); /* the nav controls reuse it */
    void *pLoadPadsUser;
    void (*pfnPlaySample)(i32 nSample, void *pUser);    /* the message-list sound seam */
    void *pSoundUser;
    const struct WorldHooks *pFrameHooks; /* the WorldHooks World_NewGame was handed; the mission-end rebuild rewires the geoscape
                                           * frame through the same object (WorldHooks lives in main() in both the app and the
                                           * tests, i.e. for the whole run) */
    i32 nLastStartNewRet; /* what Game_StartNew handed back */
} PufoWorldState;

typedef struct WorldHooks {
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser);
    void *pLoadPadsUser;
    void (*pfnPlaySample)(i32 nSample, void *pUser); /* the message-list sound seam */
    void *pSoundUser;
    /* The LoadGame teardown/frame hooks (LoadGameHooks @ pufo_save.h). */
    void (*pfnRenderFrame)(void *pUser);
    void (*pfnStartUiShutdown)(void *pUser);
    /* The geoscape frame's control plumbing: the pad blits every UiControl_BlitActivePad call makes,
     * the Map+0x60 word UiControl_HitTest clears, and the colour pair Gfx_PackRgb565Pair packs. */
    const UiControlHitCtx *pHit;
    GfxColorEnv           *pColor;
    /* Map + 0x60 (apUiSlotsA[0] / nUiSlot0AnimPhase @ 0x113A18). The shell needs the tact repeat
     * counter and the geoscape pulse's animation-phase gate to be the SAME cell, so the caller owns it
     * and World_WireGeoscapeClock stores the pointer. */
    i32                   *pnAnimPhase;
    /* Map + 0x37 (nMapPhase @ 0x1139EF), the 0xC352 anim counter. The caller owns it here too, because
     * the tactical Map block is the caller's, and the placement arm's cursor colour reads it. */
    const u8              *pnMapPhase;
    /* Smap_HitTestStrategSelect @ 0x82be8 needs the caller's SmapHitEnv, which the shell does not
     * build; the frame hands the dispatcher this seam instead (NULL means "nothing selectable"). */
    u8 (*pfnHitTestStrategSelect)(const SmapView *pView, i32 nX, i32 nY, void *pUser);
    void *pHitTestUser;
} WorldHooks;

/* The context Geoscape_SpawnMissionSite @ 0x7e49c needs: the strateg list, the seed counter, the viewport,
 * the level-0 tile grid, the base table, the mission-site table, the facility grid, the radar-pulse timer,
 * the world and the clock total -- all cells the shell owns -- plus the three hooks the mission subtypes
 * reach for (the root message append, the hangar's intercept item, the base-attack screen's opener). The
 * shell calls it at its three mission seams (the load tail, the event pass, the craft roll); it is public
 * because the assault branch is only testable through the hook this fills in. */
void World_FillMissionCtx(PufoWorldState *pW, SpawnMissionCtx *pCtx);

/* Fill every geoscape frame env over this state. Run it after the UI objects exist (Geoscape_InitUI
 * on the new-game path, before LoadGame on the load path, because LoadGame renders through the
 * frame). Idempotent: it rewrites the envs from the current state. */
void World_WireGeoscapeFrame(PufoWorldState *pW, const WorldHooks *pHooks);

/* The geoscape clock, wired by World_WireGeoscapeFrame from the same state: GeoscapeState.geo's
 * pointers into this struct, the eleven GeoscapeHooks callees, and the four GeoscapeClockHooks
 * Geoscape_AdvanceTime calls. pHooks->pHit and pHooks->pColor are stored, not copied, so the caller's
 * UiControlHitCtx / GfxColorEnv must outlive the world. */
void World_WireGeoscapeClock(PufoWorldState *pW, const WorldHooks *pHooks);

/* WM_TIMER 0xC353's geoscape half: UiPulse_OnTimer @ 0x36630 hands straight over to
 * Geoscape_PulseUpdate @ 0x954dc, the tick that advances the clock -- only while nGeoscapeScreen is 0
 * (0x95510, re-tested at 0x95608; a fresh game sits on screen 2, where no clock runs, until a screen close
 * restores 0) -- scrolls the held-direction map drag, auto-repeats the held buttons and steps the four
 * radar counters. */
void World_PulseGeoscape(PufoWorldState *pW);

/* Geoscape_CloseUI @ 0x8b94c over the wired close env: the sound sample, the dwActionFail clear, the five
 * per-base needed-flag passes and the per-screen switch. Called from the twelve 12-byte close thunks the
 * screens install. Case 2 (0x8BE68) and case 1 (0x8BE94) are the 2 -> 1 -> 0 run; case 2 returns early
 * while the base pick is pending (0x8BE58). */
void World_CloseGeoscape(PufoWorldState *pW);

/* Geoscape_OnMapClick @ 0x8b4a8 over the shell's own grid, rosters and base records. On the new-base screen
 * it has two arms: while the pick is pending it commits the pick and writes the screen id 1 (0x8B600),
 * clearing the pending byte Game_StartNew set at 0x945E0 (0x8B608) and the new-base byte (0x8B60C); with no
 * pick pending it seats the selected base on the base COUNT (0x8B644) and writes the id 7 (0x8B64C) without
 * touching the new-base byte. Returns the callee's value. */
i32 World_MapClick(PufoWorldState *pW);

/* Refresh the per-frame fields -- the zoom set's map / layer / tile pads for the mode the view
 * selected, the screen id, the anim frame-style byte -- and run Geoscape_Draw @ 0x8C180. Returns its
 * value, which is the present thunk's. */
i32 World_DrawGeoscapeFrame(PufoWorldState *pW, i32 (*pfnPresent)(void *pUser), void *pUser);

/* Geoscape_OnPointer @ 0x872d0 and Geoscape_OnPointerUp @ 0x89450 over the wired env. nX / nY are
 * logical 240x320 coordinates, as Input_OnPointer hands them over. */
void World_GeoscapePointer(PufoWorldState *pW, i32 nX, i32 nY, bool bDown);
void World_GeoscapePointerUp(PufoWorldState *pW);

/* Allocate the heap-held parts (the 200 site records) and set the defaults the original's
 * zero-filled .data has. */
bool World_Init(PufoWorldState *pW);

/* Geoscape_InitUI(0) + Game_StartNew over this state. Returns Game_StartNew's result. */
i32 World_NewGame(PufoWorldState *pW, BootAssets *pAssets, PufoFont *pFont,
                  const PufoOptions *pOptions, const WorldHooks *pHooks);

/* LoadGame @ 0x44bbc over this state: the slot file is szAppDir + "sav" + slot + ".sav".
 * Returns LoadGame's result (0 when the file is absent). */
i32 World_LoadSlot(PufoWorldState *pW, const char *pszAppDir, i32 nSlot, BootAssets *pAssets,
                   const PufoOptions *pOptions, const WorldHooks *pHooks);

/* Tact_InitMission @ 0x3c550 over this state, wired by World_InitTactical's env fill. nMode 0 is the
 * launch path (the craft-select seam's 0x83AC4 call); any other value would need the save-file blocks
 * this path does not have, and the function then refuses. Brings up the mission's storage (the cell
 * grid, TactState, the rosters' live fields) and sets bTactLive. Returns Tact_InitMission's result. */
bool World_InitTactical(PufoWorldState *pW, i32 nMode);

/* Release what World_InitTactical brought up: every apTactCells slot, the items, the five panels, the
 * showship blob, the rmp nodes, the loft buffers, the map ctx and TactState. The soldier records stay
 * owned by apSoldiers and World_Free. This is the shell's own teardown, NOT Tact_Shutdown @ 0x3d568:
 * the mission-end unit owns that (its outcome table demolishes or removes on the mission's result). */
void World_FreeTactical(PufoWorldState *pW);

/* One 0xC350 firing's handler (GameTick_OnTimer @ 0x274f0), built from the shell's own Map-scalar
 * owners and the live tactical frame. Returns true when the handler took a branch of its own: the
 * geoscape stub return (the original's 0x27518, a four-byte `BX LR`) or a full tactical tick.
 * Without a live frame the tactical branch cannot run (the original's TactState/TactCtx272 exist
 * only for a mission); the defensive false there stands in for the original's NULL deref. */
bool World_GameTickOnce(PufoWorldState *pW);

/* Mission end. World_MissionEndCheck runs Tact_CheckBattleEnd @ 0x34fbc over the live frame -- the
 * shell's stand-in for the combat call sites (Tact_KillUnit 0x4efc8, Tact_ProcessFallAction
 * 0x53318/0x53bd4) that belong to the tactical runtime unit. World_MissionCancelEnd mirrors the
 * debrief arm of UiScreen_OnCancel @ 0x16200: the teardown (the shell's World_FreeTactical stands
 * for Tact_Shutdown @ 0x3d568) and the geoscape rebuild Geoscape_InitUI mode 1 (0x162C8). */
bool World_MissionEndCheck(PufoWorldState *pW, u8 bAbort);
bool World_MissionCancelEnd(PufoWorldState *pW);

/* GeoscapeUi_FreeAll(bFreeWorld 1) plus the nav controls and the site records. */
void World_Free(PufoWorldState *pW);

#endif /* PUFO_WORLD_H */
