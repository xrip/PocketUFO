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
#include "game/pufo_clock.h"    /* GameTime */
#include "game/pufo_font.h"
#include "game/pufo_geopointer.h" /* GeoscapePointerEnv, the geoscape pointer dispatcher's env */
#include "game/pufo_geoscape.h"
#include "game/pufo_objectrec.h"
#include "game/pufo_options.h"
#include "game/pufo_save.h"
#include "game/pufo_ship.h"
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
    u8                bGeoscapeUi;              /* unk_1139FC @ Map + 0x44 */
    u8                bRoot34;                  /* unk_18652C */
    u8                bRoot35;                  /* unk_18652D */
    i32               nUnk159DC4;
    /* Map_nGlyphPalBase @ 0x1183F4 is ONE global, and the font reads it directly
     * (Font_DrawGlyph picks palette slot (Map_nGlyphPalBase + nPal), Text_DrawString temporarily
     * forces it to 2). The shell therefore does not keep its own copy: every env that the original
     * writes it through is wired to g_nGlyphPalSlotBase, the port's single instance of it. */
    BaseFacFlag       aFac[PUFO_BASEFAC_COUNT]; /* aBaseFacFlags @ 0x186E48 */
    MissionSiteRec    aMissionSites[PUFO_MISSION_SITE_COUNT]; /* unk_1865C0 */
    UiControl         ctlResearchPanel;         /* pGeoscapeCtlA */
    UiControl         ctlConfirm;               /* unk_186BA4 */
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
    UiControl    ctlFacClose;           /* g_pUiCloseCtl @ 0x186B00, the shared close control */
    UiControl    ctlFacUp;              /* the control the original allocates at 0x158874 */
    UiControl    ctlFacDown;            /* ... and 0x158878 */
    i32          nUnk15887C;            /* 0x15887C */
    i32          nUnk158880;            /* 0x158880 */
    u8           bArmedPlacement;       /* bBaseFacPlaceArmed @ 0x158888, a byte */
    u8           bFacDialogOpen;        /* unk_158884: 1 while the build dialog exists */
    UiPanel     *pFacPanel;             /* unk_15886C, the dialog's UiPanel */
    i32          nPanelSel;             /* unk_158868 */
    u8           abLastRowName[4];      /* the 0x187024 slot the dialog's 17th row reads */
    u32          nShortfall1870C8;      /* byte_1870C8, written as a dword */
    i32          nResidue1870B4;        /* dword_1870B4[0] */
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
    SoldierScreenEnv solEnv;            /* the soldier screen's open/close switch */

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
    BootAssets *pAssets; /* set by World_NewGame; the StartNew hook reaches it through here */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1,
                        const char *psz2, void *pUser); /* the nav controls reuse it */
    void *pLoadPadsUser;
    void (*pfnPlaySample)(i32 nSample, void *pUser);    /* the message-list sound seam */
    void *pSoundUser;
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
    /* Smap_HitTestStrategSelect @ 0x82be8 needs the caller's SmapHitEnv, which the shell does not
     * build; the frame hands the dispatcher this seam instead (NULL means "nothing selectable"). */
    u8 (*pfnHitTestStrategSelect)(const SmapView *pView, i32 nX, i32 nY, void *pUser);
    void *pHitTestUser;
} WorldHooks;

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
 * per-base needed-flag passes and the per-screen switch. Called from the screens' close controls, which is
 * how a new game gets from the globe (2) to the placement screen (1) to the map (0). */
void World_CloseGeoscape(PufoWorldState *pW);

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

/* GeoscapeUi_FreeAll(bFreeWorld 1) plus the nav controls and the site records. */
void World_Free(PufoWorldState *pW);

#endif /* PUFO_WORLD_H */
