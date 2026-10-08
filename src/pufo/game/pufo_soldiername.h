/* pufo_soldiername.h - soldier name editing used by the soldier screen.
 *
 * Evidence: Map_BackspaceSelectedName @ 0x63138,
 *           SoldierScreen_CopyNameToEdit @ 0x6318c,
 *           SoldierScreen_AppendNameGlyph @ 0x63208.
 *
 * All three original functions reach the selected soldier through
 * pCtxBase.apSoldiers[gUiRootPanel.apLines[187]], so the port takes the Soldier
 * directly instead of reading those globals.
 *
 * Mutable target deviation: the original works on the live soldier's szName and
 * carries its edit buffer in gUiRootPanel scratch (anLineAux[105], 40 bytes),
 * where SoldierScreen_CopyNameToEdit also clears a one-byte scratch flag at
 * byte_186B88. The port passes explicit pointers/buffers and leaves that flag to
 * the caller.
 */
#ifndef PUFO_SOLDIERNAME_H
#define PUFO_SOLDIERNAME_H

#include "game/pufo_unit.h"
#include "pufo_common.h"

/* The original appends while strlen(szName) < 0x28 (40), which lets a 40th
 * character overwrite the string terminator one byte past the 40-byte szName
 * field and into bRankType. The port instead requires the result plus its
 * terminator to fit, so the maximum name length is PUFO_NAME_EDIT_MAX - 1.
 * This one-byte overrun is the only behaviour deliberately not reproduced. */
#define PUFO_NAME_EDIT_MAX 40

/* Map_BackspaceSelectedName @ 0x63138. Returns the original length and clears
 * the last character. A zero-length name is left untouched and returns 0. */
i32 SoldierName_Backspace(Soldier *pSoldier);

/* SoldierScreen_CopyNameToEdit @ 0x6318c. Copies the soldier's szName into
 * pszEditBuf (including the terminator). pszEditBuf must hold at least
 * PUFO_NAME_EDIT_MAX bytes. */
void SoldierName_CopyToEdit(const Soldier *pSoldier, char *pszEditBuf);

/* The character code produced by the name-entry grid from a click at (x, y):
 * (x - 12) / 14 + 16 * ((y - 80) / 13 + 2), using truncating integer division
 * exactly as the original. Exposed for testing. */
u8 SoldierName_GlyphAt(i32 x, i32 y);

/* SoldierScreen_AppendNameGlyph @ 0x63208. Appends the character for the grid
 * cell at (x, y) to the soldier's szName when the result including its
 * terminator fits in the 40-byte field and the glyph has non-zero measured
 * width. Returns true when a character was appended. */
bool SoldierName_AppendGlyphAt(Soldier *pSoldier, i32 x, i32 y);

#endif /* PUFO_SOLDIERNAME_H */
