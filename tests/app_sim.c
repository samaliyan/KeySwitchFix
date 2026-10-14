/*
 * End-to-end simulation of the keyboard hook on Linux.
 *
 * src/app.c is compiled against the Win32 subset in tests/win32sim and this file
 * plays Windows: keyboard layouts (US, Persian, Russian, German, French
 * AZERTY, Arabic),
 * the focused text field (what each key or injected input does to it), the
 * layout switch an application performs when asked, the clock, the
 * application resources and the language-pack folder. Every key goes
 * through keyboard_hook_proc exactly as a real low-level hook call would.
 *
 * What it cannot show: real Windows timing, real applications, IME, real
 * keyboard-layout DLLs. The layouts below are transcribed by hand.
 */
#define WinMain app_WinMain
#include "../src/app.c"
#undef WinMain

#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- Clock ---------------------------------------------------------------- */

static DWORD sim_now = 5000000u;
DWORD GetTickCount(void) { return sim_now; }
ULONGLONG GetTickCount64(void) { return sim_now; }

/* ---- Keyboard layouts ----------------------------------------------------- */

#define SIM_KEYS 0x60
typedef struct SIM_LAYOUT {
    HKL hkl;
    const char *name;
    wchar_t normal[SIM_KEYS];
    wchar_t shifted[SIM_KEYS];
    unsigned char dead[SIM_KEYS];      /* a dead key: ToUnicodeEx returns -1 */
    unsigned char ligature[SIM_KEYS];  /* two characters (Arabic lam-alef) */
    int lower_case_only;               /* Shift does not give capitals (Arabic, Persian) */
} SIM_LAYOUT;

static SIM_LAYOUT sim_layouts[8];
static int sim_layout_count;
static HKL sim_installed[8];
static int sim_installed_count;
static HKL sim_active;
static int sim_honour_switch = 1;

static void row(SIM_LAYOUT *layout, DWORD first, const wchar_t *normal, const wchar_t *shifted) {
    DWORD i;
    for (i = 0; normal[i]; ++i) {
        /* \x01 marks a key position that types nothing. */
        layout->normal[first + i] = normal[i] == 1 ? 0 : normal[i];
        layout->shifted[first + i] = shifted && shifted[i] != 1 ? shifted[i] : 0;
    }
}

static SIM_LAYOUT *new_layout(uintptr_t hkl, const char *name) {
    SIM_LAYOUT *layout = &sim_layouts[sim_layout_count++];
    memset(layout, 0, sizeof(*layout));
    layout->hkl = (HKL)hkl;
    layout->name = name;
    return layout;
}

static void build_layouts(void) {
    SIM_LAYOUT *l;
    l = new_layout(0x04090409u, "US");
    row(l, 0x02, L"1234567890-=", L"!@#$%^&*()_+");
    l->shifted[0x0C] = L'_';
    row(l, 0x10, L"qwertyuiop[]", L"QWERTYUIOP{}");
    row(l, 0x1E, L"asdfghjkl;'`", L"ASDFGHJKL:\"~");
    row(l, 0x2B, L"\\zxcvbnm,./", L"|ZXCVBNM<>?");

    l = new_layout(0x04290429u, "Persian");
    row(l, 0x02, L"\u06F1\u06F2\u06F3\u06F4\u06F5\u06F6\u06F7\u06F8\u06F9\u06F0-=",
                 L"!@#$%^&*)(_+");
    l->shifted[0x0C] = L'_';
    row(l, 0x10, L"\u0636\u0635\u062B\u0642\u0641\u063A\u0639\u0647\u062E\u062D\u062C\u0686",
                 L"\u0652\u064C\u064D\u064B\u064F\u0650\u064E\u0651][}{");
    row(l, 0x1E, L"\u0634\u0633\u06CC\u0628\u0644\u0627\u062A\u0646\u0645\u06A9\u06AF\u067E",
                 L"\u0624\u0626\u064A\u0625\u0623\u0622\u0629\u00BB\u00AB:\u061B\u00F7");
    row(l, 0x2B, L"\\\u0638\u0637\u0632\u0631\u0630\u062F\u067E\u0648./",
                 L"|\u0643\u0653\u0698\u0670\u200C\u0654\u0621><\u061F");
    /* The 0x29 (grave) key is Persian pe on this variant; 0x32 is pe as well
       on the standard one: keep the classic Windows "Persian" layout. */
    l->normal[0x29] = L'\u067E'; l->shifted[0x29] = L'\u00F7';
    l->normal[0x32] = L'\u067E';
    l->lower_case_only = 1;

    l = new_layout(0x04190419u, "Russian");
    row(l, 0x02, L"1234567890-=", L"!\"\u2116;%:?*()_+");
    l->shifted[0x0C] = L'_';
    row(l, 0x10, L"\u0439\u0446\u0443\u043A\u0435\u043D\u0433\u0448\u0449\u0437\u0445\u044A",
                 L"\u0419\u0426\u0423\u041A\u0415\u041D\u0413\u0428\u0429\u0417\u0425\u042A");
    row(l, 0x1E, L"\u0444\u044B\u0432\u0430\u043F\u0440\u043E\u043B\u0434\u0436\u044D\u0451",
                 L"\u0424\u042B\u0412\u0410\u041F\u0420\u041E\u041B\u0414\u0416\u042D\u0401");
    row(l, 0x2B, L"\\\u044F\u0447\u0441\u043C\u0438\u0442\u044C\u0431\u044E.",
                 L"/\u042F\u0427\u0421\u041C\u0418\u0422\u042C\u0411\u042E,");

    /* The Russian keyboard added under the English language. */
    l = new_layout(0x04190409u, "Russian (filed under English)");
    *l = sim_layouts[sim_layout_count - 2];
    l->hkl = (HKL)0x04190409u;

    l = new_layout(0x04070407u, "German");
    row(l, 0x02, L"1234567890\u00DF\x01", L"!\"\u00A7$%&/()=?\x01");
    l->dead[0x0D] = 1;
    row(l, 0x10, L"qwertzuiop\u00FC+", L"QWERTZUIOP\u00DC*");
    row(l, 0x1E, L"asdfghjkl\u00F6\u00E4\x01", L"ASDFGHJKL\u00D6\u00C4\x01");
    l->dead[0x29] = 1;
    row(l, 0x2B, L"#yxcvbnm,.-", L"'YXCVBNM;:_");

    l = new_layout(0x040C040Cu, "French (AZERTY)");
    row(l, 0x02, L"&\u00E9\"'(-\u00E8_\u00E7\u00E0)=", L"1234567890\u00B0+");
    row(l, 0x10, L"azertyuiop\x01$", L"AZERTYUIOP\x01\u00A3");
    l->dead[0x1A] = 1;
    row(l, 0x1E, L"qsdfghjklm\u00F9\u00B2", L"QSDFGHJKLM%\x01");
    row(l, 0x2B, L"*wxcvbn,;:!", L"\u00B5WXCVBN?./\u00A7");

    l = new_layout(0x04010401u, "Arabic");
    row(l, 0x02, L"1234567890-=", L"!@#$%^&*)(_+");
    l->shifted[0x0C] = L'_';
    row(l, 0x10, L"\u0636\u0635\u062B\u0642\u0641\u063A\u0639\u0647\u062E\u062D\u062C\u062F",
                 L"\u064E\u064B\u064F\u064C\u0644\u0625\u2018\u00F7\u00D7\u061B<>");
    row(l, 0x1E, L"\u0634\u0633\u064A\u0628\u0644\u0627\u062A\u0646\u0645\u0643\u0637\u0630",
                 L"\u0650\u064D][\u0644\u0623\u0640\u060C/:\"\u0651");
    row(l, 0x2B, L"\\\u0626\u0621\u0624\u0631\x01\u0649\u0629\u0648\u0632\u0638",
                 L"|~\u0652}{\x01\u0622\u2019,.\u061F");
    l->ligature[0x30] = 1;
    l->lower_case_only = 1;
}

static SIM_LAYOUT *find_sim_layout(HKL hkl) {
    int i;
    for (i = 0; i < sim_layout_count; ++i)
        if (sim_layouts[i].hkl == hkl) return &sim_layouts[i];
    return NULL;
}

static void install_layouts(int count, ...) {
    va_list list;
    int i;
    va_start(list, count);
    sim_installed_count = count;
    for (i = 0; i < count; ++i) sim_installed[i] = (HKL)va_arg(list, uintptr_t);
    va_end(list);
    sim_active = sim_installed[0];
}

HKL GetKeyboardLayout(DWORD thread) { (void)thread; return sim_active; }

int GetKeyboardLayoutList(int count, HKL *layouts) {
    int i;
    if (count == 0 || !layouts) return sim_installed_count;
    for (i = 0; i < count && i < sim_installed_count; ++i) layouts[i] = sim_installed[i];
    return i;
}

UINT MapVirtualKeyExW(UINT code, UINT type, HKL layout) {
    (void)type;
    return find_sim_layout(layout) && code && code < SIM_KEYS ? code + 0x100 : 0;
}

static int sim_shift;
static int sim_caps;

static wchar_t sim_upper(wchar_t c) {
    if (c >= L'a' && c <= L'z') return (wchar_t)(c - 32);
    if (c >= 0x0430 && c <= 0x044F) return (wchar_t)(c - 32);
    if (c >= 0x00E0 && c <= 0x00FE && c != 0x00F7) return (wchar_t)(c - 32);
    return c;
}

int ToUnicodeEx(UINT virtual_key, UINT scan, const BYTE *state, LPWSTR output, int capacity,
                UINT flags, HKL hkl) {
    SIM_LAYOUT *layout = find_sim_layout(hkl);
    int shift = state && (state[VK_SHIFT] & 0x80);
    int caps = state && (state[VK_CAPITAL] & 1);
    wchar_t c;
    (void)virtual_key;
    (void)flags;
    if (!layout || scan >= SIM_KEYS || capacity < 2) return 0;
    if (layout->dead[scan]) return -1;
    if (layout->ligature[scan]) {
        output[0] = 0x0644;
        output[1] = 0x0627;
        return 2;
    }
    c = shift ? layout->shifted[scan] : layout->normal[scan];
    if (!c) return 0;
    if (caps && !layout->lower_case_only) {
        wchar_t other = shift ? layout->normal[scan] : layout->shifted[scan];
        if (sim_upper(layout->normal[scan]) == layout->shifted[scan] && other) c = other;
    }
    output[0] = c;
    return 1;
}

/* ---- The focused field ---------------------------------------------------- */

static wchar_t screen[4096];
static int screen_length;
static int sim_inputs;   /* injected events seen */

static void screen_reset(void) {
    screen_length = 0;
    screen[0] = 0;
}

static void screen_put(wchar_t c) {
    if (screen_length < 4095) screen[screen_length++] = c;
    screen[screen_length] = 0;
}

static void screen_back(void) {
    if (screen_length > 0) screen[--screen_length] = 0;
}

UINT SendInput(UINT count, INPUT *inputs, int size) {
    UINT i;
    (void)size;
    for (i = 0; i < count; ++i) {
        const KEYBDINPUT *key = &inputs[i].ki;
        ++sim_inputs;
        if (key->dwFlags & KEYEVENTF_KEYUP) continue;
        if (key->dwFlags & KEYEVENTF_UNICODE) screen_put((wchar_t)key->wScan);
        else if (key->wVk == VK_BACK) screen_back();
        else if (key->wVk == VK_SPACE) screen_put(L' ');
        else if (key->wVk == VK_RETURN) screen_put(L'\n');
        else if (key->wVk == VK_TAB) screen_put(L'\t');
    }
    return count;
}

/* An application that switches only after `sim_switch_delay` more keys
   (a busy browser): the request waits in its queue. */
static int sim_switch_delay;
static HKL sim_switch_pending;
static int sim_switch_countdown;

static void sim_switch(LPARAM layout) {
    if (!sim_honour_switch || !find_sim_layout((HKL)layout)) return;
    if (sim_switch_delay > 0) {
        /* A repeated request for the layout already queued changes nothing:
           the application works through its queue. */
        if (sim_switch_pending != (HKL)layout) {
            sim_switch_pending = (HKL)layout;
            sim_switch_countdown = sim_switch_delay;
        }
        return;
    }
    sim_active = (HKL)layout;
}

static void sim_switch_tick(void) {
    if (sim_switch_pending && --sim_switch_countdown <= 0) {
        sim_active = sim_switch_pending;
        sim_switch_pending = NULL;
    }
}

LRESULT SendMessageTimeoutW(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT flags,
                            UINT timeout, DWORD_PTR *result) {
    (void)window; (void)wparam; (void)flags; (void)timeout;
    if (result) *result = 0;
    if (message == WM_INPUTLANGCHANGEREQUEST) sim_switch(lparam);
    return 1;
}

static int sim_keep_posted;
BOOL PostMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)window; (void)wparam;
    if (message == WM_INPUTLANGCHANGEREQUEST) sim_switch(lparam);
    if (message == WM_APP_KEEP_WORD) ++sim_keep_posted;
    return TRUE;
}

HWND GetForegroundWindow(void) { return (HWND)(uintptr_t)0x1000; }
DWORD GetWindowThreadProcessId(HWND window, LPDWORD process) {
    (void)window;
    if (process) *process = 4242;
    return 77;
}
LRESULT CallNextHookEx(HHOOK hook, int code, WPARAM wparam, LPARAM lparam) {
    (void)hook; (void)code; (void)wparam; (void)lparam;
    return 0;
}
short GetKeyState(int key) {
    if (key == VK_SHIFT || key == VK_LSHIFT) return sim_shift ? (short)0x8000 : 0;
    if (key == VK_CAPITAL) return (short)(sim_caps ? 1 : 0);
    return 0;
}
short GetAsyncKeyState(int key) { return GetKeyState(key); }

/* ---- Combo boxes (the dashboard's language lists) -------------------------- */

typedef struct SIM_COMBO {
    wchar_t text[64][160];
    LRESULT data[64];
    int count;
    int selection;
    int dropped;
} SIM_COMBO;
static SIM_COMBO sim_combos[3];   /* first language, second language, writing language */

static SIM_COMBO *sim_combo(HWND window) {
    int i;
    for (i = 0; i < 3; ++i)
        if (window == (HWND)&sim_combos[i]) return &sim_combos[i];
    return NULL;
}

LRESULT SendMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    SIM_COMBO *combo = sim_combo(window);
    if (!combo) return message == CB_GETCURSEL ? -1 : 0;
    switch (message) {
        case CB_RESETCONTENT: combo->count = 0; combo->selection = -1; return 0;
        case CB_ADDSTRING:
            if (combo->count >= 64) return -1;
            wcsncpy(combo->text[combo->count], (const wchar_t *)lparam, 159);
            combo->data[combo->count] = 0;
            return combo->count++;
        case CB_SETITEMDATA: if ((int)wparam < combo->count) combo->data[wparam] = lparam; return 0;
        case CB_GETITEMDATA: return (int)wparam < combo->count ? combo->data[wparam] : -1;
        case CB_SETCURSEL: combo->selection = (int)wparam < combo->count ? (int)wparam : -1; return combo->selection;
        case CB_GETCURSEL: return combo->selection;
        case CB_GETDROPPEDSTATE: return combo->dropped;
        default: return 0;
    }
}

/* The list item whose data is the language `code`. */
static int sim_combo_find(SIM_COMBO *combo, const wchar_t *code) {
    int i;
    for (i = 0; i < combo->count; ++i)
        if (combo->data[i] >= 0 && combo->data[i] < g_language_choice_count &&
            wcscmp(g_language_choices[combo->data[i]].code, code) == 0) return i;
    return -1;
}

static void sim_notify(int id, SIM_COMBO *combo, UINT notification) {
    main_window_proc((HWND)(uintptr_t)0x2000, WM_COMMAND, MAKEWPARAM(id, notification), (LPARAM)combo);
}

/* ---- Resources and files -------------------------------------------------- */

static unsigned char *read_all(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long length;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)length + 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static struct { int id; const char *file; unsigned char *data; size_t size; } sim_resources[] = {
    {IDR_EN_BLOOM, "resources/en.bloom", NULL, 0}, {IDR_FA_BLOOM, "resources/fa.bloom", NULL, 0},
    {IDR_EN_PREFIX_BLOOM, "resources/en-prefix.bloom", NULL, 0},
    {IDR_FA_PREFIX_BLOOM, "resources/fa-prefix.bloom", NULL, 0},
    {IDR_EN_COMMON_BLOOM, "resources/en-common.bloom", NULL, 0},
    {IDR_FA_COMMON_BLOOM, "resources/fa-common.bloom", NULL, 0},
    {IDR_EN_FREQUENT_BLOOM, "resources/en-frequent.bloom", NULL, 0},
    {IDR_FA_FREQUENT_BLOOM, "resources/fa-frequent.bloom", NULL, 0},
    {IDR_EN_COMMON_PREFIX, "resources/en-common-prefix.bloom", NULL, 0},
    {IDR_FA_COMMON_PREFIX, "resources/fa-common-prefix.bloom", NULL, 0},
    {IDR_EN_RANK_TABLE, "resources/en-rank.bin", NULL, 0},
    {IDR_FA_RANK_TABLE, "resources/fa-rank.bin", NULL, 0},
    {IDR_EN_FORMS_BLOOM, "resources/en-forms.bloom", NULL, 0},
    {IDR_FA_FORMS_BLOOM, "resources/fa-forms.bloom", NULL, 0},
    {IDR_FA_PAIRS, "resources/fa-pairs.bin", NULL, 0},
};

HRSRC FindResourceW(HMODULE module, LPCWSTR name, LPCWSTR type) {
    size_t i;
    (void)module; (void)type;
    for (i = 0; i < sizeof(sim_resources) / sizeof(sim_resources[0]); ++i) {
        if ((uintptr_t)name != (uintptr_t)sim_resources[i].id) continue;
        if (!sim_resources[i].data)
            sim_resources[i].data = read_all(sim_resources[i].file, &sim_resources[i].size);
        return sim_resources[i].data ? (HRSRC)&sim_resources[i] : NULL;
    }
    return NULL;
}
DWORD SizeofResource(HMODULE module, HRSRC resource) {
    (void)module;
    return resource ? (DWORD)((__typeof__(&sim_resources[0]))resource)->size : 0;
}
HGLOBAL LoadResource(HMODULE module, HRSRC resource) { (void)module; return (HGLOBAL)resource; }
LPVOID LockResource(HGLOBAL loaded) {
    return loaded ? ((__typeof__(&sim_resources[0]))loaded)->data : NULL;
}

/* The program folder holds languages\*.kslang: the test fixtures. */
static const wchar_t *sim_packs[8];
static int sim_pack_count;

DWORD GetModuleFileNameW(HMODULE module, LPWSTR path, DWORD capacity) {
    (void)module;
    safe_copy(path, capacity, L"C:\\Sim\\KeySwitchFix.exe");
    return (DWORD)wcslen(path);
}

typedef struct SIM_FIND { int next; } SIM_FIND;
static SIM_FIND sim_find;

HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *found) {
    if (wcscmp(pattern, L"C:\\Sim\\languages\\*.kslang") != 0 || sim_pack_count == 0)
        return INVALID_HANDLE_VALUE;
    memset(found, 0, sizeof(*found));
    safe_copy(found->cFileName, 260, sim_packs[0]);
    sim_find.next = 1;
    return (HANDLE)&sim_find;
}
BOOL FindNextFileW(HANDLE search, WIN32_FIND_DATAW *found) {
    SIM_FIND *state = (SIM_FIND *)search;
    if (state->next >= sim_pack_count) return FALSE;
    memset(found, 0, sizeof(*found));
    safe_copy(found->cFileName, 260, sim_packs[state->next++]);
    return TRUE;
}
BOOL FindClose(HANDLE search) { (void)search; return TRUE; }

static const char *sim_dictionary_file = "/tmp/keyswitchfix-sim-personal-dictionary.txt";

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *security,
                   DWORD disposition, DWORD flags, HANDLE templ) {
    char real[512];
    const wchar_t *prefix = L"C:\\Sim\\languages\\";
    FILE *file;
    (void)access; (void)share; (void)security; (void)disposition; (void)flags; (void)templ;
    /* The personal dictionary: a real temporary file. */
    if (wcscmp(path, L"C:\\Sim\\data\\personal-dictionary.txt") == 0) {
        file = fopen(sim_dictionary_file, "a+b");
        return file ? (HANDLE)file : INVALID_HANDLE_VALUE;
    }
    if (wcsncmp(path, prefix, wcslen(prefix)) != 0) return INVALID_HANDLE_VALUE;
    /* languages\\ru.kslang is the fixture tests/fixtures/ru-mini.kslang. */
    {
        char name[64];
        char *dot;
        snprintf(name, sizeof(name), "%ls", path + wcslen(prefix));
        dot = strstr(name, ".kslang");
        if (!dot) return INVALID_HANDLE_VALUE;
        *dot = 0;
        if (strcmp(name, "xx") == 0) strcpy(name, "ru");   /* a copy under another name */
        snprintf(real, sizeof(real), "tests/fixtures/%s-mini.kslang", name);
    }
    file = fopen(real, "rb");
    return file ? (HANDLE)file : INVALID_HANDLE_VALUE;
}
BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER *size) {
    FILE *file = (FILE *)handle;
    long here = ftell(file);
    fseek(file, 0, SEEK_END);
    size->QuadPart = ftell(file);
    fseek(file, here, SEEK_SET);
    return TRUE;
}
BOOL ReadFile(HANDLE handle, LPVOID buffer, DWORD wanted, LPDWORD read, void *overlapped) {
    (void)overlapped;
    *read = (DWORD)fread(buffer, 1, wanted, (FILE *)handle);
    return TRUE;
}
/* UTF-8 only (CP_UTF8), as the program uses it: -1 counts the terminator. */
int WideCharToMultiByte(UINT page, DWORD flags, LPCWSTR text, int count, char *out, int capacity,
                        const char *fallback, BOOL *used_fallback) {
    int i, used = 0;
    (void)page; (void)flags; (void)fallback;
    if (used_fallback) *used_fallback = FALSE;
    if (count < 0) count = (int)wcslen(text) + 1;
    for (i = 0; i < count; ++i) {
        unsigned c = (unsigned)text[i];
        char bytes[4];
        int n = c < 0x80 ? 1 : c < 0x800 ? 2 : 3, k;
        if (n == 1) bytes[0] = (char)c;
        else if (n == 2) { bytes[0] = (char)(0xC0 | (c >> 6)); bytes[1] = (char)(0x80 | (c & 0x3F)); }
        else { bytes[0] = (char)(0xE0 | (c >> 12)); bytes[1] = (char)(0x80 | ((c >> 6) & 0x3F));
               bytes[2] = (char)(0x80 | (c & 0x3F)); }
        if (out) {
            if (used + n > capacity) return 0;
            for (k = 0; k < n; ++k) out[used + k] = bytes[k];
        }
        used += n;
    }
    return used;
}
BOOL WriteFile(HANDLE handle, LPCVOID bytes, DWORD size, LPDWORD written, void *overlapped) {
    (void)overlapped;
    *written = (DWORD)fwrite(bytes, 1, size, (FILE *)handle);
    return *written == size;
}
DWORD SetFilePointer(HANDLE handle, LONG distance, LONG *high, DWORD method) {
    (void)high;
    if (fseek((FILE *)handle, distance, method == FILE_END ? SEEK_END : method == 1 ? SEEK_CUR : SEEK_SET))
        return INVALID_SET_FILE_POINTER;
    return (DWORD)ftell((FILE *)handle);
}
/* The application in front: none (no name), or the one a test names. */
static const wchar_t *sim_process_name;
#define SIM_PROCESS_HANDLE ((HANDLE)(uintptr_t)0x3333)
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD id) {
    (void)access; (void)inherit; (void)id;
    return sim_process_name ? SIM_PROCESS_HANDLE : NULL;
}
BOOL QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR path, DWORD *size) {
    (void)flags;
    if (process != SIM_PROCESS_HANDLE || !sim_process_name) return FALSE;
    swprintf(path, *size, L"C:\\Programs\\%ls", sim_process_name);
    *size = (DWORD)wcslen(path);
    return TRUE;
}
BOOL CloseHandle(HANDLE handle) {
    if (handle == SIM_PROCESS_HANDLE) return TRUE;
    if (handle && handle != INVALID_HANDLE_VALUE && handle != (HANDLE)&sim_find) fclose((FILE *)handle);
    return TRUE;
}
LPVOID HeapAlloc(HANDLE heap, DWORD flags, size_t size) { (void)heap; (void)flags; return malloc(size); }
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID block) { (void)heap; (void)flags; free(block); return TRUE; }

/* ---- Driving the hook ----------------------------------------------------- */

static DWORD vk_for_scan(DWORD scan) {
    static const char letters[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm";
    if (scan >= 0x02 && scan <= 0x0A) return '1' + (scan - 0x02);
    if (scan == 0x0B) return '0';
    if (scan >= 0x10 && scan <= 0x32 && letters[scan - 0x10]) return (DWORD)(letters[scan - 0x10] - 'a' + 'A');
    switch (scan) {
        case 0x0C: return 0xBD; case 0x0D: return 0xBB; case 0x0E: return VK_BACK; case 0x0F: return VK_TAB;
        case 0x1A: return 0xDB; case 0x1B: return 0xDD; case 0x1C: return VK_RETURN;
        case 0x27: return 0xBA; case 0x28: return 0xDE; case 0x29: return 0xC0;
        case 0x2B: return 0xDC; case 0x33: return 0xBC; case 0x34: return VK_OEM_PERIOD;
        case 0x35: return VK_OEM_2; case 0x39: return VK_SPACE; case 0x56: return 0xE2;
        default: return 0;
    }
}

static int hook_event(DWORD vk, DWORD scan, WPARAM message) {
    KBDLLHOOKSTRUCT data;
    memset(&data, 0, sizeof(data));
    data.vkCode = vk;
    data.scanCode = scan;
    data.time = sim_now;
    return keyboard_hook_proc(0 /* HC_ACTION */, message, (LPARAM)&data) != 0;
}

/* What the application does with a key the hook let through. */
static void deliver_physical(DWORD vk, DWORD scan) {
    BYTE state[256];
    wchar_t output[4];
    int count;
    if (vk == VK_BACK) { screen_back(); return; }
    if (vk == VK_RETURN) { screen_put(L'\n'); return; }
    if (vk == VK_TAB) { screen_put(L'\t'); return; }
    if (vk == VK_SPACE) {
        SIM_LAYOUT *layout = find_sim_layout(sim_active);
        screen_put(sim_shift && layout && layout->hkl == (HKL)0x04290429u ? (wchar_t)ZWNJ : L' ');
        return;
    }
    memset(state, 0, sizeof(state));
    if (sim_shift) state[VK_SHIFT] = 0x80;
    if (sim_caps) state[VK_CAPITAL] = 1;
    count = ToUnicodeEx(vk, scan, state, output, 4, 4, sim_active);
    if (count >= 1) screen_put(output[0]);
    if (count == 2) screen_put(output[1]);
}

static void press(DWORD scan, int shift) {
    DWORD vk = vk_for_scan(scan);
    if (shift) {
        sim_shift = 1;
        hook_event(VK_LSHIFT, 0x2A, WM_KEYDOWN);
    }
    if (!hook_event(vk, scan, WM_KEYDOWN)) deliver_physical(vk, scan);
    hook_event(vk, scan, WM_KEYUP);
    sim_switch_tick();
    if (shift) {
        hook_event(VK_LSHIFT, 0x2A, WM_KEYUP);
        sim_shift = 0;
    }
    sim_now += 140;
}

/* Keys named by what they type on the US layout. */
static void us_keys(const char *text) {
    static const char lower[] = "1234567890-=\0\0qwertyuiop[]\0\0asdfghjkl;'`\0\\zxcvbnm,./";
    static const char upper[] = "!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";
    for (; *text; ++text) {
        DWORD i;
        if (*text == ' ') { press(0x39, 0); continue; }
        if (*text == '\b') { press(0x0E, 0); continue; }
        if (*text == '\t') { press(0x0F, 0); continue; }
        if (*text == '~') { press(0x39, 1); continue; }   /* Shift+Space: a half-space (ZWNJ) */
        for (i = 0; i < sizeof(lower) - 1; ++i) {
            if (lower[i] == *text) { press(0x02 + i, 0); break; }
            if (upper[i] == *text) { press(0x02 + i, 1); break; }
        }
        if (i == sizeof(lower) - 1) { fprintf(stderr, "no key for %c\n", *text); exit(2); }
    }
}

/* The user switches the keyboard by hand, a while after the last key. */
static void user_switch(uintptr_t layout) {
    sim_now += 4000;
    sim_active = (HKL)layout;
}

/* ---- Set-up --------------------------------------------------------------- */

static void sim_start(const wchar_t *first, const wchar_t *second) {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.enabled = 1;
    g_settings.sensitivity = 1;
    g_settings.spelling = KS_SPELL_BALANCED;
    g_settings.spelling_last_level = KS_SPELL_BALANCED;
    g_settings.digits = KS_DIGITS_BY_LAYOUT;
    g_settings.punctuation = 1;
    g_settings.persian_letters = 1;
    g_settings.auto_capitalize = 1;
    g_settings.vocab_it = 1;
    safe_copy(g_settings.language_first, 8, first);
    safe_copy(g_settings.language_second, 8, second);
    normalize_language_pair(g_settings.language_first, g_settings.language_second);
    g_paths_ok = 0;
    g_data_directory[0] = 0;
    apply_language_pair();
    clear_intent();
    screen_reset();
    sim_now += 60000;
}

static int failures;

static void expect_screen(const wchar_t *wanted, const char *what) {
    if (wcscmp(screen, wanted) != 0) {
        ++failures;
        printf("FAIL: %s\n   screen: [%ls]\n   wanted: [%ls]\n", what, screen, wanted);
    } else {
        printf("  ok  %s: [%ls]\n", what, screen);
    }
}

static void expect(int condition, const char *what) {
    if (!condition) {
        ++failures;
        printf("FAIL: %s\n", what);
    } else {
        printf("  ok  %s\n", what);
    }
}

static void load_builtin(void) {
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
        fprintf(stderr, "resources missing (run from the source folder)\n");
        exit(2);
    }
    g_extra_words.contains = extra_word_known;
    g_extra_words.has_prefix = extra_word_prefix;
    g_lexicons.extra = &g_extra_words;
}

/* ---- Scenarios ------------------------------------------------------------ */

static void english_persian(const wchar_t *first, const wchar_t *second) {
    KS_SLOT persian;
    printf("Pair %ls / %ls\n", first, second);
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04290429u);
    sim_start(first, second);
    persian = slot_of_model(KS_LANG_PERSIAN);
    expect(slot_from_layout((HKL)0x04290429u) == persian, "the Persian keyboard is the Persian slot");
    expect(slot_from_layout((HKL)0x04090409u) == KS_OTHER_SLOT(persian), "the US keyboard is the English slot");
    expect(slot_from_layout((HKL)0x04190419u) == KS_SLOT_NONE, "a Russian keyboard is not in this pair");
    expect(slot_from_layout((HKL)0x04010401u) == KS_SLOT_NONE, "an Arabic keyboard is not Persian");
    /* سلام typed with the US layout active. */
    us_keys("sghl ");
    expect_screen(L"\u0633\u0644\u0627\u0645 ", "Persian word typed on the US layout");
    expect(sim_active == (HKL)0x04290429u, "the application was switched to Persian");
    /* Now "hello" typed while Persian is active. */
    us_keys("hello ");
    expect_screen(L"\u0633\u0644\u0627\u0645 hello ", "English word typed on the Persian layout");
    expect(sim_active == (HKL)0x04090409u, "and back to English");
    /* A correct English sentence is left alone. */
    screen_reset();
    us_keys("this is fine ");
    expect_screen(L"this is fine ", "correct English stays");
    /* Persian digits follow the Persian layout. */
    screen_reset();
    user_switch(0x04290429u);
    us_keys("12 ");
    expect_screen(L"\u06F1\u06F2 ", "digits on the Persian layout stay Persian");
    /* Capital after a full stop (English). */
    screen_reset();
    user_switch(0x04090409u);
    us_keys("this works. then ");
    expect_screen(L"this works. Then ", "sentence capitalisation in English");
}

static void english_russian(void) {
    printf("Pair en / ru (language pack)\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04190419u);
    sim_start(L"en", L"ru");
    expect(wcscmp(language_name(KS_SLOT_B), L"Russian") == 0, "slot B is Russian");
    expect(slot_from_layout((HKL)0x04190419u) == KS_SLOT_B, "the Russian keyboard is slot B");
    expect(slot_from_layout((HKL)0x04090409u) == KS_SLOT_A, "the US keyboard is slot A");
    expect(slot_from_layout((HKL)0x04290429u) == KS_SLOT_NONE, "a Persian keyboard is not in this pair");
    expect(pair_word_key(0x34, (HKL)0x04090409u, (HKL)0x04190419u), "the period key types a Russian letter");
    expect(!pair_word_key(0x02, (HKL)0x04090409u, (HKL)0x04190419u), "digits are not word keys");
    us_keys("ghbdtn ");
    expect_screen(L"\u043F\u0440\u0438\u0432\u0435\u0442 ", "Russian typed on the US layout");
    expect(sim_active == (HKL)0x04190419u, "the application was switched to Russian");
    us_keys("hello ");
    expect_screen(L"\u043F\u0440\u0438\u0432\u0435\u0442 hello ", "English typed on the Russian layout");
    expect(sim_active == (HKL)0x04090409u, "and back to English");
    screen_reset();
    us_keys("the end. ");
    expect_screen(L"the end. ", "an English sentence end with a full stop stays");
    screen_reset();
    user_switch(0x04190419u);
    us_keys("Ghbdtn ");
    expect_screen(L"\u041F\u0440\u0438\u0432\u0435\u0442 ", "a capitalised Russian word stays");
    /* Backspace right after a correction restores what was typed. */
    screen_reset();
    user_switch(0x04090409u);
    us_keys("ghb");
    expect_screen(L"\u043F\u0440\u0438", "the word is repaired while it is typed");
    us_keys("\b");
    expect_screen(L"ghb", "Backspace right after the repair restores the keys as typed");
    /* An application that ignores the switch: the hook types the keys. */
    screen_reset();
    user_switch(0x04090409u);
    sim_honour_switch = 0;
    us_keys("ghbdtn z");
    expect_screen(L"\u043F\u0440\u0438\u0432\u0435\u0442 \u044F", "the next key is typed in Russian at once");
    us_keys("\brfr ");
    sim_honour_switch = 1;
    expect_screen(L"\u043F\u0440\u0438\u0432\u0435\u0442 \u043A\u0430\u043A ",
                  "keys after an ignored switch are typed in Russian by the hook");
    expect(slot_from_layout((HKL)0x04190409u) == KS_SLOT_B, "a Russian keyboard filed under English is Russian");
    /* Persian helpers do not act in this pair. */
    screen_reset();
    user_switch(0x04090409u);
    g_settings.digits = KS_DIGITS_PERSIAN;
    us_keys("12 ");
    expect_screen(L"12 ", "Persian digits are not forced on a pair without Persian");
    g_settings.digits = KS_DIGITS_BY_LAYOUT;
}

static void german_english(void) {
    printf("Pair de / en (German first)\n");
    install_layouts(2, (uintptr_t)0x04070407u, (uintptr_t)0x04090409u);
    sim_start(L"de", L"en");
    expect(slot_from_layout((HKL)0x04070407u) == KS_SLOT_A, "the German keyboard is slot A");
    expect(slot_from_layout((HKL)0x04090409u) == KS_SLOT_B, "the US keyboard is slot B");
    expect(pair_word_key(0x0C, (HKL)0x04070407u, (HKL)0x04090409u), "the sharp-s key is a word key");
    expect(!pair_word_key(0x0D, (HKL)0x04070407u, (HKL)0x04090409u), "the dead accent key is not");
    /* "zeitung" meant for German, typed on the US layout: yeitung. */
    user_switch(0x04090409u);
    us_keys("yeitung ");
    expect_screen(L"zeitung ", "German typed on the US layout");
    expect(sim_active == (HKL)0x04070407u, "the application was switched to German");
    /* "yes" meant for English, typed on the German layout: zes. */
    us_keys("yes ");
    expect_screen(L"zeitung yes ", "English typed on the German layout");
    /* A dead key composes with the next key: that word is left alone (the
       simulated field does not compose, so it shows the bare keys). */
    screen_reset();
    user_switch(0x04070407u);
    press(0x0D, 0);
    us_keys("yes ");
    expect_screen(L"zes ", "a word that starts after a dead key is not touched");
    us_keys("yes ");
    expect_screen(L"zes yes ", "the next word is repaired again");
    screen_reset();
    user_switch(0x04070407u);
    us_keys("haus ");
    expect_screen(L"haus ", "a German word identical in both layouts stays");
    /* English helpers follow the English slot even when it is slot B. */
    screen_reset();
    user_switch(0x04090409u);
    us_keys("this works. then ");
    expect_screen(L"this works. Then ", "English capitalisation with English as slot B");
}

static void missing_pack(void) {
    printf("Missing pack\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04290429u);
    sim_start(L"xx", L"fa");
    expect(wcscmp(g_slots[KS_SLOT_A].code, L"en") == 0, "a missing first language falls back to English");
    expect(wcscmp(g_settings.language_first, L"xx") == 0, "the setting itself is kept");
    expect(wcsstr(g_last_activity, L"missing") != NULL, "the user is told");
    sim_start(L"fa", L"zz");
    expect(wcscmp(g_slots[KS_SLOT_B].code, L"en") == 0, "a missing second language next to Persian is English");
    {
        wchar_t a[8] = L"ru", b[8] = L"ru";
        normalize_language_pair(a, b);
        expect(wcscmp(a, L"ru") == 0 && wcscmp(b, L"fa") == 0, "two equal languages are separated");
        wcscpy(a, L"R?"); wcscpy(b, L"EN");
        normalize_language_pair(a, b);
        expect(wcscmp(a, L"en") == 0 && wcscmp(b, L"fa") == 0, "invalid codes are replaced; equal ones separated");
    }
}

/* Spelling through the hook with the real tables (4.1): the slips users
   reported, colloquial words left alone, one Backspace that keeps a word,
   and a hand repair that unteaches a typo. */
static void spelling(void) {
    printf("Spelling with the real tables\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04290429u);
    sim_start(L"en", L"fa");
    g_spelling_available =
        load_rank_resource(IDR_EN_RANK_TABLE, &g_english_rank_table) &&
        load_rank_resource(IDR_FA_RANK_TABLE, &g_persian_rank_table) &&
        load_bloom_resource(IDR_EN_FORMS_BLOOM, &g_english_forms_bloom) &&
        load_bloom_resource(IDR_FA_FORMS_BLOOM, &g_persian_forms_bloom);
    expect(g_spelling_available, "the spelling tables load");
    expect(load_rank_resource(IDR_FA_PAIRS, &g_persian_pairs), "the Persian word-pair table loads");
    memset(&g_english_spelling, 0, sizeof(g_english_spelling));
    g_english_spelling.language = KS_LANG_ENGLISH;
    g_english_spelling.ranks = &g_english_rank_table;
    g_english_spelling.words = &g_english_bloom;
    g_english_spelling.common = &g_english_common_bloom;
    g_english_spelling.forms = &g_english_forms_bloom;
    memset(&g_persian_spelling, 0, sizeof(g_persian_spelling));
    g_persian_spelling.language = KS_LANG_PERSIAN;
    g_persian_spelling.ranks = &g_persian_rank_table;
    g_persian_spelling.words = &g_persian_bloom;
    g_persian_spelling.common = &g_persian_common_bloom;
    g_persian_spelling.forms = &g_persian_forms_bloom;
    g_persian_spelling.pairs = NULL;
    g_english_spelling.vocabulary = g_persian_spelling.vocabulary = &g_session_vocabulary;
    g_english_spelling.personal = g_persian_spelling.personal = &g_personal_vocabulary;
    g_english_spelling.rank_adjust = g_persian_spelling.rank_adjust = spell_rank_adjust;
    g_english_spelling.rank_adjust_context = &g_rank_language_en;
    g_persian_spelling.rank_adjust_context = &g_rank_language_fa;
    g_english_spelling.pair_adjust = g_persian_spelling.pair_adjust = spell_pair_adjust;
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    ks_vocab_reset(&g_personal_vocabulary);
    sim_active = (HKL)0x04290429u;

    us_keys("hqgh ");
    expect_screen(L"\u0627\u0635\u0644\u0627 ", "\u0627\u0636\u0644\u0627 is fixed (a homophone)");
    screen_reset();
    us_keys("dyjd ");
    expect_screen(L"\u06CC\u0639\u0646\u06CC ", "two neighbouring keys are fixed");
    screen_reset();
    us_keys("ovti ");
    expect_screen(L"\u062D\u0631\u0641\u0647 ", "a rare word one key from a frequent one is fixed");
    screen_reset();
    us_keys("kldn,ki ");
    expect_screen(L"\u0646\u0645\u06CC\u062F\u0648\u0646\u0647 ", "colloquial Persian stays as written");
    screen_reset();
    us_keys("hordes ");
    expect_screen(L"hordes ", "an inflected English word stays");
    user_switch((uintptr_t)0x04290429u);

    /* One Backspace gives the word back; typing on keeps it for good. */
    g_settings.personal_dictionary = 1;
    remove(sim_dictionary_file);
    wcscpy(g_personal_dictionary_path, L"C:\\Sim\\data\\personal-dictionary.txt");
    g_window = (HWND)(uintptr_t)0x2000;
    sim_keep_posted = 0;
    screen_reset();
    us_keys("dyjd \b");
    expect_screen(L"\u06CC\u063A\u062A\u06CC", "Backspace restores the typed word");
    expect(g_pending_keep[0] && !sim_keep_posted, "the word waits for the next key");
    us_keys(" ");
    expect(sim_keep_posted == 1, "typing on keeps the word");
    save_kept_word();
    expect(ks_vocab_trusted(&g_personal_vocabulary, L"\u06CC\u063A\u062A\u06CC"), "the kept word is the user's");
    {
        char saved[64] = {0};
        FILE *check = fopen(sim_dictionary_file, "rb");
        size_t got = check ? fread(saved, 1, sizeof(saved) - 1, check) : 0;
        if (check) fclose(check);
        expect(got > 0 && strstr(saved, "\xDB\x8C\xD8\xBA\xD8\xAA\xDB\x8C\r\n") != NULL,
               "and it is written to personal-dictionary.txt");
    }
    screen_reset();
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    user_switch((uintptr_t)0x04290429u);
    us_keys("dyjd ");
    expect_screen(L"\u06CC\u063A\u062A\u06CC ", "a kept word is not corrected in a later session");

    /* A second undo before the first word's message was handled: the first
       word is still saved. */
    ks_vocab_reset(&g_personal_vocabulary);
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    sim_keep_posted = 0;
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("dyjd \b ");
    expect(sim_keep_posted == 1 && g_pending_keep_ready, "the first word's message is queued");
    us_keys("hqgh \b");
    expect(ks_vocab_trusted(&g_personal_vocabulary, L"\u06CC\u063A\u062A\u06CC"),
           "a newer undo does not lose the word waiting to be saved");
    expect(g_pending_keep[0] && !g_pending_keep_ready, "the newer word waits for its own next key");
    g_pending_keep[0] = 0;
    /* The setting turned off while a word waits: nothing is saved. */
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    us_keys("hqgh \b ");
    g_settings.personal_dictionary = 0;
    save_kept_word();
    expect(!ks_vocab_trusted(&g_personal_vocabulary, L"\u0627\u0636\u0644\u0627") && !g_pending_keep[0],
           "with the setting off a waiting word is dropped");
    g_settings.personal_dictionary = 1;

    /* A Backspace that goes on deleting keeps nothing. */
    sim_keep_posted = 0;
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("hqgh \b\b");
    expect(!g_pending_keep[0] && !sim_keep_posted, "deleting on after an undo keeps nothing");
    expect(!ks_vocab_trusted(&g_personal_vocabulary, L"\u0627\u0636\u0644\u0627"), "the undone typo is not saved");
    /* A write that fails leaves the word untrusted, so it is tried again. */
    g_personal_dictionary_path[0] = 0;
    ks_vocab_reset(&g_personal_vocabulary);
    personal_dictionary_lines = 0;
    expect(!append_personal_dictionary(L"\u0627\u0636\u0644\u0627") &&
               !ks_vocab_trusted(&g_personal_vocabulary, L"\u0627\u0636\u0644\u0627"),
           "a word that could not be written is not marked as kept");
    remove(sim_dictionary_file);
    g_settings.personal_dictionary = 0;
    g_window = NULL;

    /* Context (4.2). Without the pair table, بزان stays (it could be
       several words); with it, the next word مادری settles it, and the
       previous word is repaired in place. */
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("fi fchk lhnvd ");
    expect_screen(L"\u0628\u0647 \u0628\u0632\u0627\u0646 \u0645\u0627\u062F\u0631\u06CC ",
                  "word by word, the slip cannot be decided");
    g_persian_spelling.pairs = &g_persian_pairs;
    /* A new session: a word typed twice in one session is taken as meant. */
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("fi fchk lhnvd ");
    expect_screen(L"\u0628\u0647 \u0632\u0628\u0627\u0646 \u0645\u0627\u062F\u0631\u06CC ",
                  "the next word settles the previous one");
    g_settings.personal_dictionary = 1;
    us_keys("\b");
    expect_screen(L"\u0628\u0647 \u0628\u0632\u0627\u0646 \u0645\u0627\u062F\u0631\u06CC",
                  "one Backspace restores both words as typed");
    expect(wcscmp(g_pending_keep, L"\u0628\u0632\u0627\u0646") == 0,
           "and the word that was changed is the one kept");
    expect(ks_ignore_list_contains(&g_spelling_ignore, L"\u0628\u0632\u0627\u0646"),
           "it is not changed again this session");
    g_pending_keep[0] = 0;
    g_settings.personal_dictionary = 0;
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("tvr nhji fhan ");
    expect_screen(L"\u0641\u0631\u0642 \u062F\u0627\u0634\u062A\u0647 \u0628\u0627\u0634\u062F ",
                  "a missing letter is found from the words around it");
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("kldn,ki ");
    expect_screen(L"\u0646\u0645\u06CC\u062F\u0648\u0646\u0647 ", "colloquial Persian still stays as written");
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("lk kldn,ki ofvd kdsj ");
    expect_screen(L"\u0645\u0646 \u0646\u0645\u06CC\u062F\u0648\u0646\u0647 \u062E\u0628\u0631\u06CC \u0646\u06CC\u0633\u062A ",
                  "a colloquial sentence stays as written");

    /* The writing memory: a typo that slipped through three times is
       unlearned by one repair by hand. مثلل is ambiguous (مثل / مثلا), so
       spelling leaves it alone. */
    g_settings.learn_writing = 1;
    ks_memory_reset(&g_memory);
    g_memory_active = 1;
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("legg legg legg ");
    expect(ks_memory_word_count(&g_memory, L"\u0645\u062B\u0644\u0644") == 3, "the typo was counted three times");
    screen_reset();
    us_keys("legg \b\bh ");
    expect_screen(L"\u0645\u062B\u0644\u0627 ", "the word is repaired by hand");
    expect(ks_memory_word_count(&g_memory, L"\u0645\u062B\u0644\u0644") == 0, "the hand repair unteaches the typo");
    expect(ks_memory_fix_count(&g_memory, L"\u0645\u062B\u0644\u0644") == 1, "and records the repair");

    /* The user's own phrases (4.2): after "من مثلا" twice, مثلل after من
       is مثلا (alone it is مثل or مثلا). */
    ks_memory_reset(&g_memory);
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("lk legh tvr lk legh ");
    expect(ks_memory_pair_count(&g_memory, L"\u0645\u0646", L"\u0645\u062B\u0644\u0627") == 2,
           "the phrase من مثلا is learned");
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("lk legg ");
    expect_screen(L"\u0645\u0646 \u0645\u062B\u0644\u0627 ", "the user's phrase settles the typo");

    /* A typo the next word repairs is never learned as a phrase; the
       repaired phrase is, so the next time the fix comes at once. */
    ks_memory_reset(&g_memory);
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("fi fchk lhnvd ");
    expect_screen(L"\u0628\u0647 \u0632\u0628\u0627\u0646 \u0645\u0627\u062F\u0631\u06CC ",
                  "the next word repairs the typo");
    expect(ks_memory_pair_count(&g_memory, L"\u0628\u0647", L"\u0628\u0632\u0627\u0646") == 0 &&
               ks_memory_pair_count(&g_memory, L"\u0628\u0632\u0627\u0646", L"\u0645\u0627\u062F\u0631\u06CC") == 0,
           "no phrase with the typo is learned");
    expect(ks_memory_pair_count(&g_memory, L"\u0628\u0647", L"\u0632\u0628\u0627\u0646") == 1 &&
               ks_memory_pair_count(&g_memory, L"\u0632\u0628\u0627\u0646", L"\u0645\u0627\u062F\u0631\u06CC") == 1,
           "the repaired phrase is learned");
    {
        /* Undoing it: the fix is not counted as the user's word, the typo is. */
        unsigned fixed_before = ks_memory_word_count(&g_memory, L"\u0632\u0628\u0627\u0646");
        unsigned typo_before = ks_memory_word_count(&g_memory, L"\u0628\u0632\u0627\u0646");
        us_keys("\b");
        expect(ks_memory_word_count(&g_memory, L"\u0632\u0628\u0627\u0646") + 1 == fixed_before &&
                   ks_memory_word_count(&g_memory, L"\u0628\u0632\u0627\u0646") == typo_before + 1,
               "undoing it gives the counts back");
        g_pending_keep[0] = 0;
    }
    {
        int round;
        for (round = 0; round < 4; ++round) {
            ks_ignore_list_reset(&g_spelling_ignore);
            ks_vocab_reset(&g_session_vocabulary);
            screen_reset();
            user_switch((uintptr_t)0x04290429u);
            us_keys("fi fchk lhnvd ");
            expect_screen(L"\u0628\u0647 \u0632\u0628\u0627\u0646 \u0645\u0627\u062F\u0631\u06CC ",
                          "repeated, the typo is still repaired");
        }
    }
    expect(ks_memory_pair_count(&g_memory, L"\u0628\u0647", L"\u0628\u0632\u0627\u0646") == 0 &&
               ks_memory_pair_count(&g_memory, L"\u0628\u0647", L"\u0632\u0628\u0627\u0646") >= 3,
           "and only the repaired phrase grows");

    /* In a code editor nothing is learned, even at Aggressive. */
    ks_memory_reset(&g_memory);
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    sim_process_name = L"Code.exe";
    g_settings.spelling = KS_SPELL_AGGRESSIVE;
    ++g_settings_generation;
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("tvr nhji fhan ");
    expect(g_memory.pair_count == 0 && ks_memory_word_count(&g_memory, L"\u062F\u0627\u0634\u062A\u0647") == 0,
           "nothing is learned in a code editor");
    sim_process_name = NULL;
    g_settings.spelling = KS_SPELL_BALANCED;
    ++g_settings_generation;

    /* A rejected fix teaches no phrase: undone twice, the pair stays at 0. */
    ks_memory_reset(&g_memory);
    {
        int round;
        for (round = 0; round < 2; ++round) {
            ks_ignore_list_reset(&g_spelling_ignore);
            ks_vocab_reset(&g_session_vocabulary);
            screen_reset();
            user_switch((uintptr_t)0x04290429u);
            us_keys("fi fchk lhnvd \b");
            g_pending_keep[0] = 0;
        }
    }
    expect(ks_memory_pair_count(&g_memory, L"\u0628\u0647", L"\u0632\u0628\u0627\u0646") == 0 &&
               ks_memory_pair_count(&g_memory, L"\u0632\u0628\u0627\u0646", L"\u0645\u0627\u062F\u0631\u06CC") == 0,
           "an undone fix leaves no phrase behind");
    /* A half-space-joined word is not respelled as a piece (the next-word
       check; a fresh memory, so the learned phrase does not fix it first). */
    ks_memory_reset(&g_memory);
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    us_keys("fi~fchk lhnvd ");
    expect_screen(L"\u0628\u0647\u200C\u0628\u0632\u0627\u0646 \u0645\u0627\u062F\u0631\u06CC ",
                  "the second half of a half-space word is left alone");
    /* A long sentence keeps its context: after 40 words the word before is
       still known (the history drops its oldest words, not all of them). */
    ks_memory_reset(&g_memory);
    ks_ignore_list_reset(&g_spelling_ignore);
    ks_vocab_reset(&g_session_vocabulary);
    screen_reset();
    user_switch((uintptr_t)0x04290429u);
    {
        int word;
        for (word = 0; word < 40; ++word) us_keys("ni ");   /* ده ده ده ... */
    }
    screen_reset();
    us_keys("fi fchk lhnvd ");
    expect_screen(L"\u0628\u0647 \u0632\u0628\u0627\u0646 \u0645\u0627\u062F\u0631\u06CC ",
                  "context works after the 31st word of a sentence");

    /* After Tab, the next field gets no context from the last one. */
    user_switch((uintptr_t)0x04090409u);
    screen_reset();
    us_keys("teh\t");
    expect_screen(L"the\t", "a fix at Tab");
    expect(g_spell_prev[0] == 0, "a fix that ends with Tab gives no context to the next field");
    g_memory_active = 0;
    g_settings.learn_writing = 0;
    g_spelling_available = 0;
}

static void late_switch(void) {
    printf("Applications that switch late (en / fa)\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04290429u);
    sim_start(L"en", L"fa");
    /* The app takes five more keys to honour a switch: the hook types
       those keys in the requested layout itself. */
    sim_switch_delay = 5;
    us_keys("sghl h");
    /* The app still has the US layout: the hook types the key for the
       Persian layout it was asked to switch to. */
    expect(sim_active == (HKL)0x04090409u, "the application has not switched yet");
    expect_screen(L"\u0633\u0644\u0627\u0645 \u0627", "a key typed before a late switch arrives is typed by the hook in Persian");
    us_keys("ello ");
    sim_switch_delay = 0;
    sim_switch_pending = NULL;
    expect_screen(L"\u0633\u0644\u0627\u0645 hello ", "and the word ends right");
}

static void russian_details(void) {
    printf("English / Russian details\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04190419u);
    sim_start(L"en", L"ru");
    us_keys("do it. ");
    expect_screen(L"do it. ", "an English word before a full stop stays (the full stop is \u044E in Russian)");
    {
        int i;
        int copies = 0;
        for (i = 0; i < g_language_choice_count; ++i) copies += wcscmp(g_language_choices[i].code, L"ru") == 0;
        expect(copies == 1 && wcsstr(find_language_choice(L"ru")->path, L"\\ru.kslang") != NULL,
               "a pack saved under another file name is not listed");
    }
    /* A dead key, then a focus change: the next word is followed again. */
    screen_reset();
    user_switch(0x04090409u);
    g_dead_key_word = 1;
    engine_reset_for_focus();
    us_keys("ghbdtn ");
    expect_screen(L"\u043F\u0440\u0438\u0432\u0435\u0442 ", "a focus change forgets a dead key");
}

static void english_arabic(void) {
    printf("Pair en / ar (lam-alef key)\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04010401u);
    sim_start(L"en", L"ar");
    expect(slot_from_layout((HKL)0x04010401u) == KS_SLOT_B, "the Arabic keyboard is slot B");
    expect(!slot_is(KS_SLOT_B, KS_LANG_PERSIAN), "Arabic is not Persian");
    us_keys("label ");
    expect_screen(L"label ", "a word with the lam-alef key is left alone, not split");
    screen_reset();
    us_keys("td ");   /* \u0641 \u064A on the Arabic keyboard */
    expect_screen(L"\u0641\u064A ", "an Arabic word typed on the US layout is repaired");
    /* An application that never switches: the lam-alef key of "about" is
       still typed as b for the English the user is writing. */
    screen_reset();
    user_switch(0x04010401u);
    sim_honour_switch = 0;
    us_keys("hello about ");
    sim_honour_switch = 1;
    expect_screen(L"hello about ", "a pending switch types the lam-alef key for English too");
    /* The other way round: Arabic asked for and never switched to. The
       lam-alef key is typed as its two letters. */
    screen_reset();
    user_switch(0x04090409u);
    sim_honour_switch = 0;
    us_keys("ugn sbl ");
    sim_honour_switch = 1;
    expect_screen(L"\u0639\u0644\u0649 \u0633\u0644\u0627\u0645 ", "the hook types the lam-alef key as its two letters");
    /* A click after an unreadable key: the next word is followed again. */
    screen_reset();
    user_switch(0x04090409u);
    us_keys("lab");
    {
        MSLLHOOKSTRUCT click;
        memset(&click, 0, sizeof(click));
        mouse_hook_proc(0, WM_LBUTTONDOWN, (LPARAM)&click);
    }
    us_keys("ugn ");   /* \u0639\u0644\u0649 on the Arabic keyboard */
    expect(wcsstr(screen, L"\u0639\u0644\u0649 ") != NULL, "a click forgets the skipped word");
    /* Persian helpers stay away from Arabic. */
    screen_reset();
    user_switch(0x04010401u);
    g_settings.persian_letters = 1;
    press(0x27, 0);   /* \u0643, Arabic kaf */
    expect(screen[0] == 0x0643, "the Arabic keyboard keeps its own kaf");
}

static void english_french(void) {
    printf("Pair en / fr (AZERTY: letters on the digit row)\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x040C040Cu);
    sim_start(L"en", L"fr");
    expect(slot_from_layout((HKL)0x040C040Cu) == KS_SLOT_B, "the AZERTY keyboard is slot B");
    us_keys("page 10 bonjour ");
    expect_screen(L"page 10 bonjour ", "numbers stay numbers");
    screen_reset();
    us_keys("0 bonjour ");
    expect_screen(L"0 bonjour ", "a lone digit is not the French word \u00E0");
    screen_reset();
    us_keys("merci 0 de ");
    expect_screen(L"merci 0 de ", "not even between French words");
}

static void pack_added_later(void) {
    printf("A pack copied in while the program runs\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04290429u);
    sim_pack_count = 3;
    sim_start(L"en", L"fa");
    expect(find_language_choice(L"ar") == NULL, "not there before");
    sim_packs[3] = L"ar.kslang";
    sim_pack_count = 4;
    refresh_language_lists();
    expect(find_language_choice(L"ar") != NULL, "listed when a language list is opened");
}

static void language_lists(void) {
    SIM_COMBO *first = &sim_combos[0];
    SIM_COMBO *second = &sim_combos[1];
    printf("The dashboard's language lists\n");
    install_layouts(2, (uintptr_t)0x04090409u, (uintptr_t)0x04290429u);
    sim_start(L"en", L"fa");
    g_language_first_combo = (HWND)first;
    g_language_second_combo = (HWND)second;
    g_language_mode = (HWND)&sim_combos[2];
    g_window = (HWND)(uintptr_t)0x2000;   /* messages to it are dropped by PostMessageW */
    g_ui_ready = 1;
    g_settings.language_mode = 1;   /* "Prefer Persian" (the second language) */
    fill_language_controls();
    SendMessageW(g_language_mode, CB_SETCURSEL, 1, 0);
    expect(first->selection == sim_combo_find(first, L"en") && second->selection == sim_combo_find(second, L"fa"),
           "the lists show English and Persian");
    /* Persian picked in the first list with the mouse; the list reports the
       close before the selection (Windows may send either order). */
    sim_notify(IDC_LANGUAGE_FIRST, first, CBN_DROPDOWN);
    first->dropped = 1;
    first->selection = sim_combo_find(first, L"fa");
    first->dropped = 0;
    sim_notify(IDC_LANGUAGE_FIRST, first, CBN_CLOSEUP);
    sim_notify(IDC_LANGUAGE_FIRST, first, CBN_SELCHANGE);
    main_window_proc((HWND)(uintptr_t)0x2000, WM_APP_LANGUAGES, 0, 0);
    expect(wcscmp(g_settings.language_first, L"fa") == 0 && wcscmp(g_settings.language_second, L"en") == 0,
           "picking the other list's language swaps the pair, once");
    expect(g_settings.language_mode == 2 && slot_is(KS_SLOT_A, KS_LANG_PERSIAN),
           "Persian is still the preferred language");
    expect(second->selection == sim_combo_find(second, L"en"), "the second list shows English");
    /* Arrows on the closed first list, then the list is opened: the choice
       made with the arrows is applied, not thrown away. */
    first->selection = sim_combo_find(first, L"ru");
    sim_notify(IDC_LANGUAGE_FIRST, first, CBN_SELCHANGE);
    expect(wcscmp(g_settings.language_first, L"fa") == 0, "arrows on a closed list do not load anything yet");
    sim_notify(IDC_LANGUAGE_FIRST, first, CBN_DROPDOWN);
    main_window_proc((HWND)(uintptr_t)0x2000, WM_APP_LANGUAGES, 0, 0);
    expect(wcscmp(g_settings.language_first, L"ru") == 0 && wcscmp(language_name(KS_SLOT_A), L"Russian") == 0,
           "opening the list applies the choice made with the arrows");
    g_ui_ready = 0;
    g_window = NULL;
    g_language_first_combo = g_language_second_combo = g_language_mode = NULL;
}

int main(void) {
    setlocale(LC_ALL, "C.UTF-8");
    build_layouts();
    sim_packs[0] = L"xx.kslang";      /* a copy of the Russian pack under another name */
    sim_packs[1] = L"ru.kslang";
    sim_packs[2] = L"de.kslang";
    sim_pack_count = 3;
    load_builtin();
    english_persian(L"en", L"fa");
    english_persian(L"fa", L"en");
    english_russian();
    russian_details();
    german_english();
    missing_pack();
    late_switch();
    spelling();
    sim_packs[3] = L"ar.kslang";
    sim_packs[4] = L"fr.kslang";
    sim_pack_count = 5;   /* pack_added_later() starts from the first three */
    english_arabic();
    english_french();
    pack_added_later();
    language_lists();
    release_language_pair();
    if (failures) {
        printf("%d simulation check(s) FAILED\n", failures);
        return 1;
    }
    printf("All hook simulation tests passed.\n");
    return 0;
}
