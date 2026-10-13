/* pufo_tactinit.h - starting one tactical mission.
 *
 * Evidence: Tact_InitMission @ 0x3c550 (4,120 bytes, 947 ARM instructions, 117 blocks).
 *
 * This is a composition root, not an algorithm. It allocates the mission's three
 * singletons, clears about thirty Map-block flags, builds five text panels, loads two
 * archive members, runs four subsystem initialisers, and then either builds a new
 * mission or reads one back from the save file. Every callee is already ported; what
 * this function owns is the ORDER and the state each callee sees.
 *
 * ## The two paths
 *
 * `nMode == 0` is a new mission. `nMode == 1` is a save load that additionally runs
 * Save_ReadObjBefore first. Any other non-zero value is a save load without it. Both
 * paths share the prologue and the tail, so the split is one branch in the middle --
 * exactly like Tact_Shutdown @ 0x3d568, which tears down what this builds.
 *
 * ## Map-block fields the context struct does not model
 *
 * The original reaches these through the context base at 0x1139b8. They enter as
 * pointers to caller storage, each cited by Map offset, the same way TactShutdownEnv
 * carries them. Several are slots of the UI slot array `apUiSlotsA` @ Map+0x60, which
 * is why some carry a slot name:
 *
 *     Map+0x00   bTactFlag            also read by Map_InitFacingScratch and GameTick_OnTimer
 *     Map+0x01   unk_1139B9           meaning not established
 *     Map+0x38   bPlayerInputEnabled
 *     Map+0x3A   nUiScreen            u16
 *     Map+0x50   bFire0               one of the three fire bytes 0x50/0x51/0x52
 *     Map+0x52   bFire2
 *     Map+0x5A   bGameTickHold
 *     Map+0x5C   bBattleEnded
 *     Map+0x64   apUiSlotsA[1]
 *     Map+0x68   apUiSlotsA[2]        low byte is the sweep/repaint gate
 *     Map+0x82   BYTE2(apUiSlotsA[8])
 *     Map+0x83   BYTE3(apUiSlotsA[8])
 *     Map+0x84   apUiSlotsA[9]
 *     Map+0x88   apUiSlotsA[10]       GameTick_OnTimer's gate; this function sets it to 10
 *     Map+0x8C   apUiSlotsA[11]       holds the saved nUiScreen
 *     Map+0x1A21 unk_1153D9           Tact_GetCameraBounds' bUseCraft, TactShutdownEnv's
 *                                     pbAlternateOutcomePath -- one byte, two readings
 *     Map+0x15100 Map_nGamma
 *     Map+0x1511E unk_128AD6          meaning not established
 *     Map+0x15148 Map_bSpawnAlt
 *     Map+0x254B0 Map_bDrawIsoLayer
 *     Map+0x254B1 gap_86301[1]        Map_RefreshCellMeta's light gate
 *     Map+0x254B4 unk_138E6C          rand(0x28) + 10
 *     Map+0x254B8 unk_138E70          rand(0x14). IDA types it a word; the store is a dword.
 *     Map+0x254D4 unk_138E8C          cleared only when nMode == 0
 *
 * Map+0x54 (unk_113A0C) is `StatusStack.nCount` and is cleared through `pMap->status`.
 *
 * ## Deviations
 *
 * 1. The original `operator new`s the three singletons (TactState 70120, TactCtx272
 *    272, TactBuf11408 11408) and stores the pointers in globals, leaving the global
 *    NULL when the allocation fails and then writing through it. The port's storage is
 *    caller-owned, so the failure branch is not representable. Recorded in bugs.tsv.
 * 2. `nMapLayers` of 0 or 4 or more leaves the showship block's two extent values
 *    reading stack slots the function never wrote. The port initialises them to zero.
 *    Recorded in bugs.tsv.
 * 3. The briefing pad is allocated and its global pointer is dropped without a free on
 *    the next mission. The port reproduces the drop, not a free.
 * 4. `UiPanel_ctor`, `UiPanel_SetBytes6to11`, `UiPanel_WrapText`, `Tact_LoadUiPads` and
 *    `Tact_InitUiControls` live in pufo_ui, and pufo_ui links pufo_game. They are
 *    reached through hooks so this module does not widen the cycle -- the same seam
 *    Tact_Shutdown uses for UiPanel_FreeLines.
 * 5. The original calls `Tact_LoadPalettes` with whatever `AssetReader_Read` left in R0.
 *    `Tact_LoadPalettes` takes no argument in ARM (it reads Map_nGamma inside
 *    Tact_BuildPaletteLuts), so the residue is harmless there. The port's already-ported
 *    `Tact_LoadPalettes(i32 nGamma)` takes the gamma explicitly, so this function passes
 *    `*pnGamma`, the value the original reads. The signature mismatch in the existing
 *    port is reported as a separate issue, not changed here.
 */
#ifndef PUFO_TACTINIT_H
#define PUFO_TACTINIT_H

#include "game/pufo_font.h"
#include "game/pufo_invdrag.h"  /* TactEquipCtx */
#include "game/pufo_los.h"      /* LosTables */
#include "game/pufo_mapcell.h"
#include "game/pufo_mapctx.h"   /* GameMap: geometry, rosters, cells, status stack, tact ctx */
#include "game/pufo_rmp.h"      /* RmpData */
#include "game/pufo_save.h"     /* DatFile */
#include "game/pufo_statusmsg.h"
#include "game/pufo_tactctl.h"
#include "game/pufo_terrain.h" /* MapTerrainEnv */
#include "game/pufo_tacthud.h"  /* TactHudInvOpenCtx */
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"     /* Soldier, VisCtx */
#include "game/pufo_vistry.h"   /* VisRay */
#include "pufo_common.h"
#include "render/pufo_render.h" /* PadImage */
#include "ui/pufo_panel.h"      /* the UiPanel TYPE only; every call is a hook */

/* The five text panels, in the order the original allocates them:
 * 0x1568E8, 0x1568E4, 0x1568E0, 0x1568DC, unk_1571EC. */
#define PUFO_TACTINIT_PANELS 5

/* lightmap.dat is exactly 23 x 23 = 0x211 bytes; see PUFO_LIGHTMAP_LEN in pufo_mapcell.h. */
#define PUFO_TACTINIT_LIGHTMAP_LEN PUFO_LIGHTMAP_LEN

/* The showship.dat blob: two u16 header words then one byte per entry. Word 0 is the
 * entry count, word 1 is the entry length, and the gate value that skips an entry is 0xFF. */
#define PUFO_SHOWSHIP_HDR_BYTES 4
#define PUFO_SHOWSHIP_NONE      0xFFu

/* The cell flag the deploy-point sweep ORs into MapCell.nFlags40 @ 0x28. */
#define PUFO_DEPLOY_CELL_FLAG 0x78u

/* The briefing text is assembled into a stack buffer of 0x348 bytes. */
#define PUFO_TACTINIT_TEXT_CAP 0x348

/* The briefing panel is the last of the five. */
#define PUFO_BRIEF_PANEL 4

/* The briefing panel's wrap width and the two line-start values (Cydonia, everything else). */
#define PUFO_TACTINIT_WRAP_W 0xDCu

/* aTextRecs @ 0xB38D0 slices the briefing text is read from, as byte offsets into the
 * loaded texts.dat blob. These are the offsets the ARM itself computes. */
#define PUFO_BRIEF_A_HEAD   0x38748 /* unk_EC018 */
#define PUFO_BRIEF_A_JOIN   0x38815 /* unk_EC0E5 */
#define PUFO_BRIEF_A_LINE0  0x38CE3 /* unk_EC5B3 */
#define PUFO_BRIEF_B_HEAD   0x388E2 /* unk_EC1B2 */
#define PUFO_BRIEF_B_JOIN1  0x389AF /* unk_EC27F */
#define PUFO_BRIEF_B_JOIN2  0x38A7C /* unk_EC34C */
#define PUFO_BRIEF_TAIL     0x38B49 /* unk_EC419, both branches */

/* The mission type that takes the first briefing branch. */
#define PUFO_MISSION_TYPE_BRIEF_A 9u

/* The deploy-point table is 78 dwords at aDeployCellOffsets @ 0xA66B8, three groups of
 * 26 four-byte records. A group is 104 bytes; a record is 4 bytes, of which bytes 0, 1
 * and 2 are the cell offset X, Y and the layer. aDeployPointCounts @ 0xA67F0 says how
 * many records of each group the mission uses: 14, 12, 26. */
#define PUFO_DEPLOY_GROUP_BYTES   104
#define PUFO_DEPLOY_RECORD_BYTES  4
#define PUFO_DEPLOY_RECORDS_GROUP 26

/* The ceiling-break shot objects, as indices into Map.apObjects @ Map+0x108. Proven from
 * the globals this function copies them out of: unk_113AC0 (Map+0x108), unk_113B20
 * (Map+0x168), unk_113B8C (Map+0x1D4). */
#define PUFO_SHOT_OBJ_VARIANT2 24  /* (0x168 - 0x108) / 4 */
#define PUFO_SHOT_OBJ_VARIANT3 51  /* (0x1D4 - 0x108) / 4 */

typedef struct TactWeaponSel {
    ObjectRec *pObj;
    u16        uAmmoTypeId;
    u8         bConst10;
} TactWeaponSel;

/* Seams. The panel and UI-bank hooks keep pufo_game off pufo_ui; the audio hooks need a
 * backend the caller owns; the inventory hook keeps the tactical HUD context with its owner. */
typedef struct TactInitHooks {
    bool (*pfnPanelCtor)(UiPanel *pPanel, i32 nLineCount, void *pUser);        /* UiPanel_ctor (0x3e730) */
    void (*pfnPanelStyle6to11)(UiPanel *pPanel, u8 b6, u8 b7, u8 b8, u8 b9,
                               u8 b10, u8 b11, void *pUser);                   /* UiPanel_SetBytes6to11 (0x3e914) */
    i32  (*pfnPanelWrapText)(UiPanel *pPanel, u8 nMaxW, u8 nLineStart,
                             const char *pszText, void *pUser);                /* UiPanel_WrapText (0x37144) */
    bool (*pfnLoadUiPads)(void *pUser);                                        /* Tact_LoadUiPads (0x37bdc) */
    void (*pfnInitUiControls)(void *pUser);                                    /* Tact_InitUiControls (0x37c8c) */
    /* Font_LoadTactSprites (0x257c4): the same seam Map_BuildMissionTerrain uses. */
    void (*pfnLoadTactSprites)(void *pUser);
    bool (*pfnSoundInit)(const char *pszPrefix, void *pUser);                  /* Sound_InitAndLoadSamples (0x9e1f0) */
    void (*pfnSoundPlayMusic)(i32 nTrack, void *pUser);                        /* Sound_LoadAndPlayMusic (0x9e2b8) */
    void (*pfnSoundApplyMusicVolume)(void *pUser);                             /* Sound_ApplyMusicVolume (0x9e454) */
    void (*pfnSoundApplySfxVolume)(void *pUser);                               /* Sound_ApplySfxVolume (0x9e4dc) */
    bool (*pfnOpenInventory)(void *pUser);                                     /* Inv_OpenScreen (0x1a1c8) */
    void *pUser;
} TactInitHooks;

/* Everything Tact_InitMission reaches through the Map block, the font block and the
 * archives, plus the callee contexts whose world-dependent parts the caller fills.
 * This function fills each context's mission-dependent fields before calling it. */
typedef struct TactInitEnv {
    /* --- the live tactical context: geometry, rosters, cell grid, status stack, tact ctx --- */
    GameMap *pMap;

    /* --- Map-block state GameMap does not model. Pointer to caller storage. --- */
    u8   *pbTactFlag;        /* Map+0x00 */
    u8   *pbUnkMap01;        /* unk_1139B9 @ Map+0x01: 1 for a new mission, 0 when no XCom unit deployed */
    u8   *pbPlayerInput;     /* unk_1139F0 @ Map+0x38 */
    u16  *puUiScreen;        /* unk_1139F2 @ Map+0x3A */
    u8   *pbFire0;           /* unk_113A08 @ Map+0x50 */
    u8   *pbFire2;           /* byte_113A0A @ Map+0x52 */
    u8   *pbGameTickHold;    /* Map_bTickHold @ Map+0x5A */
    u8   *pbBattleEnded;     /* unk_113A14 @ Map+0x5C */
    i32  *pnUiSlot1;         /* dword_113A1C @ Map+0x64 = apUiSlotsA[1] */
    u8   *pbSweepArmed;      /* Map_bSweepArmed @ Map+0x68 = LOBYTE(apUiSlotsA[2]) */
    u8   *pbSlot8Byte2;      /* unk_113A3A @ Map+0x82 */
    u8   *pbSlot8Byte3;      /* unk_113A3B @ Map+0x83 */
    i32  *pnUiSlot9;         /* unk_113A3C @ Map+0x84 = apUiSlotsA[9] */
    i32  *pnUiSlot10;        /* unk_113A40 @ Map+0x88 = apUiSlotsA[10], set to 10 */
    i32  *pnUiSlot11;        /* unk_113A44 @ Map+0x8C = apUiSlotsA[11], holds the saved nUiScreen */
    u8   *pbAltOutcomePath;  /* unk_1153D9 @ Map+0x1A21 */
    i32  *pnGamma;           /* Map_nGamma @ Map+0x15100 */
    u8   *pbUnkMap1511E;     /* unk_128AD6 @ Map+0x1511E: non-zero skips the grid-wide bUnk56 clear */
    u8   *pnCraftMapX;       /* Map_nCraftMapX @ Map+0x15139, the craft's map tile */
    u8   *pnCraftMapY;       /* Map_nCraftMapY @ Map+0x1513A */
    /* The terrain builder's own Map-block inputs, passed straight through. */
    u8   *pnTerrainVariant;  /* Map_nTerrainVariant @ Map+0x15111 */
    u8   *pnTier;            /* Map_nMissionTier @ Map+0x15112 */
    i32  *pnTierSource;      /* Map_nMissionTierRaw @ Map+0x1510C */
    u8   *pbUnk15115;        /* Map_bUnk15115 @ Map+0x15115, the stealth-budget scale */
    i32  *pnShotX;           /* Map_nFireOrigX @ Map+0x14054 */
    i32  *pnShotY;           /* Map_nFireOrigY @ Map+0x14058 */
    i32  *pnSpawnTileX;      /* Map_nSpawnTileX @ Map+0x1514C */
    i32  *pnSpawnTileY;      /* Map_nSpawnTileY @ Map+0x15150 */
    u8   *pbSpawnAlt;        /* Map_bSpawnAlt @ Map+0x15148 */
    u8   *pbDrawIsoLayer;    /* Map_bDrawIsoLayer @ Map+0x254B0 */
    u8   *pbLightEnabled;    /* gap_86301[1] @ Map+0x254B1, Map_RefreshCellMeta's gate */
    i32  *pnUnkMap254B4;     /* unk_138E6C @ Map+0x254B4 */
    i32  *pnUnkMap254B8;     /* unk_138E70 @ Map+0x254B8 */
    u8   *pbUnkMap254D4;     /* unk_138E8C @ Map+0x254D4 */
    GameClock *pClock;       /* unk_128AB4 @ Map+0x150FC */

    /* --- the singletons the original allocates. Caller-owned storage; see deviation 1. --- */
    TactState *pTactState;   /* pMapTactical @ 0x157198, new(0x111E8) */
    RmpData   *pRmp;         /* pRmpMissionData @ 0x15881C, new(0x2C90) */
    /* unk_157180 is GameMap.tact, which this function constructs in place. */

    /* --- the five text panels, in allocation order --- */
    UiPanel *apPanel[PUFO_TACTINIT_PANELS];

    /* --- the mission cell grid, Map.apCells @ Map+0x15338 ---
     * 14400 heap MapCell pointers, 3600 per layer. Map_BuildMissionTerrain allocates them
     * here and Tact_Shutdown frees them, so this is the grid this path uses. GameMap.aCells
     * is a second, embedded model of the same 14400 cells used by the modules that take a
     * GameMap; that duplication is reported as a separate issue, not resolved here. */
    MapCell **apCells;

    MapCell **ppDeployCell;  /* Tact_pDeployCell @ 0x157204: written by Tact_SetupCraftCargo,
                              * read by Tact_EquipTeamFromCraft as the loose-item container. */

    /* --- buffers the original loads or allocates --- */
    void **ppShowShip;       /* dword_156A78: the showship.dat blob, malloc'd to its size */
    u8    *pLightmap;        /* unk_156AB8, PUFO_TACTINIT_LIGHTMAP_LEN bytes */
    PadImage **ppBriefPad;   /* unk_15692C: the briefing pad */
    DatFile  *pDat;          /* unk_156938: the open save file, used by the load path only */
    PufoFont *pFont;         /* fontSys @ 0x158268 */

    /* --- the object pointers this function copies out of Map.apObjects --- */
    ObjectRec **apObjects;   /* Map.apObjects @ Map+0x108 */
    /* unk_10DB90: the selected weapon record. Field 0 is apObjects[1] (Map+0x10C), field 6 is
     * the constant 10, and field 4 is that object's anAmmoTypeId[0] @ 0xA8. */
    TactWeaponSel *pWeaponSel;
    void   *pUnk138EA0;      /* unk_138EA0 @ Map+0x254E8: dword 0 becomes apObjects[0] and the dword at
                              * +0x14 (unk_138EB4 @ Map+0x254FC) becomes 0x7D00. Meaning not established. */
    ObjectRec **ppObj156A88; /* unk_156A88 = apObjects[PUFO_SHOT_OBJ_VARIANT3] */
    ObjectRec **ppObj156910; /* unk_156910 = apObjects[PUFO_SHOT_OBJ_VARIANT2] */
    ObjectRec **ppObj156A58; /* unk_156A58 = apObjects[141] (Map+0x33C), meaning not established */

    /* --- the view scratch Map_InitFacingScratch and MapView_FocusSelected share --- */
    MapFacingScratch facing;
    MapTileSize      tileSize;   /* 0x30 / 0x32 */
    i32              nCamScrollX; /* 0x15130 */
    i32              nCamScrollY; /* 0x15134 */
    CamOffset        camOffset;   /* 0x4A40 / 0x4A41 */
    TactCursor       cursor;      /* 0x15128 / 0x1512C / 0x15138 */
    bool             bBusy;       /* TactCtx272.bBusy, read by MapCursor_SetPos */
    bool             bSelUnitLarge;
    bool             bSuppressLarge;

    /* --- the ray table Tact_BuildVisRayTable writes and Unit_UpdateVisibility reads --- */
    VisRay *apVisRays;  /* unk_B2278, 120 records */
    u8     *paVisSteps; /* byte_AD720 */

    /* --- the briefing text sources, all aTextRecs @ 0xB38D0 slices except the last --- */
    const u8 *pTextRecs;    /* aTextRecs @ 0xB38D0, may be NULL */
    u32       nTextRecCount;
    const char *pszBriefLine; /* 0x10DA38: the line written into the panel after the wrap */

    /* --- callee contexts. The caller fills the world-dependent fields; this function
     *     fills the mission-dependent ones (layer count, craft tile, rosters, lookup,
     *     counts, cursor, focus geometry) before each call. --- */
    MapTerrainEnv      terrain;      /* Map_BuildMissionTerrain @ 0x22e10 */
    CraftSpritePatchEnv craftPatch;  /* Map_PatchCraftSprites @ 0x25ab4, the load path only */
    TactSelectCtx      select;
    TactTeamSelectEnv  teamSelect;
    TactSpawnAliensEnv spawnAliens;
    TactCargoEnv       cargo;
    TactEquipCtx       equip;
    TactDeployEnv      deploy;
    FocusCtx           focus;
    MapCursorPos       cursorPos;
    VisCtx             vis;
    TactHudInvOpenCtx  invOpen;
    const u8          *pDeployOffsets;     /* aDeployCellOffsets @ 0xA66B8 */
    const u8          *pDeployPointCounts; /* aDeployPointCounts @ 0xA67F0 */
    const u8          *pTileTypes;         /* unk_159D5C, 36 tile bytes in 6 rows of 6 */

    /* --- the load path's blocks, all caller-owned --- */
    LosTables         *pLoft;          /* lofttemps.dat + unitloft.dat, filled by Tact_LoadLoftData */
    Soldier         **apSoldiers;      /* Map.apSoldiers @ Map+0x1198 */
    i32              *pnSoldierCount;  /* the count at Map+0x1998 */
    i32               nSoldierCapacity;
    TactItem        **apTactItems;     /* Map.apTactItems @ Map+0x23438 */
    i32              *pnTactItemCount; /* Map_nTactItemCount @ Map+0x1513C */
    i32              *pnFlobjsWide;    /* the dword at Map+0x118 that picks Save_AllocFlobjs's count width */
    i32               nObjectCount;    /* Map_nObjectCount @ Map+0x10A8 */
    TactSaveState     *pSaveState;
    TeamUnitsState    *pSaveRosters;
    FlobjsReadState   *pSaveFlobjs;
    TactResetState    *pSaveReset;
    const TactLoadHooks *pSaveHooks;
    RmpModuleLoad      pfnLoadRmp;     /* Save_ReadRmp's per-module loader */
    void              *pRmpUser;

    TactInitHooks hooks;
} TactInitEnv;

/* Tact_InitMission @ 0x3c550.
 *
 * nMode 0 builds a new mission. nMode 1 loads a save and runs Save_ReadObjBefore first.
 * Any other non-zero value loads a save without it. Returns true, which is the only
 * value the original ever returns.
 *
 * The caller must have the archives open (AssetReader_Find reaches the loaded directory)
 * and, on the load path, `pDat` positioned at the save file. */
bool Tact_InitMission(TactInitEnv *pEnv, i32 nMode);

#endif /* PUFO_TACTINIT_H */
