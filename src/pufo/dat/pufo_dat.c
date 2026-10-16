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

void DatFile_dtor(DatFile *pDat)
{
    if (pDat->nMode != PUFO_DAT_CLOSED) {
        CloseHandle(pDat->hFile);
        free(pDat->pDir);
        pDat->nMode = PUFO_DAT_CLOSED;
        pDat->bChunkOpen = 0;
    }
    free(pDat->pWriteBuf);
    pDat->pWriteBuf = NULL;
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

void DatFile_Close(DatFile *pDat)
{
    if (pDat->nMode != PUFO_DAT_CLOSED) {
        DatFile_FlushWrite(pDat);
        CloseHandle(pDat->hFile);
        free(pDat->pDir);
        pDat->nMode = PUFO_DAT_CLOSED;
    }
    free(pDat->pWriteBuf);
    pDat->pWriteBuf = NULL;
}

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
    if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
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

    if (nMode == PUFO_DAT_READ) {
        pDat->hFile = CreateFileW(wPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (pDat->hFile == INVALID_HANDLE_VALUE) {
            pDat->hFile = NULL;
            return false;
        }
        pDat->nEntryCount = 0;
        SetFilePointer(pDat->hFile, 0, NULL, FILE_BEGIN);
        if (!ReadFile(pDat->hFile, &pDat->nEntryCount, 4, &cbRead, NULL)) {
            return false;
        }
        pDat->pDir = malloc((size_t)(PUFO_DAT_ENTRY_SIZE * pDat->nEntryCount));
        if (pDat->pDir == NULL) {
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
    } else if (nMode == PUFO_DAT_CREATE || nMode == PUFO_DAT_REWRITE) {
        DWORD dwDisposition = (nMode == PUFO_DAT_CREATE) ? CREATE_ALWAYS : OPEN_EXISTING;
        DWORD cbWritten = 0;

        pDat->hFile = CreateFileW(wPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                  dwDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
        if (pDat->hFile == INVALID_HANDLE_VALUE) {
            pDat->hFile = NULL;
            return false;
        }
        pDat->pDir = malloc((size_t)(PUFO_DAT_ENTRY_SIZE * nEntryCount));
        if (pDat->pDir == NULL) {
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
