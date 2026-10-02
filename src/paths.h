#ifndef KEYSWITCHFIX_PATHS_H
#define KEYSWITCHFIX_PATHS_H

/* Path rules shared by the app and Setup, so both agree on where the user's
   data lives. Windows-only (header-only: each program has its own copy). */

#include <windows.h>
#include <string.h>
#include <wchar.h>

/* dir + separator + name into out (capacity in characters); 0 when it does
   not fit, and then out is empty rather than a truncated path. */
static int ks_path_join(wchar_t *out, size_t capacity, const wchar_t *dir, const wchar_t *separator,
                        const wchar_t *name) {
    size_t a = wcslen(dir), b = wcslen(separator), c = wcslen(name);
    if (!capacity) return 0;
    if (a + b + c + 1 > capacity) {
        out[0] = 0;
        return 0;
    }
    memmove(out, dir, a * sizeof(wchar_t));
    memmove(out + a, separator, b * sizeof(wchar_t));
    memmove(out + a + b, name, (c + 1) * sizeof(wchar_t));
    return 1;
}

/* The longest file name kept in the data folder, plus ".tmp". */
#define KS_DATA_NAME_ROOM (sizeof("\\personal-dictionary.txt.bak.tmp") - 1)

/* The data folder: %LOCALAPPDATA%\KeySwitchFix, or %TEMP%\KeySwitchFix when
   that path is too long for every file name to fit (a very long user name
   or a redirected profile). Returns 0 (and an empty string) when neither
   works: then nothing may be written at all, never to a relative path.
   *in_temp tells which one was chosen. */
static int ks_data_directory(wchar_t out[MAX_PATH], int *in_temp) {
    wchar_t base[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (in_temp) *in_temp = 0;
    if (length > 0 && length < MAX_PATH &&
        ks_path_join(out, MAX_PATH - KS_DATA_NAME_ROOM, base, L"\\", L"KeySwitchFix"))
        return 1;
    length = GetTempPathW(MAX_PATH, base);
    if (length > 0 && length < MAX_PATH &&
        ks_path_join(out, MAX_PATH - KS_DATA_NAME_ROOM, base,
                     base[length - 1] == L'\\' ? L"" : L"\\", L"KeySwitchFix")) {
        if (in_temp) *in_temp = 1;
        return 1;
    }
    out[0] = 0;
    return 0;
}

#endif
