/* pufo_statusmsg.h - the on-screen status message stack.
 *
 * Evidence: StatusMsg_Push @ 0x2dda4, StatusMsg_Shift @ 0x2dd20,
 *           StatusMsg_DrawStack @ 0x1afc4.
 *
 * ## Layout (resolved against the `Map` type)
 *
 *     nStatusMsgCount  @ 0x0054  (i32)
 *     anStatusMsgTTL   @ 0x1408C (i32[10] declared, used as [count])
 *     aszStatusMsg     @ 0x140B4 (1500 bytes = 15 records of 100)
 *     nStatusDrawY     @ 0x002C  (i32)
 *     pHudPad          @ 0x00FC
 *     pInvPad          @ 0x0100, reused as the draw loop's index!
 *
 * `aszStatusMsg` is exactly 1500 bytes = **15 messages of 100 bytes**, and that
 * is where the `>= 15` guard in `StatusMsg_Push` comes from.
 *
 * ## The push policy, which is not a simple stack
 *
 * `StatusMsg_Push(msg, bForce)`:
 *
 *     if (!bForce && busy) return;                 // silently dropped
 *     if (count < 3)            { append at count; ttl = 90; }
 *     else if (!busy || count >= 15) { Shift(); append at count; ttl = 90; }
 *     else                      { return; }        // silently dropped
 *     if (renderFlag) Render_Frame();
 *
 * So the rule is: **below three messages it always appends**, and **at three or
 * more it drops the oldest and appends only when the game is not busy or the
 * buffer is completely full**. A busy game with 3..14 messages showing therefore
 * discards the new message entirely. That asymmetry is the original's; the
 * threshold 3 and the capacity 15 both come straight from the guards.
 *
 * Every appended message gets a TTL of **90** frames, regardless of length.
 *
 * ## Shift
 *
 * `StatusMsg_Shift` copies records `1..count-1` down by one (both the text and the
 * TTL) and decrements the count. Verified against the decompilation, whose TTL
 * write `p_nMultiTargetCount[20514] = p_nMultiTargetCount[20515]` resolves to
 * `anStatusMsgTTL[i-1] = anStatusMsgTTL[i]` starting at i = 1.
 *
 * ## Draw
 *
 * `StatusMsg_DrawStack` walks the stack newest-first. The decompilation's source
 * pointer expression `&nFireArg6 + 100 * (count - i)` resolves through
 * `0x14050 + 100*k`, and `aszStatusMsg[0]` is at `0x140B4 = 0x14050 + 100`, so the
 * first record drawn is `aszStatusMsg[count - 1]`. The original reuses `pInvPad`
 * as the loop counter, which is why that field appears in a draw function.
 */
#ifndef PUFO_STATUSMSG_H
#define PUFO_STATUSMSG_H

#include "pufo_common.h"

/* One record is 100 bytes and the array is 1500, so the capacity is 15. */
#define PUFO_STATUS_REC_BYTES 100
#define PUFO_STATUS_MAX       15

/* The append-only threshold below which a push never discards anything. */
#define PUFO_STATUS_APPEND_MAX 3

/* Frames every pushed message lives for. */
#define PUFO_STATUS_TTL 90

typedef struct StatusStack {
    i32  nCount;                                  /* 0x54 */
    i32  anTtl[PUFO_STATUS_MAX];                  /* 0x1408C */
    char aszMsg[PUFO_STATUS_MAX][PUFO_STATUS_REC_BYTES]; /* 0x140B4 */
} StatusStack;

/* What the push needs from the world: the busy flag that gates it, whether the
 * display is active, and the frame redraw the original triggers afterwards. */
typedef struct StatusHooks {
    /* unk_157180->bBusy != 0. The original reads the tactical context. */
    bool (*pfnIsBusy)(void *pUser);
    /* LOBYTE(apUiSlotsA[2]) != 0, the flag that makes a push repaint at once. */
    bool (*pfnShouldRender)(void *pUser);
    /* Render_Frame @ 0x22310. */
    void (*pfnRender)(void *pUser);
    void *pUser;
} StatusHooks;

/* StatusMsg_Shift @ 0x2dd20: drop the oldest message. No-op at count 0.
 *
 * Note the original shifts records DOWN (index i-1 gets index i), so index 0 is
 * the OLDEST and index count-1 the newest. */
void StatusMsg_Shift(StatusStack *pStack);

/* StatusMsg_Push @ 0x2dda4. Returns true when the message was stored.
 *
 * A false return is normal and matches the original: `!bForce && busy` drops the
 * message, and so does being busy with 3..14 messages already showing. */
bool StatusMsg_Push(StatusStack *pStack, const char *pszMsg, bool bForce,
                    const StatusHooks *pHooks);

/* StatusMsg_DrawStack @ 0x1afc4.
 *
 * The stack walk is the part this module owns; the actual glyph drawing is
 * injected because it needs the font/glyph layer. `pfnDraw` receives the x and y
 * the original computes and the record to draw, newest first.
 *
 * The y arithmetic is reproduced exactly: start at 320, or at `pHudPad->nOriginY`
 * when player input is enabled AND a selected unit exists AND it is operative AND
 * the game is not busy, then subtract 14 once, then 12 per already-drawn record.
 *
 * Returns the number of records drawn. */
typedef struct StatusDrawHooks {
    void (*pfnSetColor)(u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2, void *pUser);
    void (*pfnDraw)(i32 x, i32 y, const char *pszMsg, void *pUser);
    /* The four conditions the hud-pad branch tests. */
    bool (*pfnPlayerInputEnabled)(void *pUser);
    bool (*pfnSelectedIsOperative)(void *pUser);
    bool (*pfnIsBusy)(void *pUser);
    /* pHudPad->nOriginY; only called when the branch is taken. */
    i32  (*pfnHudPadOriginY)(void *pUser);
    void *pUser;
} StatusDrawHooks;

/* The x every message is drawn at. */
#define PUFO_STATUS_DRAW_X 5

i32 StatusMsg_DrawStack(StatusStack *pStack, i32 *pnOutDrawY,
                        const StatusDrawHooks *pHooks);

#endif /* PUFO_STATUSMSG_H */
