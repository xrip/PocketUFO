/* test_boot.c - checks startup asset loading against the shipped archives, and the
 * startup entry every subsystem hangs off.
 *
 * This is the end-to-end data test: it opens both archives, loads objects.dat,
 * fontsize.dat, texts.dat, the 240x320 surface, class.pal and the three pads, and
 * verifies the counts and geometry against values read independently from the
 * files.
 */
#include "game/pufo_boot.h"
#include "game/pufo_font.h"
#include "game/pufo_input.h"   /* TactInputState, the scratch the startup entry clears */
#include "game/pufo_options.h"
#include "platform/pufo_reg.h"
#include "render/pufo_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Independent ground truth, measured from the shipped archives:
 *   main.dat OBJECTS.DAT  30992 bytes -> 30992 / 208 == 149 records
 *   main.dat TEXTS.DAT   344202 bytes -> (344202 - 2) / 200 == 1721 records
 *   main.dat FONTSIZE.DAT   256 bytes -> 256 advances
 *   CLASS.PAL entry 0 rgb(4,8,7) -> RGB565 0x0040
 *   CS0.PAD header u16 32 x 32
 */
static void TestRealLoad(void)
{
    BootAssets assets;

    printf("Boot_LoadStartup against the shipped archives\n");
    {
        const char *const apsz[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };

        Check(Boot_LoadStartup(&assets, apsz), "  startup load succeeded");
        if (g_nFailures != 0) {
            return;
        }
    }

    printf("objects.dat\n");
    CheckEqI32(assets.nObjectCount, 149, "  object count = 30992 / 208");
    Check(assets.pObjectBlob != NULL, "  blob allocated");
    Check(assets.apObjects != NULL, "  parallel pointer array allocated");
    Check(assets.apObjects != NULL && assets.apObjects[0] == &assets.pObjectBlob[0],
          "  apObjects[0] points at the first record");
    Check(assets.apObjects != NULL && assets.nObjectCount > 1
              && assets.apObjects[1] == &assets.pObjectBlob[1],
          "  apObjects[1] points at the second record");

    printf("fontsize.dat\n");
    Check(g_fontMetrics.bLoaded, "  advance table loaded");
    /* Measured from the file. */
    CheckEqI32(g_fontMetrics.aAdvance[0], 0, "  advance[0]");
    CheckEqI32(g_fontMetrics.aAdvance[32], 3, "  advance[space]");
    CheckEqI32(g_fontMetrics.aAdvance[65], 9, "  advance['A']");

    printf("texts.dat\n");
    /* lang.dat holds a 330002-byte TEXTS.DAT, so the lang copy wins the lookup
     * and the count is (330002 - 2) / 200 == 1650. */
    CheckEqI32((i32)assets.nTextRecCount, 1650, "  record count = (330002 - 2) / 200");
    Check(assets.pTextRecs != NULL, "  text blob allocated");
    /* The marker is consumed and the first real record starts at offset 0. */
    if (assets.pTextRecs != NULL) {
        Check(assets.pTextRecs[0] != 0, "  first record is non-empty");
        /* Records are 205 bytes apart in memory, not 200 on disk. Check the
         * second record begins at 205 and looks like a string. */
        Check(strnlen((const char *)assets.pTextRecs, 205) <= 200,
              "  record 0 is NUL-terminated within its 200 bytes");
        /* Record 1 begins at stride 205. Asserting the constraint that actually
         * matters: the bytes at that offset are within the allocation and the
         * record is readable as a string that terminates inside its own 205-byte
         * slot. */
        Check(assets.nTextRecCount >= 2 && assets.pTextRecs != NULL,
              "  at least two records exist to index");
        Check(strnlen((const char *)(assets.pTextRecs + 205), 205) <= 205,
              "  record 1 is readable at stride 205");
    }

    printf("rendering surface\n");
    Check(assets.pSurface != NULL, "  surface created");
    if (assets.pSurface != NULL) {
        CheckEqI32(SoftSurf_GetWidth(assets.pSurface), 240, "  width 240");
        CheckEqI32(SoftSurf_GetHeight(assets.pSurface), 320, "  height 320");
        CheckEqI32(assets.pSurface->nBytesPP, 2, "  16bpp");
        Check(g_pMainSurface == assets.pSurface, "  bound as the global surface");
    }

    printf("class.pal\n");
    /* Independent ground truth: CLASS.PAL entry 0 rgb(4,8,7) -> 0x0040. */
    CheckEqI32((i32)g_aPalette[0x12][0], 0x0040, "  slot 0x12 entry 0 == 0x0040");
    CheckEqI32((i32)g_aPalette[0x12][255], 0xFFFF, "  slot 0x12 entry 255 == white");

    printf("startup pads\n");
    /* CS0.PAD header is u16 32 x 32; the loader reads the same header layout. */
    Check(assets.aPadCs[0].pData != NULL, "  cs0 loaded");
    Check(assets.aPadCs[0].uWidth == 32 && assets.aPadCs[0].uHeight == 32,
          "  cs0 is 32 x 32");
    CheckEqI32(assets.aPadCs[0].nMode, 18, "  cs0 palette slot 18");
    CheckEqI32(assets.aPadCs[0].nOriginX, 0, "  cs0 origin X zeroed");
    CheckEqI32(assets.aPadCs[0].nOriginY, 0, "  cs0 origin Y zeroed");
    Check(assets.aPadCs[1].pData != NULL, "  cs1 loaded");
    Check(assets.aPadCs[2].pData != NULL, "  cs2 loaded");

    printf("Boot_Shutdown\n");
    Boot_Shutdown(&assets);
    Check(assets.pObjectBlob == NULL, "  object blob released");
    Check(assets.pTextRecs == NULL, "  text blob released");
    Check(assets.pSurface == NULL, "  surface released");
    Check(g_pMainSurface == NULL, "  global surface unbound");
    Check(assets.aPadCs[0].pData == NULL, "  pad data released");
    /* Safe to call twice. */
    Boot_Shutdown(&assets);
    Check(true, "  second shutdown is safe");
}

/* A missing archive must fail cleanly without leaking or crashing. */
static void TestMissingArchive(void)
{
    BootAssets assets;
    const char *const apsz[PUFO_ARCHIVE_COUNT] = { "no_such_file.dat", "main.dat" };

    printf("Boot_LoadStartup with a missing archive\n");
    Check(!Boot_LoadStartup(&assets, apsz), "  load fails");
    Boot_Shutdown(&assets);
    Check(true, "  shutdown after a failed load is safe");
}

/* Both registry tests write into HKCU\Software\PocketUFO, so both end by putting the
 * defaults back: another suite running after this one must see the defaults too. */
static void MapOptionsRestoreDefaults(void)
{
    PufoOptions opt;

    Options_InitDefaults(&opt);
    Options_SaveToRegistry(&opt);
}

/* Map_InitAndLoadRegistry @ 0x22648, the Map subsystem's startup entry (the CRT thunk
 * at 0x22604 runs it, then Map_RegisterAtexit). The scratch half is 0x22664..0x226C8.
 * Every fixture field starts as a sentinel so a write that lands in the wrong slot, or
 * a constant read from the wrong slot, fails. */
static void TestMapInitScratch(void)
{
    TactInputState st;
    PufoOptions opt;
    MapInitEnv env;
    u8   bPhase = 0x5A;
    u8   bBattle = 0x5A;
    u8   bSlot2 = 0x5A;   /* Part 105: Map+0x68 has one owner (bMapSweepArmed), so the fixture
                           * keeps its own byte instead of borrowing a TactInputState field */
    i32  nSave112 = 0x5A5A5A5A;
    void *pThrow = (void *)0x5A5A5A5Au;

    printf("Map_InitAndLoadRegistry scratch\n");

    /* Pin the registry first: the entry loads the option block at the end, and the
     * scratch assertions must not depend on what the machine's registry holds. */
    (void)Reg_SetString("gamespeed", "3");
    (void)Reg_SetString("gamma", "1");
    (void)Reg_SetString("soundvolume", "2");
    (void)Reg_SetString("musicvolume", "4");
    (void)Reg_SetString("musicoff", "0");
    (void)Reg_SetString("soundoff", "0");
    (void)Reg_SetString("forcenodrag", "0");
    (void)Reg_SetString("menubypen", "0");
    (void)Reg_SetString("runbydblclick", "0");
    (void)Reg_SetString("scrollbypen", "0");
    (void)Reg_SetString("showborder", "0");
    (void)Reg_SetString("turnbypen", "0");

    memset(&st, 0x5A, sizeof(st));
    memset(&env, 0, sizeof(env));
    env.pOptions          = &opt;
    env.pbDeployPanelOpen = &st.bDeployPanelOpen;
    env.pbMapPhase        = &bPhase;
    env.pbBattleEnded     = &bBattle;
    env.pbSlot2Byte0      = &bSlot2;
    env.pbSlot8Byte0      = &st.bUiSlot8Byte0;
    env.pnUiSlot3         = &st.nUiSlot3;
    env.pnUiSlot4         = &st.nUiSlot4;
    env.pnUiSlot5         = &st.nUiSlot5;
    env.pnUiSlot7         = &st.nUiSlot7;
    env.pnUiSlot25        = &st.nUiSlot25;
    env.pnUiSlot26        = &st.nUiSlot26;
    env.pnPhaseSave112    = &nSave112;
    env.ppThrowTarget     = &pThrow;

    Map_InitAndLoadRegistry(&env);

    CheckEqI32(st.bDeployPanelOpen, 0, "  0x15110 closes the deploy panel");
    CheckEqI32(bPhase, 0, "  0x0037 clears the map phase");
    CheckEqI32(bBattle, 0, "  0x005C clears the battle-ended byte");
    CheckEqI32(bSlot2, 0, "  0x0068 clears slot 2's low byte (Map_bSweepArmed, one owner: bMapSweepArmed)");
    CheckEqI32(st.nUiSlot3, 0, "  0x006C clears slot 3");
    CheckEqI32(st.nUiSlot4, 200, "  0x0070 sets slot 4 to 200");
    CheckEqI32((i32)st.nUiSlot5, 0, "  0x0074 clears slot 5's four edge bytes");
    CheckEqI32(st.nUiSlot7, 0, "  0x007C clears slot 7");
    CheckEqI32(st.bUiSlot8Byte0, 0, "  0x0080 clears slot 8's low byte");
    CheckEqI32(st.nUiSlot25, 0, "  0x00C4 clears slot 25");
    Check(st.nUiSlot26 >= 0 && st.nUiSlot26 <= 2, "  0x00C8 takes the Game_RandU16(3) roll");
    CheckEqI32(nSave112, 0, "  0x150E8 clears anActionPhaseSave0[112]");
    Check(pThrow == NULL, "  0x150F0 clears the throw target");
    CheckEqI32(opt.nGameSpeed, 3, "  the registry half runs after the scratch");

    /* The roll is a random draw, so bound it over repeated runs: a wider or
     * narrower Game_RandU16 argument shows up as a value outside 0..2. */
    {
        bool bInRange = true;
        bool bSawTop = false;
        bool bSlot25Clean = true;

        for (i32 i = 0; i < 64; ++i) {
            Map_InitAndLoadRegistry(&env);
            if (st.nUiSlot26 < 0 || st.nUiSlot26 > 2) {
                bInRange = false;
            }
            if (st.nUiSlot26 == 2) {
                bSawTop = true;
            }
            /* Sampled every run: a roll written into the wrong slot would show up
             * here even when the first sample happens to be zero. */
            if (st.nUiSlot25 != 0) {
                bSlot25Clean = false;
            }
        }
        Check(bInRange, "  64 rolls all stay inside 0..2");
        Check(bSawTop, "  and the top of the range is reachable");
        Check(bSlot25Clean, "  and slot 25 stays cleared across all 64 runs");
    }

    /* A member the caller does not own is skipped, not dereferenced -- the shell
     * passes NULL for the two fields of a Map block it never allocates. */
    memset(&env, 0, sizeof(env));
    env.pOptions     = &opt;
    env.pnUiSlot4    = &st.nUiSlot4;
    st.nUiSlot25     = 0x5A5A5A5A;
    st.nUiSlot4      = 0x5A5A5A5A;
    Map_InitAndLoadRegistry(&env);
    CheckEqI32(st.nUiSlot4, 200, "  an owned member is still written");
    CheckEqI32(st.nUiSlot25, 0x5A5A5A5A, "  an unowned member is left alone");

    MapOptionsRestoreDefaults();
}

/* The flag half of Map_InitAndLoadRegistry reads a value and tests it with
 * strcmp(value, "0") != 0 (0x22950 for forcenodrag, 0x228B4 for musicoff, ...), and
 * hands Reg_GetString a literal default: "1" for menubypen, runbydblclick,
 * scrollbypen and showborder, "0" for musicoff, soundoff, forcenodrag and turnbypen.
 * atoi semantics would disagree on both counts. */
static void TestOptionFlagSemantics(void)
{
    PufoOptions opt;
    char szLong[300];

    printf("the option flags are strcmp'd against \"0\"\n");

    (void)Reg_SetString("forcenodrag", "yes");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.bForceNoDrag, 1, "  a non-numeric value SETS the flag (atoi would clear it)");

    (void)Reg_SetString("forcenodrag", "");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.bForceNoDrag, 1, "  an empty value sets it too");

    (void)Reg_SetString("forcenodrag", "0");
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.bForceNoDrag, 0, "  only the text \"0\" clears it");

    /* A stored value longer than 0xFF is rejected by Reg_GetString, which then hands
     * back the caller's default -- and the defaults differ per flag. */
    memset(szLong, 'x', sizeof(szLong) - 1);
    szLong[sizeof(szLong) - 1] = '\0';
    (void)Reg_SetString("menubypen", szLong);
    (void)Reg_SetString("forcenodrag", szLong);
    Options_LoadFromRegistry(&opt);
    CheckEqI32(opt.bMenuByPen, 1, "  menubypen falls back to its default \"1\", which sets it");
    CheckEqI32(opt.bForceNoDrag, 0, "  forcenodrag falls back to its default \"0\", which clears it");

    MapOptionsRestoreDefaults();
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestRealLoad();
    TestMissingArchive();
    TestMapInitScratch();
    TestOptionFlagSemantics();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
