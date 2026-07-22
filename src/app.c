#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "core.h"
#include "../resources/resource.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#define APP_NAME L"KeySwitchFix"
#define APP_VERSION L"2.1.1"
#define APP_MUTEX L"Local\\KeySwitchFix.Native.2.0"
#define WINDOW_CLASS L"KeySwitchFix.MainWindow.2"

#define WM_APP_TRAY (WM_APP + 1)
#define WM_APP_DIAGNOSTIC (WM_APP + 2)
#define WM_APP_EXIT (WM_APP + 9)

#define ID_TRAY 1
#define ID_TIMER_STATUS 10
#define ID_TIMER_UNDO 11
#define ID_HOTKEY_UNDO 12

#define IDC_ENABLE 100
#define IDC_SENSITIVITY 101
#define IDC_APP_STARTUP 102
#define IDC_EXCLUDED 103
#define IDC_SAVE 105
#define IDC_HIDE 106
#define IDC_STATUS_LABEL 110
#define IDC_LAYOUT_LABEL 111
#define IDC_HOOK_LABEL 112
#define IDC_ACTIVITY_LABEL 113
#define IDC_COUNTS_LABEL 114

#define IDM_OPEN 200
#define IDM_TOGGLE 201
#define IDM_EXIT 203

#define INPUT_MARKER ((ULONG_PTR)0x4B534632u)

typedef struct SETTINGS {
    int enabled;
    int sensitivity;
    int start_with_windows;
    wchar_t excluded[512];
} SETTINGS;

typedef struct UNDO_RECORD {
    int valid;
    HWND window;
    KS_LANGUAGE source_language;
    UINT delimiter;
    ULONGLONG created_at;
    wchar_t original[KS_MAX_WORD + 1];
    wchar_t replacement[KS_MAX_WORD + 1];
} UNDO_RECORD;

static HINSTANCE g_instance;
static HWND g_window;
static HWND g_status_label;
static HWND g_layout_label;
static HWND g_hook_label;
static HWND g_activity_label;
static HWND g_counts_label;
static HWND g_enable_button;
static HWND g_sensitivity;
static HWND g_startup;
static HWND g_excluded;
static HHOOK g_keyboard_hook;
static HHOOK g_mouse_hook;
static NOTIFYICONDATAW g_tray;
static HFONT g_font_regular;
static HFONT g_font_medium;
static HFONT g_font_title;
static HFONT g_font_status;
static HBRUSH g_brush_white;
static HBRUSH g_brush_background;
static SETTINGS g_settings;
static wchar_t g_settings_path[MAX_PATH];
static wchar_t g_data_directory[MAX_PATH];
static int g_first_run;
static int g_exit_requested;
static int g_hotkey_registered;
static int g_shift_down;
static int g_control_down;
static int g_alt_down;
static int g_windows_down;
static UINT g_taskbar_created_message;

static KS_BLOOM g_english_bloom;
static KS_BLOOM g_persian_bloom;
static KS_TOKEN g_word[KS_MAX_WORD];
static int g_word_count;
static int g_overflow_count;
static int g_has_context;
static int g_skip_word;
static HWND g_word_window;
static KS_LANGUAGE g_word_language;
static UINT g_suppressed_vk;
static DWORD g_suppressed_at;
static UNDO_RECORD g_undo;

static volatile LONG g_keys_seen;
static volatile LONG g_words_checked;
static volatile LONG g_corrections;
static wchar_t g_last_activity[256] = L"Waiting for keyboard input...";

static LRESULT CALLBACK main_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK keyboard_hook_proc(int code, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK mouse_hook_proc(int code, WPARAM wparam, LPARAM lparam);

static void safe_copy(wchar_t *destination, size_t capacity, const wchar_t *source) {
    if (!destination || capacity == 0) return;
    if (!source) source = L"";
    wcsncpy(destination, source, capacity - 1);
    destination[capacity - 1] = 0;
}

static void set_activity(const wchar_t *text) {
    safe_copy(g_last_activity, sizeof(g_last_activity) / sizeof(g_last_activity[0]), text);
    if (g_window) PostMessageW(g_window, WM_APP_DIAGNOSTIC, 0, 0);
}

static void set_activity_pair(const wchar_t *prefix, const wchar_t *from, const wchar_t *to) {
    wchar_t buffer[256];
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%ls: %ls  ->  %ls", prefix, from, to);
    set_activity(buffer);
}

static int load_bloom_resource(int identifier, KS_BLOOM *bloom) {
    HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(identifier), RT_RCDATA);
    HGLOBAL loaded;
    const unsigned char *data;
    DWORD size;
    if (!resource) return 0;
    size = SizeofResource(g_instance, resource);
    loaded = LoadResource(g_instance, resource);
    if (!loaded) return 0;
    data = (const unsigned char *)LockResource(loaded);
    return data && ks_bloom_init(bloom, data, (size_t)size);
}

static void build_paths(void) {
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", g_data_directory,
                                            (DWORD)(sizeof(g_data_directory) / sizeof(wchar_t)));
    if (length == 0 || length >= sizeof(g_data_directory) / sizeof(wchar_t)) {
        GetTempPathW((DWORD)(sizeof(g_data_directory) / sizeof(wchar_t)), g_data_directory);
        wcscat(g_data_directory, L"KeySwitchFix");
    } else {
        wcscat(g_data_directory, L"\\KeySwitchFix");
    }
    CreateDirectoryW(g_data_directory, NULL);
    swprintf(g_settings_path, sizeof(g_settings_path) / sizeof(g_settings_path[0]),
             L"%ls\\settings.ini", g_data_directory);
}

static void load_settings(void) {
    DWORD attributes;
    build_paths();
    attributes = GetFileAttributesW(g_settings_path);
    g_first_run = (attributes == INVALID_FILE_ATTRIBUTES);
    g_settings.enabled = GetPrivateProfileIntW(L"General", L"Enabled", 1, g_settings_path);
    g_settings.sensitivity = GetPrivateProfileIntW(L"General", L"Sensitivity", 1, g_settings_path);
    if (g_settings.sensitivity < 0 || g_settings.sensitivity > 2) g_settings.sensitivity = 1;
    g_settings.start_with_windows = GetPrivateProfileIntW(L"General", L"StartWithWindows", 1, g_settings_path);
    GetPrivateProfileStringW(L"General", L"ExcludedProcesses",
                             L"1Password.exe,Bitwarden.exe,CredentialUIBroker.exe,KeePass.exe,KeePassXC.exe,LastPass.exe,LockApp.exe",
                             g_settings.excluded,
                             (DWORD)(sizeof(g_settings.excluded) / sizeof(wchar_t)), g_settings_path);
}

static void update_startup_registry(void) {
    HKEY key;
    wchar_t executable[MAX_PATH];
    wchar_t command[MAX_PATH + 8];
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    if (g_settings.start_with_windows) {
        GetModuleFileNameW(NULL, executable, MAX_PATH);
        swprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\"", executable);
        RegSetValueExW(key, APP_NAME, 0, REG_SZ, (const BYTE *)command,
                       (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, APP_NAME);
    }
    RegCloseKey(key);
}

static void save_settings(void) {
    wchar_t number[16];
    swprintf(number, 16, L"%d", g_settings.enabled);
    WritePrivateProfileStringW(L"General", L"Enabled", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.sensitivity);
    WritePrivateProfileStringW(L"General", L"Sensitivity", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.start_with_windows);
    WritePrivateProfileStringW(L"General", L"StartWithWindows", number, g_settings_path);
    WritePrivateProfileStringW(L"General", L"ExcludedProcesses", g_settings.excluded, g_settings_path);
    update_startup_registry();
}

static int minimum_length(void) {
    if (g_settings.sensitivity == 0) return 5;
    if (g_settings.sensitivity == 2) return 3;
    return 4;
}

static void clear_word(void) {
    g_word_count = 0;
    g_overflow_count = 0;
    g_has_context = 0;
    g_skip_word = 0;
    g_word_window = NULL;
    g_word_language = KS_LANG_OTHER;
}

static int key_down(int virtual_key) {
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

static int shortcut_modifier_down(void) {
    return g_control_down || g_alt_down || g_windows_down;
}

static int is_modifier(UINT key) {
    return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
           key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
           key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
           key == VK_LWIN || key == VK_RWIN || key == VK_CAPITAL;
}

static void update_modifier_state(UINT key, int down) {
    if (key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT) g_shift_down = down;
    else if (key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL) g_control_down = down;
    else if (key == VK_MENU || key == VK_LMENU || key == VK_RMENU) g_alt_down = down;
    else if (key == VK_LWIN || key == VK_RWIN) g_windows_down = down;
}

static int is_navigation(UINT key) {
    return key == VK_ESCAPE || key == VK_DELETE || key == VK_LEFT || key == VK_RIGHT ||
           key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END ||
           key == VK_PRIOR || key == VK_NEXT || key == VK_PAUSE;
}

static int is_delimiter(UINT key) {
    return key == VK_SPACE || key == VK_RETURN || key == VK_TAB;
}

static KS_LANGUAGE language_from_layout(HKL layout) {
    LANGID language_id = LOWORD((ULONG_PTR)layout);
    WORD primary = PRIMARYLANGID(language_id);
    if (primary == LANG_ENGLISH) return KS_LANG_ENGLISH;
    if (primary == 0x29) return KS_LANG_PERSIAN;
    return KS_LANG_OTHER;
}

static KS_LANGUAGE foreground_language(HWND foreground) {
    DWORD process_id = 0;
    DWORD thread_id;
    if (!foreground) return KS_LANG_OTHER;
    thread_id = GetWindowThreadProcessId(foreground, &process_id);
    return language_from_layout(GetKeyboardLayout(thread_id));
}

static const wchar_t *language_name(KS_LANGUAGE language) {
    if (language == KS_LANG_ENGLISH) return L"English";
    if (language == KS_LANG_PERSIAN) return L"Persian";
    return L"Unsupported";
}

static int basename_equals(const wchar_t *path, const wchar_t *candidate, size_t length) {
    const wchar_t *base = wcsrchr(path, L'\\');
    size_t base_length;
    if (base) ++base; else base = path;
    base_length = wcslen(base);
    return base_length == length && _wcsnicmp(base, candidate, length) == 0;
}

static int process_is_excluded(HWND foreground) {
    DWORD process_id = 0;
    HANDLE process;
    wchar_t path[MAX_PATH];
    DWORD length = MAX_PATH;
    const wchar_t *cursor;

    GetWindowThreadProcessId(foreground, &process_id);
    if (!process_id || process_id == GetCurrentProcessId()) return 1;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return 0;
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) {
        CloseHandle(process);
        return 0;
    }
    CloseHandle(process);

    cursor = g_settings.excluded;
    while (*cursor) {
        const wchar_t *start;
        const wchar_t *end;
        while (*cursor == L' ' || *cursor == L',' || *cursor == L';') ++cursor;
        start = cursor;
        while (*cursor && *cursor != L',' && *cursor != L';') ++cursor;
        end = cursor;
        while (end > start && end[-1] == L' ') --end;
        if (end > start && basename_equals(path, start, (size_t)(end - start))) return 1;
    }
    return 0;
}

static HWND focused_window(HWND foreground) {
    DWORD process_id = 0;
    DWORD thread_id = GetWindowThreadProcessId(foreground, &process_id);
    GUITHREADINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (thread_id && GetGUIThreadInfo(thread_id, &info) && info.hwndFocus) return info.hwndFocus;
    return foreground;
}

static int is_protected_field(HWND foreground) {
    HWND focus = focused_window(foreground);
    LONG_PTR style;
    wchar_t class_name[64];
    DWORD_PTR result = 0;
    if (!focus) return 0;
    style = GetWindowLongPtrW(focus, GWL_STYLE);
    if ((style & ES_PASSWORD) != 0) return 1;
    class_name[0] = 0;
    GetClassNameW(focus, class_name, 64);
    if (_wcsicmp(class_name, L"Edit") != 0 && _wcsnicmp(class_name, L"RichEdit", 8) != 0) return 0;
    if (SendMessageTimeoutW(focus, EM_GETPASSWORDCHAR, 0, 0, SMTO_ABORTIFHUNG, 40, &result) && result)
        return 1;
    return 0;
}

static HKL find_layout(KS_LANGUAGE language) {
    int count = GetKeyboardLayoutList(0, NULL);
    HKL layouts[32];
    int i;
    if (count > 32) count = 32;
    if (count > 0) {
        count = GetKeyboardLayoutList(count, layouts);
        for (i = 0; i < count; ++i) {
            if (language_from_layout(layouts[i]) == language) return layouts[i];
        }
    }
    return LoadKeyboardLayoutW(language == KS_LANG_PERSIAN ? L"00000429" : L"00000409",
                               KLF_SUBSTITUTE_OK);
}

static void request_layout(HWND foreground, KS_LANGUAGE language) {
    HKL layout = find_layout(language);
    HWND target;
    DWORD_PTR result = 0;
    if (!layout) return;
    target = focused_window(foreground);
    if (!SendMessageTimeoutW(target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)layout,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result))
        PostMessageW(target, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)layout);
}

static void add_virtual_input(INPUT *inputs, UINT *count, WORD key) {
    ZeroMemory(&inputs[*count], sizeof(INPUT));
    inputs[*count].type = INPUT_KEYBOARD;
    inputs[*count].ki.wVk = key;
    inputs[*count].ki.dwExtraInfo = INPUT_MARKER;
    ++*count;
    ZeroMemory(&inputs[*count], sizeof(INPUT));
    inputs[*count].type = INPUT_KEYBOARD;
    inputs[*count].ki.wVk = key;
    inputs[*count].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[*count].ki.dwExtraInfo = INPUT_MARKER;
    ++*count;
}

static void add_unicode_input(INPUT *inputs, UINT *count, wchar_t character) {
    ZeroMemory(&inputs[*count], sizeof(INPUT));
    inputs[*count].type = INPUT_KEYBOARD;
    inputs[*count].ki.wScan = (WORD)character;
    inputs[*count].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[*count].ki.dwExtraInfo = INPUT_MARKER;
    ++*count;
    ZeroMemory(&inputs[*count], sizeof(INPUT));
    inputs[*count].type = INPUT_KEYBOARD;
    inputs[*count].ki.wScan = (WORD)character;
    inputs[*count].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs[*count].ki.dwExtraInfo = INPUT_MARKER;
    ++*count;
}

static int send_replacement(HWND foreground, int delete_count, const wchar_t *replacement,
                            UINT delimiter, KS_LANGUAGE target_language) {
    INPUT inputs[(KS_MAX_WORD + 2) * 4];
    UINT count = 0;
    int i;
    const wchar_t *cursor;
    for (i = 0; i < delete_count; ++i) add_virtual_input(inputs, &count, VK_BACK);
    for (cursor = replacement; *cursor; ++cursor) add_unicode_input(inputs, &count, *cursor);
    if (delimiter) add_virtual_input(inputs, &count, (WORD)delimiter);
    if (SendInput(count, inputs, sizeof(INPUT)) != count) {
        set_activity(L"Windows blocked text replacement. Match the target app's privilege level.");
        return 0;
    }
    request_layout(foreground, target_language);
    return 1;
}

static void store_undo(HWND foreground, const KS_DECISION *decision, UINT delimiter) {
    ZeroMemory(&g_undo, sizeof(g_undo));
    g_undo.valid = 1;
    g_undo.window = foreground;
    g_undo.source_language = decision->source_language;
    g_undo.delimiter = delimiter;
    g_undo.created_at = GetTickCount64();
    safe_copy(g_undo.original, KS_MAX_WORD + 1, decision->original);
    safe_copy(g_undo.replacement, KS_MAX_WORD + 1, decision->replacement);
}

static int apply_decision(HWND foreground, const KS_DECISION *decision,
                          int delete_count, UINT delimiter) {
    if (is_protected_field(foreground)) {
        set_activity(L"Correction skipped in a protected password field.");
        return 0;
    }
    if (!send_replacement(foreground, delete_count, decision->replacement,
                          delimiter, decision->target_language)) return 0;
    store_undo(foreground, decision, delimiter);
    InterlockedIncrement(&g_corrections);
    set_activity_pair(L"Corrected", decision->original, decision->replacement);
    return 1;
}

static void try_undo(void) {
    HWND foreground = GetForegroundWindow();
    int delete_count;
    if (!g_undo.valid || foreground != g_undo.window ||
        GetTickCount64() - g_undo.created_at > 15000u) {
        g_undo.valid = 0;
        set_activity(L"Nothing to undo. Use the shortcut immediately after a correction.");
        return;
    }
    delete_count = (int)wcslen(g_undo.replacement) + (g_undo.delimiter ? 1 : 0);
    if (send_replacement(foreground, delete_count, g_undo.original,
                         g_undo.delimiter, g_undo.source_language)) {
        set_activity_pair(L"Restored", g_undo.replacement, g_undo.original);
    }
    g_undo.valid = 0;
}

static LRESULT CALLBACK keyboard_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    KBDLLHOOKSTRUCT *data;
    int key_up;
    HWND foreground;
    KS_LANGUAGE language;
    KS_TOKEN token;
    int shift;
    int caps;
    KS_DECISION decision;

    if (code < 0) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    data = (KBDLLHOOKSTRUCT *)lparam;
    if (data->flags & LLKHF_INJECTED) {
        if (data->dwExtraInfo != INPUT_MARKER) {
            clear_word();
            g_undo.valid = 0;
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    key_up = (wparam == WM_KEYUP || wparam == WM_SYSKEYUP);
    if (is_modifier(data->vkCode)) {
        update_modifier_state(data->vkCode, !key_up);
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if (key_up) {
        if (g_suppressed_vk == data->vkCode) {
            DWORD elapsed = GetTickCount() - g_suppressed_at;
            g_suppressed_vk = 0;
            if (elapsed < 5000u) return 1;
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if (wparam != WM_KEYDOWN && wparam != WM_SYSKEYDOWN)
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    if (data->vkCode == VK_BACK && g_control_down && g_windows_down) {
        clear_word();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    g_undo.valid = 0;
    if (!g_settings.enabled || shortcut_modifier_down()) {
        clear_word();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    foreground = GetForegroundWindow();
    language = foreground_language(foreground);
    if (!foreground || language == KS_LANG_OTHER) {
        clear_word();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if (g_has_context && (foreground != g_word_window || language != g_word_language)) clear_word();

    if (data->vkCode == VK_BACK) {
        if (g_overflow_count > 0) --g_overflow_count;
        else if (g_word_count > 0) --g_word_count;
        if (!g_word_count && !g_overflow_count) clear_word();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if (is_navigation(data->vkCode)) {
        clear_word();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    shift = g_shift_down;
    caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    if (ks_map_scancode(data->scanCode, shift, caps, &token)) {
        InterlockedIncrement(&g_keys_seen);
        if (!g_has_context) {
            g_has_context = 1;
            g_word_window = foreground;
            g_word_language = language;
            g_skip_word = process_is_excluded(foreground);
        }
        if (g_skip_word) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
        if (g_word_count < KS_MAX_WORD && !g_overflow_count) g_word[g_word_count++] = token;
        else ++g_overflow_count;

        if (!g_overflow_count && ks_evaluate(g_word, g_word_count, g_word_language,
                                              minimum_length(), &g_english_bloom,
                                              &g_persian_bloom, &decision)) {
            if (apply_decision(foreground, &decision, g_word_count - 1, 0)) {
                g_suppressed_vk = data->vkCode;
                g_suppressed_at = GetTickCount();
                clear_word();
                return 1;
            }
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    if (is_delimiter(data->vkCode)) {
        InterlockedIncrement(&g_words_checked);
        if (!g_skip_word && !g_overflow_count &&
            ks_evaluate(g_word, g_word_count, g_word_language, minimum_length(),
                        &g_english_bloom, &g_persian_bloom, &decision)) {
            if (apply_decision(foreground, &decision, g_word_count, data->vkCode)) {
                g_suppressed_vk = data->vkCode;
                g_suppressed_at = GetTickCount();
                clear_word();
                return 1;
            }
        }
    }
    clear_word();
    return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
}

static LRESULT CALLBACK mouse_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    MSLLHOOKSTRUCT *data;
    if (code >= 0 && (wparam == WM_LBUTTONDOWN || wparam == WM_RBUTTONDOWN ||
                      wparam == WM_MBUTTONDOWN || wparam == WM_XBUTTONDOWN)) {
        data = (MSLLHOOKSTRUCT *)lparam;
        if (!(data->flags & LLMHF_INJECTED)) {
            clear_word();
            g_undo.valid = 0;
        }
    }
    return CallNextHookEx(g_mouse_hook, code, wparam, lparam);
}

static void add_tray_icon(void) {
    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = ID_TRAY;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
    safe_copy(g_tray.szTip, sizeof(g_tray.szTip) / sizeof(wchar_t), L"KeySwitchFix — Active");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
}

static void update_tray_tip(void) {
    g_tray.uFlags = NIF_TIP;
    safe_copy(g_tray.szTip, sizeof(g_tray.szTip) / sizeof(wchar_t),
              g_settings.enabled ? L"KeySwitchFix — Active" : L"KeySwitchFix — Paused");
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

static void show_tray_menu(void) {
    HMENU menu = CreatePopupMenu();
    POINT point;
    AppendMenuW(menu, MF_STRING, IDM_OPEN, L"Open KeySwitchFix");
    AppendMenuW(menu, MF_STRING | (g_settings.enabled ? MF_CHECKED : 0), IDM_TOGGLE,
                L"Enable automatic correction");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    GetCursorPos(&point);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, g_window, NULL);
    DestroyMenu(menu);
}

static void show_main_window(void) {
    ShowWindow(g_window, SW_SHOWNORMAL);
    SetForegroundWindow(g_window);
}

static void update_controls_from_settings(void) {
    SendMessageW(g_sensitivity, CB_SETCURSEL, (WPARAM)g_settings.sensitivity, 0);
    SendMessageW(g_startup, BM_SETCHECK, g_settings.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_excluded, g_settings.excluded);
    InvalidateRect(g_enable_button, NULL, TRUE);
    update_tray_tip();
}

static void read_controls_to_settings(void) {
    LRESULT selection = SendMessageW(g_sensitivity, CB_GETCURSEL, 0, 0);
    if (selection >= 0 && selection <= 2) g_settings.sensitivity = (int)selection;
    g_settings.start_with_windows = SendMessageW(g_startup, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(g_excluded, g_settings.excluded,
                   (int)(sizeof(g_settings.excluded) / sizeof(wchar_t)));
}

static void update_diagnostics_ui(void) {
    wchar_t buffer[256];
    wchar_t process_name[MAX_PATH] = L"Unknown app";
    HWND foreground = GetForegroundWindow();
    DWORD process_id = 0;
    HANDLE process;
    DWORD length = MAX_PATH;
    KS_LANGUAGE language = foreground_language(foreground);
    const wchar_t *base;

    if (foreground == g_window) foreground = g_word_window;
    if (foreground) {
        GetWindowThreadProcessId(foreground, &process_id);
        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
        if (process) {
            if (QueryFullProcessImageNameW(process, 0, process_name, &length)) {
                base = wcsrchr(process_name, L'\\');
                if (base) memmove(process_name, base + 1, (wcslen(base + 1) + 1) * sizeof(wchar_t));
            }
            CloseHandle(process);
        }
        language = foreground_language(foreground);
    }

    SetWindowTextW(g_status_label, g_settings.enabled ? L"Protection is active" : L"Protection is paused");
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"Current context: %ls  •  %ls layout",
             process_name, language_name(language));
    SetWindowTextW(g_layout_label, buffer);
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"Keyboard hook: %ls  •  Undo: Ctrl + Win + Backspace",
             g_keyboard_hook ? L"Running" : L"FAILED");
    SetWindowTextW(g_hook_label, buffer);
    SetWindowTextW(g_activity_label, g_last_activity);
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"Input observed: %ld keys  •  %ld words checked  •  %ld corrections",
             g_keys_seen, g_words_checked, g_corrections);
    SetWindowTextW(g_counts_label, buffer);
    InvalidateRect(g_enable_button, NULL, TRUE);
}

static void create_ui(HWND window) {
    g_font_regular = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, L"Segoe UI");
    g_font_medium = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
    g_font_title = CreateFontW(-30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    g_font_status = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");

    g_status_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 48, 164, 480, 30,
                                   window, (HMENU)IDC_STATUS_LABEL, g_instance, NULL);
    g_layout_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 48, 199, 560, 22,
                                   window, (HMENU)IDC_LAYOUT_LABEL, g_instance, NULL);
    g_enable_button = CreateWindowW(L"BUTTON", L"Enable automatic correction",
                                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    584, 164, 126, 42, window, (HMENU)IDC_ENABLE, g_instance, NULL);

    g_sensitivity = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  150, 290, 190, 200, window, (HMENU)IDC_SENSITIVITY, g_instance, NULL);
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Conservative");
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Balanced (recommended)");
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Sensitive");
    g_startup = CreateWindowW(L"BUTTON", L"Start with Windows", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              414, 290, 210, 25, window, (HMENU)IDC_APP_STARTUP, g_instance, NULL);
    g_excluded = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 150, 324, 560, 27, window, (HMENU)IDC_EXCLUDED, g_instance, NULL);

    g_hook_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 48, 410, 640, 22,
                                 window, (HMENU)IDC_HOOK_LABEL, g_instance, NULL);
    g_activity_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
                                     48, 442, 640, 24, window, (HMENU)IDC_ACTIVITY_LABEL, g_instance, NULL);
    g_counts_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 48, 476, 640, 22,
                                   window, (HMENU)IDC_COUNTS_LABEL, g_instance, NULL);

    CreateWindowW(L"BUTTON", L"Save settings", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  24, 548, 150, 38, window, (HMENU)IDC_SAVE, g_instance, NULL);
    CreateWindowW(L"BUTTON", L"Hide to tray", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  184, 548, 140, 38, window, (HMENU)IDC_HIDE, g_instance, NULL);

    {
        HWND child = GetWindow(window, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_font_regular, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }
    }
    SendMessageW(g_status_label, WM_SETFONT, (WPARAM)g_font_status, TRUE);
    SendMessageW(g_hook_label, WM_SETFONT, (WPARAM)g_font_medium, TRUE);
    g_brush_white = CreateSolidBrush(RGB(255, 255, 255));
    g_brush_background = CreateSolidBrush(RGB(245, 247, 251));
    update_controls_from_settings();
}

static void draw_card(HDC dc, int left, int top, int right, int bottom) {
    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(225, 230, 239));
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, left, top, right, bottom, 18, 18);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void paint_main_window(HWND window) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    HBRUSH header = CreateSolidBrush(RGB(28, 42, 76));
    HFONT old_font;
    SetBkMode(dc, TRANSPARENT);
    GetClientRect(window, &client);
    FillRect(dc, &client, g_brush_background);
    {
        RECT header_rect = {0, 0, client.right, 116};
        FillRect(dc, &header_rect, header);
    }
    DeleteObject(header);

    old_font = (HFONT)SelectObject(dc, g_font_title);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 28, 25, L"KeySwitchFix", lstrlenW(L"KeySwitchFix"));
    SelectObject(dc, g_font_regular);
    SetTextColor(dc, RGB(190, 202, 230));
    TextOutW(dc, 30, 70, L"Automatic Persian / English keyboard layout repair",
             lstrlenW(L"Automatic Persian / English keyboard layout repair"));

    draw_card(dc, 24, 136, 736, 228);
    draw_card(dc, 24, 246, 736, 358);
    draw_card(dc, 24, 376, 736, 526);

    SelectObject(dc, g_font_medium);
    SetTextColor(dc, RGB(48, 59, 82));
    TextOutW(dc, 48, 260, L"Correction settings", lstrlenW(L"Correction settings"));
    TextOutW(dc, 48, 390, L"Live diagnostics", lstrlenW(L"Live diagnostics"));
    SelectObject(dc, g_font_regular);
    SetTextColor(dc, RGB(99, 112, 137));
    TextOutW(dc, 48, 294, L"Sensitivity", lstrlenW(L"Sensitivity"));
    TextOutW(dc, 48, 328, L"Excluded apps", lstrlenW(L"Excluded apps"));
    TextOutW(dc, 340, 557, L"No cloud, no logging, no background service",
             lstrlenW(L"No cloud, no logging, no background service"));
    SelectObject(dc, old_font);
    EndPaint(window, &paint);
}

static void draw_button(DRAWITEMSTRUCT *item) {
    wchar_t text[64];
    HBRUSH brush;
    HPEN pen;
    COLORREF foreground = RGB(255, 255, 255);
    RECT rectangle = item->rcItem;
    HGDIOBJ old_brush;
    HGDIOBJ old_pen;
    HGDIOBJ old_font;
    int primary = item->CtlID == IDC_SAVE || item->CtlID == IDC_ENABLE;
    COLORREF color;
    if (item->CtlID == IDC_ENABLE) color = g_settings.enabled ? RGB(38, 188, 127) : RGB(139, 148, 166);
    else if (primary) color = RGB(76, 111, 230);
    else color = RGB(93, 111, 148);
    if (item->itemState & ODS_SELECTED) color = RGB(GetRValue(color) * 4 / 5,
                                                    GetGValue(color) * 4 / 5,
                                                    GetBValue(color) * 4 / 5);
    brush = CreateSolidBrush(color);
    pen = CreatePen(PS_SOLID, 1, color);
    old_brush = SelectObject(item->hDC, brush);
    old_pen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, 14, 14);
    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
    if (item->CtlID == IDC_ENABLE)
        safe_copy(text, 64, g_settings.enabled ? L"Pause" : L"Enable");
    else
        GetWindowTextW(item->hwndItem, text, 64);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, foreground);
    old_font = SelectObject(item->hDC, g_font_medium);
    DrawTextW(item->hDC, text, -1, &rectangle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(item->hDC, old_font);
}

static LRESULT CALLBACK main_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_taskbar_created_message && message == g_taskbar_created_message) {
        add_tray_icon();
        return 0;
    }
    switch (message) {
        case WM_CREATE:
            create_ui(window);
            SetTimer(window, ID_TIMER_STATUS, 500, NULL);
            return 0;
        case WM_PAINT:
            paint_main_window(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
            SetBkColor((HDC)wparam, RGB(255, 255, 255));
            SetTextColor((HDC)wparam, RGB(62, 73, 96));
            return (LRESULT)g_brush_white;
        case WM_CTLCOLOREDIT:
            SetBkColor((HDC)wparam, RGB(255, 255, 255));
            return (LRESULT)g_brush_white;
        case WM_DRAWITEM:
            draw_button((DRAWITEMSTRUCT *)lparam);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_ENABLE:
                case IDM_TOGGLE:
                    g_settings.enabled = !g_settings.enabled;
                    clear_word();
                    save_settings();
                    update_controls_from_settings();
                    set_activity(g_settings.enabled ? L"Automatic correction enabled."
                                                    : L"Automatic correction paused.");
                    return 0;
                case IDC_SAVE:
                    read_controls_to_settings();
                    save_settings();
                    set_activity(L"Settings saved.");
                    return 0;
                case IDC_HIDE:
                    ShowWindow(window, SW_HIDE);
                    return 0;
                case IDM_OPEN:
                    show_main_window();
                    return 0;
                case IDM_EXIT:
                    g_exit_requested = 1;
                    DestroyWindow(window);
                    return 0;
            }
            break;
        case WM_HOTKEY:
            if (wparam == ID_HOTKEY_UNDO) {
                SetTimer(window, ID_TIMER_UNDO, 40, NULL);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wparam == ID_TIMER_STATUS) {
                update_diagnostics_ui();
                return 0;
            }
            if (wparam == ID_TIMER_UNDO) {
                if (!key_down(VK_CONTROL) && !key_down(VK_LWIN) && !key_down(VK_RWIN) && !key_down(VK_BACK)) {
                    KillTimer(window, ID_TIMER_UNDO);
                    try_undo();
                }
                return 0;
            }
            break;
        case WM_APP_DIAGNOSTIC:
            update_diagnostics_ui();
            return 0;
        case WM_APP_TRAY:
            if (LOWORD(lparam) == WM_LBUTTONDBLCLK) show_main_window();
            else if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) show_tray_menu();
            return 0;
        case WM_APP_EXIT:
            g_exit_requested = 1;
            DestroyWindow(window);
            return 0;
        case WM_CLOSE:
            if (!g_exit_requested) {
                ShowWindow(window, SW_HIDE);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(window, ID_TIMER_STATUS);
            KillTimer(window, ID_TIMER_UNDO);
            if (g_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_UNDO);
            if (g_keyboard_hook) UnhookWindowsHookEx(g_keyboard_hook);
            if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
            g_tray.uFlags = 0;
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            DeleteObject(g_font_regular);
            DeleteObject(g_font_medium);
            DeleteObject(g_font_title);
            DeleteObject(g_font_status);
            DeleteObject(g_brush_white);
            DeleteObject(g_brush_background);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line_ansi, int show_command) {
    HANDLE mutex;
    WNDCLASSEXW window_class;
    MSG message;
    INITCOMMONCONTROLSEX controls;
    int show_window;
    (void)previous;
    (void)show_command;
    (void)command_line_ansi;

    g_instance = instance;
    SetProcessDPIAware();
    g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    g_shift_down = key_down(VK_SHIFT) || key_down(VK_LSHIFT) || key_down(VK_RSHIFT);
    g_control_down = key_down(VK_CONTROL) || key_down(VK_LCONTROL) || key_down(VK_RCONTROL);
    g_alt_down = key_down(VK_MENU) || key_down(VK_LMENU) || key_down(VK_RMENU);
    g_windows_down = key_down(VK_LWIN) || key_down(VK_RWIN);
    mutex = CreateMutexW(NULL, TRUE, APP_MUTEX);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(WINDOW_CLASS, NULL);
        if (existing) {
            ShowWindow(existing, SW_SHOWNORMAL);
            SetForegroundWindow(existing);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    if (!load_bloom_resource(IDR_EN_BLOOM, &g_english_bloom) ||
        !load_bloom_resource(IDR_FA_BLOOM, &g_persian_bloom)) {
        MessageBoxW(NULL, L"Dictionary resources are damaged. Please reinstall KeySwitchFix.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 2;
    }
    load_settings();
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = main_window_proc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    window_class.hIconSm = window_class.hIcon;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassExW(&window_class)) {
        MessageBoxW(NULL, L"The main window could not be registered.", APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 3;
    }

    g_window = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"KeySwitchFix 2.1.1",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, 776, 634, NULL, NULL, instance, NULL);
    if (!g_window) {
        MessageBoxW(NULL, L"The main window could not be created.", APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 4;
    }

    g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook_proc, instance, 0);
    g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, instance, 0);
    if (!g_keyboard_hook) set_activity(L"Keyboard hook FAILED. Restart the app or check security software.");
    else set_activity(L"Ready. Type normally in any app; correction is automatic.");
    g_hotkey_registered = RegisterHotKey(g_window, ID_HOTKEY_UNDO,
                                          MOD_CONTROL | MOD_WIN | MOD_NOREPEAT, VK_BACK);
    if (!g_hotkey_registered)
        set_activity(L"Protection is running, but the Undo hotkey is already used by another app.");
    add_tray_icon();
    update_diagnostics_ui();

    show_window = g_first_run || wcsstr(GetCommandLineW(), L"--show") != NULL;
    if (show_window) show_main_window();
    else ShowWindow(g_window, SW_HIDE);

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(mutex);
    return (int)message.wParam;
}
