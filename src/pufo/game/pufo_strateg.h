/* pufo_strateg.h - the geoscape contact list and its per-minute updates.
 *
 * Evidence: StrategList_CountActive @ 0x81a2c, StrategList_FindByShipId @
 * 0x5a9b4, StrategList_UpdateMotion @ 0x80fd8, StrategList_UpdateDetect @
 * 0x81948, Strateg_CalcDetect @ 0x81378, Ship_ClearExpiredIntercepts @
 * 0x8128c. The list is the global `strategList` @ 0x186534 (a PtrArray of
 * StrategItem*, IDB type StrategItem, 136 bytes); the id counters are
 * nNextStrategDisplayId @ 0x186544 and nNextStrategSiteId @ 0x186548.
 *
 * Per-base side tables the detect code reads: byte_161F38 @ 0x161f38 is the
 * site state written by Geoscape_TickBases (1, 2, 11, 12 seen), and
 * unk_162008 @ 0x162008 is the per-base radar percentage.
 */
#ifndef PUFO_STRATEG_H
#define PUFO_STRATEG_H

#include "pufo_common.h"
#include "game/pufo_clock.h"   /* GameTime, for the pause byte */
#include "game/pufo_ship.h"

typedef struct StrategItem {
    u8   nType;          /* +0x00 0 base, 1 craft */
    u8   bActive;        /* +0x01 */
    u8   bCraftClass;    /* +0x02 */
    u8   bUnk3;          /* +0x03 */
    i32  nX;             /* +0x04 map x, wrapped at +/-800 */
    i32  nY;             /* +0x08 */
    i32  nSiteId;        /* +0x0c 50000 = not yet assigned */
    i32  nDisplayId;     /* +0x10 50000 = not yet assigned; equals Ship.bId for player craft */
    u8   bRandA;         /* +0x14 */
    u8   gap_15[3];
    i32  nStartX;        /* +0x18 */
    i32  nStartY;        /* +0x1c */
    i32  nDestX;         /* +0x20 */
    i32  nDestY;         /* +0x24 */
    i8   bTravelDir;     /* +0x28 */
    u8   gap_29[3];
    i32  nTimer;         /* +0x2c proximity-detect accumulation */
    u8   bFlagA;         /* +0x30 */
    u8   gap_31[7];
    u8   bVisible;       /* +0x38 */
    u8   nMissionSub;    /* +0x39 */
    u8   gap_3A[6];
    double dTrackA;      /* +0x40 */
    double dSpeed;       /* +0x48 */
    double dFracX;       /* +0x50 */
    double dFracY;       /* +0x58 */
    i32  nTravelStartSec;/* +0x60 */
    u8   bHyperDetect;   /* +0x64 */
    u8   bDetected;      /* +0x65 */
    u16  uTravelMin;     /* +0x66 */
    i32  nTravelEpochSec;/* +0x68 */
    i32  nHp;            /* +0x6c */
    u8   bRace;          /* +0x70 */
    u8   gap_71[3];
    i32  nPhase;         /* +0x74 */
    u8   bDone;          /* +0x78 */
    u8   gap_79[3];
    i32  nTargetX;       /* +0x7c */
    i32  nTargetY;       /* +0x80 */
    i32  nTargetIdx;     /* +0x84 */
} StrategItem;

_Static_assert(sizeof(StrategItem) == 136, "StrategItem must be 136 bytes");

#define STRATEG_ID_UNASSIGNED 50000

typedef struct StrategList {
    StrategItem **apItems;
    i32 nCount;
    i32 nNextDisplayId;  /* nNextStrategDisplayId @ 0x186544 */
    i32 nNextSiteId;     /* nNextStrategSiteId @ 0x186548 */
} StrategList;

/* The pieces of the geoscape world the contact updates reach into. The ship
 * array is pCtxBase.apCraft (count byte at pCtxBase+0x1194). */
typedef struct GeoscapeWorld {
    Ship *const *apCraft;
    u8    nShipCount;
    u8   *apSiteState;   /* byte_161F38, one byte per base */
    u8   *apRadarPct;    /* unk_162008, one byte per base */
    i32   nTickSeconds;  /* dword_187070: seconds per game tick (5/60/300/...) */
    i32   nRadarBoostOff;/* apLines[117]: zero adds +7 to every radar pct */
    i8    nMinuteTicks;  /* apLines[118], signed byte counter */
    GameTime *pTime;     /* the clock: bPaused is apLines[106], the byte
                          * GameTime_Pause @ 0x5d6ac writes. Contact messages
                          * pause the game, so the pause byte has to be shared
                          * state, not a per-module flag. */
} GeoscapeWorld;

/* Hooks the contact updates need from the UI / clock layer. */
typedef struct StrategHooks {
    /* UiPanel_AppendLineWithSound @ 0x5a394 only. nMsg is the original message
     * id (10 base contact, 13 intercept lost). The GameTime_Pause that follows
     * a contact in the original is written directly through pWorld->pTime, not
     * through this hook: 10, 13 and 15 pause, 43 (low supply) does not. */
    void (*pfnContactMsg)(i32 nMsg, i32 nArg3, i32 nArg4, void *pUser);
    /* Base_FindIndexBySiteId @ 0x5a8d0. */
    i32 (*pfnFindBaseBySiteId)(i32 nSiteId, void *pUser);
    void *pUser;
} StrategHooks;

/* StrategList_CountActive @ 0x81a2c: count of nType==1 && bActive==1. */
i32 StrategList_CountActive(const StrategList *pList);

/* StrategList_FindByShipId @ 0x5a9b4: index of the craft entry whose
 * nDisplayId equals apCraft[nShipIdx]->bId, or -1. */
i32 StrategList_FindByShipId(const StrategList *pList,
                             const GeoscapeWorld *pWorld, i32 nShipIdx);

/* StrategList_UpdateMotion @ 0x80fd8. Despite the name it is the proximity
 * detect pass; see the .c file for the exact rules, including the odd
 * sqrt(dx^2 + dy^2 + threshold) distance. */
void StrategList_UpdateMotion(StrategList *pList, GeoscapeWorld *pWorld);

/* StrategList_UpdateDetect @ 0x81948: radar pass over active craft. */
void StrategList_UpdateDetect(StrategList *pList, GeoscapeWorld *pWorld,
                              const StrategHooks *pHooks);

/* Strateg_CalcDetect @ 0x81378. Returns true when the ufo is detected by at
 * least one base; sets bHyperDetect for the 11/12 site states. */
bool Strateg_CalcDetect(StrategItem *pUfo, i32 nIndex, i32 nForcePct,
                        const StrategList *pList, const GeoscapeWorld *pWorld,
                        const StrategHooks *pHooks);

/* Ship_ClearExpiredIntercepts @ 0x8128c: drop intercepts whose target went
 * invisible (but not the ones still waiting for a display id). */
void Ship_ClearExpiredIntercepts(GeoscapeWorld *pWorld, StrategList *pList,
                                 const StrategHooks *pHooks);

#endif /* PUFO_STRATEG_H */
