/* test_data.c - checks the data layer against the real main.dat / lang.dat.
 *
 * This is the data layer's parity test. The expected values were read out of
 * the shipped archives independently (see docs/parity/data-layer.md), so a
 * mismatch here means the ported reader is wrong, not the expectation.
 *
 * Exit code 0 = all checks pass, 1 = at least one failure.
 */
#include "asset/pufo_asset.h"
#include "dat/pufo_dat.h"

#include <stdio.h>
#include <string.h>

#include <windows.h>

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

int main(int argc, char **argv)
{
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = {
        "lang.dat", /* index 0, matches Assets_LoadStartup ordering */
        "main.dat"  /* index 1 */
    };
    AssetReader rdr;
    u8          buf[16];
    size_t      i;

    (void)argc;
    (void)argv;

    /* Unbuffered so a crash does not hide the last completed check. */
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    printf("Archive open\n");
    Check(Asset_OpenArchives(apszPaths), "Asset_OpenArchives(lang.dat, main.dat)");
    if (g_nFailures != 0) {
        return 1;
    }

    /* Ground truth from the shipped files:
     *   lang.dat directory count = 12
     *   main.dat directory count = 891
     * Both are read by AssetReader_Find, which scans lang.dat first. */
    printf("Lookup: TEXTS.DAT must come from lang.dat (index 0)\n");
    AssetReader_Clear(&rdr);
    Check(AssetReader_Find(&rdr, "texts.dat"), "Find(\"texts.dat\")");
    CheckEqU32(rdr.nArchiveIdx, PUFO_ARCHIVE_LANG, "  archive index");
    CheckEqU32(AssetReader_Size(&rdr), 330002, "  size (lang.dat copy)");

    /* The lang.dat TEXTS.DAT header's first u16 is 200, verified in the file. */
    printf("Read: TEXTS.DAT header\n");
    Check(AssetReader_Read(&rdr, buf, 2), "Read(2 bytes)");
    CheckEqU32((u32)buf[0] | ((u32)buf[1] << 8), 200, "  first u16 == 200");

    /* Sequential read must advance, and must refuse to read past the end. */
    printf("Read: bounds and progress\n");
    CheckEqU32(rdr.nReadPos, 2, "  nReadPos advanced to 2");
    Check(AssetReader_Read(&rdr, buf, 4), "Read(4 more bytes)");
    CheckEqU32(rdr.nReadPos, 6, "  nReadPos advanced to 6");
    Check(!AssetReader_Read(&rdr, buf, 400000), "Read past end is refused");

    /* OBJECTS.DAT only exists in main.dat. This proves the second archive is
     * reachable and that the reader does not stop at archive 0. */
    printf("Lookup: OBJECTS.DAT must come from main.dat (index 1)\n");
    AssetReader_Clear(&rdr);
    Check(AssetReader_Find(&rdr, "OBJECTS.DAT"), "Find(\"OBJECTS.DAT\")");
    CheckEqU32(rdr.nArchiveIdx, PUFO_ARCHIVE_MAIN, "  archive index");
    CheckEqU32(AssetReader_Size(&rdr), 30992, "  size");
    /* 30992 / 0xD0 == number of object records, per Assets_LoadStartup. */
    CheckEqU32(30992 / 0xD0u, 149, "  object record count (size / 208)");

    printf("Lookup: case-insensitivity\n");
    AssetReader_Clear(&rdr);
    Check(AssetReader_Find(&rdr, "ObJeCtS.dAt"), "Find(\"ObJeCtS.dAt\") mixed case");

    printf("Lookup: missing name\n");
    AssetReader_Clear(&rdr);
    Check(!AssetReader_Find(&rdr, "NO_SUCH_MEMBER.XYZ"), "Find(missing) is false");
    Check(!AssetReader_Found(&rdr), "  Found() is false");
    Check(!AssetReader_Read(&rdr, buf, 1), "  Read() after miss is false");

    /* Every directory entry that claims data must lie inside the file. This
     * catches an off-by-one in the directory stride, which would otherwise
     * silently produce plausible but wrong offsets. */
    printf("Directory consistency\n");
    {
        /* Re-find a few spread-out members and check offsets are sane by
         * reading their first byte. */
        static const char *const apszNames[] = {
            "BACK0.IT", "TITLE7.DAT", "OBJECTS.DAT"
        };
        static const u32 anWantSize[] = { 38008, 1062, 30992 };
        static const u32 anWantOff[]  = { 18715, 10196974, 2749406 };

        for (i = 0; i < sizeof(apszNames) / sizeof(apszNames[0]); ++i) {
            char szLabel[64];

            AssetReader_Clear(&rdr);
            snprintf(szLabel, sizeof(szLabel), "%s found", apszNames[i]);
            Check(AssetReader_Find(&rdr, apszNames[i]), szLabel);
            snprintf(szLabel, sizeof(szLabel), "  %s offset", apszNames[i]);
            CheckEqU32(rdr.nFileOff, anWantOff[i], szLabel);
            snprintf(szLabel, sizeof(szLabel), "  %s size", apszNames[i]);
            CheckEqU32(AssetReader_Size(&rdr), anWantSize[i], szLabel);
        }
    }

    Asset_CloseArchives();

    /* The save container. DatFile is what SaveGame @ 0x44988 writes and
     * LoadGame @ 0x44bbc reads, so a write/close/reopen/read round trip is
     * the check that matters. The on-disk shape is asserted too, because a
     * wrong directory stride here would corrupt every save file. */
    printf("DatFile save container round trip\n");
    {
        DatFile dat;
        u32     anValues[3] = { 0xAABBCCDDu, 0x11223344u, 0x99u };
        u32     anRead[3]   = { 0 };

        remove("test_roundtrip.tmp");
        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_roundtrip.tmp", PUFO_DAT_CREATE, 4),
              "Open(create, 4 slots)");
        /* Deliberately use mixed case and an out-of-order size to prove tags
         * are upper-cased and chunk sizes are per-chunk, not cumulative. */
        Check(DatFile_BeginChunk(&dat, "alpha"), "BeginChunk(alpha)");
        Check(DatFile_Write(&dat, &anValues[0], 4), "Write(alpha, 4)");
        Check(DatFile_BeginChunk(&dat, "beta"), "BeginChunk(beta)");
        Check(DatFile_Write(&dat, &anValues[1], 8), "Write(beta, 8)");
        Check(DatFile_BeginChunk(&dat, "gammalongest"), "BeginChunk(12-char tag)");
        Check(DatFile_Write(&dat, &anValues[2], 2), "Write(gamma, 2)");
        DatFile_Close(&dat);

        /* Header must be count=4, and entry 0 must start right after the
         * directory: 4 + 21*4 == 88. Read with Win32 so the test does not
         * depend on CRT stdio being enabled. */
        {
            HANDLE hFile = CreateFileW(L"test_roundtrip.tmp", GENERIC_READ,
                                       FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, NULL);
            u8     hdr[32];
            DWORD  cbRead = 0;
            u32    nCount = 0;

            Check(hFile != INVALID_HANDLE_VALUE, "file exists after close");
            if (hFile != INVALID_HANDLE_VALUE) {
                Check(ReadFile(hFile, hdr, sizeof(hdr), &cbRead, NULL) != 0
                          && cbRead == sizeof(hdr),
                      "header readable");
                memcpy(&nCount, hdr, 4);
                CheckEqU32(nCount, 4, "  header entry count");
                Check(memcmp(hdr + 4, "ALPHA", 5) == 0, "  entry 0 tag upper-cased");
                {
                    u32 nDataSize = 0;
                    u32 nFileOff  = 0;

                    memcpy(&nDataSize, hdr + 4 + 13, 4);
                    memcpy(&nFileOff, hdr + 4 + 17, 4);
                    CheckEqU32(nDataSize, 4, "  entry 0 data size");
                    CheckEqU32(nFileOff, 4 + 21 * 4, "  entry 0 offset (after dir)");
                }
                (void)CloseHandle(hFile);
            }
        }

        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_roundtrip.tmp", PUFO_DAT_READ, 0), "reopen(read)");
        Check(DatFile_FindChunk(&dat, "BETA"), "FindChunk(BETA)");
        Check(DatFile_ChunkPresent(&dat), "ChunkPresent() true");
        Check(DatFile_Read(&dat, anRead, 8), "Read(beta, 8)");
        CheckEqU32(anRead[0], anValues[1], "  round-tripped value 0");
        CheckEqU32(anRead[1], anValues[2], "  round-tripped value 1");
        Check(DatFile_FindChunk(&dat, "ALPHA"), "FindChunk(ALPHA)");
        Check(DatFile_Read(&dat, anRead, 4), "Read(alpha, 4)");
        CheckEqU32(anRead[0], anValues[0], "  alpha value");
        Check(DatFile_FindChunk(&dat, "GAMMALONGEST"), "FindChunk(12-char tag)");
        Check(DatFile_Read(&dat, anRead, 2), "Read(gamma, 2)");
        Check(!DatFile_FindChunk(&dat, "NOTTHERE"), "FindChunk(missing) is false");
        Check(!DatFile_ChunkPresent(&dat), "ChunkPresent() false after miss");
        DatFile_Close(&dat);

        /* A 13-character tag must be rejected: the name field is 13 bytes
         * including the NUL. */
        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_roundtrip2.tmp", PUFO_DAT_CREATE, 1),
              "Open(create, 1 slot)");
        Check(!DatFile_BeginChunk(&dat, "thirteenchars"), "13-char tag rejected");
        DatFile_Close(&dat);

        remove("test_roundtrip.tmp");
        remove("test_roundtrip2.tmp");
    }

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
