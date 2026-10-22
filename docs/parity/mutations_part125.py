#!/usr/bin/env python3
"""Part 125's mutation proofs: the 16 `dat` rows row 121a still had uncited.

Run from the repo root, after a configured build:  python -I docs/parity/mutations_part125.py
An optional substring argument runs one case:      python -I docs/parity/mutations_part125.py BELT

Rows: DatFile_ctor 0x9eecc, DatFile_dtor 0x9eef4, DatFile_Close 0x9f174, DatFile_Exists 0x9f1bc,
DatFile_FlushWrite 0x9f234, DatFile_BeginChunk 0x9f348, DatFile_Write 0x9f4b4, DatFile_FindChunk 0x9f5ac,
DatFile_Read 0x9f698, DatFile_ChunkPresent 0x9f7a8, AssetReader_Clear 0x9f7d8, AssetReader_Find 0x9f7e4,
AssetReader_Read 0x9f8d0, AssetReader_Found 0x9f95c, AssetReader_Size 0x9f964.
AssetReader_ClearGlobal 0x83874 has no case of its own: it is `AssetReader_Clear(&g_rdr)` (LDR R0 at
0x83874, B at 0x83878) and adds nothing the Clear cases do not already test -- stated, not pretended.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

DAT  = "src/pufo/dat/pufo_dat.c"
DATH = "src/pufo/dat/pufo_dat.h"
AST  = "src/pufo/asset/pufo_asset.c"
T    = "test_data"

CASES = [
    # 0x9eecc: the capacity the whole write/read buffering is chunked against (0x9EEE8 stores 150000).
    ("0x9eecc ctor capacity 150000 -> 149999", DATH,
     b"#define PUFO_DAT_BUF_DEFAULT 150000 /* ctor default, @ 0x9eecc */",
     b"#define PUFO_DAT_BUF_DEFAULT 149999 /* MUTATION */"),

    # 0x9eef4: the dtor clears bChunkOpen at 0x9EF20 as well as nMode.
    ("0x9eef4 dtor leaves the chunk-open flag set", DAT,
     b"        pDat->bChunkOpen = 0;\n    }\n    free(pDat->pWriteBuf);\n    pDat->pWriteBuf = NULL;\n}",
     b"        pDat->bChunkOpen = 1; /* MUTATION */\n    }\n    free(pDat->pWriteBuf);\n    pDat->pWriteBuf = NULL;\n}"),

    # 0x9f174: Close flushes at 0x9F18C before it closes the handle, which is how the last chunk's
    # pending bytes reach the file at all.
    ("0x9f174 Close does not flush first", DAT,
     b"        DatFile_FlushWrite(pDat);\n        CloseHandle(pDat->hFile);",
     b"        CloseHandle(pDat->hFile); /* MUTATION: 0x9F18C missing */"),

    # 0x9f1bc: one exit test answers 0 for a directory as well as for a missing file (0x9F20C).
    ("0x9f1bc Exists accepts a directory", DAT,
     b"    if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {",
     b"    if ((ffd.dwFileAttributes & 2u) != 0) { /* MUTATION: 0x9F20C tests the wrong bit */"),

    # 0x9f234 patches the previous slot in memory at 21*nCurEntry - 8 (0x9F304) and on disk at
    # 21*nCurEntry - 4 (0x9F320). The two halves can drift apart, and only the disk half is visible in
    # the file, which is why the round trip reads sizes off the disk.
    ("0x9f234 memory patch hits the wrong field (-8 -> -4)", DAT,
     b"        nPrevSizeOff = (u32)(PUFO_DAT_ENTRY_SIZE * (u32)pDat->nCurEntry - 8u);",
     b"        nPrevSizeOff = (u32)(PUFO_DAT_ENTRY_SIZE * (u32)pDat->nCurEntry - 4u); /* MUTATION */"),
    ("0x9f234 disk patch hits the wrong field (-4 -> -8)", DAT,
     b"        SetFilePointer(pDat->hFile, (LONG)(PUFO_DAT_ENTRY_SIZE * (u32)pDat->nCurEntry - 4u),",
     b"        SetFilePointer(pDat->hFile, (LONG)(PUFO_DAT_ENTRY_SIZE * (u32)pDat->nCurEntry - 8u), /* MUTATION */"),
    # The gate at 0x9F25C is (mode 2 or 3) AND bDirty: BeginChunk calls FlushWrite for every chunk, and
    # without the bDirty half it would patch the previous slot with a nChunkSize of 0.
    ("0x9f234 FlushWrite runs for a clean container too", DAT,
     b"        || pDat->bDirty == 0) {",
     b"        || pDat->bDirty != 0) { /* MUTATION: 0x9F25C gate inverted */"),

    # 0x9f348's length gate is `strlen == 0 || strlen > 12` at 0x9F3C0: both halves are separate answers.
    ("0x9f348 tag length bound 12 -> 13", DAT,
     b"    if (nTagLen == 0 || nTagLen > PUFO_DAT_TAG_MAX) {",
     b"    if (nTagLen == 0 || nTagLen > 13) { /* MUTATION */"),
    ("0x9f348 empty tag accepted", DAT,
     b"    if (nTagLen == 0 || nTagLen > PUFO_DAT_TAG_MAX) {",
     b"    if (nTagLen < 0 || nTagLen > PUFO_DAT_TAG_MAX) { /* MUTATION */"),
    ("0x9f348 tag is not upper-cased (0x9F3B0)", DAT,
     b"    pufo_strcpy(pDat->szTag, pszTag);\n    pufo_strupr(pDat->szTag);\n\n    DatDirEntry *pEntry",
     b"    pufo_strcpy(pDat->szTag, pszTag);\n\n    DatDirEntry *pEntry /* MUTATION: strupr dropped */"),

    # 0x9f4b4 refuses before it buffers: nCurEntry == 0 at 0x9F4D0 is "no chunk was begun".
    ("0x9f4b4 Write accepted before BeginChunk", DAT,
     b"    if (pDat->nCurEntry == 0) {\n        return false;\n    }",
     b"    if (pDat->nCurEntry < 0) { /* MUTATION: 0x9F4D0 gate gone */\n        return false;\n    }"),
    # The loop advances its source pointer as it copies (0x9F584); without that each buffer takes the
    # same leading bytes.
    ("0x9f4b4 split write re-reads the same source bytes", DAT,
     b"        nSrc            += nChunk;",
     b"        nSrc            += 0; /* MUTATION: 0x9F584 dropped */"),
    # A buffer that is appended must be counted into the chunk size (0x9F550).
    ("0x9f4b4 appended buffer is not counted into the chunk", DAT,
     b"            pDat->nChunkSize += pDat->nBufUsed;\n            pDat->nBufUsed = 0;\n        }\n        u32 nChunk = pDat->nBufCap - pDat->nBufUsed;",
     b"            pDat->nChunkSize += 0; /* MUTATION: 0x9F550 dropped */\n            pDat->nBufUsed = 0;\n        }\n        u32 nChunk = pDat->nBufCap - pDat->nBufUsed;"),

    # 0x9f5ac requires nMode == 1 (0x9F5C0) and primes the buffer with nBufUsed = nBufCap (0x9F690) so the
    # first read refills from the chunk instead of serving the previous chunk's bytes.
    ("0x9f5ac FindChunk accepted in CREATE mode", DAT,
     b"    if (pDat->nMode != PUFO_DAT_READ) {\n        return false;\n    }\n    pufo_strcpy(pDat->szTag, pszTag);",
     b"    if (pDat->nMode != PUFO_DAT_CREATE) { /* MUTATION: 0x9F5C0 */\n        return false;\n    }\n    pufo_strcpy(pDat->szTag, pszTag);"),
    ("0x9f5ac FindChunk does not prime the buffer", DAT,
     b"        pDat->nBufUsed      = pDat->nBufCap; /* force a refill on first read */",
     b"        pDat->nBufUsed      = 0; /* MUTATION: 0x9F690 */"),

    # 0x9f698's bound is signed `<=` (ADD 0x9F6D8 / BGT 0x9F6E0): an exact-fit read must succeed, and the
    # refusal must leave nChunkReadPos alone. The refill is clamped to the chunk's remaining bytes.
    ("0x9f698 exact-fit read refused (> -> >=)", DAT,
     b"    if ((i32)(pDat->nChunkReadPos + nSize) > (i32)pDat->nChunkSize) {",
     b"    if ((i32)(pDat->nChunkReadPos + nSize) >= (i32)pDat->nChunkSize) { /* MUTATION */"),
    ("0x9f698 refill ignores the chunk end", DAT,
     b"            u32 nToRead = pDat->nBufCap;\n            if (pDat->nChunkSize - pDat->nChunkReadPos < nToRead) {\n                nToRead = pDat->nChunkSize - pDat->nChunkReadPos;\n            }",
     b"            u32 nToRead = pDat->nBufCap; /* MUTATION: 0x9F71C clamp dropped */"),

    # 0x9f7a8 tests mode first (0x9F7B0): a container that is not in READ mode has no open chunk.
    ("0x9f7a8 ChunkPresent mode test flipped", DAT,
     b"    if (pDat->nMode != PUFO_DAT_READ) {\n        return false;\n    }\n    if (pDat->bChunkFound == 0) {",
     b"    if (pDat->nMode != PUFO_DAT_CREATE) { /* MUTATION: 0x9F7B0 */\n        return false;\n    }\n    if (pDat->bChunkFound == 0) {"),

    # 0x9f7d8 is one store (0x9F7DC). AssetReader_ClearGlobal @ 0x83874 is this call on the original's
    # fixed reader and has no case of its own.
    ("0x9f7d8 Clear leaves bFound set", AST,
     b"    memset(pRdr, 0, sizeof(*pRdr));\n    pRdr->bFound = 0;",
     b"    memset(pRdr, 0, sizeof(*pRdr));\n    pRdr->bFound = 1; /* MUTATION */"),

    # 0x9f7e4 scans archive 0 before archive 1 and upper-cases the copy of the name it compares (0x9F80C).
    ("0x9f7e4 archive scan starts at main.dat", AST,
     b"    for (nArchive = 0; nArchive < PUFO_ARCHIVE_COUNT; ++nArchive) {",
     b"    for (nArchive = 1; nArchive < PUFO_ARCHIVE_COUNT; ++nArchive) { /* MUTATION */"),
    ("0x9f7e4 name is not upper-cased", AST,
     b"    pufo_strupr(pRdr->szName);",
     b"    /* MUTATION: 0x9F80C strupr dropped */"),

    # 0x9f8d0 refuses on the signed bound before touching the file (0x9F8F8/0x9F900).
    ("0x9f8d0 over-read is not refused", AST,
     b"    if ((i32)(pRdr->nReadPos + cb) > (i32)pRdr->nDataSize) {\n        return false;\n    }",
     b"    if ((i32)(pRdr->nReadPos + cb) < 0) { /* MUTATION: 0x9F900 bound gone */\n        return false;\n    }"),

    # 0x9f95c returns bFound (0x9F960); 0x9f964 returns the size field (0x9F968), not its neighbour.
    ("0x9f95c Found inverted", AST,
     b"    return pRdr->bFound != 0;",
     b"    return pRdr->bFound == 0; /* MUTATION */"),
    ("0x9f964 Size returns the file offset", AST,
     b"    return pRdr->nDataSize;",
     b"    return pRdr->nFileOff; /* MUTATION */"),
]

if __name__ == "__main__":
    wanted = sys.argv[1] if len(sys.argv) > 1 else None
    for name, path, old, new in CASES:
        if wanted and wanted not in name:
            continue
        run_case(name, T, path, old, new)
