/* pufo_str.h - small string helpers replacing deprecated CRT routines.
 *
 * The original uses classic CRT calls (strcpy, strcat, _strupr, mbstowcs).
 * Ported code keeps that vintage style, but the MSVC CRT marks the copying
 * routines deprecated and /WX rejects them. These helpers give the same
 * behaviour as small ASCII implementations, so /WX stays on project-wide
 * instead of being disabled.
 *
 * All of them are ASCII-only, which is safe here: archive tags, directory
 * names, .pad/.pal/.wav names, and ini-style value names are all ASCII.
 */
#ifndef PUFO_STR_H
#define PUFO_STR_H

#include "pufo_common.h"

/* Upper-cases psz in place. Equivalent to _strupr. */
void pufo_strupr(char *psz);

/* Equivalent to strcpy. Caller guarantees the destination is large enough,
 * exactly as the original does. */
void pufo_strcpy(char *pszDst, const char *pszSrc);

/* Equivalent to strncpy, but always NUL-terminates at cchMax - 1. */
void pufo_strncpy(char *pszDst, const char *pszSrc, size_t cchMax);

/* Equivalent to strcat, with an explicit capacity. Returns false and leaves the
 * destination unchanged when the result would not fit. The original has no
 * capacity checks; the extra parameter is the one place the port is stricter,
 * because overflowing a fixed stack buffer is not behaviour worth preserving. */
bool pufo_strcat(char *pszDst, const char *pszSrc, size_t cchDstMax);

#endif /* PUFO_STR_H */
