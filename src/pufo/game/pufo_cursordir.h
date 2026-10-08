/* pufo_cursordir.h - the tactical cursor-direction animation state machine.
 *
 * Evidence: TactHud_ComputeCursorDirState @ 0x1a7c8,
 *           TactHud_TickCursorDir @ 0x1e888.
 *
 * The original stores the state in `pCtxBase.apUiSlotsA`: byte 2 and byte 3 of
 * slot 8 are the two direction bytes, slot 9 is the per-tick step and slot 10
 * the countdown. The port models those four cells as CursorDirState.
 *
 * The decompiler reaches the slot-8 bytes as BYTE2/HIBYTE of the dword at
 * apUiSlotsA[8]; on little-endian ARM those are the bytes at absolute offsets
 * 0x82 and 0x83.
 *
 * `TactHud_TickCursorDir`'s busy/track condition is
 * `!bBusy || bHasTrackUnit || bUnk70115`; the original reads
 * `unk_157180->bBusy`, `unk_157180->pTrackUnit` and
 * `unk_157198->bUnk70115`, which the port injects as three flags.
 */
#ifndef PUFO_CURSORDIR_H
#define PUFO_CURSORDIR_H

#include "pufo_common.h"

typedef struct CursorDirState {
    u8  bSlot8Byte2; /* apUiSlotsA[8] byte 2 */
    u8  bSlot8Byte3; /* apUiSlotsA[8] byte 3 */
    i32 nSlot9;      /* apUiSlotsA[9], signed step added to nSlot10 */
    i32 nSlot10;     /* apUiSlotsA[10], countdown */
} CursorDirState;

/* TactHud_ComputeCursorDirState @ 0x1a7c8. The four (byte3, byte2) cases map to
 * (step, countdown): (0,0)->(0,10), (0,1)->(-1,9), (1,0)->(1,1), (1,1)->(0,0). */
void TactHud_ComputeCursorDirState(CursorDirState *pState);

/* TactHud_TickCursorDir @ 0x1e888. Advances nSlot10 unless it is 0 or 10,
 * shifts byte2 into byte3 and rewrites byte2 as 0 (condition true) or 1
 * (condition false), then recomputes the direction when the byte2 test holds. */
void TactHud_TickCursorDir(CursorDirState *pState, bool bBusy,
                           bool bHasTrackUnit, bool bUnk70115);

#endif /* PUFO_CURSORDIR_H */
