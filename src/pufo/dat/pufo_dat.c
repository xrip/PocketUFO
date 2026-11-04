/* pufo_dat.c - the game's tagged archive container.
 *
 * Evidence: see pufo_dat.h for the full address list.
 *
 * Layout note, confirmed from the DatFile_FlushWrite disassembly @ 0x9f234:
 * BeginChunk reserves directory slot nCurEntry (writing nDataSize = 0 and
 * nFileOff = current EOF) and then increments nCurEntry. FlushWrite appends the
 * pending buffer and patches the PREVIOUS slot, pDir[nCurEntry-1].nDataSize:
 *
 *     R3 = nCurEntry; R5 = 21; R1 = 21*nCurEntry; R3 = R1 - 8
 *     address = pDir + 21*nCurEntry - 8  ==  &pDir[nCurEntry-1].nDataSize
 *     SetFilePointer(21*nCurEntry - 4)   ==  file offset of that same field
 *
 * The 4-byte read just before the patch only reloads the old value into a
 * scratch slot that is immediately overwritten; it is vestigial.
 */
#include "dat/pufo_dat.h"

#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "util/pufo_str.h"

/* ANSI -> UTF-16, the original's mbstowcs. */
static bool Dat_ToWide(const char *pszPath, wchar_t *pwOut, size_t cchOut)
{
    int n = MultiByteToWideChar(CP_ACP, 0, pszPath, -1, pwOut, (int)cchOut);

    return n > 0;
}

/* Guarded Open can own resources before nMode is published. */
static void Dat_ReleaseOwned(DatFile *pDat)
{
    if (pDat->hFile != NULL && pDat->hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(pDat->hFile);
    }
    pDat->hFile = NULL;
    free(pDat->pDir);
    pDat->pDir = NULL;
    free(pDat->pWriteBuf);
    pDat->pWriteBuf = NULL;
    pDat->nMode = PUFO_DAT_CLOSED;
}


/* DatFile_ctor @ 0x9eecc. Six stores, nothing else: nMode 0 at 0x9EED8, pDir 0 at 0x9EEDC,
 * bChunkOpen 0 at 0x9EEE0, pWriteBuf 0 at 0x9EEE4, nBufCap 150000 at 0x9EEE8, nBufUsed 0 at 0x9EEEC.
 * The port memsets the whole struct first, so the eight fields the original leaves alone are zero too. */
void DatFile_ctor(DatFile *pDat)
{
    memset(pDat, 0, sizeof(*pDat));
    pDat->nMode   = PUFO_DAT_CLOSED;
    pDat->pDir    = NULL;
    pDat->bDirty  = 0;
    pDat->pWriteBuf = NULL;
    pDat->nBufCap = PUFO_DAT_BUF_DEFAULT;
    pDat->nBufUsed = 0;
}

/* DatFile_dtor @ 0x9eef4. When nMode != 0: CloseHandle(this+0x10) at 0x9EF0C, then a delete whose
 * argument is the literal address of the archive-directory global -- `LDR R0, =unk_1571B8` at 0x9EF10,
 * BL delete at 0x9EF14 -- then nMode = 0 at 0x9EF1C and bChunkOpen = 0 at 0x9EF20; finally a guarded
 * delete of this+0x34 at 0x9EF2C. The struct's own directory pointer at +0x0C is not freed here
 * (DatFile_Close frees it at 0x9F19C) and unk_1571B8 is not a DatFile member: Assets_LoadStartup writes
 * it at 0x38308 and GameAssets_Shutdown frees it again as unk_1571B8[i] at 0x38E80. The port frees the
 * struct's own directory and no global -- the deviation tests/test_data.c states. */
void DatFile_dtor(DatFile *pDat)
{
    if (pDat->nMode != PUFO_DAT_CLOSED) {
        pDat->bChunkOpen = 0;
    }
    Dat_ReleaseOwned(pDat);
}

/* The thunk the registrar installs. In the original it names the global container the save driver
 * opens slots on; the port's DatFile objects are caller-owned, so it releases the one
 * DatFile_RegisterAtexit was handed. */
static DatFile *g_pDatFileToFree;

void DatFile_AtexitThunk(void)
{
    if (g_pDatFileToFree != NULL) {
        DatFile_dtor(g_pDatFileToFree);
    }
}

int DatFile_RegisterAtexit(DatFile *pDat)
{
    g_pDatFileToFree = pDat;
    return atexit(DatFile_AtexitThunk); /* 0x3F0D8 */
}

/* DatFile_Close @ 0x9f174. Body only when nMode != 0 (0x9F188): FlushWrite at 0x9F18C -- so a chunk's
 * pending bytes reach the file before anything is read back -- CloseHandle at 0x9F194, the directory
 * free at 0x9F19C, nMode = 0 at 0x9F1A0; then the guarded write-buffer free at 0x9F1AC with
 * pWriteBuf = 0 at 0x9F1B4, which is what makes a second Close safe. The IDB types it void -- R0 at the
 * exit is only whatever the buffer load at 0x9F1A4 left there. */
void DatFile_Close(DatFile *pDat)
{
    if (pDat->nMode != PUFO_DAT_CLOSED) {
        DatFile_FlushWrite(pDat);
    }
    Dat_ReleaseOwned(pDat);
}

/* DatFile_Exists @ 0x9f1bc. mbstowcs at 0x9F1E4, FindFirstFileW at 0x9F1F4, then one exit test at
 * 0x9F208 that tests dwFileAttributes mask 1 (FILE_ATTRIBUTE_READONLY). A missing path or a read-only
 * match answers 0; a non-read-only directory answers 1. FindClose at 0x9F21C runs only on the success
 * path, so the original leaks one find handle per read-only match; the port closes in that case too,
 * which tests/test_data.c
 * states as the deviation. */
bool DatFile_Exists(const char *pszPath)
{
    WIN32_FIND_DATAW ffd;
    HANDLE           hFind;
    wchar_t          wPath[1024];

    if (!Dat_ToWide(pszPath, wPath, 1024)) {
        return false;
    }
    hFind = FindFirstFileW(wPath, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }
    if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
        /* The original returns without closing here; that leaks one handle per
         * call. Not reproduced: the port closes it. */
        FindClose(hFind);
        return false;
    }
    FindClose(hFind);
    return true;
}

bool DatFile_Open(DatFile *pDat, const char *pszPath, DatMode nMode, int nEntryCount)
{
    wchar_t wPath[1024];
    DWORD   cbRead = 0;
    DWORD   nDirBytes;

    if (!Dat_ToWide(pszPath, wPath, 1024)) {
        return false;
    }

    if (nMode == PUFO_DAT_READ || nMode == PUFO_DAT_REWRITE) {
        /* Raw mode 3 opens write-only, then follows this directory-read path.
         * The port's read-failure guard rejects it without rewriting the file. */
        const DWORD dwAccess = (nMode == PUFO_DAT_READ) ? GENERIC_READ : GENERIC_WRITE;
        const DWORD dwShare = (nMode == PUFO_DAT_READ) ? FILE_SHARE_READ : FILE_SHARE_WRITE;
        pDat->hFile = CreateFileW(wPath, dwAccess, dwShare, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (pDat->hFile == INVALID_HANDLE_VALUE) {
            pDat->hFile = NULL;
            return false;
        }
        pDat->nEntryCount = 0;
        SetFilePointer(pDat->hFile, 0, NULL, FILE_BEGIN);
        if (!ReadFile(pDat->hFile, &pDat->nEntryCount, 4, &cbRead, NULL)) {
            Dat_ReleaseOwned(pDat);
            return false;
        }
        pDat->pDir = malloc((size_t)(PUFO_DAT_ENTRY_SIZE * pDat->nEntryCount));
        if (pDat->pDir == NULL) {
            Dat_ReleaseOwned(pDat);
            return false;
        }
        (void)ReadFile(pDat->hFile, pDat->pDir,
                       (DWORD)(PUFO_DAT_ENTRY_SIZE * pDat->nEntryCount), &cbRead, NULL);
        /* nCurEntry becomes the index of the first empty-named entry. Save
         * files zero-fill their directory, so this is the written chunk count. */
        pDat->nCurEntry = 0;
        for (i32 i = 0; i < pDat->nEntryCount; ++i) {
            if (pDat->pDir[i].szName[0] == '\0') {
                pDat->nCurEntry = i;
                break;
            }
        }
    } else if (nMode == PUFO_DAT_CREATE) {
        DWORD cbWritten = 0;

        pDat->hFile = CreateFileW(wPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (pDat->hFile == INVALID_HANDLE_VALUE) {
            pDat->hFile = NULL;
            return false;
        }
        pDat->pDir = malloc((size_t)(PUFO_DAT_ENTRY_SIZE * nEntryCount));
        if (pDat->pDir == NULL) {
            Dat_ReleaseOwned(pDat);
            return false;
        }
        memset(pDat->pDir, 0, (size_t)(PUFO_DAT_ENTRY_SIZE * nEntryCount));
        nDirBytes = (DWORD)nEntryCount;
        (void)WriteFile(pDat->hFile, &nDirBytes, 4, &cbWritten, NULL);
        (void)WriteFile(pDat->hFile, pDat->pDir,
                        (DWORD)(PUFO_DAT_ENTRY_SIZE * nEntryCount), &cbWritten, NULL);
        pDat->nCurEntry   = 0;
        pDat->nEntryCount = nEntryCount;
    } else {
        return false;
    }

    free(pDat->pWriteBuf);
    pDat->pWriteBuf = NULL;
    pDat->nBufUsed = 0;
    pDat->nMode    = nMode;
    if (pDat->nBufCap != 0) {
        pDat->pWriteBuf = malloc(pDat->nBufCap);
    }
    pDat->bDirty = 0;
    return true;
}

/* DatFile_FlushWrite @ 0x9f234. Runs only when (nMode == 2 || nMode == 3) && bDirty, tested together
 * at 0x9F25C. Appends the pending buffer at EOF (0x9F280/0x9F294) and folds it into nChunkSize at
 * 0x9F2A8, then, when nChunkSize > 0, patches the slot the previous BeginChunk reserved: in memory at
 * 21*nCurEntry - 8 (0x9F304) and on disk at 21*nCurEntry - 4 (0x9F320 with WriteFile at 0x9F338). The
 * 4-byte read at 0x9F2DC that loads the old value into the stack scratch is overwritten at 0x9F2EC
 * before it is ever used. bDirty is cleared at 0x9F33C, inside the gate. */
void DatFile_FlushWrite(DatFile *pDat)
{
    DWORD cbWritten = 0;
    u32   nPrevSizeOff;

    if ((pDat->nMode != PUFO_DAT_CREATE && pDat->nMode != PUFO_DAT_REWRITE)
        || pDat->bDirty == 0) {
        return;
    }

    if (pDat->nBufUsed != 0) {
        SetFilePointer(pDat->hFile, 0, NULL, FILE_END);
        (void)WriteFile(pDat->hFile, pDat->pWriteBuf, pDat->nBufUsed, &cbWritten, NULL);
        pDat->nChunkSize += pDat->nBufUsed;
    }

    if (pDat->nChunkSize > 0) {
        /* Patch the previous slot's nDataSize, both in memory and on disk. */
        nPrevSizeOff = (u32)(PUFO_DAT_ENTRY_SIZE * (u32)pDat->nCurEntry - 8u);
        memmove((u8 *)pDat->pDir + nPrevSizeOff, &pDat->nChunkSize, 4);
        SetFilePointer(pDat->hFile, (LONG)(PUFO_DAT_ENTRY_SIZE * (u32)pDat->nCurEntry - 4u),
                       NULL, FILE_BEGIN);
        (void)WriteFile(pDat->hFile, &pDat->nChunkSize, 4, &cbWritten, NULL);
    }
    pDat->bDirty = 0;
}

/* DatFile_BeginChunk @ 0x9f348. FlushWrite first at 0x9F358; mode gate at 0x9F36C; "no slots left"
 * gate `nCurEntry == nEntryCount` at 0x9F37C; the tag is copied into the struct's own szTag by the
 * byte loop at 0x9F398..0x9F3A8 and upper-cased at 0x9F3B0, and only then is its length tested at
 * 0x9F3C0 (`== 0 || > 12`). The port tests the length before it copies, so a rejected tag cannot
 * overwrite szTag and the fields after it -- stated in tests/test_data.c. Then the reserved slot is
 * written: name copy, nDataSize = 0 at 0x9F414, nFileOff = GetFileSize at 0x9F444, the whole 0x15-byte
 * entry to file offset 21*nCurEntry + 4 (0x9F460/0x9F484), and the state reset nBufUsed = 0,
 * bDirty = 0, nCurEntry += 1, nChunkSize = 0 at 0x9F490..0x9F4A0. */
bool DatFile_BeginChunk(DatFile *pDat, const char *pszTag)
{
    DWORD cbWritten = 0;
    size_t nTagLen;
    u32   nFileSize;

    DatFile_FlushWrite(pDat);
    if (pDat->nMode != PUFO_DAT_CREATE && pDat->nMode != PUFO_DAT_REWRITE) {
        return false;
    }
    if (pDat->nCurEntry == pDat->nEntryCount) {
        return false;
    }
    nTagLen = strlen(pszTag);
    if (nTagLen == 0 || nTagLen > PUFO_DAT_TAG_MAX) {
        return false;
    }

    pufo_strcpy(pDat->szTag, pszTag);
    pufo_strupr(pDat->szTag);

    DatDirEntry *pEntry = &pDat->pDir[pDat->nCurEntry];
    pufo_strcpy(pEntry->szName, pDat->szTag);
    pEntry->nDataSize = 0;
    nFileSize = GetFileSize(pDat->hFile, NULL);
    pEntry->nFileOff  = nFileSize;

    SetFilePointer(pDat->hFile, (LONG)(PUFO_DAT_ENTRY_SIZE * (u32)pDat->nCurEntry + 4u),
                   NULL, FILE_BEGIN);
    (void)WriteFile(pDat->hFile, pEntry, PUFO_DAT_ENTRY_SIZE, &cbWritten, NULL);

    pDat->nBufUsed   = 0;
    pDat->bDirty     = 0;
    pDat->nCurEntry += 1;
    pDat->nChunkSize = 0;
    return true;
}

/* DatFile_Write @ 0x9f4b4. Guard order is `nCurEntry == 0` at 0x9F4D0, `(nSize & 0x80000000)` at
 * 0x9F4D8, mode at 0x9F4EC. The buffering loop is 0x9F4FC..0x9F59C: at nBufUsed >= nBufCap it appends
 * the buffer at EOF (0x9F524/0x9F53C) and folds it into nChunkSize at 0x9F550, then copies
 * min(nBufCap - nBufUsed, nLeft) bytes from pBuf + nSrc at 0x9F578, advancing nSrc at 0x9F584 and
 * setting bDirty at 0x9F594. The original has no buffer guard: with nBufCap == 0 that loop makes no
 * progress at all (the append writes 0 bytes, the copy is 0 bytes, nLeft never drops) and the call
 * never returns. The port refuses instead, which tests/test_data.c names as the deviation. */
bool DatFile_Write(DatFile *pDat, const void *pBuf, u32 nSize)
{
    DWORD cbWritten = 0;
    i32   nLeft;
    u32   nSrc = 0;

    if (pDat->nCurEntry == 0) {
        return false;
    }
    /* Without a ctor call nBufCap is 0 and the buffering loop below would spin
     * forever writing nothing. Fail instead of hanging. */
    if (pDat->pWriteBuf == NULL || pDat->nBufCap == 0) {
        return false;
    }
    if ((nSize & 0x80000000u) != 0) {
        return false;
    }
    if (pDat->nMode != PUFO_DAT_CREATE && pDat->nMode != PUFO_DAT_REWRITE) {
        return false;
    }

    nLeft = (i32)nSize;
    while (nLeft > 0) {
        if (pDat->nBufUsed >= pDat->nBufCap) {
            SetFilePointer(pDat->hFile, 0, NULL, FILE_END);
            (void)WriteFile(pDat->hFile, pDat->pWriteBuf, pDat->nBufUsed, &cbWritten, NULL);
            pDat->nChunkSize += pDat->nBufUsed;
            pDat->nBufUsed = 0;
        }
        u32 nChunk = pDat->nBufCap - pDat->nBufUsed;
        if (nChunk >= (u32)nLeft) {
            nChunk = (u32)nLeft;
        }
        memmove(pDat->pWriteBuf + pDat->nBufUsed, (const u8 *)pBuf + nSrc, nChunk);
        nLeft           -= (i32)nChunk;
        nSrc            += nChunk;
        pDat->nBufUsed  += nChunk;
        pDat->bDirty     = 1;
    }
    return true;
}

/* DatFile_FindChunk @ 0x9f5ac. Requires nMode == 1 at 0x9F5C0, copies the tag into the struct at
 * 0x9F5CC..0x9F5DC and upper-cases it at 0x9F5E4, then scans slots 0..nCurEntry-1 with the 21-byte
 * stride (0x9F61C). On a hit it stores bChunkFound at 0x9F654, nChunkSize at 0x9F664, nChunkFileOff at
 * 0x9F67C, nChunkReadPos = 0 at 0x9F688, bChunkOpen = 1 at 0x9F68C and nBufUsed = nBufCap at 0x9F690:
 * that last store is what makes the first DatFile_Read refill from the chunk instead of serving the
 * bytes the previous chunk left in the buffer. On a miss only bChunkFound (0x9F628) and bChunkOpen
 * (0x9F62C) are cleared; size and offset keep their old values. */
bool DatFile_FindChunk(DatFile *pDat, const char *pszTag)
{
    i32 i;

    if (pDat->nMode != PUFO_DAT_READ) {
        return false;
    }
    pufo_strcpy(pDat->szTag, pszTag);
    pufo_strupr(pDat->szTag);

    for (i = 0; i < pDat->nCurEntry; ++i) {
        if (strcmp(pDat->pDir[i].szName, pDat->szTag) == 0) {
            pDat->bChunkFound   = 1;
            pDat->nChunkFileOff = pDat->pDir[i].nFileOff;
            pDat->nChunkSize    = pDat->pDir[i].nDataSize;
            pDat->nChunkReadPos = 0;
            pDat->nBufUsed      = pDat->nBufCap; /* force a refill on first read */
            pDat->bChunkOpen    = 1;
            return true;
        }
    }
    pDat->bChunkFound = 0;
    pDat->bChunkOpen  = 0;
    return false;
}

/* DatFile_Read @ 0x9f698. Four gates in order: nMode == 1 (0x9F6B0), bChunkOpen (0x9F6B8),
 * bChunkFound (0x9F6C4), and the signed bound nChunkReadPos + nSize <= nChunkSize (ADD 0x9F6D8,
 * BGT 0x9F6E0), so a read that exactly fills the chunk is allowed. Each refusal goes straight to the
 * epilogue at 0x9F7A0: no SetFilePointer at 0x9F72C, no ReadFile at 0x9F744, and nChunkReadPos is not
 * advanced. R0 is never normalised on those paths (only MOV R4,R0 at 0x9F6A0 writes it), so the
 * original answers a refusal with the DatFile pointer and a success with the memmove result: a pointer
 * either way, never a 0/1 flag. None of the 351 BL sites reads that register; the port returns false
 * on refusal, which no original caller can observe. The refill at 0x9F6FC..0x9F744 reads
 * min(nBufCap, nChunkSize - nChunkReadPos) bytes. */
bool DatFile_Read(DatFile *pDat, void *pBuf, u32 nSize)
{
    DWORD cbRead = 0;
    i32   nLeft;

    if (pDat->nMode != PUFO_DAT_READ || pDat->bChunkOpen == 0 || pDat->bChunkFound == 0) {
        return false;
    }
    if ((i32)(pDat->nChunkReadPos + nSize) > (i32)pDat->nChunkSize) {
        return false;
    }

    nLeft = (i32)nSize;
    while (nLeft > 0) {
        if (pDat->nBufUsed >= pDat->nBufCap) {
            u32 nToRead = pDat->nBufCap;
            if (pDat->nChunkSize - pDat->nChunkReadPos < nToRead) {
                nToRead = pDat->nChunkSize - pDat->nChunkReadPos;
            }
            SetFilePointer(pDat->hFile,
                           (LONG)(pDat->nChunkFileOff + pDat->nChunkReadPos),
                           NULL, FILE_BEGIN);
            (void)ReadFile(pDat->hFile, pDat->pWriteBuf, nToRead, &cbRead, NULL);
            pDat->nBufUsed = 0;
        }
        u32 nChunk = pDat->nBufCap - pDat->nBufUsed;
        if (pDat->nBufUsed + (u32)nLeft <= pDat->nBufCap) {
            nChunk = (u32)nLeft;
        }
        memmove((u8 *)pBuf + (nSize - (u32)nLeft),
                pDat->pWriteBuf + pDat->nBufUsed, nChunk);
        nLeft             -= (i32)nChunk;
        pDat->nBufUsed    += nChunk;
        pDat->nChunkReadPos += nChunk;
    }
    return true;
}

/* DatFile_ChunkPresent @ 0x9f7a8. Three tests, in this order, each with its own 0 exit:
 * nMode == 1 at 0x9F7B0, bChunkFound at 0x9F7BC, bChunkOpen at 0x9F7C0 with the 0 at 0x9F7D0 and the
 * 1 at 0x9F7C4. It reads no file and no directory entry. */
bool DatFile_ChunkPresent(const DatFile *pDat)
{
    if (pDat->nMode != PUFO_DAT_READ) {
        return false;
    }
    if (pDat->bChunkFound == 0) {
        return false;
    }
    return pDat->bChunkOpen != 0;
}
