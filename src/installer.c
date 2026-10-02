#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

#include "../resources/resource.h"
#include "defaults.h"
#include "paths.h"

#define APP_NAME L"KeySwitchFix"
#define APP_VERSION L"4.0.1"
#define APP_WINDOW_CLASS L"KeySwitchFix.MainWindow.2"
#define WM_APP_EXIT (WM_APP + 9)

static HINSTANCE g_instance;
static wchar_t g_install_directory[MAX_PATH];
static wchar_t g_app_path[MAX_PATH];
static wchar_t g_uninstaller_path[MAX_PATH];
static wchar_t g_data_directory[MAX_PATH];
static wchar_t g_settings_path[MAX_PATH];
static wchar_t g_desktop_shortcut[MAX_PATH];
static wchar_t g_start_menu_shortcut[MAX_PATH];
static int g_install_complete;
static int g_launch_after_finish;
static int g_shortcuts_created;
static int g_was_running;
static int g_paths_ok;
static wchar_t g_install_problems[256];   /* parts of the install that failed */
static wchar_t g_languages_directory[MAX_PATH];   /* <install folder>\languages */

static void set_registry_string(HKEY key, const wchar_t *name, const wchar_t *value) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                   (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

/* Every path is checked: a very long %LOCALAPPDATA% must stop Setup with a
   clear message rather than install to a truncated or wrong folder. The
   data folder follows the same rule as the app (paths.h). */
static void build_paths(void) {
    wchar_t local_app_data[MAX_PATH];
    wchar_t shell_path[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    g_paths_ok = length > 0 && length < MAX_PATH &&
                 ks_path_join(g_install_directory, MAX_PATH - 40, local_app_data, L"\\",
                              L"Programs\\KeySwitchFix") &&
                 ks_path_join(g_app_path, MAX_PATH, g_install_directory, L"\\", L"KeySwitchFix.exe") &&
                 ks_path_join(g_uninstaller_path, MAX_PATH, g_install_directory, L"\\",
                              L"Uninstall KeySwitchFix.exe") &&
                 ks_path_join(g_languages_directory, MAX_PATH - 40, g_install_directory, L"\\",
                              L"languages") &&
                 ks_data_directory(g_data_directory, NULL) &&
                 ks_path_join(g_settings_path, MAX_PATH, g_data_directory, L"\\", L"settings.ini");
    g_desktop_shortcut[0] = 0;
    g_start_menu_shortcut[0] = 0;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY | CSIDL_FLAG_CREATE,
                                   NULL, SHGFP_TYPE_CURRENT, shell_path)))
        ks_path_join(g_desktop_shortcut, MAX_PATH, shell_path, L"\\", L"KeySwitchFix.lnk");
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS | CSIDL_FLAG_CREATE,
                                   NULL, SHGFP_TYPE_CURRENT, shell_path)))
        ks_path_join(g_start_menu_shortcut, MAX_PATH, shell_path, L"\\", L"KeySwitchFix.lnk");
}

static void ensure_directories(void) {
    wchar_t programs[MAX_PATH];
    wchar_t *slash;
    wcscpy(programs, g_install_directory);
    slash = wcsrchr(programs, L'\\');
    if (slash) *slash = 0;
    CreateDirectoryW(programs, NULL);
    CreateDirectoryW(g_install_directory, NULL);
    CreateDirectoryW(g_data_directory, NULL);
}

static int process_path_equals(DWORD process_id, const wchar_t *expected_path) {
    HANDLE process;
    wchar_t path[MAX_PATH];
    DWORD length = MAX_PATH;
    int matches = 0;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return 0;
    if (QueryFullProcessImageNameW(process, 0, path, &length))
        matches = _wcsicmp(path, expected_path) == 0;
    CloseHandle(process);
    return matches;
}

static int stop_running_app(void) {
    HWND window = FindWindowW(APP_WINDOW_CLASS, NULL);
    int attempt;
    int was_running = 0;
    if (window) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (process_path_equals(process_id, g_app_path)) {
            /* The app saves its statistics and writing memory on exit:
               give it up to five seconds before anything is forced. */
            HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
            was_running = 1;
            PostMessageW(window, WM_APP_EXIT, 0, 0);
            if (process) {
                WaitForSingleObject(process, 5000);
                CloseHandle(process);
            }
        }
    }
    for (attempt = 0; attempt < 20 && FindWindowW(APP_WINDOW_CLASS, NULL); ++attempt) Sleep(50);

    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W entry;
        if (snapshot == INVALID_HANDLE_VALUE) return was_running;
        ZeroMemory(&entry, sizeof(entry));
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID != GetCurrentProcessId() &&
                    _wcsicmp(entry.szExeFile, L"KeySwitchFix.exe") == 0 &&
                    process_path_equals(entry.th32ProcessID, g_app_path)) {
                    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
                    was_running = 1;
                    if (process) {
                        if (WaitForSingleObject(process, 2000) == WAIT_TIMEOUT) TerminateProcess(process, 0);
                        CloseHandle(process);
                    }
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return was_running;
}

/* Writes an embedded file next to its final place as "<target>.new".
   Nothing installed is touched until every file is staged. */
static int stage_resource_file(int resource_id, const wchar_t *target) {
    HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    HGLOBAL loaded;
    const void *bytes;
    DWORD size;
    HANDLE file;
    DWORD written = 0;
    wchar_t temporary[MAX_PATH];
    if (!resource) return 0;
    size = SizeofResource(g_instance, resource);
    loaded = LoadResource(g_instance, resource);
    bytes = loaded ? LockResource(loaded) : NULL;
    if (!bytes || !size) return 0;
    if (!ks_path_join(temporary, MAX_PATH, target, L"", L".new")) return 0;
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!WriteFile(file, bytes, size, &written, NULL) || written != size) {
        CloseHandle(file);
        DeleteFileW(temporary);
        return 0;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
    return 1;
}

/* Swaps staged files in. The previous files are kept as "<target>.old"
   until all swaps succeeded; on any failure every target is put back, so
   an upgrade never leaves a half-installed or missing program. */
static int commit_staged_files(const wchar_t *const *targets, int count) {
    wchar_t staged[MAX_PATH];
    wchar_t old[MAX_PATH];
    int done;
    int i;
    for (done = 0; done < count; ++done) {
        if (!ks_path_join(staged, MAX_PATH, targets[done], L"", L".new") ||
            !ks_path_join(old, MAX_PATH, targets[done], L"", L".old")) break;
        DeleteFileW(old);
        if (GetFileAttributesW(targets[done]) != INVALID_FILE_ATTRIBUTES &&
            !MoveFileExW(targets[done], old, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
        if (!MoveFileExW(staged, targets[done], MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            MoveFileExW(old, targets[done], MOVEFILE_REPLACE_EXISTING);
            break;
        }
    }
    if (done == count) {
        for (i = 0; i < count; ++i) {
            if (ks_path_join(old, MAX_PATH, targets[i], L"", L".old")) DeleteFileW(old);
        }
        return 1;
    }
    for (i = done - 1; i >= 0; --i) {
        if (!ks_path_join(old, MAX_PATH, targets[i], L"", L".old")) continue;
        if (GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES)
            MoveFileExW(old, targets[i], MOVEFILE_REPLACE_EXISTING);
        else
            DeleteFileW(targets[i]);   /* there was no previous version */
    }
    for (i = 0; i < count; ++i)
        if (ks_path_join(staged, MAX_PATH, targets[i], L"", L".new")) DeleteFileW(staged);
    return 0;
}

static void discard_staged_files(const wchar_t *const *targets, int count) {
    wchar_t staged[MAX_PATH];
    int i;
    for (i = 0; i < count; ++i)
        if (ks_path_join(staged, MAX_PATH, targets[i], L"", L".new")) DeleteFileW(staged);
}

/* The copy that was closed for the update runs again after a failure. */
static void restart_previous_copy(void) {
    if (g_was_running && GetFileAttributesW(g_app_path) != INVALID_FILE_ATTRIBUTES)
        ShellExecuteW(NULL, L"open", g_app_path, NULL, g_install_directory, SW_SHOWNORMAL);
}

/* ---- Language packs -------------------------------------------------------
 * Setup carries every pack in one resource ("KSLB", u32 count, then
 * { char name[24], u32 offset, u32 size } per pack). They are written to
 * <install folder>\languages, where the app lists them. A build without
 * packs (no wordfreq) has an empty bundle, and the app offers English and
 * Persian only.
 */

static unsigned read_u32(const unsigned char *data) {
    return (unsigned)data[0] | ((unsigned)data[1] << 8) | ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

/* "ru.kslang": lower-case letters, digits and '-' before the extension,
   nothing that could leave the folder. */
static int pack_file_name(const unsigned char *raw, wchar_t *name) {
    int i;
    int length = 0;
    while (length < 24 && raw[length]) ++length;
    if (length < 8 || length >= 24 || memcmp(raw + length - 7, ".kslang", 7) != 0) return 0;
    for (i = 0; i < length - 7; ++i) {
        unsigned char c = raw[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    for (i = 0; i < length; ++i) name[i] = (wchar_t)raw[i];
    name[length] = 0;
    return 1;
}

static int write_whole_file(const wchar_t *path, const void *bytes, DWORD size) {
    DWORD written = 0;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    int ok;
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(file, bytes, size, &written, NULL) && written == size;
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) DeleteFileW(path);
    return ok;
}

/* Deletes the packs (and staging leftovers, "*.kslang.new") in `folder`
   that `keep` does not name, all of them when keep is NULL, and then the
   folder when nothing else is in it. */
static void remove_packs_in(const wchar_t *folder, const wchar_t *const *keep, int keep_count) {
    static const wchar_t *const patterns[] = {L"*.kslang", L"*.kslang.new"};
    size_t p;
    for (p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        wchar_t pattern[MAX_PATH];
        WIN32_FIND_DATAW found;
        HANDLE search;
        if (!ks_path_join(pattern, MAX_PATH, folder, L"\\", patterns[p])) return;
        search = FindFirstFileW(pattern, &found);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            wchar_t path[MAX_PATH];
            int i;
            int kept = 0;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            for (i = 0; i < keep_count && !kept; ++i) kept = _wcsicmp(found.cFileName, keep[i]) == 0;
            if (!kept && ks_path_join(path, MAX_PATH, folder, L"\\", found.cFileName))
                DeleteFileW(path);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    if (!keep_count) RemoveDirectoryW(folder);
}

/* The packs Setup installed next to the program. Packs of your own belong
   in the data folder (LANGUAGE_PACKS.md): an upgrade replaces this one. */
static void remove_language_packs(const wchar_t *const *keep, int keep_count) {
    if (!g_paths_ok) return;
    remove_packs_in(g_languages_directory, keep, keep_count);
}

/* Returns 0 when a pack could not be written; the program itself is
   installed by then and works with English and Persian. */
static int install_language_packs(void) {
    HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(IDR_LANGUAGE_BUNDLE), RT_RCDATA);
    HGLOBAL loaded;
    const unsigned char *bundle;
    DWORD size;
    unsigned count;
    unsigned i;
    int ok = 1;
    static wchar_t names[64][24];
    const wchar_t *keep[64];
    int kept = 0;
    if (!resource) return 1;   /* a build without packs */
    size = SizeofResource(g_instance, resource);
    loaded = LoadResource(g_instance, resource);
    bundle = loaded ? (const unsigned char *)LockResource(loaded) : NULL;
    if (!bundle || size < 8 || memcmp(bundle, "KSLB", 4) != 0) return 0;
    count = read_u32(bundle + 4);
    if (count > 64 || 8u + count * 32u > size) return 0;
    if (count == 0) return 1;
    if (!CreateDirectoryW(g_languages_directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    for (i = 0; i < count; ++i) {
        const unsigned char *entry = bundle + 8 + i * 32u;
        unsigned offset = read_u32(entry + 24);
        unsigned length = read_u32(entry + 28);
        wchar_t path[MAX_PATH];
        wchar_t staged[MAX_PATH];
        if (!pack_file_name(entry, names[kept]) || offset > size || length > size - offset || !length ||
            !ks_path_join(path, MAX_PATH, g_languages_directory, L"\\", names[kept]) ||
            !ks_path_join(staged, MAX_PATH, path, L"", L".new")) {
            ok = 0;
            continue;
        }
        if (!write_whole_file(staged, bundle + offset, length) ||
            !MoveFileExW(staged, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(staged);
            ok = 0;
            /* An older copy of this pack that is still there stays in use. */
        }
        keep[kept] = names[kept];
        ++kept;
    }
    /* Packs an earlier version installed that this one no longer has. */
    if (ok) remove_language_packs(keep, kept);
    return ok;
}

static int create_shortcut(const wchar_t *shortcut_path) {
    IShellLinkW *link = NULL;
    IPersistFile *persist = NULL;
    HRESULT initialized;
    HRESULT result;
    int should_uninitialize;
    if (!shortcut_path || !*shortcut_path) return 0;

    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    should_uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return 0;

    result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                              &IID_IShellLinkW, (void **)&link);
    if (SUCCEEDED(result)) result = IShellLinkW_SetPath(link, g_app_path);
    if (SUCCEEDED(result)) result = IShellLinkW_SetArguments(link, L"--show");
    if (SUCCEEDED(result)) result = IShellLinkW_SetWorkingDirectory(link, g_install_directory);
    if (SUCCEEDED(result)) result = IShellLinkW_SetDescription(link,
                                                               L"Open KeySwitchFix");
    if (SUCCEEDED(result)) result = IShellLinkW_SetIconLocation(link, g_app_path, 0);
    if (SUCCEEDED(result))
        result = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (SUCCEEDED(result)) result = IPersistFile_Save(persist, shortcut_path, TRUE);

    if (persist) IPersistFile_Release(persist);
    if (link) IShellLinkW_Release(link);
    if (should_uninitialize) CoUninitialize();
    return SUCCEEDED(result);
}

static void remove_shortcuts(void) {
    if (g_desktop_shortcut[0]) DeleteFileW(g_desktop_shortcut);
    if (g_start_menu_shortcut[0]) DeleteFileW(g_start_menu_shortcut);
}

static int update_startup(int enabled) {
    HKEY key;
    wchar_t command[MAX_PATH + 8];
    int ok;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return 0;
    if (enabled) {
        swprintf(command, MAX_PATH + 8, L"\"%ls\"", g_app_path);
        ok = RegSetValueExW(key, APP_NAME, 0, REG_SZ, (const BYTE *)command,
                            (DWORD)((wcslen(command) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LONG result = RegDeleteValueW(key, APP_NAME);
        ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

/* An upgrade keeps the user's earlier choice: the Run entry exists only when
   "Start with Windows" was on. A fresh install defaults to on. */
static int startup_default(void) {
    HKEY key;
    int present = 0;
    if (GetFileAttributesW(g_settings_path) == INVALID_FILE_ATTRIBUTES) return 1;
    /* Kept settings (an uninstall that kept the data, or an upgrade) hold
       the user's choice; the Run entry is only a fallback. */
    {
        int saved = (int)GetPrivateProfileIntW(L"General", L"StartWithWindows", -1, g_settings_path);
        if (saved == 0 || saved == 1) return saved;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        present = RegQueryValueExW(key, APP_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    return present;
}

static int write_initial_settings(int startup) {
    wchar_t number[8];
    int ok = 1;
    int settings_existed = GetFileAttributesW(g_settings_path) != INVALID_FILE_ATTRIBUTES;
    /* A completed install must start in a working state, even when an older
       settings file had automatic correction paused. */
    ok &= WritePrivateProfileStringW(L"General", L"Enabled", L"1", g_settings_path) != 0;
    if (!settings_existed) {
        ok &= WritePrivateProfileStringW(L"General", L"Sensitivity", L"1", g_settings_path) != 0;
        ok &= WritePrivateProfileStringW(L"General", L"ExcludedProcesses", KS_DEFAULT_EXCLUDED,
                                         g_settings_path) != 0;
    }
    swprintf(number, 8, L"%d", startup ? 1 : 0);
    ok &= WritePrivateProfileStringW(L"General", L"StartWithWindows", number, g_settings_path) != 0;
    return ok;
}

static int register_uninstaller(void) {
    HKEY key;
    int ok;
    wchar_t uninstall_command[MAX_PATH + 32];
    wchar_t quiet_command[MAX_PATH + 40];
    DWORD one = 1;
    DWORD size_kb = 4400;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KeySwitchFix",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return 0;
    swprintf(uninstall_command, MAX_PATH + 32, L"\"%ls\"", g_uninstaller_path);
    swprintf(quiet_command, MAX_PATH + 40, L"\"%ls\" /silent", g_uninstaller_path);
    ok = RegSetValueExW(key, L"DisplayName", 0, REG_SZ, (const BYTE *)L"KeySwitchFix",
                        (DWORD)sizeof(L"KeySwitchFix")) == ERROR_SUCCESS;
    set_registry_string(key, L"DisplayVersion", APP_VERSION);
    set_registry_string(key, L"Publisher", L"KeySwitchFix");
    set_registry_string(key, L"InstallLocation", g_install_directory);
    set_registry_string(key, L"DisplayIcon", g_app_path);
    ok &= RegSetValueExW(key, L"UninstallString", 0, REG_SZ, (const BYTE *)uninstall_command,
                         (DWORD)((wcslen(uninstall_command) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    set_registry_string(key, L"QuietUninstallString", quiet_command);
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, (BYTE *)&one, sizeof(one));
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, (BYTE *)&one, sizeof(one));
    RegSetValueExW(key, L"EstimatedSize", 0, REG_DWORD, (BYTE *)&size_kb, sizeof(size_kb));
    RegCloseKey(key);
    return ok;
}

/* A cleanup left by an uninstall that could not delete a running file
   would delete the new installation at the next sign-in. */
static void cancel_pending_cleanup(void) {
    HKEY key;
    int serial;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    for (serial = 1; serial <= 8; ++serial) {
        wchar_t name[40];
        swprintf(name, 40, L"KeySwitchFix cleanup %d", serial);
        RegDeleteValueW(key, name);
    }
    RegCloseKey(key);
}

static void note_problem(const wchar_t *what) {
    size_t used = wcslen(g_install_problems);
    size_t capacity = sizeof(g_install_problems) / sizeof(g_install_problems[0]);
    if (used + wcslen(what) + 3 >= capacity) return;
    if (used) wcscat(g_install_problems, L", ");
    wcscat(g_install_problems, what);
}

static int perform_install(HWND dialog, int startup) {
    const wchar_t *targets[2];
    int desktop_created;
    int start_menu_created;
    targets[0] = g_app_path;
    targets[1] = g_uninstaller_path;
    g_install_problems[0] = 0;
    if (!g_paths_ok) {
        MessageBoxW(dialog,
                    L"The installation folder path is too long for Windows. KeySwitchFix cannot be installed "
                    L"for this user name.", L"KeySwitchFix Setup", MB_OK | MB_ICONERROR);
        return 0;
    }
    ensure_directories();
    cancel_pending_cleanup();
    g_was_running = stop_running_app();
    if (!stage_resource_file(IDR_APP_BINARY, g_app_path) ||
        !stage_resource_file(IDR_UNINSTALL_BINARY, g_uninstaller_path)) {
        discard_staged_files(targets, 2);
        restart_previous_copy();
        MessageBoxW(dialog,
                    L"The program files could not be written (disk full, or blocked by security software). "
                    L"Nothing was changed.", L"KeySwitchFix Setup", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (!commit_staged_files(targets, 2)) {
        restart_previous_copy();
        MessageBoxW(dialog,
                    L"The program files could not be replaced (a file is in use). The previous version was "
                    L"kept. Close KeySwitchFix and try again.", L"KeySwitchFix Setup", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (!install_language_packs()) note_problem(L"language packs");
    if (!write_initial_settings(startup)) note_problem(L"settings");
    if (!update_startup(startup)) note_problem(L"start with Windows");
    if (!register_uninstaller()) note_problem(L"Installed apps entry");
    desktop_created = create_shortcut(g_desktop_shortcut);
    start_menu_created = create_shortcut(g_start_menu_shortcut);
    g_shortcuts_created = desktop_created && start_menu_created;
    if (!g_shortcuts_created) note_problem(L"shortcuts");
    return 1;
}

static void show_install_complete(HWND dialog) {
    wchar_t result_text[448];
    g_install_complete = 1;
    SetWindowTextW(dialog, L"KeySwitchFix Setup — Installation complete");
    SetDlgItemTextW(dialog, IDC_INSTALL_TITLE, L"Installation complete");
    if (!g_install_problems[0])
        swprintf(result_text, sizeof(result_text) / sizeof(result_text[0]),
                 L"KeySwitchFix %ls was installed successfully. Desktop and Start Menu shortcuts are ready.",
                 APP_VERSION);
    else
        swprintf(result_text, sizeof(result_text) / sizeof(result_text[0]),
                 L"KeySwitchFix %ls was installed, but Windows refused some parts: %ls.",
                 APP_VERSION, g_install_problems);
    SetDlgItemTextW(dialog, IDC_INSTALL_TEXT, result_text);
    SetDlgItemTextW(dialog, IDC_STARTUP, L"Launch KeySwitchFix now");
    SendDlgItemMessageW(dialog, IDC_STARTUP, BM_SETCHECK, BST_CHECKED, 0);
    SetDlgItemTextW(dialog, IDC_INSTALL_NOTE,
                    g_was_running
                        ? L"The running copy was closed for the update. Click Finish to start the new version."
                        : L"Click Finish to close Setup. The application will open afterwards.");
    SetDlgItemTextW(dialog, IDC_INSTALL, L"Finish");
    EnableWindow(GetDlgItem(dialog, IDC_INSTALL), TRUE);
    ShowWindow(GetDlgItem(dialog, IDC_CANCEL), SW_HIDE);
    SetFocus(GetDlgItem(dialog, IDC_INSTALL));
}

static INT_PTR CALLBACK installer_dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    switch (message) {
        case WM_INITDIALOG:
            g_install_complete = 0;
            g_launch_after_finish = 0;
            SendMessageW(dialog, WM_SETICON, ICON_BIG,
                         (LPARAM)LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP)));
            SendDlgItemMessageW(dialog, IDC_STARTUP, BM_SETCHECK,
                                startup_default() ? BST_CHECKED : BST_UNCHECKED, 0);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wparam) == IDC_INSTALL) {
                if (g_install_complete) {
                    g_launch_after_finish =
                        SendDlgItemMessageW(dialog, IDC_STARTUP, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    EndDialog(dialog, IDOK);
                    return TRUE;
                }
                int startup = SendDlgItemMessageW(dialog, IDC_STARTUP, BM_GETCHECK, 0, 0) == BST_CHECKED;
                EnableWindow(GetDlgItem(dialog, IDC_INSTALL), FALSE);
                SetDlgItemTextW(dialog, IDC_INSTALL, L"Installing...");
                if (perform_install(dialog, startup)) show_install_complete(dialog);
                else {
                    SetDlgItemTextW(dialog, IDC_INSTALL, L"Install");
                    EnableWindow(GetDlgItem(dialog, IDC_INSTALL), TRUE);
                }
                return TRUE;
            }
            if (LOWORD(wparam) == IDC_CANCEL || LOWORD(wparam) == IDCANCEL) {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

static void remove_uninstall_registry(void) {
    HKEY parent;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                      0, KEY_WRITE, &parent) == ERROR_SUCCESS) {
        RegDeleteTreeW(parent, L"KeySwitchFix");
        RegCloseKey(parent);
    }
}

static int current_module_is_installed_uninstaller(void) {
    wchar_t self[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, self, MAX_PATH);
    return length > 0 && length < MAX_PATH && _wcsicmp(self, g_uninstaller_path) == 0;
}

/* rmdir /S is only ever run on our own folder: ...\Programs\KeySwitchFix. */
static int install_directory_is_ours(void) {
    size_t length = wcslen(g_install_directory);
    static const wchar_t suffix[] = L"\\Programs\\KeySwitchFix";
    size_t suffix_length = sizeof(suffix) / sizeof(suffix[0]) - 1;
    return g_paths_ok && length > suffix_length + 3 &&
           _wcsicmp(g_install_directory + length - suffix_length, suffix) == 0;
}

/* A file that is still in use is deleted at the next sign-in: a per-user
   uninstaller cannot use MOVEFILE_DELAY_UNTIL_REBOOT (it needs admin). */
static int delete_at_next_sign_in(const wchar_t *path) {
    HKEY key;
    wchar_t system_directory[MAX_PATH];
    wchar_t command[MAX_PATH * 3];
    wchar_t name[40];
    static int serial;
    int ok = 0;
    if (!GetSystemDirectoryW(system_directory, MAX_PATH)) return 0;
    (void)path;
    /* The whole install folder (the program, the uninstaller, language
       packs): one short command, well under the 260 characters Windows
       allows for RunOnce. */
    if (!install_directory_is_ours()) return 0;
    swprintf(command, MAX_PATH * 3, L"\"%ls\\cmd.exe\" /D /C rmdir /S /Q \"%ls\"",
             system_directory, g_install_directory);
    if (wcslen(command) >= 260) return 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return 0;
    swprintf(name, 40, L"KeySwitchFix cleanup %d", ++serial);
    ok = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)command,
                        (DWORD)((wcslen(command) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

static void schedule_self_delete(void) {
    wchar_t self[MAX_PATH];
    wchar_t system_directory[MAX_PATH];
    wchar_t cmd_path[MAX_PATH];
    wchar_t command[MAX_PATH * 4];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    /*
     * Resolve cmd.exe explicitly. A bare "cmd.exe" is searched starting with
     * the process directory and the current directory, so an uninstaller
     * launched from an untrusted folder could run a planted binary.
     */
    if (!GetSystemDirectoryW(system_directory, MAX_PATH))
        wcscpy(system_directory, L"C:\\Windows\\System32");
    swprintf(cmd_path, MAX_PATH, L"%ls\\cmd.exe", system_directory);
    if (install_directory_is_ours())
        swprintf(command, MAX_PATH * 4,
                 L"\"%ls\" /D /C \"%ls\\ping.exe\" 127.0.0.1 -n 3 >NUL & del /F /Q \"%ls\" & rmdir /S /Q \"%ls\"",
                 cmd_path, system_directory, self, g_install_directory);
    else
        swprintf(command, MAX_PATH * 4,
                 L"\"%ls\" /D /C \"%ls\\ping.exe\" 127.0.0.1 -n 3 >NUL & del /F /Q \"%ls\"",
                 cmd_path, system_directory, self);
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    ZeroMemory(&process, sizeof(process));
    /* Started in the system folder: never in the folder it deletes, never
       where a planted ping.exe could be found. */
    if (CreateProcessW(cmd_path, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, system_directory,
                       &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else delete_at_next_sign_in(self);
}

/* Deletes the files KeySwitchFix keeps in a data folder and the language
   packs in its languages folder (the uninstall question names them); any
   other file the user put there stays, and with it the folder. */
static void purge_data_folder(const wchar_t *folder) {
    static const wchar_t *const data_files[] = {
        L"settings.ini", L"personal-dictionary.txt", L"snippets.txt", L"snippets.txt.bak",
        L"stats.ini", L"writing-memory.txt", L"settings.ini.tmp", L"stats.ini.tmp",
        L"personal-dictionary.txt.tmp", L"snippets.txt.tmp", L"snippets.txt.bak.tmp",
        L"writing-memory.txt.tmp"
    };
    size_t i;
    wchar_t path[MAX_PATH];
    if (!folder || !*folder) return;
    for (i = 0; i < sizeof(data_files) / sizeof(data_files[0]); ++i)
        if (ks_path_join(path, MAX_PATH, folder, L"\\", data_files[i])) DeleteFileW(path);
    /* languages\ in the data folder: the packs the user added. */
    if (ks_path_join(path, MAX_PATH, folder, L"\\", L"languages")) remove_packs_in(path, NULL, 0);
    RemoveDirectoryW(folder);
}

static int perform_uninstall(int silent, int purge) {
    int remove_settings = purge;
    int pending_restart = 0;
    if (!silent) {
        if (MessageBoxW(NULL, L"Uninstall KeySwitchFix from this Windows account?",
                        L"Uninstall KeySwitchFix", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        remove_settings = MessageBoxW(NULL,
                                      L"Also remove your settings and everything KeySwitchFix learned "
                                      L"(personal dictionary, snippets, writing memory, statistics and the "
                                      L"language packs you added)?\n\n"
                                      L"Choose No to keep them for a later reinstall.",
                                      L"Uninstall KeySwitchFix",
                                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    }
    stop_running_app();
    if (!DeleteFileW(g_app_path) && GetFileAttributesW(g_app_path) != INVALID_FILE_ATTRIBUTES) {
        if (delete_at_next_sign_in(g_app_path)) {
            pending_restart = 1;
        } else {
            if (!silent)
                MessageBoxW(NULL,
                            L"KeySwitchFix could not be removed. Close the application and try again.",
                            L"Uninstall failed", MB_OK | MB_ICONERROR);
            return 0;
        }
    }
    update_startup(0);
    remove_uninstall_registry();
    remove_shortcuts();
    /* A silent uninstall (winget, scripts, "reinstall") keeps the user's
       data; /purge removes it. */
    if (remove_settings) {
        purge_data_folder(g_data_directory);
        /* The app falls back to %TEMP%\\KeySwitchFix when the usual path is
           too long: that copy goes too. */
        {
            wchar_t temp[MAX_PATH];
            DWORD length = GetTempPathW(MAX_PATH, temp);
            wchar_t fallback[MAX_PATH];
            if (length && length < MAX_PATH &&
                ks_path_join(fallback, MAX_PATH, temp, temp[length - 1] == L'\\' ? L"" : L"\\", L"KeySwitchFix"))
                purge_data_folder(fallback);
        }
    }
    if (!silent) {
        MessageBoxW(NULL,
                    pending_restart
                        ? L"KeySwitchFix will be completely removed the next time you sign in to Windows."
                        : L"KeySwitchFix was removed successfully.",
                    L"Uninstall complete", MB_OK | MB_ICONINFORMATION);
    }
    if (current_module_is_installed_uninstaller()) {
        schedule_self_delete();
    } else {
        DeleteFileW(g_uninstaller_path);
        remove_language_packs(NULL, 0);
        RemoveDirectoryW(g_install_directory);
    }
    return 1;
}

/* Command-line switches are whole arguments, matched without regard to
   case (/SILENT). The program path (argument 0) is never a switch. */
static int has_switch(const wchar_t *command_line, const wchar_t *name) {
    int count = 0;
    int i;
    int found = 0;
    LPWSTR *arguments = CommandLineToArgvW(command_line, &count);
    if (!arguments) return 0;
    for (i = 1; i < count && !found; ++i)
        if (_wcsicmp(arguments[i], name) == 0) found = 1;
    LocalFree(arguments);
    return found;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command) {
    const wchar_t *wide_command = GetCommandLineW();
    wchar_t module_path[MAX_PATH];
    const wchar_t *base_name;
    int uninstall;
    int silent = has_switch(wide_command, L"/silent");
    (void)previous;
    (void)command_line;
    (void)show_command;
    g_instance = instance;
    {
        /* Load system DLLs from System32 only, never from the folder the
           program was started from (Downloads, for Setup). */
        typedef BOOL (WINAPI *SET_DLL_DIRECTORIES)(DWORD);
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        SET_DLL_DIRECTORIES set_directories =
            kernel ? (SET_DLL_DIRECTORIES)(void *)GetProcAddress(kernel, "SetDefaultDllDirectories") : NULL;
        if (set_directories) set_directories(0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    }
    build_paths();
    GetModuleFileNameW(NULL, module_path, MAX_PATH);
    base_name = wcsrchr(module_path, L'\\');
    if (base_name) ++base_name; else base_name = module_path;
    uninstall = has_switch(wide_command, L"/uninstall") ||
                wcsstr(base_name, L"Uninstall") != NULL;
    if (uninstall) return perform_uninstall(silent, has_switch(wide_command, L"/purge")) ? 0 : 1;
    {
        INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_INSTALLER), NULL,
                                         installer_dialog_proc, 0);
        if (result == IDOK && g_launch_after_finish)
            ShellExecuteW(NULL, L"open", g_app_path, L"--show", g_install_directory, SW_SHOWNORMAL);
        return (int)result;
    }
}
