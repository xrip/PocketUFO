/* pufo_soldiername.c - see pufo_soldiername.h for evidence and deviations. */
#include "game/pufo_soldiername.h"

#include <string.h>

#include "game/pufo_font.h"   /* Text_MeasureWidth */
#include "util/pufo_str.h"    /* pufo_strcat */

i32 SoldierName_Backspace(Soldier *pSoldier)
{
    size_t nLen;

    if (pSoldier == NULL) {
        return 0;
    }
    nLen = strlen(pSoldier->szName);
    if (nLen == 0) {
        return 0;
    }
    /* The original writes the byte at &uMissions + nLen + 1, which is
     * szName[nLen - 1] because szName starts one byte after uMissions. */
    pSoldier->szName[nLen - 1] = '\0';
    return (i32)nLen;
}

void SoldierName_CopyToEdit(const Soldier *pSoldier, char *pszEditBuf)
{
    if (pSoldier == NULL || pszEditBuf == NULL) {
        return;
    }
    /* do { c = *src++; *dst++ = c; } while (c != 0); */
    memcpy(pszEditBuf, pSoldier->szName, strlen(pSoldier->szName) + 1);
}

u8 SoldierName_GlyphAt(i32 x, i32 y)
{
    return (u8)((x - 12) / 14 + 16 * ((y - 80) / 13 + 2));
}

bool SoldierName_AppendGlyphAt(Soldier *pSoldier, i32 x, i32 y)
{
    char szGlyph[2];

    if (pSoldier == NULL) {
        return false;
    }
    if (strlen(pSoldier->szName) >= PUFO_NAME_EDIT_MAX) {
        return false;
    }
    szGlyph[0] = (char)SoldierName_GlyphAt(x, y);
    szGlyph[1] = '\0';
    /* The original stores the glyph byte then zeroes the following byte, so the
     * scratch buffer it measures is exactly this one-character string. */
    if (Text_MeasureWidth(szGlyph) == 0) {
        return false;
    }
    /* The original calls strcat with no capacity check; szName is 40 bytes, so
     * the append is refused here when the result plus its terminator would not
     * fit (see the header for the deliberate one-byte-overrun deviation). */
    return pufo_strcat(pSoldier->szName, szGlyph, sizeof(pSoldier->szName));
}
