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
#include "game/pufo_enemyai.h"  /* EnemyAiTickCtx/EnemyAiActCtx, the phase-3 driver wired in Part 52 */
#include "game/pufo_font.h"
#include "game/pufo_geopointer.h" /* GeoscapePointerEnv, the geoscape pointer dispatcher's env */
#include "game/pufo_geoscape.h"
#include "game/pufo_isodraw.h"   /* IsoDrawCtx + the iso layer envs, the tactical frame (Part 63) */
#include "game/pufo_objectrec.h"
#include "game/pufo_options.h"
#include "game/pufo_path.h"     /* PathToCellCtx/PathScratch, the walk Unit_PathToCell @ 0x2c2a8 runs */
#include "game/pufo_plane.h"    /* the hangar (screen 6): BasePlaneScreen, its panel sets and envs */
#include "game/pufo_save.h"
#include "game/pufo_ship.h"
#include "game/pufo_tactinit.h" /* TactInitEnv/TactInitHooks/TactWeaponSel + the callee envs the block below fills */
#include "game/pufo_tactui.h"   /* TactUiState, the inventory drop state Tact_EquipTeamFromCraft drives */
#include "game/pufo_strateg.h"
#include "game/pufo_unit.h"
#include "ui/pufo_control.h"
#include "ui/pufo_geoiinit.h"
#include "ui/pufo_menubtn.h"   /* TactInputState/TactInputHooks (via pufo_input.h), MenuHooks/MenuPads
                                * (via pufo_menu.h), UiButtonCtx and SaveLoadSlotHooks -- the tact bank
                                * bring-up the shell wires since Part 54 */
#include "ui/pufo_modal.h"    /* UiModalState, the modal bank the shell owns since Part 51 */
#include "ui/pufo_geouimanage.h"
#include "ui/pufo_uibank.h"   /* Tact_LoadUiPads + PUFO_UIPAD_* slots, the tact pad table the shell owns (Part 53) */
#include "util/pufo_ptrarray.h"

#define PUFO_SOLDIER_CAPACITY 512 /* Map + 0x1198 read by size (2048 bytes of pointers) */

/* One owner for every env the geoscape frame reaches, plus its globals. See the block above
 * GeoscapeFrameGlobals in game/pufo_geoscape.h for the evidence and for what the wiring covers; this
 * header holds the definition because BaseFacStatusEnv and GeoscapeGlobeHoverEnv are complete only
 * here (pufo_geoscape.h is the one pufo_baseui.h includes). */
typedef struct GeoscapeFrameState {
    GeoscapeDrawEnv        draw;      /* the dispatcher's arm wiring, 0x8C1B4..0x8C20C */
    GeoscapeMapViewEnv     map;       /* 0x8C2B0..0x8C690 */
    SmapIconDrawEnv        icon;      /* 0x8C694..0x8C698, carried by draw.pIconEnv */
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
    /* Case 6, the hangar (0x9309C..0x93DD4), opened by nav control slot 3 (BasePlaneScreen_Open @ 0x617f4,
     * installed at 0x67B98). Geoscape_DrawPlaneArm reads the object block's fifteen control words, the two
     * stores-row panels and the six stat counts, and calls BasePlaneScreen_Draw @ 0x7d374 at 0x930F8 with
     * the block base; that callee hands the whole frame to BasePlaneScreen_DrawCraftDetail @ 0x7baf8 when
     * bPlaneShowCraftDetail is set, so the detail env travels inside the draw env (0x7D37C). */
    GeoscapePlaneArmEnv      planeArm;    /* the arm itself */
    GeoscapeScreenArmEnv     planeCtl;    /* the blit seam its controls go through */
    PlaneDrawEnv             planeDraw;   /* 0x7d374, the callee's env */
    PlaneDetailEnv           planeDetail; /* 0x7baf8, carried by the draw env */
    /* Case 8, the research screen (0x94114..0x94154), opened by nav control slot 5 (ResearchScreen_Open
     * @ 0x616ac, installed at 0x67BAC). This arm blits NO pad of its own: the DRAW comes first (0x9411C)
     * and ResearchScreen_Draw blits the screen's "Research" pad at 0x73A40; then the arm blits the two
     * topic-list scroll controls at aResearchTopics + 0x1D48 / + 0x1D4C (CMP #2 at 0x9414C). */
    ResearchDrawEnv          researchDraw; /* 0x73a34, the callee's env */
    GeoscapeScreenArmEnv     researchCtl;  /* its two scroll controls, 0x94120..0x94150 */
    /* Case 9 (0x9415C..0x941A4), the manufacturing screen: pad blit, controls [0..1] blit, then
     * ManufScreen_DrawFrame @ 0x6e48c, which owns the panels, the shortfall line and the six
     * bottom controls' pads (0x70AF8). Case 11/12 (0x93DD4..0x93E58): pad blit, controls [0..1],
     * the SHARED BuySellScreen_Draw @ 0x93E10, then the buy/sell tail that tracks the hovered row
     * in panels B and C (0x93EB4). */
    ManufFrameEnv            manufDraw;    /* 0x6e48c, the frame env */
    GeoscapeScreenArmEnv     manufCtl;     /* the tab scroll pair the arm blits (0x15EE1C/0x15EE20) */
    BuySellDrawEnv           buySellDraw;  /* 0x77e6c, the frame both buy and sell run */
    GeoscapeScreenArmEnv     buySellCtl;   /* 0x161A0C / 0x161A10, the two controls the arm blits */
    GeoscapeBuySellTrackEnv  buySellTrack; /* the tail cases 10, 11 and 12 share (0x93EB4) */
    /* Case 10, the transfer screen (0x93E5C..0x93EB4): the "transfer" pad blit, the four controls
     * (CMP #4 at 0x93E8C), TransferScreen_Draw @ 0x97fa4 at 0x93EB0, then the shared tail. */
    TransferDrawEnv          transferDraw; /* 0x97fa4, the callee's env */
    GeoscapeScreenArmEnv     transferCtl;  /* 0x1870DC..0x1870E8, the four controls the arm blits */
    /* Case 16, the ufopaedia screen (0x941AC..0x94288): Ui_DrawBaseInfo @ 0x9b5f4 gets the BLOCK BASE
     * (0x941AC) and blits the screen pad (0x9B600) and the close control (0x9B7C0); the arm blits no pad
     * of its own, only control ranges picked by the three gate bytes -- [4..5] when the page flag is up and
     * the entry dirty byte is clear (0x941DC), [0..3] when dirty AND wide (0x94220) or [2..3] when dirty
     * and not wide (0x9425C). Part 104. */
    UiBaseInfoDrawEnv        ufopaediaDraw; /* 0x9b5f4, the callee's env */
    GeoscapeUfopaediaArmEnv  ufopaediaGates; /* the three gate bytes @ 0x18730C / 0x187310 / 0x1871FC */
    GeoscapeScreenArmEnv     ufopaediaCtl;   /* the six controls the arm blits (0x1872E8..0x1872FC) */
    BaseFacStatusEnv       fac;       /* the panel head's BuildStatusMsg env, 0x8C82C */
    UiAnimBlitEnv          anim;      /* the 0x1864F8 anim root: 0x187080, 0x187058, 0x186566 */
    GeoscapeFrameGlobals   g;
} GeoscapeFrameState;

/* The options subsystem's owners: the shell's copy of the .bss the options screen runs on.
 * `OptionsScreen_Init @ 0x6be74` builds 7 menu rows and 12 radio buttons out of caller-owned storage
 * (its teardown branch runs UiControl_FreePads and leaves the storage), so the nineteen objects live
 * here. `OptionsScreen_Open @ 0x6d0ac` allocates its own prev/next pair and its four panels into the
 * pointer cells below and releases them through its close half, so the shell owns the cells only. The
 * flat 21-slot array is the original's single `aOptionsListCtl @ 0x159E20`, which the frame (0x6C888,
 * 0x6CD60) and the press arm (0x89160, 0x89210) index; World_RefreshGeoscapeFrame rebuilds it from the
 * three groups plus the pair, because the pair only exists while the overlay is open. */
typedef struct OptionsOwners {
    UiControl           aListCtl[PUFO_OPTIONS_LIST_COUNT];  /* aOptionsListCtl[0..6] @ 0x159E20..0x159E38 */
    UiControl           aMusicCtl[PUFO_OPTIONS_VOL_COUNT];  /* [9..14] @ 0x159E44, aOptionsMusicBtn */
    UiControl           aSfxCtl[PUFO_OPTIONS_VOL_COUNT];    /* [15..20] @ 0x159E5C, aOptionsSfxBtn */
    UiControl          *apPairCtl[2];     /* [7] / [8] @ 0x159E3C / 0x159E40: Open's two allocations */
    UiControl          *apCtlFlat[PUFO_OPTIONS_CTL_COUNT];  /* the flat view the frame and the press use */
    UiPanel            *apPanel[PUFO_OPTIONS_PANEL_COUNT];  /* 0x159E18, 0x159E78, 0x159E7C, 0x159E1C */
    OptionsScreenCtl    ctl;              /* the 19 pointers Init fills */
    OptionsVolumeEnv    vol;              /* the state RefreshVolumeHighlight re-selects from */
    OptionsTabEnv       tab;              /* the state LayoutButtons lays out */
    OptionsInitEnv      init;             /* OptionsScreen_Init's env */
    SaveAudioEnv        saveAudio;        /* Options_SaveAudioSettings' env, ctlA's click handler */
    OptionsOpenEnv      open;             /* OptionsScreen_Open @ 0x6d0ac's env, the overlay's half */
    OptionsConfirmEnv   confirm;          /* OptionsScreen_OnConfirm @ 0x6cf18, the names panel's handler */
    GeoscapeScreenHooks screenHooks;      /* Ui_OpenScreen18 / Ui_CloseScreen18's hooks */
    GeoscapeScreenDrawEnv draw;           /* case 18's own env, the call at 0x942F8 */
} OptionsOwners;

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
    /* 0x186530 (the geoscape block's +0x38 strateg count dword) lives in saveStrateg.nStrategCount:
     * the original keeps one cell there, written by Game_StartNew (0x94680) and by Save_ReadStrateg
     * (0x411CC) and read by Save_WriteStrateg's loop (0x3FE28) and Ship_RemoveWithCargo (0x368D4). */
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
    /* The options screen's own two cells, the run 0x159E80 / 0x159E84 / 0x159E88 that the original
     * keeps in one block. bOptionsScreenOpen @ 0x159E80 is the toggle every options reader tests --
     * OptionsScreen_Open 0x6D0B4/0x6D0C8/0x6D1DC/0x6DC40, the pointer confirm gate 0x89338, the
     * release arm 0x8AEB0, Geoscape_CloseUI 0x8BD04, Geoscape_PulseUpdate 0x95B34, the draw overlay
     * 0x6C368. gOptionsScratch @ 0x159E88 is the IDB's `char[8]`: [0] is the menu id, read SIGNED at
     * 0x89128 and 0x6C68C, [4] is the "a menu is up" byte the pointer (0x89110), the draw (0x6C640),
     * OptionsScreen_Init (0x6BE78, cleared at 0x6C0FC) and the save/load slot handlers all use. The
     * shell owns both because the original's globals are never absent: Geoscape_OnPointer case 18
     * derefs 0x159E8C and 0x159E80 with no null test, and a press on a base entry reaches it. */
    u8                bOptionsScreenOpen;       /* bOptionsScreenOpen @ 0x159E80 */
    i8                abOptionsScratch[8];      /* gOptionsScratch @ 0x159E88, the IDB's char[8] */
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
    OptionsOwners       options;                /* the shell's owner of the options screen's own state */
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
     * still a seam and why. A save game restores these through the tgamedata chunk;
     * Save_ReadTGameData @ 0x44248 is ported at pufo_save.c:2791, so a loaded game takes them
     * from the file. */
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
    /* --- the load path's mission bring-up (Part 65) ---
     * LoadGame holds the slot file open across Tact_InitMission(1) (the call at 0x44E24 sits before
     * DatFile_Close at 0x44E2C) and the mission's whole tactical half is read out of it there. These
     * stage what the readers fill, and the hook block carries the callbacks the reader needs -- the
     * original reaches all of them through the Map block directly. The rosters point at pMap's own
     * arrays and at apTactHidden, exactly as World_SaveSlot's write side does. */
    DatFile        *pLoadDat;           /* borrowed from World_LoadSlot, never owned */
    TactSaveState   sTactSave;          /* the 23 tgamedata fields */
    TeamUnitsState  sTactSaveRosters;   /* teamunits / aliens / diedsolds / hiddensold (not the sTactRosters
                                         * UnitRosters view the vis ctx uses) */
    FlobjsReadState sTactFlobjs;
    TactResetState  sTactReset;         /* the ~30-field tactical reset at 0x444C0..0x445CC */
    TactLoadHooks   sTactLoadHooks;
    void         *pTactShowShip;        /* dword_156A78: the showship.dat blob, malloc'd to its header size */
    PadImage     *pTactBriefPad;        /* unk_15692C: the briefing pad Tact_InitMission drops without
                                         * freeing (deviation 3); the shell releases it at teardown */
    PadImage     *apTactPads[PUFO_UIPAD_SLOT_COUNT];
        /* The tact pad table Tact_LoadUiPads @ 0x3b068 fills (wired from the shell in Part 53).
         * Slot map (pufo_uibank.h): 0..31 the 32 manual pads (Map+0x199C sprite set, mode 34,
         * origin forced 50,55), 32..42 the eleven buttons "b0".."b10" (Map+0xCC, mode 1, origin 0),
         * 43..53 the named singletons: dpanelv 0x113AB4, invv 0x113AB8, mscanv 0x113ABC, soldier
         * 0x128AC0, selsold 0x156928, sbsold 0x138EB8, ssold 0x156A80, enemystp 0x156AA0, endmiss
         * 0x156A74, twonone 0x156A70, minimcur 0x1571F0. Tact_Shutdown frees exactly these (the
         * 32-slot set at 0x3E150, the 11 map pads, and the singleton frees 0x3E17C..0x3E318);
         * World_FreeTactical mirrors that release. */
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
    FovField      sTactFov;         /* the FOV field Map_ComputeFov @ 0x2deac writes (Map+77788 in
                                     * the original); Part 106: the shot resolution's window refresh
                                     * runs through it */
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
    UiModalState  sModal;               /* the modal dialogue bank (Part 51): apUiSlotsA[12..19] @
                                         * Map+0x90..0xAC (UiModal_Begin @ 0x26430 / UiModal_End @
                                         * 0x2648C, the open flag at 0x26454/0x26494 read by
                                         * Render_Frame 0x224D4 and Input_OnPointerUp 0x14B68), plus the
                                         * two scratch cells Begin clears: apUiSlotsA[24] @ Map+0xC0 --
                                         * the same cell the input state models as nSlot24Hit (the
                                         * recorded multi-view issue; the app's hover copy is the live
                                         * one) -- and anActionPhaseSave0[112] @ 0x128AA0, the frame
                                         * tail's cell, which World_WireGeoscapeFrame now points at
                                         * this field (the shell's own frameEnd owner moved here). */
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

    /* --- the tact UI controls and the live input state (Part 54) ------------------
     * Tact_InitUiControls @ 0x3b498 installs the widget click handlers and positions the groups on
     * the caller's bank (WorldHooks.pBank); the original allocates the look control here and parks it
     * at unk_1571E4, so the port embeds it here too. sTactInput MOVED here from the app's g_tactState:
     * an installed MenuBtn handler calls Input_OnConfirm(pState, ...) on the live input state
     * (pufo_menubtn.c), so the app's copy and the world's copy must be one object. sTactUi is the
     * staging copy its handlers read and write: World_TactUiSyncIn/SyncOut (pufo_world.c) move its
     * fields to and from the live owners around every bank click. */
    TactInputState  sTactInput;
    TactInputState  sTactInputPre;      /* the SyncIn snapshot (Part 60): SyncOut writes back only the
                                         * cells the dispatch changed, so a handler running inside a
                                         * release and writing these live cells is not undone by a
                                         * flush of the pre-handler values */
    UiControl       sTactLookCtl;       /* unk_1571E4, the screen-0 look control. Input_OnConfirm @ 0x15f3c
                                         * clears its bEnabled on the armed multi-target commit (the
                                         * *unk_1571E4+9 scratch flag) */
    MenuPads        sTactMenuPads;      /* the 29 option-pad bytes 0xACE5A..0xAD1DA: the one-hot group
                                         * pads and flag pads the options handlers maintain (pufo_menu.h) */

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

    /* --- the enemy phase (Tact_EnemyAiTick @ 0x4cbe0) and its path walk (Part 52) ---
     * pfnEnemyAiTick (TurnHooks, 0x4D950's phase-3 arm) dispatches Tact_EnemyAiTick over
     * sTactEnemyAi; the tick handles ONE enemy unit per call and ends the enemy turn at its
     * tail (0x4D920: EndEnemyTurn + KickQueue) when nothing acted. The walked-path cells live
     * in GameMap (Map_apPathCells / Map_nPathCount / Map_pPathDestUnit); the flood frontier
     * lists are the shell's. */
    PathToCellCtx   sTactPath;          /* Unit_PathToCell's ctx */
    PathScratch     sTactPathScratch;   /* the flood fill's two frontier lists (the unk_10DB80 block) */
    EnemyAiTickCtx  sTactEnemyAi;       /* the phase-3 tick's ctx, refreshed by World_TactEnemyAiTickThunk */
    EnemyAiActCtx   sTactEnemyAiAct;    /* the panic AI's ctx (EnemyAi_TryFireOrWander @ 0x4abc8) */
    EnemyStepCtx    sTactStep;          /* the ctx BeforeEnemyStep (0x4b9b8) and AfterEnemyStep (0x4c4a0) run on,
                                         * rebuilt fresh by World_TactFillStepCtx at each step call (Part 56) */
    CombatCtx       sTactCombat;        /* the step ctx's proximity seam leans on this -- the same build
                                         * World_TactTickActiveUnit assembles per tick */
    ProximityCtx    sTactProx;          /* handed to MapCell_TickProximityItems (0x4b724) by the step ctx */
    i32             nStepXcomCount;     /* i32 views of the byte counts 0x128AD7/0x128ADC: EnemyStepCtx */
    i32             nStepEnemyCount;    /* reads them through i32*, the map owns them as bytes; step thunks sync */
    char            aszDeployMsg1[256]; /* unk_E90D9, the deploy confirm "in" message, formatted by the
                                         * widget-56 opener (Map_BuildDeployConfirmYesNo 0x18a68, Part 58) */
    char            aszDeployMsg2[256]; /* unk_E9273, the matching "out" message */
    CamOffset       sTactCamOffset;     /* Map+0x4A40/0x4A41: the pointer drag's camera scratch
                                         * (Part 60). Read by the iso layer (IsoViewportGuide's
                                         * frame and the walker's focus) since Part 63. */
    /* The tactical frame the shell paints (Part 63): Render_Frame @ 0x22310's default branch over
     * the live mission. sTactIso is the per-cell draw context the original keeps as one global and
     * hands to every tactical draw; anTactGroupBase is the 28-entry u16 window (fontSys + 2 * idx)
     * the iso chain indexes, which PufoFont models as separate group fields. */
    IsoDrawCtx      sTactIso;
    u16             anTactGroupBase[PUFO_ISO_GROUP_BASE_COUNT];
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
    /* The base-info screen (Geoscape_Draw case 3, 0x93F08). BaseInfoScreen_Init @ 0x57688 owns six
     * globals and BaseInfoScreen_BuildPanels @ 0x557f4 owns three more; the shell is their only holder,
     * so they are fields here: the pad 0x158828, the two controls 0x15882C (OnOk) / 0x158830 (the
     * panel-mode toggle), the three 42-line panels 0x158824 (names) / 0x158820 (free) / 0x158838
     * (totals), the built flag 0x15883C, the mode byte byte_AB190 @ 0xAB190, the work base byte_1870F4,
     * the base the geoscape acts on byte_158834 and the base-select overlay byte_1870F8. The pad is
     * Init's allocation and Geoscape_CloseUI case 3 frees it (0x8BE10); the three panels and the two
     * controls are BuildPanels' allocations and its release arm frees them (0x56F0C..0x56F90). */
    PadImage         *pBaseInfoPad;
    UiControl        *apBaseInfoCtl[2];
    UiPanel          *apBaseInfoPanel[3];
    u8                bBaseInfoPanelsBuilt;
    u8                bBaseInfoPanelMode;      /* byte_AB190 @ 0xAB190 */
    i8                nBaseInfoWorkBase;       /* byte_1870F4 */
    i8                nShownBaseIdx;           /* byte_158834 */
    u8                bBaseInfoOverlayOpen;    /* byte_1870F8 */
    BaseInfoInitEnv   baseInfoInit;
    BaseInfoBuildEnv  baseInfoBuild;
    BaseInfoDrawEnv   baseInfoDraw;
    BaseInfoToggleEnv baseInfoToggle;
    ScreenInitHooks   baseInfoOpenHooks;       /* BaseInfoScreen_Open @ 0x6167c, nav slot 1's handler */
    GeoscapeBaseInfoArmEnv baseInfoArm;
    GeoscapeScreenArmEnv   baseInfoCtl;
    /* The hangar (screen 6, opened by nav control slot 3). BasePlaneScreen_Init @ 0x7d9e4 allocates the
     * object block and tail-calls BasePlaneScreen_BuildPanels @ 0x7cc8c; the crew pair comes from
     * BasePlaneScreen_BuildListPanels @ 0x7a3f4 and the stores-transfer set from
     * BasePlaneScreen_BuildControls @ 0x7b014, each behind one of the block's own controls. In the
     * original every one of these lives under a fixed global; here the shell is their only holder. */
    BasePlaneScreen      planeBlock;             /* the 108-byte block at 0x161A48: the backdrop pad, the five
                                                  * craft art pads, the six weapon icon pads, the four main and
                                                  * seven weapon controls */
    UiControl           *apPlaneCtl[PUFO_PLANE_ARM_CTLS]; /* the arm's flat view of those controls: 0..3 the main
                                                  * four, 4..10 the weapon seven, 11/12 the crew nav pair
                                                  * (0x161AA4 / 0x161AA8) and 13/14 the stores nav pair
                                                  * (0x161AAC / 0x161AB0). Re-synced by World_WireGeoscapeFrame,
                                                  * because each builder allocates and releases its own pair. */
    PlaneCraftPanels     planeCraftPanels;       /* pPlaneCraftPanel 0x161EFC / pPlaneCraftStatusPanel 0x161ED0 */
    PlaneListPanels      planeListPanels;        /* the crew nav pair, the crew panel 0x161EDC and its status
                                                  * column 0x161A3C */
    PlaneStorePanels     planeStorePanels;       /* slot 0x161AC8, avail 0x161A40, sel-row 0x161F00,
                                                  * commit-row 0x161EEC, taken 0x161EF4, plus their nav pair */
    PlaneWeaponPanels    planeStatPanels;        /* 0x161F10 / 0x161F14 the two selected weapons and the two
                                                  * panel gates at 0x161F18 / 0x161F1C */
    u8      bPlaneBuilt;              /* bPlanePanelsBuilt @ 0x161F08 */
    u8      bPlaneRowFlag;            /* bPanelFlagScratch @ 0x161F0C: a row click sets it, the detail draw consumes it */
    i32     nPlaneSelCraftIdx;        /* nPlaneSelCraftIdx @ 0x161F04, the index the hangar draw reads */
    u8      bPlaneShowDetail;         /* bPlaneShowCraftDetail @ 0x161F20 */
    u8      bPlaneListBuilt;          /* bPlaneListPanelsBuilt @ 0x161F24 */
    u8      bPlaneStoresBuilt;        /* bPlaneStoresCommitted @ 0x161F28 */
    i32     nHangarCraftIdx;          /* nSelectedCraftIdx @ 0x161AC4, the index the stores and weapon side reads */
    i32     anPlaneCarryCount[PUFO_PLANE_CARRY_SLOTS]; /* anSlotCarryCount @ 0x161AD0: the staged per-slot change
                                                  * the confirm finally moves between base and craft */
    i32     nPlaneCrewQuota;          /* nPlaneCrewQuota @ 0x161EE0 */
    i32     nPlaneSoldiersAboard;     /* unk_161AB8 */
    i32     nPlaneSoldiersAtBase;     /* unk_161EE4 */
    i32     nPlaneSoldiersUnassigned; /* unk_161AB4 */
    i32     nPlaneCrewCarried;        /* unk_161AC0, the five crew slots' carried sum */
    i32     anPlaneStats[6];          /* the six stat counts, in the arm's row order: 0x161ED8, 0x161EE8,
                                                  * 0x161EF8, 0x161EF0, 0x161ABC, 0x161ED4 */
    i32     nPlaneStatRows;           /* nPlaneStatRowCount @ 0x161A38 */
    i16     nPlaneCursorX;            /* halfword views of nSmapCursorX / nSmapCursorY @ 0x186BD0 / 0x186BD4.
                                                  * The frame keeps them as i32; case 6 loads them LDRSH (0x930D0,
                                                  * 0x930D4), so the arm is given the halfword pair, re-copied on
                                                  * every wiring. */
    i16     nPlaneCursorY;
    ScreenScratch planeScratch;       /* dword_186BE8: the row the slot column's click caches, which the two
                                                  * quantity handlers and the hovered object line read back */
    BasePlaneScreenInitEnv planeInit;      /* Init's env: the block, the two controls it wires, its thunks */
    PlaneBuildEnv          planeBuild;     /* BuildPanels' env: Init's tail call and the close's rebuild */
    PlaneListPanelsEnv     planeList;      /* BuildListPanels' env */
    PlaneStoreBuildEnv     planeStoreBuild;/* BuildControls' env */
    PlaneStoresEnv         planeStores;    /* the two quantity handlers' env */
    PlaneCraftSelectEnv    planeSelect;    /* the two craft-list clicks' env */
    PlaneCrewPanelEnv      planeCrew;      /* the crew row click's env */
    PlaneWeaponEnv         planeWeapon;    /* RearmFromStores / ApplyWeaponSwap's env */
    PlaneCloseEnv          planeClose;     /* BasePlaneScreen_Close @ 0x7c9e8's env */
    BuySellApplyQtyEnv     planeApply;     /* BuySellScreen_ApplyQtyChanges @ 0x78d90: the confirm's env */
    BasePlaneDetailEnv     planeStatEnv;   /* BasePlane_ResetDetailState @ 0x7c9dc, on the draw's reset branch */
    ScreenInitHooks        planeOpenHooks; /* BasePlaneScreen_Open's hooks, nav slot 3's handler */
    /* The research screen (screen 8), opened by nav control slot 5: ResearchScreen_Open @ 0x616ac is
     * installed at 0x67BAC (apNavCtl + 0x14), plays sample 17 at 0x616B4 and writes id 8 at 0x6176C BEFORE
     * its tail call into ResearchScreen_Init @ 0x749a8. The original keeps the screen's own pad, its four
     * controls and its three panels in loose globals -- the pad and the controls INSIDE the topic table
     * (aResearchTopics[1874..1877], the block base + 0x1D48) -- so the shell owns the same six objects plus
     * the two cells the topic row click caches. */
    UiControl              aResearchCtl[4];    /* Init's four, in the original's slot order: the topic-list
                                                * scroll pair at (205,18) / (205,38) and the quantity pair at
                                                * (10,163) / (10,183) -- g_aResearchCtlDesc @ 0xABB10 */
    UiControl             *apResearchCtl[4];   /* the flat view: case 8 blits [0..1] (0x94120..0x94150), the
                                                * press arm hit-tests 0..1 with no row selected and 2..3 with one */
    void                  *apResearchRoot[5];  /* the block Init writes: [0] the "Research" pad (mode 6,
                                                * aResearch_0 @ 0xABB90), [1..4] the four controls */
    UiPanel               *pResearchTopicPanel;   /* pResearchTopicPanel @ 0x160C68, x 10 y 70, the topic list */
    UiPanel               *pResearchQtyPanel;     /* pResearchPanelB @ 0x15EEFC, x 197, right-aligned */
    UiPanel               *pResearchProgPanel;    /* pResearchPanelC @ 0x15EF00, x 233 */
    ScreenScratch         researchScratch;   /* unk_160C80 (bUiRowSelected, the byte 0x73334 stores) and
                                              * nResearchTopicRowCache @ 0x160C74 (the row 0x73360 caches) */
    u8                    bResearchBuilt;    /* bResearchPanelsBuilt @ 0x160C7C, BuildPanels' switch */
    ScreenResetOpenEnv    researchOpen;      /* ResearchScreen_Open's env: the prologue's live cells */
    ResearchInitEnv       researchInit;      /* 0x749a8 */
    ResearchBuildEnv      researchBuild;     /* 0x733f4: Init's call at 0x74BB8 and CloseUI case 8's teardown */
    ResearchQtyEnv        researchQty;       /* 0x72ea0 / 0x72f70 / 0x72d14, the three quantity controls */
    ResearchStartEnv      researchStart;     /* 0x73240: the start button, and case 8's first close arm */
    ResearchCancelEnv     researchCancel;    /* 0x73380: the cancel button */
    /* The ufopaedia screen (geoscape screen 16), opened by Ui_OpenBaseInfoAlt @ 0x84d18 from the status
     * panel's report confirm (Part 104). The original keeps one eight-pointer block at g_pUfopaediaScreenPad
     * @ 0x1872E0 -- [0] the screen pad (Ui_InitBaseInfo's allocation at 0x9DDF8, freed by Ufopaedia_Open's
     * teardown at 0x9B580), [1] the entry pad g_pUfopaediaPad @ 0x1872E4 (allocated by every
     * Ufopaedia_RefreshEntry, freed at 0x9B5B8..0x9B5DC -- see the recorded ORIGINAL BUG there), [2..7] the
     * six controls at 0x1872E8..0x1872FC -- and the state bytes as loose globals. The shell owns the same
     * block: the six controls are embedded (the original heap-allocates them in Init and the teardown
     * releases them -- deviation, the port's controls persist and only their pads cycle through
     * UiControl_LoadPads), and the nine cells below are the originals' named globals. */
    UiControl              aUfopaediaCtl[PUFO_UFOPAEDIA_CTL_COUNT]; /* the six, on g_aUiBaseInfoCtlDesc
                                               * @ 0xAC548: the text-panel scroll pair at (142,210)/(172,210),
                                               * the ufopaedia page pair at (10,210)/(40,210), the category
                                               * selector pair at (200,18)/(200,38) */
    UiControl             *apUfopaediaCtl[PUFO_UFOPAEDIA_CTL_COUNT]; /* the flat view: the draw arm's three
                                               * BlitCtlRun ranges (0x941DC..0x94280) and the press arm's
                                               * hit-test ranges (0x88E0C..0x88E30) index it */
    void                  *apUfopaediaRoot[8]; /* g_pUfopaediaScreenPad @ 0x1872E0: [0] the pad, [1] the
                                               * entry pad, [2..7] Init's control stores (0x9DE3C) */
    i32                   nUfopaediaEntryIdx;  /* unk_1871F4, the entry slot inside the category */
    i32                   nUfopaediaCategory;  /* g_nUfopaediaCategory @ 0x1871F8, 0..8 -- also the selector
                                               * row Ufopaedia_CacheSelRow caches (0x99fd0) */
    i32                   nUfopaediaTopic;     /* g_nUfopaediaTopic @ 0x187300 */
    i32                   nUfopaediaEntryNum;  /* g_nUfopaediaEntryNum @ 0x1872D8, normalised by the draw */
    i32                   nUfopaediaEntrySub;  /* g_nUfopaediaEntrySub @ 0x1872DC, derived by that
                                               * normalisation (UiBaseInfoDrawEnv.pnSub) */
    u8                    bUfopaediaWidePanel; /* g_bUfopaediaWidePanel @ 0x1871FC: wide text layout, and the
                                               * draw/press gate that picks the control range */
    u8                    bUfopaediaPadDirty;  /* g_bUfopaediaPadDirty @ 0x187304: keeps RefreshEntry's
                                               * re-pick (0x9A054) out after Init's reselect (0x9E038) */
    u8                    bUfopaediaEntryDirty;/* bUfopaediaEntryDirty @ 0x187310: "an entry is up" */
    u8                    bUfopaediaReselect;  /* Ufopaedia_bReselectPending @ 0x18706C: Ui_OpenBaseInfoAlt
                                               * sets it on the way in (0x84D1C), Init consumes it (0x9DEF4) */
    u8                    bUfopaediaBuilt;     /* unk_187308: Ufopaedia_Open's build/teardown toggle AND the
                                               * draw arm's "screen built" gate -- one byte, two names */
    u8                    bUfopaediaOpen;      /* bUfopaediaOpen @ 0x18730C: "a page is up" -- set by Init's
                                               * reselect (0x9E07C) and by Ufopaedia_CacheSelRow (0x99FD4),
                                               * cleared by CloseUI case 16 (0x8BD48) */
    UfopaediaEnv          ufopaedia;    /* 0x9a8cc / 0x9a014 / 0x9a60c / 0x9a754: the state step 6 builds */
    UiBaseInfoInitEnv     ufopaediaInit; /* Ui_InitBaseInfo @ 0x9ddc8's env */
    /* Case 9, the manufacturing screen (0x9415C..0x941A4), opened by nav control slot 6:
     * ManufScreen_Open @ 0x614cc is installed at 0x67BBC, plays sample 0x11 and writes id 9 AFTER its
     * tail call into ManufScreen_Init @ 0x710b0. The original keeps the screen block at unk_159E98 --
     * [0] the "manuf" pad (mode 7), the six controls at block+0x4F84..0x4F98 (0x15EE1C..0x15EE30) --
     * and the four panel columns at 0x15EE44/0x15EE48/0x15EE40/0x15EE38. The shell owns the same
     * objects plus the two row cells and the requested-unit column aManufProductB @ 0x15B79C, whose
     * "not shell state yet" note (World_BuildManufClearEnv) this wiring ends: the qty/engineer pair
     * writes it, the hourly commit reads it. */
    UiControl              aManufCtl[6];      /* Init's six, on g_aManufCtlDesc @ 0xAB9F8: the tab scroll
                                               * pair (205,18)/(205,38), the quantity pair (10,251)/(10,271)
                                               * and the engineers pair (140,251)/(140,271) */
    UiControl             *apManufCtl[6];     /* the flat view: case 9 blits [0..1] (0x94168..0x94198), the
                                               * press arm hit-tests 2..5 with a product selected and 0..1
                                               * without (0x88...) */
    void                  *apManufRoot[7];    /* the screen block: [0] the pad, [1..6] the six controls,
                                               * as ManufScreen_Init stores them (0x710E0 / 0x71160) */
    UiPanel               *pManufProductPanel; /* pManufProductPanel @ 0x15EE44: names, aux = product id */
    UiPanel               *pManufPanelB;       /* pManufPanelB @ 0x15EE48: the allocated-work column */
    UiPanel               *pManufPanelC;       /* pManufPanelC @ 0x15EE40: the percent column */
    UiPanel               *pManufPanelD;       /* pManufPanelD @ 0x15EE38: the asked/produced column */
    u8                    bManufBuilt;         /* unk_15EE4C, BuildPanels' switch */
    u8                    bManufQtyGate;       /* bManufQtyGate @ 0x15EE50, set when a component is short */
    u8                    bManufProductSelected; /* bManufProductSelected @ 0x15EE34 */
    i32                   nManufProductRow;    /* nManufProductRowCache @ 0x15EE3C */
    i32                   anProductReq[PUFO_PRODUCT_ENTRIES_PER_BASE * PUFO_FAC_BASE_COUNT]; /* aManufProductB
                                               * @ 0x15B79C: units the player asked for, per base and product */
    ManufInitEnv          manufInit;      /* 0x710b0 */
    ManufBuildEnv         manufBuild;     /* 0x6e9e8: Init's tail call at 0x71268 and CloseUI case 9's teardown */
    ManufQtyEnv           manufQty;       /* 0x6dddc / 0x6e09c / 0x6df6c / 0x6dec0 */
    ManufEngineerEnv      manufEngineers; /* 0x6e13c / 0x6e330 */
    ManufSelectEnv        manufSelect;    /* 0x6e914: the product row click */
    ManufClearEnv         manufClear;     /* the persistent ClearProductSlot tables: the hourly hook,
                                           * ConfirmAndClose @ 0x6e98c and the frame draw both read it */
    ManufDrawEnv          manufDrawEnv;   /* ManufScreen_Draw @ 0x6e3e0: pGeoscapeCtlA's click handler and
                                           * CloseUI case 9's selected-product rebuild (0x8BD78) */
    i32                   nManufSelScratch159E90; /* gUiRootPanel.anLineAux[147] @ 0x159E90: the dword
                                           * ManufScreen_SyncSelection @ 0x6dd24 stores panel D's row cursor in */
    ScreenInitHooks       manufOpenHooks; /* nav slot 6's handler (0x67BBC) */
    /* Cases 11 and 12, the buy/sell screen (0x93DD4..0x93E58, one shared BuySellScreen_Draw at 0x93E10),
     * opened by nav slots 8/9: BuySellScreen_OpenBuy @ 0x61794 (0x67BD4) and BuySellScreen_OpenSell
     * @ 0x617c4 (0x67BE0), id 11/12 written BEFORE the tail call into the shared BuySellScreen_Init
     * @ 0x78c30. The pad is the single slot unk_161A08 (the original allocates it in Init at 0x78C70);
     * the four controls are the globals at 0x161A0C..0x161A18 (case 11/12 blit [0..1] only, 0x93DE0/0x93E2C),
     * the five columns sit at 0x161A1C/0x161A00/0x161A20/0x160C88/0x1615F8, and the pending deltas fill
     * g_anBuySellSlotDelta @ 0x161600 -- the cells CommitTransaction @ 0x74bf8 finally moves. */
    UiControl              aBuySellCtl[4];     /* g_aBuySellCtl @ 0x161A0C..0x161A18, caller-owned storage
                                                * the two builders seat with their own descriptors */
    UiControl             *apBuySellCtl[2];    /* the arm/press view: 0x161A0C and 0x161A10, the two the
                                                * frame blits and the press arm hit-tests (0x88804, 0x8A52C) */
    PadImage              *pBuySellPad;        /* pBuySellScreenPad @ 0x161A08, Init's allocation, freed by
                                                * CloseUI cases 11/12 at 0x8C0F0 / 0x8C13C */
    UiPanel               *pBuySellPanelA;     /* pBuySellPanelA @ 0x161A1C, the name column */
    UiPanel               *pBuySellPanelB;     /* pBuySellPanelB @ 0x161A00, the "<" column */
    UiPanel               *pBuySellPanelC;     /* pBuySellPanelC @ 0x161A20, the ">" column */
    UiPanel               *pBuySellPanelD;     /* pBuySellPanelD @ 0x160C88, the available count */
    UiPanel               *pBuySellPanelE;     /* pBuySellPanelE @ 0x1615F8, the pending quantity */
    u8                    bBuySellBuilt;       /* bBuySellPanelsBuilt @ 0x161A28, the sell mode's switch */
    u8                    bBuySellCtlBuilt;    /* unk_161A24, the buy mode's switch */
    u8                    bBuySellCursorOnRow; /* unk_161A30, 1 inside the cursor gate, 0 outside */
    ScreenScratch         buySellScratch;      /* the row cursor unk_186BE8; in the original this dword is
                                                * the SAME global the hangar caches its row in (planeScratch)
                                                * -- the port gives each screen its own copy, as research does */
    i32                   nBuySellTransTotal;  /* BuySell_nTransTotal @ 0x161A2C, the money the transaction moved */
    i32                   nSoldierHireCount;   /* nSoldierHireCount @ 0x159D90, the 1000-row hire tally Init clears
                                                * (0x78CB4) and the ">" / "<" soldier rows move; the save chunk's
                                                * four bytes at 0x159D90 round-trip through this cell now */
    i32                   nBuySellFundsSnapshot; /* BuySell_nFundsSnapshot @ 0x161460, the rollback baseline the
                                                * builders' teardown restores when the player leaves uncommitted */
    i32                   anBuySellSlotDelta[PUFO_RESEARCH_TOPIC_COUNT]; /* g_anBuySellSlotDelta @ 0x161600,
                                                * one pending unit count per object slot (Map_nObjectCount entries,
                                                * 144 shipped) */
    i32                   anAssignedCraftList[PUFO_PRODUCT_ENTRIES_PER_BASE]; /* dword_161468: [0] the count,
                                                * [1..] the craft CommitTransaction found in panel A rows 117..121 */
    i32                   anSoldierIdList[256]; /* g_aSoldierIdList @ 0x160C90: [0] the count, [1..] the soldier
                                                * ids from the panel A rows 1000 */
    BuySellInitEnv        buySellInit;    /* 0x78c30, shared by both modes */
    BuySellBuildEnv       buySellBuild;   /* 0x76de0: sell's tail call and CloseUI case 12's teardown */
    BuySellControlsEnv    buySellControls;/* 0x76160: buy's tail call and CloseUI case 11's teardown */
    BuySellCapacityEnv    buySellCapacity;/* 0x75b88 / 0x75878: the ">" and "<" row handlers */
    BuySellRowEditEnv     buySellRowEdit; /* 0x75160 / 0x75464: the FillBuyPanel / FillSellPanel fills */
    BuySellCommitEnv      buySellCommit;  /* 0x74bf8: the sell confirm */
    BuySellConfirmEnv     buySellConfirm; /* 0x75090: the buy confirm, the transfer working site's promote */
    ScreenInitHooks       buySellBuyHooks;  /* nav slot 8's handler (0x67BD4) */
    ScreenInitHooks       buySellSellHooks; /* nav slot 9's handler (0x67BE0) */
    /* Case 10, the transfer screen, opened by nav control slot 7: TransferScreen_Open @ 0x614fc is
     * installed at 0x67BC4, plays sample 17 at 0x61508 and writes id 10 at 0x6151C AFTER its tail
     * call into Ui_InitTransfer @ 0x9955c. The original keeps the screen block at 0x1870D8 -- [0] the
     * "transfer" pad (mode 13, aTransfer @ 0xAC438), the four controls at 0x1870DC..0x1870E8 -- and
     * shares the five panel columns with the buy/sell screen. The shell owns the same objects plus
     * its own copy of the row cache dword and the two scratch bytes beside the root array
     * (unk_1870D0 / unk_1870F0, the only reads Transfer_ResetSite makes of them). */
    UiControl              aTransferCtl[4];   /* Init's four, seated by its Setup pass on
                                               * g_aTransferCtlDesc @ 0xAC3B8: the scroll pair at
                                               * (205,18)/(205,38), the reset and the commit button */
    UiControl             *apTransferCtl[4];  /* the flat view: the frame blits [0..3] (0x93E68..0x93E90),
                                               * the press arms hit-test them (0x889F0, 0x8A6AC) */
    void                  *apTransferRoot[5]; /* the block Init writes: [0] the pad (0x9958C),
                                               * [1..4] the four controls (0x995E4) */
    u8                    bTransferBuilt;     /* bTransferPanelsBuilt @ 0x1870EC, the BuildPanels switch and
                                               * the strip gate Base_HitTestFacilityStrip tests (0x6723C) */
    ScreenScratch         transferScratch;    /* the row cache unk_186BE8, the transfer screen's copy, as
                                               * the research and buy/sell screens each keep their own */
    i8                    bTransferScratch1870D0; /* unk_1870D0, read SIGNED by Transfer_ResetSite's gate;
                                                   * no writer exists anywhere in the binary */
    i8                    bTransferScratch1870F0; /* unk_1870F0, the same */
    TransferInitEnv       transferInit;      /* 0x9955c */
    TransferBuildEnv      transferBuild;     /* 0x96b30: Init's call at 0x995A4 and CloseUI case 10's
                                              * teardown rebuild at 0x8BE40 */
    TransferResetEnv      transferReset;     /* 0x9690c: the reset button's click */
    TransferMoveEnv       transferMove;      /* 0x9602c: the decrement column's click */
    TransferCapacityEnv   transferCapacity;  /* 0x96288: the increment column's click */
    TransferCommitEnv     transferCommit;    /* 0x97aa4: the commit button's click */
    ScreenInitHooks       transferOpenHooks; /* nav slot 7's handler (0x67BC4) */
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
    u8          *pnMapPhase;         /* nMapPhase @ 0x1139EF (Map + 0x37): the 0xC352 anim counter the
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
    BaseClearShipsEnv clearShipsEnv;  /* the removal pair plus the two cells it lacks: CloseUI case 12's
                                       * Base_RemoveAllShips teardown @ 0x8C160, fed from shipEnv below */
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
    /* OptionsScreen_ReturnToTitle's tail callee (GameUI_InitStartup @ 0x38a28, the store at 0x6B490):
     * the startup-screen owner supplies it, because the menu controls, the font and the option block
     * are its objects (Part 102). The quit page's Да reaches it through the world's own thunk. */
    i32 (*pfnInitStartupUi)(void *pUser);
    /* szAppDir @ 0x1883b0, the save-file directory the caller owns. The options save/load rows hand it
     * to World_SaveSlot / World_LoadSlot, the same string App_HookSaveGame uses (Part 102). */
    const char *pszSaveDir;
    /* GX_Present @ 0x27788, the tail Render_Frame reaches at 0x22600 (Part 63). The geoscape branch
     * never gets here -- Geoscape_Draw presents itself (0x22328) -- so only the tactical seam uses
     * this hook. */
    i32 (*pfnPresentFrame)(void *pUser);
    void *pPresentUser;
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
    u8                    *pnMapPhase;
    /* Smap_HitTestStrategSelect @ 0x82be8, the map select test the press dispatcher runs at 0x87C10
     * and 0x87C94. The shell builds its SmapHitEnv itself (World_GeoHitTestStrategSelect), so this
     * hook only replaces that default. */
    u8 (*pfnHitTestStrategSelect)(const SmapView *pView, i32 nX, i32 nY, void *pUser);
    void *pHitTestUser;

    /* --- the tact bank bring-up (Tact_InitUiControls @ 0x3b498, Part 54) ---
     * pBank is the caller's 130-widget bank: InitUiControls installs the click handlers, positions
     * the groups and loads the widget pads through the pfnLoadPads seam above. The rest is what the
     * installed handlers reach back into the caller for: the caller's TactInputHooks (MenuBtn_Confirm
     * hands them to Input_OnConfirm @ 0x15f3c), the UiBank_bActionFlag scratch Input_OnAction sets,
     * the SaveLoad_Slot0..6 pair (Part 62 wires both arms: the save arm runs World_SaveSlot over
     * the live world), the slot-list builder Menu_OpenSlotList
     * injects, the list-mode cell *0x157188, and a WRITABLE alias of the same option block pOptions
     * points at: Menu_SetSpeed/Gamma/MusicVolume/SfxVolume and the five toggles write options the way
     * the original writes the Map option bytes. */
    UiWidgetBank            *pBank;
    const TactInputHooks    *pTactInputHooks;
    /* App_ResizeMainWindow @ 0x9ecb8 seam (Part 61): the HWND and its stored size pair belong to
     * the caller; widget 58's handler reaches them only through this hook. */
    void (*pfnResizeMainWindow)(void *pUser);
    void *pResizeUser;
    bool                    *pbBankActionFlag;
    const SaveLoadSlotHooks *pSaveLoadHooks;
    void (*pfnFormatSlotList)(void *pUser); /* SaveSlot_FormatList @ 0x45904 */
    void *pFormatSlotListUser;
    /* The block that hook writes: the caller's seven 40-byte slot labels, MainMenu_pSlotCtx @ 0x156938.
     * Screens 10 and 11 draw those seven labels onto the modal-panel pads (TactHud_DrawModalRight
     * 0x21cec reads &MainMenu_pSlotCtx[40 * i]), so the tact frame needs the same pointer the list
     * builder writes into. */
    const char              *pSlotLabels;   /* MainMenu_pSlotCtx @ 0x156938, 7 x 40 */
    i32                     *pnListMode;    /* *0x157188 */
    PufoOptions             *pTactOptions;
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

/* WM_TIMER 0xC353's tact half during a live mission (Part 105): the same UiPulse_OnTimer @ 0x36630,
 * fed through SyncIn/SyncOut so the held-key repeats, the slot-0 force-release of bank widgets
 * 22/23 and the slot-4 long-press arm read and move the LIVE cells, the way the original reaches
 * them straight off Map + pBank at 0xA0C64. A no-op unless a mission owns the pen. */
void World_PulseTact(PufoWorldState *pW);

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

/* Inv_CloseAndCommit @ 0x15a48 over the live mission: the loadout snapshot, then the deploy
 * (Tact_DeployTeamAndHwps @ 0x39980, MapView_FocusSelected, Map_RefreshCellMeta and the per-unit
 * visibility pass), then the loose-clip merge and Map_nUiScreen = 0. The seam Input_OnConfirm's
 * screen-1 arm calls (0x16104). */
void World_InvCloseAndCommit(PufoWorldState *pW);

/* Allocate the heap-held parts (the 200 site records) and set the defaults the original's
 * zero-filled .data has. */
bool World_Init(PufoWorldState *pW);

/* Geoscape_InitUI(0) + Game_StartNew over this state. Returns Game_StartNew's result. */
i32 World_NewGame(PufoWorldState *pW, BootAssets *pAssets, PufoFont *pFont,
                  const PufoOptions *pOptions, const WorldHooks *pHooks);

/* LoadGame @ 0x44bbc over this state: the slot file is szAppDir + "sav" + slot + ".sav".
 * Returns LoadGame's result (0 when the file is absent). pFont is required since Part 65: a save that
 * carries a mission rebuilds it through Tact_InitMission(1), and that path needs fontSys @ 0x158268
 * for Font_LoadTactSprites and the four xcom_<n> sprite sets (Save_ReadTGameData 0x4444C..0x4449C). */
i32 World_LoadSlot(PufoWorldState *pW, const char *pszAppDir, i32 nSlot, BootAssets *pAssets,
                   PufoFont *pFont, const PufoOptions *pOptions, const WorldHooks *pHooks);

/* SaveGame @ 0x44988 over this state (Part 62): the same slot path as World_LoadSlot. The tactical
 * chunks are built only while a mission is live; the mission counter byte is incremented through an
 * i32 mirror, as the driver's tact branch does. Returns SaveGame's result. */
i32 World_SaveSlot(PufoWorldState *pW, const char *pszAppDir, i32 nSlot, const WorldHooks *pHooks);

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

/* The shell's mirror of Tact_Shutdown @ 0x3d568 (the Part 47 choice: World_FreeTactical releases the
 * shell-owned counterparts of the original's global blocks, the tact flag carries the Map+0x00 latch
 * clear at 0x3D588, and the font release is the 0x3D6DC store). bKeepSome 1 is the run-END arm -- the
 * original reaches it from Tact_CheckBattleEnd 0x35014 / 0x35180, UiScreen_OnBack's quit arm 0x156D4 and
 * App_QuitFromTact 0x18E20 -- and its tail 0x3E14C..0x3E154 runs PtrArray_DeleteAll on the LIVE geoscape
 * strateg list while leaving the block's +0x38 count mirror (0x186530, the shell's
 * saveStrateg.nStrategCount) stale. App_HookShutdownQuit runs it for the quit dialog's Yes (Part 103). */
void World_EndHookShutdown(u8 bKeepSome, void *pUser);

/* Pointer dispatch while a mission owns the pen (Part 60). The caller feeds the pen events
 * (Input_OnPointer @ 0x13c6c press/move and Input_OnPointerUp @ 0x148d0) through the shell's
 * TactPointerWorld; the world fills the tactical fields (queue, lookup, HUD pad, camera scratch,
 * look control), moves the live Map cells in and out of sTactInput, and runs the ported
 * dispatchers. The pen reaches them only while World_bTactPointerLive -- the launch clears
 * Map.bGeoscapeUi at 0x86D34, which is the original's own branch gate (0x13CE0 / 0x14908). */
bool World_bTactPointerLive(const PufoWorldState *pW);
void World_TactPointerMove(PufoWorldState *pW, TactPointerWorld *pPtr, i32 nX, i32 nY, bool bDown);
void World_TactPointerUp(PufoWorldState *pW, TactPointerWorld *pPtr);

/* Keyboard dispatch while a mission owns the keys (Part 67). Same route as the pen: the world stages
 * the live Map cells into sTactInput, runs PocketUFO_WndProc's WM_KEYDOWN chain (0xA0DC8..0xA0F74) or
 * WM_KEYUP chain (0xA0CE0..0xA0DC4) -- ported as Input_OnKeyDown/Input_OnKeyUp in pufo_input.c -- and
 * flushes them back. The held-repeat bytes come from the caller's widget bank (+0x104C..0x104F), the
 * same four WM_SETFOCUS clears and UiPulse_OnTimer repeats. Which key means which slot is the shell's
 * key map: the original fills its record at 0x1884D8 from GXGetDefaultKeys(2) (0x9FF78..0x9FFB0). */
void World_TactKeyDown(PufoWorldState *pW, PufoKeySlot eSlot);
void World_TactKeyUp(PufoWorldState *pW, PufoKeySlot eSlot);

/* The keyboard arms' callee seams, each a thin wrapper over the live mission and a no-op without one.
 * They are what the shell installs on TactInputHooks: the arms of Input_OnAction/Input_OnSecondary/
 * Input_OnConfirm/Input_OnMenuEscape reach them while sTactInput is already staged, so none of them
 * re-stages (compare World_InvCloseAndCommit, the Part 66 seam of the same kind). */
void World_TactConfirmInPlace(void);                        /* Input_OnConfirm @ 0x15f3c, 0x15660 */
void World_TactMapClick(void);                              /* Tact_HandleMapClick @ 0x1512c, 0x15668 */
void World_TactSelectNextUnit(void);                        /* Tact_SelectNextUnit @ 0x17304, 0x11974 */
void World_TactSelectPrevUnit(void);                        /* Tact_SelectPrevUnit @ 0x17260 */
void World_TactInvScrollUp(void);                           /* Inv_ScrollUp @ 0x173e4, 0x11774 */
void World_TactInvScrollDown(void);                         /* Inv_ScrollDown @ 0x173a8 */
void World_TactSetCursorPos(i32 nX, i32 nY);                /* MapCursor_SetPos @ 0x27014 */
void World_TactPushTextRecord(i32 nRecIndex);               /* StatusMsg_Push @ 0x2dda4, record nRecIndex */
void World_TactEscapeToGeoscape(void);                      /* Tact_Shutdown(0) + Geoscape_InitUI(1), 0x162BC/0x162C8 */

/* The two geoscape arms of the four directional handlers (the BL sites 0x116D8, 0x117C8, 0x118C8,
 * 0x119C8): each sets one byte of the held-direction block
 * 0x1864F4..0x1864F7, which the world owns as abScrollFlags. The panel-scroll dispatcher the up/down
 * arms run for the other screen ids is Geoscape_UiFocusPrev/Next @ 0x8afa0 / 0x8b1f4. */
void World_GeoscapePanelFlag(PufoWorldState *pW, bool bPanelA); /* Geoscape_SetPanelFlagA @ 0x8b448, B @ 0x8b478 */
void World_GeoscapeFocusScroll(PufoWorldState *pW, bool bPrev); /* Geoscape_UiFocusPrev/Next */

/* Render_Frame @ 0x22310's tactical branch over the live mission (Part 63): the cursor tick, the
 * iso layer (MapView_DrawIsoLayer @ 0x21e2c) and its frame furniture, the modal dialog
 * (UiModal_DrawDialog @ 0x264a0, whose button boxes land in the input state's slots 20..23 that the
 * pen's status-line arm reads), the fade block and the status stack, then the present hook.
 * Screens 0, 8, 9 and 13 (the debrief status banner, TactHud_DrawStatusBanner @ 0x1e920) are wired;
 * every other screen -- and every non-mission state -- leaves World_bTactFrameLive false and the
 * caller keeps its own frame path. */
bool World_bTactFrameLive(const PufoWorldState *pW);
void World_TactFrame(PufoWorldState *pW);

/* The WM_TIMER 0xC352 arm at 0xA0990..0xA09AC: with Map_nBannerFadeTicks positive AND Map_nUiScreen
 * 13 it runs Render_Frame_DecRef @ 0x36818 -- decrement the counter, then draw the whole frame, which
 * is what plays the debrief's 10-tick fade out. Any other screen or a counter at zero does nothing. */
void World_TickBannerFade(PufoWorldState *pW);

/* GeoscapeUi_FreeAll(bFreeWorld 1) plus the nav controls and the site records. */
void World_Free(PufoWorldState *pW);

#endif /* PUFO_WORLD_H */
