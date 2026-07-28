#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "core.h"
#include "../resources/resource.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#define APP_NAME L"KeySwitchFix"
#define APP_VERSION L"2.7.0"
#define APP_MUTEX L"Local\\KeySwitchFix.Native.2.0"
#define WINDOW_CLASS L"KeySwitchFix.MainWindow.2"

#define WM_APP_TRAY (WM_APP + 1)
#define WM_APP_DIAGNOSTIC (WM_APP + 2)
#define WM_APP_EXIT (WM_APP + 9)

#define ID_TRAY 1
#define ID_TIMER_STATUS 10
#define ID_TIMER_UNDO 11
#define ID_HOTKEY_UNDO 12
#define ID_TIMER_SMART_CORRECTION 13

#define IDC_ENABLE 100
#define IDC_SENSITIVITY 101
#define IDC_APP_STARTUP 102
#define IDC_EXCLUDED 103
#define IDC_LANGUAGE_MODE 104
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
#define IDM_LANGUAGE_AUTO 204
#define IDM_LANGUAGE_PERSIAN 205
#define IDM_LANGUAGE_ENGLISH 206

#define INPUT_MARKER ((ULONG_PTR)0x4B534632u)
#define KS_MAX_PHRASE_CHARS KS_MAX_SEQUENCE_CHARS

typedef struct SETTINGS {
    int enabled;
    int sensitivity;
    int language_mode;
    int start_with_windows;
    wchar_t excluded[512];
} SETTINGS;

typedef struct UNDO_RECORD {
    int valid;
    HWND window;
    KS_LANGUAGE source_language;
    UINT delimiter;
    ULONGLONG created_at;
    wchar_t original[KS_MAX_PHRASE_CHARS + 1];
    wchar_t replacement[KS_MAX_PHRASE_CHARS + 1];
} UNDO_RECORD;

typedef struct WORD_HISTORY {
    KS_TOKEN tokens[KS_MAX_WORD];
    int count;
    KS_LANGUAGE visible_language;
} WORD_HISTORY;

static HINSTANCE g_instance;
static HWND g_window;
static HWND g_status_label;
static HWND g_layout_label;
static HWND g_hook_label;
static HWND g_activity_label;
static HWND g_counts_label;
static HWND g_enable_button;
static HWND g_sensitivity;
static HWND g_language_mode;
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
static HKL g_last_english_layout;
static HKL g_last_persian_layout;

static KS_BLOOM g_english_bloom;
static KS_BLOOM g_persian_bloom;
static KS_BLOOM g_english_common_bloom;
static KS_BLOOM g_persian_common_bloom;
static KS_BLOOM g_english_frequent_bloom;
static KS_BLOOM g_persian_frequent_bloom;
static KS_BLOOM g_english_prefix_bloom;
static KS_BLOOM g_persian_prefix_bloom;
static KS_BLOOM g_english_common_prefix_bloom;
static KS_BLOOM g_persian_common_prefix_bloom;
static KS_LEXICONS g_lexicons;
static KS_TOKEN g_word[KS_MAX_WORD];
static int g_word_count;
static int g_overflow_count;
static int g_has_context;
static int g_skip_word;
static HWND g_word_window;
static KS_LANGUAGE g_word_language;
static UINT g_suppressed_vk;
static DWORD g_suppressed_at;
static DWORD g_last_word_key_at;
static DWORD g_average_key_interval = 150;
static HWND g_intent_window;
static KS_LANGUAGE_CONTEXT g_intent_context;
static ULONGLONG g_intent_updated_at;
static HWND g_sentence_window;
static int g_sentence_started;
static WORD_HISTORY g_history[KS_MAX_SEQUENCE_WORDS - 1];
static int g_history_count;
static int g_history_chars;
static int g_history_overflowed;
static HWND g_history_window;
static WORD_HISTORY g_pending_word;
static int g_pending_word_valid;
static HWND g_pending_word_window;
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
    wchar_t buffer[384];
    wchar_t from_preview[97];
    wchar_t to_preview[97];
    safe_copy(from_preview, sizeof(from_preview) / sizeof(from_preview[0]), from);
    safe_copy(to_preview, sizeof(to_preview) / sizeof(to_preview[0]), to);
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%ls: %ls  ->  %ls",
             prefix, from_preview, to_preview);
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
    g_settings.language_mode = GetPrivateProfileIntW(L"General", L"LanguageMode", 0, g_settings_path);
    if (g_settings.language_mode < 0 || g_settings.language_mode > 2) g_settings.language_mode = 0;
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
    swprintf(number, 16, L"%d", g_settings.language_mode);
    WritePrivateProfileStringW(L"General", L"LanguageMode", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.start_with_windows);
    WritePrivateProfileStringW(L"General", L"StartWithWindows", number, g_settings_path);
    WritePrivateProfileStringW(L"General", L"ExcludedProcesses", g_settings.excluded, g_settings_path);
    update_startup_registry();
}

static void cancel_smart_correction(void) {
    if (g_window) KillTimer(g_window, ID_TIMER_SMART_CORRECTION);
}

static void clear_word(void) {
    cancel_smart_correction();
    g_word_count = 0;
    g_overflow_count = 0;
    g_has_context = 0;
    g_skip_word = 0;
    g_word_window = NULL;
    g_word_language = KS_LANG_OTHER;
    g_last_word_key_at = 0;
}

static void clear_history(void) {
    g_history_count = 0;
    g_history_chars = 0;
    g_history_overflowed = 0;
    g_history_window = NULL;
    g_pending_word_valid = 0;
    g_pending_word_window = NULL;
}

static void abandon_history(void) {
    g_history_count = 0;
    g_history_chars = 0;
    g_history_overflowed = 1;
    g_history_window = NULL;
    g_pending_word_valid = 0;
    g_pending_word_window = NULL;
}

static void store_pending_word(HWND window, const KS_TOKEN *tokens, int count,
                               KS_LANGUAGE visible_language) {
    if (!window || !tokens || count < 1 || count > KS_MAX_WORD ||
        (visible_language != KS_LANG_ENGLISH &&
         visible_language != KS_LANG_PERSIAN)) {
        abandon_history();
        return;
    }
    if ((g_history_window && g_history_window != window) ||
        (g_pending_word_valid && g_pending_word_window != window)) {
        clear_history();
    }
    memcpy(g_pending_word.tokens, tokens,
           (size_t)count * sizeof(tokens[0]));
    g_pending_word.count = count;
    g_pending_word.visible_language = visible_language;
    g_pending_word_valid = 1;
    g_pending_word_window = window;
}

static void history_push(HWND window, const KS_TOKEN *tokens, int count,
                         KS_LANGUAGE visible_language, UINT delimiter) {
    if (!window || !tokens || count < 1 || count > KS_MAX_WORD ||
        delimiter != VK_SPACE ||
        (visible_language != KS_LANG_ENGLISH &&
         visible_language != KS_LANG_PERSIAN)) {
        clear_history();
        return;
    }
    if (g_history_overflowed) return;
    if (g_history_window != window) {
        clear_history();
        g_history_window = window;
    }
    if (g_history_count >= KS_MAX_SEQUENCE_WORDS - 1 ||
        g_history_chars + count + (g_history_count ? 1 : 0) >
            KS_MAX_SEQUENCE_CHARS - KS_MAX_WORD - 1) {
        abandon_history();
        return;
    }
    memcpy(g_history[g_history_count].tokens, tokens,
           (size_t)count * sizeof(tokens[0]));
    g_history[g_history_count].count = count;
    g_history[g_history_count].visible_language = visible_language;
    g_history_chars += count + (g_history_count ? 1 : 0);
    ++g_history_count;
}

static int commit_pending_word(HWND window, UINT delimiter) {
    WORD_HISTORY pending;
    if (!g_pending_word_valid || g_pending_word_window != window ||
        delimiter != VK_SPACE)
        return 0;
    pending = g_pending_word;
    g_pending_word_valid = 0;
    g_pending_word_window = NULL;
    history_push(window, pending.tokens, pending.count,
                 pending.visible_language, delimiter);
    return !g_history_overflowed;
}

static void clear_intent(void) {
    g_intent_window = NULL;
    ks_context_reset(&g_intent_context);
    g_intent_updated_at = 0;
}

static void remember_intent(HWND window, KS_LANGUAGE language, int strength) {
    if (!window || (language != KS_LANG_ENGLISH && language != KS_LANG_PERSIAN)) return;
    if (g_intent_window != window) {
        ks_context_reset(&g_intent_context);
        g_intent_window = window;
    }
    ks_context_observe(&g_intent_context, language, strength);
    g_intent_updated_at = GetTickCount64();
}

static KS_LANGUAGE current_intent(HWND window, int *strength) {
    if (strength) *strength = 0;
    if (g_settings.language_mode == 1) {
        if (strength) *strength = 5;
        return KS_LANG_PERSIAN;
    }
    if (g_settings.language_mode == 2) {
        if (strength) *strength = 5;
        return KS_LANG_ENGLISH;
    }
    if (!window || window != g_intent_window || !g_intent_updated_at ||
        GetTickCount64() - g_intent_updated_at > 90000u) {
        clear_intent();
        return KS_LANG_OTHER;
    }
    return ks_context_current(&g_intent_context, strength);
}

static int sentence_start(HWND window) {
    if (!window) return 1;
    if (g_sentence_window != window) {
        g_sentence_window = window;
        g_sentence_started = 0;
        clear_intent();
    }
    return !g_sentence_started;
}

static void mark_sentence_word(HWND window) {
    if (!window) return;
    if (g_sentence_window != window) {
        g_sentence_window = window;
        clear_intent();
    }
    g_sentence_started = 1;
}

static void reset_sentence(HWND window) {
    g_sentence_window = window;
    g_sentence_started = 0;
    clear_intent();
    clear_history();
}

static void start_new_sentence(HWND window) {
    if (g_sentence_window != window) {
        g_sentence_window = window;
        clear_intent();
    }
    /*
     * Keep the weighted language evidence for the same document/window.
     * Sentence position starts over, while the previous sentence still tells
     * us whether the surrounding writing is predominantly Persian or English.
     */
    g_sentence_started = 0;
    clear_history();
}

static void observe_typing_interval(void) {
    DWORD now = GetTickCount();
    if (g_last_word_key_at) {
        DWORD interval = now - g_last_word_key_at;
        g_average_key_interval = ks_update_key_interval_ms(
            g_average_key_interval, interval);
    }
    g_last_word_key_at = now;
}

static void schedule_smart_correction(void) {
    UINT delay = (UINT)ks_idle_delay_ms(g_settings.sensitivity, g_average_key_interval);
    if (g_window) SetTimer(g_window, ID_TIMER_SMART_CORRECTION, delay, NULL);
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

static int is_shift_key(UINT key) {
    return key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT;
}

static int is_alt_key(UINT key) {
    return key == VK_MENU || key == VK_LMENU || key == VK_RMENU;
}

static int is_control_key(UINT key) {
    return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL;
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

static int is_correction_boundary(UINT key) {
    return is_delimiter(key) || (key == VK_OEM_PERIOD && !g_shift_down);
}

static int is_sentence_terminator(UINT key) {
    if (key == VK_RETURN || (key == VK_OEM_PERIOD && !g_shift_down)) return 1;
    if (g_shift_down && (key == '1' || key == VK_OEM_2)) return 1;
    return 0;
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
    HKL layout;
    KS_LANGUAGE language;
    if (!foreground) return KS_LANG_OTHER;
    thread_id = GetWindowThreadProcessId(foreground, &process_id);
    layout = GetKeyboardLayout(thread_id);
    language = language_from_layout(layout);
    if (language == KS_LANG_ENGLISH) g_last_english_layout = layout;
    else if (language == KS_LANG_PERSIAN) g_last_persian_layout = layout;
    return language;
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
    HKL remembered =
        language == KS_LANG_PERSIAN
            ? g_last_persian_layout : g_last_english_layout;
    if (remembered && language_from_layout(remembered) == language)
        return remembered;
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

static int translated_layout_character(HKL layout, DWORD scan_code,
                                       int shift, int caps,
                                       wchar_t *character) {
    BYTE keyboard_state[256];
    wchar_t output[4];
    UINT virtual_key;
    int count;
    if (!layout || !character) return 0;
    ZeroMemory(keyboard_state, sizeof(keyboard_state));
    if (shift) keyboard_state[VK_SHIFT] = 0x80;
    if (caps) keyboard_state[VK_CAPITAL] = 1;
    virtual_key = MapVirtualKeyExW(scan_code, MAPVK_VSC_TO_VK_EX, layout);
    if (!virtual_key) return 0;
    /*
     * Bit 2 keeps ToUnicodeEx from mutating the keyboard buffer on supported
     * Windows versions. Letter keys in both supported layouts produce one
     * BMP code point; dead keys and ligatures are deliberately not captured
     * as part of a word.
     */
    count = ToUnicodeEx(virtual_key, scan_code, keyboard_state, output,
                        (int)(sizeof(output) / sizeof(output[0])), 4, layout);
    if (count != 1 || output[0] < 0x20) return 0;
    *character = output[0];
    return 1;
}

static int map_physical_key(DWORD scan_code, int shift, int caps,
                            KS_TOKEN *token) {
    HKL english_layout = find_layout(KS_LANG_ENGLISH);
    HKL persian_layout = find_layout(KS_LANG_PERSIAN);
    KS_TOKEN fallback;
    int fallback_ok;
    int english_ok;
    int persian_ok;

    if (!token) return 0;
    /*
     * ToUnicodeEx also translates Space, digits, and punctuation. They are
     * printable characters but not members of a Persian/English word. Gate
     * runtime translation through the deliberately small physical-key map so
     * Space reaches the boundary evaluator instead of being swallowed into
     * the current word.
     */
    if (!ks_is_word_scancode(scan_code)) return 0;
    ZeroMemory(&fallback, sizeof(fallback));
    fallback_ok = ks_map_scancode(scan_code, shift, caps, &fallback);
    english_ok = translated_layout_character(
        english_layout, scan_code, shift, caps, &token->english);
    persian_ok = translated_layout_character(
        persian_layout, scan_code, shift, caps, &token->persian);
    if (english_ok && persian_ok) return 1;
    if (!english_ok && fallback_ok) token->english = fallback.english;
    if (!persian_ok && fallback_ok) token->persian = fallback.persian;
    return token->english != 0 && token->persian != 0;
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
    INPUT inputs[(KS_MAX_PHRASE_CHARS + 2) * 4];
    UINT count = 0;
    int i;
    const wchar_t *cursor;
    size_t replacement_length;
    if (!replacement || delete_count < 0 ||
        delete_count > KS_MAX_PHRASE_CHARS) return 0;
    replacement_length = wcslen(replacement);
    if (replacement_length > KS_MAX_PHRASE_CHARS) return 0;
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
    safe_copy(g_undo.original, KS_MAX_PHRASE_CHARS + 1, decision->original);
    safe_copy(g_undo.replacement, KS_MAX_PHRASE_CHARS + 1, decision->replacement);
}

static void store_phrase_undo(HWND foreground, KS_LANGUAGE source_language,
                              UINT delimiter, const wchar_t *original,
                              const wchar_t *replacement) {
    ZeroMemory(&g_undo, sizeof(g_undo));
    g_undo.valid = 1;
    g_undo.window = foreground;
    g_undo.source_language = source_language;
    g_undo.delimiter = delimiter;
    g_undo.created_at = GetTickCount64();
    safe_copy(g_undo.original, KS_MAX_PHRASE_CHARS + 1, original);
    safe_copy(g_undo.replacement, KS_MAX_PHRASE_CHARS + 1, replacement);
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
    mark_sentence_word(foreground);
    remember_intent(foreground, decision->target_language, 3);
    InterlockedIncrement(&g_corrections);
    set_activity_pair(L"Corrected", decision->original, decision->replacement);
    return 1;
}

static void tokens_to_language(const KS_TOKEN *tokens, int count,
                               KS_LANGUAGE language, wchar_t *output) {
    if (language == KS_LANG_PERSIAN)
        ks_tokens_to_persian(tokens, count, output);
    else
        ks_tokens_to_english(tokens, count, output);
}

static int append_phrase_word(wchar_t *phrase, size_t capacity,
                              const wchar_t *word, int add_space) {
    size_t length = wcslen(phrase);
    size_t word_length = wcslen(word);
    if (add_space) {
        if (length + 1 >= capacity) return 0;
        phrase[length++] = L' ';
        phrase[length] = 0;
    }
    if (length + word_length >= capacity) return 0;
    wcscpy(phrase + length, word);
    return 1;
}

static int try_sequence_correction(HWND foreground,
                                   const KS_TOKEN *current_tokens,
                                   int current_count,
                                   KS_LANGUAGE current_visible_language,
                                   UINT delimiter,
                                   KS_LANGUAGE context_language,
                                   int context_strength) {
    KS_SEQUENCE_WORD words[KS_MAX_SEQUENCE_WORDS];
    KS_SEQUENCE_RESULT result;
    wchar_t original[KS_MAX_PHRASE_CHARS + 1];
    wchar_t replacement[KS_MAX_PHRASE_CHARS + 1];
    wchar_t word_text[KS_MAX_WORD + 1];
    int maximum_words;
    int word_count;
    int start;
    int index;
    int needs_change;

    if (!foreground || !current_tokens || current_count < 1 ||
        current_count > KS_MAX_WORD || delimiter != VK_SPACE)
        return 0;
    if (g_history_window != foreground || g_history_count < 1) return 0;

    maximum_words = g_history_count + 1;
    if (maximum_words > KS_MAX_SEQUENCE_WORDS)
        maximum_words = KS_MAX_SEQUENCE_WORDS;
    for (word_count = maximum_words; word_count >= 2; --word_count) {
        start = g_history_count - (word_count - 1);
        for (index = 0; index < word_count - 1; ++index) {
            words[index].tokens = g_history[start + index].tokens;
            words[index].count = g_history[start + index].count;
        }
        words[word_count - 1].tokens = current_tokens;
        words[word_count - 1].count = current_count;
        if (!ks_evaluate_sequence(words, word_count, g_settings.sensitivity,
                                  context_language, context_strength,
                                  &g_lexicons, &result))
            continue;

        original[0] = 0;
        replacement[0] = 0;
        needs_change = 0;
        for (index = 0; index < word_count; ++index) {
            KS_LANGUAGE visible_language;
            if (index < word_count - 1) {
                visible_language =
                    g_history[start + index].visible_language;
            } else {
                visible_language = current_visible_language;
            }
            if (visible_language != result.language) needs_change = 1;
            tokens_to_language(words[index].tokens, words[index].count,
                               visible_language, word_text);
            if (!append_phrase_word(original,
                                    sizeof(original) / sizeof(original[0]),
                                    word_text, index > 0))
                return 0;
            tokens_to_language(words[index].tokens, words[index].count,
                               result.language, word_text);
            if (!append_phrase_word(replacement,
                                    sizeof(replacement) /
                                        sizeof(replacement[0]),
                                    word_text, index > 0))
                return 0;
        }
        if (!needs_change) return 0;
        if (is_protected_field(foreground)) {
            set_activity(L"Correction skipped in a protected password field.");
            return 0;
        }
        if (!send_replacement(foreground, (int)wcslen(original), replacement,
                              delimiter, result.language))
            return 0;
        store_phrase_undo(foreground, current_visible_language, delimiter,
                          original, replacement);
        /*
         * Keep monitoring from the beginning of the sentence. Only the
         * corrected suffix changes its visible language; earlier words remain
         * exactly as tracked. The current word is then committed with the
         * Space that SendInput already inserted.
         */
        for (index = 0; index < word_count - 1; ++index)
            g_history[start + index].visible_language = result.language;
        history_push(foreground, current_tokens, current_count,
                     result.language, delimiter);
        mark_sentence_word(foreground);
        remember_intent(foreground, result.language, 4);
        InterlockedIncrement(&g_corrections);
        set_activity_pair(L"Phrase corrected", original, replacement);
        return 1;
    }
    return 0;
}

static void try_smart_correction(void) {
    HWND foreground;
    KS_LANGUAGE language;
    KS_LANGUAGE intent;
    int intent_strength;
    KS_DECISION decision;

    cancel_smart_correction();
    if (!g_settings.enabled || !g_has_context || g_skip_word || g_overflow_count ||
        g_word_count < 2 || shortcut_modifier_down()) return;

    foreground = GetForegroundWindow();
    language = foreground_language(foreground);
    if (!foreground || foreground != g_word_window || language != g_word_language) {
        clear_word();
        return;
    }

    intent = current_intent(foreground, &intent_strength);
    InterlockedIncrement(&g_words_checked);
    if (ks_evaluate_contextual(
            g_word, g_word_count, g_word_language, g_settings.sensitivity,
            intent, intent_strength, sentence_start(foreground), KS_PHASE_IDLE,
            &g_lexicons,
            &decision) == KS_LIVE_CORRECT_NOW &&
        apply_decision(foreground, &decision, g_word_count, 0)) {
        store_pending_word(foreground, g_word, g_word_count,
                           decision.target_language);
        clear_word();
    }
}

static int try_undo(int consume_delimiter) {
    HWND foreground = GetForegroundWindow();
    int delete_count;
    UINT restored_delimiter;
    if (!g_undo.valid || foreground != g_undo.window ||
        GetTickCount64() - g_undo.created_at > 15000u) {
        g_undo.valid = 0;
        set_activity(L"Nothing to undo. Press Backspace immediately after a correction.");
        return 0;
    }
    delete_count = (int)wcslen(g_undo.replacement) + (g_undo.delimiter ? 1 : 0);
    restored_delimiter = consume_delimiter ? 0 : g_undo.delimiter;
    if (send_replacement(foreground, delete_count, g_undo.original,
                         restored_delimiter, g_undo.source_language)) {
        set_activity_pair(L"Restored", g_undo.replacement, g_undo.original);
        clear_intent();
        remember_intent(foreground, g_undo.source_language, 4);
        mark_sentence_word(foreground);
        clear_history();
        g_undo.valid = 0;
        return 1;
    }
    g_undo.valid = 0;
    return 0;
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
    KS_LIVE_RESULT live_result;
    KS_LANGUAGE intent;
    int intent_strength;

    if (code < 0) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    data = (KBDLLHOOKSTRUCT *)lparam;
    if (data->flags & LLKHF_INJECTED) {
        if (data->dwExtraInfo != INPUT_MARKER) {
            clear_word();
            clear_history();
            g_undo.valid = 0;
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    key_up = (wparam == WM_KEYUP || wparam == WM_SYSKEYUP);
    if (is_modifier(data->vkCode)) {
        update_modifier_state(data->vkCode, !key_up);
        if (!key_up &&
            ((is_shift_key(data->vkCode) && (g_alt_down || g_control_down)) ||
             ((is_alt_key(data->vkCode) || is_control_key(data->vkCode)) &&
              g_shift_down))) {
            clear_intent();
            clear_history();
        }
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
    if (data->vkCode == VK_SPACE && g_windows_down) clear_intent();

    if (data->vkCode == VK_BACK && !shortcut_modifier_down() &&
        g_undo.valid && GetForegroundWindow() == g_undo.window &&
        GetTickCount64() - g_undo.created_at <= 15000u) {
        clear_word();
        if (try_undo(1)) {
            g_suppressed_vk = data->vkCode;
            g_suppressed_at = GetTickCount();
            return 1;
        }
    }

    g_undo.valid = 0;
    if (!g_settings.enabled || shortcut_modifier_down()) {
        clear_word();
        clear_history();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    foreground = GetForegroundWindow();
    language = foreground_language(foreground);
    if (!foreground || language == KS_LANG_OTHER) {
        clear_word();
        clear_history();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if ((g_history_window && g_history_window != foreground) ||
        (g_pending_word_valid && g_pending_word_window != foreground)) {
        clear_intent();
        clear_history();
    }
    if (g_has_context && (foreground != g_word_window || language != g_word_language)) {
        if (foreground != g_word_window) {
            clear_intent();
            clear_history();
        }
        clear_word();
    }

    if (data->vkCode == VK_BACK) {
        cancel_smart_correction();
        g_last_word_key_at = 0;
        if (g_overflow_count > 0) --g_overflow_count;
        else if (g_word_count > 0) --g_word_count;
        if (!g_word_count && !g_overflow_count) {
            clear_word();
            clear_history();
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if (is_navigation(data->vkCode)) {
        clear_word();
        reset_sentence(foreground);
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    shift = g_shift_down;
    caps = (GetKeyState(VK_CAPITAL) & 1) != 0;
    if (map_physical_key(data->scanCode, shift, caps, &token)) {
        /*
         * A live correction becomes a completed sentence word only after the
         * user presses Space. If another word key arrives first, our inferred
         * boundary was wrong and the cached sentence/caret model is unsafe.
         */
        if (g_pending_word_valid) clear_history();
        cancel_smart_correction();
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
        observe_typing_interval();

        if (!g_overflow_count) {
            intent = current_intent(foreground, &intent_strength);
            live_result = ks_evaluate_contextual(
                g_word, g_word_count, g_word_language, g_settings.sensitivity,
                intent, intent_strength, sentence_start(foreground), KS_PHASE_LIVE,
                &g_lexicons,
                &decision);
            if (live_result == KS_LIVE_CORRECT_NOW) {
                if (apply_decision(foreground, &decision, g_word_count - 1, 0)) {
                    store_pending_word(foreground, g_word, g_word_count,
                                       decision.target_language);
                    g_suppressed_vk = data->vkCode;
                    g_suppressed_at = GetTickCount();
                    clear_word();
                    return 1;
                }
            } else if (live_result == KS_LIVE_WAIT_FOR_IDLE) {
                schedule_smart_correction();
            }
        }

        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    if (is_correction_boundary(data->vkCode)) {
        int english_known = 0;
        int persian_known = 0;
        int english_frequent = 0;
        int persian_frequent = 0;
        int had_word = g_word_count > 0 || g_overflow_count > 0;
        int retained_word = 0;
        int terminates_sentence = is_sentence_terminator(data->vkCode);
        InterlockedIncrement(&g_words_checked);
        if (!had_word && g_pending_word_valid) {
            if (data->vkCode == VK_SPACE &&
                g_pending_word_window == foreground) {
                commit_pending_word(foreground, data->vkCode);
                mark_sentence_word(foreground);
                clear_word();
                return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
            }
            clear_history();
        }
        intent = current_intent(foreground, &intent_strength);
        /*
         * Re-evaluate the longest available phrase before committing to an
         * isolated-word decision. A later word can therefore supply the
         * missing evidence for earlier ambiguous text.
         */
        if (!g_skip_word && !g_overflow_count && g_word_count > 0 &&
            try_sequence_correction(
                foreground, g_word, g_word_count, g_word_language,
                data->vkCode, intent, intent_strength)) {
            g_suppressed_vk = data->vkCode;
            g_suppressed_at = GetTickCount();
            if (terminates_sentence) start_new_sentence(foreground);
            clear_word();
            return 1;
        }
        if (!g_skip_word && !g_overflow_count &&
            ks_evaluate_contextual(
                g_word, g_word_count, g_word_language, g_settings.sensitivity,
                intent, intent_strength, sentence_start(foreground),
                KS_PHASE_BOUNDARY, &g_lexicons,
                &decision) == KS_LIVE_CORRECT_NOW) {
            if (apply_decision(foreground, &decision, g_word_count, data->vkCode)) {
                history_push(foreground, g_word, g_word_count,
                             decision.target_language, data->vkCode);
                g_suppressed_vk = data->vkCode;
                g_suppressed_at = GetTickCount();
                if (terminates_sentence) start_new_sentence(foreground);
                clear_word();
                return 1;
            }
        } else if (!g_skip_word && !g_overflow_count &&
                   ks_classify_word(g_word, g_word_count, &g_lexicons,
                                    &english_known, &persian_known,
                                    &english_frequent, &persian_frequent)) {
            int active_known =
                g_word_language == KS_LANG_ENGLISH ? english_known : persian_known;
            int active_frequent =
                g_word_language == KS_LANG_ENGLISH
                    ? english_frequent : persian_frequent;
            int ambiguous = english_known && persian_known;
            if (active_known) {
                remember_intent(foreground, g_word_language,
                                ambiguous ? 1 : active_frequent ? 3 : 2);
            }
        }
        if (!g_skip_word && !g_overflow_count && g_word_count > 0) {
            history_push(foreground, g_word, g_word_count,
                         g_word_language, data->vkCode);
            retained_word = data->vkCode == VK_SPACE;
        }
        if (had_word && !retained_word) clear_history();
        if (!had_word) clear_history();
        if (had_word) mark_sentence_word(foreground);
        if (terminates_sentence) start_new_sentence(foreground);
    } else if (is_sentence_terminator(data->vkCode)) {
        start_new_sentence(foreground);
    } else {
        /*
         * Punctuation, editing commands, and unsupported characters make the
         * cached caret model unreliable. Drop phrase history rather than
         * risking deletion of unrelated text.
         */
        clear_history();
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
            clear_history();
            /*
             * A click commonly moves the caret inside the same document. Keep
             * the per-window sentence language so the first ambiguous word
             * after that click still has useful context. current_intent()
             * refuses to apply it to a different foreground window.
             */
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
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
    safe_copy(g_tray.szTip, sizeof(g_tray.szTip) / sizeof(wchar_t), L"KeySwitchFix — Active");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
}

static void update_tray_tip(void) {
    g_tray.uFlags = NIF_TIP | NIF_SHOWTIP;
    safe_copy(g_tray.szTip, sizeof(g_tray.szTip) / sizeof(wchar_t),
              g_settings.enabled ? L"KeySwitchFix — Active" : L"KeySwitchFix — Paused");
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

static void show_tray_menu(void) {
    HMENU menu = CreatePopupMenu();
    HMENU language_menu = CreatePopupMenu();
    POINT point;
    AppendMenuW(menu, MF_STRING, IDM_OPEN, L"Open KeySwitchFix");
    AppendMenuW(menu, MF_STRING | (g_settings.enabled ? MF_CHECKED : 0), IDM_TOGGLE,
                L"Enable automatic correction");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(language_menu,
                MF_STRING | (g_settings.language_mode == 0 ? MF_CHECKED : 0),
                IDM_LANGUAGE_AUTO, L"Auto — use sentence context");
    AppendMenuW(language_menu,
                MF_STRING | (g_settings.language_mode == 1 ? MF_CHECKED : 0),
                IDM_LANGUAGE_PERSIAN, L"Prefer Persian for collisions");
    AppendMenuW(language_menu,
                MF_STRING | (g_settings.language_mode == 2 ? MF_CHECKED : 0),
                IDM_LANGUAGE_ENGLISH, L"Prefer English for collisions");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)language_menu, L"Writing language");
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
    SendMessageW(g_language_mode, CB_SETCURSEL, (WPARAM)g_settings.language_mode, 0);
    SendMessageW(g_startup, BM_SETCHECK, g_settings.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_excluded, g_settings.excluded);
    InvalidateRect(g_enable_button, NULL, TRUE);
    update_tray_tip();
}

static void read_controls_to_settings(void) {
    LRESULT selection = SendMessageW(g_sensitivity, CB_GETCURSEL, 0, 0);
    if (selection >= 0 && selection <= 2) g_settings.sensitivity = (int)selection;
    selection = SendMessageW(g_language_mode, CB_GETCURSEL, 0, 0);
    if (selection >= 0 && selection <= 2) g_settings.language_mode = (int)selection;
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
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"Keyboard hook: %ls  •  Undo: Backspace (or Ctrl + Win + Backspace)",
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
                              500, 290, 210, 25, window, (HMENU)IDC_APP_STARTUP, g_instance, NULL);
    g_language_mode = CreateWindowW(WC_COMBOBOXW, L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                    150, 324, 190, 200, window,
                                    (HMENU)IDC_LANGUAGE_MODE, g_instance, NULL);
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Auto — sentence context");
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Prefer Persian collisions");
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Prefer English collisions");
    g_excluded = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 150, 358, 560, 27, window, (HMENU)IDC_EXCLUDED, g_instance, NULL);

    g_hook_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 48, 444, 640, 22,
                                 window, (HMENU)IDC_HOOK_LABEL, g_instance, NULL);
    g_activity_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
                                     48, 476, 640, 24, window, (HMENU)IDC_ACTIVITY_LABEL, g_instance, NULL);
    g_counts_label = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 48, 510, 640, 22,
                                   window, (HMENU)IDC_COUNTS_LABEL, g_instance, NULL);

    CreateWindowW(L"BUTTON", L"Save settings", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  24, 608, 150, 38, window, (HMENU)IDC_SAVE, g_instance, NULL);
    CreateWindowW(L"BUTTON", L"Hide to tray", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                  184, 608, 140, 38, window, (HMENU)IDC_HIDE, g_instance, NULL);

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
    draw_card(dc, 24, 246, 736, 392);
    draw_card(dc, 24, 410, 736, 560);

    SelectObject(dc, g_font_medium);
    SetTextColor(dc, RGB(48, 59, 82));
    TextOutW(dc, 48, 260, L"Correction settings", lstrlenW(L"Correction settings"));
    TextOutW(dc, 48, 424, L"Live diagnostics", lstrlenW(L"Live diagnostics"));
    SelectObject(dc, g_font_regular);
    SetTextColor(dc, RGB(99, 112, 137));
    TextOutW(dc, 48, 294, L"Sensitivity", lstrlenW(L"Sensitivity"));
    TextOutW(dc, 48, 328, L"Writing language", lstrlenW(L"Writing language"));
    TextOutW(dc, 48, 362, L"Excluded apps", lstrlenW(L"Excluded apps"));
    TextOutW(dc, 340, 617, L"No cloud, no logging, no background service",
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
                    clear_history();
                    g_undo.valid = 0;
                    save_settings();
                    update_controls_from_settings();
                    set_activity(g_settings.enabled ? L"Automatic correction enabled."
                                                    : L"Automatic correction paused.");
                    return 0;
                case IDC_SAVE:
                    read_controls_to_settings();
                    clear_word();
                    clear_history();
                    g_undo.valid = 0;
                    save_settings();
                    set_activity(L"Settings saved.");
                    return 0;
                case IDC_HIDE:
                    ShowWindow(window, SW_HIDE);
                    return 0;
                case IDM_OPEN:
                    show_main_window();
                    return 0;
                case IDM_LANGUAGE_AUTO:
                case IDM_LANGUAGE_PERSIAN:
                case IDM_LANGUAGE_ENGLISH:
                    g_settings.language_mode =
                        LOWORD(wparam) == IDM_LANGUAGE_AUTO ? 0 :
                        LOWORD(wparam) == IDM_LANGUAGE_PERSIAN ? 1 : 2;
                    clear_word();
                    clear_history();
                    clear_intent();
                    g_undo.valid = 0;
                    save_settings();
                    update_controls_from_settings();
                    set_activity(g_settings.language_mode == 0
                        ? L"Writing language: automatic sentence context."
                        : g_settings.language_mode == 1
                            ? L"Writing language: Persian wins ambiguous collisions."
                            : L"Writing language: English wins ambiguous collisions.");
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
                    try_undo(0);
                }
                return 0;
            }
            if (wparam == ID_TIMER_SMART_CORRECTION) {
                try_smart_correction();
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
            KillTimer(window, ID_TIMER_SMART_CORRECTION);
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
        !load_bloom_resource(IDR_FA_BLOOM, &g_persian_bloom) ||
        !load_bloom_resource(IDR_EN_COMMON_BLOOM, &g_english_common_bloom) ||
        !load_bloom_resource(IDR_FA_COMMON_BLOOM, &g_persian_common_bloom) ||
        !load_bloom_resource(IDR_EN_FREQUENT_BLOOM, &g_english_frequent_bloom) ||
        !load_bloom_resource(IDR_FA_FREQUENT_BLOOM, &g_persian_frequent_bloom) ||
        !load_bloom_resource(IDR_EN_PREFIX_BLOOM, &g_english_prefix_bloom) ||
        !load_bloom_resource(IDR_FA_PREFIX_BLOOM, &g_persian_prefix_bloom) ||
        !load_bloom_resource(IDR_EN_COMMON_PREFIX, &g_english_common_prefix_bloom) ||
        !load_bloom_resource(IDR_FA_COMMON_PREFIX, &g_persian_common_prefix_bloom)) {
        MessageBoxW(NULL, L"Dictionary resources are damaged. Please reinstall KeySwitchFix.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 2;
    }
    g_lexicons.english_words = &g_english_bloom;
    g_lexicons.persian_words = &g_persian_bloom;
    g_lexicons.english_common = &g_english_common_bloom;
    g_lexicons.persian_common = &g_persian_common_bloom;
    g_lexicons.english_frequent = &g_english_frequent_bloom;
    g_lexicons.persian_frequent = &g_persian_frequent_bloom;
    g_lexicons.english_prefixes = &g_english_prefix_bloom;
    g_lexicons.persian_prefixes = &g_persian_prefix_bloom;
    g_lexicons.english_common_prefixes = &g_english_common_prefix_bloom;
    g_lexicons.persian_common_prefixes = &g_persian_common_prefix_bloom;
    ks_context_reset(&g_intent_context);
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

    g_window = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"KeySwitchFix 2.7.0",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, 776, 694, NULL, NULL, instance, NULL);
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
