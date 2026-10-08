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
