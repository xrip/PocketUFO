/* test_ptrarray.c - the growable pointer list, index semantics pinned.
 *
 * Evidence: PtrArray_ctor @ 0xa0f80, PtrArray_Insert @ 0xa0f94, PtrArray_Add @ 0xa1044,
 *           PtrArray_Remove @ 0xa1054, PtrArray_Clear @ 0xa1120, PtrArray_Get @ 0xa1130,
 *           PtrArray_GetCount @ 0xa1154.
 *
 * The two functions this file exists for are Insert and Remove. Neither was called by any test
 * before, so the whole index contract -- where an out-of-range index lands, what an out-of-range
 * removal count clamps to, and the 0xFFFF "remove everything" code -- was unpinned. The original
 * reallocates and copies on every change (no amortisation), and this file keeps that visible by
 * reading the list back item by item rather than only the count.
 */
#include "util/pufo_ptrarray.h"

#include <stdio.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

/* Items are identified by their address, so the list order can be read back exactly. */
static int g_aItems[8];

static void ArrReset(PtrArray *pArr)
{
    PtrArray_ctor(pArr);
    for (int i = 0; i < 8; ++i) {
        g_aItems[i] = i;
    }
}

static bool ArrEquals(PtrArray *pArr, const int apWant[], int nWant)
{
    if (PtrArray_GetCount(pArr) != nWant) {
        return false;
    }
    for (int i = 0; i < nWant; ++i) {
        if (PtrArray_Get(pArr, i) != &g_aItems[apWant[i]]) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ Insert @ 0xa0f94 */

static void TestInsertIndexContract(void)
{
    static const int apOne[]   = {7};
    static const int apHead[]  = {7, 0, 1, 2};
    static const int apMiddle[] = {0, 1, 7, 2};
    static const int apTail[]  = {0, 1, 2, 7};
    static const int apTwo[]   = {0, 7};
    PtrArray arr;

    printf("PtrArray_Insert @ 0xa0f94 -- where an index lands\n");

    /* An empty array: the index is irrelevant, the item lands at 0. */
    ArrReset(&arr);
    PtrArray_Insert(&arr, 5, &g_aItems[7]);
    CheckEqI32(PtrArray_GetCount(&arr), 1, "an insert into an empty array yields one item");
    Check(ArrEquals(&arr, apOne, 1), "  at index 0");

    /* Index 0 pushes everything right. */
    ArrReset(&arr);
    PtrArray_Add(&arr, &g_aItems[0]);
    PtrArray_Add(&arr, &g_aItems[1]);
    PtrArray_Add(&arr, &g_aItems[2]);
    PtrArray_Insert(&arr, 0, &g_aItems[7]);
    Check(ArrEquals(&arr, apHead, 4), "index 0 goes in front, the rest shift right");

    /* A middle index splits the list. */
    ArrReset(&arr);
    PtrArray_Add(&arr, &g_aItems[0]);
    PtrArray_Add(&arr, &g_aItems[1]);
    PtrArray_Add(&arr, &g_aItems[2]);
    PtrArray_Insert(&arr, 2, &g_aItems[7]);
    Check(ArrEquals(&arr, apMiddle, 4), "index 2 lands between the old items 1 and 2");

    /* 0x9f94's guard: `if (nIndex < 0 || nIndex >= nCount) nIndex = nCount`. A negative index and an
     * index past the end both land at the end -- they are not refused and not clamped to 0. */
    ArrReset(&arr);
    PtrArray_Add(&arr, &g_aItems[0]);
    PtrArray_Add(&arr, &g_aItems[1]);
    PtrArray_Add(&arr, &g_aItems[2]);
    PtrArray_Insert(&arr, -3, &g_aItems[7]);
    Check(ArrEquals(&arr, apTail, 4), "a negative index appends");

    ArrReset(&arr);
    PtrArray_Add(&arr, &g_aItems[0]);
    PtrArray_Add(&arr, &g_aItems[1]);
    PtrArray_Add(&arr, &g_aItems[2]);
    PtrArray_Insert(&arr, 99, &g_aItems[7]);
    Check(ArrEquals(&arr, apTail, 4), "an index past the end appends");

    ArrReset(&arr);
    PtrArray_Add(&arr, &g_aItems[0]);
    PtrArray_Insert(&arr, 1, &g_aItems[7]); /* nIndex == nCount */
    Check(ArrEquals(&arr, apTwo, 2), "an index equal to the count appends too");

    PtrArray_Clear(&arr);
}

/* ------------------------------------------------------------------ Remove @ 0xa1054 */

static void TestRemoveContract(void)
{
    static const int apMiddle[] = {0, 3};
    static const int apTail[]   = {0, 1};
    static const int apClamped[] = {0};
    PtrArray arr;

    printf("PtrArray_Remove @ 0xa1054 -- the count, the clamp and 0xFFFF\n");

    ArrReset(&arr);
    for (int i = 0; i < 4; ++i) {
        PtrArray_Add(&arr, &g_aItems[i]);
    }

    /* A middle removal closes the gap. */
    PtrArray_Remove(&arr, 1, 2);
    CheckEqI32(PtrArray_GetCount(&arr), 2, "removing 2 items at index 1 leaves 2");
    Check(ArrEquals(&arr, apMiddle, 2), "  the tail moved down into the gap");

    /* nCount <= 0 with an explicit index is a no-op (0x1054's early `return`). */
    PtrArray_Remove(&arr, 0, 0);
    Check(ArrEquals(&arr, apMiddle, 2), "nCount 0 removes nothing");
    PtrArray_Remove(&arr, 0, -4);
    Check(ArrEquals(&arr, apMiddle, 2), "a negative nCount removes nothing");

    /* nIndex >= nCount is a no-op too. */
    PtrArray_Remove(&arr, 5, 1);
    Check(ArrEquals(&arr, apMiddle, 2), "an index past the end removes nothing");

    /* The clamp: `if (nIndex + nRemove > nCount) nRemove = nCount - nIndex`. Asking for more than
     * exists takes what exists, from that index to the end. */
    ArrReset(&arr);
    for (int i = 0; i < 4; ++i) {
        PtrArray_Add(&arr, &g_aItems[i]);
    }
    PtrArray_Remove(&arr, 2, 9);
    Check(ArrEquals(&arr, apTail, 2), "an over-long removal clamps to the end of the list");

    ArrReset(&arr);
    for (int i = 0; i < 4; ++i) {
        PtrArray_Add(&arr, &g_aItems[i]);
    }
    PtrArray_Remove(&arr, 0, 9);
    CheckEqI32(PtrArray_GetCount(&arr), 0, "the same clamp from index 0 empties the list");
    Check(arr.apItems == NULL, "  an emptied list stores no array, as 0x1054 does");

    /* Removal of the whole list drops the array pointer (0x1054 stores null when the result is empty),
     * and the count goes to zero. */
    ArrReset(&arr);
    PtrArray_Add(&arr, &g_aItems[0]);
    PtrArray_Add(&arr, &g_aItems[1]);
    PtrArray_Remove(&arr, 0, 2);
    CheckEqI32(PtrArray_GetCount(&arr), 0, "removing every item leaves count 0");
    Check(arr.apItems == NULL, "  and no array at all");

    /* 0xFFFF is the "all" code: nCount is not read at all. */
    ArrReset(&arr);
    for (int i = 0; i < 4; ++i) {
        PtrArray_Add(&arr, &g_aItems[i]);
    }
    PtrArray_Remove(&arr, PUFO_PTRARRAY_INDEX_LAST, 0);
    CheckEqI32(PtrArray_GetCount(&arr), 0, "0xFFFF removes everything even with nCount 0");
    Check(arr.apItems == NULL, "  and leaves no array");

    /* Removing up to the last slot must keep the items before it. This is the bug the earlier test
     * hid: a `>=` here threw the whole array away whenever the removal touched the last index. */
    ArrReset(&arr);
    for (int i = 0; i < 3; ++i) {
        PtrArray_Add(&arr, &g_aItems[i]);
    }
    PtrArray_Remove(&arr, 1, 2);
    Check(ArrEquals(&arr, apClamped, 1), "a removal reaching the last slot keeps the items before it");
    Check(arr.apItems != NULL, "  the array is still there");

    PtrArray_Clear(&arr);
}

int main(void)
{
    TestInsertIndexContract();
    TestRemoveContract();

    if (g_nFailures > 0) {
        printf("test_ptrarray: %d check(s) FAILED\n", g_nFailures);
        return 1;
    }
    printf("test_ptrarray: all checks passed\n");
    return 0;
}
