#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "core.h"
#include "spell.h"
#include "../resources/resource.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#define APP_NAME L"KeySwitchFix"
#define APP_VERSION L"2.9.0"
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
#define ID_TIMER_HOOK_WATCHDOG 14
#define ID_HOTKEY_TOGGLE 15

/*
 * Windows silently removes a low-level hook whose callback exceeds the
 * LowLevelHooksTimeout budget (a debugger, a hung target, or a system stall
 * is enough). Nothing tells the application; SetWindowsHookEx's handle stays
 * non-NULL. The watchdog compares GetLastInputInfo with the last event the
 * hooks actually delivered and reinstalls them when they fell silent.
 */
#define HOOK_WATCHDOG_INTERVAL_MS 5000u
#define HOOK_SILENCE_LIMIT_MS 4000u
#define ZWNJ 0x200Cu

#define IDC_ENABLE 100
#define IDC_SENSITIVITY 101
#define IDC_APP_STARTUP 102
#define IDC_EXCLUDED 103
#define IDC_LANGUAGE_MODE 104
#define IDC_SAVE 105
#define IDC_HIDE 106
#define IDC_SPELLING 107
#define IDC_PERSONAL_DICTIONARY 108
#define IDC_TILE_VALUE 120      /* 120..122 */
#define IDC_TILE_CAPTION 130    /* 130..132 */
#define IDC_STATUS_LABEL 110
#define IDC_LAYOUT_LABEL 111
#define IDC_HOOK_LABEL 112
#define IDC_ACTIVITY_LABEL 113

#define IDM_OPEN 200
#define IDM_TOGGLE 201
#define IDM_EXIT 203
#define IDM_LANGUAGE_AUTO 204
#define IDM_LANGUAGE_PERSIAN 205
#define IDM_LANGUAGE_ENGLISH 206
#define IDM_EXCLUDE_CURRENT 207
#define IDM_SPELLING 208

#define INPUT_MARKER ((ULONG_PTR)0x4B534632u)
#define KS_MAX_PHRASE_CHARS KS_MAX_SEQUENCE_CHARS

typedef struct SETTINGS {
    int enabled;
    int sensitivity;
    int language_mode;
    int start_with_windows;
    /* KS_SPELL_OFF .. KS_SPELL_AGGRESSIVE; the level restored by the tray
       toggle when spelling is switched back on. */
    int spelling;
    int spelling_last_level;
    /* Opt-in: words whose correction the user undoes are saved to a personal
       dictionary file and never corrected again. */
    int personal_dictionary;
    wchar_t excluded[512];
} SETTINGS;

typedef struct UNDO_RECORD {
    int valid;
    HWND window;
    KS_LANGUAGE source_language;
    UINT delimiter;
    int delimiter_zwnj;
    /* 1 when this was a spelling fix; undoing it teaches the ignore list. */
    int spelling;
    ULONGLONG created_at;
    wchar_t original[KS_MAX_PHRASE_CHARS + 1];
    wchar_t replacement[KS_MAX_PHRASE_CHARS + 1];
} UNDO_RECORD;

typedef struct WORD_HISTORY {
    KS_TOKEN tokens[KS_MAX_WORD];
    int count;
    KS_LANGUAGE visible_language;
    /* The character that followed this word on screen: Space or ZWNJ. */
    wchar_t separator;
} WORD_HISTORY;

static HINSTANCE g_instance;
static HWND g_window;
static HWND g_status_label;
static HWND g_layout_label;
static HWND g_hook_label;
static HWND g_activity_label;
static HWND g_enable_button;
static HWND g_sensitivity;
static HWND g_language_mode;
static HWND g_spelling;
static HWND g_startup;
static HWND g_excluded;
static HHOOK g_keyboard_hook;
static HHOOK g_mouse_hook;
static NOTIFYICONDATAW g_tray;
static HFONT g_font_regular;
static HFONT g_font_medium;
static HFONT g_font_title;
static HFONT g_font_status;
static HFONT g_font_tile;
static HFONT g_font_small;
static HBRUSH g_brush_white;
static HBRUSH g_brush_background;
static SETTINGS g_settings;
static wchar_t g_settings_path[MAX_PATH];
static wchar_t g_data_directory[MAX_PATH];
static int g_first_run;
static int g_exit_requested;
static int g_hotkey_registered;
static int g_toggle_hotkey_registered;
static int g_dpi = 96;
static DWORD g_last_hook_tick;
static int g_hook_reinstalls;
static DWORD g_hook_reinstalled_at;
static wchar_t g_last_typed_process[MAX_PATH];
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
static KS_RANK_TABLE g_english_rank_table;
static KS_RANK_TABLE g_persian_rank_table;
static KS_SPELL_LEXICON g_english_spelling;
static KS_SPELL_LEXICON g_persian_spelling;
static KS_IGNORE_LIST g_spelling_ignore;
static KS_VOCAB g_session_vocabulary;
static KS_VOCAB g_personal_vocabulary;
static wchar_t g_personal_dictionary_path[MAX_PATH];
static int g_spelling_available;
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
static volatile LONG g_spelling_fixes;
static wchar_t g_last_activity[256] = L"Waiting for keyboard input...";

static LRESULT CALLBACK main_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK keyboard_hook_proc(int code, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK mouse_hook_proc(int code, WPARAM wparam, LPARAM lparam);
static void update_diagnostics_ui(void);

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

static int load_rank_resource(int identifier, KS_RANK_TABLE *table) {
    HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(identifier), RT_RCDATA);
    HGLOBAL loaded;
    const unsigned char *data;
    DWORD size;
    memset(table, 0, sizeof(*table));
    if (!resource) return 0;
    size = SizeofResource(g_instance, resource);
    loaded = LoadResource(g_instance, resource);
    if (!loaded) return 0;
    data = (const unsigned char *)LockResource(loaded);
    return data && ks_rank_table_init(table, data, (size_t)size);
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
    swprintf(g_personal_dictionary_path,
             sizeof(g_personal_dictionary_path) / sizeof(g_personal_dictionary_path[0]),
             L"%ls\\personal-dictionary.txt", g_data_directory);
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
    g_settings.spelling = GetPrivateProfileIntW(L"Spelling", L"Level", KS_SPELL_BALANCED, g_settings_path);
    if (g_settings.spelling < KS_SPELL_OFF || g_settings.spelling > KS_SPELL_AGGRESSIVE)
        g_settings.spelling = KS_SPELL_BALANCED;
    g_settings.spelling_last_level = GetPrivateProfileIntW(L"Spelling", L"LastLevel", KS_SPELL_BALANCED, g_settings_path);
    if (g_settings.spelling_last_level < KS_SPELL_CONSERVATIVE ||
        g_settings.spelling_last_level > KS_SPELL_AGGRESSIVE)
        g_settings.spelling_last_level = KS_SPELL_BALANCED;
    if (g_settings.spelling) g_settings.spelling_last_level = g_settings.spelling;
    g_settings.personal_dictionary =
        GetPrivateProfileIntW(L"Spelling", L"PersonalDictionary", 0, g_settings_path) != 0;
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
    swprintf(number, 16, L"%d", g_settings.spelling);
    WritePrivateProfileStringW(L"Spelling", L"Level", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.spelling_last_level);
    WritePrivateProfileStringW(L"Spelling", L"LastLevel", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.personal_dictionary);
    WritePrivateProfileStringW(L"Spelling", L"PersonalDictionary", number, g_settings_path);
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
                         KS_LANGUAGE visible_language, UINT delimiter,
                         int delimiter_zwnj) {
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
    g_history[g_history_count].separator =
        delimiter_zwnj ? (wchar_t)ZWNJ : L' ';
    g_history_chars += count + (g_history_count ? 1 : 0);
    ++g_history_count;
}

static int commit_pending_word(HWND window, UINT delimiter,
                               int delimiter_zwnj) {
    WORD_HISTORY pending;
    if (!g_pending_word_valid || g_pending_word_window != window ||
        delimiter != VK_SPACE)
        return 0;
    pending = g_pending_word;
    g_pending_word_valid = 0;
    g_pending_word_window = NULL;
    history_push(window, pending.tokens, pending.count,
                 pending.visible_language, delimiter, delimiter_zwnj);
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

/* Returns the executable file name (without directory) of a window's process. */
static int query_process_basename(HWND window, wchar_t *name, size_t capacity) {
    DWORD process_id = 0;
    HANDLE process;
    wchar_t path[MAX_PATH];
    DWORD length = MAX_PATH;
    const wchar_t *base;

    if (!name || capacity == 0) return 0;
    name[0] = 0;
    if (!window) return 0;
    GetWindowThreadProcessId(window, &process_id);
    if (!process_id) return 0;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return 0;
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) {
        CloseHandle(process);
        return 0;
    }
    CloseHandle(process);
    base = wcsrchr(path, L'\\');
    safe_copy(name, capacity, base ? base + 1 : path);
    return name[0] != 0;
}

static int excluded_list_contains(const wchar_t *name) {
    const wchar_t *cursor = g_settings.excluded;
    if (!name || !*name) return 0;
    while (*cursor) {
        const wchar_t *start;
        const wchar_t *end;
        while (*cursor == L' ' || *cursor == L',' || *cursor == L';') ++cursor;
        start = cursor;
        while (*cursor && *cursor != L',' && *cursor != L';') ++cursor;
        end = cursor;
        while (end > start && end[-1] == L' ') --end;
        if (end > start && basename_equals(name, start, (size_t)(end - start))) return 1;
    }
    return 0;
}

static void excluded_list_toggle(const wchar_t *name) {
    wchar_t rebuilt[512];
    const wchar_t *cursor = g_settings.excluded;
    size_t used = 0;
    int removed = 0;

    if (!name || !*name) return;
    rebuilt[0] = 0;
    while (*cursor) {
        const wchar_t *start;
        const wchar_t *end;
        size_t length;
        while (*cursor == L' ' || *cursor == L',' || *cursor == L';') ++cursor;
        start = cursor;
        while (*cursor && *cursor != L',' && *cursor != L';') ++cursor;
        end = cursor;
        while (end > start && end[-1] == L' ') --end;
        length = (size_t)(end - start);
        if (length == 0) continue;
        if (basename_equals(name, start, length)) {
            removed = 1;
            continue;
        }
        if (used + length + 2 >= sizeof(rebuilt) / sizeof(rebuilt[0])) break;
        if (used) rebuilt[used++] = L',';
        memcpy(rebuilt + used, start, length * sizeof(wchar_t));
        used += length;
        rebuilt[used] = 0;
    }
    if (!removed) {
        size_t length = wcslen(name);
        if (used + length + 2 < sizeof(rebuilt) / sizeof(rebuilt[0])) {
            if (used) rebuilt[used++] = L',';
            memcpy(rebuilt + used, name, length * sizeof(wchar_t));
            used += length;
            rebuilt[used] = 0;
        }
    }
    safe_copy(g_settings.excluded, sizeof(g_settings.excluded) / sizeof(wchar_t), rebuilt);
}

static int process_is_excluded(HWND foreground) {
    DWORD process_id = 0;
    wchar_t name[MAX_PATH];

    GetWindowThreadProcessId(foreground, &process_id);
    if (!process_id || process_id == GetCurrentProcessId()) return 1;
    if (!query_process_basename(foreground, name, MAX_PATH)) return 0;
    /* Remembered so the tray menu can offer "Exclude <this app>". */
    safe_copy(g_last_typed_process, MAX_PATH, name);
    return excluded_list_contains(name);
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
            if (language_from_layout(layouts[i]) == language) {
                if (language == KS_LANG_PERSIAN) g_last_persian_layout = layouts[i];
                else g_last_english_layout = layouts[i];
                return layouts[i];
            }
        }
    }
    /*
     * Never call LoadKeyboardLayout here. It would silently add a keyboard to
     * the user's language bar on every keystroke whenever one of the two
     * languages is not installed. The static fallback table still translates
     * keys, and the diagnostics explain what is missing.
     */
    return NULL;
}

static const wchar_t *missing_layout_name(void) {
    if (!find_layout(KS_LANG_PERSIAN)) return L"Persian";
    if (!find_layout(KS_LANG_ENGLISH)) return L"English";
    return NULL;
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
    if (!english_ok) token->english = fallback_ok ? fallback.english : 0;
    if (!persian_ok) token->persian = fallback_ok ? fallback.persian : 0;
    if (token->english == 0 || token->persian == 0) return 0;
    /*
     * Diacritics produced by Shift+letter on the Persian layout stay in the
     * token: the core strips them for dictionary lookup, and the English
     * side ("Excel" mistyped on the Persian layout) must remain correctable.
     */
    token->persian = ks_canonical_persian(token->persian);
    return 1;
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

/*
 * delimiter_zwnj: the boundary was Shift+Space, which the Persian layouts
 * turn into a zero-width non-joiner (می‌خواهم, کتاب‌ها). Replaying it as a
 * plain VK_SPACE would race the layout switch and usually insert a visible
 * space, so the exact character is injected instead when the target text is
 * Persian.
 */
static int send_replacement(HWND foreground, int delete_count, const wchar_t *replacement,
                            UINT delimiter, int delimiter_zwnj,
                            KS_LANGUAGE target_language) {
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
    if (delimiter == VK_SPACE && delimiter_zwnj) {
        /*
         * Shift is still physically down while these events are processed,
         * so a replayed VK_SPACE would become whatever the *current* layout
         * makes of Shift+Space. Inject the exact character instead: a ZWNJ
         * for Persian text, a plain space for English.
         */
        add_unicode_input(inputs, &count,
                          target_language == KS_LANG_PERSIAN ? (wchar_t)ZWNJ : L' ');
    } else if (delimiter) {
        add_virtual_input(inputs, &count, (WORD)delimiter);
    }
    if (SendInput(count, inputs, sizeof(INPUT)) != count) {
        set_activity(L"Windows blocked text replacement. Match the target app's privilege level.");
        return 0;
    }
    request_layout(foreground, target_language);
    return 1;
}

static void store_phrase_undo(HWND foreground, KS_LANGUAGE source_language,
                              UINT delimiter, int delimiter_zwnj,
                              const wchar_t *original,
                              const wchar_t *replacement) {
    ZeroMemory(&g_undo, sizeof(g_undo));
    g_undo.valid = 1;
    g_undo.window = foreground;
    g_undo.source_language = source_language;
    g_undo.delimiter = delimiter;
    g_undo.delimiter_zwnj = delimiter_zwnj;
    g_undo.created_at = GetTickCount64();
    safe_copy(g_undo.original, KS_MAX_PHRASE_CHARS + 1, original);
    safe_copy(g_undo.replacement, KS_MAX_PHRASE_CHARS + 1, replacement);
}

static void store_undo(HWND foreground, const KS_DECISION *decision,
                       UINT delimiter, int delimiter_zwnj) {
    store_phrase_undo(foreground, decision->source_language, delimiter,
                      delimiter_zwnj, decision->original, decision->replacement);
}

static int apply_decision(HWND foreground, const KS_DECISION *decision,
                          int delete_count, UINT delimiter, int delimiter_zwnj) {
    if (is_protected_field(foreground)) {
        set_activity(L"Correction skipped in a protected password field.");
        return 0;
    }
    if (!send_replacement(foreground, delete_count, decision->replacement,
                          delimiter, delimiter_zwnj,
                          decision->target_language)) return 0;
    store_undo(foreground, decision, delimiter, delimiter_zwnj);
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
                              const wchar_t *word, wchar_t separator) {
    size_t length = wcslen(phrase);
    size_t word_length = wcslen(word);
    if (separator) {
        if (length + 1 >= capacity) return 0;
        phrase[length++] = separator;
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
                                   UINT delimiter, int delimiter_zwnj,
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
            wchar_t separator =
                index > 0 ? g_history[start + index - 1].separator : 0;
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
                                    word_text, separator))
                return 0;
            tokens_to_language(words[index].tokens, words[index].count,
                               result.language, word_text);
            /* A ZWNJ only exists in Persian; English words get a space. */
            if (separator == (wchar_t)ZWNJ && result.language != KS_LANG_PERSIAN)
                separator = L' ';
            if (!append_phrase_word(replacement,
                                    sizeof(replacement) /
                                        sizeof(replacement[0]),
                                    word_text, separator))
                return 0;
        }
        if (!needs_change) return 0;
        if (is_protected_field(foreground)) {
            set_activity(L"Correction skipped in a protected password field.");
            return 0;
        }
        if (!send_replacement(foreground, (int)wcslen(original), replacement,
                              delimiter, delimiter_zwnj, result.language))
            return 0;
        store_phrase_undo(foreground, current_visible_language, delimiter,
                          delimiter_zwnj, original, replacement);
        /*
         * Keep monitoring from the beginning of the sentence. Only the
         * corrected suffix changes its visible language; earlier words remain
         * exactly as tracked. The current word is then committed with the
         * Space that SendInput already inserted.
         */
        for (index = 0; index < word_count - 1; ++index) {
            g_history[start + index].visible_language = result.language;
            if (result.language != KS_LANG_PERSIAN)
                g_history[start + index].separator = L' ';
        }
        history_push(foreground, current_tokens, current_count,
                     result.language, delimiter, delimiter_zwnj);
        mark_sentence_word(foreground);
        remember_intent(foreground, result.language, 4);
        InterlockedIncrement(&g_corrections);
        set_activity_pair(L"Phrase corrected", original, replacement);
        return 1;
    }
    return 0;
}

/*
 * Personal dictionary: one word per line, UTF-8. Loaded only when the user
 * has opted in; every word is trusted (never corrected). Written only when a
 * spelling fix is undone while the option is on.
 */
static int personal_dictionary_lines;

static void save_personal_dictionary(void) {
    HANDLE file;
    int i;
    file = CreateFileW(g_personal_dictionary_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    for (i = 0; i < g_personal_vocabulary.count; ++i) {
        char utf8[KS_MAX_WORD * 4 + 4];
        DWORD written = 0;
        int length = WideCharToMultiByte(CP_UTF8, 0, g_personal_vocabulary.entries[i].text, -1,
                                         utf8, (int)sizeof(utf8) - 3, NULL, NULL);
        if (length <= 1) continue;
        utf8[length - 1] = '\r';
        utf8[length] = '\n';
        WriteFile(file, utf8, (DWORD)(length + 1), &written, NULL);
    }
    CloseHandle(file);
    personal_dictionary_lines = g_personal_vocabulary.count;
}

static void load_personal_dictionary(void) {
    HANDLE file;
    DWORD size;
    DWORD read = 0;
    char *bytes;
    wchar_t *text = NULL;
    int characters = 0;
    wchar_t *cursor;
    int lines = 0;
    int utf16 = 0;

    ks_vocab_reset(&g_personal_vocabulary);
    personal_dictionary_lines = 0;
    if (!g_settings.personal_dictionary) return;
    file = CreateFileW(g_personal_dictionary_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;      /* no dictionary yet */
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(file);
        return;
    }
    if (size > 4u * 1024u * 1024u) {
        CloseHandle(file);
        set_activity(L"personal-dictionary.txt is larger than 4 MB and was not loaded.");
        return;
    }
    bytes = (char *)HeapAlloc(GetProcessHeap(), 0, size + 2);
    if (!bytes) {
        CloseHandle(file);
        return;
    }
    if (!ReadFile(file, bytes, size, &read, NULL)) read = 0;
    CloseHandle(file);
    bytes[read] = 0;
    bytes[read + 1] = 0;

    /* Notepad may have saved the file as UTF-16 LE; honour its BOM. */
    utf16 = read >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE;
    if (utf16) {
        characters = (int)((read - 2) / sizeof(wchar_t));
        text = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, ((size_t)characters + 1) * sizeof(wchar_t));
        if (text) {
            memcpy(text, bytes + 2, (size_t)characters * sizeof(wchar_t));
            text[characters] = 0;
        }
    } else {
        characters = MultiByteToWideChar(CP_UTF8, 0, bytes, (int)read, NULL, 0);
        if (characters > 0)
            text = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, ((size_t)characters + 1) * sizeof(wchar_t));
        if (text) {
            MultiByteToWideChar(CP_UTF8, 0, bytes, (int)read, text, characters);
            text[characters] = 0;
        }
    }
    if (text) {
        cursor = text;
        if (*cursor == 0xFEFF) ++cursor;
        while (*cursor) {
            wchar_t *end = cursor;
            wchar_t *start;
            while (*end && *end != L'\n' && *end != L'\r') ++end;
            /* Trim spaces and tabs on both sides. */
            start = cursor;
            while (start < end && (*start == L' ' || *start == L'\t')) ++start;
            while (end > start && (end[-1] == L' ' || end[-1] == L'\t')) --end;
            if (end > start && (size_t)(end - start) <= KS_MAX_WORD && *start != 0xFFFD) {
                wchar_t word[KS_MAX_WORD + 1];
                memcpy(word, start, (size_t)(end - start) * sizeof(wchar_t));
                word[end - start] = 0;
                ks_vocab_trust(&g_personal_vocabulary, word);   /* deduplicates */
                ++lines;
            }
            while (*end == L'\n' || *end == L'\r') ++end;
            cursor = end;
        }
        HeapFree(GetProcessHeap(), 0, text);
    }
    HeapFree(GetProcessHeap(), 0, bytes);
    personal_dictionary_lines = lines;
    /* Rewrite a file that has duplicates, whitespace, UTF-16, or more lines
       than the ring keeps, so it never grows without bound. */
    if (utf16 || lines != g_personal_vocabulary.count || lines > KS_VOCAB_CAPACITY)
        save_personal_dictionary();
}

static void append_personal_dictionary(const wchar_t *word) {
    HANDLE file;
    char utf8[KS_MAX_WORD * 4 + 4];
    int length;
    DWORD written = 0;

    if (!g_settings.personal_dictionary || !word || !*word) return;
    if (ks_vocab_trusted(&g_personal_vocabulary, word)) return;
    ks_vocab_trust(&g_personal_vocabulary, word);
    if (personal_dictionary_lines >= KS_VOCAB_CAPACITY) {
        /* The ring has recycled its oldest entry; compact the file to match. */
        save_personal_dictionary();
        return;
    }
    length = WideCharToMultiByte(CP_UTF8, 0, word, -1, utf8, (int)sizeof(utf8) - 3, NULL, NULL);
    if (length <= 1) return;
    utf8[length - 1] = '\r';
    utf8[length] = '\n';
    file = CreateFileW(g_personal_dictionary_path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        set_activity(L"The personal dictionary could not be written.");
        return;
    }
    WriteFile(file, utf8, (DWORD)(length + 1), &written, NULL);
    CloseHandle(file);
    ++personal_dictionary_lines;
}

/*
 * Spelling correction runs only after the layout logic has declined: the word
 * is unknown in the active language AND its other-layout reading is unknown
 * too, so it is neither a layout mistake nor a collision. It fires at a word
 * boundary only, on lowercase English or letter-only Persian, and one plain
 * Backspace restores the typed spelling and remembers it for the session.
 */
/*
 * Code editors and terminals are full of identifiers that sit one edit away
 * from a frequent word (bool/book, endl/end, async/sync). Layout repair stays
 * active there, but spelling correction is skipped below Aggressive. The
 * user-editable "Excluded apps" list still disables everything.
 */
static int spelling_skipped_process(HWND foreground) {
    static const wchar_t *const developer_tools[] = {
        L"WindowsTerminal.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe",
        L"conhost.exe", L"OpenConsole.exe", L"mintty.exe", L"alacritty.exe",
        L"wezterm-gui.exe", L"putty.exe", L"Code.exe", L"Code - Insiders.exe",
        L"Cursor.exe", L"windsurf.exe", L"devenv.exe", L"idea64.exe",
        L"pycharm64.exe", L"webstorm64.exe", L"phpstorm64.exe", L"rider64.exe",
        L"clion64.exe", L"goland64.exe", L"datagrip64.exe", L"studio64.exe",
        L"sublime_text.exe", L"notepad++.exe", L"atom.exe", L"ssms.exe",
        L"sqldeveloper64W.exe", L"dbeaver.exe", L"HeidiSQL.exe",
        L"git-bash.exe", L"bash.exe", L"wsl.exe", L"ubuntu.exe"
    };
    wchar_t name[MAX_PATH];
    size_t i;
    /* Queried fresh: g_last_typed_process is only refreshed when a query
       succeeds at word start and may describe an earlier application. */
    if (!query_process_basename(foreground, name, MAX_PATH)) return 0;
    for (i = 0; i < sizeof(developer_tools) / sizeof(developer_tools[0]); ++i) {
        if (_wcsicmp(name, developer_tools[i]) == 0) return 1;
    }
    return 0;
}

#define SPELL_NOT_CONSULTED 0   /* off, unavailable, or not a candidate word */
#define SPELL_APPLIED 1
#define SPELL_DECLINED 2        /* the model looked and found nothing safe */
#define SPELL_SUPPRESSED 3      /* a fix existed but the context forbids it */

static int try_spelling_correction(HWND foreground, UINT delimiter, int delimiter_zwnj) {
    wchar_t typed[KS_MAX_WORD + 1];
    KS_SPELL_RESULT result;
    KS_DECISION decision;
    const KS_SPELL_LEXICON *lexicon;

    if (!g_spelling_available || g_settings.spelling == KS_SPELL_OFF) return SPELL_NOT_CONSULTED;
    if (!foreground || g_word_count < 3 || g_word_count > KS_MAX_WORD) return SPELL_NOT_CONSULTED;
    if (g_word_language != KS_LANG_ENGLISH && g_word_language != KS_LANG_PERSIAN) return SPELL_NOT_CONSULTED;
    lexicon = g_word_language == KS_LANG_PERSIAN ? &g_persian_spelling : &g_english_spelling;
    tokens_to_language(g_word, g_word_count, g_word_language, typed);
    if (!ks_spell_correct(typed, g_settings.spelling, lexicon, &g_spelling_ignore, &result) ||
        !result.should_correct)
        return SPELL_DECLINED;
    /* The process and password-field queries cost system calls; they run
       only once a correction is actually about to be applied. */
    if (g_settings.spelling < KS_SPELL_AGGRESSIVE && spelling_skipped_process(foreground))
        return SPELL_SUPPRESSED;
    if (is_protected_field(foreground)) {
        set_activity(L"Correction skipped in a protected password field.");
        return SPELL_SUPPRESSED;
    }
    memset(&decision, 0, sizeof(decision));
    decision.should_correct = 1;
    decision.key_count = g_word_count;
    decision.confidence = result.confidence;
    decision.source_language = g_word_language;
    decision.target_language = g_word_language;
    safe_copy(decision.original, KS_MAX_WORD + 1, result.original);
    safe_copy(decision.replacement, KS_MAX_WORD + 1, result.replacement);
    if (!send_replacement(foreground, g_word_count, decision.replacement,
                          delimiter, delimiter_zwnj, g_word_language))
        return 0;
    store_undo(foreground, &decision, delimiter, delimiter_zwnj);
    g_undo.spelling = 1;
    mark_sentence_word(foreground);
    remember_intent(foreground, g_word_language, 2);
    InterlockedIncrement(&g_spelling_fixes);
    set_activity_pair(result.kind == KS_SPELL_KIND_ZWNJ ? L"Half-space"
                      : result.kind == KS_SPELL_KIND_SPLIT ? L"Missing space"
                      : L"Spelling",
                      decision.original, decision.replacement);
    /* The physical tokens no longer describe the text on screen, so the
       sentence model cannot safely rewrite this word again. */
    clear_history();
    return SPELL_APPLIED;
}

/*
 * The learned vocabulary is trained only from words the model actually
 * examined and left alone, in a context where it would have corrected them:
 * never from developer tools, password fields, or when spelling is off.
 */
static void observe_vocabulary(HWND foreground) {
    wchar_t typed[KS_MAX_WORD + 1];
    if (g_word_count < 3 || g_word_count > KS_MAX_WORD) return;
    if (g_settings.spelling < KS_SPELL_AGGRESSIVE && spelling_skipped_process(foreground)) return;
    if (is_protected_field(foreground)) return;
    tokens_to_language(g_word, g_word_count, g_word_language, typed);
    ks_vocab_observe(&g_session_vocabulary, typed);
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
        apply_decision(foreground, &decision, g_word_count, 0, 0)) {
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
                         restored_delimiter, g_undo.delimiter_zwnj,
                         g_undo.source_language)) {
        set_activity_pair(L"Restored", g_undo.replacement, g_undo.original);
        /* The user rejected a spelling fix: that spelling is now theirs.
           It is written to disk only when the personal dictionary is on. */
        if (g_undo.spelling) {
            /* One undo makes the word trusted for this session. It reaches
               the personal dictionary only when the user has typed or
               restored it before, so a stray Backspace cannot teach a typo
               permanently. */
            int seen_before = ks_vocab_observe(&g_session_vocabulary, g_undo.original) >= 2;
            ks_ignore_list_add(&g_spelling_ignore, g_undo.original);
            ks_vocab_trust(&g_session_vocabulary, g_undo.original);
            if (seen_before) append_personal_dictionary(g_undo.original);
            if (g_spelling_fixes > 0) InterlockedDecrement(&g_spelling_fixes);
        }
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
    int mapped;
    int zwnj_key;

    if (code < 0) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    g_last_hook_tick = GetTickCount();
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
    /*
     * The legacy Windows Persian layout also produces a ZWNJ from a letter
     * key (Shift+B). When the active layout is Persian, that key is a word
     * boundary exactly like Shift+Space, not part of the word.
     */
    mapped = map_physical_key(data->scanCode, shift, caps, &token);
    zwnj_key = mapped && language == KS_LANG_PERSIAN &&
               token.persian == (wchar_t)ZWNJ;
    if (mapped && !zwnj_key) {
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
                if (apply_decision(foreground, &decision, g_word_count - 1, 0, 0)) {
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

    if (zwnj_key || is_correction_boundary(data->vkCode)) {
        int english_known = 0;
        int persian_known = 0;
        int english_frequent = 0;
        int persian_frequent = 0;
        int had_word = g_word_count > 0 || g_overflow_count > 0;
        int retained_word = 0;
        /*
         * Shift+Space is the zero-width non-joiner on both Windows Persian
         * layouts. It ends the current token exactly like Space, but the
         * character on screen (and any replayed delimiter) must stay a ZWNJ.
         * A ZWNJ letter key is modelled as the same Space-with-ZWNJ boundary.
         */
        UINT boundary_key = zwnj_key ? VK_SPACE : data->vkCode;
        int zwnj = zwnj_key || (boundary_key == VK_SPACE && g_shift_down);
        int terminates_sentence = is_sentence_terminator(boundary_key);
        if (had_word) InterlockedIncrement(&g_words_checked);
        if (!had_word && g_pending_word_valid) {
            if (boundary_key == VK_SPACE &&
                g_pending_word_window == foreground) {
                commit_pending_word(foreground, boundary_key, zwnj);
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
                boundary_key, zwnj, intent, intent_strength)) {
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
            if (apply_decision(foreground, &decision, g_word_count,
                               boundary_key, zwnj)) {
                history_push(foreground, g_word, g_word_count,
                             decision.target_language, boundary_key, zwnj);
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
            int target_known =
                g_word_language == KS_LANG_ENGLISH ? persian_known : english_known;
            int consult_spelling = 0;
            if (active_known) {
                remember_intent(foreground, g_word_language,
                                ambiguous ? 1 : active_frequent ? 3 : 2);
            }
            /*
             * When is the spelling model consulted?
             *  - the word is unknown in both layouts: a misspelling or the
             *    user's own word;
             *  - Persian, known, unambiguous: the base dictionary stores
             *    ZWNJ-free spellings, so میپرسیدند is "known" although the
             *    standard form is می‌پرسیدند; only the joiner is restored;
             *  - English, "known" only through the raw corpus tier (alot,
             *    thankyou): the layout logic rightly trusts that tier, the
             *    spelling lexicon deliberately does not.
             */
            if (!target_known) {
                if (!active_known) consult_spelling = 1;
                else if (g_word_language == KS_LANG_PERSIAN && !ambiguous) consult_spelling = 1;
                else if (g_word_language == KS_LANG_ENGLISH && g_spelling_available &&
                         g_word_count >= 3) {
                    wchar_t typed[KS_MAX_WORD + 1];
                    tokens_to_language(g_word, g_word_count, g_word_language, typed);
                    if (!ks_spell_known(typed, &g_english_spelling)) consult_spelling = 1;
                }
            }
            if (consult_spelling) {
                int outcome = try_spelling_correction(foreground, boundary_key, zwnj);
                if (outcome == SPELL_APPLIED) {
                    g_suppressed_vk = data->vkCode;
                    g_suppressed_at = GetTickCount();
                    if (terminates_sentence) start_new_sentence(foreground);
                    clear_word();
                    return 1;
                }
                if (outcome == SPELL_DECLINED && !active_known) observe_vocabulary(foreground);
            }
        }
        if (!g_skip_word && !g_overflow_count && g_word_count > 0) {
            history_push(foreground, g_word, g_word_count,
                         g_word_language, boundary_key, zwnj);
            retained_word = boundary_key == VK_SPACE;
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
    if (code >= 0) g_last_hook_tick = GetTickCount();
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

/*
 * Returns 1 when a previously installed hook had already been detached by
 * Windows (UnhookWindowsHookEx fails with ERROR_INVALID_HOOK_HANDLE), which is
 * the only reliable sign that the silence was a real hook death rather than
 * input this process was never allowed to see.
 */
static int install_hooks(void) {
    int was_detached = 0;
    if (g_keyboard_hook && !UnhookWindowsHookEx(g_keyboard_hook)) was_detached = 1;
    if (g_mouse_hook && !UnhookWindowsHookEx(g_mouse_hook)) was_detached = 1;
    g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook_proc, g_instance, 0);
    g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, g_instance, 0);
    g_last_hook_tick = GetTickCount();
    clear_word();
    clear_history();
    g_undo.valid = 0;
    return was_detached;
}

static void check_hook_health(void) {
    LASTINPUTINFO info;
    DWORD silence;
    int was_detached;

    if (!g_keyboard_hook || !g_mouse_hook) {
        /*
         * A failed SetWindowsHookEx is retried on every interval, but only
         * for the hook that is missing: the healthy one keeps running and
         * the in-progress word, sentence, and Undo state are left alone.
         */
        int keyboard_was_down = !g_keyboard_hook;
        if (!g_keyboard_hook)
            g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook_proc, g_instance, 0);
        if (!g_mouse_hook)
            g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, g_instance, 0);
        if (keyboard_was_down && g_keyboard_hook) {
            g_last_hook_tick = GetTickCount();
            set_activity(L"Keyboard hook is running again.");
        }
        return;
    }
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) return;
    /*
     * GetLastInputInfo reports the newest input the system delivered. Our
     * hooks see every keyboard and mouse event (including the ones we
     * inject), so the system being well ahead of them means either that
     * Windows detached a hook, or that the input went to an elevated window
     * or the secure desktop, which UIPI hides from a non-elevated hook.
     * Re-arm once per quiet episode and wait for a real event before
     * considering another reinstall, instead of churning every interval.
     */
    silence = info.dwTime - g_last_hook_tick;
    if ((LONG)silence <= (LONG)HOOK_SILENCE_LIMIT_MS) return;
    if (g_hook_reinstalled_at &&
        (LONG)(g_last_hook_tick - g_hook_reinstalled_at) <= 0) return;
    was_detached = install_hooks();
    g_hook_reinstalled_at = g_last_hook_tick;
    if (!g_keyboard_hook || !g_mouse_hook) {
        set_activity(L"Keyboard hook FAILED to reinstall. Restart the app or check security software.");
        return;
    }
    if (was_detached) {
        /* Only a confirmed detachment is counted and shown to the user. */
        ++g_hook_reinstalls;
        set_activity(L"Windows had detached the keyboard hook; it has been reinstalled.");
    }
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
    AppendMenuW(menu, MF_STRING |
                      (g_settings.spelling != KS_SPELL_OFF ? MF_CHECKED : 0) |
                      (g_spelling_available ? 0 : MF_GRAYED),
                IDM_SPELLING, L"Fix spelling mistakes");
    if (g_last_typed_process[0]) {
        wchar_t label[MAX_PATH + 48];
        swprintf(label, sizeof(label) / sizeof(label[0]),
                 excluded_list_contains(g_last_typed_process)
                     ? L"Resume correction in %ls"
                     : L"Exclude %ls",
                 g_last_typed_process);
        AppendMenuW(menu, MF_STRING, IDM_EXCLUDE_CURRENT, label);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    GetCursorPos(&point);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, g_window, NULL);
    /* Required after TrackPopupMenu from a tray icon (KB Q135788), otherwise
       the menu does not dismiss when the user clicks elsewhere. */
    PostMessageW(g_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void show_main_window(void) {
    ShowWindow(g_window, SW_SHOWNORMAL);
    SetForegroundWindow(g_window);
    /* WM_SHOWWINDOW is not delivered for every restore path; make sure the
       diagnostics refresh is running whenever the dashboard is on screen. */
    update_diagnostics_ui();
    SetTimer(g_window, ID_TIMER_STATUS, 500, NULL);
}

/* ------------------------------------------------------------------------ */
/* Dashboard                                                                 */
/*                                                                           */
/* Geometry is authored at 96 DPI on an 840x748 client area and scaled once  */
/* at startup. Every label column is wide enough for its longest caption in  */
/* Segoe UI 16px with room to spare, so nothing is clipped by a neighbour.   */
/* ------------------------------------------------------------------------ */

#define UI_CLIENT_WIDTH 840
#define UI_CLIENT_HEIGHT 752
#define UI_HEADER_HEIGHT 128
#define UI_MARGIN 32
#define UI_CARD_LEFT UI_MARGIN
#define UI_CARD_RIGHT (UI_CLIENT_WIDTH - UI_MARGIN)
#define UI_LABEL_LEFT 56
#define UI_CONTROL_LEFT 260
#define UI_CONTROL_WIDTH 300
#define UI_SIDE_LEFT 572
#define UI_SIDE_WIDTH 224

static const COLORREF UI_HEADER_TOP = RGB(22, 34, 66);
static const COLORREF UI_HEADER_BOTTOM = RGB(44, 72, 132);
static const COLORREF UI_BACKGROUND = RGB(245, 247, 251);
static const COLORREF UI_CARD_BORDER = RGB(222, 228, 238);
static const COLORREF UI_TEXT = RGB(40, 51, 74);
static const COLORREF UI_MUTED = RGB(104, 116, 140);
static const COLORREF UI_ACCENT = RGB(76, 111, 230);
static const COLORREF UI_GREEN = RGB(38, 176, 120);
static const COLORREF UI_GREY = RGB(139, 148, 166);
static const COLORREF UI_TILE = RGB(240, 244, 251);

static HBRUSH g_brush_tile;
static HWND g_tile_values[3];
static HWND g_tile_captions[3];
static HWND g_personal_dictionary;

static int scale(int value) {
    return MulDiv(value, g_dpi, 96);
}

static HFONT create_ui_font(int height, int weight) {
    return CreateFontW(-scale(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Segoe UI");
}

static HWND create_child(const wchar_t *class_name, const wchar_t *text, DWORD style,
                         DWORD extended_style, int left, int top, int width, int height,
                         HWND parent, int identifier) {
    return CreateWindowExW(extended_style, class_name, text, WS_CHILD | WS_VISIBLE | style,
                           scale(left), scale(top), scale(width), scale(height),
                           parent, (HMENU)(INT_PTR)identifier, g_instance, NULL);
}

static void update_controls_from_settings(void) {
    SendMessageW(g_sensitivity, CB_SETCURSEL, (WPARAM)g_settings.sensitivity, 0);
    SendMessageW(g_language_mode, CB_SETCURSEL, (WPARAM)g_settings.language_mode, 0);
    SendMessageW(g_spelling, CB_SETCURSEL, (WPARAM)g_settings.spelling, 0);
    EnableWindow(g_spelling, g_spelling_available);
    EnableWindow(g_personal_dictionary, g_spelling_available);
    SendMessageW(g_personal_dictionary, BM_SETCHECK,
                 g_settings.personal_dictionary ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_startup, BM_SETCHECK, g_settings.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g_excluded, g_settings.excluded);
    InvalidateRect(g_enable_button, NULL, TRUE);
    if (g_window) {
        /* The header pill shows the active/paused state. */
        RECT header = {0, 0, scale(UI_CLIENT_WIDTH), scale(UI_HEADER_HEIGHT)};
        InvalidateRect(g_window, &header, FALSE);
    }
    update_tray_tip();
}

static void read_controls_to_settings(void) {
    LRESULT selection = SendMessageW(g_sensitivity, CB_GETCURSEL, 0, 0);
    int personal_before = g_settings.personal_dictionary;
    if (selection >= 0 && selection <= 2) g_settings.sensitivity = (int)selection;
    selection = SendMessageW(g_language_mode, CB_GETCURSEL, 0, 0);
    if (selection >= 0 && selection <= 2) g_settings.language_mode = (int)selection;
    selection = SendMessageW(g_spelling, CB_GETCURSEL, 0, 0);
    if (selection >= KS_SPELL_OFF && selection <= KS_SPELL_AGGRESSIVE) {
        g_settings.spelling = (int)selection;
        if (g_settings.spelling) g_settings.spelling_last_level = g_settings.spelling;
    }
    g_settings.personal_dictionary =
        SendMessageW(g_personal_dictionary, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (g_settings.personal_dictionary && !personal_before) load_personal_dictionary();
    if (!g_settings.personal_dictionary) ks_vocab_reset(&g_personal_vocabulary);
    g_settings.start_with_windows = SendMessageW(g_startup, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(g_excluded, g_settings.excluded,
                   (int)(sizeof(g_settings.excluded) / sizeof(wchar_t)));
}

/* SetWindowText repaints even when nothing changed; on a 500 ms timer that
   shows up as flicker. Only touch a label whose text is actually different. */
static void set_label_text(HWND label, const wchar_t *text) {
    wchar_t current[512];
    if (!label) return;
    current[0] = 0;
    GetWindowTextW(label, current, (int)(sizeof(current) / sizeof(current[0])));
    if (wcscmp(current, text) != 0) SetWindowTextW(label, text);
}

static void set_tile_value(int index, LONG value) {
    wchar_t buffer[32];
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%ld", value);
    set_label_text(g_tile_values[index], buffer);
}

static void update_diagnostics_ui(void) {
    wchar_t buffer[512];
    wchar_t process_name[MAX_PATH];
    HWND foreground = GetForegroundWindow();
    KS_LANGUAGE language;
    const wchar_t *missing_layout;

    if (foreground == g_window) foreground = g_word_window;
    if (!query_process_basename(foreground, process_name, MAX_PATH))
        safe_copy(process_name, MAX_PATH, L"No application yet");
    language = foreground_language(foreground);

    set_label_text(g_status_label, g_settings.enabled
        ? L"Protection is active" : L"Protection is paused");
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"Typing in %ls  •  %ls layout%ls",
             process_name, language_name(language),
             g_spelling_available ? L"" : L"  •  spelling data missing");
    set_label_text(g_layout_label, buffer);

    missing_layout = missing_layout_name();
    if (missing_layout) {
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"The %ls keyboard layout is not installed in Windows. Add it under Settings > Time & language > Language.",
                 missing_layout);
    } else if (g_hook_reinstalls) {
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"Keyboard hook: %ls (re-armed %d×)  •  Undo: Backspace  •  Pause: Ctrl + Win + K",
                 g_keyboard_hook ? L"Running" : L"FAILED", g_hook_reinstalls);
    } else {
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"Keyboard hook: %ls  •  Undo: Backspace or Ctrl + Win + Backspace  •  Pause: Ctrl + Win + K",
                 g_keyboard_hook ? L"Running" : L"FAILED");
    }
    set_label_text(g_hook_label, buffer);
    set_label_text(g_activity_label, g_last_activity);
    set_tile_value(0, g_keys_seen);
    set_tile_value(1, g_corrections);
    set_tile_value(2, g_spelling_fixes);
    InvalidateRect(g_enable_button, NULL, TRUE);
}

static void create_tile(HWND window, int index, int left, int top, const wchar_t *caption) {
    g_tile_values[index] = create_child(L"STATIC", L"0", SS_ENDELLIPSIS, 0,
                                        left + 16, top + 10, 200, 32, window, IDC_TILE_VALUE + index);
    g_tile_captions[index] = create_child(L"STATIC", caption, SS_ENDELLIPSIS, 0,
                                          left + 16, top + 44, 200, 20, window,
                                          IDC_TILE_CAPTION + index);
}

static void create_ui(HWND window) {
    g_font_regular = create_ui_font(16, FW_NORMAL);
    g_font_medium = create_ui_font(17, FW_SEMIBOLD);
    g_font_title = create_ui_font(30, FW_BOLD);
    g_font_status = create_ui_font(22, FW_SEMIBOLD);
    g_font_tile = create_ui_font(26, FW_BOLD);
    g_font_small = create_ui_font(14, FW_NORMAL);

    /* Card 1: status */
    g_status_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                  UI_LABEL_LEFT, 172, 500, 32, window, IDC_STATUS_LABEL);
    g_layout_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                  UI_LABEL_LEFT, 210, 560, 22, window, IDC_LAYOUT_LABEL);
    g_enable_button = create_child(L"BUTTON", L"", BS_OWNERDRAW, 0,
                                   652, 178, 140, 42, window, IDC_ENABLE);

    /* Card 2: settings */
    g_sensitivity = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 0,
                                 UI_CONTROL_LEFT, 322, UI_CONTROL_WIDTH, 200, window, IDC_SENSITIVITY);
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Conservative");
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Balanced (recommended)");
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Sensitive");
    g_startup = create_child(L"BUTTON", L"Start with Windows", BS_AUTOCHECKBOX, 0,
                             UI_SIDE_LEFT, 324, UI_SIDE_WIDTH, 26, window, IDC_APP_STARTUP);

    g_language_mode = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 0,
                                   UI_CONTROL_LEFT, 364, UI_CONTROL_WIDTH, 200, window, IDC_LANGUAGE_MODE);
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Auto — sentence context");
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Prefer Persian for collisions");
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Prefer English for collisions");

    g_spelling = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 0,
                              UI_CONTROL_LEFT, 406, UI_CONTROL_WIDTH, 200, window, IDC_SPELLING);
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Conservative");
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Balanced (recommended)");
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Aggressive");
    g_personal_dictionary = create_child(L"BUTTON", L"Remember undone words", BS_AUTOCHECKBOX, 0,
                                         UI_SIDE_LEFT, 408, UI_SIDE_WIDTH, 26, window,
                                         IDC_PERSONAL_DICTIONARY);

    g_excluded = create_child(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                              UI_CONTROL_LEFT, 448, UI_CARD_RIGHT - UI_CONTROL_LEFT - 24,
                              28, window, IDC_EXCLUDED);

    /* Card 3: diagnostics */
    create_tile(window, 0, UI_LABEL_LEFT, 560, L"keys observed");
    create_tile(window, 1, UI_LABEL_LEFT + 248, 560, L"layout fixes");
    create_tile(window, 2, UI_LABEL_LEFT + 496, 560, L"spelling fixes");
    g_hook_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                UI_LABEL_LEFT, 640, 736, 22, window, IDC_HOOK_LABEL);
    g_activity_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                    UI_LABEL_LEFT, 666, 736, 22, window, IDC_ACTIVITY_LABEL);

    create_child(L"BUTTON", L"Save settings", BS_OWNERDRAW, 0, UI_MARGIN, 704, 160, 36, window, IDC_SAVE);
    create_child(L"BUTTON", L"Hide to tray", BS_OWNERDRAW, 0, UI_MARGIN + 172, 704, 150, 36, window, IDC_HIDE);

    {
        HWND child = GetWindow(window, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_font_regular, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }
    }
    SendMessageW(g_status_label, WM_SETFONT, (WPARAM)g_font_status, TRUE);
    SendMessageW(g_layout_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(g_hook_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(g_activity_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    {
        int i;
        for (i = 0; i < 3; ++i) {
            SendMessageW(g_tile_values[i], WM_SETFONT, (WPARAM)g_font_tile, TRUE);
            SendMessageW(g_tile_captions[i], WM_SETFONT, (WPARAM)g_font_small, TRUE);
        }
    }
    g_brush_white = CreateSolidBrush(RGB(255, 255, 255));
    g_brush_background = CreateSolidBrush(UI_BACKGROUND);
    g_brush_tile = CreateSolidBrush(UI_TILE);
    update_controls_from_settings();
}

static int is_tile_label(HWND control) {
    int i;
    for (i = 0; i < 3; ++i)
        if (control == g_tile_values[i] || control == g_tile_captions[i]) return 1;
    return 0;
}

static void fill_round_rect(HDC dc, int left, int top, int right, int bottom, int radius,
                            COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, scale(left), scale(top), scale(right), scale(bottom), scale(radius), scale(radius));
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void draw_card(HDC dc, int left, int top, int right, int bottom) {
    fill_round_rect(dc, left, top, right, bottom, 18, RGB(255, 255, 255), UI_CARD_BORDER);
}

static void draw_text(HDC dc, int left, int top, const wchar_t *text) {
    TextOutW(dc, scale(left), scale(top), text, lstrlenW(text));
}

static void draw_text_right(HDC dc, int right, int top, const wchar_t *text) {
    SIZE extent;
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &extent);
    TextOutW(dc, scale(right) - extent.cx, scale(top), text, lstrlenW(text));
}

static void draw_header(HDC dc, const RECT *client) {
    int height = scale(UI_HEADER_HEIGHT);
    int y;
    /* Vertical gradient in 4-pixel bands: no msimg32 dependency. */
    for (y = 0; y < height; y += 4) {
        RECT band = {0, y, client->right, y + 4 < height ? y + 4 : height};
        int r = GetRValue(UI_HEADER_TOP) + (GetRValue(UI_HEADER_BOTTOM) - GetRValue(UI_HEADER_TOP)) * y / height;
        int g = GetGValue(UI_HEADER_TOP) + (GetGValue(UI_HEADER_BOTTOM) - GetGValue(UI_HEADER_TOP)) * y / height;
        int b = GetBValue(UI_HEADER_TOP) + (GetBValue(UI_HEADER_BOTTOM) - GetBValue(UI_HEADER_TOP)) * y / height;
        HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
        FillRect(dc, &band, brush);
        DeleteObject(brush);
    }
    SelectObject(dc, g_font_title);
    SetTextColor(dc, RGB(255, 255, 255));
    draw_text(dc, UI_MARGIN, 30, L"KeySwitchFix");
    SelectObject(dc, g_font_regular);
    SetTextColor(dc, RGB(196, 208, 236));
    draw_text(dc, UI_MARGIN + 2, 76, L"Persian ↔ English layout repair and spelling correction");
    SelectObject(dc, g_font_small);
    SetTextColor(dc, RGB(160, 176, 214));
    draw_text_right(dc, UI_CARD_RIGHT, 96, L"v" APP_VERSION L"  •  offline  •  no logging");

    /* Status pill */
    {
        const wchar_t *text = g_settings.enabled ? L"Active" : L"Paused";
        COLORREF dot = g_settings.enabled ? UI_GREEN : UI_GREY;
        fill_round_rect(dc, UI_CARD_RIGHT - 120, 36, UI_CARD_RIGHT, 68, 16,
                        RGB(52, 82, 146), RGB(70, 104, 174));
        {
            HBRUSH brush = CreateSolidBrush(dot);
            HPEN pen = CreatePen(PS_SOLID, 1, dot);
            HGDIOBJ old_brush = SelectObject(dc, brush);
            HGDIOBJ old_pen = SelectObject(dc, pen);
            Ellipse(dc, scale(UI_CARD_RIGHT - 104), scale(46), scale(UI_CARD_RIGHT - 92), scale(58));
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(pen);
            DeleteObject(brush);
        }
        SelectObject(dc, g_font_medium);
        SetTextColor(dc, RGB(255, 255, 255));
        draw_text(dc, UI_CARD_RIGHT - 82, 41, text);
    }
}

static void paint_main_window(HWND window) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    RECT client;
    HFONT old_font;
    SetBkMode(dc, TRANSPARENT);
    GetClientRect(window, &client);
    FillRect(dc, &client, g_brush_background);
    old_font = (HFONT)SelectObject(dc, g_font_title);
    draw_header(dc, &client);

    draw_card(dc, UI_CARD_LEFT, 152, UI_CARD_RIGHT, 252);
    draw_card(dc, UI_CARD_LEFT, 272, UI_CARD_RIGHT, 500);
    draw_card(dc, UI_CARD_LEFT, 520, UI_CARD_RIGHT, 692);

    /* Tiles */
    fill_round_rect(dc, UI_LABEL_LEFT, 560, UI_LABEL_LEFT + 232, 624, 14, UI_TILE, UI_TILE);
    fill_round_rect(dc, UI_LABEL_LEFT + 248, 560, UI_LABEL_LEFT + 480, 624, 14, UI_TILE, UI_TILE);
    fill_round_rect(dc, UI_LABEL_LEFT + 496, 560, UI_LABEL_LEFT + 728, 624, 14, UI_TILE, UI_TILE);

    SelectObject(dc, g_font_medium);
    SetTextColor(dc, UI_TEXT);
    draw_text(dc, UI_LABEL_LEFT, 286, L"Correction settings");
    draw_text(dc, UI_LABEL_LEFT, 534, L"Live diagnostics");
    SelectObject(dc, g_font_regular);
    SetTextColor(dc, UI_MUTED);
    draw_text(dc, UI_LABEL_LEFT, 326, L"Sensitivity");
    draw_text(dc, UI_LABEL_LEFT, 368, L"Writing language");
    draw_text(dc, UI_LABEL_LEFT, 410, L"Spelling");
    draw_text(dc, UI_LABEL_LEFT, 452, L"Excluded apps");
    SelectObject(dc, g_font_small);
    draw_text_right(dc, UI_CARD_RIGHT, 713, L"No cloud, no logging, no background service");
    SelectObject(dc, old_font);
    EndPaint(window, &paint);
}

static void draw_button(DRAWITEMSTRUCT *item) {
    wchar_t text[64];
    HBRUSH brush;
    HPEN pen;
    RECT rectangle = item->rcItem;
    HGDIOBJ old_brush;
    HGDIOBJ old_pen;
    HGDIOBJ old_font;
    COLORREF color;
    if (item->CtlID == IDC_ENABLE) color = g_settings.enabled ? UI_GREEN : UI_GREY;
    else if (item->CtlID == IDC_SAVE) color = UI_ACCENT;
    else color = RGB(93, 111, 148);
    if (item->itemState & ODS_SELECTED) color = RGB(GetRValue(color) * 4 / 5,
                                                    GetGValue(color) * 4 / 5,
                                                    GetBValue(color) * 4 / 5);
    brush = CreateSolidBrush(color);
    pen = CreatePen(PS_SOLID, 1, color);
    old_brush = SelectObject(item->hDC, brush);
    old_pen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom,
              scale(14), scale(14));
    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
    if (item->CtlID == IDC_ENABLE)
        safe_copy(text, 64, g_settings.enabled ? L"Pause" : L"Resume");
    else
        GetWindowTextW(item->hwndItem, text, 64);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, RGB(255, 255, 255));
    old_font = SelectObject(item->hDC, g_font_medium);
    DrawTextW(item->hDC, text, -1, &rectangle, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(item->hDC, old_font);
}

static void toggle_enabled(void) {
    g_settings.enabled = !g_settings.enabled;
    clear_word();
    clear_history();
    g_undo.valid = 0;
    save_settings();
    update_controls_from_settings();
    set_activity(g_settings.enabled ? L"Automatic correction enabled."
                                    : L"Automatic correction paused.");
}

static LRESULT CALLBACK main_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_taskbar_created_message && message == g_taskbar_created_message) {
        add_tray_icon();
        return 0;
    }
    switch (message) {
        case WM_CREATE:
            create_ui(window);
            SetTimer(window, ID_TIMER_HOOK_WATCHDOG, HOOK_WATCHDOG_INTERVAL_MS, NULL);
            return 0;
        case WM_SHOWWINDOW:
            /* The 500 ms diagnostics refresh only needs to run while the
               dashboard is visible; hidden in the tray it was pure overhead. */
            if (wparam) {
                update_diagnostics_ui();
                SetTimer(window, ID_TIMER_STATUS, 500, NULL);
            } else {
                KillTimer(window, ID_TIMER_STATUS);
            }
            break;
        case WM_PAINT:
            paint_main_window(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
            if (is_tile_label((HWND)lparam)) {
                int is_value = (HWND)lparam == g_tile_values[0] ||
                               (HWND)lparam == g_tile_values[1] ||
                               (HWND)lparam == g_tile_values[2];
                SetBkColor((HDC)wparam, UI_TILE);
                SetTextColor((HDC)wparam, is_value ? UI_ACCENT : UI_MUTED);
                return (LRESULT)g_brush_tile;
            }
            SetBkColor((HDC)wparam, RGB(255, 255, 255));
            SetTextColor((HDC)wparam, (HWND)lparam == g_status_label ? UI_TEXT : RGB(62, 73, 96));
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
                    toggle_enabled();
                    return 0;
                case IDM_SPELLING:
                    g_settings.spelling = g_settings.spelling == KS_SPELL_OFF
                        ? g_settings.spelling_last_level : KS_SPELL_OFF;
                    clear_word();
                    g_undo.valid = 0;
                    save_settings();
                    update_controls_from_settings();
                    set_activity(g_settings.spelling == KS_SPELL_OFF
                        ? L"Spelling correction is off."
                        : g_settings.spelling == KS_SPELL_CONSERVATIVE
                            ? L"Spelling correction: conservative."
                            : g_settings.spelling == KS_SPELL_AGGRESSIVE
                                ? L"Spelling correction: aggressive."
                                : L"Spelling correction: balanced.");
                    return 0;
                case IDM_EXCLUDE_CURRENT:
                    if (g_last_typed_process[0]) {
                        wchar_t note[MAX_PATH + 64];
                        excluded_list_toggle(g_last_typed_process);
                        clear_word();
                        clear_history();
                        g_undo.valid = 0;
                        save_settings();
                        update_controls_from_settings();
                        swprintf(note, sizeof(note) / sizeof(note[0]),
                                 excluded_list_contains(g_last_typed_process)
                                     ? L"Correction is now skipped in %ls."
                                     : L"Correction is active again in %ls.",
                                 g_last_typed_process);
                        set_activity(note);
                    }
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
            if (wparam == ID_HOTKEY_TOGGLE) {
                toggle_enabled();
                return 0;
            }
            break;
        case WM_TIMER:
            if (wparam == ID_TIMER_STATUS) {
                if (IsWindowVisible(window) && !IsIconic(window))
                    update_diagnostics_ui();
                return 0;
            }
            if (wparam == ID_TIMER_HOOK_WATCHDOG) {
                check_hook_health();
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
            KillTimer(window, ID_TIMER_HOOK_WATCHDOG);
            if (g_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_UNDO);
            if (g_toggle_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_TOGGLE);
            if (g_keyboard_hook) UnhookWindowsHookEx(g_keyboard_hook);
            if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
            g_tray.uFlags = 0;
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            DeleteObject(g_font_regular);
            DeleteObject(g_font_medium);
            DeleteObject(g_font_title);
            DeleteObject(g_font_status);
            DeleteObject(g_font_tile);
            DeleteObject(g_font_small);
            DeleteObject(g_brush_white);
            DeleteObject(g_brush_background);
            DeleteObject(g_brush_tile);
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
    {
        HDC screen = GetDC(NULL);
        if (screen) {
            g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
            ReleaseDC(NULL, screen);
        }
        if (g_dpi < 96) g_dpi = 96;
    }
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
    /*
     * Spelling correction needs the frequency tables. They are generated from
     * wordfreq at build time; if a build shipped without them, the layout
     * repair keeps working and the dashboard says spelling is unavailable.
     */
    g_spelling_available =
        load_rank_resource(IDR_EN_RANK_TABLE, &g_english_rank_table) &&
        load_rank_resource(IDR_FA_RANK_TABLE, &g_persian_rank_table);
    memset(&g_english_spelling, 0, sizeof(g_english_spelling));
    g_english_spelling.language = KS_LANG_ENGLISH;
    g_english_spelling.ranks = &g_english_rank_table;
    g_english_spelling.words = &g_english_bloom;
    g_english_spelling.common = &g_english_common_bloom;
    memset(&g_persian_spelling, 0, sizeof(g_persian_spelling));
    g_persian_spelling.language = KS_LANG_PERSIAN;
    g_persian_spelling.ranks = &g_persian_rank_table;
    g_persian_spelling.words = &g_persian_bloom;
    g_persian_spelling.common = &g_persian_common_bloom;
    g_english_spelling.vocabulary = &g_session_vocabulary;
    g_persian_spelling.vocabulary = &g_session_vocabulary;
    g_english_spelling.personal = &g_personal_vocabulary;
    g_persian_spelling.personal = &g_personal_vocabulary;
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    ks_context_reset(&g_intent_context);
    load_settings();
    load_personal_dictionary();
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

    {
        /* Client area authored at UI_CLIENT_WIDTH × UI_CLIENT_HEIGHT (96 DPI). */
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT frame = {0, 0, scale(UI_CLIENT_WIDTH), scale(UI_CLIENT_HEIGHT)};
        AdjustWindowRectEx(&frame, style, FALSE, WS_EX_APPWINDOW);
        g_window = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"KeySwitchFix 2.9.0",
                                   style, CW_USEDEFAULT, CW_USEDEFAULT,
                                   frame.right - frame.left, frame.bottom - frame.top,
                                   NULL, NULL, instance, NULL);
    }
    if (!g_window) {
        MessageBoxW(NULL, L"The main window could not be created.", APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 4;
    }

    install_hooks();
    if (!g_keyboard_hook) set_activity(L"Keyboard hook FAILED. Restart the app or check security software.");
    else if (!g_mouse_hook) set_activity(L"Mouse hook FAILED; caret clicks cannot be observed. Check security software.");
    else if (missing_layout_name()) {
        set_activity(L"Both English and Persian keyboard layouts must be installed in Windows.");
    } else set_activity(L"Ready. Type normally in any app; correction is automatic.");
    g_hotkey_registered = RegisterHotKey(g_window, ID_HOTKEY_UNDO,
                                          MOD_CONTROL | MOD_WIN | MOD_NOREPEAT, VK_BACK);
    if (!g_hotkey_registered)
        set_activity(L"Protection is running, but the Undo hotkey is already used by another app.");
    /* Ctrl + Win + K pauses and resumes correction without opening the tray. */
    g_toggle_hotkey_registered = RegisterHotKey(g_window, ID_HOTKEY_TOGGLE,
                                                 MOD_CONTROL | MOD_WIN | MOD_NOREPEAT, 'K');
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
