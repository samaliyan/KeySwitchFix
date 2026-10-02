/*
 * Setup's language-pack step, run on Linux against tests/win32sim with an
 * in-memory folder: valid packs are written, unsafe or damaged bundle
 * entries are refused, packs an earlier version left behind are removed,
 * and the uninstaller clears the folder.
 */
#define wWinMain installer_wWinMain
#define WinMain installer_WinMain
#include "../src/installer.c"
#undef wWinMain
#undef WinMain

#include <stdlib.h>

/* ---- A tiny in-memory file system ---------------------------------------- */

typedef struct SIM_FILE {
    wchar_t path[MAX_PATH];
    unsigned char *data;
    DWORD size;
    int used;
} SIM_FILE;
static SIM_FILE sim_files[64];
static int sim_directory_exists;
static int sim_fail_writes;

static SIM_FILE *sim_lookup(const wchar_t *path) {
    int i;
    for (i = 0; i < 64; ++i)
        if (sim_files[i].used && _wcsicmp(sim_files[i].path, path) == 0) return &sim_files[i];
    return NULL;
}

static SIM_FILE *sim_create(const wchar_t *path) {
    SIM_FILE *file = sim_lookup(path);
    int i;
    if (file) {
        free(file->data);
        file->data = NULL;
        file->size = 0;
        return file;
    }
    for (i = 0; i < 64; ++i) {
        if (!sim_files[i].used) {
            memset(&sim_files[i], 0, sizeof(sim_files[i]));
            sim_files[i].used = 1;
            wcsncpy(sim_files[i].path, path, MAX_PATH - 1);
            return &sim_files[i];
        }
    }
    return NULL;
}

BOOL CreateDirectoryW(LPCWSTR path, SECURITY_ATTRIBUTES *security) {
    (void)path; (void)security;
    if (sim_directory_exists) { SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
    sim_directory_exists = 1;
    return TRUE;
}

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *security,
                   DWORD disposition, DWORD flags, HANDLE templ) {
    (void)access; (void)share; (void)security; (void)disposition; (void)flags; (void)templ;
    if (sim_fail_writes) return INVALID_HANDLE_VALUE;
    return (HANDLE)sim_create(path);
}
BOOL WriteFile(HANDLE handle, LPCVOID bytes, DWORD size, LPDWORD written, void *overlapped) {
    SIM_FILE *file = (SIM_FILE *)handle;
    (void)overlapped;
    file->data = (unsigned char *)malloc(size ? size : 1);
    memcpy(file->data, bytes, size);
    file->size = size;
    *written = size;
    return TRUE;
}
BOOL FlushFileBuffers(HANDLE handle) { (void)handle; return TRUE; }
BOOL CloseHandle(HANDLE handle) { (void)handle; return TRUE; }
BOOL DeleteFileW(LPCWSTR path) {
    SIM_FILE *file = sim_lookup(path);
    if (!file) return FALSE;
    free(file->data);
    file->used = 0;
    return TRUE;
}
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    SIM_FILE *source = sim_lookup(from);
    SIM_FILE *target;
    (void)flags;
    if (!source) return FALSE;
    target = sim_lookup(to);
    if (target) { free(target->data); target->used = 0; }
    wcsncpy(source->path, to, MAX_PATH - 1);
    return TRUE;
}
BOOL RemoveDirectoryW(LPCWSTR path) {
    int i;
    (void)path;
    for (i = 0; i < 64; ++i) if (sim_files[i].used) return FALSE;
    sim_directory_exists = 0;
    return TRUE;
}

/* Patterns are "<folder>\\*<suffix>". */
typedef struct SIM_SEARCH { int next; wchar_t folder[MAX_PATH]; wchar_t suffix[32]; } SIM_SEARCH;
static SIM_SEARCH sim_search;
static int sim_next_file(WIN32_FIND_DATAW *found) {
    while (sim_search.next < 64) {
        SIM_FILE *file = &sim_files[sim_search.next++];
        const wchar_t *base;
        size_t length;
        size_t suffix = wcslen(sim_search.suffix);
        size_t folder = wcslen(sim_search.folder);
        if (!file->used) continue;
        length = wcslen(file->path);
        if (length < suffix || wcscmp(file->path + length - suffix, sim_search.suffix) != 0) continue;
        if (wcsncmp(file->path, sim_search.folder, folder) != 0 || wcschr(file->path + folder, L'\\')) continue;
        base = wcsrchr(file->path, L'\\');
        memset(found, 0, sizeof(*found));
        wcsncpy(found->cFileName, base ? base + 1 : file->path, 259);
        return 1;
    }
    return 0;
}
HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *found) {
    const wchar_t *star = wcsrchr(pattern, L'*');
    sim_search.next = 0;
    if (!star) return INVALID_HANDLE_VALUE;
    wcsncpy(sim_search.folder, pattern, (size_t)(star - pattern));
    sim_search.folder[star - pattern] = 0;
    wcsncpy(sim_search.suffix, star + 1, 31);
    return sim_next_file(found) ? (HANDLE)&sim_search : INVALID_HANDLE_VALUE;
}
BOOL FindNextFileW(HANDLE search, WIN32_FIND_DATAW *found) { (void)search; return sim_next_file(found); }
BOOL FindClose(HANDLE search) { (void)search; return TRUE; }

/* ---- The bundle resource -------------------------------------------------- */

static unsigned char sim_bundle[4096];
static DWORD sim_bundle_size;
static int sim_has_bundle;

HRSRC FindResourceW(HMODULE module, LPCWSTR name, LPCWSTR type) {
    (void)module; (void)type;
    return sim_has_bundle && (uintptr_t)name == IDR_LANGUAGE_BUNDLE ? (HRSRC)sim_bundle : NULL;
}
DWORD SizeofResource(HMODULE module, HRSRC resource) { (void)module; (void)resource; return sim_bundle_size; }
HGLOBAL LoadResource(HMODULE module, HRSRC resource) { (void)module; return (HGLOBAL)resource; }
LPVOID LockResource(HGLOBAL loaded) { return loaded; }

static void put_u32(unsigned char *at, unsigned value) {
    at[0] = (unsigned char)value; at[1] = (unsigned char)(value >> 8);
    at[2] = (unsigned char)(value >> 16); at[3] = (unsigned char)(value >> 24);
}

/* names[i] with contents "<i>" repeated `size` times. */
static void make_bundle(int count, const char *const *names, unsigned size) {
    unsigned offset = 8u + (unsigned)count * 32u;
    int i;
    memset(sim_bundle, 0, sizeof(sim_bundle));
    memcpy(sim_bundle, "KSLB", 4);
    put_u32(sim_bundle + 4, (unsigned)count);
    for (i = 0; i < count; ++i) {
        unsigned char *entry = sim_bundle + 8 + i * 32;
        strncpy((char *)entry, names[i], 24);
        put_u32(entry + 24, offset);
        put_u32(entry + 28, size);
        memset(sim_bundle + offset, '0' + i, size);
        offset += size;
    }
    sim_bundle_size = offset;
    sim_has_bundle = 1;
}

static int failures;
static void expect(int condition, const char *what) {
    printf(condition ? "  ok  %s\n" : "FAIL: %s\n", what);
    if (!condition) ++failures;
}

static int file_count(void) {
    int i, n = 0;
    for (i = 0; i < 64; ++i) n += sim_files[i].used;
    return n;
}

int main(void) {
    static const char *const two[] = {"de.kslang", "ru.kslang"};
    static const char *const one[] = {"ru.kslang"};
    static const char *const unsafe[] = {"..\\x.kslang", "ru.kslang"};
    static const char *const bad_names[] = {"De.kslang", "ru.txt", "a/b.kslang", ".kslang"};
    wchar_t path[MAX_PATH];
    int i;
    g_paths_ok = 1;
    wcscpy(g_install_directory, L"C:\\Users\\u\\AppData\\Local\\Programs\\KeySwitchFix");
    wcscpy(g_languages_directory, L"C:\\Users\\u\\AppData\\Local\\Programs\\KeySwitchFix\\languages");

    sim_has_bundle = 0;
    expect(install_language_packs() == 1 && file_count() == 0, "a build without packs installs nothing and succeeds");
    make_bundle(0, NULL, 0);
    expect(install_language_packs() == 1 && file_count() == 0, "an empty bundle installs nothing");

    make_bundle(2, two, 100);
    {
        wchar_t leftover[MAX_PATH];
        swprintf(leftover, MAX_PATH, L"%ls\\xx.kslang.new", g_languages_directory);
        sim_create(leftover);   /* a staging file a crash left behind */
    }
    expect(install_language_packs() == 1, "two packs install");
    swprintf(path, MAX_PATH, L"%ls\\de.kslang", g_languages_directory);
    expect(sim_lookup(path) && sim_lookup(path)->size == 100 && sim_lookup(path)->data[0] == '0', "de.kslang written");
    expect(file_count() == 2, "no staging files are left");

    make_bundle(1, one, 50);
    expect(install_language_packs() == 1, "an upgrade with one pack installs");
    expect(!sim_lookup(path), "the pack the new version no longer has is removed");
    swprintf(path, MAX_PATH, L"%ls\\ru.kslang", g_languages_directory);
    expect(sim_lookup(path) && sim_lookup(path)->size == 50, "ru.kslang replaced");

    make_bundle(2, unsafe, 10);
    expect(install_language_packs() == 0, "an unsafe name is never written and the install reports a problem");
    expect(file_count() == 1 && sim_lookup(path), "and nothing else is removed then");

    for (i = 0; i < 4; ++i) {
        wchar_t name[24];
        unsigned char raw[24];
        memset(raw, 0, sizeof(raw));
        strncpy((char *)raw, bad_names[i], 24);
        expect(!pack_file_name(raw, name), bad_names[i]);
    }

    make_bundle(1, one, 50);
    put_u32(sim_bundle + 8 + 28, 5000);   /* longer than the bundle */
    expect(install_language_packs() == 0, "an entry past the end of the bundle is refused");
    put_u32(sim_bundle + 4, 1000);
    expect(install_language_packs() == 0, "an absurd count is refused");
    memcpy(sim_bundle, "XXXX", 4);
    expect(install_language_packs() == 0, "a damaged bundle is refused");

    make_bundle(1, one, 70);
    sim_fail_writes = 1;
    expect(install_language_packs() == 0, "a write failure is reported");
    expect(sim_lookup(path) && sim_lookup(path)->size == 10, "and the earlier pack stays");
    sim_fail_writes = 0;

    remove_language_packs(NULL, 0);
    expect(file_count() == 0 && !sim_directory_exists, "the uninstaller removes the packs and the folder");

    /* /purge: the packs the user added to the data folder go too. */
    {
        wchar_t user_pack[MAX_PATH];
        wchar_t setting[MAX_PATH];
        wcscpy(user_pack, L"C:\\Users\\u\\AppData\\Local\\KeySwitchFix\\languages\\ka.kslang");
        wcscpy(setting, L"C:\\Users\\u\\AppData\\Local\\KeySwitchFix\\settings.ini");
        sim_create(user_pack);
        sim_create(setting);
        purge_data_folder(L"C:\\Users\\u\\AppData\\Local\\KeySwitchFix");
        expect(file_count() == 0, "purge removes settings and the user's language packs");
    }

    if (failures) {
        printf("%d installer check(s) FAILED\n", failures);
        return 1;
    }
    printf("All Setup language-pack tests passed.\n");
    return 0;
}
