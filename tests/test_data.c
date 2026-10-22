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
     *
     * AssetReader_Find @ 0x9f7e4 copies the requested name into the reader's own 15-byte buffer
     * (the byte loop at 0x9F7F4..0x9F804) and upper-cases that copy at 0x9F80C, then scans archive
     * 0 fully before archive 1: the archive loop counts 0..1 (`++v5 >= 2` at 0x9F864) and each
     * directory is walked with the 21-byte stride at 0x9F850 against nAssetDirCount[0x1472C0]. A hit
     * stores bFound = 1 at 0x9F87C, then memmoves the directory entry's size at 0x9F8A8 and its file
     * offset at 0x9F8C0, clears nReadPos at 0x9F8C4 and stores the archive index at 0x9F8C8.
     * AssetReader_Size @ 0x9f964 is one load of that size field (LDR at 0x9F968) and
     * AssetReader_Found @ 0x9f95c is one load of bFound (LDRB at 0x9F960). */
    printf("Lookup: TEXTS.DAT must come from lang.dat (index 0)\n");
    AssetReader_Clear(&rdr);
    Check(AssetReader_Find(&rdr, "texts.dat"), "Find(\"texts.dat\")");
    CheckEqU32(rdr.nArchiveIdx, PUFO_ARCHIVE_LANG, "  archive index (0x9f8c8 stores it)");
    CheckEqU32(AssetReader_Size(&rdr), 330002, "  size (lang.dat copy)");
    Check(AssetReader_Found(&rdr), "  Found() is true after a hit (0x9f95c reads bFound, not its inverse)");

    /* The lang.dat TEXTS.DAT header's first u16 is 200, verified in the file. */
    printf("Read: TEXTS.DAT header\n");
    Check(AssetReader_Read(&rdr, buf, 2), "Read(2 bytes)");
    CheckEqU32((u32)buf[0] | ((u32)buf[1] << 8), 200, "  first u16 == 200");

    /* Sequential read must advance, and must refuse to read past the end.
     * AssetReader_Read @ 0x9f8d0 tests bFound first (LDRB at 0x9F8DC, CMP at 0x9F8E8) and then compares
     * the signed sum `nReadPos + cb <= nDataSize` (ADD at 0x9F8F8, BGT exit at 0x9F900). */
    printf("Read: bounds and progress\n");
    CheckEqU32(rdr.nReadPos, 2, "  nReadPos advanced to 2");
    Check(AssetReader_Read(&rdr, buf, 4), "Read(4 more bytes)");
    CheckEqU32(rdr.nReadPos, 6, "  nReadPos advanced to 6");

    /* What a refused read must leave alone. The original's two failure branches (BEQ at 0x9F8EC,
     * BGT at 0x9F900) jump straight to the epilogue at 0x9F950 without touching the file handle, so no
     * SetFilePointer @ 0x9F920, no ReadFile @ 0x9F940 and no `nReadPos += cb` @ 0x9F94C run.
     * The return value is NOT a success flag there: R0 is never normalised, so a refused over-read
     * returns nReadPos (0x9F8F0 `LDR R0,[R4,#0x18]`) -- 6 here, i.e. truthy -- and a not-found reader
     * returns its own pointer. Of the 23 BL sites none reads R0 before overwriting it; the only
     * site where the value survives is Map_LoadMa1 @ 0x54C2C, which returns it as its own BOOL, and
     * all nine of Map_LoadMa1's call sites in Font_LoadTactSprites drop it too. The port returns
     * false in both cases: a cleaner return that no original caller can observe. */
    printf("Read: what a refused read leaves alone\n");
    memset(buf, 0x5A, sizeof(buf));
    Check(!AssetReader_Read(&rdr, buf, 400000), "Read past end is refused");
    CheckEqU32(rdr.nReadPos, 6, "  the refused read does not advance nReadPos");
    Check(buf[0] == 0x5A && buf[15] == 0x5A, "  the refused read writes nothing into the caller's buffer");

    /* AssetReader_Clear @ 0x9f7d8 is one store: STRB of 0 into bFound at 0x9F7DC, nothing else. That
     * byte is what 0x9f8d0 tests first (LDRB at 0x9F8DC, CMP at 0x9F8E8) and what 0x9f95c returns, and
     * 0x9f7e4 writes it at 0x9F87C on a hit and at 0x9F868 on a miss. The original also has
     * AssetReader_ClearGlobal @ 0x83874, 12 bytes, which is `AssetReader_Clear(&g_rdr)` on the single
     * reader it keeps at 0x187028 -- the port hands each caller its own reader, so that wrapper is
     * cited here at the shared routine instead of having a function of its own. */
    printf("Clear after a hit\n");
    AssetReader_Clear(&rdr);
    Check(!AssetReader_Found(&rdr), "  Clear makes Found() false again");
    Check(!AssetReader_Read(&rdr, buf, 1), "  a cleared reader refuses a read");

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

    /* A miss must be reported, not half-applied: 0x9f7e4's exit path is `++v5 >= 2` at 0x9F864 storing
     * bFound = 0 at 0x9F868, and 0x9f8d0 refuses on that byte before any file access. */
    printf("Lookup: missing name\n");
    AssetReader_Clear(&rdr);
    Check(!AssetReader_Find(&rdr, "NO_SUCH_MEMBER.XYZ"), "Find(missing) is false");
    Check(!AssetReader_Found(&rdr), "  Found() is false (0x9f95c: LDRB of bFound at 0x9F960)");
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
            /* AssetReader_Size @ 0x9f964 loads the entry's nDataSize (0x9F8A8 memmove put it there), not
             * the nFileOff its neighbour loads at 0x9F8C0. These three members have different sizes and
             * offsets, so the two fields cannot be swapped here without being noticed. */
            snprintf(szLabel, sizeof(szLabel), "  %s size is not its offset", apszNames[i]);
            Check(AssetReader_Size(&rdr) != rdr.nFileOff, szLabel);
        }
    }

    /* The end of a small member makes the boundary observable without a huge buffer. main.dat directory
     * entry 41 is "0.CTS", 24 bytes at 2730656, with the next member starting right after it at 2730680,
     * so reading past the end pulls that neighbour's bytes in rather than faulting. 20 + 4 lands exactly
     * on the bound, which the `<=` at 0x9F900 allows; one more byte must be refused. If the bound were
     * gone the read would succeed, advance nReadPos and write the neighbour's first byte. */
    printf("Read: the exact end of a 24-byte member\n");
    {
        u8 aSmall[24];

        AssetReader_Clear(&rdr);
        Check(AssetReader_Find(&rdr, "0.CTS"), "Find(0.CTS)");
        CheckEqU32(AssetReader_Size(&rdr), 24, "  24 bytes (main.dat directory)");
        memset(aSmall, 0, sizeof(aSmall));
        Check(AssetReader_Read(&rdr, aSmall, 20), "  Read(20)");
        Check(AssetReader_Read(&rdr, aSmall + 20, 4), "  Read(4) exactly on the member end (0x9F900 allows `==`)");
        CheckEqU32(rdr.nReadPos, 24, "  position is at the member end");
        memset(aSmall + 20, 0x5A, 4);
        Check(!AssetReader_Read(&rdr, aSmall + 20, 1), "  one byte past the member end is refused");
        CheckEqU32(rdr.nReadPos, 24, "  the position did not move");
        Check(aSmall[20] == 0x5A && aSmall[23] == 0x5A, "  nothing was written into the caller's buffer");
    }

    Asset_CloseArchives();

    /* The save container. DatFile is what SaveGame @ 0x44988 writes and
     * LoadGame @ 0x44bbc reads, so a write/close/reopen/read round trip is
     * the check that matters. The on-disk shape is asserted too, because a
     * wrong directory stride here would corrupt every save file.
     *
     * DatFile_BeginChunk @ 0x9f348 is the directory writer: FlushWrite first at 0x9F358, then the
     * mode gate (`!= 2 && != 3` at 0x9F36C), the slot-full gate (`nCurEntry == nEntryCount` at 0x9F37C),
     * the tag copy into the struct's own szTag at 0x9F398..0x9F3A8 followed by strupr at 0x9F3B0 and
     * only then the length gate `v5 == 0 || v5 > 12` at 0x9F3C0. It stores nDataSize = 0 at 0x9F414,
     * nFileOff = GetFileSize at 0x9F444, writes the whole 0x15-byte entry at file offset 21*nCurEntry+4
     * (SetFilePointer 0x9F460, WriteFile 0x9F484), and finishes with nBufUsed = 0, bDirty = 0,
     * nCurEntry += 1 and nChunkSize = 0 at 0x9F490..0x9F4A0.
     * DatFile_Write @ 0x9f4b4 guards in order: `nCurEntry == 0` at 0x9F4D0, `(nSize & 0x80000000)` at
     * 0x9F4D8, mode at 0x9F4EC, then buffers in a loop and sets bDirty = 1 at 0x9F594.
     * DatFile_FlushWrite @ 0x9f234 only runs when (mode == 2 || mode == 3) && bDirty at 0x9F25C; it
     * appends the pending buffer at 0x9F280/0x9F294 and patches the previous slot at
     * 21*nCurEntry - 8 in memory (0x9F304) and 21*nCurEntry - 4 on disk (0x9F320/0x9F338).
     * DatFile_Close @ 0x9f174 flushes at 0x9F18C before CloseHandle at 0x9F194 and the directory free
     * at 0x9F19C, so the last chunk's pending bytes are on disk before anything is readable. */
    printf("DatFile save container round trip\n");
    {
        DatFile dat;
        u32     anValues[3] = { 0xAABBCCDDu, 0x11223344u, 0x99u };
        u32     anRead[3]   = { 0 };

        remove("test_roundtrip.tmp");
        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_roundtrip.tmp", PUFO_DAT_CREATE, 4),
              "Open(create, 4 slots)");
        /* A write before any BeginChunk is refused: 0x9F4D0 `CMP R3,#0 / BEQ` on nCurEntry. */
        Check(!DatFile_Write(&dat, &anValues[0], 4), "Write before BeginChunk is refused (0x9f4b4 nCurEntry gate)");
        /* Deliberately use mixed case and an out-of-order size to prove tags
         * are upper-cased (0x9F3B0) and chunk sizes are per-chunk, not cumulative. */
        Check(DatFile_BeginChunk(&dat, "alpha"), "BeginChunk(alpha)");
        Check(DatFile_Write(&dat, &anValues[0], 4), "Write(alpha, 4)");
        /* The empty tag is refused by the same gate as the 13-character one: `v5 == 0` at 0x9F3C0. */
        Check(!DatFile_BeginChunk(&dat, ""), "BeginChunk(\"\") is refused (0x9f348 length gate)");
        Check(DatFile_BeginChunk(&dat, "beta"), "BeginChunk(beta)");
        /* BeginChunk ran FlushWrite first (0x9F358), and FlushWrite patches the slot the *previous*
         * BeginChunk reserved in two separate writes: the struct at 21*nCurEntry - 8 (0x9F304) and the
         * file at 21*nCurEntry - 4 (0x9F320/0x9F338). With nCurEntry == 1 here both land in entry 0, at
         * +13 (nDataSize) and at +17 on disk -- so the in-memory half has its own check, and it has to
         * name the field it must not touch as well. */
        CheckEqU32(dat.pDir[0].nDataSize, 4, "  in-memory entry 0 size patched (0x9F304)");
        CheckEqU32(dat.pDir[0].nFileOff, 88, "  that store left entry 0's offset alone");
        Check(DatFile_Write(&dat, &anValues[1], 8), "Write(beta, 8)");
        Check(DatFile_BeginChunk(&dat, "gammalongest"), "BeginChunk(12-char tag)");
        Check(DatFile_Write(&dat, &anValues[2], 2), "Write(gamma, 2)");
        DatFile_Close(&dat);

        /* Header must be count=4, and entry 0 must start right after the
         * directory: 4 + 21*4 == 88. Read with Win32 so the test does not
         * depend on CRT stdio being enabled.
         *
         * Sizes are read from the DISK copy, not from the struct: FlushWrite patches the same field
         * twice, in memory at 21*nCurEntry - 8 (0x9F304) and on disk at 21*nCurEntry - 4
         * (SetFilePointer 0x9F320, WriteFile 0x9F338), and only the disk half can drift. Entry 1 is at
         * 4 + 21 = 25, entry 2 at 4 + 42 = 46, so 67 bytes covers the names and both u32 fields. */
        {
            HANDLE hFile = CreateFileW(L"test_roundtrip.tmp", GENERIC_READ,
                                       FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, NULL);
            u8     hdr[80];
            DWORD  cbRead = 0;
            u32    nCount = 0;

            Check(hFile != INVALID_HANDLE_VALUE, "file exists after close");
            if (hFile != INVALID_HANDLE_VALUE) {
                Check(ReadFile(hFile, hdr, sizeof(hdr), &cbRead, NULL) != 0
                          && cbRead == sizeof(hdr),
                      "header readable");
                memcpy(&nCount, hdr, 4);
                CheckEqU32(nCount, 4, "  header entry count");
                Check(memcmp(hdr + 4, "ALPHA", 5) == 0, "  entry 0 tag upper-cased (0x9f348 strupr at 0x9F3B0)");
                {
                    static const u32 anDiskSize[3] = { 4, 8, 2 };
                    static const u32 anDiskOff[3]  = { 88, 92, 100 };
                    static const char *const apszTag[3] = { "ALPHA", "BETA", "GAMMALONGEST" };
                    u32 k;

                    for (k = 0; k < 3; ++k) {
                        char     szLabel[64];
                        u32      nDataSize = 0;
                        u32      nFileOff  = 0;
                        const u8 *pEntry   = hdr + 4 + 21 * k;

                        memcpy(&nDataSize, pEntry + 13, 4);
                        memcpy(&nFileOff, pEntry + 17, 4);
                        snprintf(szLabel, sizeof(szLabel), "  disk entry %u tag %s", k, apszTag[k]);
                        Check(memcmp(pEntry, apszTag[k], strlen(apszTag[k]) + 1) == 0, szLabel);
                        snprintf(szLabel, sizeof(szLabel), "  disk entry %u data size (0x9f234 patches 21*n-8)", k);
                        CheckEqU32(nDataSize, anDiskSize[k], szLabel);
                        snprintf(szLabel, sizeof(szLabel), "  disk entry %u offset (0x9f348 GetFileSize)", k);
                        CheckEqU32(nFileOff, anDiskOff[k], szLabel);
                    }
                }
                (void)CloseHandle(hFile);
            }
        }

        /* DatFile_FindChunk @ 0x9f5ac requires mode == 1 (0x9F5C0), copies the tag into the struct and
         * upper-cases it (0x9F5CC..0x9F5E4), scans 0..nCurEntry-1 with the 21-byte stride at 0x9F61C,
         * and on a hit stores nChunkSize at 0x9F664, nChunkFileOff at 0x9F67C, nChunkReadPos = 0 at
         * 0x9F688, bChunkOpen = 1 at 0x9F68C and -- the part a reader depends on -- nBufUsed = nBufCap
         * at 0x9F690, so the first DatFile_Read refills the buffer from the chunk instead of reusing
         * the bytes the previous chunk left in it. On a miss it clears bChunkFound (0x9F628) and
         * bChunkOpen (0x9F62C) and leaves the size and offset alone.
         * DatFile_Read @ 0x9f698 gates on mode == 1 (0x9F6B0), bChunkOpen (LDRB [R4,#0x30] 0x9F6B8),
         * bChunkFound (LDRB [R4,#0x14] 0x9F6C4) and the signed bound nChunkReadPos + nSize <=
         * nChunkSize (0x9F6D0..0x9F6E0): an 8-byte read of an 8-byte chunk sits exactly on that bound
         * and must succeed. The four refusal branches (0x9F6B4, 0x9F6C0, 0x9F6CC, 0x9F6E0) go straight
         * to the epilogue at 0x9F7A0, so a refusal neither moves the file handle nor advances
         * nChunkReadPos. As in 0x9f8d0 the return register is never normalised -- MOV R4,R0 at 0x9F6A0
         * is the only write to R0 on those paths -- so the original reports a refusal as a non-zero
         * value; of its 351 BL sites none reads R0 before overwriting it.
         * DatFile_ChunkPresent @ 0x9f7a8 tests three bytes in order: mode == 1 at 0x9F7B0,
         * bChunkFound at 0x9F7BC, bChunkOpen at 0x9F7C0 with the 0 result at 0x9F7D0. */
        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_roundtrip.tmp", PUFO_DAT_READ, 0), "reopen(read)");
        Check(DatFile_FindChunk(&dat, "BETA"), "FindChunk(BETA)");
        Check(DatFile_ChunkPresent(&dat), "ChunkPresent() true");
        Check(DatFile_Read(&dat, anRead, 8), "Read(beta, 8) exactly fills the 8-byte chunk");
        CheckEqU32(anRead[0], anValues[1], "  round-tripped value 0");
        CheckEqU32(anRead[1], anValues[2], "  round-tripped value 1");
        CheckEqU32((u32)dat.nChunkReadPos, 8, "  the chunk position advanced to the chunk end (0x9F794)");
        Check(!DatFile_Read(&dat, anRead, 1), "  one byte past the chunk end is refused (0x9F6E0)");
        CheckEqU32((u32)dat.nChunkReadPos, 8, "  the refused read left the chunk position alone");
        Check(DatFile_FindChunk(&dat, "ALPHA"), "FindChunk(ALPHA)");
        Check(DatFile_Read(&dat, anRead, 4), "Read(alpha, 4)");
        CheckEqU32(anRead[0], anValues[0], "  alpha value");
        Check(DatFile_FindChunk(&dat, "GAMMALONGEST"), "FindChunk(12-char tag)");
        Check(DatFile_Read(&dat, anRead, 2), "Read(gamma, 2)");
        Check(!DatFile_FindChunk(&dat, "NOTTHERE"), "FindChunk(missing) is false");
        Check(!DatFile_ChunkPresent(&dat), "ChunkPresent() false after miss");
        DatFile_Close(&dat);
        DatFile_Close(&dat); /* 0x9f174 skips the whole body when nMode is already 0 (0x9F188) */

        /* A 13-character tag must be rejected: the name field is 13 bytes
         * including the NUL. The original's gate is `strlen == 0 || strlen > 12` at 0x9F3C0 -- note it
         * runs *after* the copy and strupr at 0x9F398..0x9F3B0, and that copy is unbounded: 12 characters
         * fill szTag (13 bytes at +0x15) exactly, 13 reach the pad byte at +0x22, and 15 or more reach
         * nChunkFileOff at +0x24 -- all of it written before the call refuses. The port checks the length
         * first. The
         * original also has no buffer guard here at all: DatFile_Write with nBufCap == 0 spins forever
         * at 0x9F510..0x9F56C (the refill writes nothing, nChunk stays 0, nLeft never drops); the port
         * refuses, checked below. */
        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_roundtrip2.tmp", PUFO_DAT_CREATE, 1),
              "Open(create, 1 slot)");
        Check(!DatFile_BeginChunk(&dat, "thirteenchars"), "13-char tag rejected (0x9F3C0)");
        Check(!DatFile_ChunkPresent(&dat), "ChunkPresent() is false outside READ mode (0x9F7B0)");
        DatFile_Close(&dat);

        remove("test_roundtrip.tmp");
        remove("test_roundtrip2.tmp");
    }

    /* DatFile_ctor @ 0x9eecc writes six fields and nothing else: nMode = 0 at 0x9EED8, pDir = 0 at
     * 0x9EEDC, bChunkOpen = 0 at 0x9EEE0, pWriteBuf = 0 at 0x9EEE4, nBufCap = 150000 at 0x9EEE8 and
     * nBufUsed = 0 at 0x9EEEC. That 150000 is the capacity every later write is chunked against
     * (0x9F510 compares with it, 0x9F560 sizes each copy from it) and the size DatFile_Open allocates
     * the write buffer with. The port memsets the whole struct first, which also zeroes the eight
     * fields the original leaves alone (nEntryCount, nCurEntry, nChunkFileOff, nChunkSize,
     * nChunkReadPos, bChunkFound, szTag, bDirty) -- a stated deviation, so only the six stores the
     * original actually makes are checked here, and 150000 is written as a literal, not as the macro
     * that defines it. */
    printf("DatFile_ctor's six stores\n");
    {
        DatFile fresh;

        memset(&fresh, 0xAA, sizeof(fresh));
        DatFile_ctor(&fresh);
        CheckEqU32(fresh.nMode, 0, "  nMode cleared (0x9EED8)");
        CheckEqU32(fresh.nBufCap, 150000, "  nBufCap is 150000 (0x9EEE8)");
        CheckEqU32(fresh.nBufUsed, 0, "  nBufUsed cleared (0x9EEEC)");
        Check(fresh.pDir == NULL, "  pDir cleared (0x9EEDC)");
        Check(fresh.pWriteBuf == NULL, "  pWriteBuf cleared (0x9EEE4)");
        Check(fresh.bChunkOpen == 0, "  bChunkOpen cleared (0x9EEE0)");
    }

    /* DatFile_Exists @ 0x9f1bc: mbstowcs at 0x9F1E4, FindFirstFileW at 0x9F1F4, and one exit test at
     * 0x9F20C that returns 0 both for INVALID_HANDLE_VALUE and for dwFileAttributes bit 1
     * (FILE_ATTRIBUTE_DIRECTORY). FindClose @ 0x9F21C runs only on the success path, so the original
     * leaks one find handle for every directory it tests; the port closes in that case too, which is
     * the stated deviation. The directory half is a separate answer from the missing-file half, so it
     * gets its own check. */
    printf("DatFile_Exists\n");
    Check(DatFile_Exists("lang.dat"), "Exists(lang.dat) is true");
    Check(!DatFile_Exists("no_such_member_xyz.dat"), "Exists(missing) is false");
    Check(!DatFile_Exists("docs"), "Exists(a directory) is false (0x9F20C attribute test)");

    /* DatFile_Write @ 0x9f4b4 buffers in a loop. When nBufUsed reaches nBufCap it appends the buffer at
     * EOF (SetFilePointer FROM_END at 0x9F524, WriteFile at 0x9F53C) and adds those bytes into
     * nChunkSize at 0x9F550; otherwise it copies min(nBufCap - nBufUsed, nLeft) bytes from pBuf + nSrc
     * at 0x9F578 and advances nSrc at 0x9F584. A write larger than the buffer therefore has to arrive
     * in order and still count as one chunk. The capacity is set to 4 here -- through the field the
     * loop reads, not through a size that happens to fit -- so three iterations run. On the read side
     * the same capacity drives the refill at 0x9F6FC..0x9F744, and FindChunk's `nBufUsed = nBufCap`
     * store at 0x9F690 is what forces that first refill. */
    printf("DatFile_Write / DatFile_Read buffer split\n");
    {
        static const u8 aPayload[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
        u8    aOut[10];
        DatFile dat;

        remove("test_split.tmp");
        memset(aOut, 0, sizeof(aOut));
        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_split.tmp", PUFO_DAT_CREATE, 2), "Open(create, 2 slots)");
        Check(DatFile_BeginChunk(&dat, "split"), "BeginChunk(split)");
        /* 0x9F5C0: FindChunk answers 0 unless nMode is 1, so a write-side container cannot start a read. */
        Check(!DatFile_FindChunk(&dat, "split"), "FindChunk in CREATE mode is refused (0x9f5ac)");
        /* With a zero capacity the original's loop cannot make progress: the append at 0x9F53C writes
         * nothing, the copy size at 0x9F560 is nBufCap - nBufUsed = 0, so nLeft never drops and 0x9F59C
         * branches back forever. The port refuses. */
        dat.nBufCap = 0;
        Check(!DatFile_Write(&dat, aPayload, 4), "Write with a zero-capacity buffer is refused (0x9f4b4)");
        dat.nBufCap = 4; /* the capacity 0x9F510 and 0x9F560 read on every iteration */
        Check(DatFile_Write(&dat, aPayload, sizeof(aPayload)), "Write(10) through a 4-byte buffer");
        CheckEqU32(dat.nChunkSize, 8, "  two full buffers went to the file (0x9F550)");
        CheckEqU32(dat.nBufUsed, 2, "  the tail stayed buffered (0x9F58C)");
        Check(dat.bDirty != 0, "  the write marks the container dirty (0x9F594)");
        DatFile_Close(&dat);

        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_split.tmp", PUFO_DAT_READ, 0), "reopen(read)");
        dat.nBufCap = 4;
        Check(DatFile_FindChunk(&dat, "SPLIT"), "FindChunk(SPLIT)");
        CheckEqU32(dat.nBufUsed, 4, "  FindChunk primed the buffer by setting nBufUsed = nBufCap (0x9F690)");
        Check(DatFile_Read(&dat, aOut, sizeof(aOut)), "Read(10) through a 4-byte buffer");
        Check(memcmp(aOut, aPayload, sizeof(aOut)) == 0, "  all ten bytes arrived in order (0x9F578 / 0x9F774)");
        CheckEqU32(dat.nChunkSize, 10, "  the directory size covers the whole chunk (0x9F664, 0x9F338)");
        DatFile_Close(&dat);
        remove("test_split.tmp");
    }

    /* DatFile_RegisterAtexit @ 0x3f0d0 and its thunk j_DatFile_dtor @ 0x3f0d8: the registrar hands the
     * container to atexit, and the thunk runs DatFile_dtor @ 0x9eef4 on the container it was handed.
     * The thunk's own work is four stores plus two frees: CloseHandle(this+0x10) at 0x9EF0C, a delete
     * whose argument is loaded as the *address* of the archive-directory global unk_1571B8
     * (`LDR R0, =unk_1571B8` at 0x9EF10, BL delete at 0x9EF14), nMode = 0 at 0x9EF1C, bChunkOpen = 0 at
     * 0x9EF20, then a guarded delete of this+0x34 at 0x9EF2C. The struct's own directory pointer at
     * +0x0C is never freed here -- DatFile_Close @ 0x9F19C frees that one -- and 0x1571B8 is not a
     * DatFile field: Assets_LoadStartup writes it as the archive 0 directory at 0x38308 and
     * GameAssets_Shutdown frees it again as unk_1571B8[i] at 0x38E80 (a value load, not an address
     * load). The port frees the struct's own directory and no global, which is the stated deviation.
     * The block is static because the registered entry runs the thunk once more at process exit. */
    printf("DatFile_RegisterAtexit / DatFile_AtexitThunk (0x3f0d0, 0x3f0d8, DatFile_dtor 0x9eef4)\n");
    {
        static DatFile dat;

        remove("test_atexit.tmp");
        DatFile_ctor(&dat);
        Check(DatFile_Open(&dat, "test_atexit.tmp", PUFO_DAT_CREATE, 2), "Open(create, 2 slots)");
        Check(DatFile_RegisterAtexit(&dat) == 0, "registrar returns atexit's success value");
        Check(dat.nMode == PUFO_DAT_CREATE, "container still open before the thunk runs");

        DatFile_AtexitThunk();

        Check(dat.nMode == PUFO_DAT_CLOSED, "thunk closed the container");
        Check(dat.pWriteBuf == NULL, "thunk released the write buffer");
        Check(dat.bChunkOpen == 0, "thunk cleared the chunk-open flag");

        /* The registered entry runs this same thunk at process exit; a second run must be safe. */
        DatFile_AtexitThunk();
        Check(dat.nMode == PUFO_DAT_CLOSED, "a second thunk run is idempotent");

        remove("test_atexit.tmp");
    }

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
