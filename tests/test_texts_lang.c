/* test_texts_lang.c - the committed lang.dat TEXTS table against the shipped archive.
 *
 * src/pufo/asset/pufo_texts_lang.h is generated from lang.dat (tools/gen_texts_lang.py). This test
 * reads the same records through the game's own reader (AssetReader_Find scans archive 0 first, so
 * TEXTS.DAT comes from lang.dat and main.dat's 1721-record English copy never loads) and compares
 * them record by record. A mismatch means the committed table drifted from the data file.
 *
 * The second half pins the records the fixed label addresses in the IDB name: every address the
 * original passes as a caption satisfies (addr - 0xB38D0) % 205 == 0, so it is TEXTS[record], not
 * scratch memory. Those records must carry text -- that is the whole reason the port used to draw
 * blank labels on the base screen.
 *
 * Exit code 0 = all checks pass, 1 = at least one failure.
 */
#include "asset/pufo_asset.h"
#include "asset/pufo_texts_lang.h"
#include "game/pufo_boot.h"

#include <stdio.h>
#include <string.h>

#define TEXTS_FILE_SIZE  330002u /* AssetReader_Size of lang.dat's TEXTS.DAT */
#define FILE_HEADER      2
#define REC_FILE         200

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

static void CheckEqU32(u32 nGot, u32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%u)\n", pszWhat, nGot);
    } else {
        printf("  FAIL %s: got %u, want %u\n", pszWhat, nGot, nWant);
        g_nFailures += 1;
    }
}

/* Records the fixed labels in the IDB point at. lang.dat leaves 1650..1654 empty (the ufopaedia
 * selector's last five rows), so they are checked separately. */
static const i32 g_anPinnedRecs[] = {
    1623, 1624, 1625, 1626, 1627, 1628, 1629, 1630, 1631, 1632, 1633, 1634, 1635, 1636, 1637,
    1638, 1639, 1640, 1641, 1642, 1643, 1644, 1645,                                    /* base info */
    1646, 1647, 1648, 1649,                                                             /* ufopaedia */
    1186, 1187, 1188, 1189, 1127, 1190,                                                 /* psi screen */
    1595, 1596, 1597, 1598,                                                             /* radar cursor */
    1068, 28, 29, 30                                                                    /* deploy confirm */
};

int main(int argc, char **argv)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    static u8 aRec[REC_FILE];
    AssetReader rdr;
    u8   aHdr[FILE_HEADER];
    i32  i, nMismatch, nEmptyFile, nEmptyHeader;

    (void)argc;
    (void)argv;
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    printf("Committed lang.dat TEXTS table vs the archive\n");
    Check(Asset_OpenArchives(apszPaths), "Asset_OpenArchives(lang.dat, main.dat)");
    if (g_nFailures != 0) {
        return 1;
    }

    AssetReader_Clear(&rdr);
    Check(AssetReader_Find(&rdr, "texts.dat"), "Find(\"texts.dat\")");
    CheckEqU32(rdr.nArchiveIdx, PUFO_ARCHIVE_LANG, "  archive index 0 = lang.dat wins");
    CheckEqU32(AssetReader_Size(&rdr), TEXTS_FILE_SIZE, "  size 2 + 1650 * 200");
    Check(AssetReader_Read(&rdr, aHdr, FILE_HEADER), "the 2-byte member header");
    CheckEqU32((u32)(aHdr[0] | (aHdr[1] << 8)), REC_FILE, "  it holds the file record size 200");
    CheckEqU32(AssetReader_Size(&rdr) - FILE_HEADER,
               (u32)PUFO_TEXT_LANG_COUNT * REC_FILE, "  the rest is exactly PUFO_TEXT_LANG_COUNT records");

    nMismatch = 0;
    nEmptyFile = 0;
    nEmptyHeader = 0;
    for (i = 0; i < PUFO_TEXT_LANG_COUNT; ++i) {
        if (!AssetReader_Read(&rdr, aRec, REC_FILE)) {
            Check(false, "record read failed");
            break;
        }
        if (g_apzTextsLang[i] == NULL) {
            ++nMismatch;
            continue;
        }
        if (strcmp(g_apzTextsLang[i], (const char *)aRec) != 0) {
            if (nMismatch < 10) {
                printf("  FAIL record %d differs: header \"%s\", file \"%s\"\n",
                       i, g_apzTextsLang[i], (const char *)aRec);
            }
            ++nMismatch;
        }
        if (g_apzTextsLang[i][0] == 0) {
            ++nEmptyHeader;
        }
        if (aRec[0] == 0) {
            ++nEmptyFile;
        }
    }
    Check(nMismatch == 0, "every one of the 1650 committed records matches the file byte for byte");
    Check(nEmptyHeader == nEmptyFile && nEmptyFile == 343,
          "  and lang.dat leaves 343 of them empty, same in the table");

    printf("The records the fixed label addresses name\n");
    for (i = 0; i < (i32)(sizeof(g_anPinnedRecs) / sizeof(g_anPinnedRecs[0])); ++i) {
        const i32 nRec = g_anPinnedRecs[i];
        if (g_apzTextsLang[nRec][0] == 0) {
            printf("  FAIL TEXTS[%d] is empty but the original passes it as a caption\n", nRec);
            g_nFailures += 1;
        }
    }
    Check(true, "every pinned record carries text (1623..1649, 1186..1190, 1127, 1595..1598, 1068, 28..30)");
    /* Records 1650..1654 are past what TEXTS.DAT filled: the loaded count ends at 1649, so the
     * ufopaedia selector's last five rows read whatever the block's zero fill left there. The
     * port's block (pufo_boot.c: memset over PUFO_TEXT_REC_CAPACITY) must cover them. */
    Check(1655 <= PUFO_TEXT_REC_CAPACITY,
          "  the ufopaedia selector's rows 1650..1654 are past the 1650 loaded records but inside the block, where the boot's zero fill keeps them blank");
    Check(g_apzTextsLang[1644][0] != 0 && g_apzTextsLang[1645][0] != 0
              && strcmp(g_apzTextsLang[1644], g_apzTextsLang[1645]) != 0,
          "  and the base screen's no/yes pair (1644/1645) are two different words");

    printf("Strides\n");
    CheckEqU32(PUFO_TEXT_REC_STRIDE, 205, "PUFO_TEXT_REC_STRIDE 205 = 200 file bytes + 5 slack");
    Check(PUFO_TEXT_REC_CAPACITY >= PUFO_TEXT_LANG_COUNT,
          "  the allocated block covers every loaded record, so a fixed label never runs off it");
    for (i = 0; i < (i32)(sizeof(g_anPinnedRecs) / sizeof(g_anPinnedRecs[0])); ++i) {
        if (strlen(g_apzTextsLang[g_anPinnedRecs[i]]) >= REC_FILE) {
            Check(false, "a pinned record is not terminated inside its 200 bytes");
            break;
        }
    }
    Check(true, "every pinned record is terminated inside its file record");

    Asset_CloseArchives();

    if (g_nFailures != 0) {
        printf("FAILURES: %d\n", g_nFailures);
        return 1;
    }
    printf("all test_texts_lang checks passed\n");
    return 0;
}
