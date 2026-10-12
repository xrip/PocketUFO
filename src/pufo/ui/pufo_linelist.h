/* pufo_linelist.h - the 3401-byte line list IDA merges under the name UiPanel.
 *
 * Evidence: UiPanel_InsertLine @ 0x5a294, UiPanel_GetLineFlag @ 0x5a334,
 *           UiPanel_GetLineColor @ 0x5a33c, UiPanel_AppendLineWithSound @ 0x5a394,
 *           UiPanel_RemoveLine @ 0x5a3dc, UiPanel_SetLineColors @ 0x5a450.
 *
 * ## Why this is not a UiPanel
 *
 * Those six functions index their first argument as
 *
 *     u8   abLineFlags[200]   at +0x000 .. +0x0C7   (plain byte index per line)
 *     rec  aRec[200]          at +0x0C8 .. +0xD47   (16 bytes each)
 *     u8   nLineCount         at +0xD48 (3400)
 *
 * and every one of them agrees: the flag is `base + nLine`, the record is
 * `base + 16*nLine + 200`, and the count is the byte at 3400. The capacity falls out
 * of the arithmetic: 200 + 16*200 = 3400, so the flags and the records tile the block
 * exactly and the count byte sits right after them.
 *
 * The 7092-byte `UiPanel` in pufo_panel.h is the real type of the DRAW path, proven
 * two independent ways: `gUiRootPanel @ 0x158890 + apLines(0xBE4) + 4*105 == 0x159618`
 * is the game-clock field and `+4*106 == 0x15961C` is the geoscape flag, and
 * `UiScrollList_TrackHold @ 0x3ed44` addresses the same 7092 layout field for field.
 * IDA merged the two under one name, so these six are ported here as their own type
 * rather than as UiPanel members.
 *
 * The caller that shows the alias is `ResearchScreen_RebuildTopicList (0x74270)`: it
 * passes `(int)&gUiRootPanel` to `UiPanel_AppendLineWithSound @ 0x5a394` and to
 * `UiPanel_SetLineColors @ 0x5a450`, and reads the line count as
 * `LOBYTE(gUiRootPanel.apLines[89]) - 1` -- `apLines[89]` is offset 3400 of the 7092
 * layout, which is exactly this structure's count byte. The original overlays the two
 * views on the same 7092 bytes. The port keeps them as separate types and does not
 * model the overlay. */
#ifndef PUFO_LINELIST_H
#define PUFO_LINELIST_H

#include "pufo_common.h"

#define PUFO_LINELIST_CAPACITY 200
#define PUFO_LINELIST_COUNT_OFF 3400 /* 200 flags + 200 * 16 record bytes */
#define PUFO_LINELIST_SLOTS 4

typedef struct UiLineList {
    u8  abLineFlags[PUFO_LINELIST_CAPACITY];
    u32 anColor[PUFO_LINELIST_CAPACITY][PUFO_LINELIST_SLOTS];
    u8  nLineCount;
} UiLineList;

/* UiPanel_InsertLine @ 0x5a294. Insert at `nLine`.
 *
 * When the current count is above `nLine`, entries are shifted up one place starting
 * from the top end and working down: `flags[k] = flags[k-1]` and the whole 16-byte
 * record moves. Then the slot is written and the count increments.
 *
 * The original does NOT clamp `nLine` to the count: inserting past the end leaves a
 * gap and still increments the count. That is reproduced, not fixed. */
void UiLineList_Insert(UiLineList *pList, u8 nLine, u8 bFlag,
                       u32 nC0, u32 nC1, u32 nC2, u32 nC3);

/* UiPanel_SetLineColors @ 0x5a450. Overwrite one line's flag and four colour slots
 * without touching the count. */
void UiLineList_SetLine(UiLineList *pList, u8 nLine, u8 bFlag,
                        u32 nC0, u32 nC1, u32 nC2, u32 nC3);

/* UiPanel_GetLineFlag @ 0x5a334. */
u8 UiLineList_GetFlag(const UiLineList *pList, i32 nLine);

/* UiPanel_GetLineColor @ 0x5a33c. Slot 0..3, anything else reads zero. */
u32 UiLineList_GetColor(const UiLineList *pList, i32 nLine, i32 nSlot);

/* UiPanel_RemoveLine @ 0x5a3dc. Shift entries down from `nLine` upwards, then
 * decrement the count, but only when it is not already zero. Removing from an empty
 * list leaves the count at zero. */
void UiLineList_Remove(UiLineList *pList, u8 nLine);

/* UiPanel_AppendLineWithSound @ 0x5a394. Insert at the current count, then play
 * sample 7. The original returns Sound_Play's value; the port returns void. */
void UiLineList_AppendWithSound(UiLineList *pList, u8 bFlag,
                                u32 nC0, u32 nC1, u32 nC2, u32 nC3,
                                void (*pfnPlaySample)(i32 nSample, void *pUser),
                                void *pUser);

#endif /* PUFO_LINELIST_H */
