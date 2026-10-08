/* pufo_asset.h - archive registry and asset reader.
 *
 * Evidence: Assets_LoadStartup @ 0x382d0 (opens the archives),
 *           GameAssets_Shutdown @ 0x38e4c (closes them),
 *           AssetReader_Clear @ 0x9f7d8, AssetReader_Find @ 0x9f7e4,
 *           AssetReader_Read @ 0x9f8d0, AssetReader_Found @ 0x9f95c,
 *           AssetReader_Size @ 0x9f964, AssetReader_ClearGlobal @ 0x83874.
 *
 * Assets_LoadStartup opens exactly two archives in order:
 *   index 0 -> lang.dat   (szAssetPathW[0] @ 0x187f58, built in InitInstance)
 *   index 1 -> main.dat
 * For each it reads a u32 entry count, then that many 21-byte DirEntry records,
 * and keeps the file handle open. Lookups upper-case the requested name and
 * scan archive 0 fully before archive 1, so lang.dat wins on a name collision.
 * That ordering matters: TEXTS.DAT exists in both, and the game intentionally
 * takes the lang.dat copy.
 */
#ifndef PUFO_ASSET_H
#define PUFO_ASSET_H

#include "dat/pufo_dat.h"
#include "pufo_common.h"

#include <windows.h>

#define PUFO_ARCHIVE_COUNT   2
#define PUFO_ASSET_NAME_MAX  15 /* includes NUL, matches the IDB struct */
#define PUFO_ARCHIVE_LANG    0
#define PUFO_ARCHIVE_MAIN    1

/* The IDB's AssetReader is 32 bytes on the original ILP32 target. As with
 * DatFile, the Win64 in-memory offsets differ because of pointer size; only
 * the named fields are used, so that is harmless. */
typedef struct AssetReader {
    u8  bFound;        /* 0x00 */
    char szName[PUFO_ASSET_NAME_MAX]; /* 0x01 */
    u32 nFileOff;      /* 0x10 */
    u32 nDataSize;     /* 0x14 */
    u32 nReadPos;      /* 0x18 */
    u32 nArchiveIdx;   /* 0x1c */
} AssetReader;

/* Open both archives. ppszPaths[0] = lang.dat, ppszPaths[1] = main.dat.
 * Returns true only if both opened and their directories loaded. */
bool Asset_OpenArchives(const char *const *ppszPaths);

/* Close both archives and free their directories. Safe to call twice. */
void Asset_CloseArchives(void);

void  AssetReader_Clear(AssetReader *pRdr);

/* Locate pszName (case-insensitive). Returns true if found. */
bool  AssetReader_Find(AssetReader *pRdr, const char *pszName);

/* Sequential read from the located member. Returns true on success. */
bool  AssetReader_Read(AssetReader *pRdr, void *pBuf, u32 cb);

bool  AssetReader_Found(const AssetReader *pRdr);
u32   AssetReader_Size(const AssetReader *pRdr);

#endif /* PUFO_ASSET_H */
