/* pufo_linelist.c - the 3401-byte line list.
 *
 * Evidence and the type conflict with UiPanel: see pufo_linelist.h.
 */
#include "ui/pufo_linelist.h"

#include <string.h>

/* The original moves a record with a 16-byte byte-at-a-time loop; the record is one
 * aligned 16-byte block, so memcpy is the same move. */
static void Line_MoveRec(UiLineList *pList, i32 nDst, i32 nSrc)
{
    memcpy(&pList->anColor[nDst][0], &pList->anColor[nSrc][0], sizeof(pList->anColor[0]));
}

/* The write boundary of UiPanel_InsertLine @ 0x5a294 and UiPanel_SetLineColors @ 0x5a450. The flag
 * is a byte store (STRB at 0x5A2F8) and every colour slot is truncated to a byte before its dword
 * store: slot 0 through `AND R3, R3, #0xFF` at 0x5A2FC, slots 1..3 through `LDRB R3, [SP,#arg_N]`
 * at 0x5A30C, 0x5A314 and 0x5A31C. A caller that hands an index in a colour slot therefore cannot
 * put more than 255 there -- Smap_HitTestStrategSelect @ 0x836A0 stores -1 into 0x187054 and appends
 * it, and the original writes 255. The port masked nothing until that call site made it visible. */
static void Line_Write(UiLineList *pList, i32 nLine, u8 bFlag,
                       u32 nC0, u32 nC1, u32 nC2, u32 nC3)
{
    pList->abLineFlags[nLine] = bFlag;
    pList->anColor[nLine][0] = nC0 & PUFO_LINE_COLOR_MAX;
    pList->anColor[nLine][1] = nC1 & PUFO_LINE_COLOR_MAX;
    pList->anColor[nLine][2] = nC2 & PUFO_LINE_COLOR_MAX;
    pList->anColor[nLine][3] = nC3 & PUFO_LINE_COLOR_MAX;
}

void UiLineList_Insert(UiLineList *pList, u8 nLine, u8 bFlag,
                       u32 nC0, u32 nC1, u32 nC2, u32 nC3)
{
    i32 nCount;
    i32 nFrom;

    if (pList == NULL) {
        return;
    }
    nCount = (i32)pList->nLineCount;

    /* 0x5A2AC: shift only when the count is past the insertion point, and the shift
     * runs from the top end downwards. */
    if (nCount > (i32)nLine) {
        i32 nSteps = nCount - (i32)nLine;
        for (i32 k = nCount; nSteps > 0; --nSteps, --k) {
            /* Deviation 1: the original writes flags[count] and record[count], which
             * runs past the 200-entry block when the list is full. The port stops at
             * the capacity instead. */
            if (k <= 0 || k >= PUFO_LINELIST_CAPACITY) {
                continue;
            }
            pList->abLineFlags[k] = pList->abLineFlags[k - 1];
            Line_MoveRec(pList, k, k - 1);
        }
        nFrom = (i32)nLine;
        if (nFrom < PUFO_LINELIST_CAPACITY) {
            Line_Write(pList, nFrom, bFlag, nC0, nC1, nC2, nC3);
        }
    } else if ((i32)nLine < PUFO_LINELIST_CAPACITY) {
        /* No shift: the original writes the slot anyway, so inserting past the end
         * leaves a gap and still increments the count. Reproduced as-is. */
        Line_Write(pList, (i32)nLine, bFlag, nC0, nC1, nC2, nC3);
    }

    /* 0x5A32C: the count increments unconditionally, and it is a byte. */
    pList->nLineCount = (u8)(pList->nLineCount + 1);
}

void UiLineList_SetLine(UiLineList *pList, u8 nLine, u8 bFlag,
                        u32 nC0, u32 nC1, u32 nC2, u32 nC3)
{
    if (pList == NULL || (i32)nLine >= PUFO_LINELIST_CAPACITY) {
        return;
    }
    Line_Write(pList, (i32)nLine, bFlag, nC0, nC1, nC2, nC3);
}

u8 UiLineList_GetFlag(const UiLineList *pList, i32 nLine)
{
    if (pList == NULL || nLine < 0 || nLine >= PUFO_LINELIST_CAPACITY) {
        return 0;
    }
    return pList->abLineFlags[nLine];
}

u32 UiLineList_GetColor(const UiLineList *pList, i32 nLine, i32 nSlot)
{
    if (pList == NULL || nLine < 0 || nLine >= PUFO_LINELIST_CAPACITY) {
        return 0;
    }
    /* 0x5A348: the switch reads slot 0..3 and returns zero for anything else. */
    if (nSlot < 0 || nSlot >= PUFO_LINELIST_SLOTS) {
        return 0;
    }
    return pList->anColor[nLine][nSlot];
}

void UiLineList_Remove(UiLineList *pList, u8 nLine)
{
    if (pList == NULL) {
        return;
    }
    i32 nCount = (i32)pList->nLineCount;

    if ((i32)nLine < nCount) {
        for (i32 k = (i32)nLine; k < nCount; ++k) {
            /* Deviation 1 again: flags[count] is read when the list is full. */
            if (k + 1 >= PUFO_LINELIST_CAPACITY) {
                break;
            }
            pList->abLineFlags[k] = pList->abLineFlags[k + 1];
            Line_MoveRec(pList, k, k + 1);
        }
    }
    /* 0x5A444: the decrement is guarded, so an empty list stays empty. */
    if (pList->nLineCount != 0) {
        pList->nLineCount = (u8)(pList->nLineCount - 1);
    }
}

void UiLineList_AppendWithSound(UiLineList *pList, u8 bFlag,
                                u32 nC0, u32 nC1, u32 nC2, u32 nC3,
                                void (*pfnPlaySample)(i32 nSample, void *pUser),
                                void *pUser)
{
    if (pList == NULL) {
        return;
    }
    /* 0x5A394: append is an insert at the current count, so no shift ever runs. */
    UiLineList_Insert(pList, pList->nLineCount, bFlag, nC0, nC1, nC2, nC3);
    if (pfnPlaySample != NULL) {
        pfnPlaySample(7, pUser);
    }
}
