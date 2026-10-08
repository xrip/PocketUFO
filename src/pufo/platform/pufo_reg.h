/* pufo_reg.h - registry string persistence for the option block.
 *
 * Evidence: Reg_SetString @ 0x9ed0c, Reg_GetString @ 0x9edbc.
 *
 * Both use HKEY_CURRENT_USER and the subkey in SubKey @ 0xac680
 * ("Software\\PocketUFO"). Values are written as REG_SZ even when they hold
 * numbers, because the callers itoa() them first (see Options_SaveToRegistry
 * @ 0x114d8). The original converts names with mbstowcs into the fixed globals
 * ValueName @ 0x187750 and Data @ 0x187328; those are global scratch buffers,
 * which is why Reg_GetString returns a pointer to a shared buffer rather than
 * filling a caller buffer.
 */
#ifndef PUFO_REG_H
#define PUFO_REG_H

#include "pufo_common.h"

/* The original's Data @ 0x187328 is 256 bytes; Reg_GetString rejects any
 * stored value longer than 0xFF. */
#define PUFO_REG_VALUE_MAX 256

/* Writes pszValue under HKEY_CURRENT_USER\Software\PocketUFO\pszName as REG_SZ.
 * Returns true on success. */
bool Reg_SetString(const char *pszName, const char *pszValue);

/* Reads pszName. On any failure (missing, wrong type, too long) returns
 * pszDefault, exactly like Reg_GetString @ 0x9edbc. The returned pointer refers
 * to an internal static buffer that is overwritten by the next call. */
const char *Reg_GetString(const char *pszName, const char *pszDefault);

#endif /* PUFO_REG_H */
