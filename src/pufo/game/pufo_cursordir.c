/* pufo_cursordir.c - see pufo_cursordir.h for evidence and the byte mapping. */
#include "game/pufo_cursordir.h"

void TactHud_ComputeCursorDirState(CursorDirState *pState)
{
    u8 bByte3 = pState->bSlot8Byte3;
    u8 bByte2 = pState->bSlot8Byte2;

    if (bByte3 == 0 && bByte2 == 0) {
        pState->nSlot9 = 0;
        pState->nSlot10 = 10;
    } else if (bByte3 == 0) {
        pState->nSlot9 = -1;
        pState->nSlot10 = 9;
    } else if (bByte2 == 0) {
        pState->nSlot9 = 1;
        pState->nSlot10 = 1;
    } else {
        pState->nSlot9 = 0;
        pState->nSlot10 = 0;
    }
}

void TactHud_TickCursorDir(CursorDirState *pState, bool bBusy,
                           bool bHasTrackUnit, bool bUnk70115)
{
    u8 bByte2 = pState->bSlot8Byte2;
    u8 bNewByte2 = (!bBusy || bHasTrackUnit || bUnk70115) ? 0 : 1;
    /* The original computes v4 = (old byte2 == expected) and recomputes when v4
     * is false: non-busy recomputes for old byte2 != 0, busy for old byte2 != 1. */
    bool bRecompute = (bNewByte2 == 0) ? (bByte2 != 0) : (bByte2 != 1);

    /* The countdown is only advanced when it is neither 0 nor 10. */
    if (pState->nSlot10 != 0 && pState->nSlot10 != 10) {
        pState->nSlot10 = pState->nSlot10 + pState->nSlot9;
    }
    /* The old byte2 shifts into byte3, then byte2 takes the new value. */
    pState->bSlot8Byte3 = bByte2;
    pState->bSlot8Byte2 = bNewByte2;
    if (bRecompute) {
        TactHud_ComputeCursorDirState(pState);
    }
}
