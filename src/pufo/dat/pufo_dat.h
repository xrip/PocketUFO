/* pufo_dat.h - the game's tagged archive container.
 *
 * Evidence: DatFile_ctor @ 0x9eecc, DatFile_dtor @ 0x9eef4,
 *           DatFile_Open @ 0x9ef38, DatFile_Close @ 0x9f174,
 *           DatFile_Exists @ 0x9f1bc, DatFile_FlushWrite @ 0x9f234,
 *           DatFile_BeginChunk @ 0x9f348, DatFile_Write @ 0x9f4b4,
 *           DatFile_FindChunk @ 0x9f5ac, DatFile_Read @ 0x9f698,
 *           DatFile_ChunkPresent @ 0x9f7a8.
 *
 * On-disk layout, confirmed byte-for-byte against the real main.dat and
 * lang.dat (see docs/parity/data-layer.md):
 *
 *   u32                  nEntryCount
 *   DatDirEntry[count]   21 bytes each
 *   <chunk payloads>     each chunk's bytes, in directory order
 *
 * The name field is 13 bytes including the NUL, which caps tags at 12
 * characters (DatFile_BeginChunk rejects longer ones).
 *
 * Who uses what: DatFile is the save-file container. main.dat and lang.dat are
 * read through the archive registry + AssetReader in pufo_asset.h, not through
 * DatFile. In READ mode nCurEntry is the index of the first empty-named
 * directory slot (save files zero-fill their directory, so this equals the
 * written chunk count) and FindChunk scans only 0..nCurEntry-1.
 */
#ifndef PUFO_DAT_H
#define PUFO_DAT_H

#include "pufo_common.h"

#include <windows.h>

#include <stddef.h>
#define PUFO_DAT_NAME_MAX    13 /* includes NUL */
#define PUFO_DAT_TAG_MAX     12 /* BeginChunk rejects longer tags */
#define PUFO_DAT_ENTRY_SIZE  21 /* sizeof(DatDirEntry) on disk, @ 0x9eecc */
#define PUFO_DAT_BUF_DEFAULT 150000 /* ctor default, @ 0x9eecc */

/* The original uses classic CRT string routines (strcpy, strcat, _strupr).
 * Ported code keeps that vintage style, but the MSVC CRT marks them deprecated,
 * which /WX rejects. The helpers live in util/pufo_str.h. */

typedef enum DatMode {
    PUFO_DAT_CLOSED  = 0,
    PUFO_DAT_READ    = 1,
    PUFO_DAT_CREATE  = 2, /* create/truncate */
    PUFO_DAT_REWRITE = 3  /* open existing, rewrite in place */
} DatMode;

/* 21 bytes on disk. This MUST be packed: without it the compiler aligns the
 * struct to 24 bytes to satisfy u32 alignment after a 13-byte name, and every
 * directory read past entry 0 is garbage. The IDA struct is 21 bytes because
 * the original ARM build packed it the same way. The static assert below is
 * the guard against anyone removing the packing. */
#pragma pack(push, 1)
typedef struct DatDirEntry {
    char szName[PUFO_DAT_NAME_MAX];
    u32  nDataSize;
    u32  nFileOff;
} DatDirEntry;
#pragma pack(pop)

_Static_assert(sizeof(DatDirEntry) == PUFO_DAT_ENTRY_SIZE,
               "DatDirEntry must stay 21 bytes to match the on-disk format");

/* The IDB's DatFile is 68 bytes on the original ILP32 target. This port does
 * NOT reproduce those byte offsets: pointers and HANDLE are 8 bytes on Win64,
 * so the in-memory offsets differ. That is safe because this struct is never
 * written to disk or shared with the original binary; only the named fields
 * matter. What must stay exact is the on-disk DatDirEntry stride above. */
typedef struct DatFile {
    u32           nMode;          /* 0x00 */
    i32           nEntryCount;    /* 0x04 */
    i32           nCurEntry;      /* 0x08 */
    DatDirEntry  *pDir;           /* 0x0c */
    HANDLE        hFile;          /* 0x10 */
    u8            bChunkFound;    /* 0x14, IDA gap_20 */
    char          szTag[PUFO_DAT_NAME_MAX]; /* 0x15 */
    u8            pad_22[2];      /* 0x22, IDA gap_34[0..1] */
    u32           nChunkFileOff;  /* 0x24, IDA gap_34[2] */
    u32           nChunkSize;     /* 0x28, IDA nUnk40 */
    u32           nChunkReadPos;  /* 0x2c, IDA gap_44[0] */
    u8            bChunkOpen;     /* 0x30, IDA gap_44[4] */
    u8            pad_31[3];      /* 0x31, IDA gap_44[5..7] */
    u8           *pWriteBuf;      /* 0x34 */
    u32           nBufCap;        /* 0x38 */
    u32           nBufUsed;       /* 0x3c */
    u8            bDirty;         /* 0x40 */
    u8            pad_41[3];      /* 0x41, IDA gap_65[0..2] */
} DatFile;

void DatFile_ctor(DatFile *pDat);
void DatFile_dtor(DatFile *pDat);

/* nEntryCount is only used when nMode is PUFO_DAT_CREATE. */
bool DatFile_Open(DatFile *pDat, const char *pszPath, DatMode nMode, int nEntryCount);
void DatFile_Close(DatFile *pDat);

/* true if the path exists on disk and is not a directory. */
bool DatFile_Exists(const char *pszPath);

/* --- writing (CREATE or REWRITE) --- */

bool DatFile_BeginChunk(DatFile *pDat, const char *pszTag);
bool DatFile_Write(DatFile *pDat, const void *pBuf, u32 nSize);
void DatFile_FlushWrite(DatFile *pDat);

/* --- reading (READ) --- */

bool DatFile_FindChunk(DatFile *pDat, const char *pszTag);
bool DatFile_Read(DatFile *pDat, void *pBuf, u32 nSize);
bool DatFile_ChunkPresent(const DatFile *pDat);

#endif /* PUFO_DAT_H */
