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
    u8        abRadarPct[PUFO_FAC_BASE_COUNT];  /* unk_162008 */
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
    u8                bFlag187080;
    UiLineList        msgLines;                 /* the root message list @ 0x158890 */
    i32               nSelectedStrategIdx;      /* 0x186BE4 */
    i32               nSelectedShipIdx;         /* 0x187054 */
    i32               nStrategIndex;            /* 0x159E74 */
    i32               nRadarPulseTimer;         /* nRadarPulseTimer @ 0xAC224 */
    u8                bFlag1870A0;              /* byte_18709C[4] */
    u8                bFlag18708C;
    u8                bFlag187090;
    u8                bFlag187098;
    OptionsScreenHooks optionsHooks;            /* the radar click's options branch */
    NavControls       nav;
    NavCtlEnv         navEnv;

    /* The geoscape map click (Geoscape_OnMapClick @ 0x8b4a8) and the base-facility screen it can
     * open. The tap coordinates stay zero until Geoscape_OnPointer (0x872d0) is ported. */
    i32          nSmapTapX;             /* nSmapTapX @ 0x186B7C */
    i32          nSmapTapY;             /* nSmapTapY @ 0x186B80 */
    u8           bPlaceActive;          /* bBasePlaceActive @ 0x1870BC */
    u8           bPlaceFail;            /* bBasePlaceFail @ 0x1870B8 */
    u8           bFacDone;              /* bBaseFacDone @ 0x187078 */
    u8           bPendingRegionId;  /* bPendingBaseKind @ 0x159DF4: 0xFF is the "nothing pending"
                                         * sentinel, but the image's .data value is 0 and the port
                                         * keeps it (see World_Init) */
    i32          nPlaceX;               /* 0x159D94 */
    i32          nPlaceY;               /* 0x159D98 */
    char         szLabelScratch[108];   /* szRadarMsg @ 0x186B10 */
    UiControl    ctlFacClose;           /* unk_186B00 */
    UiControl    ctlFacUp;              /* the control the original allocates at 0x158874 */
    UiControl    ctlFacDown;            /* ... and 0x158878 */
    i32          nUnk15887C;            /* 0x15887C */
    i32          nUnk158880;            /* 0x158880 */
    u8           bArmedPlacement;       /* bBaseFacPlaceArmed @ 0x158888, a byte */
    u8           bFacDialogOpen;        /* unk_158884: 1 while the build dialog exists */
    UiPanel     *pFacPanel;             /* unk_15886C, the dialog's UiPanel */
    i32          nPanelSel;             /* unk_158868 */
    u8           bNewBaseMode;          /* byte_187074[4] */
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
    GeoscapeWorld     world;
    GameTime          time;
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
} WorldHooks;

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
