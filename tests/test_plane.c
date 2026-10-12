/* test_plane.c - the craft loadout screen's weapon selection.
 * (BasePlaneScreen_SelectWeapon25..30 @ 0x79b20..0x79c74.) */
#include "game/pufo_plane.h"
#include "pufo_test.h"

#include <stdio.h>
#include <string.h>

static i32 g_nSwapCalls;
static i32 g_nSwapResult;

static i32 ApplySwap(void *pUser)
{
    (void)pUser;
    ++g_nSwapCalls;
    return g_nSwapResult;
}

static void TestSelectWeapon(void)
{
    PlaneWeaponPanels p;

    printf("craft loadout weapon selection\n");

    /* Both panels open: both selections move together. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 1;
    p.nPanelBSel = 2;
    p.bPanelAOpen = 1;
    p.bPanelBOpen = 1;
    g_nSwapCalls = 0;
    g_nSwapResult = 41;

    TestEqI32("the handler returns the swap result",
              BasePlaneScreen_SelectWeapon(&p, 25, ApplySwap, NULL), 41);
    TestEqI32("panel A takes the new weapon", p.nPanelASel, 25);
    TestEqI32("panel B takes it too", p.nPanelBSel, 25);
    TestEqI32("the swap runs once", g_nSwapCalls, 1);

    /* Only panel A open: panel B keeps its old selection. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 1;
    p.nPanelBSel = 7;
    p.bPanelAOpen = 1;
    p.bPanelBOpen = 0;
    g_nSwapResult = 0;
    TestEqI32("a closed panel keeps its selection",
              BasePlaneScreen_SelectWeapon(&p, 30, ApplySwap, NULL), 0);
    TestEqI32("the open panel still moves", p.nPanelASel, 30);
    TestEqI32("the closed panel does not", p.nPanelBSel, 7);

    /* Only panel B open. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 9;
    p.nPanelBSel = 3;
    p.bPanelAOpen = 0;
    p.bPanelBOpen = 1;
    BasePlaneScreen_SelectWeapon(&p, 28, ApplySwap, NULL);
    TestEqI32("panel A is gated off", p.nPanelASel, 9);
    TestEqI32("panel B moves", p.nPanelBSel, 28);

    /* Neither open: nothing is written, and the swap still runs. */
    memset(&p, 0, sizeof(p));
    p.nPanelASel = 11;
    p.nPanelBSel = 12;
    g_nSwapCalls = 0;
    BasePlaneScreen_SelectWeapon(&p, 26, ApplySwap, NULL);
    TestEqI32("no gate means no write", p.nPanelASel, 11);
    TestEqI32("the swap runs regardless", g_nSwapCalls, 1);

    /* The selection is a byte: 0x79B34 uses STRBNE. */
    memset(&p, 0, sizeof(p));
    p.bPanelAOpen = 1;
    BasePlaneScreen_SelectWeapon(&p, 0x119, ApplySwap, NULL);
    TestEqI32("the index is stored as a byte", p.nPanelASel, 0x19);

    TestEqI32("NULL panels are tolerated",
              BasePlaneScreen_SelectWeapon(NULL, 25, ApplySwap, NULL), 0);
    g_nSwapResult = 0;
    TestEqI32("and a missing swap hook returns zero",
              BasePlaneScreen_SelectWeapon(&p, 25, NULL, NULL), 0);
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestSelectWeapon();

    return TestSummary("test_plane");
}
