/* pufo_ptrarray.c - the game's growable pointer list.
 *
 * Evidence: see pufo_ptrarray.h for the address list.
 *
 * Note on a decompiler artifact: IDA renders the tail copies as
 *   4 * (0x3FFFFFFF * nIndex + nCount)
 * which is the 32-bit form of 4 * (nCount - nIndex). The source is the plain
 * difference; the mask form only appears because the original computed
 * `4*nCount - 4*nIndex` in 32-bit arithmetic. Reproduced as the difference.
 */
#include "util/pufo_ptrarray.h"

#include <stdlib.h>
#include <string.h>

void PtrArray_ctor(PtrArray *pArr)
{
    pArr->nCount  = 0;
    pArr->apItems = NULL;
}

void PtrArray_Insert(PtrArray *pArr, int nIndex, void *pItem)
{
    void **apNew;
    int    nCount = pArr->nCount;

    apNew = malloc(sizeof(void *) * ((size_t)nCount + 1u));
    if (apNew == NULL) {
        return;
    }

    if (nIndex < 0 || nIndex >= nCount) {
        nIndex = nCount;
    }

    if (nIndex > 0) {
        memmove(apNew, pArr->apItems, sizeof(void *) * (size_t)nIndex);
    }
    apNew[nIndex] = pItem;
    if (nIndex < nCount) {
        memmove(&apNew[nIndex + 1], &pArr->apItems[nIndex],
                sizeof(void *) * (size_t)(nCount - nIndex));
    }

    free(pArr->apItems);
    pArr->apItems = apNew;
    pArr->nCount  = nCount + 1;
}

void PtrArray_Add(PtrArray *pArr, void *pItem)
{
    PtrArray_Insert(pArr, PUFO_PTRARRAY_INDEX_LAST, pItem);
}

void PtrArray_Remove(PtrArray *pArr, int nIndex, int nCount)
{
    int    nRemove = nCount;
    int    nTotal  = pArr->nCount;
    void **apNew;

    if (nIndex == PUFO_PTRARRAY_INDEX_LAST) {
        nIndex  = 0;
        nRemove = nTotal;
    } else {
        if (nCount <= 0) {
            return;
        }
        if (nIndex >= nTotal) {
            return;
        }
    }

    if (nIndex + nRemove > nTotal) {
        nRemove = nTotal - nIndex;
    }

    if (nIndex + nRemove >= nTotal) {
        apNew = NULL;
    } else {
        apNew = malloc(sizeof(void *) * ((size_t)nTotal + 1u));
        if (apNew == NULL) {
            return;
        }
        if (nIndex > 0) {
            memmove(apNew, pArr->apItems, sizeof(void *) * (size_t)nIndex);
        }
        memmove(&apNew[nIndex], &pArr->apItems[nIndex + nRemove],
                sizeof(void *) * (size_t)(nTotal - (nIndex + nRemove)));
    }

    free(pArr->apItems);
    pArr->apItems = apNew;
    pArr->nCount  = nTotal - nRemove;
}

void PtrArray_Clear(PtrArray *pArr)
{
    PtrArray_Remove(pArr, PUFO_PTRARRAY_INDEX_LAST, 0);
}

void *PtrArray_Get(const PtrArray *pArr, int i)
{
    if (i < 0 || i >= pArr->nCount) {
        return NULL;
    }
    return pArr->apItems[i];
}

int PtrArray_GetCount(const PtrArray *pArr)
{
    return pArr->nCount;
}

void PtrArray_DeleteAll(PtrArray *pArr)
{
    int i;

    for (i = 0; i < PtrArray_GetCount(pArr); ++i) {
        free(PtrArray_Get(pArr, i));
    }
    PtrArray_Clear(pArr);
}

void PtrArray_DeleteAt(PtrArray *pArr, int nIndex)
{
    free(PtrArray_Get(pArr, nIndex));
    PtrArray_Remove(pArr, nIndex, 1);
}
