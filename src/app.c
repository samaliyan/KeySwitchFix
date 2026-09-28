#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <oleacc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "core.h"
#include "spell.h"
#include "typing.h"
#include "domain.h"
#include "memory.h"
#include "../resources/resource.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#define APP_NAME L"KeySwitchFix"
#define APP_VERSION L"3.2.0"
#define APP_MUTEX L"Local\\KeySwitchFix.Native.2.0"
#define WINDOW_CLASS L"KeySwitchFix.MainWindow.2"

#define WM_APP_TRAY (WM_APP + 1)
#define WM_APP_DIAGNOSTIC (WM_APP + 2)
#define WM_APP_EXIT (WM_APP + 9)
/* Posted from the hook: work that must not run inside the hook callback. */
#define WM_APP_SAVE_STATS (WM_APP + 3)
#define WM_APP_FOCUS_QUERY (WM_APP + 4)

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
#define ID_TIMER_TRAY_RETRY 20
#define ID_TIMER_FOCUS_QUERY 21

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
#define IDC_LEARN_WRITING 140
#define IDC_VOCAB_IT 141
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
#define IDM_LEARN_WRITING 214
#define IDM_OPEN_MEMORY 215
#define IDM_FORGET_MEMORY 216
#define IDM_VOCAB_IT 217

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
    /* 3.1: remember the user's words and hand repairs on this PC (opt-in),
       and the IT & computing vocabulary pack. */
    int learn_writing;
    int vocab_it;
    wchar_t excluded[512];
} SETTINGS;

typedef struct UNDO_RECORD {
    int valid;
    HWND window;
    /* The control that had keyboard focus: an undo is only ever typed back
       into the same field (after Tab the caret is in another one). */
    HWND focus;
    KS_LANGUAGE source_language;
    UINT delimiter;
    int delimiter_zwnj;
    /* 1 when this was a spelling fix; undoing it teaches the ignore list. */
    int spelling;
    /* 1 when this was a repair learned from the user; undoing unlearns it. */
    int learned;
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
/* Writing memory (opt-in) and the vocabulary packs. */
static KS_WRITING_MEMORY g_memory;
static int g_memory_active;
static wchar_t g_memory_path[MAX_PATH];
static DWORD g_memory_saved_at;
/* Last-write time of writing-memory.txt as this process last read or wrote
   it: a newer file means the user edited it, and the file wins. */
static FILETIME g_memory_file_time;
static KS_EXTRA_WORDS g_extra_words;
static const KS_LANGUAGE g_rank_language_en = KS_LANG_ENGLISH;
static const KS_LANGUAGE g_rank_language_fa = KS_LANG_PERSIAN;
/* Hand-repair tracking. The "peak" is the longest text the current word
   had before the user started deleting; if the finished word differs, the
   pair is a repair the memory can learn ("عسیسم" → "عزیزم"). */
static unsigned long g_key_serial;
static wchar_t g_word_peak[KS_MAX_WORD + 1];
static int g_word_peak_length;
/* 1 when the peak is a finished word (reopened after its Space). */
static int g_word_peak_complete;
static wchar_t g_orphan_peak[KS_MAX_WORD + 1];
static HWND g_orphan_window;
static unsigned long g_orphan_serial;
static int g_orphan_complete;
/* The word most recently counted, so an Undo right after can take it back. */
static wchar_t g_last_noted[KS_MAX_WORD + 1];
/*
 * Facts about the focused control, computed once per focus instead of per
 * key: process-based exclusions, developer tools and remote sessions,
 * password fields (Win32 Edit styles, and MSAA's "protected" state, which
 * browsers, Electron and WPF expose for password inputs), Excel cells
 * (AutoComplete selections) and Office apps with AutoCorrect.
 */
typedef struct FOCUS_FACTS FOCUS_FACTS;
struct FOCUS_FACTS {
    HWND focus;
    unsigned long focus_serial;
    unsigned long settings_generation;
    DWORD computed_at;
    int excluded;
    int developer_tool;
    int remote;
    int protected_field;
    int excel;
    int autocorrect_app;
};
static FOCUS_FACTS g_facts;
static unsigned long g_settings_generation = 1;
/* Focus changes reported by the WinEvent hook. */
static HWINEVENTHOOK g_focus_event_hook;
static unsigned long g_focus_serial;
static HWND g_focus_event_window;
static LONG g_focus_event_object;
static LONG g_focus_event_child;
static int g_focus_event_protected;
/* Re-entrancy: a low-level hook call is delivered as a sent message, so a
   new key can arrive while this thread waits inside SendMessageTimeout on
   behalf of an earlier key. The nested key passes through untouched, and
   the outer operation, which no longer knows what is on screen, gives up. */
static int g_engine_depth;
static int g_engine_interrupted;
static int g_word_skip_untrusted;   /* the current word is skipped only because it is untrusted */
static DWORD g_hook_entered_at;   /* tick when the hook call being handled began */
/* Excel: 1 while the active cell's content is exactly what was typed since
   the cell was entered (Enter, Tab, arrows, a single click), so the rest of
   an AutoComplete suggestion may sit selected after the caret. F2 or a
   double-click edits existing text: then nothing may be deleted forward. */
static int g_cell_fresh = 1;
static int g_cell_edit_mode;
static DWORD g_last_click_at;
/* Set by every path that swallows the current key-down. */
static int g_key_swallowed;

/* The last word finished with Space or Enter, so a Backspace straight after
   it can reopen it for editing. */
static KS_TOKEN g_prev_word[KS_MAX_WORD];
static int g_prev_word_count;
static KS_LANGUAGE g_prev_word_language;
static HWND g_prev_word_window;
static unsigned long g_prev_word_serial;
static DWORD g_prev_word_at;
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
static void update_tray_tip(void);
static int is_protected_field(HWND foreground);
static int spelling_skipped_process(HWND foreground);
static int process_is_excluded(HWND foreground);
static void engine_reset_for_focus(void);
static const FOCUS_FACTS *focus_facts(HWND foreground);

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
    swprintf(g_memory_path, sizeof(g_memory_path) / sizeof(g_memory_path[0]),
             L"%ls\\writing-memory.txt", g_data_directory);
}

/* True when this copy is the installed one (%LOCALAPPDATA%\Programs\KeySwitchFix).
   A copy run from Downloads must not register itself to start with Windows
   unless the user asks for it. */
static int running_installed_copy(void) {
    wchar_t self[MAX_PATH];
    wchar_t expected[MAX_PATH];
    wchar_t local[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    size_t prefix;
    if (!length || length >= MAX_PATH) return 0;
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return 0;
    swprintf(expected, MAX_PATH, L"%ls\\Programs\\KeySwitchFix\\", local);
    prefix = wcslen(expected);
    return wcslen(self) > prefix && _wcsnicmp(self, expected, prefix) == 0;
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
    g_settings.start_with_windows = GetPrivateProfileIntW(L"General", L"StartWithWindows",
                                                          running_installed_copy() ? 1 : 0,
                                                          g_settings_path);
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
    g_settings.learn_writing = GetPrivateProfileIntW(L"Memory", L"LearnWriting", 0, g_settings_path) != 0;
    g_settings.vocab_it = GetPrivateProfileIntW(L"Vocabulary", L"IT", 1, g_settings_path) != 0;
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
    ++g_settings_generation;   /* cached per-focus facts depend on settings */
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
    swprintf(number, 16, L"%d", g_settings.learn_writing);
    WritePrivateProfileStringW(L"Memory", L"LearnWriting", number, g_settings_path);
    swprintf(number, 16, L"%d", g_settings.vocab_it);
    WritePrivateProfileStringW(L"Vocabulary", L"IT", number, g_settings_path);
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
    wchar_t temporary[MAX_PATH];
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
    /* Write a sibling temporary file and swap it in, so a crash or a full
       disk never leaves a half-written memory or dictionary behind. */
    swprintf(temporary, MAX_PATH, L"%ls.tmp", path);
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD size = (DWORD)(length - 1 + 3);
        ok = WriteFile(file, utf8, size, &written, NULL) != 0 && written == size;
        if (ok) ok = FlushFileBuffers(file) != 0;
        CloseHandle(file);
        if (ok) ok = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        if (!ok) DeleteFileW(temporary);
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
    L"#         {time} 14:05   {time:fa} ۱۴:۰۵\r\n"
    L"#         {n} new line (Enter; in chat apps that sends the message)   {t} Tab   {{ }} literal braces\r\n"
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
    {
        /* A 3.0/3.1 file used \\n and \\t: rewrite it once to {n} and {t}
           so existing multi-line snippets keep working. */
        size_t capacity = wcslen(text) * 2 + 16;
        wchar_t *migrated = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(wchar_t));
        wchar_t backup[MAX_PATH + 8];
        swprintf(backup, sizeof(backup) / sizeof(backup[0]), L"%ls.bak", g_snippets_path);
        if (migrated && ks_snippets_migrate(text, migrated, capacity) &&
            CopyFileW(g_snippets_path, backup, FALSE) && write_text_file(g_snippets_path, migrated)) {
            HeapFree(GetProcessHeap(), 0, text);
            text = migrated;
            migrated = NULL;
            ZeroMemory(&g_snippets_loaded_time, sizeof(g_snippets_loaded_time));
            set_activity(L"snippets.txt was updated: \\n and \\t are now written {n} and {t}.");
        }
        if (migrated) HeapFree(GetProcessHeap(), 0, migrated);
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

/* ---- Writing memory ------------------------------------------------------ */

static int memory_file_time(FILETIME *time) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExW(g_memory_path, GetFileExInfoStandard, &attributes)) return 0;
    *time = attributes.ftLastWriteTime;
    return 1;
}

static void memory_load(void);

/* The user saved the file in an editor since we last touched it. */
static int memory_file_edited(void) {
    FILETIME now;
    return memory_file_time(&now) && CompareFileTime(&now, &g_memory_file_time) != 0;
}

static void memory_save(void) {
    size_t length;
    wchar_t *text;
    if (!g_memory_active) return;
    if (memory_file_edited()) {
        /* Hand edits win over what was learned since the last save. */
        memory_load();
        set_activity(L"writing-memory.txt was edited; KeySwitchFix reloaded it.");
        return;
    }
    if (!g_memory.dirty) return;
    length = ks_memory_serialize(&g_memory, NULL, 0);
    text = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(wchar_t));
    if (!text) return;
    ks_memory_serialize(&g_memory, text, length + 1);
    if (write_text_file(g_memory_path, text)) {
        g_memory.dirty = 0;
        memory_file_time(&g_memory_file_time);
    } else {
        set_activity(L"Could not save writing-memory.txt in the KeySwitchFix data folder.");
    }
    HeapFree(GetProcessHeap(), 0, text);
    g_memory_saved_at = GetTickCount();
}

static void memory_load(void) {
    wchar_t *text;
    ks_memory_reset(&g_memory);
    g_memory_active = 0;
    if (!g_settings.learn_writing) return;
    ZeroMemory(&g_memory_file_time, sizeof(g_memory_file_time));
    text = read_text_file(g_memory_path, &g_memory_file_time);
    if (text) {
        ks_memory_parse(&g_memory, text);
        HeapFree(GetProcessHeap(), 0, text);
    }
    g_memory_active = 1;
}

/* Settings changed: start or stop learning without losing what was learned. */
static void memory_apply_setting(void) {
    if (g_settings.learn_writing && !g_memory_active) memory_load();
    else if (!g_settings.learn_writing && g_memory_active) {
        memory_save();
        ks_memory_reset(&g_memory);
        g_memory_active = 0;
    }
}

static void memory_forget(void) {
    ks_memory_reset(&g_memory);
    DeleteFileW(g_memory_path);
    set_activity(L"Writing memory cleared: every learned word and repair is forgotten.");
}

static void open_memory_file(void) {
    if (g_memory_active) {
        g_memory.dirty = 1;
        memory_save();
    } else if (GetFileAttributesW(g_memory_path) == INVALID_FILE_ATTRIBUTES) {
        set_activity(L"Turn on \u201cLearn my writing\u201d first; the memory file is created as you type.");
        return;
    }
    ShellExecuteW(NULL, L"open", L"notepad.exe", g_memory_path, NULL, SW_SHOWNORMAL);
    set_activity(L"writing-memory.txt opened. Save it and your edits apply within seconds.");
}

/* Known words beyond the Blooms: the vocabulary pack, and words this user
   types often. English arrives lower-case, Persian without diacritics. */
static int extra_word_known(const void *context, KS_LANGUAGE language, const wchar_t *word) {
    (void)context;
    if (g_settings.vocab_it && ks_domain_contains(KS_DOMAIN_IT, language, word)) return 1;
    return g_memory_active && ks_memory_word_count(&g_memory, word) >= KS_MEMORY_KNOWN_COUNT;
}

static int extra_word_prefix(const void *context, KS_LANGUAGE language, const wchar_t *prefix) {
    (void)context;
    return g_settings.vocab_it && ks_domain_has_prefix(KS_DOMAIN_IT, language, prefix);
}

/* Spelling ranks: pack terms rank as everyday words (zipf 4.0); the user's
   frequent words rank by how often they are typed, and dictionary words the
   user favours are lifted a little, so ambiguous typos resolve toward the
   words this person writes. */
static int spell_rank_adjust(const void *context, const wchar_t *word, int table_rank) {
    KS_LANGUAGE language = *(const KS_LANGUAGE *)context;
    if (table_rank < 0 && g_settings.vocab_it && ks_domain_contains(KS_DOMAIN_IT, language, word))
        table_rank = 40;
    return g_memory_active ? ks_memory_rank_adjust(&g_memory, word, table_rank) : table_rank;
}

static int letters_only_word(const wchar_t *text, KS_LANGUAGE language) {
    if (!text || !text[0] || !text[1]) return 0;
    for (; *text; ++text) {
        if (language == KS_LANG_ENGLISH ? !ks_is_latin_letter(*text) : !ks_is_persian_letter(*text))
            return 0;
    }
    return 1;
}

/* all lower-case, or only the first letter capital ("Teh" at a sentence
   start); ALL-CAPS and camelCase are names, acronyms or code. */
static int ordinary_case(const wchar_t *text) {
    const wchar_t *cursor;
    for (cursor = text; *cursor; ++cursor)
        if (*cursor >= L'A' && *cursor <= L'Z' && cursor != text) return 0;
    return 1;
}

static void lower_first(wchar_t *text) {
    for (; *text; ++text)
        if (*text >= L'A' && *text <= L'Z') *text = (wchar_t)(*text - L'A' + L'a');
}

/* One finished word, as it now stands on screen. Never called for
   developer tools, excluded apps or password fields (typing_helpers). */
static void memory_note_word(const wchar_t *text, KS_LANGUAGE language) {
    wchar_t word[KS_MAX_WORD + 1];
    if (!g_memory_active || !text || wcslen(text) > KS_MAX_WORD) return;
    safe_copy(word, KS_MAX_WORD + 1, text);
    if (language == KS_LANG_ENGLISH) lower_first(word);
    if (!letters_only_word(word, language)) return;
    ks_memory_observe_word(&g_memory, word);
    safe_copy(g_last_noted, KS_MAX_WORD + 1, word);
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
    g_key_swallowed = 1;
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
    g_word_skip_untrusted = 0;
    g_word_auto_capitalized = 0;
    g_word_peak[0] = 0;
    g_word_peak_length = 0;
    g_word_peak_complete = 0;
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

/*
 * Every engine operation (a key in the hook, the pause timer, Undo, the
 * clean-up hotkey, the focus query) runs inside engine_enter/engine_leave.
 * A key that arrives while one is running (hooks are re-entered whenever
 * the thread pumps messages) is let through untouched and marks the
 * operation interrupted: whatever it was about to do would count keys that
 * are no longer on the screen. The letters that follow such a key belong
 * to a word the engine did not see begin, so that word is left alone.
 */
static int g_word_untrusted;
static int g_engine_key_interrupted;

static int engine_enter(void) {
    if (g_engine_depth > 0) return 0;
    ++g_engine_depth;
    g_engine_interrupted = 0;
    g_engine_key_interrupted = 0;
    return 1;
}

static void engine_leave(void) {
    if (g_engine_interrupted) {
        clear_word();
        clear_history();
        g_undo.valid = 0;
        g_prev_word_count = 0;
        g_orphan_peak[0] = 0;
        /* Only a key that slipped through leaves a word whose beginning the
           engine did not see; a click or a focus change starts clean. */
        if (g_engine_key_interrupted) g_word_untrusted = 1;
    }
    g_engine_interrupted = 0;
    g_engine_key_interrupted = 0;
    --g_engine_depth;
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

/* The tracked flags are resynchronised from the physical state on every
   key-down (see keyboard_hook_proc): a key-up the hook never saw — on the
   secure desktop after Win+L, in an elevated window after Alt+Tab — must
   not leave a modifier "stuck" and the engine silently disabled. */
static void resync_modifiers(void) {
    g_shift_down = key_down(VK_SHIFT);
    g_control_down = key_down(VK_CONTROL);
    g_alt_down = key_down(VK_MENU);
    g_windows_down = key_down(VK_LWIN) || key_down(VK_RWIN);
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

/*
 * Which alphabet a keyboard layout types. The language ID of the HKL is the
 * input *language*, not the keyboard: a Persian keyboard can be added under
 * English and vice versa. So the layout is asked what the A and Q keys
 * produce; the language ID is only the fallback. Cached per HKL.
 */
static KS_LANGUAGE language_from_layout(HKL layout) {
    static HKL cached_layouts[16];
    static KS_LANGUAGE cached_languages[16];
    static int cached_count;
    LANGID language_id = LOWORD((ULONG_PTR)layout);
    WORD primary = PRIMARYLANGID(language_id);
    KS_LANGUAGE language = KS_LANG_OTHER;
    static const UINT probes[2] = {0x1E, 0x10};
    int i;
    if (!layout) return KS_LANG_OTHER;
    for (i = 0; i < cached_count; ++i)
        if (cached_layouts[i] == layout) return cached_languages[i];
    for (i = 0; i < 2 && language == KS_LANG_OTHER; ++i) {
        BYTE state[256];
        wchar_t output[4];
        UINT virtual_key = MapVirtualKeyExW(probes[i], MAPVK_VSC_TO_VK_EX, layout);
        int count;
        if (!virtual_key) continue;
        ZeroMemory(state, sizeof(state));
        count = ToUnicodeEx(virtual_key, probes[i], state, output, 4, 4, layout);
        if (count != 1) continue;
        if ((output[0] >= L'a' && output[0] <= L'z') || (output[0] >= L'A' && output[0] <= L'Z'))
            language = KS_LANG_ENGLISH;
        else if (output[0] >= 0x0600 && output[0] <= 0x06FF)
            language = primary == 0x29 || primary == LANG_ENGLISH || primary == 0 ? KS_LANG_PERSIAN
                                                                                  : KS_LANG_OTHER;
    }
    /* An Arabic, Urdu or Kurdish keyboard also types Arabic script; only a
       Persian one (or one filed under English) is ours. Latin keyboards
       for other languages (German, French) stay unsupported. */
    if (language == KS_LANG_ENGLISH && primary != LANG_ENGLISH && primary != 0x29) language = KS_LANG_OTHER;
    if (language == KS_LANG_OTHER && primary == LANG_ENGLISH) language = KS_LANG_ENGLISH;
    if (language == KS_LANG_OTHER && primary == 0x29) language = KS_LANG_PERSIAN;
    if (cached_count < 16) {
        cached_layouts[cached_count] = layout;
        cached_languages[cached_count++] = language;
    }
    return language;
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

static int compute_process_excluded(HWND foreground) {
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

static int edit_control_protected(HWND foreground) {
    HWND focus = focused_window(foreground);
    LONG_PTR style;
    wchar_t class_name[64];
    DWORD_PTR result = 0;
    if (!focus) return 0;
    class_name[0] = 0;
    GetClassNameW(focus, class_name, 64);
    /* ES_PASSWORD is an Edit-control style bit; on other classes the same
       bit means something else. Superclassed edits (WinForms
       "WindowsForms10.EDIT…", Delphi TEdit, VB6 text boxes) report "Edit"
       as their real class. */
    if (_wcsicmp(class_name, L"Edit") != 0 && _wcsnicmp(class_name, L"RichEdit", 8) != 0) {
        wchar_t real_class[64];
        real_class[0] = 0;
        RealGetWindowClassW(focus, real_class, 64);
        if (_wcsicmp(real_class, L"Edit") != 0) {
            /* Unknown text-box classes: only the password character, which
               non-edit classes simply do not answer, is asked. */
            if (!wcsstr(class_name, L"EDIT") && !wcsstr(class_name, L"Edit") &&
                !wcsstr(class_name, L"TextBox")) return 0;
            if (SendMessageTimeoutW(focus, EM_GETPASSWORDCHAR, 0, 0, SMTO_ABORTIFHUNG, 40, &result) && result)
                return 1;
            return 0;
        }
    }
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

/* 1 when the layout that will render this key produces exactly one
   printable character for it (not a dead key, not a ligature, not nothing):
   only then does one key stand for one character on screen. */
static int active_layout_types_one_character(DWORD scan_code, int shift, int caps) {
    HKL layout = g_target.thread ? GetKeyboardLayout(g_target.thread) : NULL;
    wchar_t character = 0;
    if (!layout) return 1;
    return translated_layout_character(layout, scan_code, shift, caps, &character);
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
    INPUT inputs[(KS_MAX_PHRASE_CHARS + 3) * 4];
    UINT count = 0;
    int excel_delete;
    int i;
    const wchar_t *cursor;
    size_t replacement_length;
    if (!replacement || delete_count < 0 ||
        delete_count > KS_MAX_PHRASE_CHARS) return 0;
    replacement_length = wcslen(replacement);
    if (replacement_length > KS_MAX_PHRASE_CHARS) return 0;
    /* Excel's AutoComplete leaves the rest of a matching entry selected
       after the caret; the first Backspace would only remove the selection.
       Delete clears it first, but only in a freshly entered cell: while
       existing text is edited it would delete the user's next character. */
    excel_delete = delete_count > 0 && g_cell_fresh && focus_facts(foreground)->excel;
    /* Let hook calls that are already waiting run now (they are nested and
       only mark this operation interrupted), so no key can land between the
       check below and the injected input. */
    {
        MSG pending;
        PeekMessageW(&pending, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    /* A key arrived while this operation waited on another thread: the text
       on screen is no longer what the delete count assumes. */
    if (g_engine_interrupted) return 0;
    /* Windows passes a key on by itself when the hook takes too long; if it
       did, the key is already in the application and the count is off. */
    if (g_hook_entered_at && GetTickCount() - g_hook_entered_at > 150u) {
        set_activity(L"A correction was skipped because the system was busy.");
        return 0;
    }
    if (excel_delete) add_virtual_input(inputs, &count, VK_DELETE);
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
    g_undo.focus = focused_window(foreground);
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
    /* Word, Outlook and OneNote rewrite text behind the caret themselves
       (AutoCorrect: dont → don't): the history no longer matches the
       screen, so earlier words are never rewritten there. */
    if (focus_facts(foreground)->autocorrect_app) return 0;

    /* At most the three previous words: a longer rewrite deletes more text
       than anyone can check at a glance. */
    maximum_words = g_history_count + 1;
    if (maximum_words > 4) maximum_words = 4;
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
        if (wcslen(original) > 64) continue;
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
static int developer_tool_process(HWND foreground) {
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
        L"kitty.exe", L"MobaXterm.exe", L"SecureCRT.exe"
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

/* Remote sessions and virtual machines: the local layout state says nothing
   about the remote machine (which may run its own KeySwitchFix), so the
   engine does not touch these windows at all. */
static int process_name_in(HWND foreground, const wchar_t *const *names, size_t count) {
    wchar_t name[MAX_PATH];
    size_t i;
    HWND focus = focused_window(foreground);
    int pass;
    for (pass = 0; pass < 2; ++pass) {
        if (!query_process_basename(pass ? focus : foreground, name, MAX_PATH)) continue;
        for (i = 0; i < count; ++i)
            if (_wcsicmp(name, names[i]) == 0) return 1;
    }
    return 0;
}

static int remote_session_process(HWND foreground) {
    static const wchar_t *const remote[] = {
        L"mstsc.exe", L"msrdc.exe", L"vmconnect.exe", L"VirtualBoxVM.exe", L"vmware.exe",
        L"vmware-vmx.exe", L"vmware-remotemks.exe", L"wfica32.exe", L"CDViewer.exe",
        L"AnyDesk.exe", L"TeamViewer.exe", L"RustDesk.exe", L"parsecd.exe", L"putty.exe",
        L"vncviewer.exe", L"tvnviewer.exe", L"mRemoteNG.exe", L"RDCMan.exe", L"Royal TS.exe"
    };
    return process_name_in(foreground, remote, sizeof(remote) / sizeof(remote[0]));
}

static int autocorrect_process(HWND foreground) {
    static const wchar_t *const office[] = {
        L"WINWORD.EXE", L"OUTLOOK.EXE", L"ONENOTE.EXE", L"POWERPNT.EXE", L"olk.exe"
    };
    return process_name_in(foreground, office, sizeof(office) / sizeof(office[0]));
}

static const FOCUS_FACTS *focus_facts(HWND foreground) {
    HWND focus = focused_window(foreground);
    DWORD now = GetTickCount();
    if (g_facts.focus == focus && g_facts.focus_serial == g_focus_serial &&
        g_facts.settings_generation == g_settings_generation && now - g_facts.computed_at < 5000u)
        return &g_facts;
    ZeroMemory(&g_facts, sizeof(g_facts));
    g_facts.focus = focus;
    g_facts.focus_serial = g_focus_serial;
    g_facts.settings_generation = g_settings_generation;
    g_facts.computed_at = now ? now : 1;
    g_facts.excluded = compute_process_excluded(foreground);
    g_facts.remote = remote_session_process(foreground);
    g_facts.developer_tool = g_facts.remote || developer_tool_process(foreground);
    g_facts.autocorrect_app = autocorrect_process(foreground);
    /* g_focus_event_protected is -1 while the accessibility query for the
       field is still running: unknown counts as protected. */
    /* The latest focus event describes the focused element; frameworks
       that raise it on a child or host window of the focus (WinUI input
       sites, Java, some Electron builds) still belong to the same
       top-level window. */
    g_facts.protected_field = edit_control_protected(foreground) ||
                              (g_focus_event_protected != 0 && g_focus_event_window &&
                               (g_focus_event_window == focus ||
                                GetAncestor(g_focus_event_window, GA_ROOT) ==
                                    GetAncestor(focus ? focus : foreground, GA_ROOT)));
    if (focus) {
        wchar_t class_name[32];
        class_name[0] = 0;
        GetClassNameW(focus, class_name, 32);
        /* EXCEL< is the formula bar: text is edited there, never replaced
           as a whole, so it is not treated as a cell. */
        g_facts.excel = _wcsnicmp(class_name, L"EXCEL", 5) == 0 && class_name[5] != L'<';
    }
    return &g_facts;
}

static int is_protected_field(HWND foreground) {
    return focus_facts(foreground)->protected_field;
}

static int spelling_skipped_process(HWND foreground) {
    return focus_facts(foreground)->developer_tool;
}

static int process_is_excluded(HWND foreground) {
    const FOCUS_FACTS *facts = focus_facts(foreground);
    return facts->excluded || facts->remote;
}

/*
 * Focus moved to another control (or another element of a web page). The
 * word being typed, the sentence model, a pending layout switch, and the
 * Undo record all described the old field: drop them.
 */
static void engine_reset_for_focus(void) {
    /* The Excel cell state is left alone: F2 or a double-click may move the
       focus to the cell editor, which is exactly the "editing" state. */
    g_word_untrusted = 0;
    clear_word();
    clear_history();
    forget_layout_request();
    g_undo.valid = 0;
    g_prev_word_count = 0;
    g_orphan_peak[0] = 0;
    g_capitalize_armed = 0;
    g_capitalize_next = 0;
}

/* 1 when MSAA reports the focused element as a protected (password) field. */
static int accessible_is_protected(HWND window, LONG object_id, LONG child_id) {
    IAccessible *accessible = NULL;
    VARIANT child;
    int protected_now = 0;
    VariantInit(&child);
    if (SUCCEEDED(AccessibleObjectFromEvent(window, (DWORD)object_id, (DWORD)child_id,
                                            &accessible, &child)) && accessible) {
        VARIANT state;
        VariantInit(&state);
        if (SUCCEEDED(accessible->lpVtbl->get_accState(accessible, child, &state)) &&
            V_VT(&state) == VT_I4 && (V_I4(&state) & STATE_SYSTEM_PROTECTED))
            protected_now = 1;
        VariantClear(&state);
        accessible->lpVtbl->Release(accessible);
    }
    VariantClear(&child);
    return protected_now;
}

static void run_focus_query(void);
static int g_focus_query_posted;
static int g_focus_query_running;

static void CALLBACK focus_event_proc(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object_id,
                                      LONG child_id, DWORD thread_id, DWORD event_time) {
    int busy = g_focus_query_running;
    (void)hook;
    (void)thread_id;
    (void)event_time;
    if (event != EVENT_OBJECT_FOCUS || !window) return;
    if (window == g_focus_event_window && object_id == g_focus_event_object &&
        child_id == g_focus_event_child) return;
    g_focus_event_window = window;
    g_focus_event_object = object_id;
    g_focus_event_child = child_id;
    ++g_focus_serial;
    if (g_engine_depth > 0) g_engine_interrupted = 1;
    engine_reset_for_focus();
    /*
     * Password detection for fields Windows' own controls do not describe:
     * browsers, Electron and WPF mark password inputs "protected" in MSAA.
     * Until the answer arrives the field counts as protected (-1). The COM
     * call pumps messages: keys typed meanwhile pass through untouched
     * (engine depth), and a focus event that arrives meanwhile is picked up
     * by the query's loop instead of being left unchecked.
     */
    g_focus_event_protected = -1;
    g_facts.computed_at = 0;
    if (busy) return;   /* the running query loops and picks this one up */
    /* Inside another operation (even inside the keyboard hook, when it
       pumps messages) a cross-process COM call could exceed the hook time
       limit: the query runs from the message loop instead. */
    if (g_engine_depth > 0) {
        if (!g_focus_query_posted && g_window) {
            g_focus_query_posted = 1;
            PostMessageW(g_window, WM_APP_FOCUS_QUERY, 0, 0);
        }
        return;
    }
    run_focus_query();
}

/* Asks MSAA whether the focused element is a password field. Keys typed
   while COM pumps messages pass through untouched (engine depth). Focus
   that keeps moving is followed a few times; after that the field stays
   "unknown", which counts as protected. */
static void run_focus_query(void) {
    static int busy;
    int round;
    if (busy || !engine_enter()) return;
    busy = 1;
    g_focus_query_running = 1;
    for (round = 0; round < 4; ++round) {
        HWND queried_window = g_focus_event_window;
        LONG queried_object = g_focus_event_object;
        LONG queried_child = g_focus_event_child;
        unsigned long serial = g_focus_serial;
        int protected_now = accessible_is_protected(queried_window, queried_object, queried_child);
        if (serial == g_focus_serial) {
            g_focus_event_protected = protected_now;
            break;
        }
    }
    busy = 0;
    g_focus_query_running = 0;
    engine_leave();
    g_facts.computed_at = 0;
    /* Focus kept moving: look again from the message loop. */
    if (g_focus_event_protected == -1 && !g_focus_query_posted && g_window) {
        g_focus_query_posted = 1;
        PostMessageW(g_window, WM_APP_FOCUS_QUERY, 0, 0);
    }
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
    /* A fix that adds a half-space or a space to a 32-key word does not fit
       the decision record; truncating it would type a wrong word. */
    if (wcslen(result.replacement) > KS_MAX_WORD) return SPELL_DECLINED;
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
    return now == g_layout_request_from && now != g_layout_request_language &&
           GetTickCount() - g_layout_request_started < 10000u;
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

static void try_smart_correction_body(void);

/* The adaptive-pause correction runs from a timer, outside the hook; it is
   guarded like the hook so a key arriving mid-operation aborts it. */
static void try_smart_correction(void) {
    if (!engine_enter()) return;
    try_smart_correction_body();
    engine_leave();
}

static void try_smart_correction_body(void) {
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
        clear_history();
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

/* What one Backspace removes is a character, not a UTF-16 unit: count code
   points (a surrogate pair is one). */
static int backspaces_for(const wchar_t *text) {
    int count = 0;
    for (; *text; ++text)
        if (!(*text >= 0xDC00 && *text <= 0xDFFF)) ++count;
    return count;
}

static int try_undo(int consume_delimiter) {
    HWND foreground = GetForegroundWindow();
    int delete_count;
    UINT restored_delimiter;
    if (!g_undo.valid || foreground != g_undo.window ||
        focused_window(foreground) != g_undo.focus ||
        GetTickCount64() - g_undo.created_at > (consume_delimiter ? 5000u : 15000u)) {
        g_undo.valid = 0;
        set_activity(L"Nothing to undo. Press Backspace immediately after a correction.");
        return 0;
    }
    /*
     * Enter and Tab have already acted: a message was sent, a cell was left,
     * focus moved to the next field. Deleting "back" from there would erase
     * text the correction never touched, and replaying them would act twice.
     * The same holds for a snippet that typed Enter or Tab itself.
     */
    if (g_undo.delimiter == VK_RETURN || g_undo.delimiter == VK_TAB ||
        wcschr(g_undo.replacement, L'\n') || wcschr(g_undo.replacement, L'\t')) {
        g_undo.valid = 0;
        set_activity(L"Undo is not available after Enter, Tab, or a multi-line snippet.");
        return 0;
    }
    /* Emoji sequences (variation selectors, joiners, surrogate pairs) are
       erased as one character by some editors and as several by others:
       the Backspace count would be a guess. */
    {
        const wchar_t *c;
        for (c = g_undo.replacement; *c; ++c) {
            if (*c == 0xFE0F || *c == 0x200D || (*c >= 0xD800 && *c <= 0xDFFF)) {
                g_undo.valid = 0;
                set_activity(L"Undo is not available for a snippet with emoji.");
                return 0;
            }
        }
    }
    delete_count = backspaces_for(g_undo.replacement) + (g_undo.delimiter ? 1 : 0);
    restored_delimiter = consume_delimiter ? 0 : g_undo.delimiter;
    if (send_replacement(foreground, delete_count, g_undo.original,
                         restored_delimiter, g_undo.delimiter_zwnj,
                         g_undo.source_language)) {
        set_activity_pair(L"Restored", g_undo.replacement, g_undo.original);
        /* The user rejected a spelling fix: that spelling is now theirs.
           It is written to disk only when the personal dictionary is on. */
        if (g_memory_active && g_last_noted[0] && wcslen(g_undo.replacement) <= KS_MAX_WORD) {
            /* The replacement was just counted as the user's word; it was
               not their word after all. */
            wchar_t noted[KS_MAX_WORD + 1];
            safe_copy(noted, KS_MAX_WORD + 1, g_undo.replacement);
            lower_first(noted);
            if (wcscmp(noted, g_last_noted) == 0) ks_memory_unobserve_word(&g_memory, noted);
            g_last_noted[0] = 0;
        }
        if (g_undo.learned) {
            /* The user rejected a learned repair: unlearn it and leave the
               word alone for the rest of the session. */
            wchar_t typo[KS_MAX_WORD + 1];
            safe_copy(typo, KS_MAX_WORD + 1, g_undo.original);
            if (g_undo.source_language == KS_LANG_ENGLISH) lower_first(typo);
            ks_memory_reject_fix(&g_memory, typo);
            ks_ignore_list_add(&g_spelling_ignore, g_undo.original);
            if (g_spelling_fixes > 0) InterlockedDecrement(&g_spelling_fixes);
        }
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

/* Developer tools, excluded processes, remote sessions and password fields
   get the raw keys: digits, punctuation and letters are never reshaped
   there. focus_facts caches per focused element and is invalidated by focus
   events (a browser's login fields share one window) and settings changes. */
static int helpers_suppressed(HWND foreground) {
    const FOCUS_FACTS *facts = focus_facts(foreground);
    return facts->excluded || facts->remote || facts->developer_tool || facts->protected_field;
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
        /* "brgds" at a sentence start was capitalised as it was typed. */
        if (!snippet && g_word_auto_capitalized && typed[0] >= L'A' && typed[0] <= L'Z') {
            wchar_t lower[KS_MAX_WORD + 1];
            wcscpy(lower, typed);
            lower[0] = (wchar_t)(lower[0] - L'A' + L'a');
            snippet = ks_snippet_find(&g_snippets, lower);
        }
        if (snippet) {
            /* One character short of the injection limit, so Undo (which
               also removes the delimiter) can always take it back. */
            wchar_t expansion[KS_MAX_PHRASE_CHARS];
            KS_DATE_INFO now;
            current_date_info(&now);
            ks_expand_macros(snippet->text, &now, expansion, KS_MAX_PHRASE_CHARS);
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


static void remember_prev_word(HWND foreground, const KS_TOKEN *tokens, int count,
                               KS_LANGUAGE language) {
    if (count < 1 || count > KS_MAX_WORD ||
        (language != KS_LANG_ENGLISH && language != KS_LANG_PERSIAN)) return;
    memcpy(g_prev_word, tokens, (size_t)count * sizeof(tokens[0]));
    g_prev_word_count = count;
    g_prev_word_language = language;
    g_prev_word_window = foreground;
    g_prev_word_serial = g_key_serial;
    g_prev_word_at = GetTickCount();
}

/*
 * At a word boundary, with the writing memory on:
 *  - the word was repaired by hand (its peak differs from what is there now):
 *    record the repair, provided the result is a real word or one the user
 *    types often — never apply anything to a word the user just fixed;
 *  - otherwise, if this exact typo has a learned repair, apply it (Undo
 *    with Backspace, which also unlearns it).
 * Returns 1 when the text was replaced.
 */
static int memory_learn_or_repair(HWND foreground, UINT boundary_key, int zwnj, int learn) {
    wchar_t typed[KS_MAX_WORD + 1];
    wchar_t key[KS_MAX_WORD + 1];
    const KS_SPELL_LEXICON *lexicon;
    const wchar_t *fix;
    int typo_known;
    if (g_word_language != KS_LANG_ENGLISH && g_word_language != KS_LANG_PERSIAN) return 0;
    lexicon = g_word_language == KS_LANG_PERSIAN ? &g_persian_spelling : &g_english_spelling;
    tokens_to_language(g_word, g_word_count, g_word_language, typed);
    if (g_word_language == KS_LANG_ENGLISH && !ordinary_case(typed)) return 0;
    safe_copy(key, KS_MAX_WORD + 1, typed);
    if (g_word_language == KS_LANG_ENGLISH) lower_first(key);
    if (!letters_only_word(key, g_word_language)) return 0;

    if (g_word_peak[0]) {
        wchar_t peak[KS_MAX_WORD + 1];
        int pair;
        if (!learn) return 0;
        safe_copy(peak, KS_MAX_WORD + 1, g_word_peak);
        if (g_word_language == KS_LANG_ENGLISH) {
            if (!ordinary_case(peak)) return 0;
            lower_first(peak);
        }
        pair = g_word_peak_complete ? ks_memory_is_fix_pair(peak, key)
                                    : ks_memory_is_fix_pair_midword(peak, key);
        if (pair && letters_only_word(peak, g_word_language) &&
            (ks_spell_known(key, lexicon) ||
             ks_memory_word_count(&g_memory, key) >= KS_MEMORY_KNOWN_COUNT)) {
            int seen = ks_memory_observe_fix(&g_memory, peak, key);
            if (seen == KS_MEMORY_FIX_COUNT)
                set_activity_pair(L"Learned your repair", peak, key);
        }
        return 0;
    }

    typo_known = ks_spell_known(key, lexicon);
    fix = ks_memory_lookup_fix(&g_memory, key, typo_known);
    /* A real word the user also types on purpose ("then" for "than"): the
       repair must have been made more often than the word was left alone. */
    if (fix && typo_known &&
        (int)ks_memory_word_count(&g_memory, key) >= ks_memory_fix_count(&g_memory, key))
        fix = NULL;
    if (!fix || is_protected_field(foreground)) return 0;
    {
        wchar_t replacement[KS_MAX_WORD + 1];
        safe_copy(replacement, KS_MAX_WORD + 1, fix);
        /* Keep a capital the user (or sentence capitalisation) typed. */
        if (g_word_language == KS_LANG_ENGLISH && typed[0] >= L'A' && typed[0] <= L'Z' &&
            replacement[0] >= L'a' && replacement[0] <= L'z')
            replacement[0] = (wchar_t)(replacement[0] - L'a' + L'A');
        if (!send_replacement(foreground, g_word_count, replacement, boundary_key, zwnj,
                              g_word_language))
            return 0;
        store_phrase_undo(foreground, g_word_language, boundary_key, zwnj, typed, replacement);
        g_undo.learned = 1;
        mark_sentence_word(foreground);
        InterlockedIncrement(&g_spelling_fixes);
        stats_count_correction(typed, 1);
        if (learn) memory_note_word(replacement, g_word_language);
        set_activity_pair(L"Your repair", typed, replacement);
    }
    return 1;
}

/* Cached in focus_facts, which follows focus events inside one window. */
static int translation_target_protected(HWND foreground) {
    return is_protected_field(foreground);
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
    /* Windows already passed the key on (the hook took too long), or keys
       slipped past meanwhile: injecting now would type it twice. */
    if (g_engine_interrupted || (g_hook_entered_at && GetTickCount() - g_hook_entered_at > 150u))
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
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

static LRESULT keyboard_hook_body(int code, WPARAM wparam, LPARAM lparam);

/* Excel's cell state (see g_cell_fresh) follows every physical key, even one
   that arrives while another operation runs. */
static void track_cell_state(DWORD key) {
    int alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    switch (key) {
        case VK_F2: g_cell_fresh = 0; g_cell_edit_mode = 1; break;
        case VK_RETURN:
            /* Alt+Enter is a line break inside the cell being edited. */
            if (alt) { g_cell_fresh = 0; g_cell_edit_mode = 1; }
            else { g_cell_fresh = 1; g_cell_edit_mode = 0; }
            break;
        case VK_TAB: case VK_ESCAPE:
            g_cell_fresh = 1; g_cell_edit_mode = 0; break;
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_PRIOR: case VK_NEXT:
            if (!g_cell_edit_mode) g_cell_fresh = 1;
            break;
        default: break;
    }
}

static LRESULT CALLBACK keyboard_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    const KBDLLHOOKSTRUCT *data = (const KBDLLHOOKSTRUCT *)lparam;
    int key_down_event;
    int foreign_key_down;
    LRESULT result;
    if (code < 0) return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    key_down_event = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) &&
                     !(data->flags & LLKHF_INJECTED);
    /* Keys typed by another program (auto-type, macro tools) change the
       text just as much as physical ones. */
    foreign_key_down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) &&
                       (data->flags & LLKHF_INJECTED) && data->dwExtraInfo != INPUT_MARKER;
    if (g_engine_depth > 0) {
        /* See g_engine_depth: never interpret a key in the middle of
           another key's operation. */
        g_last_hook_tick = GetTickCount();
        if (key_down_event || foreign_key_down) {
            g_engine_interrupted = 1;
            g_engine_key_interrupted = 1;
            ++g_key_serial;
            if (key_down_event) track_cell_state(data->vkCode);
            if (data->vkCode < 256) g_suppressed_at[data->vkCode] = 0;
        }
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
    engine_enter();
    g_key_swallowed = 0;
    g_hook_entered_at = GetTickCount();
    result = keyboard_hook_body(code, wparam, lparam);
    /* A key-down that reached the application (an auto-repeat after a
       swallowed first press, say) must get its key-up too, or the key stays
       down for the application. */
    if (key_down_event && !g_key_swallowed && data->vkCode < 256)
        g_suppressed_at[data->vkCode] = 0;
    if (key_down_event) {
        track_cell_state(data->vkCode);
        /* A word boundary ends whatever word the engine did not see begin. */
        if (data->vkCode == VK_SPACE || data->vkCode == VK_RETURN || data->vkCode == VK_TAB)
            g_word_untrusted = 0;
    }
    g_hook_entered_at = 0;
    engine_leave();
    return result;
}

static LRESULT keyboard_hook_body(int code, WPARAM wparam, LPARAM lparam) {
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
            if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) ++g_key_serial;
            clear_word();
            clear_history();
            forget_layout_request();
            g_prev_word_count = 0;
            g_orphan_peak[0] = 0;
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
    /* Every physical key-down: "the key right after X" tests use it. */
    ++g_key_serial;
    resync_modifiers();
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
        g_undo.delimiter != VK_RETURN && g_undo.delimiter != VK_TAB &&
        GetTickCount64() - g_undo.created_at <= 5000u) {
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
        else {
            /* The rest of this word is typed in another layout; the history
               can no longer say what is on screen before it. */
            clear_word();
            clear_history();
        }
    }

    if (data->vkCode == VK_BACK) {
        int had_word = g_word_count > 0 || g_overflow_count > 0;
        g_capitalize_armed = 0;
        g_capitalize_next = 0;
        g_last_key_was_digit = 0;
        cancel_smart_correction();
        g_last_word_key_at = 0;
        if (!had_word && !g_has_context && g_memory_active && g_prev_word_count > 0 &&
            g_prev_word_window == foreground && g_prev_word_serial + 1 == g_key_serial &&
            GetTickCount() - g_prev_word_at < 30000u) {
            /*
             * Backspace straight after "word␣": this key deletes the space
             * and the caret is back at the end of the word. Reopen it, so
             * the edit the user is about to make is seen as a repair of that
             * word ("عسیسم␣" ← ← ← … "عزیزم␣").
             */
            int i;
            memcpy(g_word, g_prev_word, (size_t)g_prev_word_count * sizeof(g_word[0]));
            for (i = 0; i < g_prev_word_count; ++i) g_word_visible[i] = g_prev_word_language;
            g_word_count = g_prev_word_count;
            g_overflow_count = 0;
            g_word_mixed = 0;
            g_has_context = 1;
            g_word_window = foreground;
            g_word_language = g_prev_word_language;
            g_skip_word = process_is_excluded(foreground);
            tokens_to_language(g_word, g_word_count, g_word_language, g_word_peak);
            g_word_peak_length = g_word_count;
            g_word_peak_complete = 1;
            /* It was counted at its Space; it is being edited now. */
            {
                wchar_t noted[KS_MAX_WORD + 1];
                safe_copy(noted, KS_MAX_WORD + 1, g_word_peak);
                if (g_word_language == KS_LANG_ENGLISH) lower_first(noted);
                ks_memory_unobserve_word(&g_memory, noted);
            }
            g_prev_word_count = 0;
            clear_history();
            return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
        }
        g_prev_word_count = 0;
        /* Remember the longest form of the word before the deletions. */
        if (g_memory_active && had_word && !g_overflow_count && !g_word_mixed && g_word_count >= 2 &&
            g_word_count >= g_word_peak_length) {
            tokens_to_language(g_word, g_word_count, g_word_language, g_word_peak);
            g_word_peak_length = g_word_count;
        }
        if (g_overflow_count > 0) --g_overflow_count;
        else if (g_word_count > 0) --g_word_count;
        if (!g_word_count && !g_overflow_count) {
            /* The user deleted the whole word, a rejected correction
               included: stop typing for the layout we asked for. Deleting
               a separator that follows a finished word is not that. The
               deleted word is kept for a moment: if the very next key starts
               a new word, that word is its replacement. */
            if (g_memory_active && had_word && g_word_peak_length >= 3) {
                safe_copy(g_orphan_peak, KS_MAX_WORD + 1, g_word_peak);
                g_orphan_window = foreground;
                g_orphan_serial = g_key_serial;
                g_orphan_complete = g_word_peak_complete;
            }
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
    if (mapped && !translate && !active_layout_types_one_character(data->scanCode, shift, caps)) {
        /* A dead key (US-International ' and `) or a key the active layout
           leaves empty: the screen does not get one character for it, so
           the word model would miscount. Stop tracking this word. */
        clear_word();
        clear_history();
        return CallNextHookEx(g_keyboard_hook, code, wparam, lparam);
    }
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
            g_word_skip_untrusted = !g_skip_word && g_word_untrusted;
            if (g_word_skip_untrusted) g_skip_word = 1;
            g_word_untrusted = 0;
            /* Retyping a word that was just deleted in full. */
            if (g_orphan_peak[0] && g_orphan_window == foreground &&
                g_orphan_serial + 1 == g_key_serial) {
                safe_copy(g_word_peak, KS_MAX_WORD + 1, g_orphan_peak);
                g_word_peak_length = (int)wcslen(g_word_peak);
                g_word_peak_complete = g_orphan_complete;
            }
        }
        g_orphan_peak[0] = 0;
        g_prev_word_count = 0;
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
        /* An untrusted word is not corrected, but a pending layout switch
           still types its keys in the requested alphabet. */
        if (g_skip_word)
            return deliver_key(code, wparam, lparam, data, g_word_skip_untrusted ? translate : 0,
                               shift, caps, foreground, language, 0);
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
        int memory_learning;
        int note_word = 1;
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
        /* Writing memory: learn a hand repair, or apply a learned one. */
        /* Learning (and noting words) happens only at a Space: a password
           in a field Windows cannot identify ends with Enter or Tab. */
        memory_learning = typing_helpers && g_memory_active && boundary_key == VK_SPACE;
        if (!g_skip_word && !g_overflow_count && !g_word_mixed && g_word_count > 0 &&
            typing_helpers && g_memory_active && boundary_key != VK_TAB &&
            memory_learn_or_repair(foreground, boundary_key, zwnj, memory_learning)) {
            suppress_key_up(data->vkCode);
            if (terminates_sentence) start_new_sentence(foreground);
            clear_history();
            clear_word();
            return 1;
        }
        if (!had_word && g_pending_word_valid) {
            if (boundary_key == VK_SPACE &&
                g_pending_word_window == foreground) {
                if (memory_learning && g_pending_word.count > 0) {
                    wchar_t text[KS_MAX_WORD + 1];
                    tokens_to_language(g_pending_word.tokens, g_pending_word.count,
                                       g_pending_word.visible_language, text);
                    memory_note_word(text, g_pending_word.visible_language);
                    remember_prev_word(foreground, g_pending_word.tokens, g_pending_word.count,
                                       g_pending_word.visible_language);
                }
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
                if (memory_learning) memory_note_word(decision.replacement, decision.target_language);
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
            /* If the replacement fails below, the text on screen is a
               layout mistake, not the user's word. */
            note_word = 0;
            if (apply_decision(foreground, &decision, g_word_count,
                               boundary_key, zwnj)) {
                if (memory_learning) memory_note_word(decision.replacement, decision.target_language);
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
            /* A word that is gibberish here but a word in the other layout
               is an uncorrected layout mistake: never learn it. */
            if (target_known && !active_known) note_word = 0;
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
                    if (memory_learning) memory_note_word(g_undo.replacement, g_word_language);
                    suppress_key_up(data->vkCode);
                    if (terminates_sentence) start_new_sentence(foreground);
                    clear_word();
                    return 1;
                }
                if (outcome == SPELL_DECLINED && !active_known) observe_vocabulary(foreground);
            }
        }
        if (!g_skip_word && !g_overflow_count && g_word_count > 0) {
            if (memory_learning && !g_word_mixed && note_word) {
                wchar_t text[KS_MAX_WORD + 1];
                tokens_to_language(g_word, g_word_count, g_word_language, text);
                memory_note_word(text, g_word_language);
                remember_prev_word(foreground, g_word, g_word_count, g_word_language);
            }
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
        /* Injected clicks count too (remote-control tools, accessibility
           software, pen and touch): KeySwitchFix itself never injects mouse
           input, and any click can move the caret. */
        (void)data;
        if (g_engine_depth > 0) g_engine_interrupted = 1;
        if (wparam == WM_LBUTTONDOWN) {
            DWORD now = GetTickCount();
            int double_click = g_last_click_at && now - g_last_click_at <= GetDoubleClickTime();
            g_cell_fresh = !double_click;
            g_cell_edit_mode = double_click;
            g_last_click_at = double_click ? 0 : now;
        }
        g_word_untrusted = 0;
        {
            clear_word();
            clear_history();
            forget_layout_request();
            /* The caret may be anywhere now. */
            g_prev_word_count = 0;
            g_orphan_peak[0] = 0;
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
        if (ok) {
            /* A clean-up passes through the clipboard for a moment: keep it
               out of Win+V history and cloud clipboard sync. */
            UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
            HGLOBAL marker = exclude ? GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)) : NULL;
            if (marker) {
                DWORD *zero = (DWORD *)GlobalLock(marker);
                if (zero) {
                    *zero = 0;
                    GlobalUnlock(marker);
                }
                if (!SetClipboardData(exclude, marker)) GlobalFree(marker);
            }
        }
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
static HWND g_cleanup_target;
static unsigned long g_cleanup_focus_serial;
static unsigned long g_cleanup_key_serial;

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
    g_cleanup_target = foreground;
    g_cleanup_focus_serial = g_focus_serial;
    SetTimer(g_window, ID_TIMER_CLEANUP, 30, NULL);
}

static void continue_selection_cleanup(void) {
    /* The user switched windows, moved to another field, or typed (moving
       the caret or the selection) meanwhile: never paste somewhere else. */
    if ((g_cleanup_step == 1 || g_cleanup_step == 2) &&
        (GetForegroundWindow() != g_cleanup_target || g_focus_serial != g_cleanup_focus_serial ||
         (g_cleanup_step == 2 && g_key_serial != g_cleanup_key_serial))) {
        set_activity(L"Clean-up cancelled: the window changed.");
        if (g_cleanup_step == 1) {
            finish_selection_cleanup();
            return;
        }
        g_cleanup_step = 4;   /* put the user's clipboard back */
    }
    switch (g_cleanup_step) {
        case 1:
            if (any_modifier_down()) {
                if (GetTickCount() - g_cleanup_started_at > 3000u) finish_selection_cleanup();
                return;
            }
            clipboard_snapshot_take(&g_cleanup_saved_clipboard);
            g_cleanup_sequence = GetClipboardSequenceNumber();
            send_shortcut('C');
            g_cleanup_key_serial = g_key_serial;
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

static void show_tray_menu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    HMENU language_menu = CreatePopupMenu();
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
                    IDM_PUNCTUATION, L"Persian punctuation after Persian words (؟\x200E ،\x200E ؛\x200E)");
        AppendMenuW(helpers_menu, MF_STRING | (g_settings.auto_capitalize ? MF_CHECKED : 0),
                    IDM_CAPITALIZE, L"Capitalise English sentences");
        AppendMenuW(helpers_menu, MF_STRING | (g_settings.snippets ? MF_CHECKED : 0),
                    IDM_SNIPPETS, L"Expand snippets");
        AppendMenuW(helpers_menu, MF_STRING, IDM_EDIT_SNIPPETS, L"Edit snippets…");
        AppendMenuW(helpers_menu, MF_STRING | (g_settings.vocab_it ? MF_CHECKED : 0),
                    IDM_VOCAB_IT, L"IT && computing vocabulary");
        AppendMenuW(helpers_menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(helpers_menu, MF_STRING | MF_GRAYED, IDM_CLEANUP,
                    g_cleanup_hotkey_registered ? L"Clean up selected text:  Ctrl + Win + X"
                                                : L"Clean up selected text: hotkey used by another app");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)helpers_menu, L"Typing helpers");
    }
    {
        HMENU memory_menu = CreatePopupMenu();
        AppendMenuW(memory_menu, MF_STRING | (g_settings.learn_writing ? MF_CHECKED : 0),
                    IDM_LEARN_WRITING, L"Learn my writing");
        AppendMenuW(memory_menu, MF_STRING, IDM_OPEN_MEMORY, L"Open writing memory…");
        AppendMenuW(memory_menu, MF_STRING, IDM_FORGET_MEMORY, L"Forget everything learned…");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)memory_menu, L"Writing memory");
    }
    if (g_last_typed_process[0]) {
        wchar_t label[2 * MAX_PATH + 48];
        wchar_t escaped[2 * MAX_PATH];
        size_t in = 0, out = 0;
        /* "&" in a file name would become a menu accelerator. */
        for (; g_last_typed_process[in] && out + 2 < sizeof(escaped) / sizeof(escaped[0]); ++in) {
            if (g_last_typed_process[in] == L'&') escaped[out++] = L'&';
            escaped[out++] = g_last_typed_process[in];
        }
        escaped[out] = 0;
        swprintf(label, sizeof(label) / sizeof(label[0]),
                 excluded_list_contains(g_last_typed_process)
                     ? L"Resume correction in %ls"
                     : L"Exclude %ls",
                 escaped);
        AppendMenuW(menu, MF_STRING, IDM_EXCLUDE_CURRENT, label);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    SetMenuDefaultItem(menu, IDM_OPEN, FALSE);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, x, y, 0, g_window, NULL);
    /* Required after TrackPopupMenu from a tray icon (KB Q135788), otherwise
       the menu does not dismiss when the user clicks elsewhere. */
    PostMessageW(g_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

/* ------------------------------------------------------------------------ */
/* Dashboard                                                                 */
/*                                                                           */
/* One window, authored at 96 DPI on an 840x540 client area: a header, a     */
/* status card that is always visible (state, last activity, Pause), four    */
/* pages chosen with a segmented bar (Correction, Typing, Memory & words,    */
/* Statistics), and a footer. Settings apply the moment they change; there   */
/* is no Save button to forget. Every control is a real child window with a */
/* real label, so Tab, Alt+letter, Ctrl+Tab, Esc and screen readers work.   */
/* The layout is rebuilt for the monitor's DPI (per-monitor v2), and scaled  */
/* down when the work area is too small (1366x768 at 150 %).                 */
/* ------------------------------------------------------------------------ */

#define UI_CLIENT_WIDTH 840
#define UI_CLIENT_HEIGHT 540
#define UI_HEADER_HEIGHT 68
#define UI_MARGIN 32
#define UI_CARD_LEFT UI_MARGIN
#define UI_CARD_RIGHT (UI_CLIENT_WIDTH - UI_MARGIN)
#define UI_LABEL_LEFT 56
#define UI_CONTROL_LEFT 236
#define UI_CONTROL_WIDTH 300
#define UI_SIDE_LEFT 556
#define UI_SIDE_WIDTH 232
#define UI_STATUS_TOP 80
#define UI_STATUS_BOTTOM 140
#define UI_NAV_TOP 152
#define UI_NAV_HEIGHT 32
#define UI_NAV_WIDTH 160
#define UI_NAV_GAP 8
#define UI_PAGE_TOP 192
#define UI_PAGE_BOTTOM 480
#define UI_ROW_PITCH 38
#define UI_ROW(index) (UI_PAGE_TOP + 20 + (index) * UI_ROW_PITCH)
#define UI_FOOTER_TOP 492
#define UI_BUTTON_HEIGHT 34
#define UI_TILE_WIDTH 173
#define UI_TILE_HEIGHT 56
#define UI_TILE_GAP 12
#define UI_PAGES 4

#define IDC_NAV_FIRST 150      /* 150..153 */
#define IDC_OPEN_DATA 160
#define IDC_OPEN_MEMORY 161
#define IDC_FORGET_MEMORY 162
#define IDC_MEMORY_STATE 163
#define IDC_TYPING_IN 164
#define IDC_SHORTCUTS 165

static const COLORREF UI_HEADER_TOP = RGB(22, 34, 66);
static const COLORREF UI_HEADER_BOTTOM = RGB(44, 72, 132);
static const COLORREF UI_BACKGROUND = RGB(245, 247, 251);
static const COLORREF UI_CARD_BORDER = RGB(222, 228, 238);
static const COLORREF UI_TEXT = RGB(33, 43, 64);
static const COLORREF UI_MUTED = RGB(88, 99, 122);
static const COLORREF UI_ACCENT = RGB(62, 96, 214);
static const COLORREF UI_GREEN = RGB(30, 150, 100);
static const COLORREF UI_AMBER = RGB(196, 120, 20);
static const COLORREF UI_RED = RGB(196, 60, 60);
static const COLORREF UI_GREY = RGB(128, 138, 156);
static const COLORREF UI_TILE = RGB(238, 243, 252);
static const COLORREF UI_SLATE = RGB(84, 101, 138);

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
static HWND g_learn_writing;
static HWND g_vocab_it;
static HWND g_memory_state;
static HWND g_shortcuts_label;
static HWND g_nav[UI_PAGES];
static HWND g_page_controls[UI_PAGES][32];
static int g_page_count[UI_PAGES];
static int g_page;
/* Statics drawn in the muted colour (row labels and hints). */
static int g_high_contrast;      /* Windows high-contrast theme: system colours only */
static int g_ui_ready;           /* controls exist and may be read */
static HWND g_saved_focus;       /* restored when the window is activated again */
static const wchar_t *const g_page_names[UI_PAGES] = {
    L"Correction", L"Typing", L"Memory && words", L"Statistics" };
static HWND g_row_labels[24];
static int g_row_label_count;
static HWND g_spelling_label;
static LRESULT g_dropdown_selection = -1;   /* list item when the list opened */
static UINT g_show_message;      /* a second instance asks the first to show itself */
static int g_tray_retries;
static HWND g_muted[48];
static int g_muted_count;
static HWND g_hover_button;
static int g_monitor_dpi = 96;
static int g_fit_percent = 100;
static HICON g_icon_normal;
static HICON g_icon_paused;

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

static void page_add(int page, HWND control) {
    if (page < 0 || page >= UI_PAGES || !control) return;
    if (g_page_count[page] < 32) g_page_controls[page][g_page_count[page]++] = control;
    if (page != g_page) ShowWindow(control, SW_HIDE);
}

static HWND muted(HWND control) {
    if (g_muted_count < 48) g_muted[g_muted_count++] = control;
    return control;
}

static int is_muted(HWND control) {
    int i;
    for (i = 0; i < g_muted_count; ++i)
        if (g_muted[i] == control) return 1;
    return 0;
}

/* A row label: a real STATIC with a mnemonic, created right before the
   control it names so Alt+letter and screen readers find that control. */
static void row_label(HWND window, int page, int row, const wchar_t *text) {
    HWND label = muted(create_child(L"STATIC", text, SS_LEFT, 0, UI_LABEL_LEFT, UI_ROW(row) + 4,
                                    UI_CONTROL_LEFT - UI_LABEL_LEFT - 8, 22, window, 0));
    if (g_row_label_count < 24) g_row_labels[g_row_label_count++] = label;
    page_add(page, label);
}

static int is_row_label(HWND control) {
    int i;
    for (i = 0; i < g_row_label_count; ++i)
        if (g_row_labels[i] == control) return 1;
    return 0;
}

/* Colours: the designed palette, or the system's in a high-contrast theme. */
static COLORREF ui_color(COLORREF designed, int system_index) {
    return g_high_contrast ? GetSysColor(system_index) : designed;
}

static void refresh_high_contrast(void) {
    HIGHCONTRASTW contrast;
    ZeroMemory(&contrast, sizeof(contrast));
    contrast.cbSize = sizeof(contrast);
    g_high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                      (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

static HWND checkbox(HWND window, int page, int left, int top, int width, const wchar_t *text, int id) {
    HWND control = create_child(L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, 0, left, top, width, 24,
                                window, id);
    page_add(page, control);
    return control;
}

static HWND combo(HWND window, int page, int row, int id, const wchar_t *const *items, int count) {
    HWND control = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 0,
                                UI_CONTROL_LEFT, UI_ROW(row), UI_CONTROL_WIDTH, 240, window, id);
    int i;
    for (i = 0; i < count; ++i) SendMessageW(control, CB_ADDSTRING, 0, (LPARAM)items[i]);
    page_add(page, control);
    return control;
}

static HWND button(HWND window, int page, const wchar_t *text, int left, int top, int width, int id) {
    HWND control = create_child(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, 0, left, top, width,
                                UI_BUTTON_HEIGHT, window, id);
    if (page >= 0) page_add(page, control);
    return control;
}

static HWND hint(HWND window, int page, int left, int top, int width, int height, const wchar_t *text, int id) {
    HWND control = muted(create_child(L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, left, top, width, height,
                                      window, id));
    page_add(page, control);
    return control;
}

static void show_page(int page) {
    int p;
    int i;
    if (page < 0) page = UI_PAGES - 1;
    if (page >= UI_PAGES) page = 0;
    g_page = page;
    for (p = 0; p < UI_PAGES; ++p)
        for (i = 0; i < g_page_count[p]; ++i)
            ShowWindow(g_page_controls[p][i], p == page ? SW_SHOW : SW_HIDE);
    for (p = 0; p < UI_PAGES; ++p) {
        if (!g_nav[p]) continue;
        /* One Tab stop for the tab strip (arrows move inside it), and the
           selected page is named so a screen reader announces it. */
        {
            LONG_PTR style = GetWindowLongPtrW(g_nav[p], GWL_STYLE);
            wchar_t name[64];
            SetWindowLongPtrW(g_nav[p], GWL_STYLE, p == page ? (style | WS_TABSTOP) : (style & ~(LONG_PTR)WS_TABSTOP));
            swprintf(name, 64, p == page ? L"%ls (selected page)" : L"%ls", g_page_names[p]);
            SetWindowTextW(g_nav[p], name);
        }
        InvalidateRect(g_nav[p], NULL, FALSE);
    }
    /* The page card's tiles are painted by the window itself. */
    if (g_window) InvalidateRect(g_window, NULL, FALSE);
}

/* ---- Tray icon ----------------------------------------------------------- */

/* A desaturated, lighter copy of the icon for the paused / not-working
   states, so the state is visible in the notification area itself. */
static HICON make_grey_icon(HICON source) {
    ICONINFO info;
    BITMAP bitmap;
    BITMAPINFO header;
    DWORD *pixels = NULL;
    void *bits = NULL;
    HBITMAP grey = NULL;
    HICON result = NULL;
    HDC screen;
    int count;
    int i;
    if (!source || !GetIconInfo(source, &info)) return NULL;
    if (!info.hbmColor) {
        DeleteObject(info.hbmMask);
        return NULL;
    }
    GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
    ZeroMemory(&header, sizeof(header));
    header.bmiHeader.biSize = sizeof(header.bmiHeader);
    header.bmiHeader.biWidth = bitmap.bmWidth;
    header.bmiHeader.biHeight = -bitmap.bmHeight;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    count = bitmap.bmWidth * bitmap.bmHeight;
    screen = GetDC(NULL);
    pixels = (DWORD *)HeapAlloc(GetProcessHeap(), 0, (size_t)count * sizeof(DWORD));
    if (pixels && GetDIBits(screen, info.hbmColor, 0, (UINT)bitmap.bmHeight, pixels, &header, DIB_RGB_COLORS)) {
        for (i = 0; i < count; ++i) {
            DWORD pixel = pixels[i];
            DWORD alpha = pixel >> 24;
            DWORD red = (pixel >> 16) & 0xFF;
            DWORD green = (pixel >> 8) & 0xFF;
            DWORD blue = pixel & 0xFF;
            DWORD level = (red * 30 + green * 59 + blue * 11) / 100;
            level = (level + 150) / 2;
            /* Icon colour bitmaps carry straight (not premultiplied) alpha. */
            pixels[i] = (alpha << 24) | (level << 16) | (level << 8) | level;
        }
        grey = CreateDIBSection(screen, &header, DIB_RGB_COLORS, &bits, NULL, 0);
        if (grey && bits) {
            memcpy(bits, pixels, (size_t)count * sizeof(DWORD));
            DeleteObject(info.hbmColor);
            info.hbmColor = grey;
            grey = NULL;
            result = CreateIconIndirect(&info);
        }
    }
    if (grey) DeleteObject(grey);
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    ReleaseDC(NULL, screen);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return result;
}

/* 0 active, 1 paused, 2 not working (reason in *why). */
static int app_state(const wchar_t **why) {
    const wchar_t *missing = missing_layout_name();
    if (why) *why = L"";
    if (!g_keyboard_hook) {
        if (why) *why = L"keyboard hook blocked";
        return 2;
    }
    if (missing) {
        if (why) *why = missing[0] == L'P' ? L"Persian layout missing" : L"English layout missing";
        return 2;
    }
    return g_settings.enabled ? 0 : 1;
}

static void load_tray_icons(void) {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (!g_icon_normal)
        g_icon_normal = (HICON)LoadImageW(g_instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, 0);
    if (!g_icon_normal) g_icon_normal = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
    if (!g_icon_paused) g_icon_paused = make_grey_icon(g_icon_normal);
}

/* The taskbar was recreated (Explorer restart, or a DPI change of the
   taskbar): the small-icon size may differ, so the icons are rebuilt. */
static void reload_tray_icons(void) {
    if (g_icon_paused) DestroyIcon(g_icon_paused);
    if (g_icon_normal) DestroyIcon(g_icon_normal);
    g_icon_paused = NULL;
    g_icon_normal = NULL;
    load_tray_icons();
}

static void add_tray_icon(void) {
    load_tray_icons();
    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = ID_TRAY;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = g_icon_normal;
    safe_copy(g_tray.szTip, sizeof(g_tray.szTip) / sizeof(wchar_t), L"KeySwitchFix");
    Shell_NotifyIconW(NIM_DELETE, &g_tray);   /* a stale entry would make NIM_ADD fail */
    if (!Shell_NotifyIconW(NIM_ADD, &g_tray)) {
        /* Explorer may not be ready yet at logon; try again shortly (for a
           minute at most; TaskbarCreated adds it later anyway). */
        if (++g_tray_retries <= 20) SetTimer(g_window, ID_TIMER_TRAY_RETRY, 3000, NULL);
        return;
    }
    g_tray_retries = 0;
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
    update_tray_tip();
}

static void update_tray_tip(void) {
    const wchar_t *why;
    int state = app_state(&why);
    load_tray_icons();
    g_tray.uFlags = NIF_TIP | NIF_SHOWTIP | NIF_ICON;
    g_tray.hIcon = state == 0 || !g_icon_paused ? g_icon_normal : g_icon_paused;
    safe_copy(g_tray.szTip, sizeof(g_tray.szTip) / sizeof(wchar_t),
              state == 0 ? L"KeySwitchFix — Active"
                         : state == 1 ? (g_toggle_hotkey_registered ? L"KeySwitchFix — Paused (Ctrl + Win + K resumes)"
                                                                    : L"KeySwitchFix — Paused")
                                      : L"KeySwitchFix — Not working: open it for details");
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

static void show_balloon(const wchar_t *title, const wchar_t *text) {
    /* NIF_REALTIME: a note that cannot be shown now is dropped rather than
       queued in the notification centre. */
    g_tray.uFlags = NIF_INFO | NIF_REALTIME;
    g_tray.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    safe_copy(g_tray.szInfoTitle, sizeof(g_tray.szInfoTitle) / sizeof(wchar_t), title);
    safe_copy(g_tray.szInfo, sizeof(g_tray.szInfo) / sizeof(wchar_t), text);
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

/* ---- Settings <-> controls ----------------------------------------------- */

static void update_controls_from_settings(void) {
    SendMessageW(g_sensitivity, CB_SETCURSEL, (WPARAM)g_settings.sensitivity, 0);
    SendMessageW(g_language_mode, CB_SETCURSEL, (WPARAM)g_settings.language_mode, 0);
    SendMessageW(g_spelling, CB_SETCURSEL, (WPARAM)g_settings.spelling, 0);
    EnableWindow(g_spelling, g_spelling_available);
    /* A disabled list's label would send Alt+P to the next control. */
    EnableWindow(g_spelling_label, g_spelling_available);
    EnableWindow(g_personal_dictionary, g_spelling_available);
    SendMessageW(g_personal_dictionary, BM_SETCHECK,
                 g_settings.personal_dictionary ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_startup, BM_SETCHECK, g_settings.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_digits, CB_SETCURSEL, (WPARAM)g_settings.digits, 0);
    SendMessageW(g_punctuation, BM_SETCHECK, g_settings.punctuation ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_persian_letters, BM_SETCHECK, g_settings.persian_letters ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_capitalize, BM_SETCHECK, g_settings.auto_capitalize ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_snippets_check, BM_SETCHECK, g_settings.snippets ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_learn_writing, BM_SETCHECK, g_settings.learn_writing ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_vocab_it, BM_SETCHECK, g_settings.vocab_it ? BST_CHECKED : BST_UNCHECKED, 0);
    if (GetFocus() != g_excluded) SetWindowTextW(g_excluded, g_settings.excluded);
    SetWindowTextW(g_enable_button, g_settings.enabled ? L"Pa&use" : L"Res&ume");
    InvalidateRect(g_enable_button, NULL, FALSE);
    if (g_window) {
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
    g_settings.learn_writing = SendMessageW(g_learn_writing, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_settings.vocab_it = SendMessageW(g_vocab_it, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(g_excluded, g_settings.excluded,
                   (int)(sizeof(g_settings.excluded) / sizeof(wchar_t)));
}

/* The excluded-apps box differs from the saved list. */
static int excluded_edit_changed(void) {
    wchar_t text[512];
    if (!g_ui_ready || !g_excluded) return 0;
    text[0] = 0;
    GetWindowTextW(g_excluded, text, 512);
    return wcscmp(text, g_settings.excluded) != 0;
}

/* Any control changed: the setting takes effect and is saved at once. */
static void apply_controls(void) {
    /* While the controls are being destroyed (a DPI rebuild, exit) their
       values read as zero: never save those. */
    if (!g_ui_ready) return;
    read_controls_to_settings();
    memory_apply_setting();
    clear_word();
    clear_history();
    g_undo.valid = 0;
    save_settings();
    update_tray_tip();
    set_activity(L"Setting saved.");
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
    wchar_t digits[32];
    wchar_t buffer[48];
    size_t length;
    size_t i;
    size_t out = 0;
    swprintf(digits, sizeof(digits) / sizeof(digits[0]), L"%ld", value < 0 ? 0L : value);
    length = wcslen(digits);
    /* 18,204 reads faster than 18204. */
    for (i = 0; i < length; ++i) {
        if (i > 0 && (length - i) % 3 == 0) buffer[out++] = L',';
        buffer[out++] = digits[i];
    }
    buffer[out] = 0;
    set_label_text(g_tile_values[index], buffer);
}

static void update_shortcuts_text(void) {
    wchar_t buffer[512];
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"Undo a correction:  Backspace right after it%ls\n"
             L"Pause or resume everywhere:  Ctrl + Win + K%ls\n"
             L"Clean up selected text:  Ctrl + Win + X%ls\n"
             L"Switch pages here:  Ctrl + Tab     Hide this window:  Esc",
             g_hotkey_registered ? L", or Ctrl + Win + Backspace" : L"",
             g_toggle_hotkey_registered ? L"" : L"   (unavailable: used by another app)",
             g_cleanup_hotkey_registered ? L"" : L"   (unavailable: used by another app)");
    set_label_text(g_shortcuts_label, buffer);
}

static void update_diagnostics_ui(void) {
    wchar_t buffer[512];
    wchar_t process_name[MAX_PATH];
    HWND foreground = GetForegroundWindow();
    KS_LANGUAGE language;
    const wchar_t *why;
    int state = app_state(&why);

    if (foreground == g_window) foreground = g_word_window;
    language = foreground_language(foreground);
    if (!query_process_basename(focused_window(foreground), process_name, MAX_PATH))
        safe_copy(process_name, MAX_PATH, L"no application yet");

    if (state == 2) {
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"Not working: %ls", why);
        set_label_text(g_status_label, buffer);
        /* The line below the status says what to do about it. */
        set_label_text(g_activity_label,
                       !g_keyboard_hook
                           ? L"Restart KeySwitchFix; if it persists, allow it in your security software."
                           : L"Add English and Persian in Settings > Time & language > Language & region.");
    } else {
        set_label_text(g_status_label, state == 0 ? L"Protection is active" : L"Protection is paused");
        set_label_text(g_activity_label, g_last_activity);
    }

    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"Typing in %ls with the %ls layout.%ls%ls",
             process_name, language_name(language),
             g_spelling_available ? L"" : L" Spelling data is missing from this build.",
             g_layout_requests_ignored > 0 ? L" Some apps did not switch layouts; KeySwitchFix typed those keys itself." : L"");
    set_label_text(g_layout_label, buffer);
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"Keyboard hook: %ls%ls",
             g_keyboard_hook ? L"running" : L"FAILED",
             g_hook_reinstalls ? L" (re-armed after Windows removed it)" : L"");
    set_label_text(g_hook_label, buffer);

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
        if (ks_stats_top(&g_stats, 0, &word, &count) && count >= 2)
            swprintf(top, 160, L"\nMost corrected this session: %ls\x200E (%u)", word, count);
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"%ld active days, about %ld minute%ls saved in total.%ls",
                 g_stats.days_active, minutes, minutes == 1 ? L"" : L"s", top);
        set_label_text(g_stats_label, buffer);
    }
    if (g_memory_active)
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"The memory holds %d words and %d learned repairs.",
                 g_memory.word_count, ks_memory_active_fix_count(&g_memory));
    else
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
                 L"Learning is off. Nothing you type is remembered.");
    set_label_text(g_memory_state, buffer);
    update_shortcuts_text();
    if (g_window) {
        /* Only the header badge and the status stripe depend on the state. */
        RECT header = {0, 0, scale(UI_CLIENT_WIDTH), scale(UI_HEADER_HEIGHT)};
        RECT stripe = {scale(UI_CARD_LEFT), scale(UI_STATUS_TOP), scale(UI_CARD_LEFT + 24),
                       scale(UI_STATUS_BOTTOM)};
        InvalidateRect(g_window, &header, FALSE);
        InvalidateRect(g_window, &stripe, FALSE);
    }
}

/* ---- Building the window ------------------------------------------------- */

static void create_tile(HWND window, int index, int left, int top, const wchar_t *caption) {
    g_tile_values[index] = create_child(L"STATIC", L"0", SS_ENDELLIPSIS | SS_NOPREFIX, 0,
                                        left + 14, top + 6, UI_TILE_WIDTH - 28, 28, window,
                                        IDC_TILE_VALUE + index);
    g_tile_captions[index] = create_child(L"STATIC", caption, SS_ENDELLIPSIS | SS_NOPREFIX, 0,
                                          left + 14, top + 34, UI_TILE_WIDTH - 28, 18, window,
                                          IDC_TILE_CAPTION + index);
    page_add(3, g_tile_values[index]);
    page_add(3, g_tile_captions[index]);
}

static LRESULT CALLBACK button_subclass_proc(HWND control, UINT message, WPARAM wparam, LPARAM lparam,
                                             UINT_PTR id, DWORD_PTR data) {
    (void)id;
    (void)data;
    if (message == WM_MOUSEMOVE && g_hover_button != control) {
        TRACKMOUSEEVENT track;
        HWND previous = g_hover_button;
        g_hover_button = control;
        ZeroMemory(&track, sizeof(track));
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = control;
        TrackMouseEvent(&track);
        if (previous) InvalidateRect(previous, NULL, FALSE);
        InvalidateRect(control, NULL, FALSE);
    } else if (message == WM_MOUSELEAVE && g_hover_button == control) {
        g_hover_button = NULL;
        InvalidateRect(control, NULL, FALSE);
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

static void subclass_buttons(HWND window) {
    HWND child = GetWindow(window, GW_CHILD);
    while (child) {
        wchar_t class_name[16];
        class_name[0] = 0;
        GetClassNameW(child, class_name, 16);
        if (_wcsicmp(class_name, L"Button") == 0 &&
            (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW)
            SetWindowSubclass(child, button_subclass_proc, 1, 0);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void create_ui(HWND window) {
    static const wchar_t *const sensitivity[] = {
        L"Conservative", L"Balanced (recommended)", L"Sensitive" };
    static const wchar_t *const writing[] = {
        L"Auto — sentence context", L"Prefer Persian for collisions", L"Prefer English for collisions" };
    static const wchar_t *const spelling[] = {
        L"Off", L"Conservative", L"Balanced (recommended)", L"Aggressive" };
    static const wchar_t *const digits[] = {
        L"As the layout types them", L"Follow the layout (۱۲۳ / 123)", L"Always Persian ۱۲۳",
        L"Always English 123" };
    const wchar_t *const *pages = g_page_names;
    int i;
    int card_width = UI_CARD_RIGHT - UI_CARD_LEFT;

    ZeroMemory(g_page_count, sizeof(g_page_count));
    g_muted_count = 0;
    g_row_label_count = 0;
    refresh_high_contrast();
    g_hover_button = NULL;
    g_font_regular = create_ui_font(15, FW_NORMAL);
    g_font_medium = create_ui_font(15, FW_SEMIBOLD);
    g_font_title = create_ui_font(26, FW_BOLD);
    g_font_status = create_ui_font(20, FW_SEMIBOLD);
    g_font_tile = create_ui_font(24, FW_BOLD);
    g_font_small = create_ui_font(13, FW_NORMAL);

    /* Status card: always visible. */
    {
        int status_width = UI_CARD_RIGHT - 24 - 132 - 16 - UI_LABEL_LEFT;
        g_status_label = create_child(L"STATIC", L"", SS_ENDELLIPSIS | SS_NOPREFIX, 0,
                                      UI_LABEL_LEFT, UI_STATUS_TOP + 8, status_width, 28, window,
                                      IDC_STATUS_LABEL);
        g_activity_label = muted(create_child(L"STATIC", L"", SS_ENDELLIPSIS | SS_NOPREFIX, 0,
                                              UI_LABEL_LEFT, UI_STATUS_TOP + 36, status_width, 18, window,
                                              IDC_ACTIVITY_LABEL));
    }
    g_enable_button = button(window, -1, L"Pa&use", UI_CARD_RIGHT - 24 - 132, UI_STATUS_TOP + 13, 132,
                             IDC_ENABLE);

    /* Page selector. */
    for (i = 0; i < UI_PAGES; ++i)
        g_nav[i] = button(window, -1, pages[i], UI_CARD_LEFT + i * (UI_NAV_WIDTH + UI_NAV_GAP), UI_NAV_TOP,
                          UI_NAV_WIDTH, IDC_NAV_FIRST + i);

    /* Page 0: Correction. */
    /* Creation order is the Tab order: the three lists, then the two
       switches beside them, then the excluded-apps box. */
    row_label(window, 0, 0, L"&Sensitivity");
    g_sensitivity = combo(window, 0, 0, IDC_SENSITIVITY, sensitivity, 3);
    row_label(window, 0, 1, L"&Writing language");
    g_language_mode = combo(window, 0, 1, IDC_LANGUAGE_MODE, writing, 3);
    row_label(window, 0, 2, L"S&pelling");
    g_spelling_label = g_row_labels[g_row_label_count - 1];
    g_spelling = combo(window, 0, 2, IDC_SPELLING, spelling, 4);
    g_startup = checkbox(window, 0, UI_SIDE_LEFT, UI_ROW(0) + 2, UI_SIDE_WIDTH, L"Start with Wi&ndows",
                         IDC_APP_STARTUP);
    g_personal_dictionary = checkbox(window, 0, UI_SIDE_LEFT, UI_ROW(2) + 2, UI_SIDE_WIDTH,
                                     L"&Remember undone words", IDC_PERSONAL_DICTIONARY);
    row_label(window, 0, 3, L"E&xcluded apps");
    g_excluded = create_child(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE,
                              UI_CONTROL_LEFT, UI_ROW(3), UI_CARD_RIGHT - 24 - UI_CONTROL_LEFT, 26, window,
                              IDC_EXCLUDED);
    page_add(0, g_excluded);
    /* The setting holds 511 characters: stop there rather than cut silently. */
    SendMessageW(g_excluded, EM_SETLIMITTEXT, 511, 0);
    hint(window, 0, UI_CONTROL_LEFT, UI_ROW(4) - 4, UI_CARD_RIGHT - 24 - UI_CONTROL_LEFT, 40,
         L"Program file names separated by commas, for example KeePass.exe. The tray menu can "
         L"exclude the app you last typed in with one click.", 0);
    hint(window, 0, UI_LABEL_LEFT, UI_ROW(5) + 12, card_width - 48, 60,
         L"Wrong-layout words are repaired as you type; ambiguous ones when the word ends. "
         L"One Backspace right after a correction restores what you typed.", 0);

    /* Page 1: Typing. */
    row_label(window, 1, 0, L"&Digits");
    g_digits = combo(window, 1, 0, IDC_DIGITS, digits, 4);
    row_label(window, 1, 1, L"Punctuation");
    g_punctuation = checkbox(window, 1, UI_CONTROL_LEFT, UI_ROW(1) + 2, 520,
                             L"&Persian ؟\x200E ،\x200E ؛\x200E after Persian words, ? , ; after English", IDC_PUNCTUATION);
    row_label(window, 1, 2, L"Letters");
    g_persian_letters = checkbox(window, 1, UI_CONTROL_LEFT, UI_ROW(2) + 2, 520,
                                 L"&Type Arabic ي ك as Persian ی ک", IDC_PERSIAN_LETTERS);
    row_label(window, 1, 3, L"Capitals");
    g_capitalize = checkbox(window, 1, UI_CONTROL_LEFT, UI_ROW(3) + 2, 520,
                            L"&Capitalise English sentences and the lone i", IDC_CAPITALIZE);
    row_label(window, 1, 4, L"Snippets");
    g_snippets_check = checkbox(window, 1, UI_CONTROL_LEFT, UI_ROW(4) + 2, 300,
                                L"E&xpand snippet shortcuts", IDC_SNIPPETS);
    button(window, 1, L"Edit s&nippets…", UI_SIDE_LEFT, UI_ROW(4) - 4, 160, IDC_EDIT_SNIPPETS);
    hint(window, 1, UI_LABEL_LEFT, UI_ROW(5) + 12, card_width - 48, 60,
         L"Select text in any application and press Ctrl + Win + X to clean it up: Persian "
         L"letters, digits and punctuation. None of these helpers act in code editors, "
         L"terminals, remote desktops or password fields.", 0);

    /* Page 2: Memory & words. */
    row_label(window, 2, 0, L"Writing memory");
    g_learn_writing = checkbox(window, 2, UI_CONTROL_LEFT, UI_ROW(0) + 2, 520,
                               L"&Learn my writing (kept only on this PC)", IDC_LEARN_WRITING);
    hint(window, 2, UI_CONTROL_LEFT, UI_ROW(1) - 2, UI_CARD_RIGHT - 24 - UI_CONTROL_LEFT, 42,
         L"Learns the repairs you make by hand (fix a typo twice and it is fixed for you) and "
         L"the words you use most. Only words that end with Space; never in password fields.", 0);
    g_memory_state = hint(window, 2, UI_CONTROL_LEFT, UI_ROW(2) + 10, UI_CARD_RIGHT - 24 - UI_CONTROL_LEFT,
                          20, L"", IDC_MEMORY_STATE);
    button(window, 2, L"&Open memory file…", UI_CONTROL_LEFT, UI_ROW(3) + 2, 180, IDC_OPEN_MEMORY);
    button(window, 2, L"Forg&et everything…", UI_CONTROL_LEFT + 192, UI_ROW(3) + 2, 180, IDC_FORGET_MEMORY);
    /* Half a row below the buttons, so they do not crowd the switch. */
    row_label(window, 2, 4, L"Vocabulary");
    SetWindowPos(g_row_labels[g_row_label_count - 1], NULL, scale(UI_LABEL_LEFT), scale(UI_ROW(4) + 18), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    g_vocab_it = checkbox(window, 2, UI_CONTROL_LEFT, UI_ROW(4) + 16, 520,
                          L"&IT && computing terms (about 1,400 English and Persian words)", IDC_VOCAB_IT);

    /* Page 3: Statistics. */
    create_tile(window, 0, UI_LABEL_LEFT, UI_ROW(0) - 4, L"layout fixes today");
    create_tile(window, 1, UI_LABEL_LEFT + (UI_TILE_WIDTH + UI_TILE_GAP), UI_ROW(0) - 4, L"spelling fixes today");
    create_tile(window, 2, UI_LABEL_LEFT + 2 * (UI_TILE_WIDTH + UI_TILE_GAP), UI_ROW(0) - 4, L"fixes, all time");
    create_tile(window, 3, UI_LABEL_LEFT + 3 * (UI_TILE_WIDTH + UI_TILE_GAP), UI_ROW(0) - 4, L"keys today");
    /* Heights allow two lines of the regular font (stats), three of the
       small one (layout diagnostics) and four for the shortcut list, with
       room for the rounding of larger scales (a 13 px line is 17 px high,
       20 px at 150 % is 27). */
    g_stats_label = create_child(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, UI_LABEL_LEFT, UI_ROW(1) + 26,
                                 card_width - 48, 44, window, IDC_STATS_LABEL);
    page_add(3, g_stats_label);
    g_layout_label = muted(create_child(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, UI_LABEL_LEFT, UI_ROW(2) + 36,
                                        card_width - 48, 56, window, IDC_LAYOUT_LABEL));
    page_add(3, g_layout_label);
    g_hook_label = muted(create_child(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, UI_LABEL_LEFT, UI_ROW(4) + 18,
                                      card_width - 48, 18, window, IDC_HOOK_LABEL));
    page_add(3, g_hook_label);
    g_shortcuts_label = hint(window, 3, UI_LABEL_LEFT, UI_ROW(5), card_width - 48, 76, L"", IDC_SHORTCUTS);

    /* Footer. */
    button(window, -1, L"&Hide to tray", UI_MARGIN, UI_FOOTER_TOP, 150, IDC_HIDE);
    button(window, -1, L"Open data &folder", UI_MARGIN + 160, UI_FOOTER_TOP, 170, IDC_OPEN_DATA);

    {
        HWND child = GetWindow(window, GW_CHILD);
        while (child) {
            SendMessageW(child, WM_SETFONT, (WPARAM)g_font_regular, TRUE);
            child = GetWindow(child, GW_HWNDNEXT);
        }
    }
    SendMessageW(g_status_label, WM_SETFONT, (WPARAM)g_font_status, TRUE);
    SendMessageW(g_activity_label, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    /* Hints and diagnostics use the small font; row labels keep the
       regular one so they line up with the controls they name. */
    for (i = 0; i < g_muted_count; ++i)
        if (!is_row_label(g_muted[i]))
            SendMessageW(g_muted[i], WM_SETFONT, (WPARAM)g_font_small, TRUE);
    for (i = 0; i < UI_TILE_COUNT; ++i) {
        SendMessageW(g_tile_values[i], WM_SETFONT, (WPARAM)g_font_tile, TRUE);
        SendMessageW(g_tile_captions[i], WM_SETFONT, (WPARAM)g_font_small, TRUE);
    }
    g_brush_white = CreateSolidBrush(RGB(255, 255, 255));
    g_brush_background = CreateSolidBrush(UI_BACKGROUND);
    g_brush_tile = CreateSolidBrush(UI_TILE);
    subclass_buttons(window);
    g_ui_ready = 1;
    show_page(g_page);
    update_controls_from_settings();
}

static void destroy_ui(HWND window) {
    HWND child;
    /* Destroying a focused edit sends EN_KILLFOCUS; by then other controls
       are gone. Take the focus away first and stop reading controls. */
    if (g_ui_ready && GetFocus() == g_excluded && excluded_edit_changed()) apply_controls();
    g_ui_ready = 0;
    g_saved_focus = NULL;
    if (IsChild(window, GetFocus())) SetFocus(window);
    while ((child = GetWindow(window, GW_CHILD)) != NULL) DestroyWindow(child);
    DeleteObject(g_font_regular);
    DeleteObject(g_font_medium);
    DeleteObject(g_font_title);
    DeleteObject(g_font_status);
    DeleteObject(g_font_tile);
    DeleteObject(g_font_small);
    DeleteObject(g_brush_white);
    DeleteObject(g_brush_background);
    DeleteObject(g_brush_tile);
}

/* The DPI of the monitor a window is on (Windows 10 1607+), else the
   system DPI. */
static int window_dpi(HWND window) {
    typedef UINT (WINAPI *GET_DPI_FOR_WINDOW)(HWND);
    static GET_DPI_FOR_WINDOW get_dpi_for_window;
    static int resolved;
    if (!resolved) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        resolved = 1;
        if (user32) get_dpi_for_window = (GET_DPI_FOR_WINDOW)(void *)GetProcAddress(user32, "GetDpiForWindow");
    }
    if (get_dpi_for_window && window) {
        UINT dpi = get_dpi_for_window(window);
        if (dpi >= 72) return (int)dpi;
    }
    {
        HDC screen = GetDC(NULL);
        int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
        if (screen) ReleaseDC(NULL, screen);
        return dpi >= 72 ? dpi : 96;
    }
}

/* Frame size for a given DPI: under per-monitor awareness the caption and
   borders follow the monitor, not the system DPI (Windows 10 1607+). */
static void adjust_for_dpi(RECT *frame, DWORD style, int dpi) {
    typedef BOOL (WINAPI *ADJUST_FOR_DPI)(RECT *, DWORD, BOOL, DWORD, UINT);
    static ADJUST_FOR_DPI adjust;
    static int resolved;
    if (!resolved) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        resolved = 1;
        if (user32) adjust = (ADJUST_FOR_DPI)(void *)GetProcAddress(user32, "AdjustWindowRectExForDpi");
    }
    if (adjust && adjust(frame, style, FALSE, WS_EX_APPWINDOW, (UINT)dpi)) return;
    AdjustWindowRectEx(frame, style, FALSE, WS_EX_APPWINDOW);
}

/* Scale for `monitor_dpi`, reduced when the window would not fit the work
   area (the footer must always be reachable). */
static void choose_scale(int monitor_dpi, const RECT *work_area) {
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    g_monitor_dpi = monitor_dpi;
    g_fit_percent = 100;
    for (;;) {
        RECT probe;
        g_dpi = MulDiv(g_monitor_dpi, g_fit_percent, 100);
        probe.left = 0;
        probe.top = 0;
        probe.right = scale(UI_CLIENT_WIDTH);
        probe.bottom = scale(UI_CLIENT_HEIGHT);
        adjust_for_dpi(&probe, style, g_monitor_dpi);
        if (!work_area || g_fit_percent <= 60 ||
            (probe.bottom - probe.top <= work_area->bottom - work_area->top &&
             probe.right - probe.left <= work_area->right - work_area->left))
            break;
        g_fit_percent -= 5;
    }
}

static void work_area_for(HWND window, RECT *area) {
    MONITORINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (window && GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        *area = info.rcWork;
        return;
    }
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, area, 0)) SetRect(area, 0, 0, 1280, 720);
}

/* Moved to a monitor with another DPI (or created on one): rebuild every
   control at the new size. */
static void rebuild_for_dpi(HWND window, int monitor_dpi, const RECT *suggested) {
    RECT area;
    RECT frame;
    HWND focus_page = NULL;
    int focus_id = 0;
    {
        HWND focus = GetFocus();
        if (!focus || !IsChild(window, focus)) focus = g_saved_focus;
        if (focus && IsChild(window, focus)) focus_id = GetDlgCtrlID(focus);
    }
    work_area_for(window, &area);
    choose_scale(monitor_dpi, &area);
    destroy_ui(window);
    create_ui(window);
    /* The same control keeps (or will get back) the focus. */
    if (focus_id) g_saved_focus = GetDlgItem(window, focus_id);
    SetRect(&frame, 0, 0, scale(UI_CLIENT_WIDTH), scale(UI_CLIENT_HEIGHT));
    adjust_for_dpi(&frame, (DWORD)GetWindowLongPtrW(window, GWL_STYLE), g_monitor_dpi);
    if (suggested)
        SetWindowPos(window, NULL, suggested->left, suggested->top, frame.right - frame.left,
                     frame.bottom - frame.top, SWP_NOZORDER | SWP_NOACTIVATE);
    else
        SetWindowPos(window, NULL, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
    focus_page = g_saved_focus ? g_saved_focus : g_nav[g_page];
    if (IsWindowVisible(window) && GetForegroundWindow() == window && focus_page) SetFocus(focus_page);
    update_diagnostics_ui();
    InvalidateRect(window, NULL, TRUE);
}

static void center_in_work_area(HWND window) {
    RECT area;
    RECT frame;
    work_area_for(window, &area);
    GetWindowRect(window, &frame);
    SetWindowPos(window, NULL,
                 area.left + ((area.right - area.left) - (frame.right - frame.left)) / 2,
                 area.top + ((area.bottom - area.top) - (frame.bottom - frame.top)) / 2,
                 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void show_main_window_by(int keyboard) {
    static int placed;
    /* Centred the first time, and again if its monitor was unplugged. */
    if (!placed || !MonitorFromWindow(g_window, MONITOR_DEFAULTTONULL)) {
        center_in_work_area(g_window);
        placed = 1;
    }
    update_controls_from_settings();
    ShowWindow(g_window, SW_SHOWNORMAL);
    SetForegroundWindow(g_window);
    /* Opened with the mouse: no focus rectangle or underlines until the
       keyboard is used (the dialog manager shows them then). */
    SendMessageW(g_window, WM_CHANGEUISTATE,
                 MAKEWPARAM(keyboard ? UIS_CLEAR : UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
    if (g_saved_focus && IsChild(g_window, g_saved_focus) && IsWindowVisible(g_saved_focus))
        SetFocus(g_saved_focus);
    else if (g_nav[g_page]) SetFocus(g_nav[g_page]);
    /* WM_SHOWWINDOW is not delivered for every restore path; make sure the
       diagnostics refresh is running whenever the dashboard is on screen. */
    update_diagnostics_ui();
    SetTimer(g_window, ID_TIMER_STATUS, 1000, NULL);
}

static void show_main_window(void) {
    show_main_window_by(0);
}

/* ---- Painting ------------------------------------------------------------ */

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
    fill_round_rect(dc, left, top, right, bottom, 16, ui_color(RGB(255, 255, 255), COLOR_WINDOW),
                    ui_color(UI_CARD_BORDER, COLOR_WINDOWTEXT));
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
    const wchar_t *why;
    int state = app_state(&why);
    if (g_high_contrast) {
        RECT band = {0, 0, client->right, height};
        FillRect(dc, &band, GetSysColorBrush(COLOR_WINDOW));
        SelectObject(dc, g_font_title);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        draw_text(dc, UI_MARGIN, 10, L"KeySwitchFix");
        SelectObject(dc, g_font_small);
        draw_text(dc, UI_MARGIN + 2, 44, L"v" APP_VERSION);
        SelectObject(dc, g_font_medium);
        draw_text_right(dc, UI_CARD_RIGHT, 24, state == 0 ? L"Active" : state == 1 ? L"Paused" : L"Problem");
        return;
    }
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
    draw_text(dc, UI_MARGIN, 10, L"KeySwitchFix");
    SelectObject(dc, g_font_small);
    SetTextColor(dc, RGB(196, 208, 236));
    draw_text(dc, UI_MARGIN + 2, 44, L"Persian ↔ English layout repair, spelling and typing helpers  •  v" APP_VERSION);
    {
        const wchar_t *text = state == 0 ? L"Active" : state == 1 ? L"Paused" : L"Problem";
        COLORREF dot = state == 0 ? RGB(80, 220, 150) : state == 1 ? UI_GREY : RGB(255, 170, 60);
        HBRUSH brush;
        HPEN pen;
        HGDIOBJ old_brush;
        HGDIOBJ old_pen;
        fill_round_rect(dc, UI_CARD_RIGHT - 118, 19, UI_CARD_RIGHT, 49, 15, RGB(52, 82, 146), RGB(70, 104, 174));
        brush = CreateSolidBrush(dot);
        pen = CreatePen(PS_SOLID, 1, dot);
        old_brush = SelectObject(dc, brush);
        old_pen = SelectObject(dc, pen);
        Ellipse(dc, scale(UI_CARD_RIGHT - 102), scale(28), scale(UI_CARD_RIGHT - 90), scale(40));
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);
        SelectObject(dc, g_font_medium);
        SetTextColor(dc, RGB(255, 255, 255));
        draw_text(dc, UI_CARD_RIGHT - 82, 24, text);
    }
}

static void paint_main_window(HWND window) {
    PAINTSTRUCT paint;
    HDC target = BeginPaint(window, &paint);
    RECT client;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    HGDIOBJ old_font;
    int i;
    GetClientRect(window, &client);
    /* Double-buffered: the whole frame is composed off screen, then copied. */
    dc = CreateCompatibleDC(target);
    bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    old_bitmap = SelectObject(dc, bitmap);
    SetBkMode(dc, TRANSPARENT);
    FillRect(dc, &client, g_high_contrast ? GetSysColorBrush(COLOR_WINDOW) : g_brush_background);
    old_font = SelectObject(dc, g_font_title);
    draw_header(dc, &client);
    draw_card(dc, UI_CARD_LEFT, UI_STATUS_TOP, UI_CARD_RIGHT, UI_STATUS_BOTTOM);
    {
        /* State stripe on the status card. */
        const wchar_t *why;
        int state = app_state(&why);
        fill_round_rect(dc, UI_CARD_LEFT + 10, UI_STATUS_TOP + 14, UI_CARD_LEFT + 16, UI_STATUS_BOTTOM - 14, 6,
                        state == 0 ? UI_GREEN : state == 1 ? UI_GREY : UI_AMBER,
                        state == 0 ? UI_GREEN : state == 1 ? UI_GREY : UI_AMBER);
    }
    draw_card(dc, UI_CARD_LEFT, UI_PAGE_TOP, UI_CARD_RIGHT, UI_PAGE_BOTTOM);
    if (g_page == 3 && !g_high_contrast) {
        for (i = 0; i < UI_TILE_COUNT; ++i) {
            int left = UI_LABEL_LEFT + i * (UI_TILE_WIDTH + UI_TILE_GAP);
            fill_round_rect(dc, left, UI_ROW(0) - 4, left + UI_TILE_WIDTH, UI_ROW(0) - 4 + UI_TILE_HEIGHT,
                            12, UI_TILE, UI_TILE);
        }
    }
    SelectObject(dc, g_font_small);
    SetTextColor(dc, ui_color(UI_MUTED, COLOR_WINDOWTEXT));
    draw_text_right(dc, UI_CARD_RIGHT, UI_FOOTER_TOP + 9, L"Offline  •  nothing leaves this PC");
    BitBlt(target, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right - paint.rcPaint.left,
           paint.rcPaint.bottom - paint.rcPaint.top, dc, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

static COLORREF shade(COLORREF color, int percent) {
    return RGB(GetRValue(color) * percent / 100, GetGValue(color) * percent / 100,
               GetBValue(color) * percent / 100);
}

static COLORREF tint(COLORREF color, int percent) {
    return RGB(GetRValue(color) + (255 - GetRValue(color)) * percent / 100,
               GetGValue(color) + (255 - GetGValue(color)) * percent / 100,
               GetBValue(color) + (255 - GetBValue(color)) * percent / 100);
}

static void draw_button(DRAWITEMSTRUCT *item) {
    wchar_t text[64];
    RECT rectangle = item->rcItem;
    HBRUSH brush;
    HPEN pen;
    HGDIOBJ old_brush;
    HGDIOBJ old_pen;
    HGDIOBJ old_font;
    COLORREF fill;
    COLORREF border;
    COLORREF ink;
    COLORREF behind = RGB(255, 255, 255);
    UINT id = item->CtlID;
    int hover = item->hwndItem == g_hover_button;
    int pressed = (item->itemState & ODS_SELECTED) != 0;
    int disabled = (item->itemState & ODS_DISABLED) != 0;

    if (id >= IDC_NAV_FIRST && id < IDC_NAV_FIRST + UI_PAGES) {
        int selected = (int)(id - IDC_NAV_FIRST) == g_page;
        behind = UI_BACKGROUND;
        fill = selected ? UI_ACCENT : hover ? RGB(226, 233, 247) : RGB(234, 239, 248);
        border = selected ? UI_ACCENT : UI_CARD_BORDER;
        ink = selected ? RGB(255, 255, 255) : UI_TEXT;
    } else if (id == IDC_ENABLE) {
        /* Darker green than the stripe: white text needs 4.5:1 contrast. */
        fill = g_settings.enabled ? UI_SLATE : shade(UI_GREEN, 78);
        if (hover) fill = shade(fill, 90);
        border = fill;
        ink = RGB(255, 255, 255);
    } else if (id == IDC_HIDE || id == IDC_OPEN_DATA) {
        behind = UI_BACKGROUND;
        fill = hover ? RGB(233, 238, 248) : RGB(255, 255, 255);
        border = UI_CARD_BORDER;
        ink = UI_TEXT;
    } else if (id == IDC_FORGET_MEMORY) {
        fill = hover ? tint(UI_RED, 88) : RGB(255, 255, 255);
        border = tint(UI_RED, 40);
        ink = UI_RED;
    } else {
        fill = hover ? tint(UI_ACCENT, 88) : RGB(255, 255, 255);
        border = tint(UI_ACCENT, 45);
        ink = UI_ACCENT;
    }
    if (pressed) fill = shade(fill, 88);
    if (disabled) {
        fill = RGB(240, 242, 246);
        border = UI_CARD_BORDER;
        ink = UI_GREY;
    }
    if (g_high_contrast) {
        int selected = id >= IDC_NAV_FIRST && id < IDC_NAV_FIRST + UI_PAGES &&
                       (int)(id - IDC_NAV_FIRST) == g_page;
        behind = GetSysColor(COLOR_WINDOW);
        fill = GetSysColor(selected || hover ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
        border = GetSysColor(COLOR_BTNTEXT);
        ink = GetSysColor(disabled ? COLOR_GRAYTEXT : selected || hover ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT);
    }
    /* The rounded corners show what is behind the button. */
    brush = CreateSolidBrush(behind);
    FillRect(item->hDC, &rectangle, brush);
    DeleteObject(brush);
    brush = CreateSolidBrush(fill);
    pen = CreatePen(PS_SOLID, 1, border);
    old_brush = SelectObject(item->hDC, brush);
    old_pen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom,
              scale(12), scale(12));
    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
    if (id >= IDC_NAV_FIRST && id < IDC_NAV_FIRST + UI_PAGES)
        safe_copy(text, 64, g_page_names[id - IDC_NAV_FIRST]);   /* without "(selected page)" */
    else
        GetWindowTextW(item->hwndItem, text, 64);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, ink);
    old_font = SelectObject(item->hDC, g_font_medium);
    DrawTextW(item->hDC, text, -1, &rectangle,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | ((item->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
    SelectObject(item->hDC, old_font);
    if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rectangle;
        InflateRect(&focus, -scale(4), -scale(4));
        DrawFocusRect(item->hDC, &focus);
    }
}

static void toggle_enabled(int announce) {
    g_settings.enabled = !g_settings.enabled;
    clear_word();
    clear_history();
    g_undo.valid = 0;
    save_settings();
    update_controls_from_settings();
    set_activity(g_settings.enabled ? L"Automatic correction resumed."
                                    : L"Automatic correction paused.");
    if (announce && !IsWindowVisible(g_window))
        show_balloon(g_settings.enabled ? L"KeySwitchFix resumed" : L"KeySwitchFix paused",
                     g_settings.enabled ? L"Correction is active again."
                                        : L"Nothing is corrected until you press Ctrl + Win + K again.");
    if (g_window) InvalidateRect(g_window, NULL, FALSE);
}

static void open_data_folder(void) {
    ShellExecuteW(NULL, L"open", g_data_directory, NULL, NULL, SW_SHOWNORMAL);
}

/* Keyboard navigation for the dashboard: Tab, Alt+letter, Enter and Esc via
   IsDialogMessage, plus Ctrl+Tab / Ctrl+Shift+Tab to change pages. */
static int dashboard_message(MSG *message) {
    if (!g_window || !IsWindowVisible(g_window)) return 0;
    if (message->hwnd != g_window && !IsChild(g_window, message->hwnd)) return 0;
    if (message->message == WM_KEYDOWN && message->wParam == VK_TAB &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        show_page(g_page + ((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1));
        SendMessageW(g_window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
        SetFocus(g_nav[g_page]);
        return 1;
    }
    /* Left and Right move between the page tabs, like a tab strip. */
    if (message->message == WM_KEYDOWN && (message->wParam == VK_LEFT || message->wParam == VK_RIGHT)) {
        int i;
        for (i = 0; i < UI_PAGES; ++i) {
            if (message->hwnd == g_nav[i]) {
                show_page(i + (message->wParam == VK_LEFT ? -1 : 1));
                SendMessageW(g_window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
                SetFocus(g_nav[g_page]);
                return 1;
            }
        }
    }
    return IsDialogMessageW(g_window, message);
}

/* Hide to the tray. The first time on a fresh install, say where it went. */
static void hide_dashboard(HWND window) {
    static int told;
    if (GetFocus() == g_excluded && excluded_edit_changed()) apply_controls();
    ShowWindow(window, SW_HIDE);
    if (g_first_run && !told) {
        told = 1;
        show_balloon(L"KeySwitchFix keeps running",
                     L"It lives in the tray. Click the icon to open settings.");
    }
}

static LRESULT CALLBACK main_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_taskbar_created_message && message == g_taskbar_created_message) {
        g_tray_retries = 0;
        reload_tray_icons();
        add_tray_icon();
        return 0;
    }
    if (g_show_message && message == g_show_message) {
        show_main_window();
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
        case WM_DPICHANGED:
            rebuild_for_dpi(window, HIWORD(wparam), (const RECT *)lparam);
            return 0;
        case WM_SHOWWINDOW:
            if (wparam) {
                update_diagnostics_ui();
                SetTimer(window, ID_TIMER_STATUS, 1000, NULL);
            } else {
                /* An edit in progress (the excluded-apps box) is kept. */
                if (GetFocus() == g_excluded && excluded_edit_changed()) apply_controls();
                KillTimer(window, ID_TIMER_STATUS);
            }
            break;
        case WM_PAINT:
            paint_main_window(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            if (g_high_contrast) {
                SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor((HDC)wparam, GetSysColor(COLOR_WINDOW));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            if (message == WM_CTLCOLOREDIT) {
                SetTextColor((HDC)wparam, UI_TEXT);
                SetBkColor((HDC)wparam, RGB(255, 255, 255));
                return (LRESULT)g_brush_white;
            }
            if (message == WM_CTLCOLORBTN) return (LRESULT)g_brush_white;
            if (is_tile_label((HWND)lparam)) {
                int is_value = GetDlgCtrlID((HWND)lparam) >= IDC_TILE_VALUE &&
                               GetDlgCtrlID((HWND)lparam) < IDC_TILE_VALUE + UI_TILE_COUNT;
                SetBkColor((HDC)wparam, UI_TILE);
                SetTextColor((HDC)wparam, is_value ? UI_ACCENT : UI_MUTED);
                return (LRESULT)g_brush_tile;
            }
            SetBkColor((HDC)wparam, RGB(255, 255, 255));
            SetTextColor((HDC)wparam, is_muted((HWND)lparam) ? UI_MUTED : UI_TEXT);
            return (LRESULT)g_brush_white;
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            refresh_high_contrast();
            RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            break;
        case WM_ACTIVATE:
            /* A top-level window that is not a dialog loses its focused
               control on deactivation; keep it for the return. */
            if (LOWORD(wparam) == WA_INACTIVE) {
                HWND focus = GetFocus();
                if (focus && IsChild(window, focus)) g_saved_focus = focus;
            } else if (!HIWORD(wparam)) {   /* not while minimised */
                if (g_saved_focus && IsChild(window, g_saved_focus) && IsWindowVisible(g_saved_focus))
                    SetFocus(g_saved_focus);
                else if (g_nav[g_page])
                    SetFocus(g_nav[g_page]);
                return 0;
            }
            break;
        case WM_DRAWITEM:
            draw_button((DRAWITEMSTRUCT *)lparam);
            return TRUE;
        case WM_COMMAND: {
            UINT id = LOWORD(wparam);
            UINT notification = HIWORD(wparam);
            /* Settings controls apply immediately. */
            if ((notification == CBN_SELCHANGE &&
                 (id == IDC_SENSITIVITY || id == IDC_LANGUAGE_MODE || id == IDC_SPELLING || id == IDC_DIGITS)) ||
                (notification == BN_CLICKED &&
                 (id == IDC_APP_STARTUP || id == IDC_PERSONAL_DICTIONARY || id == IDC_PUNCTUATION ||
                  id == IDC_PERSIAN_LETTERS || id == IDC_CAPITALIZE || id == IDC_SNIPPETS ||
                  id == IDC_LEARN_WRITING || id == IDC_VOCAB_IT)) ||
                (notification == CBN_CLOSEUP &&
                 (id == IDC_SENSITIVITY || id == IDC_LANGUAGE_MODE || id == IDC_SPELLING || id == IDC_DIGITS)) ||
                (notification == EN_KILLFOCUS && id == IDC_EXCLUDED)) {
                /* Arrowing through an open list saves once, when it closes;
                   leaving the box unchanged saves nothing. */
                if (notification == CBN_SELCHANGE && SendMessageW((HWND)lparam, CB_GETDROPPEDSTATE, 0, 0))
                    return 0;
                /* Closed with Esc or on the same item: nothing changed. */
                if (notification == CBN_CLOSEUP &&
                    SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0) == g_dropdown_selection)
                    return 0;
                if (notification == EN_KILLFOCUS && !excluded_edit_changed()) return 0;
                apply_controls();
                if (id == IDC_LEARN_WRITING) update_diagnostics_ui();
                return 0;
            }
            if (notification == CBN_DROPDOWN) {
                g_dropdown_selection = SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0);
                return 0;
            }
            if (id >= IDC_NAV_FIRST && id < IDC_NAV_FIRST + UI_PAGES) {
                show_page((int)(id - IDC_NAV_FIRST));
                InvalidateRect(window, NULL, FALSE);
                return 0;
            }
            switch (id) {
                case IDOK: {
                    /* Enter: the dialog manager asks for the default button,
                       which owner-drawn buttons cannot be. Press the focused
                       button, or commit the excluded-apps box. */
                    HWND focus = GetFocus();
                    if (focus && IsChild(window, focus) &&
                        (GetWindowLongPtrW(focus, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW &&
                        GetDlgCtrlID(focus) != IDC_EXCLUDED)
                        SendMessageW(focus, BM_CLICK, 0, 0);
                    else if (focus == g_excluded)
                        apply_controls();
                    return 0;
                }
                case IDCANCEL:
                    hide_dashboard(window);
                    return 0;
                case IDC_ENABLE:
                    toggle_enabled(0);
                    return 0;
                case IDM_TOGGLE:
                    toggle_enabled(0);   /* the user just saw the menu: no note */
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
                case IDC_HIDE:
                    hide_dashboard(window);
                    return 0;
                case IDC_OPEN_DATA:
                    open_data_folder();
                    return 0;
                case IDC_EDIT_SNIPPETS:
                case IDM_EDIT_SNIPPETS:
                    open_snippets_file();
                    return 0;
                case IDM_LEARN_WRITING:
                    g_settings.learn_writing = !g_settings.learn_writing;
                    memory_apply_setting();
                    save_settings();
                    update_controls_from_settings();
                    set_activity(g_settings.learn_writing
                        ? L"Learning your writing: words and hand repairs are remembered on this PC."
                        : L"Learning paused. What was learned is kept; nothing new is recorded.");
                    return 0;
                case IDC_OPEN_MEMORY:
                case IDM_OPEN_MEMORY:
                    open_memory_file();
                    return 0;
                case IDC_FORGET_MEMORY:
                case IDM_FORGET_MEMORY:
                    if (MessageBoxW(window,
                                    L"Forget every word and repair KeySwitchFix has learned from your typing?\n\n"
                                    L"This deletes writing-memory.txt and cannot be undone.",
                                    APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                        memory_forget();
                        update_diagnostics_ui();
                    }
                    return 0;
                case IDM_VOCAB_IT:
                    g_settings.vocab_it = !g_settings.vocab_it;
                    save_settings();
                    update_controls_from_settings();
                    set_activity(g_settings.vocab_it ? L"IT and computing vocabulary: on."
                                                     : L"IT and computing vocabulary: off.");
                    return 0;
                case IDM_PUNCTUATION:
                case IDM_CAPITALIZE:
                case IDM_SNIPPETS: {
                    int *flag = id == IDM_PUNCTUATION ? &g_settings.punctuation
                              : id == IDM_CAPITALIZE ? &g_settings.auto_capitalize
                              : &g_settings.snippets;
                    *flag = !*flag;
                    save_settings();
                    update_controls_from_settings();
                    set_activity(id == IDM_PUNCTUATION
                        ? (g_settings.punctuation ? L"Persian punctuation after Persian words: on." : L"Persian punctuation: off.")
                        : id == IDM_CAPITALIZE
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
                        id == IDM_LANGUAGE_AUTO ? 0 : id == IDM_LANGUAGE_PERSIAN ? 1 : 2;
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
        }
        case WM_HOTKEY:
            if (wparam == ID_HOTKEY_UNDO) {
                SetTimer(window, ID_TIMER_UNDO, 40, NULL);
                return 0;
            }
            if (wparam == ID_HOTKEY_TOGGLE) {
                toggle_enabled(1);
                return 0;
            }
            if (wparam == ID_HOTKEY_CLEANUP) {
                if (engine_enter()) {
                    start_selection_cleanup();
                    engine_leave();
                }
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
                update_tray_tip();
                return 0;
            }
            if (wparam == ID_TIMER_TRAY_RETRY) {
                KillTimer(window, ID_TIMER_TRAY_RETRY);
                add_tray_icon();
                return 0;
            }
            if (wparam == ID_TIMER_UNDO) {
                if (!key_down(VK_CONTROL) && !key_down(VK_LWIN) && !key_down(VK_RWIN) && !key_down(VK_BACK)) {
                    if (engine_enter()) {
                        KillTimer(window, ID_TIMER_UNDO);
                        try_undo(0);
                        engine_leave();
                    }
                }
                return 0;
            }
            if (wparam == ID_TIMER_FOCUS_QUERY) {
                KillTimer(window, ID_TIMER_FOCUS_QUERY);
                PostMessageW(window, WM_APP_FOCUS_QUERY, 0, 0);
                return 0;
            }
            if (wparam == ID_TIMER_SMART_CORRECTION) {
                try_smart_correction();
                return 0;
            }
            if (wparam == ID_TIMER_CLEANUP) {
                if (engine_enter()) {
                    continue_selection_cleanup();
                    engine_leave();
                }
                return 0;
            }
            if (wparam == ID_TIMER_STATS) {
                stats_touch_day();
                stats_save();
                return 0;
            }
            if (wparam == ID_TIMER_SNIPPETS) {
                if (g_settings.snippets) snippets_reload(0);
                /* Hand edits of the file are picked up within two seconds;
                   learned words reach the disk at most once a minute. */
                if (g_memory_active && memory_file_edited()) memory_save();
                else if (g_memory_active && g_memory.dirty &&
                         GetTickCount() - g_memory_saved_at > 60000u)
                    memory_save();
                return 0;
            }
            break;
        case WM_APP_DIAGNOSTIC:
            if (IsWindowVisible(window)) update_diagnostics_ui();
            return 0;
        case WM_APP_FOCUS_QUERY:
            g_focus_query_posted = 0;
            if (g_engine_depth > 0) {
                /* Still inside an operation: try again shortly. */
                g_focus_query_posted = 1;
                SetTimer(window, ID_TIMER_FOCUS_QUERY, 20, NULL);
            } else if (g_focus_event_protected == -1) {
                run_focus_query();
            }
            return 0;
        case WM_APP_SAVE_STATS:
            stats_save();
            return 0;
        case WM_QUERYENDSESSION:
            stats_save();
            memory_save();
            save_settings();
            return TRUE;
        case WM_ENDSESSION:
            if (wparam) {
                stats_save();
                memory_save();
            }
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lparam)) {
                case NIN_SELECT:
                case WM_LBUTTONDBLCLK:
                    show_main_window();
                    break;
                case NIN_KEYSELECT:
                    show_main_window_by(1);
                    break;
                case WM_CONTEXTMENU:
                    /* Version 4: the anchor (mouse or keyboard) is in wparam. */
                    show_tray_menu(GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam));
                    break;
                default:
                    break;
            }
            return 0;
        case WM_APP_EXIT:
            g_exit_requested = 1;
            DestroyWindow(window);
            return 0;
        case WM_CLOSE:
            if (!g_exit_requested) {
                hide_dashboard(window);
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
            KillTimer(window, ID_TIMER_TRAY_RETRY);
            stats_save();
            memory_save();
            clipboard_snapshot_free(&g_cleanup_saved_clipboard);
            if (g_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_UNDO);
            if (g_toggle_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_TOGGLE);
            if (g_cleanup_hotkey_registered) UnregisterHotKey(window, ID_HOTKEY_CLEANUP);
            if (g_focus_event_hook) UnhookWinEvent(g_focus_event_hook);
            if (g_keyboard_hook) UnhookWindowsHookEx(g_keyboard_hook);
            if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
            g_tray.uFlags = 0;
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            destroy_ui(window);
            if (g_icon_paused) DestroyIcon(g_icon_paused);
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
    /* MSAA queries in focus_event_proc need COM on this (the UI) thread. */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    /* DPI awareness (PerMonitorV2) is declared in the manifest. */
    g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    g_shift_down = key_down(VK_SHIFT) || key_down(VK_LSHIFT) || key_down(VK_RSHIFT);
    g_control_down = key_down(VK_CONTROL) || key_down(VK_LCONTROL) || key_down(VK_RCONTROL);
    g_alt_down = key_down(VK_MENU) || key_down(VK_LMENU) || key_down(VK_RMENU);
    g_windows_down = key_down(VK_LWIN) || key_down(VK_RWIN);
    g_show_message = RegisterWindowMessageW(L"KeySwitchFix.ShowDashboard");
    mutex = CreateMutexW(NULL, TRUE, APP_MUTEX);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(WINDOW_CLASS, NULL);
        if (existing) {
            /* The running copy shows itself (centred, refreshed); this one
               lets it take the foreground. */
            DWORD process_id = 0;
            GetWindowThreadProcessId(existing, &process_id);
            AllowSetForegroundWindow(process_id);
            PostMessageW(existing, g_show_message, 0, 0);
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
    g_extra_words.contains = extra_word_known;
    g_extra_words.has_prefix = extra_word_prefix;
    g_extra_words.context = NULL;
    g_lexicons.extra = &g_extra_words;
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
    g_english_spelling.rank_adjust = spell_rank_adjust;
    g_english_spelling.rank_adjust_context = &g_rank_language_en;
    g_persian_spelling.rank_adjust = spell_rank_adjust;
    g_persian_spelling.rank_adjust_context = &g_rank_language_fa;
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    ks_context_reset(&g_intent_context);
    load_settings();
    load_personal_dictionary();
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&controls);

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = main_window_proc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    window_class.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassExW(&window_class)) {
        MessageBoxW(NULL, L"The main window could not be registered.", APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 3;
    }

    {
        /* Client area authored at UI_CLIENT_WIDTH x UI_CLIENT_HEIGHT (96 DPI),
           scaled for the primary monitor and shrunk to fit its work area. */
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        RECT frame;
        RECT work_area;
        work_area_for(NULL, &work_area);
        choose_scale(window_dpi(NULL), &work_area);
        SetRect(&frame, 0, 0, scale(UI_CLIENT_WIDTH), scale(UI_CLIENT_HEIGHT));
        adjust_for_dpi(&frame, style, g_monitor_dpi);
        g_window = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS, L"KeySwitchFix " APP_VERSION,
                                   style, CW_USEDEFAULT, CW_USEDEFAULT,
                                   frame.right - frame.left, frame.bottom - frame.top,
                                   NULL, NULL, instance, NULL);
        /* Created on a monitor with another DPI: rebuild at that size. */
        if (g_window && window_dpi(g_window) != g_monitor_dpi)
            rebuild_for_dpi(g_window, window_dpi(g_window), NULL);
    }
    if (!g_window) {
        MessageBoxW(NULL, L"The main window could not be created.", APP_NAME, MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 4;
    }

    stats_load();
    snippets_reload(1);
    memory_load();
    install_hooks();
    /* Focus changes (including between fields of one web page) reset the
       engine and tell it about password fields; see focus_event_proc. */
    g_focus_event_hook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, NULL,
                                         focus_event_proc, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
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
        /* Tab, arrows, mnemonics and Ctrl+Tab inside the dashboard. */
        if (dashboard_message(&message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(mutex);
    return (int)message.wParam;
}
