#ifndef PUFO_TACTHUD_H
#define PUFO_TACTHUD_H

/* Tactical HUD action buttons: the click handlers the tactical screen installs
 * through Tact_InitUiControls @ 0x3b498. They act on Map.pSelUnit directly and
 * are reached from UiControl_HitTest's pfnClick, not from a key.
 *
 * Evidence: TactHud_ToggleKneel @ 0x16570, TactHud_ClimbUp @ 0x1674c,
 *           TactHud_ClimbDown @ 0x16af0. */

#include "game/pufo_mapcell.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"

/* The texts.dat records the kneel action announces. Kneeling is the shot
 * accuracy bonus, so the message is about marksmanship, not about the pose:
 * aTextRecs + 205*1606 = 0x5060E and + 205*1607 = 0x506DB, read straight from
 * the ARM at 0x1661C and 0x16654. */
#define PUFO_TXTREC_KNEEL_ON  1606
#define PUFO_TXTREC_KNEEL_OFF 1607

typedef struct TactHudCtx {
    Soldier       *pSelUnit;      /* Map.pSelUnit @ 0x15334 */
    TactState     *pQueue;        /* unk_157198 */
    MapGeom        geom;          /* nMapDimX @ 0x1511B, nMapDimY @ 0x1511C */
    MapCellLookup  pfnLookup;
    void          *pLookupUser;

    bool (*pfnArmorTuWarn)(const Soldier *pUnit, i32 nTuCost, void *pUser);
    void (*pfnPushTextRecord)(i32 nRecord, void *pUser);
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void  *pCallbackUser;
} TactHudCtx;

/* TactHud_ToggleKneel @ 0x16570. Costs 2 TU and flips Soldier.bFlags bit 0.
 * The pose is adjusted by hand (nAnimPose +/- 1); Unit_UpdateAnimPose is not
 * called, unlike the queue's stand-from-kneel command. Nothing is queued. */
void TactHud_ToggleKneel(const TactHudCtx *pCtx);

/* TactHud_ClimbUp @ 0x1674c / TactHud_ClimbDown @ 0x16af0. Both check the cell
 * one layer up (down) and, when it is clear, enqueue a vertical step with
 * nFacing 1 (-1) and start the queue. Neither spends TU here; the cost is paid
 * when the queue reaches command 5. */
void TactHud_ClimbUp(const TactHudCtx *pCtx);
void TactHud_ClimbDown(const TactHudCtx *pCtx);

#endif /* PUFO_TACTHUD_H */
