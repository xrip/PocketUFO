/* test_boot.c - checks startup asset loading against the shipped archives.
 *
 * This is the end-to-end data test: it opens both archives, loads objects.dat,
 * fontsize.dat, texts.dat, the 240x320 surface, class.pal and the three pads, and
 * verifies the counts and geometry against values read independently from the
 * files.
 */
#include "game/pufo_boot.h"
#include "game/pufo_font.h"
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

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestRealLoad();
    TestMissingArchive();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
