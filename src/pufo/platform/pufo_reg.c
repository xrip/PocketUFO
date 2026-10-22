/* pufo_reg.c - registry string persistence for the option block.
 *
 * Evidence: Reg_SetString @ 0x9ed0c, Reg_GetString @ 0x9edbc.
 */
#include "platform/pufo_reg.h"

#include <string.h>

#include <windows.h>

/* SubKey @ 0xac680. The IDA string is stored UTF-16, which is why it renders
 * garbled in the decompiler; the ASCII value is Software\PocketUFO. */
#define PUFO_REG_SUBKEY L"Software\\PocketUFO"

/* The original's ValueName @ 0x187750 is a 1000-wide wchar buffer and Data
 * @ 0x187328 is 256 bytes. Sized the same. */
static wchar_t s_wValueName[1000];
static char    s_szData[PUFO_REG_VALUE_MAX];

static bool Reg_ToWide(const char *psz, wchar_t *pwOut, size_t cchOut)
{
    return MultiByteToWideChar(CP_ACP, 0, psz, -1, pwOut, (int)cchOut) > 0;
}

/* Opens (creating if needed) HKCU\Software\PocketUFO. Both original functions
 * use RegCreateKeyExW with samDesired 0xF003F and no options. */
static bool Reg_OpenKey(HKEY *phKey)
{
    DWORD dwDisposition = 0;

    *phKey = NULL;
    return RegCreateKeyExW(HKEY_CURRENT_USER, PUFO_REG_SUBKEY, 0, NULL, 0,
                           0xF003Fu, NULL, phKey, &dwDisposition) == ERROR_SUCCESS;
}

/* Reg_SetString @ 0x9ed0c (176 bytes). RegCreateKeyExW(HKEY_CURRENT_USER, SubKey @ 0xac680,
 * samDesired 0xF003F built as `MOV R3,#0xF0000` + `ORR R3,R3,#0x3F` at 0x9ED2C/0x9ED38) at 0x9ED60;
 * on failure the branch at 0x9ED68 skips the write entirely and still calls RegCloseKey on the
 * handle that is still 0. On success: mbstowcs into the shared 1002-entry ValueName buffer @ 0x187750
 * with count 0x3E8 at 0x9ED78, strlen at 0x9ED80, cbData = strlen+1 at 0x9ED8C, and RegSetValueExW
 * with dwType 1 (REG_SZ) and the ANSI bytes as lpData at 0x9EDA0.
 *
 * What it RETURNS is the RegCloseKey result, not the write result: `BL RegCloseKey` at 0x9EDA8 leaves
 * its value in R0 and the epilogue at 0x9EDAC/0x9EDB0 hands it to the caller, so a successful write
 * answers 0 (ERROR_SUCCESS) and a failed key open answers an error code. The port answers a bool that
 * means "the write worked". A census makes that deviation safe: the 16 BL sites are all inside
 * Options_SaveToRegistry (0x11504..0x11678) and Options_SaveAudioSettings (0x6B834..0x6B8A0), and
 * walking forward from each one none reads R0 before overwriting it; Options_SaveToRegistry's own
 * single call site (Input_OnConfirm at 0x161F0) writes `MOV R0,#0` next. Nothing in the game can see
 * which of the two answers it is. */
bool Reg_SetString(const char *pszName, const char *pszValue)
{
    HKEY  hKey = NULL;
    DWORD cbValue;
    LONG  nResult;

    if (!Reg_OpenKey(&hKey)) {
        return false;
    }
    if (!Reg_ToWide(pszName, s_wValueName, 1000)) {
        (void)RegCloseKey(hKey);
        return false;
    }

    /* The original writes REG_SZ with cbData = strlen + 1, using the ANSI bytes
     * directly. That is what the shipped code does; reproduced as-is so the
     * stored values stay byte-compatible with the original's registry. */
    cbValue = (DWORD)strlen(pszValue) + 1u;
    nResult = RegSetValueExW(hKey, s_wValueName, 0, REG_SZ,
                             (const BYTE *)pszValue, cbValue);
    (void)RegCloseKey(hKey);
    return nResult == ERROR_SUCCESS;
}

/* Reg_GetString @ 0x9edbc (272 bytes). Type is pre-set to 1 at 0x9EDC4/0x9EDC8 and cbData to 0 at
 * 0x9EE10 before RegCreateKeyExW at 0x9EE1C; the failure branch at 0x9EE24 returns the caller's
 * default at 0x9EEC0 with no close, because there is no handle. Then mbstowcs at 0x9EE34, and a
 * PROBE query at 0x9EE54 with lpData = 0 that only asks for Type and cbData. Three answers send the
 * caller to the default (`MOV R4,R5` at 0x9EEAC): the query failed (BNE 0x9EE5C), Type != 1
 * (`CMP R3,#1` 0x9EE64 / `BNE` 0x9EE68), or cbData > 0xFF (`CMP R3,#0xFF` 0x9EE70 / `BHI` 0x9EE74).
 * Otherwise the result pointer is the global Data buffer @ 0x187328 (`LDR R4,=0x187328` at 0x9EE7C --
 * one buffer for the whole process, shared by every reader) and the real query at 0x9EE98 fills it;
 * its own failure is not checked, so a value that disappears between the probe and the read comes back
 * as whatever the buffer holds. Both paths join at 0x9EEB0, close the key at 0x9EEB4, and return R4 at
 * 0x9EEB8.
 *
 * The one buffer is safe only because of how it is used: all 12 BL sites are in
 * Map_InitAndLoadRegistry (0x226CC..0x22ACC) and each one dereferences R0 straight away (`LDRSB
 * R2,[R0]` then `ADD R0,R0,#1`, a byte walk over the string), so no two results are ever live at the
 * same time. A caller that kept two of them would see the second read overwrite the first.
 *
 * The room and the limit do not meet. Data @ 0x187328 has exactly 256 bytes -- the next named global,
 * paSoundSamples, is at 0x187428 -- but `BHI 0x9EEAC` rejects cbData strictly above 0xFF, so the
 * largest accepted value is 255 bytes. With Reg_SetString's strlen+1 (0x9ED8C) that is 0..254
 * characters, and the last byte of the buffer is never written. Measured through the real registry in
 * tests/test_boot.c: 254 characters read back whole, 255 are refused. The port adds one guard the
 * original has not: it forces a NUL at the end of its buffer, which matters only for a value written
 * by another program without one. */
const char *Reg_GetString(const char *pszName, const char *pszDefault)
{
    HKEY  hKey = NULL;
    DWORD dwType = REG_SZ;
    DWORD cbData = 0;
    LONG  nResult;

    if (!Reg_OpenKey(&hKey)) {
        return pszDefault;
    }
    if (!Reg_ToWide(pszName, s_wValueName, 1000)) {
        (void)RegCloseKey(hKey);
        return pszDefault;
    }

    nResult = RegQueryValueExW(hKey, s_wValueName, NULL, &dwType, NULL, &cbData);
    /* The original rejects a wrong type and any value > 0xFF bytes, then falls
     * back to the caller default. Reproduced exactly. */
    if (nResult != ERROR_SUCCESS || dwType != REG_SZ || cbData > 0xFFu) {
        (void)RegCloseKey(hKey);
        return pszDefault;
    }

    if (RegQueryValueExW(hKey, s_wValueName, NULL, &dwType,
                         (BYTE *)s_szData, &cbData) != ERROR_SUCCESS) {
        (void)RegCloseKey(hKey);
        return pszDefault;
    }
    /* Guard against a stored value that is not NUL-terminated. */
    s_szData[PUFO_REG_VALUE_MAX - 1] = '\0';
    (void)RegCloseKey(hKey);
    return s_szData;
}
