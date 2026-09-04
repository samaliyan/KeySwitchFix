#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

#include "../resources/resource.h"

#define APP_NAME L"KeySwitchFix"
#define APP_VERSION L"2.8.0"
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

static void set_registry_string(HKEY key, const wchar_t *name, const wchar_t *value) {
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                   (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

static void build_paths(void) {
    wchar_t local_app_data[MAX_PATH];
    wchar_t shell_path[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (!length || length >= MAX_PATH) GetTempPathW(MAX_PATH, local_app_data);
    swprintf(g_install_directory, MAX_PATH, L"%ls\\Programs\\KeySwitchFix", local_app_data);
    swprintf(g_app_path, MAX_PATH, L"%ls\\KeySwitchFix.exe", g_install_directory);
    swprintf(g_uninstaller_path, MAX_PATH, L"%ls\\Uninstall KeySwitchFix.exe", g_install_directory);
    swprintf(g_data_directory, MAX_PATH, L"%ls\\KeySwitchFix", local_app_data);
    swprintf(g_settings_path, MAX_PATH, L"%ls\\settings.ini", g_data_directory);
    g_desktop_shortcut[0] = 0;
    g_start_menu_shortcut[0] = 0;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY | CSIDL_FLAG_CREATE,
                                   NULL, SHGFP_TYPE_CURRENT, shell_path)))
        swprintf(g_desktop_shortcut, MAX_PATH, L"%ls\\KeySwitchFix.lnk", shell_path);
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS | CSIDL_FLAG_CREATE,
                                   NULL, SHGFP_TYPE_CURRENT, shell_path)))
        swprintf(g_start_menu_shortcut, MAX_PATH, L"%ls\\KeySwitchFix.lnk", shell_path);
}

static void ensure_directories(void) {
    wchar_t local_app_data[MAX_PATH];
    wchar_t programs[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (!length || length >= MAX_PATH) GetTempPathW(MAX_PATH, local_app_data);
    swprintf(programs, MAX_PATH, L"%ls\\Programs", local_app_data);
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

static void stop_running_app(void) {
    HWND window = FindWindowW(APP_WINDOW_CLASS, NULL);
    int attempt;
    if (window) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if (process_path_equals(process_id, g_app_path))
            PostMessageW(window, WM_APP_EXIT, 0, 0);
    }
    for (attempt = 0; attempt < 20 && FindWindowW(APP_WINDOW_CLASS, NULL); ++attempt) Sleep(50);

    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W entry;
        if (snapshot == INVALID_HANDLE_VALUE) return;
        ZeroMemory(&entry, sizeof(entry));
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID != GetCurrentProcessId() &&
                    _wcsicmp(entry.szExeFile, L"KeySwitchFix.exe") == 0 &&
                    process_path_equals(entry.th32ProcessID, g_app_path)) {
                    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
                    if (process) {
                        if (WaitForSingleObject(process, 300) == WAIT_TIMEOUT) TerminateProcess(process, 0);
                        CloseHandle(process);
                    }
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
}

static int write_resource_file(int resource_id, const wchar_t *target) {
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
    swprintf(temporary, MAX_PATH, L"%ls.tmp", target);
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
    if (!MoveFileExW(temporary, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary);
        return 0;
    }
    return 1;
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

static void update_startup(int enabled) {
    HKEY key;
    wchar_t command[MAX_PATH + 8];
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    if (enabled) {
        swprintf(command, MAX_PATH + 8, L"\"%ls\"", g_app_path);
        set_registry_string(key, APP_NAME, command);
    } else RegDeleteValueW(key, APP_NAME);
    RegCloseKey(key);
}

static void write_initial_settings(int startup) {
    wchar_t number[8];
    int settings_existed = GetFileAttributesW(g_settings_path) != INVALID_FILE_ATTRIBUTES;
    /* A completed install must start in a working state, even when an older
       settings file had automatic correction paused. */
    WritePrivateProfileStringW(L"General", L"Enabled", L"1", g_settings_path);
    if (!settings_existed) {
        WritePrivateProfileStringW(L"General", L"Sensitivity", L"1", g_settings_path);
        WritePrivateProfileStringW(
            L"General", L"ExcludedProcesses",
            L"1Password.exe,Bitwarden.exe,CredentialUIBroker.exe,KeePass.exe,KeePassXC.exe,LastPass.exe,LockApp.exe",
            g_settings_path);
    }
    swprintf(number, 8, L"%d", startup ? 1 : 0);
    WritePrivateProfileStringW(L"General", L"StartWithWindows", number, g_settings_path);
}

static void register_uninstaller(void) {
    HKEY key;
    wchar_t uninstall_command[MAX_PATH + 32];
    wchar_t quiet_command[MAX_PATH + 40];
    DWORD one = 1;
    DWORD size_kb = 1400;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KeySwitchFix",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    swprintf(uninstall_command, MAX_PATH + 32, L"\"%ls\"", g_uninstaller_path);
    swprintf(quiet_command, MAX_PATH + 40, L"\"%ls\" /silent", g_uninstaller_path);
    set_registry_string(key, L"DisplayName", L"KeySwitchFix");
    set_registry_string(key, L"DisplayVersion", APP_VERSION);
    set_registry_string(key, L"Publisher", L"KeySwitchFix");
    set_registry_string(key, L"InstallLocation", g_install_directory);
    set_registry_string(key, L"DisplayIcon", g_app_path);
    set_registry_string(key, L"UninstallString", uninstall_command);
    set_registry_string(key, L"QuietUninstallString", quiet_command);
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, (BYTE *)&one, sizeof(one));
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, (BYTE *)&one, sizeof(one));
    RegSetValueExW(key, L"EstimatedSize", 0, REG_DWORD, (BYTE *)&size_kb, sizeof(size_kb));
    RegCloseKey(key);
}

static int perform_install(HWND dialog, int startup) {
    int desktop_created;
    int start_menu_created;
    ensure_directories();
    stop_running_app();
    if (!write_resource_file(IDR_APP_BINARY, g_app_path)) {
        MessageBoxW(dialog,
                    L"The application file could not be installed. Close any previous version and try again.",
                    L"KeySwitchFix Setup", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (!write_resource_file(IDR_UNINSTALL_BINARY, g_uninstaller_path)) {
        MessageBoxW(dialog, L"The uninstaller could not be created.",
                    L"KeySwitchFix Setup", MB_OK | MB_ICONERROR);
        DeleteFileW(g_app_path);
        return 0;
    }
    write_initial_settings(startup);
    update_startup(startup);
    register_uninstaller();
    desktop_created = create_shortcut(g_desktop_shortcut);
    start_menu_created = create_shortcut(g_start_menu_shortcut);
    g_shortcuts_created = desktop_created && start_menu_created;
    return 1;
}

static void show_install_complete(HWND dialog) {
    wchar_t result_text[256];
    g_install_complete = 1;
    SetWindowTextW(dialog, L"KeySwitchFix Setup — Installation complete");
    SetDlgItemTextW(dialog, IDC_INSTALL_TITLE, L"Installation complete");
    swprintf(result_text, sizeof(result_text) / sizeof(result_text[0]),
             g_shortcuts_created
                 ? L"KeySwitchFix %ls was installed successfully. Desktop and Start Menu shortcuts are ready."
                 : L"KeySwitchFix %ls was installed, but Windows could not create one or more shortcuts.",
             APP_VERSION);
    SetDlgItemTextW(dialog, IDC_INSTALL_TEXT, result_text);
    SetDlgItemTextW(dialog, IDC_STARTUP, L"Launch KeySwitchFix now");
    SendDlgItemMessageW(dialog, IDC_STARTUP, BM_SETCHECK, BST_CHECKED, 0);
    SetDlgItemTextW(dialog, IDC_INSTALL_NOTE,
                    L"Click Finish to close Setup. The application will open afterwards.");
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
            SendDlgItemMessageW(dialog, IDC_STARTUP, BM_SETCHECK, BST_CHECKED, 0);
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
    swprintf(command, MAX_PATH * 4,
             L"\"%ls\" /D /C ping 127.0.0.1 -n 3 >NUL & del /F /Q \"%ls\" & rmdir \"%ls\"",
             cmd_path, self, g_install_directory);
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    ZeroMemory(&process, sizeof(process));
    if (CreateProcessW(cmd_path, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL,
                       &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else MoveFileExW(self, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
}

static int perform_uninstall(int silent) {
    int remove_settings = 0;
    int pending_restart = 0;
    if (!silent) {
        if (MessageBoxW(NULL, L"Uninstall KeySwitchFix from this Windows account?",
                        L"Uninstall KeySwitchFix", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        remove_settings = MessageBoxW(NULL, L"Also remove your settings and preferences?",
                                      L"Uninstall KeySwitchFix",
                                      MB_YESNO | MB_ICONQUESTION) == IDYES;
    }
    stop_running_app();
    if (!DeleteFileW(g_app_path) && GetFileAttributesW(g_app_path) != INVALID_FILE_ATTRIBUTES) {
        if (MoveFileExW(g_app_path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
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
    if (remove_settings || silent) {
        DeleteFileW(g_settings_path);
        RemoveDirectoryW(g_data_directory);
    }
    if (!silent) {
        MessageBoxW(NULL,
                    pending_restart
                        ? L"KeySwitchFix will be completely removed after the next Windows restart."
                        : L"KeySwitchFix was removed successfully.",
                    L"Uninstall complete", MB_OK | MB_ICONINFORMATION);
    }
    if (current_module_is_installed_uninstaller()) {
        schedule_self_delete();
    } else {
        DeleteFileW(g_uninstaller_path);
        RemoveDirectoryW(g_install_directory);
    }
    return 1;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command) {
    const wchar_t *wide_command = GetCommandLineW();
    wchar_t module_path[MAX_PATH];
    const wchar_t *base_name;
    int uninstall;
    int silent = wcsstr(wide_command, L"/silent") != NULL;
    (void)previous;
    (void)command_line;
    (void)show_command;
    g_instance = instance;
    build_paths();
    GetModuleFileNameW(NULL, module_path, MAX_PATH);
    base_name = wcsrchr(module_path, L'\\');
    if (base_name) ++base_name; else base_name = module_path;
    uninstall = wcsstr(wide_command, L"/uninstall") != NULL ||
                wcsstr(base_name, L"Uninstall") != NULL;
    if (uninstall) return perform_uninstall(silent) ? 0 : 1;
    {
        INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_INSTALLER), NULL,
                                         installer_dialog_proc, 0);
        if (result == IDOK && g_launch_after_finish)
            ShellExecuteW(NULL, L"open", g_app_path, L"--show", g_install_directory, SW_SHOWNORMAL);
        return (int)result;
    }
}
