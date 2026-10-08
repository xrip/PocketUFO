/* pufo_str.c - small string helpers replacing deprecated CRT routines.
 *
 * Evidence: the originals are _strupr @ 0xa60e4 (COREDLL), strcpy/strcat from
 * the CRT, used throughout DatFile_*, AssetReader_*, and PadImage_Load.
 */
#include "util/pufo_str.h"

void pufo_strupr(char *psz)
{
    for (; *psz != '\0'; ++psz) {
        if (*psz >= 'a' && *psz <= 'z') {
            *psz = (char)(*psz - ('a' - 'A'));
        }
    }
}

void pufo_strcpy(char *pszDst, const char *pszSrc)
{
    while ((*pszDst++ = *pszSrc++) != '\0') {
        /* copy */
    }
}

void pufo_strncpy(char *pszDst, const char *pszSrc, size_t cchMax)
{
    size_t i;

    if (cchMax == 0) {
        return;
    }
    for (i = 0; i + 1u < cchMax && pszSrc[i] != '\0'; ++i) {
        pszDst[i] = pszSrc[i];
    }
    pszDst[i] = '\0';
}

bool pufo_strcat(char *pszDst, const char *pszSrc, size_t cchDstMax)
{
    size_t nLen = 0;
    size_t i;

    while (pszDst[nLen] != '\0') {
        ++nLen;
    }
    for (i = 0; pszSrc[i] != '\0'; ++i) {
        if (nLen + i + 1u >= cchDstMax) {
            return false;
        }
        pszDst[nLen + i] = pszSrc[i];
    }
    pszDst[nLen + i] = '\0';
    return true;
}
