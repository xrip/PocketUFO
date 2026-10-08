/* pufo_asset.c - archive registry and asset reader.
 *
 * Evidence: see pufo_asset.h for the address list.
 */
#include "asset/pufo_asset.h"

#include <stdlib.h>
#include <string.h>

#include "util/pufo_str.h"

/* Archive registry, mirroring the original globals:
 *   hAssetFile     @ 0x1568d0
 *   unk_1571B8     @ 0x1571b8  (DatDirEntry *[2])
 *   nAssetDirCount @ 0x1472c0  (int[2])
 */
static HANDLE       s_hAssetFile[PUFO_ARCHIVE_COUNT];
static DatDirEntry *s_pAssetDir[PUFO_ARCHIVE_COUNT];
static i32          s_nAssetDirCount[PUFO_ARCHIVE_COUNT];

static bool Asset_ToWide(const char *psz, wchar_t *pwOut, size_t cchOut)
{
    return MultiByteToWideChar(CP_ACP, 0, psz, -1, pwOut, (int)cchOut) > 0;
}

void Asset_CloseArchives(void)
{
    int i;

    for (i = 0; i < PUFO_ARCHIVE_COUNT; ++i) {
        if (s_hAssetFile[i] != NULL && s_hAssetFile[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(s_hAssetFile[i]);
        }
        s_hAssetFile[i] = NULL;
        free(s_pAssetDir[i]);
        s_pAssetDir[i]    = NULL;
        s_nAssetDirCount[i] = 0;
    }
}

bool Asset_OpenArchives(const char *const *ppszPaths)
{
    int i;

    Asset_CloseArchives();

    for (i = 0; i < PUFO_ARCHIVE_COUNT; ++i) {
        wchar_t wPath[1024];
        DWORD   cbRead = 0;
        u32     nCount = 0;

        if (ppszPaths[i] == NULL || !Asset_ToWide(ppszPaths[i], wPath, 1024)) {
            Asset_CloseArchives();
            return false;
        }

        s_hAssetFile[i] = CreateFileW(wPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (s_hAssetFile[i] == INVALID_HANDLE_VALUE) {
            s_hAssetFile[i] = NULL;
            Asset_CloseArchives();
            return false;
        }

        SetFilePointer(s_hAssetFile[i], 0, NULL, FILE_BEGIN);
        if (!ReadFile(s_hAssetFile[i], &nCount, 4, &cbRead, NULL)) {
            Asset_CloseArchives();
            return false;
        }

        s_nAssetDirCount[i] = (i32)nCount;
        s_pAssetDir[i] = malloc((size_t)(PUFO_DAT_ENTRY_SIZE * (size_t)nCount));
        if (s_pAssetDir[i] == NULL && nCount != 0) {
            Asset_CloseArchives();
            return false;
        }
        if (nCount != 0
            && !ReadFile(s_hAssetFile[i], s_pAssetDir[i],
                         (DWORD)(PUFO_DAT_ENTRY_SIZE * (size_t)nCount), &cbRead, NULL)) {
            Asset_CloseArchives();
            return false;
        }
    }
    return true;
}

void AssetReader_Clear(AssetReader *pRdr)
{
    memset(pRdr, 0, sizeof(*pRdr));
    pRdr->bFound = 0;
}

bool AssetReader_Find(AssetReader *pRdr, const char *pszName)
{
    u32 nArchive;

    /* The original copies the name then upper-cases it in place; the copy is
     * what makes the caller's buffer reusable. pufo_strupr is ASCII-only,
     * which matches the archive tags. */
    pufo_strncpy(pRdr->szName, pszName, PUFO_ASSET_NAME_MAX);
    pufo_strupr(pRdr->szName);

    for (nArchive = 0; nArchive < PUFO_ARCHIVE_COUNT; ++nArchive) {
        i32 nCount = s_nAssetDirCount[nArchive];
        i32 i;

        for (i = 0; i < nCount; ++i) {
            const DatDirEntry *pEntry = &s_pAssetDir[nArchive][i];

            if (strcmp(pEntry->szName, pRdr->szName) == 0) {
                pRdr->bFound      = 1;
                pRdr->nDataSize   = pEntry->nDataSize;
                pRdr->nFileOff    = pEntry->nFileOff;
                pRdr->nReadPos    = 0;
                pRdr->nArchiveIdx = nArchive;
                return true;
            }
        }
    }

    pRdr->bFound = 0;
    return false;
}

bool AssetReader_Read(AssetReader *pRdr, void *pBuf, u32 cb)
{
    DWORD cbRead = 0;

    if (pRdr->bFound == 0) {
        return false;
    }
    /* The original compares u32 offsets as signed; a chunk larger than 2 GB
     * would wrap. Kept identical to avoid a behavioural difference. */
    if ((i32)(pRdr->nReadPos + cb) > (i32)pRdr->nDataSize) {
        return false;
    }

    SetFilePointer(s_hAssetFile[pRdr->nArchiveIdx],
                   (LONG)(pRdr->nReadPos + pRdr->nFileOff), NULL, FILE_BEGIN);
    if (!ReadFile(s_hAssetFile[pRdr->nArchiveIdx], pBuf, cb, &cbRead, NULL)) {
        return false;
    }
    pRdr->nReadPos += cb;
    return true;
}

bool AssetReader_Found(const AssetReader *pRdr)
{
    return pRdr->bFound != 0;
}

u32 AssetReader_Size(const AssetReader *pRdr)
{
    return pRdr->nDataSize;
}
