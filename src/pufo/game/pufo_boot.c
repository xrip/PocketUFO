/* pufo_boot.c - startup asset loading.
 *
 * Evidence: Assets_LoadStartup @ 0x382d0, GameAssets_Shutdown @ 0x38e4c.
 *
 * The load order in Boot_LoadStartup follows the original exactly. The stride of
 * 0xCD (205) for the text records is confirmed from the disassembly at 0x38718,
 * where the source pointer advances by 205 while each destination group advances
 * by its own stride (20, 40, 20, 20, 20, 30).
 */
#include "game/pufo_boot.h"

#include <stdlib.h>
#include <string.h>

/* Assets_LoadStartup allocates the object blob and the text blob with
 * operator new; these two globals are what GameAssets_Shutdown frees
 * (unk_157190 @ 0x157190, unk_156A54 @ 0x156a54). */
static void Boot_FreeBlobs(BootAssets *pAssets)
{
    free(pAssets->pObjectBlob);
    pAssets->pObjectBlob = NULL;
    free(pAssets->pTextRecs);
    pAssets->pTextRecs = NULL;
    free(pAssets->apObjects);
    pAssets->apObjects = NULL;
}

bool Boot_LoadStartup(BootAssets *pAssets, const char *const *ppszArchivePaths)
{
    AssetReader rdr;
    u32         i;

    memset(pAssets, 0, sizeof(*pAssets));

    /* Step 0: open lang.dat (index 0) then main.dat (index 1). */
    if (!Asset_OpenArchives(ppszArchivePaths)) {
        return false;
    }

    /* Step 1: objects.dat -> ObjectRec array plus the parallel pointer array.
     * nObjectCount = size / 0xD0 (208), the record size. */
    AssetReader_Clear(&rdr);
    if (AssetReader_Find(&rdr, "objects.dat")) {
        u32 cb = AssetReader_Size(&rdr);

        pAssets->nObjectCount = (u16)(cb / sizeof(ObjectRecFull));
        pAssets->nObjectCap   = pAssets->nObjectCount;
        pAssets->pObjectBlob  = malloc(cb);
        if (pAssets->pObjectBlob == NULL) {
            goto fail;
        }
        if (!AssetReader_Read(&rdr, pAssets->pObjectBlob, cb)) {
            goto fail;
        }
        pAssets->apObjects = malloc(sizeof(ObjectRecFull *) * (pAssets->nObjectCount != 0
                                                              ? pAssets->nObjectCount : 1u));
        if (pAssets->apObjects == NULL) {
            goto fail;
        }
        for (i = 0; i < pAssets->nObjectCount; ++i) {
            pAssets->apObjects[i] = &pAssets->pObjectBlob[i];
        }
    }

    /* Step 2: fontsize.dat -> the 256-byte advance table. */
    FontMetrics_Load();

    /* Step 3: texts.dat -> (size - 2) / 200 records, expanded at stride 205.
     * The leading u16 is a format marker, not a record size, which is why the
     * subtraction is there. */
    AssetReader_Clear(&rdr);
    if (AssetReader_Find(&rdr, "texts.dat")) {
        u32 cb        = AssetReader_Size(&rdr);
        u32 nRecords  = (cb >= PUFO_TEXT_HDR_SIZE)
                      ? (cb - PUFO_TEXT_HDR_SIZE) / PUFO_TEXT_REC_SIZE_DISK : 0;
        u8  aHdr[PUFO_TEXT_HDR_SIZE];

        if (nRecords != 0) {
            /* Read and discard the marker; the original reads it into aTextRecs
             * and then overwrites from aTextRecs + 0, so the marker is consumed
             * either way. */
            if (!AssetReader_Read(&rdr, aHdr, PUFO_TEXT_HDR_SIZE)) {
                goto fail;
            }
            pAssets->pTextRecs = malloc((size_t)PUFO_TEXT_REC_CAPACITY * PUFO_TEXT_REC_STRIDE);
            if (pAssets->pTextRecs == NULL) {
                goto fail;
            }
            /* The original leaves the 5 slack bytes per record untouched. Zeroing
             * them is equivalent for string reads (each record is NUL-terminated
             * within its 200 bytes) and avoids reading uninitialised memory. The
             * records past the loaded count stay zero, as the original's zero-filled
             * .data does (see PUFO_TEXT_REC_CAPACITY). */
            memset(pAssets->pTextRecs, 0, (size_t)PUFO_TEXT_REC_CAPACITY * PUFO_TEXT_REC_STRIDE);
            if (nRecords > (u32)PUFO_TEXT_REC_CAPACITY) {
                nRecords = PUFO_TEXT_REC_CAPACITY; /* deviation: the original would run past
                                                    * aTextRecs into the Map block */
            }
            for (i = 0; i < nRecords; ++i) {
                if (!AssetReader_Read(&rdr, pAssets->pTextRecs + (size_t)i * PUFO_TEXT_REC_STRIDE,
                                      PUFO_TEXT_REC_SIZE_DISK)) {
                    break;
                }
            }
            pAssets->nTextRecCount = nRecords;

            /* Step 3b: slice the eight string tables out of the loaded blob, as
             * Assets_LoadStartup does immediately after reading it. */
            (void)TextTables_Extract(pAssets->pTextRecs,
                                     nRecords * PUFO_TEXT_REC_STRIDE);
        }
    }

    /* Step 4: the 240x320 16bpp surface that everything renders into. */
    pAssets->pSurface = malloc(sizeof(SoftSurf));
    if (pAssets->pSurface == NULL) {
        goto fail;
    }
    SoftSurf_ctor(pAssets->pSurface);
    if (SoftSurf_AllocPixels(pAssets->pSurface, PUFO_SURF_W, PUFO_SURF_H, PIXMODE_16BPP) == NULL) {
        goto fail;
    }
    g_pMainSurface = pAssets->pSurface;

    /* Step 5: class.pal into palette slot 0x12. */
    Palette_Load("class.pal", 0x12u);

    /* Step 6: the three startup pads at mode 18, origins zeroed. */
    {
        static const char *const apszPads[3] = { "cs0", "cs1", "cs2" };

        PadImage_ctor(&pAssets->aPadCs[0]);
        PadImage_ctor(&pAssets->aPadCs[1]);
        PadImage_ctor(&pAssets->aPadCs[2]);
        for (i = 0; i < 3; ++i) {
            (void)PadImage_Load(&pAssets->aPadCs[i], apszPads[i], 18u);
            pAssets->aPadCs[i].nOriginX = 0;
            pAssets->aPadCs[i].nOriginY = 0;
        }
    }

    return true;

fail:
    Boot_FreeBlobs(pAssets);
    /* The extracted string tables are copies, so they are released separately. */
    TextTables_Free();
    /* A partial load is torn down the same way the original's shutdown would. */
    g_fontMetrics.bLoaded = false;
    g_pMainSurface = NULL;
    if (pAssets->pSurface != NULL) {
        SoftSurf_dtor(pAssets->pSurface);
        free(pAssets->pSurface);
        pAssets->pSurface = NULL;
    }
    Asset_CloseArchives();
    return false;
}

/* GameAssets_Shutdown @ 0x38e4c. The original's order:
 *   free the object blob, free the text blob, close both archives and free their
 *   directories, release the three pads, destroy the surface.
 * Note the original frees only the two blobs it allocated with operator new and
 * never frees the archive directory arrays in this function (DatFile_dtor does
 * that elsewhere). Asset_CloseArchives frees them here, which is a strict
 * improvement and not observable to the game. */
void Boot_Shutdown(BootAssets *pAssets)
{
    if (pAssets == NULL) {
        return;
    }

    Boot_FreeBlobs(pAssets);
    TextTables_Free();

    for (int i = 0; i < 3; ++i) {
        PadImage_FreeData(&pAssets->aPadCs[i]);
    }

    if (pAssets->pSurface != NULL) {
        if (g_pMainSurface == pAssets->pSurface) {
            g_pMainSurface = NULL;
        }
        SoftSurf_dtor(pAssets->pSurface);
        free(pAssets->pSurface);
        pAssets->pSurface = NULL;
    }

    Asset_CloseArchives();
    pAssets->nObjectCount  = 0;
    pAssets->nObjectCap    = 0;
    pAssets->nTextRecCount = 0;
}

/* --- text table extraction (Assets_LoadStartup @ 0x382d0, step 3b) --- */

PufoTextTables g_textTables;

void TextTables_Free(void)
{
    free(g_textTables.pB);
    free(g_textTables.pC);
    free(g_textTables.pD);
    free(g_textTables.pA);
    free(g_textTables.pE);
    free(g_textTables.pF);
    free(g_textTables.pG);
    free(g_textTables.pH);
    memset(&g_textTables, 0, sizeof(g_textTables));
}

/* Copies <nCount> records of <nStride> bytes from <nSrc> in the blob, always
 * NUL-terminating inside the destination slot. */
static bool TextTables_CopyTable(const u8 *pBlob, u32 cbBlob, u32 nSrc,
                                 u32 nCount, u32 nStride, u8 *pDst)
{
    u32 i;

    if ((u32)nStride == 0 || (u32)nCount == 0) {
        return false;
    }
    /* The last record must start inside the blob. The original reads
     * nStride bytes regardless and can run past the end; the bound check here is
     * a deviation that turns an overread into an empty table. The widening to
     * 64-bit avoids an overflow in the multiply for large counts. */
    if (nSrc >= cbBlob
        || (unsigned long long)nSrc + (unsigned long long)nStride * nCount
               > (unsigned long long)cbBlob) {
        return false;
    }
    memset(pDst, 0, (size_t)nCount * nStride);
    for (i = 0; i < nCount; ++i) {
        const u8 *pRec = pBlob + nSrc + (size_t)i * nStride;
        u8       *pOut = pDst + (size_t)i * nStride;
        u32       j;

        /* The record is a C string; copy up to the stride, always terminating. */
        for (j = 0; j + 1u < nStride && pRec[j] != '\0'; ++j) {
            pOut[j] = pRec[j];
        }
        pOut[j] = '\0';
    }
    return true;
}

bool TextTables_Extract(const u8 *pBlob, u32 cbBlob)
{
    bool bOk = true;

    TextTables_Free();
    if (pBlob == NULL || cbBlob == 0) {
        return false;
    }

#define PUFO_ALLOC(dst, cnt, stride)                                            \
    do {                                                                        \
        (dst) = malloc((size_t)(cnt) * (stride));                               \
        if ((dst) == NULL) {                                                    \
            TextTables_Free();                                                  \
            return false;                                                       \
        }                                                                       \
    } while (0)

    PUFO_ALLOC(g_textTables.pB, PUFO_TXT_B_COUNT, PUFO_TXT_B_STRIDE);
    PUFO_ALLOC(g_textTables.pC, PUFO_TXT_C_COUNT, PUFO_TXT_C_STRIDE);
    PUFO_ALLOC(g_textTables.pD, PUFO_TXT_D_COUNT, PUFO_TXT_D_STRIDE);
    PUFO_ALLOC(g_textTables.pA, PUFO_TXT_A_COUNT, PUFO_TXT_A_STRIDE);
    PUFO_ALLOC(g_textTables.pE, PUFO_TXT_E_COUNT, PUFO_TXT_E_STRIDE);
    PUFO_ALLOC(g_textTables.pF, PUFO_TXT_F_COUNT, PUFO_TXT_F_STRIDE);
    PUFO_ALLOC(g_textTables.pG, 1u, PUFO_TXT_G_CAP);
    PUFO_ALLOC(g_textTables.pH, 1u, PUFO_TXT_H_CAP);
#undef PUFO_ALLOC

    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_A_SRC,
                                PUFO_TXT_A_COUNT, PUFO_TXT_A_STRIDE,
                                (u8 *)g_textTables.pA);
    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_B_SRC,
                                PUFO_TXT_B_COUNT, PUFO_TXT_B_STRIDE,
                                (u8 *)g_textTables.pB);
    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_C_SRC,
                                PUFO_TXT_C_COUNT, PUFO_TXT_C_STRIDE,
                                (u8 *)g_textTables.pC);
    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_D_SRC,
                                PUFO_TXT_D_COUNT, PUFO_TXT_D_STRIDE,
                                (u8 *)g_textTables.pD);
    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_E_SRC,
                                PUFO_TXT_E_COUNT, PUFO_TXT_E_STRIDE,
                                (u8 *)g_textTables.pE);
    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_F_SRC,
                                PUFO_TXT_F_COUNT, PUFO_TXT_F_STRIDE,
                                (u8 *)g_textTables.pF);
    /* G and H are single strings, copied with the same helper using count 1. */
    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_G_SRC, 1u,
                                PUFO_TXT_G_CAP, (u8 *)g_textTables.pG);
    bOk &= TextTables_CopyTable(pBlob, cbBlob, PUFO_TXT_H_SRC, 1u,
                                PUFO_TXT_H_CAP, (u8 *)g_textTables.pH);

    g_textTables.bLoaded = bOk;
    return bOk;
}

const char *TextTables_FirstName(u8 nIndex)
{
    if (!g_textTables.bLoaded || g_textTables.pD == NULL || nIndex >= PUFO_TXT_D_COUNT) {
        return "";
    }
    return g_textTables.pD[nIndex];
}

const char *TextTables_LastName(u8 nIndex)
{
    if (!g_textTables.bLoaded || g_textTables.pC == NULL || nIndex >= PUFO_TXT_C_COUNT) {
        return "";
    }
    return g_textTables.pC[nIndex];
}

const char *TextTables_XcomRank(u8 nRank)
{
    if (!g_textTables.bLoaded || g_textTables.pE == NULL || nRank >= PUFO_TXT_E_COUNT) {
        return "";
    }
    return g_textTables.pE[nRank];
}

const char *TextTables_AlienRank(u8 nRank)
{
    if (!g_textTables.bLoaded || g_textTables.pA == NULL || nRank >= PUFO_TXT_A_COUNT) {
        return "";
    }
    return g_textTables.pA[nRank];
}
