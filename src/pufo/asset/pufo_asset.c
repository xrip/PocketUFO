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

/* AssetReader_Clear @ 0x9f7d8 is 12 bytes and one store: bFound = 0 at 0x9F7DC. AssetReader_ClearGlobal
 * @ 0x83874 is that call on the single reader the original keeps at 0x187028: three instructions,
 * `LDR R0, =0x187028` at 0x83874, `B AssetReader_Clear` at 0x83878, literal at 0x8387C. It is reached
 * from one tail branch (0x83870, the last instruction of the geoscape routine ahead of it) and from one
 * pointer-table slot at 0x18aa08. The port memsets the caller's own reader, so the fields the original
 * leaves stale (name, offset, size, position, archive index) are zero here too. */
void AssetReader_Clear(AssetReader *pRdr)
{
    memset(pRdr, 0, sizeof(*pRdr));
    pRdr->bFound = 0;
}

/* AssetReader_Find @ 0x9f7e4. Copies the requested name into the reader's own szName (byte loop
 * 0x9F7F4..0x9F804) and upper-cases that copy at 0x9F80C, then scans archive 0 fully before archive 1
 * (`++v5 >= 2` at 0x9F864) over the counts at 0x1472C0 with the 21-byte stride at 0x9F850. A hit stores
 * bFound at 0x9F87C, memmoves the size at 0x9F8A8 and the offset at 0x9F8C0 (both fields are zeroed at
 * 0x9F88C/0x9F894 first, which the memmoves immediately overwrite), clears nReadPos at 0x9F8C4 and
 * stores the archive index at 0x9F8C8. A miss stores bFound = 0 at 0x9F868 and nothing else. */
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

/* AssetReader_Read @ 0x9f8d0. Tests bFound first (LDRB 0x9F8DC, CMP 0x9F8E8), then the signed bound
 * nReadPos + cb <= nDataSize (ADD 0x9F8F8, BGT 0x9F900). Both refusals branch to the epilogue at
 * 0x9F950 with the file handle untouched, so no SetFilePointer at 0x9F920, no ReadFile at 0x9F940 and no
 * `nReadPos += cb` at 0x9F94C. R0 keeps what the refusal loaded: the reader pointer when bFound is 0
 * (MOV R4,R0 at 0x9F8D8 leaves R0 alone) or nReadPos when the bound fails (LDR R0,[R4,#0x18] at 0x9F8F0)
 * -- a non-zero value in both cases, never a 0/1 flag. Of the 23 BL sites none reads R0 before
 * overwriting it; only Map_LoadMa1 at 0x54C2C passes the value on, as its own return, and all nine of
 * its call sites in Font_LoadTactSprites drop it. The port returns false on refusal. */
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

/* AssetReader_Found @ 0x9f95c: LDRB of bFound at 0x9F960, returned as-is, 8 bytes, reached from three
 * call sites (0x11084, 0x4630C, 0x9E33C). It checks nothing else. */
bool AssetReader_Found(const AssetReader *pRdr)
{
    return pRdr->bFound != 0;
}

/* AssetReader_Size @ 0x9f964: LDR of nDataSize at 0x9F968 -- the size field Find stored at 0x9F8A8, not
 * the file offset next to it (0x9F8C0) -- with 40 or more call sites, the widest reach of the family.
 * These two accessors sit next to each other and their cites are kept separate on purpose: a shared
 * comment block attributes both addresses to whichever function it sits above. */
u32 AssetReader_Size(const AssetReader *pRdr)
{
    return pRdr->nDataSize;
}
