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

/*
 * The profile functions (WritePrivateProfileStringW) write a file in the
 * ANSI code page unless it already starts with a UTF-16 byte-order mark; a
 * program name with letters outside that code page would be stored as
 * "????.exe". Before the first write, create the file with the mark, or
 * convert an existing ANSI file once. Best effort: on any failure the file
 * is left exactly as it was.
 */
static void ks_ensure_unicode_ini(const wchar_t *path) {
    HANDLE file;
    DWORD size;
    DWORD done = 0;
    char *bytes;
    wchar_t *wide;
    int characters;
    static const unsigned char mark[2] = {0xFF, 0xFE};
    if (!path || !*path) return;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) return;
        file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) return;
        WriteFile(file, mark, 2, &done, NULL);
        CloseHandle(file);
        return;
    }
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size > 1024u * 1024u) {
        CloseHandle(file);
        return;
    }
    bytes = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)size + 1);
    if (!bytes || (size && (!ReadFile(file, bytes, size, &done, NULL) || done != size))) {
        if (bytes) HeapFree(GetProcessHeap(), 0, bytes);
        CloseHandle(file);
        return;
    }
    CloseHandle(file);
    if (size >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) {
        HeapFree(GetProcessHeap(), 0, bytes);   /* already UTF-16 */
        return;
    }
    characters = size ? MultiByteToWideChar(CP_ACP, 0, bytes, (int)size, NULL, 0) : 0;
    wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, ((size_t)characters + 1) * sizeof(wchar_t));
    if (wide && (!size || MultiByteToWideChar(CP_ACP, 0, bytes, (int)size, wide, characters) == characters)) {
        wchar_t temporary[MAX_PATH + 8];
        if (ks_path_join(temporary, MAX_PATH + 8, path, L"", L".tmp")) {
            file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD body = (DWORD)((size_t)characters * sizeof(wchar_t));
                int ok = WriteFile(file, mark, 2, &done, NULL) && done == 2 &&
                         (!body || (WriteFile(file, wide, body, &done, NULL) && done == body));
                CloseHandle(file);
                if (!ok || !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING)) DeleteFileW(temporary);
            }
        }
    }
    if (wide) HeapFree(GetProcessHeap(), 0, wide);
    HeapFree(GetProcessHeap(), 0, bytes);
}

#endif
