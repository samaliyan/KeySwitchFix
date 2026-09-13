#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "core.h"
#include "spell.h"
#include "typing.h"
#include "../resources/resource.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#define APP_NAME L"KeySwitchFix"
#define APP_VERSION L"3.0.1"
#define APP_MUTEX L"Local\\KeySwitchFix.Native.2.0"
#define WINDOW_CLASS L"KeySwitchFix.MainWindow.2"

#define WM_APP_TRAY (WM_APP + 1)
#define WM_APP_DIAGNOSTIC (WM_APP + 2)
#define WM_APP_EXIT (WM_APP + 9)
/* Posted from the hook: work that must not run inside the hook callback. */
#define WM_APP_SAVE_STATS (WM_APP + 3)

#define ID_TRAY 1
#define ID_TIMER_STATUS 10
#define ID_TIMER_UNDO 11
#define ID_HOTKEY_UNDO 12
#define ID_TIMER_SMART_CORRECTION 13
#define ID_TIMER_HOOK_WATCHDOG 14
#define ID_HOTKEY_TOGGLE 15
#define ID_HOTKEY_CLEANUP 16
#define ID_TIMER_CLEANUP 17
#define ID_TIMER_STATS 18
#define ID_TIMER_SNIPPETS 19

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
#define IDC_DIGITS 109
#define IDC_PUNCTUATION 114
#define IDC_PERSIAN_LETTERS 115
#define IDC_CAPITALIZE 116
#define IDC_SNIPPETS 117
#define IDC_EDIT_SNIPPETS 118
#define IDC_STATS_LABEL 119
#define IDC_TILE_VALUE 120      /* 120..123 */
#define IDC_TILE_CAPTION 130    /* 130..133 */
#define UI_TILE_COUNT 4
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
#define IDM_PUNCTUATION 209
#define IDM_CAPITALIZE 210
#define IDM_SNIPPETS 211
#define IDM_EDIT_SNIPPETS 212
#define IDM_CLEANUP 213

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
    /* Typing helpers (3.0): digit policy (KS_DIGITS_*), Persian punctuation
       after Persian words, Arabic → Persian letters while typing, English
       sentence capitalisation, snippet expansion. */
    int digits;
    int punctuation;
    int persian_letters;
    int auto_capitalize;
    int snippets;
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
static int g_cleanup_hotkey_registered;
/* Snippets: the table, its file, and the file time it was loaded from. */
static KS_SNIPPET_TABLE g_snippets;
static wchar_t g_snippets_path[MAX_PATH];
static FILETIME g_snippets_loaded_time;
static DWORD g_snippets_checked_at;
/* Usage statistics: counters persist (stats.ini); words stay in memory. */
static KS_STATS g_stats;
static wchar_t g_stats_path[MAX_PATH];
/* English auto-capitalisation state machine (see arm_capitalization). */
static int g_capitalize_armed;
static int g_capitalize_next;
static HWND g_capitalize_window;
static int g_last_key_was_digit;
/* Language of the last finished word, for shaping the punctuation typed
   after it. */
static KS_LANGUAGE g_last_word_language;
static HWND g_last_word_window;
static DWORD g_last_word_at;
/* 1 while the key before the current word was Space/Enter/Tab: the word
   sits in prose, not after "(" or "=". */
static int g_previous_key_boundary;
static int g_word_after_boundary;
/* The first letter of the current word was capitalised by the hook. */
static int g_word_auto_capitalized;
/* Ctrl+Win+X clean-up of the selection: a small state machine driven by
   ID_TIMER_CLEANUP so no modifier is still held when Ctrl+C is injected. */
static int g_cleanup_step;
static DWORD g_cleanup_sequence;
static DWORD g_cleanup_started_at;
static int g_cleanup_retries;
/* The user's own clipboard, every HGLOBAL-based format, restored afterwards. */
#define CLIPBOARD_SNAPSHOT_MAX 32
typedef struct CLIPBOARD_SNAPSHOT {
    UINT format[CLIPBOARD_SNAPSHOT_MAX];
    void *data[CLIPBOARD_SNAPSHOT_MAX];
    SIZE_T size[CLIPBOARD_SNAPSHOT_MAX];
    int count;
    int valid;
} CLIPBOARD_SNAPSHOT;
static CLIPBOARD_SNAPSHOT g_cleanup_saved_clipboard;
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
/*
 * The window that really receives the user's keys. GetForegroundWindow() is
 * not always it: a Store/UWP application (the Windows 11 Notepad, Settings,
 * Mail, WhatsApp, ...) is hosted by ApplicationFrameHost.exe, whose frame
 * window is the foreground window while the text is rendered by a
 * Windows.UI.Core.CoreWindow child that lives in another process and thread.
 * Keyboard layouts are per thread, so the layout must be read from, and the
 * switch must be requested of, the thread that owns the focused window.
 */
typedef struct KS_INPUT_TARGET {
    HWND top;      /* the foreground window */
    HWND focus;    /* the window with keyboard focus (or the UWP CoreWindow) */
    DWORD thread;  /* the thread whose layout translates the keys */
} KS_INPUT_TARGET;
static KS_INPUT_TARGET g_target;

/* The most recent layout switch this application asked for. */
static HWND g_layout_request_window;
static KS_LANGUAGE g_layout_request_language;
/* The layout that was active when the switch was requested. */
static KS_LANGUAGE g_layout_request_from;
static DWORD g_layout_request_at;
/* Set once per request when the target is seen ignoring it; drives the
   diagnostics and the self-translation fallback. */
static int g_layout_request_unhonoured;
/* Keys the hook has typed itself for this request. */
static int g_layout_request_translated;
/* When the request was made; g_layout_request_at moves with every key typed
   for it, this does not, and bounds the whole episode. */
static DWORD g_layout_request_started;
static LONG g_layout_requests;
static LONG g_layout_requests_ignored;

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
/* The layout that actually rendered each key of the current word. Normally
   all equal g_word_language; they differ when a layout switch we requested
   landed in the middle of a word (see mixed-word repair). */
static KS_LANGUAGE g_word_visible[KS_MAX_WORD];
static int g_word_mixed;
static int g_word_count;
static int g_overflow_count;
static int g_has_context;
static int g_skip_word;
static HWND g_word_window;
static KS_LANGUAGE g_word_language;
/* Key-downs the hook swallowed: their key-ups must be swallowed too. One
   slot per virtual key, because fast typing overlaps key-downs and key-ups. */
static DWORD g_suppressed_at[256];
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
static volatile LONG g_snippets_used;
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
    swprintf(g_snippets_path, sizeof(g_snippets_path) / sizeof(g_snippets_path[0]),
             L"%ls\\snippets.txt", g_data_directory);
    swprintf(g_stats_path, sizeof(g_stats_path) / sizeof(g_stats_path[0]),
             L"%ls\\stats.ini", g_data_directory);
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
    g_settings.digits = GetPrivateProfileIntW(L"Typing", L"Digits", KS_DIGITS_BY_LAYOUT, g_settings_path);
    if (g_settings.digits < KS_DIGITS_OFF || g_settings.digits > KS_DIGITS_LATIN)
        g_settings.digits = KS_DIGITS_BY_LAYOUT;
    g_settings.punctuation = GetPrivateProfileIntW(L"Typing", L"Punctuation", 1, g_settings_path) != 0;
    g_settings.persian_letters = GetPrivateProfileIntW(L"Typing", L"PersianLetters", 1, g_settings_path) != 0;
    g_settings.auto_capitalize = GetPrivateProfileIntW(L"Typing", L"Capitalize", 1, g_settings_path) != 0;
    g_settings.snippets = GetPrivateProfileIntW(L"Typing", L"Snippets", 1, g_settings_path) != 0;
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
    swprintf(number, 16, L"%d", g_settings.digits);
    WritePrivateProfileStringW(L"Typing", L"Digits", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.punctuation);
    WritePrivateProfileStringW(L"Typing", L"Punctuation", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.persian_letters);
    WritePrivateProfileStringW(L"Typing", L"PersianLetters", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.auto_capitalize);
    WritePrivateProfileStringW(L"Typing", L"Capitalize", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.snippets);
    WritePrivateProfileStringW(L"Typing", L"Snippets", number, g_settings_path);
    update_startup_registry();
}

/* ---- Statistics persistence ---------------------------------------------- */

static void stats_load(void) {
    SYSTEMTIME now;
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.total_layout = GetPrivateProfileIntW(L"Stats", L"TotalLayout", 0, g_stats_path);
    g_stats.total_spelling = GetPrivateProfileIntW(L"Stats", L"TotalSpelling", 0, g_stats_path);
    g_stats.total_keys = GetPrivateProfileIntW(L"Stats", L"TotalKeys", 0, g_stats_path);
    g_stats.days_active = GetPrivateProfileIntW(L"Stats", L"DaysActive", 0, g_stats_path);
    g_stats.today_year = GetPrivateProfileIntW(L"Stats", L"TodayYear", 0, g_stats_path);
    g_stats.today_month = GetPrivateProfileIntW(L"Stats", L"TodayMonth", 0, g_stats_path);
    g_stats.today_day = GetPrivateProfileIntW(L"Stats", L"TodayDay", 0, g_stats_path);
    g_stats.today_layout = GetPrivateProfileIntW(L"Stats", L"TodayLayout", 0, g_stats_path);
    g_stats.today_spelling = GetPrivateProfileIntW(L"Stats", L"TodaySpelling", 0, g_stats_path);
    g_stats.today_keys = GetPrivateProfileIntW(L"Stats", L"TodayKeys", 0, g_stats_path);
    GetLocalTime(&now);
    ks_stats_roll_day(&g_stats, now.wYear, now.wMonth, now.wDay);
}

static void stats_save(void) {
    static const struct { const wchar_t *key; long *value; } fields[] = {
        {L"TotalLayout", &g_stats.total_layout}, {L"TotalSpelling", &g_stats.total_spelling},
        {L"TotalKeys", &g_stats.total_keys}, {L"DaysActive", &g_stats.days_active},
        {L"TodayLayout", &g_stats.today_layout}, {L"TodaySpelling", &g_stats.today_spelling},
        {L"TodayKeys", &g_stats.today_keys}
    };
    wchar_t number[24];
    size_t i;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        swprintf(number, 24, L"%ld", *fields[i].value);
        WritePrivateProfileStringW(L"Stats", fields[i].key, number, g_stats_path);
    }
    swprintf(number, 24, L"%d", g_stats.today_year);
    WritePrivateProfileStringW(L"Stats", L"TodayYear", number, g_stats_path);
    swprintf(number, 24, L"%d", g_stats.today_month);
    WritePrivateProfileStringW(L"Stats", L"TodayMonth", number, g_stats_path);
    swprintf(number, 24, L"%d", g_stats.today_day);
    WritePrivateProfileStringW(L"Stats", L"TodayDay", number, g_stats_path);
}

/* Called on every counted event; rolls the day over at midnight. */
static void stats_touch_day(void) {
    static DWORD checked_at;
    DWORD now = GetTickCount();
    SYSTEMTIME time;
    if (now - checked_at < 60000u && checked_at) return;
    checked_at = now;
    GetLocalTime(&time);
    if (time.wYear != g_stats.today_year || time.wMonth != g_stats.today_month ||
        time.wDay != g_stats.today_day) {
        ks_stats_roll_day(&g_stats, time.wYear, time.wMonth, time.wDay);
        /* Called from the hook too: the file write happens on the window's
           own message, never inside the hook callback. */
        if (g_window) PostMessageW(g_window, WM_APP_SAVE_STATS, 0, 0);
    }
}

static void stats_count_key(void) {
    stats_touch_day();
    g_stats.today_keys += 1;
    g_stats.total_keys += 1;
}

static void stats_count_correction(const wchar_t *original, int spelling) {
    stats_touch_day();
    ks_stats_observe_correction(&g_stats, original, spelling);
}

/* ---- Text files (UTF-8, or UTF-16 LE with BOM as Notepad saves it) ------- */

/* Returns a heap-allocated, NUL-terminated wide string (caller frees with
   HeapFree) or NULL. Files above 4 MB are refused. */
static wchar_t *read_text_file(const wchar_t *path, FILETIME *written) {
    HANDLE file;
    DWORD size;
    DWORD read = 0;
    char *bytes;
    wchar_t *text = NULL;
    int characters;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (written) GetFileTime(file, NULL, NULL, written);
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size > 4u * 1024u * 1024u) {
        CloseHandle(file);
        return NULL;
    }
    bytes = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)size + 2);
    if (!bytes) {
        CloseHandle(file);
        return NULL;
    }
    if (size && !ReadFile(file, bytes, size, &read, NULL)) read = 0;
    CloseHandle(file);
    bytes[read] = 0;
    bytes[read + 1] = 0;
    if (read >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) {
        characters = (int)((read - 2) / sizeof(wchar_t));
        text = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, ((size_t)characters + 1) * sizeof(wchar_t));
        if (text) {
            memcpy(text, bytes + 2, (size_t)characters * sizeof(wchar_t));
            text[characters] = 0;
        }
    } else {
        characters = read ? MultiByteToWideChar(CP_UTF8, 0, bytes, (int)read, NULL, 0) : 0;
        text = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, ((size_t)(characters > 0 ? characters : 0) + 1) * sizeof(wchar_t));
        if (text) {
            if (characters > 0) MultiByteToWideChar(CP_UTF8, 0, bytes, (int)read, text, characters);
            text[characters > 0 ? characters : 0] = 0;
        }
    }
    HeapFree(GetProcessHeap(), 0, bytes);
    return text;
}

static int write_text_file(const wchar_t *path, const wchar_t *text) {
    HANDLE file;
    int length;
    char *utf8;
    DWORD written = 0;
    int ok = 0;
    length = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (length <= 0) return 0;
    utf8 = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)length + 3);
    if (!utf8) return 0;
    utf8[0] = (char)0xEF; utf8[1] = (char)0xBB; utf8[2] = (char)0xBF;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8 + 3, length, NULL, NULL);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        ok = WriteFile(file, utf8, (DWORD)(length - 1 + 3), &written, NULL) != 0;
        CloseHandle(file);
    }
    HeapFree(GetProcessHeap(), 0, utf8);
    return ok;
}

/* ---- Snippets ------------------------------------------------------------ */

static const wchar_t SNIPPETS_TEMPLATE[] =
    L"# KeySwitchFix snippets — one per line:   shortcut = text\r\n"
    L"# Type the shortcut as a word and press Space, Enter or Tab: it is replaced by the text.\r\n"
    L"# Backspace right after an expansion brings the shortcut back.\r\n"
    L"# Pick shortcuts that are not real words (they would expand every time you type them).\r\n"
    L"# Macros: {jdate} ۱۴۰۵/۰۶/۲۱   {jdate:en} 1405/06/21   {jdate:long} ۲۱ شهریور ۱۴۰۵   {jweekday} شنبه\r\n"
    L"#         {date} 2026-09-12   {date:long} 12 September 2026   {weekday} Saturday\r\n"
    L"#         {time} 14:05   {time:fa} ۱۴:۰۵   \\n = new line (Enter; in chat apps that sends the message)\r\n"
    L"# Lines starting with # are comments. Save the file; changes apply within two seconds.\r\n"
    L"\r\n"
    L"tarikh = {jdate}\r\n"
    L"tdate = {jdate:long}\r\n"
    L"gdate = {date}\r\n"
    L"tnow = {time}\r\n"
    L"brgds = Best regards,\r\n"
    L"tsh = با تشکر و احترام\r\n"
    L"tarikhh = {jdate:long} — {jweekday}\r\n";

static void snippets_reload(int force) {
    wchar_t *text;
    FILETIME written;
    g_snippets_checked_at = GetTickCount();
    if (!force) {
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (!GetFileAttributesExW(g_snippets_path, GetFileExInfoStandard, &attributes)) {
            g_snippets.count = 0;
            return;
        }
        if (CompareFileTime(&attributes.ftLastWriteTime, &g_snippets_loaded_time) == 0) return;
    }
    ZeroMemory(&written, sizeof(written));
    text = read_text_file(g_snippets_path, &written);
    if (!text) {
        /* Unreadable (sharing violation, over 4 MB): remembered so the
           attempt is not repeated every two seconds until the file changes. */
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (GetFileAttributesExW(g_snippets_path, GetFileExInfoStandard, &attributes))
            g_snippets_loaded_time = attributes.ftLastWriteTime;
        g_snippets.count = 0;
        return;
    }
    g_snippets_loaded_time = written;
    if (wcslen(text) > 262144) {
        /* 256 K characters is far beyond 256 snippets; refuse rather than
           parse megabytes on every save. */
        HeapFree(GetProcessHeap(), 0, text);
        g_snippets.count = 0;
        set_activity(L"snippets.txt is too large (over 256 K characters) and was not loaded.");
        return;
    }
    ks_snippets_parse(&g_snippets, text);
    HeapFree(GetProcessHeap(), 0, text);
}

static void open_snippets_file(void) {
    if (GetFileAttributesW(g_snippets_path) == INVALID_FILE_ATTRIBUTES) {
        if (!write_text_file(g_snippets_path, SNIPPETS_TEMPLATE)) {
            set_activity(L"Could not create snippets.txt in the KeySwitchFix data folder.");
            return;
        }
    }
    ShellExecuteW(NULL, L"open", L"notepad.exe", g_snippets_path, NULL, SW_SHOWNORMAL);
    set_activity(L"snippets.txt opened; save it and the new shortcuts are live within seconds.");
}

static void current_date_info(KS_DATE_INFO *info) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    info->year = now.wYear;
    info->month = now.wMonth;
    info->day = now.wDay;
    info->hour = now.wHour;
    info->minute = now.wMinute;
    info->second = now.wSecond;
    info->weekday = now.wDayOfWeek;
}


static void suppress_key_up(DWORD virtual_key) {
    if (virtual_key < 256) {
        DWORD now = GetTickCount();
        g_suppressed_at[virtual_key] = now ? now : 1;
    }
}

static void cancel_smart_correction(void) {
    if (g_window) KillTimer(g_window, ID_TIMER_SMART_CORRECTION);
}

static void clear_word(void) {
    cancel_smart_correction();
    g_word_auto_capitalized = 0;
    g_word_count = 0;
    g_word_mixed = 0;
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
    /* Tracked state plus the physical state: a Ctrl transition the hook
       missed must never turn Ctrl+S into a typed "s". */
    return g_control_down || g_alt_down || g_windows_down ||
           key_down(VK_CONTROL) || key_down(VK_MENU) || key_down(VK_LWIN) || key_down(VK_RWIN);
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

static void resolve_input_target(HWND foreground, KS_INPUT_TARGET *target) {
    wchar_t class_name[64];
    DWORD frame_thread;
    target->top = foreground;
    target->focus = NULL;
    target->thread = 0;
    if (!foreground) return;
    class_name[0] = 0;
    GetClassNameW(foreground, class_name, 64);
    if (_wcsicmp(class_name, L"ApplicationFrameWindow") == 0) {
        /* UWP host: the real input window is the CoreWindow child. */
        HWND core = FindWindowExW(foreground, NULL, L"Windows.UI.Core.CoreWindow", NULL);
        if (core) target->focus = core;
    }
    if (!target->focus) {
        GUITHREADINFO info;
        frame_thread = GetWindowThreadProcessId(foreground, NULL);
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        if (frame_thread && GetGUIThreadInfo(frame_thread, &info) && info.hwndFocus)
            target->focus = info.hwndFocus;
    }
    if (!target->focus) target->focus = foreground;
    target->thread = GetWindowThreadProcessId(target->focus, NULL);
    if (!target->thread) target->thread = GetWindowThreadProcessId(foreground, NULL);
}

/* g_target is refreshed for every key the hook examines and reused by the
   helpers that run inside that same hook call. */
static const KS_INPUT_TARGET *input_target(HWND foreground) {
    if (!foreground || g_target.top != foreground || !g_target.thread)
        resolve_input_target(foreground, &g_target);
    return &g_target;
}

static KS_LANGUAGE target_language(const KS_INPUT_TARGET *target) {
    HKL layout;
    KS_LANGUAGE language;
    if (!target || !target->thread) return KS_LANG_OTHER;
    layout = GetKeyboardLayout(target->thread);
    /* A thread whose layout cannot be read (it may be exiting): fall back to
       the foreground window's thread rather than going blind. */
    if (!layout && target->top)
        layout = GetKeyboardLayout(GetWindowThreadProcessId(target->top, NULL));
    language = language_from_layout(layout);
    if (language == KS_LANG_ENGLISH) g_last_english_layout = layout;
    else if (language == KS_LANG_PERSIAN) g_last_persian_layout = layout;
    return language;
}

static KS_LANGUAGE foreground_language(HWND foreground) {
    if (!foreground) return KS_LANG_OTHER;
    /* Always re-resolve: focus moves between controls without the foreground
       window changing (Tab between fields, a dialog's edit box). */
    resolve_input_target(foreground, &g_target);
    return target_language(&g_target);
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

static HWND focused_window(HWND foreground);

static int process_is_excluded(HWND foreground) {
    DWORD process_id = 0;
    wchar_t name[MAX_PATH];

    wchar_t focus_name[MAX_PATH];
    HWND focus = focused_window(foreground);
    DWORD focus_process_id = 0;

    GetWindowThreadProcessId(foreground, &process_id);
    GetWindowThreadProcessId(focus, &focus_process_id);
    if (!process_id || process_id == GetCurrentProcessId() ||
        focus_process_id == GetCurrentProcessId()) return 1;
    if (!query_process_basename(foreground, name, MAX_PATH)) return 0;
    /*
     * The focused control may belong to another process: the app behind a
     * Store/UWP frame (ApplicationFrameHost.exe hosts notepad.exe), or an
     * embedded web view (msedgewebview2.exe inside Teams or Outlook). The
     * exclusion list matches either; the tray offers the application the
     * user recognises: the host, unless the host is only the UWP frame.
     */
    focus_name[0] = 0;
    if (focus_process_id && focus_process_id != process_id)
        query_process_basename(focus, focus_name, MAX_PATH);
    safe_copy(g_last_typed_process, MAX_PATH,
              focus_name[0] && _wcsicmp(name, L"ApplicationFrameHost.exe") == 0 ? focus_name : name);
    return excluded_list_contains(name) || (focus_name[0] && excluded_list_contains(focus_name));
}

static HWND focused_window(HWND foreground) {
    const KS_INPUT_TARGET *target = input_target(foreground);
    return target->focus ? target->focus : foreground;
}

static int is_protected_field(HWND foreground) {
    HWND focus = focused_window(foreground);
    LONG_PTR style;
    wchar_t class_name[64];
    DWORD_PTR result = 0;
    if (!focus) return 0;
    class_name[0] = 0;
    GetClassNameW(focus, class_name, 64);
    /* ES_PASSWORD is an Edit-control style bit; on other classes the same
       bit means something else. */
    if (_wcsicmp(class_name, L"Edit") != 0 && _wcsnicmp(class_name, L"RichEdit", 8) != 0) return 0;
    style = GetWindowLongPtrW(focus, GWL_STYLE);
    if ((style & ES_PASSWORD) != 0) return 1;
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

static BOOL CALLBACK post_layout_to_thread_window(HWND window, LPARAM layout) {
    PostMessageW(window, WM_INPUTLANGCHANGEREQUEST, 0, layout);
    return TRUE;
}

static int layout_active_on(const KS_INPUT_TARGET *target, KS_LANGUAGE language) {
    return target->thread &&
           language_from_layout(GetKeyboardLayout(target->thread)) == language;
}

/*
 * Ask the application to activate a layout. WM_INPUTLANGCHANGEREQUEST is what
 * Windows itself posts when the user presses the layout hotkey, and
 * DefWindowProc turns it into ActivateKeyboardLayout for the *receiving
 * thread*. Three things can go wrong, and each has a fallback:
 *  - the focused window swallows the message: the top-level window and then
 *    every window of that thread are asked as well;
 *  - the thread is busy: the synchronous send times out and the request is
 *    posted, so it is still queued ahead of the keys the user types next;
 *  - the application ignores it entirely: the hook notices that the next
 *    keys still arrive in the old layout and translates them itself
 *    (see translate_pending_switch in the hook).
 */
static BOOL CALLBACK post_layout_to_child_window(HWND window, LPARAM layout) {
    if (GetWindowThreadProcessId(window, NULL) == g_target.thread)
        PostMessageW(window, WM_INPUTLANGCHANGEREQUEST, 0, layout);
    return TRUE;
}

static void request_layout(HWND foreground, KS_LANGUAGE language) {
    HKL layout = find_layout(language);
    const KS_INPUT_TARGET *target;
    DWORD_PTR result = 0;
    if (!layout || !foreground) return;
    /* Always re-resolved: the undo paths run before the hook has sampled
       the target for this key, and focus may have moved meanwhile. */
    resolve_input_target(foreground, &g_target);
    target = &g_target;
    int superseding;
    if (!target->focus || !target->thread) return;
    /* An earlier request for the other layout may still be queued in the
       application (its thread was busy). The new one must be posted behind
       it even when the layout looks right at this moment. */
    superseding = g_layout_request_at && g_layout_request_window == foreground &&
                  g_layout_request_language != language &&
                  GetTickCount() - g_layout_request_started < 3000u;
    g_layout_request_window = foreground;
    g_layout_request_language = language;
    g_layout_request_from = target_language(target);
    g_layout_request_at = GetTickCount();
    g_layout_request_started = g_layout_request_at;
    g_layout_request_unhonoured = 0;
    g_layout_request_translated = 0;
    InterlockedIncrement(&g_layout_requests);
    if (layout_active_on(target, language) && !superseding) return;
    /*
     * One short synchronous attempt: when it succeeds the switch is verified
     * immediately; when the thread is busy the request is queued instead,
     * still ahead of the keys the user types next. The low-level hook has a
     * tight time budget, so no second blocking wait follows.
     */
    if (!SendMessageTimeoutW(target->focus, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)layout,
                             SMTO_ABORTIFHUNG, 50, &result)) {
        PostMessageW(target->focus, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)layout);
        return; /* verified by the hook on the next key */
    }
    if (layout_active_on(target, language)) return;
    /*
     * The focused window swallowed the message. Ask the other windows of the
     * same thread (the top-level window unless, as in a UWP host, it belongs
     * to another process; then the CoreWindow's siblings and children).
     */
    if (target->top && target->top != target->focus &&
        GetWindowThreadProcessId(target->top, NULL) == target->thread)
        PostMessageW(target->top, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)layout);
    EnumThreadWindows(target->thread, post_layout_to_thread_window, (LPARAM)layout);
    if (target->top) EnumChildWindows(target->top, post_layout_to_child_window, (LPARAM)layout);
}

/* Non-blocking repeat of the last request, used while keys are being
   translated because the application has not switched yet. */
static void repeat_layout_request(HWND foreground) {
    HKL layout = find_layout(g_layout_request_language);
    const KS_INPUT_TARGET *target = input_target(foreground);
    if (!layout || !target->focus || layout_active_on(target, g_layout_request_language)) return;
    PostMessageW(target->focus, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)layout);
    if (target->top && target->top != target->focus)
        PostMessageW(target->top, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)layout);
}

/* The user switched layouts by hand (Alt+Shift, Ctrl+Shift, Win+Space, the
   language bar): whatever we asked for earlier no longer describes what they
   want, so the request window closes. */
static void forget_layout_request(void) {
    /* Also the punctuation context: after a deliberate switch the marks
       follow the new layout. */
    g_last_word_at = 0;
    g_layout_request_at = 0;
    g_layout_request_window = NULL;
    g_layout_request_unhonoured = 0;
    g_layout_request_translated = 0;
}

/*
 * True when we asked this window for `requested` less than three seconds ago,
 * the user has not switched by hand since, and the keys are still being
 * translated by the layout that was active before the request. The keys the
 * user types in that state are meant for the requested layout.
 */
static int switch_still_pending(HWND foreground, KS_LANGUAGE now) {
    return g_layout_request_at &&
           g_layout_request_window == foreground &&
           now == g_layout_request_from &&
           now != g_layout_request_language &&
           (g_layout_request_language == KS_LANG_ENGLISH ||
            g_layout_request_language == KS_LANG_PERSIAN) &&
           GetTickCount() - g_layout_request_at < 3000u &&
           GetTickCount() - g_layout_request_started < 10000u;
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
    for (cursor = replacement; *cursor; ++cursor) {
        /* Snippets may contain line breaks and tabs; those are keys, not
           characters, in every application. */
        if (*cursor == L'\n') add_virtual_input(inputs, &count, VK_RETURN);
        else if (*cursor == L'\t') add_virtual_input(inputs, &count, VK_TAB);
        else if (*cursor != L'\r') add_unicode_input(inputs, &count, *cursor);
    }
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
    /*
     * Then ask for the layout switch. Everything injected is a Unicode
     * character or a layout-independent virtual key, so the order does not
     * matter for the replacement itself; asking only after a successful
     * injection means a blocked replacement never leaves a pending request
     * behind. If the application is slow to honour it, the hook types the
     * next keys for the requested layout itself (deliver_key).
     */
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

static void remember_corrected_word(HWND foreground, KS_LANGUAGE language);

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
    remember_corrected_word(foreground, decision->target_language);
    InterlockedIncrement(&g_corrections);
    stats_count_correction(decision->original, 0);
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
        remember_corrected_word(foreground, result.language);
        InterlockedIncrement(&g_corrections);
        stats_count_correction(NULL, 0);
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
        L"git-bash.exe", L"bash.exe", L"wsl.exe", L"ubuntu.exe",
        /* Remote sessions: the local layout state says nothing about the
           remote machine, which may run its own KeySwitchFix. */
        L"mstsc.exe", L"msrdc.exe", L"vmconnect.exe", L"VirtualBoxVM.exe", L"vmware.exe",
        L"vmware-vmx.exe", L"wfica32.exe", L"AnyDesk.exe", L"TeamViewer.exe", L"RustDesk.exe",
        L"parsecd.exe", L"kitty.exe", L"MobaXterm.exe", L"SecureCRT.exe"
    };
    wchar_t name[MAX_PATH];
    size_t i;
    /* Queried fresh: g_last_typed_process is only refreshed when a query
       succeeds at word start and may describe an earlier application. */
    HWND focus = focused_window(foreground);
    int pass;
    for (pass = 0; pass < 2; ++pass) {
        /* The host process, then the focused control's process (a UWP app
           behind its frame, a web view inside an IDE). */
        if (!query_process_basename(pass ? focus : foreground, name, MAX_PATH)) continue;
        for (i = 0; i < sizeof(developer_tools) / sizeof(developer_tools[0]); ++i) {
            if (_wcsicmp(name, developer_tools[i]) == 0) return 1;
        }
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
    /* A capital the hook itself put at a sentence start is not the user's
       signal for a name: check the lower-case word, restore the capital. The
       ignore list is case-sensitive, so the capitalised form (recorded when
       such a fix is undone) is honoured here first. */
    if (g_word_auto_capitalized && typed[0] >= L'A' && typed[0] <= L'Z') {
        if (ks_ignore_list_contains(&g_spelling_ignore, typed)) return SPELL_DECLINED;
        typed[0] = (wchar_t)(typed[0] - L'A' + L'a');
    }
    if (!ks_spell_correct(typed, g_settings.spelling, lexicon, &g_spelling_ignore, &result) ||
        !result.should_correct)
        return SPELL_DECLINED;
    if (g_word_auto_capitalized) {
        if (result.original[0] >= L'a' && result.original[0] <= L'z')
            result.original[0] = (wchar_t)(result.original[0] - L'a' + L'A');
        if (result.replacement[0] >= L'a' && result.replacement[0] <= L'z')
            result.replacement[0] = (wchar_t)(result.replacement[0] - L'a' + L'A');
    }
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
        return SPELL_SUPPRESSED;
    store_undo(foreground, &decision, delimiter, delimiter_zwnj);
    g_undo.spelling = 1;
    mark_sentence_word(foreground);
    remember_intent(foreground, g_word_language, 2);
    InterlockedIncrement(&g_spelling_fixes);
    stats_count_correction(decision.original, 1);
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

/*
 * Mixed-word repair.
 *
 * A layout switch requested after a correction can be applied by the target
 * application a few keystrokes late (browsers and Electron apps process it
 * asynchronously; a busy app misses the 100 ms synchronous window). The
 * user keeps typing "standard", the first keys render in the old layout and
 * the rest in the new one: "staدیشقی". The physical keys are still one word,
 * so the word is kept, each key remembers the layout that rendered it, and as
 * soon as the whole sequence spells a word in exactly one language the
 * on-screen mixture is replaced by that word.
 */
static int layout_change_was_ours(HWND foreground, KS_LANGUAGE now) {
    if (g_layout_request_window != foreground) return 0;
    /* The switch we asked for arriving late... */
    if (g_layout_request_language == now && GetTickCount() - g_layout_request_at < 3000u) return 1;
    /* ...or keys still rendered by the old layout after the translation
       window closed (the request was never honoured and the user has not
       switched by hand since, or the window would have been forgotten). */
    return now == g_layout_request_from && now != g_layout_request_language;
}

static KS_LANGUAGE current_word_layout(void) {
    return g_word_mixed && g_word_count > 0 ? g_word_visible[g_word_count - 1] : g_word_language;
}

static int word_is_uniform(void) {
    int i;
    for (i = 1; i < g_word_count; ++i)
        if (g_word_visible[i] != g_word_visible[0]) return 0;
    return 1;
}

static void mixed_visible_text(wchar_t *output) {
    int i;
    for (i = 0; i < g_word_count; ++i)
        output[i] = g_word_visible[i] == KS_LANG_PERSIAN ? g_word[i].persian : g_word[i].english;
    output[g_word_count] = 0;
}

static void lowercase_ascii(wchar_t *text) {
    for (; *text; ++text)
        if (*text >= L'A' && *text <= L'Z') *text = *text - L'A' + L'a';
}

static KS_LIVE_RESULT evaluate_mixed_word(HWND foreground, KS_EVALUATION_PHASE phase,
                                          KS_DECISION *decision) {
    int english_known = 0;
    int persian_known = 0;
    KS_LANGUAGE winner;
    wchar_t candidate[KS_MAX_WORD + 1];
    int strength = 0;

    memset(decision, 0, sizeof(*decision));
    if (!g_word_mixed || g_word_count < 1 || g_word_count > KS_MAX_WORD) return KS_LIVE_NONE;
    /* Backspace may have removed every key of one layout (or the switch
       landed right before a boundary with a single key typed); the word is
       then an ordinary word again and the normal evaluators own it. */
    if (word_is_uniform()) {
        g_word_mixed = 0;
        g_word_language = g_word_visible[0];
        return KS_LIVE_NONE;
    }
    if (g_word_count < 2) return KS_LIVE_NONE;
    if (!ks_classify_word(g_word, g_word_count, &g_lexicons,
                          &english_known, &persian_known, NULL, NULL))
        return KS_LIVE_NONE;
    if (english_known && !persian_known) winner = KS_LANG_ENGLISH;
    else if (persian_known && !english_known) winner = KS_LANG_PERSIAN;
    else if (english_known && persian_known) {
        /* Both readings are words: only at a boundary, and only when the
           document language says which one. */
        KS_LANGUAGE intent = current_intent(foreground, &strength);
        if (phase != KS_PHASE_BOUNDARY || strength < 2) return KS_LIVE_NONE;
        winner = intent;
        if (winner != KS_LANG_ENGLISH && winner != KS_LANG_PERSIAN) return KS_LIVE_NONE;
    } else {
        return KS_LIVE_NONE;
    }

    tokens_to_language(g_word, g_word_count, winner, candidate);
    decision->should_correct = 1;
    decision->key_count = g_word_count;
    decision->confidence = 90;
    decision->source_language = g_word_visible[g_word_count - 1];
    decision->target_language = winner;
    mixed_visible_text(decision->original);
    safe_copy(decision->replacement, KS_MAX_WORD + 1, candidate);

    if (phase == KS_PHASE_BOUNDARY) return KS_LIVE_CORRECT_NOW;
    /* While typing continues, a word that is also the beginning of a longer
       word waits for the adaptive pause, exactly like layout repair. */
    if (winner == KS_LANG_ENGLISH) lowercase_ascii(candidate);
    if (ks_bloom_contains(winner == KS_LANG_ENGLISH ? g_lexicons.english_prefixes
                                                     : g_lexicons.persian_prefixes, candidate))
        return KS_LIVE_WAIT_FOR_IDLE;
    return KS_LIVE_CORRECT_NOW;
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
    if (switch_still_pending(foreground, language)) language = g_layout_request_language;
    if (!foreground || foreground != g_word_window || language != current_word_layout()) {
        clear_word();
        return;
    }

    intent = current_intent(foreground, &intent_strength);
    InterlockedIncrement(&g_words_checked);
    if (g_word_mixed) {
        KS_LIVE_RESULT mixed = evaluate_mixed_word(foreground, KS_PHASE_IDLE, &decision);
        if (mixed == KS_LIVE_CORRECT_NOW &&
            apply_decision(foreground, &decision, g_word_count, 0, 0)) {
            store_pending_word(foreground, g_word, g_word_count, decision.target_language);
            clear_word();
            return;
        }
        /* evaluate_mixed_word may have found the word uniform again and
           handed it back to the ordinary path; fall through only then. */
        if (g_word_mixed || !g_has_context) return;
    }
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
            /* "Teh" undone at a sentence start must also protect "teh". */
            if (g_undo.original[0] >= L'A' && g_undo.original[0] <= L'Z' &&
                wcslen(g_undo.original) <= KS_MAX_WORD) {
                wchar_t lower[KS_MAX_WORD + 1];
                safe_copy(lower, KS_MAX_WORD + 1, g_undo.original);
                lower[0] = (wchar_t)(lower[0] - L'A' + L'a');
                ks_ignore_list_add(&g_spelling_ignore, lower);
                ks_vocab_trust(&g_session_vocabulary, lower);
            }
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

/* ---- Typing helpers inside the hook -------------------------------------- */

/* Developer tools, excluded processes and password fields get the raw keys:
   digits, punctuation and letters are never reshaped there. Cached per
   focused window because the process lookup is not free. */
static int helpers_suppressed(HWND foreground) {
    static HWND cached_focus;
    static DWORD cached_at;
    static int cached_result;
    HWND focus = focused_window(foreground);
    DWORD now = GetTickCount();
    if (focus != cached_focus || now - cached_at > 3000u || !cached_at) {
        cached_focus = focus;
        cached_at = now ? now : 1;
        cached_result = process_is_excluded(foreground) || spelling_skipped_process(foreground) ||
                        is_protected_field(foreground);
    }
    return cached_result;
}

static void remember_last_word(HWND foreground) {
    if (g_has_context && g_word_count > 0 && !g_overflow_count && g_word_window == foreground &&
        (g_word_language == KS_LANG_PERSIAN || g_word_language == KS_LANG_ENGLISH)) {
        g_last_word_language = g_word_language;
        g_last_word_window = foreground;
        g_last_word_at = GetTickCount();
    }
}

static void remember_corrected_word(HWND foreground, KS_LANGUAGE language) {
    if (language != KS_LANG_PERSIAN && language != KS_LANG_ENGLISH) return;
    g_last_word_language = language;
    g_last_word_window = foreground;
    g_last_word_at = GetTickCount();
}

/* The language that decides how "?" "," ";" are shaped: the word just
   typed or finished in this window within the last five seconds, else the
   layout the key arrived in. A manual layout switch clears it (see
   forget_layout_request), so a user who switches to Persian to type "؟"
   after an English word is never overruled. */
static KS_LANGUAGE punctuation_context(HWND foreground, KS_LANGUAGE layout) {
    if (g_last_word_window == foreground && g_last_word_at &&
        GetTickCount() - g_last_word_at < 5000u &&
        (g_last_word_language == KS_LANG_PERSIAN || g_last_word_language == KS_LANG_ENGLISH))
        return g_last_word_language;
    return layout;
}

/* Applies the typing-helper settings to one character about to reach the
   application. `capitalize` is the sentence-start request for letter keys. */
static wchar_t shape_character(wchar_t character, KS_LANGUAGE layout, HWND foreground,
                               int capitalize) {
    wchar_t shaped = character;
    if (g_settings.persian_letters) shaped = ks_persian_form(shaped);
    if (g_settings.digits != KS_DIGITS_OFF) shaped = ks_shape_digit(shaped, g_settings.digits, layout);
    if (g_settings.punctuation && (shaped == L'?' || shaped == L',' || shaped == L';' ||
                                   shaped == 0x061F || shaped == 0x060C || shaped == 0x061B)) {
        KS_LANGUAGE context = punctuation_context(foreground, layout);
        if (context == KS_LANG_PERSIAN) shaped = ks_persian_punctuation(shaped);
        else if (context == KS_LANG_ENGLISH) shaped = ks_latin_punctuation(shaped);
    }
    if (capitalize && shaped >= L'a' && shaped <= L'z') shaped = (wchar_t)(shaped - L'a' + L'A');
    return shaped;
}

/* A period ends a sentence when the word before it is a real word: two or
   more letters, English, not an abbreviation such as "dr" or "e.g". */
static int arm_capitalization_after_word(void) {
    wchar_t word[KS_MAX_WORD + 1];
    if (!g_settings.auto_capitalize || !g_has_context || g_word_mixed || g_overflow_count ||
        g_word_count < 2 || g_word_language != KS_LANG_ENGLISH || g_last_key_was_digit)
        return 0;
    tokens_to_language(g_word, g_word_count, KS_LANG_ENGLISH, word);
    lowercase_ascii(word);
    return !ks_is_abbreviation(word);
}

/*
 * At a word boundary: expands a snippet shortcut, or capitalises the lone
 * English pronoun "i". Returns 1 when the text was replaced (the boundary
 * key was replayed inside the replacement).
 */
static int expand_snippet_or_pronoun(HWND foreground, UINT boundary_key, int zwnj,
                                     int pronoun_context) {
    wchar_t typed[KS_MAX_WORD + 1];
    const KS_SNIPPET *snippet;
    if (g_word_count < 1 || g_word_count > KS_MAX_WORD) return 0;
    if (g_word_language != KS_LANG_ENGLISH && g_word_language != KS_LANG_PERSIAN) return 0;
    tokens_to_language(g_word, g_word_count, g_word_language, typed);
    if (g_settings.snippets) {
        snippet = ks_snippet_find(&g_snippets, typed);
        if (snippet) {
            wchar_t expansion[KS_MAX_PHRASE_CHARS + 1];
            KS_DATE_INFO now;
            current_date_info(&now);
            ks_expand_macros(snippet->text, &now, expansion, KS_MAX_PHRASE_CHARS + 1);
            if (expansion[0] &&
                send_replacement(foreground, g_word_count, expansion, boundary_key, zwnj,
                                 g_word_language)) {
                store_phrase_undo(foreground, g_word_language, boundary_key, zwnj, typed, expansion);
                InterlockedIncrement(&g_snippets_used);
                set_activity_pair(L"Snippet", typed, expansion);
                return 1;
            }
            return 0;
        }
    }
    /* "i" alone becomes "I" only in prose: after Space, following an
       English word typed within five seconds, before a Space — never in
       "for i in", "i = 0", "j.i." or at the start of a field. */
    if (g_settings.auto_capitalize && pronoun_context && g_word_count == 1 &&
        g_word_language == KS_LANG_ENGLISH && typed[0] == L'i' && boundary_key == VK_SPACE) {
        if (send_replacement(foreground, 1, L"I", boundary_key, zwnj, KS_LANG_ENGLISH)) {
            store_phrase_undo(foreground, KS_LANG_ENGLISH, boundary_key, zwnj, L"i", L"I");
            return 1;
        }
    }
    return 0;
}


/* is_protected_field costs a message round trip for Edit controls; while
   keys are being translated it is asked once per focused window. */
static int translation_target_protected(HWND foreground) {
    static HWND cached_focus;
    static DWORD cached_at;
    static int cached_result;
    HWND focus = focused_window(foreground);
    DWORD now = GetTickCount();
    if (focus != cached_focus || now - cached_at > 2000u) {
        cached_focus = focus;
        cached_at = now;
        cached_result = is_protected_field(foreground);
    }
    return cached_result;
}

/*
 * Lets a key through, or — while a layout switch we requested has not been
 * honoured yet — swallows the physical key and types the character the
 * requested layout would have produced (letters, digits, punctuation, the
 * ZWNJ of Shift+Space). The application therefore never shows the wrong
 * alphabet, whether its switch is merely late or never comes. Keys that
 * produce the same character in both layouts, control keys, and password
 * fields are left alone: nothing may be rewritten there.
 */
static LRESULT deliver_key(int code, WPARAM wparam, LPARAM lparam,
                           const KBDLLHOOKSTRUCT *data, int translate,
                           int shift, int caps, HWND foreground,
                           KS_LANGUAGE language, int capitalize) {
    INPUT inputs[2];
    UINT count = 0;
    wchar_t wanted = 0;
    wchar_t current = 0;
    wchar_t shaped;
    HKL current_layout;
    int helpers = (g_settings.persian_letters || g_settings.digits != KS_DIGITS_OFF ||
                   g_settings.punctuation || capitalize) && !helpers_suppressed(foreground);
    if (!translate && !helpers) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    /* Numeric keypad keys depend on Num Lock, not on the layout. */
    if ((data->vkCode >= VK_NUMPAD0 && data->vkCode <= VK_DIVIDE) || (data->flags & LLKHF_EXTENDED))
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    current_layout = g_target.thread ? GetKeyboardLayout(g_target.thread) : NULL;
    if (!current_layout) current_layout = find_layout(g_layout_request_from);
    if (!translated_layout_character(current_layout, data->scanCode, shift, caps, &current))
        current = 0;
    if (translate) {
        if (!translated_layout_character(find_layout(g_layout_request_language),
                                         data->scanCode, shift, caps, &wanted))
            return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    } else {
        wanted = current;
    }
    if (!wanted) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    shaped = helpers ? shape_character(wanted, language, foreground, capitalize) : wanted;
    if (shaped == current) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    if (translate && translation_target_protected(foreground))
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    add_unicode_input(inputs, &count, shaped);
    if (SendInput(count, inputs, sizeof(INPUT)) != count)
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    suppress_key_up(data->vkCode);
    if (translate && wanted != current) {
        ++g_layout_request_translated;
        /* A switch that merely arrives late is not "ignored": keys typed in
           the first quarter second after the request are given the benefit
           of the doubt. */
        if (!g_layout_request_unhonoured &&
            GetTickCount() - g_layout_request_started >= 250u) {
            wchar_t name[MAX_PATH];
            wchar_t message[MAX_PATH + 96];
            g_layout_request_unhonoured = 1;
            InterlockedIncrement(&g_layout_requests_ignored);
            if (!query_process_basename(focused_window(foreground), name, MAX_PATH))
                safe_copy(name, MAX_PATH, L"The application");
            swprintf(message, sizeof(message) / sizeof(message[0]),
                     L"%ls did not switch to the %ls layout; KeySwitchFix is typing the keys for it.",
                     name, language_name(g_layout_request_language));
            set_activity(message);
        }
        /* The request stays open while the user keeps typing for it; it
           closes three seconds after the last key, or on a manual switch. */
        g_layout_request_at = GetTickCount();
        repeat_layout_request(foreground);
    }
    return 1;
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
    int translate;
    int capitalize = 0;
    int typing_helpers;
    int pronoun_context = 0;

    if (code < 0) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    g_last_hook_tick = GetTickCount();
    data = (KBDLLHOOKSTRUCT *)lparam;
    if (data->flags & LLKHF_INJECTED) {
        if (data->dwExtraInfo != INPUT_MARKER) {
            clear_word();
            clear_history();
            forget_layout_request();
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
            forget_layout_request();
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if (key_up) {
        if (data->vkCode < 256 && g_suppressed_at[data->vkCode]) {
            DWORD elapsed = GetTickCount() - g_suppressed_at[data->vkCode];
            g_suppressed_at[data->vkCode] = 0;
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
    if (data->vkCode == VK_SPACE && g_windows_down) {
        clear_intent();
        forget_layout_request();
    }

    if (data->vkCode == VK_BACK && !shortcut_modifier_down() &&
        g_undo.valid && GetForegroundWindow() == g_undo.window &&
        GetTickCount64() - g_undo.created_at <= 15000u) {
        clear_word();
        if (try_undo(1)) {
            suppress_key_up(data->vkCode);
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
    /*
     * We asked this window to switch layouts a moment ago and it has not
     * done so yet (the message is queued behind a busy thread, or the
     * application ignores it). The user is already typing for the layout we
     * asked for, so the word model uses that layout and, for letter keys,
     * the hook types the character itself instead of letting the old layout
     * render it. This is what turns "staدیشقی" back into "standard".
     */
    translate = switch_still_pending(foreground, language);
    if (translate) language = g_layout_request_language;
    /* Digit, punctuation and letter shaping never touch code editors,
       terminals, excluded apps or password fields. */
    typing_helpers = !helpers_suppressed(foreground);
    if (g_capitalize_window != foreground) {
        g_capitalize_armed = 0;
        g_capitalize_next = 0;
    }
    if ((g_history_window && g_history_window != foreground) ||
        (g_pending_word_valid && g_pending_word_window != foreground)) {
        clear_intent();
        clear_history();
    }
    if (g_has_context && foreground != g_word_window) {
        clear_intent();
        clear_history();
        clear_word();
    } else if (g_has_context && language != current_word_layout()) {
        /*
         * The layout changed since the previous key of this word. If that is
         * the switch we asked for arriving late, keep the word and remember
         * which keys rendered in which layout so the mixture can be repaired.
         * A manual switch (Alt+Shift), or a change to a layout we did not ask
         * for, starts a new word as before. The check is against the layout
         * of the previous key, so it fires once per actual change and not on
         * every later key of a long or slowly typed word.
         */
        if (g_word_count > 0 && layout_change_was_ours(foreground, language)) g_word_mixed = 1;
        else clear_word();
    }

    if (data->vkCode == VK_BACK) {
        int had_word = g_word_count > 0 || g_overflow_count > 0;
        g_capitalize_armed = 0;
        g_capitalize_next = 0;
        g_last_key_was_digit = 0;
        cancel_smart_correction();
        g_last_word_key_at = 0;
        if (g_overflow_count > 0) --g_overflow_count;
        else if (g_word_count > 0) --g_word_count;
        if (!g_word_count && !g_overflow_count) {
            /* The user deleted the whole word, a rejected correction
               included: stop typing for the layout we asked for. Deleting
               a separator that follows a finished word is not that. */
            clear_word();
            clear_history();
            if (had_word) forget_layout_request();
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    if (is_navigation(data->vkCode)) {
        clear_word();
        reset_sentence(foreground);
        forget_layout_request();
        g_capitalize_armed = 0;
        g_capitalize_next = 0;
        g_last_key_was_digit = 0;
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }

    /* The physical state, not the tracked one: a Shift transition the hook
       missed (reinstall, an elevated window) must not mis-case a key. */
    shift = key_down(VK_SHIFT);
    g_shift_down = shift;
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
        if (g_pending_word_valid && !g_has_context &&
            g_pending_word_window == foreground &&
            g_pending_word.count > 0 && g_pending_word.count < KS_MAX_WORD &&
            (g_pending_word.visible_language == language ||
             g_layout_request_window == foreground)) {
            /*
             * A live correction fired in the middle of this word ("sta" was
             * repaired after three keys) and the user keeps typing it. The
             * corrected keys are the beginning of the word, not a finished
             * word: resume them so "standard" is judged whole, never "dard"
             * on its own.
             */
            int i;
            memcpy(g_word, g_pending_word.tokens,
                   (size_t)g_pending_word.count * sizeof(g_word[0]));
            for (i = 0; i < g_pending_word.count; ++i)
                g_word_visible[i] = g_pending_word.visible_language;
            g_word_count = g_pending_word.count;
            /* Keys still rendered by the old layout after a pause longer
               than the translation window: a mixed word, repaired whole at
               the next evaluation. */
            g_word_mixed = g_pending_word.visible_language != language;
            g_overflow_count = 0;
            g_has_context = 1;
            g_word_window = foreground;
            g_word_language = g_pending_word.visible_language;
            g_skip_word = process_is_excluded(foreground);
            g_pending_word_valid = 0;
            g_pending_word_window = NULL;
        } else if (g_pending_word_valid) clear_history();
        cancel_smart_correction();
        InterlockedIncrement(&g_keys_seen);
        stats_count_key();
        if (!g_has_context) {
            g_has_context = 1;
            g_word_window = foreground;
            g_word_language = language;
            g_skip_word = process_is_excluded(foreground);
        }
        /* First letter of a sentence: capitalise it (English only, no Shift
           or Caps Lock, never in code editors). Any letter disarms. */
        capitalize = g_settings.auto_capitalize && typing_helpers && g_capitalize_next &&
                     language == KS_LANG_ENGLISH && g_word_count == 0 && !shift && !caps;
        g_capitalize_next = 0;
        g_capitalize_armed = 0;
        g_last_key_was_digit = 0;
        /* The word model must see the capital too; the spelling model is told
           so it can still repair "Teh" at a sentence start. */
        if (capitalize && token.english >= L'a' && token.english <= L'z')
            token.english = (wchar_t)(token.english - L'a' + L'A');
        if (g_word_count == 0) {
            g_word_after_boundary = g_previous_key_boundary;
            g_word_auto_capitalized = capitalize;
        }
        g_previous_key_boundary = 0;
        if (g_skip_word) return deliver_key(code, wparam, lparam, data, 0, shift, caps, foreground, language, 0);
        if (g_word_count < KS_MAX_WORD && !g_overflow_count) {
            /* Sampled in the hook, before the target translates the key: if
               the switch lands in between, this one key is attributed to the
               old layout. Delete counts are unaffected (one char per key);
               only the Undo text of that key can differ from the screen. */
            g_word_visible[g_word_count] = language;
            g_word[g_word_count++] = token;
        } else ++g_overflow_count;
        observe_typing_interval();

        if (!g_overflow_count && g_word_mixed) {
            live_result = evaluate_mixed_word(foreground, KS_PHASE_LIVE, &decision);
            if (live_result == KS_LIVE_CORRECT_NOW) {
                if (apply_decision(foreground, &decision, g_word_count - 1, 0, 0)) {
                    store_pending_word(foreground, g_word, g_word_count,
                                       decision.target_language);
                    suppress_key_up(data->vkCode);
                    clear_word();
                    return 1;
                }
            } else if (live_result == KS_LIVE_WAIT_FOR_IDLE) {
                schedule_smart_correction();
            }
            /* A word that became uniform again falls into the ordinary live
               evaluation below for this very key. */
            if (g_word_mixed) return deliver_key(code, wparam, lparam, data, translate, shift, caps, foreground, language, capitalize);
        }

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
                    suppress_key_up(data->vkCode);
                    clear_word();
                    return 1;
                }
            } else if (live_result == KS_LIVE_WAIT_FOR_IDLE) {
                schedule_smart_correction();
            }
        }

        return deliver_key(code, wparam, lparam, data, translate, shift, caps, foreground, language, capitalize);
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
        /* Sentence capitalisation: a period after a real word (not an
           abbreviation, not a number) arms it; the Space or Enter that
           follows makes the next letter capital. */
        if (boundary_key == VK_SPACE || boundary_key == VK_RETURN) {
            g_capitalize_next = g_capitalize_armed || (g_capitalize_next && !had_word);
            g_capitalize_armed = 0;
        } else if (boundary_key == VK_OEM_PERIOD) {
            g_capitalize_armed = arm_capitalization_after_word();
            g_capitalize_next = 0;
        } else {
            g_capitalize_armed = 0;
            g_capitalize_next = 0;
        }
        g_capitalize_window = foreground;
        g_last_key_was_digit = 0;
        /* Was the word before this one an English word typed in prose? Read
           before this word replaces it: the lone-"i" rule needs it. */
        pronoun_context = g_word_after_boundary && g_last_word_window == foreground &&
                          g_last_word_language == KS_LANG_ENGLISH && g_last_word_at &&
                          GetTickCount() - g_last_word_at < 5000u;
        remember_last_word(foreground);
        g_previous_key_boundary = boundary_key == VK_SPACE || boundary_key == VK_RETURN ||
                                  boundary_key == VK_TAB;
        /* Snippets and the lone "i" come before every other evaluation:
           the user asked for exactly this text. */
        if (!g_skip_word && !g_overflow_count && !g_word_mixed && g_word_count > 0 &&
            typing_helpers && expand_snippet_or_pronoun(foreground, boundary_key, zwnj, pronoun_context)) {
            suppress_key_up(data->vkCode);
            if (terminates_sentence) start_new_sentence(foreground);
            clear_history();
            clear_word();
            return 1;
        }
        if (!had_word && g_pending_word_valid) {
            if (boundary_key == VK_SPACE &&
                g_pending_word_window == foreground) {
                commit_pending_word(foreground, boundary_key, zwnj);
                mark_sentence_word(foreground);
                clear_word();
                return deliver_key(code, wparam, lparam, data, translate, shift, caps, foreground, language, capitalize);
            }
            clear_history();
        }
        intent = current_intent(foreground, &intent_strength);
        if (!g_skip_word && !g_overflow_count && g_word_mixed) {
            /* A word split across two layouts is repaired as a whole; the
               sentence model cannot reason about the mixture. */
            KS_LIVE_RESULT mixed = evaluate_mixed_word(foreground, KS_PHASE_BOUNDARY, &decision);
            if (mixed == KS_LIVE_CORRECT_NOW &&
                apply_decision(foreground, &decision, g_word_count, boundary_key, zwnj)) {
                history_push(foreground, g_word, g_word_count,
                             decision.target_language, boundary_key, zwnj);
                suppress_key_up(data->vkCode);
                if (terminates_sentence) start_new_sentence(foreground);
                clear_word();
                return 1;
            }
            if (g_word_mixed) {
                /* Still mixed and not repairable: leave the text, drop the
                   sentence model, start fresh. */
                clear_history();
                clear_word();
                if (terminates_sentence) start_new_sentence(foreground);
                return deliver_key(code, wparam, lparam, data, translate, shift, caps, foreground, language, capitalize);
            }
            /* Uniform again (Backspace removed the other layout's keys):
               the ordinary boundary evaluation below owns the word. */
        }
        /*
         * Re-evaluate the longest available phrase before committing to an
         * isolated-word decision. A later word can therefore supply the
         * missing evidence for earlier ambiguous text.
         */
        if (!g_skip_word && !g_overflow_count && g_word_count > 0 &&
            try_sequence_correction(
                foreground, g_word, g_word_count, g_word_language,
                boundary_key, zwnj, intent, intent_strength)) {
            suppress_key_up(data->vkCode);
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
                suppress_key_up(data->vkCode);
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
                    suppress_key_up(data->vkCode);
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
        /* "!" and "?" end a sentence when a real English word precedes them
           ("Really?"), or when the period before them already armed it
           ("What?!"). A lone "?" in a formula does not. */
        g_capitalize_armed = typing_helpers && language == KS_LANG_ENGLISH &&
                             ((g_has_context && g_word_count >= 2 && !g_overflow_count &&
                               !g_word_mixed && g_word_language == KS_LANG_ENGLISH) ||
                              (g_capitalize_armed && !g_has_context));
        g_capitalize_next = 0;
        g_capitalize_window = foreground;
        g_last_key_was_digit = 0;
        g_previous_key_boundary = 0;
        remember_last_word(foreground);
        start_new_sentence(foreground);
    } else {
        /*
         * Punctuation, editing commands, and unsupported characters make the
         * cached caret model unreliable. Drop phrase history rather than
         * risking deletion of unrelated text.
         */
        clear_history();
        g_capitalize_armed = 0;
        g_capitalize_next = 0;
        g_last_key_was_digit = data->vkCode >= '0' && data->vkCode <= '9';
        g_previous_key_boundary = 0;
        /* "سلام؟": the punctuation follows the word being typed; remember
           its language before the word is dropped, so the mark is shaped
           for it. */
        remember_last_word(foreground);
    }
    clear_word();
    return deliver_key(code, wparam, lparam, data, translate, shift, caps, foreground, language, capitalize);
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
            forget_layout_request();
            g_capitalize_armed = 0;
            g_capitalize_next = 0;
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

/* ---- Ctrl+Win+X: clean up the selected text ------------------------------ */

static int open_clipboard_retry(void) {
    int attempt;
    for (attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(g_window)) return 1;
        Sleep(15);
    }
    return 0;
}

static void clipboard_snapshot_free(CLIPBOARD_SNAPSHOT *snapshot) {
    int i;
    for (i = 0; i < snapshot->count; ++i)
        if (snapshot->data[i]) HeapFree(GetProcessHeap(), 0, snapshot->data[i]);
    ZeroMemory(snapshot, sizeof(*snapshot));
}

/* Formats whose clipboard handle is not an HGLOBAL, or that the system
   synthesises from others, are not copied. */
static int clipboard_format_copyable(UINT format) {
    switch (format) {
        case CF_BITMAP: case CF_ENHMETAFILE: case CF_METAFILEPICT: case CF_PALETTE:
        case CF_OWNERDISPLAY: case CF_DSPBITMAP: case CF_DSPENHMETAFILE:
        case CF_DSPMETAFILEPICT: case CF_TEXT: case CF_OEMTEXT: case CF_LOCALE:
            return 0;
        default:
            return format < CF_PRIVATEFIRST || format > CF_GDIOBJLAST;
    }
}

static void clipboard_snapshot_take(CLIPBOARD_SNAPSHOT *snapshot) {
    UINT format = 0;
    SIZE_T total = 0;
    clipboard_snapshot_free(snapshot);
    if (!open_clipboard_retry()) return;
    snapshot->valid = 1;
    while ((format = EnumClipboardFormats(format)) != 0 && snapshot->count < CLIPBOARD_SNAPSHOT_MAX) {
        HANDLE handle;
        SIZE_T size;
        const void *source;
        void *copy;
        if (!clipboard_format_copyable(format)) continue;
        handle = GetClipboardData(format);
        if (!handle) continue;
        size = GlobalSize(handle);
        if (!size || total + size > 32u * 1024u * 1024u) continue;
        source = GlobalLock(handle);
        if (!source) continue;
        copy = HeapAlloc(GetProcessHeap(), 0, size);
        if (copy) {
            memcpy(copy, source, size);
            snapshot->format[snapshot->count] = format;
            snapshot->data[snapshot->count] = copy;
            snapshot->size[snapshot->count] = size;
            ++snapshot->count;
            total += size;
        }
        GlobalUnlock(handle);
    }
    CloseClipboard();
}

static void clipboard_snapshot_restore(const CLIPBOARD_SNAPSHOT *snapshot, DWORD expected_sequence) {
    int i;
    if (!snapshot->valid || !open_clipboard_retry()) return;
    /* Checked with the clipboard open: a copy that landed meanwhile is
       newer than the snapshot and must not be clobbered. */
    if (GetClipboardSequenceNumber() != expected_sequence) {
        CloseClipboard();
        return;
    }
    EmptyClipboard();
    for (i = 0; i < snapshot->count; ++i) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, snapshot->size[i]);
        void *buffer = memory ? GlobalLock(memory) : NULL;
        if (!buffer) {
            if (memory) GlobalFree(memory);
            continue;
        }
        memcpy(buffer, snapshot->data[i], snapshot->size[i]);
        GlobalUnlock(memory);
        if (!SetClipboardData(snapshot->format[i], memory)) GlobalFree(memory);
    }
    CloseClipboard();
}

static wchar_t *clipboard_text_copy(void) {
    wchar_t *copy = NULL;
    HANDLE handle;
    const wchar_t *text;
    size_t length;
    size_t limit;
    if (!open_clipboard_retry()) return NULL;
    handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        limit = GlobalSize(handle) / sizeof(wchar_t);
        text = (const wchar_t *)GlobalLock(handle);
        if (text && limit) {
            for (length = 0; length < limit && text[length]; ++length) {}
            if (length < 1024u * 1024u) {
                copy = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(wchar_t));
                if (copy) {
                    memcpy(copy, text, length * sizeof(wchar_t));
                    copy[length] = 0;
                }
            }
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return copy;
}

static int clipboard_set_text(const wchar_t *text) {
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void *buffer;
    int ok = 0;
    if (!memory) return 0;
    buffer = GlobalLock(memory);
    if (!buffer) {
        GlobalFree(memory);
        return 0;
    }
    memcpy(buffer, text, bytes);
    GlobalUnlock(memory);
    if (open_clipboard_retry()) {
        EmptyClipboard();
        ok = SetClipboardData(CF_UNICODETEXT, memory) != NULL;
        CloseClipboard();
    }
    if (!ok) GlobalFree(memory);
    return ok;
}

static void send_shortcut(WORD key) {
    INPUT inputs[4];
    UINT count = 0;
    ZeroMemory(inputs, sizeof(inputs));
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = VK_CONTROL;
    inputs[count].ki.dwExtraInfo = INPUT_MARKER;
    ++count;
    add_virtual_input(inputs, &count, key);
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = VK_CONTROL;
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[count].ki.dwExtraInfo = INPUT_MARKER;
    ++count;
    SendInput(count, inputs, sizeof(INPUT));
}

static void finish_selection_cleanup(void) {
    KillTimer(g_window, ID_TIMER_CLEANUP);
    g_cleanup_step = 0;
    clipboard_snapshot_free(&g_cleanup_saved_clipboard);
}

static int any_modifier_down(void) {
    return key_down(VK_CONTROL) || key_down(VK_LWIN) || key_down(VK_RWIN) ||
           key_down(VK_SHIFT) || key_down(VK_MENU) || key_down('X');
}

/*
 * Step 1: wait until every modifier and X are released (injecting Ctrl+C
 * while Win or Shift is still down would be another shortcut entirely),
 * then snapshot the clipboard and copy the selection.
 * Step 2: once the clipboard shows the copy, read it back, clean it, paste
 * it, and give the application time to take the paste (slow editors read
 * the clipboard late, so the wait is generous).
 * Step 4: put the user's own clipboard back — unless something else has
 * been copied meanwhile.
 */
static void start_selection_cleanup(void) {
    HWND foreground = GetForegroundWindow();
    if (g_cleanup_step) return;
    if (!foreground || foreground == g_window) {
        set_activity(L"Select text in another application, then press Ctrl + Win + X.");
        return;
    }
    /* Never inject Ctrl+C into a terminal (it interrupts the running
       program), a remote session, a password manager, or an excluded app. */
    if (process_is_excluded(foreground) || spelling_skipped_process(foreground)) {
        set_activity(L"Clean-up is not available in this application (terminal, remote session, or excluded app).");
        return;
    }
    g_cleanup_step = 1;
    g_cleanup_retries = 0;
    g_cleanup_started_at = GetTickCount();
    SetTimer(g_window, ID_TIMER_CLEANUP, 30, NULL);
}

static void continue_selection_cleanup(void) {
    switch (g_cleanup_step) {
        case 1:
            if (any_modifier_down()) {
                if (GetTickCount() - g_cleanup_started_at > 3000u) finish_selection_cleanup();
                return;
            }
            clipboard_snapshot_take(&g_cleanup_saved_clipboard);
            g_cleanup_sequence = GetClipboardSequenceNumber();
            send_shortcut('C');
            g_cleanup_step = 2;
            SetTimer(g_window, ID_TIMER_CLEANUP, 120, NULL);
            return;
        case 2: {
            wchar_t *text;
            if (GetClipboardSequenceNumber() == g_cleanup_sequence) {
                /* Slow applications copy asynchronously; wait a little. */
                if (++g_cleanup_retries < 5) return;
                set_activity(L"Nothing was selected, or the application did not copy it.");
                finish_selection_cleanup();
                return;
            }
            /* The clipboard now holds the selection: that is the state a
               later restore must find untouched. */
            g_cleanup_sequence = GetClipboardSequenceNumber();
            text = clipboard_text_copy();
            if (!text || !*text) {
                set_activity(L"The selection is not text.");
                if (text) HeapFree(GetProcessHeap(), 0, text);
                g_cleanup_step = 4;
                SetTimer(g_window, ID_TIMER_CLEANUP, 50, NULL);
                return;
            }
            if (!ks_clean_text(text, g_settings.persian_letters, g_settings.digits, g_settings.punctuation)) {
                set_activity(L"The selected text is already clean.");
                HeapFree(GetProcessHeap(), 0, text);
                g_cleanup_step = 4;
                SetTimer(g_window, ID_TIMER_CLEANUP, 50, NULL);
                return;
            }
            if (!clipboard_set_text(text)) {
                set_activity(L"Could not write to the clipboard.");
                HeapFree(GetProcessHeap(), 0, text);
                g_cleanup_step = 4;
                SetTimer(g_window, ID_TIMER_CLEANUP, 50, NULL);
                return;
            }
            HeapFree(GetProcessHeap(), 0, text);
            g_cleanup_sequence = GetClipboardSequenceNumber();
            send_shortcut('V');
            set_activity(L"Selection cleaned up: Persian letters, digits and punctuation.");
            g_cleanup_step = 4;
            SetTimer(g_window, ID_TIMER_CLEANUP, 900, NULL);
            return;
        }
        case 4:
            /* Restore only if nobody copied something newer meanwhile. */
            clipboard_snapshot_restore(&g_cleanup_saved_clipboard, g_cleanup_sequence);
            finish_selection_cleanup();
            return;
        default:
            finish_selection_cleanup();
            return;
    }
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
    {
        HMENU helpers_menu = CreatePopupMenu();
        AppendMenuW(helpers_menu, MF_STRING | (g_settings.punctuation ? MF_CHECKED : 0),
                    IDM_PUNCTUATION, L"Persian punctuation after Persian words (؟ ، ؛)");
        AppendMenuW(helpers_menu, MF_STRING | (g_settings.auto_capitalize ? MF_CHECKED : 0),
                    IDM_CAPITALIZE, L"Capitalise English sentences");
        AppendMenuW(helpers_menu, MF_STRING | (g_settings.snippets ? MF_CHECKED : 0),
                    IDM_SNIPPETS, L"Expand snippets");
        AppendMenuW(helpers_menu, MF_STRING, IDM_EDIT_SNIPPETS, L"Edit snippets…");
        AppendMenuW(helpers_menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(helpers_menu, MF_STRING | MF_GRAYED, IDM_CLEANUP,
                    L"Clean up selected text:  Ctrl + Win + X");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)helpers_menu, L"Typing helpers");
    }
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
/* at startup; if the screen's work area is smaller than the scaled window,  */
/* the scale is reduced so the whole dashboard, footer buttons included, is  */
/* always on screen. Every label column is wide enough for its longest       */
/* caption in Segoe UI 16px with room to spare, so nothing is clipped.       */
/* ------------------------------------------------------------------------ */

#define UI_CLIENT_WIDTH 840
#define UI_CLIENT_HEIGHT 748
#define UI_HEADER_HEIGHT 100
#define UI_MARGIN 32
#define UI_CARD_LEFT UI_MARGIN
#define UI_CARD_RIGHT (UI_CLIENT_WIDTH - UI_MARGIN)
#define UI_LABEL_LEFT 56
#define UI_CONTROL_LEFT 250
#define UI_CONTROL_WIDTH 296
#define UI_SIDE_LEFT 556
#define UI_SIDE_WIDTH 228

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
static HWND g_tile_values[UI_TILE_COUNT];
static HWND g_tile_captions[UI_TILE_COUNT];
static HWND g_personal_dictionary;
static HWND g_digits;
static HWND g_punctuation;
static HWND g_persian_letters;
static HWND g_capitalize;
static HWND g_snippets_check;
static HWND g_stats_label;

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
    SendMessageW(g_digits, CB_SETCURSEL, (WPARAM)g_settings.digits, 0);
    SendMessageW(g_punctuation, BM_SETCHECK, g_settings.punctuation ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_persian_letters, BM_SETCHECK, g_settings.persian_letters ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_capitalize, BM_SETCHECK, g_settings.auto_capitalize ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_snippets_check, BM_SETCHECK, g_settings.snippets ? BST_CHECKED : BST_UNCHECKED, 0);
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
    selection = SendMessageW(g_digits, CB_GETCURSEL, 0, 0);
    if (selection >= KS_DIGITS_OFF && selection <= KS_DIGITS_LATIN) g_settings.digits = (int)selection;
    g_settings.punctuation = SendMessageW(g_punctuation, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.persian_letters = SendMessageW(g_persian_letters, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.auto_capitalize = SendMessageW(g_capitalize, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.snippets = SendMessageW(g_snippets_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
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
    language = foreground_language(foreground);
    if (!query_process_basename(focused_window(foreground), process_name, MAX_PATH))
        safe_copy(process_name, MAX_PATH, L"No application yet");

    set_label_text(g_status_label, g_settings.enabled
        ? L"Protection is active" : L"Protection is paused");
    if (g_layout_requests_ignored > 0)
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"Typing in %ls  •  %ls layout%ls  •  %ld of %ld layout switches were not accepted by the app (keys translated by KeySwitchFix)",
                 process_name, language_name(language),
                 g_spelling_available ? L"" : L"  •  spelling data missing",
                 (long)g_layout_requests_ignored, (long)g_layout_requests);
    else
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
                 L"Keyboard hook: %ls  •  Undo: Backspace or Ctrl + Win + Backspace  •  Pause: Ctrl + Win + K  •  Clean up selection: Ctrl + Win + X",
                 g_keyboard_hook ? L"Running" : L"FAILED");
    }
    set_label_text(g_hook_label, buffer);
    set_label_text(g_activity_label, g_last_activity);
    stats_touch_day();
    set_tile_value(0, g_stats.today_layout);
    set_tile_value(1, g_stats.today_spelling);
    set_tile_value(2, g_stats.total_layout + g_stats.total_spelling);
    set_tile_value(3, (LONG)g_stats.today_keys);
    {
        const wchar_t *word = NULL;
        unsigned count = 0;
        long minutes = ks_stats_seconds_saved(&g_stats) / 60;
        wchar_t top[160];
        top[0] = 0;
        if (ks_stats_top(&g_stats, 0, &word, &count) && count >= 2) {
            const wchar_t *second = NULL;
            unsigned second_count = 0;
            if (ks_stats_top(&g_stats, 1, &second, &second_count) && second_count >= 2)
                swprintf(top, 160, L"  •  most corrected: %ls (%u), %ls (%u)", word, count, second, second_count);
            else
                swprintf(top, 160, L"  •  most corrected: %ls (%u)", word, count);
        }
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"Session: %ld keys, %ld fixes  •  %ld active days  •  about %ld minute%ls saved in total%ls",
                 (long)g_keys_seen, (long)(g_corrections + g_spelling_fixes + g_snippets_used),
                 g_stats.days_active, minutes, minutes == 1 ? L"" : L"s", top);
        set_label_text(g_stats_label, buffer);
    }
    InvalidateRect(g_enable_button, NULL, TRUE);
}

#define UI_TILE_WIDTH 176

static void create_tile(HWND window, int index, int left, int top, const wchar_t *caption) {
    g_tile_values[index] = create_child(L"STATIC", L"0", SS_ENDELLIPSIS, 0,
                                        left + 14, top + 6, UI_TILE_WIDTH - 28, 28, window,
                                        IDC_TILE_VALUE + index);
    g_tile_captions[index] = create_child(L"STATIC", caption, SS_ENDELLIPSIS, 0,
                                          left + 14, top + 33, UI_TILE_WIDTH - 28, 16, window,
                                          IDC_TILE_CAPTION + index);
}

/* Vertical layout (design pixels): cards and rows are computed from these so
   the settings card can grow without hand-tuned coordinates. */
#define UI_CARD1_TOP (UI_HEADER_HEIGHT + 16)
#define UI_CARD1_BOTTOM (UI_CARD1_TOP + 72)
#define UI_CARD2_TOP (UI_CARD1_BOTTOM + 16)
#define UI_ROW_PITCH 40
#define UI_ROW(index) (UI_CARD2_TOP + 50 + (index) * UI_ROW_PITCH)
#define UI_CARD2_BOTTOM (UI_ROW(5) + 46)
#define UI_CARD3_TOP (UI_CARD2_BOTTOM + 16)
#define UI_TILE_TOP (UI_CARD3_TOP + 40)
#define UI_TILE_HEIGHT 52
#define UI_TILE_GAP 10
#define UI_STATS_TOP (UI_TILE_TOP + UI_TILE_HEIGHT + 10)
#define UI_HOOK_TOP (UI_STATS_TOP + 20)
#define UI_ACTIVITY_TOP (UI_HOOK_TOP + 20)
#define UI_CARD3_BOTTOM (UI_ACTIVITY_TOP + 26)
#define UI_BUTTON_TOP (UI_CARD3_BOTTOM + 14)
#define UI_BUTTON_HEIGHT 36

static void create_ui(HWND window) {
    g_font_regular = create_ui_font(15, FW_NORMAL);
    g_font_medium = create_ui_font(16, FW_SEMIBOLD);
    g_font_title = create_ui_font(28, FW_BOLD);
    g_font_status = create_ui_font(21, FW_SEMIBOLD);
    g_font_tile = create_ui_font(24, FW_BOLD);
    g_font_small = create_ui_font(13, FW_NORMAL);

    /* Card 1: status */
    g_status_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                  UI_LABEL_LEFT, UI_CARD1_TOP + 14, 570, 30, window, IDC_STATUS_LABEL);
    g_layout_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                  UI_LABEL_LEFT, UI_CARD1_TOP + 44, 570, 20, window, IDC_LAYOUT_LABEL);
    g_enable_button = create_child(L"BUTTON", L"", BS_OWNERDRAW, 0,
                                   UI_CARD_RIGHT - 24 - 140, UI_CARD1_TOP + 16, 140, 40, window, IDC_ENABLE);

    /* Card 2: settings — label column, control column, side column */
    g_sensitivity = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 0,
                                 UI_CONTROL_LEFT, UI_ROW(0), UI_CONTROL_WIDTH, 200, window, IDC_SENSITIVITY);
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Conservative");
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Balanced (recommended)");
    SendMessageW(g_sensitivity, CB_ADDSTRING, 0, (LPARAM)L"Sensitive");
    g_startup = create_child(L"BUTTON", L"Start with Windows", BS_AUTOCHECKBOX, 0,
                             UI_SIDE_LEFT, UI_ROW(0) + 2, UI_SIDE_WIDTH, 24, window, IDC_APP_STARTUP);

    g_language_mode = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 0,
                                   UI_CONTROL_LEFT, UI_ROW(1), UI_CONTROL_WIDTH, 200, window, IDC_LANGUAGE_MODE);
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Auto — sentence context");
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Prefer Persian for collisions");
    SendMessageW(g_language_mode, CB_ADDSTRING, 0, (LPARAM)L"Prefer English for collisions");
    g_capitalize = create_child(L"BUTTON", L"Auto-capitalise English", BS_AUTOCHECKBOX, 0,
                                UI_SIDE_LEFT, UI_ROW(1) + 2, UI_SIDE_WIDTH, 24, window, IDC_CAPITALIZE);

    g_spelling = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 0,
                              UI_CONTROL_LEFT, UI_ROW(2), UI_CONTROL_WIDTH, 200, window, IDC_SPELLING);
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Conservative");
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Balanced (recommended)");
    SendMessageW(g_spelling, CB_ADDSTRING, 0, (LPARAM)L"Aggressive");
    g_personal_dictionary = create_child(L"BUTTON", L"Remember undone words", BS_AUTOCHECKBOX, 0,
                                         UI_SIDE_LEFT, UI_ROW(2) + 2, UI_SIDE_WIDTH, 24, window,
                                         IDC_PERSONAL_DICTIONARY);

    g_digits = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 0,
                            UI_CONTROL_LEFT, UI_ROW(3), UI_CONTROL_WIDTH, 200, window, IDC_DIGITS);
    SendMessageW(g_digits, CB_ADDSTRING, 0, (LPARAM)L"As the layout types them");
    SendMessageW(g_digits, CB_ADDSTRING, 0, (LPARAM)L"Follow the layout (۱۲۳ / 123)");
    SendMessageW(g_digits, CB_ADDSTRING, 0, (LPARAM)L"Always Persian ۱۲۳");
    SendMessageW(g_digits, CB_ADDSTRING, 0, (LPARAM)L"Always English 123");
    g_punctuation = create_child(L"BUTTON", L"Persian marks ؟ ، ؛", BS_AUTOCHECKBOX, 0,
                                 UI_SIDE_LEFT, UI_ROW(3) + 2, UI_SIDE_WIDTH, 24, window, IDC_PUNCTUATION);

    g_persian_letters = create_child(L"BUTTON", L"Persian letters: ي ك → ی ک", BS_AUTOCHECKBOX, 0,
                                     UI_CONTROL_LEFT, UI_ROW(4) + 2, UI_CONTROL_WIDTH, 24, window,
                                     IDC_PERSIAN_LETTERS);
    g_snippets_check = create_child(L"BUTTON", L"Expand snippet shortcuts", BS_AUTOCHECKBOX, 0,
                                    UI_SIDE_LEFT, UI_ROW(4) + 2, UI_SIDE_WIDTH, 24, window, IDC_SNIPPETS);

    g_excluded = create_child(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                              UI_CONTROL_LEFT, UI_ROW(5), UI_CONTROL_WIDTH, 28, window, IDC_EXCLUDED);
    create_child(L"BUTTON", L"Edit snippets…", BS_OWNERDRAW, 0,
                 UI_SIDE_LEFT, UI_ROW(5) - 2, 150, 32, window, IDC_EDIT_SNIPPETS);

    /* Card 3: diagnostics and statistics */
    create_tile(window, 0, UI_LABEL_LEFT, UI_TILE_TOP, L"layout fixes today");
    create_tile(window, 1, UI_LABEL_LEFT + (UI_TILE_WIDTH + UI_TILE_GAP), UI_TILE_TOP, L"spelling fixes today");
    create_tile(window, 2, UI_LABEL_LEFT + 2 * (UI_TILE_WIDTH + UI_TILE_GAP), UI_TILE_TOP, L"fixes, all time");
    create_tile(window, 3, UI_LABEL_LEFT + 3 * (UI_TILE_WIDTH + UI_TILE_GAP), UI_TILE_TOP, L"keys today");
    g_stats_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                 UI_LABEL_LEFT, UI_STATS_TOP, 736, 18, window, IDC_STATS_LABEL);
    g_hook_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                UI_LABEL_LEFT, UI_HOOK_TOP, 736, 18, window, IDC_HOOK_LABEL);
    g_activity_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS, 0,
                                    UI_LABEL_LEFT, UI_ACTIVITY_TOP, 736, 18, window, IDC_ACTIVITY_LABEL);

    /* Footer */
    create_child(L"BUTTON", L"Save settings", BS_OWNERDRAW, 0, UI_MARGIN, UI_BUTTON_TOP, 160, UI_BUTTON_HEIGHT,
                 window, IDC_SAVE);
    create_child(L"BUTTON", L"Hide to tray", BS_OWNERDRAW, 0, UI_MARGIN + 172, UI_BUTTON_TOP, 150,
                 UI_BUTTON_HEIGHT, window, IDC_HIDE);

    {
        HWND child = GetWindow(window, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_font_regular, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }
    }
    SendMessageW(g_status_label, WM_SETFONT, (WPARAM)g_font_status, TRUE);
    SendMessageW(g_layout_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(g_stats_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(g_hook_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SendMessageW(g_activity_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    {
        int i;
        for (i = 0; i < UI_TILE_COUNT; ++i) {
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
    for (i = 0; i < UI_TILE_COUNT; ++i)
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
    /* Left: name and tagline. Right: status pill above the version line.
       Everything sits inside the gradient with a clear margin below. */
    SelectObject(dc, g_font_title);
    SetTextColor(dc, RGB(255, 255, 255));
    draw_text(dc, UI_MARGIN, 22, L"KeySwitchFix");
    SelectObject(dc, g_font_regular);
    SetTextColor(dc, RGB(196, 208, 236));
    draw_text(dc, UI_MARGIN + 2, 60, L"Persian ↔ English layout repair, spelling, and typing helpers");
    SelectObject(dc, g_font_small);
    SetTextColor(dc, RGB(160, 176, 214));
    draw_text_right(dc, UI_CARD_RIGHT, 66, L"v" APP_VERSION L"  •  offline  •  no logging");

    /* Status pill */
    {
        const wchar_t *text = g_settings.enabled ? L"Active" : L"Paused";
        COLORREF dot = g_settings.enabled ? UI_GREEN : UI_GREY;
        fill_round_rect(dc, UI_CARD_RIGHT - 118, 24, UI_CARD_RIGHT, 54, 15,
                        RGB(52, 82, 146), RGB(70, 104, 174));
        {
            HBRUSH brush = CreateSolidBrush(dot);
            HPEN pen = CreatePen(PS_SOLID, 1, dot);
            HGDIOBJ old_brush = SelectObject(dc, brush);
            HGDIOBJ old_pen = SelectObject(dc, pen);
            Ellipse(dc, scale(UI_CARD_RIGHT - 102), scale(33), scale(UI_CARD_RIGHT - 90), scale(45));
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(pen);
            DeleteObject(brush);
        }
        SelectObject(dc, g_font_medium);
        SetTextColor(dc, RGB(255, 255, 255));
        draw_text(dc, UI_CARD_RIGHT - 80, 29, text);
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

    draw_card(dc, UI_CARD_LEFT, UI_CARD1_TOP, UI_CARD_RIGHT, UI_CARD1_BOTTOM);
    draw_card(dc, UI_CARD_LEFT, UI_CARD2_TOP, UI_CARD_RIGHT, UI_CARD2_BOTTOM);
    draw_card(dc, UI_CARD_LEFT, UI_CARD3_TOP, UI_CARD_RIGHT, UI_CARD3_BOTTOM);

    /* Tiles */
    {
        int i;
        for (i = 0; i < UI_TILE_COUNT; ++i) {
            int left = UI_LABEL_LEFT + i * (UI_TILE_WIDTH + UI_TILE_GAP);
            fill_round_rect(dc, left, UI_TILE_TOP, left + UI_TILE_WIDTH, UI_TILE_TOP + UI_TILE_HEIGHT,
                            14, UI_TILE, UI_TILE);
        }
    }

    SelectObject(dc, g_font_medium);
    SetTextColor(dc, UI_TEXT);
    draw_text(dc, UI_LABEL_LEFT, UI_CARD2_TOP + 14, L"Correction and typing settings");
    draw_text(dc, UI_LABEL_LEFT, UI_CARD3_TOP + 12, L"Live diagnostics and statistics");
    SelectObject(dc, g_font_regular);
    SetTextColor(dc, UI_MUTED);
    draw_text(dc, UI_LABEL_LEFT, UI_ROW(0) + 4, L"Sensitivity");
    draw_text(dc, UI_LABEL_LEFT, UI_ROW(1) + 4, L"Writing language");
    draw_text(dc, UI_LABEL_LEFT, UI_ROW(2) + 4, L"Spelling");
    draw_text(dc, UI_LABEL_LEFT, UI_ROW(3) + 4, L"Digits");
    draw_text(dc, UI_LABEL_LEFT, UI_ROW(4) + 4, L"Typing helpers");
    draw_text(dc, UI_LABEL_LEFT, UI_ROW(5) + 4, L"Excluded apps");
    SelectObject(dc, g_font_small);
    draw_text_right(dc, UI_CARD_RIGHT, UI_BUTTON_TOP + 10, L"No cloud, no logging, no background service");
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
    else if (item->CtlID == IDC_EDIT_SNIPPETS) color = RGB(76, 140, 180);
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
            /* Counters reach the disk every ten minutes and at exit; the
               snippets file is checked for changes every two seconds. */
            SetTimer(window, ID_TIMER_STATS, 600000, NULL);
            SetTimer(window, ID_TIMER_SNIPPETS, 2000, NULL);
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
                               (HWND)lparam == g_tile_values[2] ||
                               (HWND)lparam == g_tile_values[3];
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
                case IDC_EDIT_SNIPPETS:
                case IDM_EDIT_SNIPPETS:
                    open_snippets_file();
                    return 0;
                case IDM_PUNCTUATION:
                case IDM_CAPITALIZE:
                case IDM_SNIPPETS: {
                    int *flag = LOWORD(wparam) == IDM_PUNCTUATION ? &g_settings.punctuation
                              : LOWORD(wparam) == IDM_CAPITALIZE ? &g_settings.auto_capitalize
                              : &g_settings.snippets;
                    *flag = !*flag;
                    save_settings();
                    update_controls_from_settings();
                    set_activity(LOWORD(wparam) == IDM_PUNCTUATION
                        ? (g_settings.punctuation ? L"Persian punctuation after Persian words: on." : L"Persian punctuation: off.")
                        : LOWORD(wparam) == IDM_CAPITALIZE
                            ? (g_settings.auto_capitalize ? L"English sentence capitalisation: on." : L"English sentence capitalisation: off.")
                            : (g_settings.snippets ? L"Snippets: on." : L"Snippets: off."));
                    return 0;
                }
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
            if (wparam == ID_HOTKEY_CLEANUP) {
                start_selection_cleanup();
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
            if (wparam == ID_TIMER_CLEANUP) {
                continue_selection_cleanup();
                return 0;
            }
            if (wparam == ID_TIMER_STATS) {
                stats_touch_day();
                stats_save();
                return 0;
            }
            if (wparam == ID_TIMER_SNIPPETS) {
                if (g_settings.snippets) snippets_reload(0);
                return 0;
            }
            break;
        case WM_APP_DIAGNOSTIC:
            update_diagnostics_ui();
            return 0;
        case WM_APP_SAVE_STATS:
            stats_save();
            return 0;
        case WM_QUERYENDSESSION:
            stats_save();
            save_settings();
            return TRUE;
        case WM_ENDSESSION:
            if (wparam) stats_save();
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
            KillTimer(window, ID_TIMER_CLEANUP);
            KillTimer(window, ID_TIMER_STATS);
            KillTimer(window, ID_TIMER_SNIPPETS);
            stats_save();
            clipboard_snapshot_free(&g_cleanup_saved_clipboard);
            if (g_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_UNDO);
            if (g_toggle_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_TOGGLE);
            if (g_cleanup_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_CLEANUP);
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
        RECT frame;
        RECT work_area;
        /*
         * Fit to the screen: on a small laptop screen or a high scaling
         * factor the scaled dashboard could be taller than the work area,
         * with the footer buttons off screen. Shrink the scale until the
         * whole window fits; fonts and controls scale with it.
         */
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
            int available_height = work_area.bottom - work_area.top;
            int available_width = work_area.right - work_area.left;
            int guess;
            for (guess = 0; guess < 8; ++guess) {
                RECT probe = {0, 0, scale(UI_CLIENT_WIDTH), scale(UI_CLIENT_HEIGHT)};
                AdjustWindowRectEx(&probe, style, FALSE, WS_EX_APPWINDOW);
                if (probe.bottom - probe.top <= available_height &&
                    probe.right - probe.left <= available_width) break;
                g_dpi = MulDiv(g_dpi, 95, 100);
                if (g_dpi < 72) { g_dpi = 72; break; }
            }
        }
        frame.left = 0;
        frame.top = 0;
        frame.right = scale(UI_CLIENT_WIDTH);
        frame.bottom = scale(UI_CLIENT_HEIGHT);
        AdjustWindowRectEx(&frame, style, FALSE, WS_EX_APPWINDOW);
        g_window = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"KeySwitchFix 3.0.1",
                                   style, CW_USEDEFAULT, CW_USEDEFAULT,
                                   frame.right - frame.left, frame.bottom - frame.top,
                                   NULL, NULL, instance, NULL);
    }
    if (!g_window) {
        MessageBoxW(NULL, L"The main window could not be created.", APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 4;
    }

    stats_load();
    snippets_reload(1);
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
    /* Ctrl + Win + X cleans up the selected text (Persian letters, digits,
       punctuation) in place. */
    g_cleanup_hotkey_registered = RegisterHotKey(g_window, ID_HOTKEY_CLEANUP,
                                                  MOD_CONTROL | MOD_WIN | MOD_NOREPEAT, 'X');
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
