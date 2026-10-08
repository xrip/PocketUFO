/* test_modal.c - the modal yes/no dialogue state
 * (UiModal_Begin @ 0x26430, UiModal_End @ 0x2648c). */
#include <stdio.h>
#include <string.h>

#include "ui/pufo_modal.h"

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

static void Yes(void) {}
static void No(void) {}

static void TestBeginEnd(void)
{
    UiModalState modal;

    printf("UiModal_Begin / UiModal_End\n");
    memset(&modal, 0xAA, sizeof(modal));
    UiModal_Begin(&modal, 140, 24, 4, 5, Yes, (void *)"NO", No);

    Check(modal.bOpen, "  open flag set");
    CheckEqI32(modal.nField48, 0, "  pCtx+48 cleared");
    CheckEqI32(modal.nActionPhaseSave112, 0, "  anActionPhaseSave0[112] cleared");
    CheckEqI32(modal.anRect12_15[0], 140, "  slot 12 stored");
    CheckEqI32(modal.anRect12_15[1], 24, "  slot 13 stored");
    CheckEqI32(modal.anRect12_15[2], 4, "  slot 14 stored");
    CheckEqI32(modal.anRect12_15[3], 5, "  slot 15 stored");
    Check(modal.pfn16 == Yes, "  slot 16 callback stored");
    Check(modal.p17 != NULL, "  slot 17 argument stored");
    Check(modal.pfn18 == No, "  slot 18 callback stored");

    UiModal_End(&modal);
    Check(!modal.bOpen, "  end clears only the open flag");
    CheckEqI32(modal.anRect12_15[0], 140, "  stored fields survive end");
    Check(modal.pfn16 == Yes, "  callbacks survive end");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestBeginEnd();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
