/* pufo_ptrarray.h - the game's growable pointer list.
 *
 * Evidence: PtrArray_ctor @ 0xa0f80, PtrArray_Insert @ 0xa0f94,
 *           PtrArray_Add @ 0xa1044, PtrArray_Remove @ 0xa1054,
 *           PtrArray_Clear @ 0xa1120, PtrArray_Get @ 0xa1130,
 *           PtrArray_GetCount @ 0xa1154, PtrArray_DeleteAll @ 0x3e434,
 *           PtrArray_DeleteAt @ 0x82bac.
 *
 * Layout matches the IDB: { int nCount; void **apItems; }.
 * Insert/Remove are non-amortized: every change reallocates and copies, exactly
 * as the original does. Kept because the original's callers rely on the exact
 * index semantics, not on the cost.
 */
#ifndef PUFO_PTRARRAY_H
#define PUFO_PTRARRAY_H

#include "pufo_common.h"

#define PUFO_PTRARRAY_INDEX_LAST 0xFFFF /* nIndex == this means "all" */

typedef struct PtrArray {
    int   nCount;
    void **apItems;
} PtrArray;

void  PtrArray_ctor(PtrArray *pArr);
void  PtrArray_Insert(PtrArray *pArr, int nIndex, void *pItem);
void  PtrArray_Add(PtrArray *pArr, void *pItem);
void  PtrArray_Remove(PtrArray *pArr, int nIndex, int nCount);
void  PtrArray_Clear(PtrArray *pArr);
void *PtrArray_Get(const PtrArray *pArr, int i);
int   PtrArray_GetCount(const PtrArray *pArr);
void  PtrArray_DeleteAll(PtrArray *pArr);
void  PtrArray_DeleteAt(PtrArray *pArr, int nIndex);

#endif /* PUFO_PTRARRAY_H */
