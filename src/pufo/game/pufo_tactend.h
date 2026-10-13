/* pufo_tactend.h - tearing down one tactical mission.
 *
 * Evidence: Tact_Shutdown @ 0x3d568 (3,788 bytes).
 *
 * The original takes one byte. Non-zero keeps the geoscape world and drops only the
 * tactical UI plus the tactical item pool; zero also commits the mission outcome and
 * removes the tactical units. Both paths then free the same tactical map and sprite
 * state, so the split is one branch in the middle, not two separate functions.
 *
 * ## The bank pad walk
 *
 * Thirteen groups, each freeing three adjacent pad pointers per widget. Hex-Rays
 * renders the inner step as `v12 = (UiControl *)((char *)v12 + 4` and the group base
 * as `v17[19]`, which reads as four WIDGETS apart. It is not: the step is four BYTES,
 * so the three pointers are one widget's pPad0/pPad1/pPad2 (UiControl + 0x14, +0x18,
 * +0x1C) and the group base is a widget index. The zeroing writes back into the same
 * three slots -- `*((_DWORD *)&unk_AC930 + 8*j + v16 + 5)` is unk_AC930 + 32*j +
 * 4*(v16+5), and unk_AC930 is pBank + 0x260 = pBank + 32*19, so dword 5 is +0x14.
 *
 * The thirteen groups cover bank widgets 0..112 and 128..129. StartUi_Shutdown
 * (0x38d7c) covers 113..127. Together they cover all 130 exactly once, which is the
 * cross-check that the group table below is right.
 *
 * ## Deviations
 *
 * 1. The original operator-deletes a global and leaves the global dangling. The port
 *    takes every freed slot as a pointer-to-pointer and clears it, because a second
 *    shutdown would otherwise be a double free. This is the same deviation already
 *    recorded for UiControl_FreePads @ 0x1a2c8.
 * 2. `UiPanel_FreeLines` (0x3e8c8) and `UiControl_FreePads` (0x1a2c8) live in
 *    pufo_ui, and pufo_ui links pufo_game. They are reached through hooks so this
 *    module does not close the cycle. The hooks are load-bearing: without them
 *    pufo_game would depend on pufo_ui. See the separate issue in PLAN.md.
 */
#ifndef PUFO_TACTEND_H
#define PUFO_TACTEND_H

#include "game/pufo_font.h"
#include "game/pufo_geoscape.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_rmp.h"
#include "game/pufo_ship.h"
#include "pufo_common.h"
#include "ui/pufo_control.h"
#include "ui/pufo_panel.h"

struct PtrArray;
struct Soldier;

/* The four message panels get nLineCount = 14 before they are freed; the fifth gets 28. */
#define PUFO_SHUTDOWN_MSG_PANELS   4
#define PUFO_SHUTDOWN_LINE_COUNT   14
#define PUFO_SHUTDOWN_MSG_PANEL_28 28

/* Pad globals freed before the bank walk, and after it. Order is the original's. */
#define PUFO_SHUTDOWN_PAD_EARLY 2   /* 0x15692C, 0x156928 */
#define PUFO_SHUTDOWN_PAD_LATE  7   /* 0x113AB8, 0x1571F0, 0x156A70, 0x156AA0,
                                     * 0x156A74, 0x128AC0, 0x113AB4 */
#define PUFO_SHUTDOWN_PAD_TAIL  3   /* 0x113ABC, 0x138EB8, 0x156A80 */

/* The tactical sprite set at Map+0x199C, freed as 4 x 2 x 4, and the eleven slots at
 * Map+0xCC. */
#define PUFO_SHUTDOWN_SPRITE_SET 32
#define PUFO_SHUTDOWN_MAP_PADS   11

/* One bank pad group: the first widget and how many widgets it covers. */
typedef struct TactShutdownBankGroup {
    u8 nFirst;
    u8 nCount;
} TactShutdownBankGroup;

#define PUFO_SHUTDOWN_BANK_GROUPS 13
extern const TactShutdownBankGroup g_aTactShutdownBankGroups[PUFO_SHUTDOWN_BANK_GROUPS];

/* Seams. The first two stand in for the audio and timer shutdowns, which need a
 * backend the caller owns -- the same seam StartUi_ShutdownHooks already uses. The
 * last two stand in for the pufo_ui callees, so pufo_game does not depend on pufo_ui. */
typedef struct TactShutdownHooks {
    void (*pfnSoundShutdown)(void *pUser);                    /* Sound_Shutdown (0x9e3d4) */
    void (*pfnKillTimers)(void *pUser);                       /* KillTimers_BySpeed (0x9e8b4) */
    void (*pfnPanelFreeLines)(UiPanel *pPanel, void *pUser);  /* UiPanel_FreeLines (0x3e8c8) */
    void (*pfnControlFreePads)(UiControl *pCtl, void *pUser); /* UiControl_FreePads (0x1a2c8) */
    void *pUser;
} TactShutdownHooks;

typedef struct TactShutdownEnv {
    /* --- the tactical UI --- */
    /* Every slot below is a pointer to the caller's storage, not a pointer held in the
     * env: the env is const, and the port clears each freed slot through it. */
    UiPanel  **appMsgPanel;      /* 4 entries: 0x1568DC, 0x1568E0, 0x1568E4, 0x1568E8 */
    UiPanel  **appLinePanel;     /* unk_1571EC, nLineCount set to 28 */
    UiControl **appLookCtl;      /* dword_1571E4 */
    UiControl  *pBank;           /* pBank @ 0xAC6D0 */

    PadImage **appPadEarly;      /* PUFO_SHUTDOWN_PAD_EARLY entries */
    PadImage **appPadLate;       /* PUFO_SHUTDOWN_PAD_LATE entries */
    PadImage **appPadTail;       /* PUFO_SHUTDOWN_PAD_TAIL entries */
    PadImage **appSpriteSet;     /* 32 slots at Map+0x199C */
    PadImage **appMapPads;       /* 11 slots at Map+0xCC */

    RmpData  **appRmp;           /* pRmpMissionData @ 0x15881C */
    void     **appBlock156A78;   /* deleted with no null test */
    void     **appBlock187050;   /* deleted only on the bKeepSome path */
    PufoFont  *pFont;            /* fontSys @ 0x158268 */
    u8        *pbActionLatch;    /* Map+0x00, cleared first */

    /* --- the tactical item pool, freed on both paths --- */
    TactItem **apTactItems;      /* Map+0x23438 */
    i32       *pnTactItemCount;  /* Map_nTactItemCount @ Map+0x1513C */

    /* --- the bKeepSome path: the geoscape rosters stay, their tactical copies go --- */
    void **apStrategCraftCopy;   /* Map+0x1198, count unk_115350 (u16) */
    u16   *pnStrategCraftCopy;
    void **apBaseCopy;           /* Map+0x1174, count is the signed byte at 0x159D80 */
    i8    *pnBaseCount;
    void **apCraftCopy;          /* Map+0x10AC, count Map_nCraftCount */
    u8    *pnCraftCount;
    struct PtrArray *pStrategList;

    /* --- the full-shutdown path: tactical units --- */
    struct Soldier **apCivilianUnits;  /* Map+0x15244 */
    i32             *pnCivilianCount;  /* Map+0x15120 */
    struct Soldier **apEnemyUnits;     /* Map+0x151CC */
    u8              *pnEnemyCount;     /* Map+0x15124, a byte in the original */
    struct Soldier **apXcomUnits;      /* Map+0x15154 */
    u8              *pnXcomCount;      /* Map+0x1511F, a byte in the original */

    /* --- the mission outcome decision table --- */
    u8 *pbMissionMode;           /* Map_bMissionMode @ Map+0x1511D */
    u8 *pbMissionAborted;        /* unk_128A8C @ Map+0x150D4 */
    u8 *pbAlternateOutcomePath;  /* unk_1153D9 */
    u8 *pbFacilityType;          /* unk_159D81, index into the base's facility grid */
    i32 *pnStrategIndex;         /* MEMORY[0x159E74], the current strateg item */
    u8 *pnCraftIndex;            /* Map_nCraftIndex @ Map+0x15113 */
    u8 *pbSpawnOnceFlag;         /* 0x162028, the replacement-spawn gate */
    const BaseDemolishEnv   *pDemolishEnv;
    const ShipRemovalEnv    *pShipEnv;
    const TacticalLootEnv   *pLootEnv;

    /* --- the tactical map itself --- */
    MapCell **apCells;      /* Map_apCells0 @ Map+0x15338, 3600 slots per layer */
    u8      *pnMapDimX;     /* Map_nMapDimX @ Map+0x1511B, in map tiles */
    u8      *pnMapDimY;     /* Map_nMapDimY @ Map+0x1511C, in map tiles */
    void    **appLoftemps;  /* unk_15718C */
    void    **appUnitloft;  /* unk_1571E8 */
    void    **appMapTactical; /* pMapTactical @ 0x157198 */
    void    **appMapCtx;    /* unk_157180 */

    TactShutdownHooks hooks;
} TactShutdownEnv;

/* Tact_Shutdown @ 0x3d568. bKeepSome non-zero keeps the world and the units and drops
 * only the tactical UI, the tactical item pool and the tactical map. */
void Tact_Shutdown(const TactShutdownEnv *pEnv, u8 bKeepSome);

#endif /* PUFO_TACTEND_H */
