#include "typing.h"

#include <string.h>
#include <wctype.h>

/* ---- Character shaping ------------------------------------------------ */

wchar_t ks_persian_form(wchar_t c) {
    switch (c) {
        case 0x064A: return 0x06CC; /* ي → ی */
        case 0x0649: return 0x06CC; /* ى → ی */
        case 0x0643: return 0x06A9; /* ك → ک */
        default: break;
    }
    if (c >= 0x0660 && c <= 0x0669) return (wchar_t)(0x06F0 + (c - 0x0660));
    return c;
}

wchar_t ks_persian_digit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return (wchar_t)(0x06F0 + (c - L'0'));
    if (c >= 0x0660 && c <= 0x0669) return (wchar_t)(0x06F0 + (c - 0x0660));
    return c;
}

wchar_t ks_latin_digit(wchar_t c) {
    if (c >= 0x06F0 && c <= 0x06F9) return (wchar_t)(L'0' + (c - 0x06F0));
    if (c >= 0x0660 && c <= 0x0669) return (wchar_t)(L'0' + (c - 0x0660));
    return c;
}

wchar_t ks_persian_punctuation(wchar_t c) {
    switch (c) {
        case L'?': return 0x061F; /* ؟ */
        case L',': return 0x060C; /* ، */
        case L';': return 0x061B; /* ؛ */
        default: return c;
    }
}

wchar_t ks_latin_punctuation(wchar_t c) {
    switch (c) {
        case 0x061F: return L'?';
        case 0x060C: return L',';
        case 0x061B: return L';';
        default: return c;
    }
}

int ks_is_digit_any(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= 0x0660 && c <= 0x0669) ||
           (c >= 0x06F0 && c <= 0x06F9);
}

int ks_is_persian_letter(wchar_t c) {
    /* Arabic block letters (including the Persian additions), minus the
       digits and punctuation that share the block. */
    if (c >= 0x0621 && c <= 0x064A) return 1;
    if (c >= 0x0671 && c <= 0x06D3) return 1;
    if (c == 0x200C) return 0;
    return 0;
}

int ks_is_latin_letter(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

wchar_t ks_shape_digit(wchar_t c, int policy, KS_LANGUAGE layout) {
    if (!ks_is_digit_any(c)) return c;
    switch (policy) {
        case KS_DIGITS_PERSIAN: return ks_persian_digit(c);
        case KS_DIGITS_LATIN: return ks_latin_digit(c);
        case KS_DIGITS_BY_LAYOUT:
            if (layout == KS_LANG_PERSIAN) return ks_persian_digit(c);
            if (layout == KS_LANG_ENGLISH) return ks_latin_digit(c);
            return c;
        default: return c;
    }
}

static int is_space(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x200C || c == 0x00A0;
}

/* Counted per word, not per letter, so one long URL or e-mail address does
   not outweigh the Persian sentence around it. */
int ks_text_is_persian(const wchar_t *text) {
    int persian = 0;
    int latin = 0;
    if (!text) return 0;
    while (*text) {
        int has_persian = 0;
        int has_latin = 0;
        while (is_space(*text)) ++text;
        if (!*text) break;
        for (; *text && !is_space(*text); ++text) {
            if (ks_is_persian_letter(*text)) has_persian = 1;
            else if (ks_is_latin_letter(*text)) has_latin = 1;
        }
        if (has_persian && !has_latin) ++persian;
        else if (has_latin && !has_persian) ++latin;
    }
    return persian > latin;
}

/*
 * Digits and punctuation are shaped per whitespace-delimited token: a token
 * that contains Latin letters (a URL, an e-mail address, a product code, a
 * fragment of code) keeps its ASCII digits and marks even inside a Persian
 * paragraph; a token without them follows the paragraph's script. Letters
 * are normalised everywhere.
 */
int ks_clean_text(wchar_t *text, int letters, int digits, int punctuation) {
    int changed = 0;
    int persian;
    wchar_t *token;
    if (!text) return 0;
    persian = ks_text_is_persian(text);
    token = text;
    while (*token) {
        wchar_t *end = token;
        wchar_t *cursor;
        int latin = 0;
        while (is_space(*token)) ++token;
        if (!*token) break;
        for (end = token; *end && !is_space(*end); ++end)
            if (ks_is_latin_letter(*end)) latin = 1;
        for (cursor = token; cursor < end; ++cursor) {
            wchar_t c = *cursor;
            wchar_t shaped = c;
            if (letters) shaped = ks_persian_form(shaped);
            if (!latin) {
                shaped = ks_shape_digit(shaped, digits, persian ? KS_LANG_PERSIAN : KS_LANG_ENGLISH);
                if (punctuation && persian) shaped = ks_persian_punctuation(shaped);
            }
            if (shaped != c) {
                *cursor = shaped;
                changed = 1;
            }
        }
        token = end;
    }
    return changed;
}

/* ---- Auto-capitalisation ---------------------------------------------- */

int ks_is_abbreviation(const wchar_t *word) {
    static const wchar_t *const abbreviations[] = {
        L"mr", L"mrs", L"ms", L"dr", L"prof", L"sr", L"jr", L"st", L"mt", L"ft",
        L"etc", L"vs", L"eg", L"ie", L"cf", L"approx", L"dept", L"inc", L"ltd",
        L"corp", L"fig", L"vol", L"pp", L"avg", L"tel", L"ext", L"www", L"com",
        L"org", L"net", L"io", L"ir", L"uk", L"de", L"ai", L"dev", L"app", L"exe",
        L"dll", L"txt", L"jpg", L"png", L"pdf", L"doc", L"docx", L"xlsx", L"zip",
        L"html", L"js", L"py", L"cs", L"cpp", L"jan", L"feb", L"mar", L"apr",
        L"jun", L"jul", L"aug", L"sep", L"sept", L"oct", L"nov", L"dec", L"ave",
        L"blvd", L"gen", L"col", L"capt", L"lt", L"sgt", L"rev", L"hon", L"univ",
        L"a", L"b", L"c", L"d", L"e", L"f", L"g", L"h", L"i", L"j", L"k", L"l",
        L"m", L"n", L"o", L"p", L"q", L"r", L"s", L"t", L"u", L"v", L"w", L"x",
        L"y", L"z"
    };
    size_t i;
    if (!word || !*word) return 1;
    for (i = 0; i < sizeof(abbreviations) / sizeof(abbreviations[0]); ++i)
        if (wcscmp(word, abbreviations[i]) == 0) return 1;
    return 0;
}

/* ---- Jalali calendar --------------------------------------------------- */

/* Truncating division and remainder, exactly as the reference algorithm
   (jalaali-js, `~~(a / b)`) defines them; C's operators already truncate. */
static long fdiv(long a, long b) {
    return a / b;
}

static long fmod_(long a, long b) {
    return a - (a / b) * b;
}

static long g2d(long gy, long gm, long gd) {
    long d = fdiv((gy + fdiv(gm - 8, 6) + 100100) * 1461, 4) +
             fdiv(153 * fmod_(gm + 9, 12) + 2, 5) + gd - 34840408;
    d = d - fdiv(fdiv(gy + 100100 + fdiv(gm - 8, 6), 100) * 3, 4) + 752;
    return d;
}

static void d2g(long jdn, long *gy, long *gm, long *gd) {
    long j = 4 * jdn + 139361631;
    long i;
    j = j + fdiv(fdiv(4 * jdn + 183187720, 146097) * 3, 4) * 4 - 3908;
    i = fdiv(fmod_(j, 1461), 4) * 5 + 308;
    *gd = fdiv(fmod_(i, 153), 5) + 1;
    *gm = fmod_(fdiv(i, 153), 12) + 1;
    *gy = fdiv(j, 1461) - 100100 + fdiv(8 - *gm, 6);
}

/*
 * The 33-year cycle algorithm (Kazimierz M. Borkowski, as implemented in
 * jalaali-js), valid for Jalali years −61 .. 3177.
 */
static void jal_cal(long jy, int without_leap, long *leap, long *gy, long *march) {
    static const long breaks[] = {
        -61, 9, 38, 199, 426, 686, 756, 818, 1111, 1181, 1210, 1635,
        2060, 2097, 2192, 2262, 2324, 2394, 2456, 3178
    };
    const int count = (int)(sizeof(breaks) / sizeof(breaks[0]));
    long leap_j = -14;
    long jp = breaks[0];
    long jm = 0;
    long jump = 0;
    long leap_g;
    long n;
    int i;
    *gy = jy + 621;
    *leap = 0;
    if (jy < jp || jy >= breaks[count - 1]) {
        *march = 21;
        return;
    }
    for (i = 1; i < count; ++i) {
        jm = breaks[i];
        jump = jm - jp;
        if (jy < jm) break;
        leap_j = leap_j + fdiv(jump, 33) * 8 + fdiv(fmod_(jump, 33), 4);
        jp = jm;
    }
    n = jy - jp;
    leap_j = leap_j + fdiv(n, 33) * 8 + fdiv(fmod_(n, 33) + 3, 4);
    if (fmod_(jump, 33) == 4 && jump - n == 4) leap_j += 1;
    leap_g = fdiv(*gy, 4) - fdiv((fdiv(*gy, 100) + 1) * 3, 4) - 150;
    *march = 20 + leap_j - leap_g;
    if (!without_leap) {
        if (jump - n < 6) n = n - jump + fdiv(jump + 4, 33) * 33;
        *leap = fmod_(fmod_(n + 1, 33) - 1, 4);
        if (*leap == -1) *leap = 4;
    }
}

void ks_gregorian_to_jalali(int gy, int gm, int gd, int *jy, int *jm, int *jd) {
    long jdn = g2d(gy, gm, gd);
    long year, month, day, leap, march, k;
    long g_year, g_month, g_day;
    d2g(jdn, &g_year, &g_month, &g_day);
    year = g_year - 621;
    jal_cal(year, 0, &leap, &g_year, &march);
    k = jdn - g2d(g_year, 3, march);
    if (k >= 0) {
        if (k <= 185) {
            month = 1 + fdiv(k, 31);
            day = fmod_(k, 31) + 1;
            goto done;
        }
        k -= 186;
    } else {
        year -= 1;
        k += 179;
        if (leap == 1) k += 1;
    }
    month = 7 + fdiv(k, 30);
    day = fmod_(k, 30) + 1;
done:
    if (jy) *jy = (int)year;
    if (jm) *jm = (int)month;
    if (jd) *jd = (int)day;
}

void ks_jalali_to_gregorian(int jy, int jm, int jd, int *gy, int *gm, int *gd) {
    long leap, g_year, march, jdn;
    long year, month, day;
    jal_cal(jy, 1, &leap, &g_year, &march);
    jdn = g2d(g_year, 3, march) + (jm - 1) * 31 - fdiv(jm, 7) * (jm - 7) + jd - 1;
    d2g(jdn, &year, &month, &day);
    if (gy) *gy = (int)year;
    if (gm) *gm = (int)month;
    if (gd) *gd = (int)day;
}

const wchar_t *ks_jalali_month_name(int month) {
    static const wchar_t *const names[] = {
        L"فروردین", L"اردیبهشت", L"خرداد", L"تیر", L"مرداد", L"شهریور",
        L"مهر", L"آبان", L"آذر", L"دی", L"بهمن", L"اسفند"
    };
    if (month < 1 || month > 12) return L"";
    return names[month - 1];
}

const wchar_t *ks_persian_weekday_name(int weekday) {
    static const wchar_t *const names[] = {
        L"یکشنبه", L"دوشنبه", L"سه\x200cشنبه", L"چهارشنبه", L"پنجشنبه", L"جمعه", L"شنبه"
    };
    if (weekday < 0 || weekday > 6) return L"";
    return names[weekday];
}

const wchar_t *ks_english_weekday_name(int weekday) {
    static const wchar_t *const names[] = {
        L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"
    };
    if (weekday < 0 || weekday > 6) return L"";
    return names[weekday];
}

const wchar_t *ks_english_month_name(int month) {
    static const wchar_t *const names[] = {
        L"January", L"February", L"March", L"April", L"May", L"June", L"July",
        L"August", L"September", L"October", L"November", L"December"
    };
    if (month < 1 || month > 12) return L"";
    return names[month - 1];
}

/* ---- Macros -------------------------------------------------------------- */

static size_t put_text(wchar_t *out, size_t capacity, size_t used, const wchar_t *text, int persian_digits) {
    for (; *text; ++text) {
        if (used + 1 >= capacity) break;
        out[used++] = persian_digits ? ks_persian_digit(*text) : *text;
    }
    return used;
}

static size_t put_number(wchar_t *out, size_t capacity, size_t used, long value, int width, int persian_digits) {
    wchar_t digits[24];
    int length = 0;
    int i;
    if (value < 0) value = -value;
    do {
        digits[length++] = (wchar_t)(L'0' + value % 10);
        value /= 10;
    } while (value && length < 20);
    while (length < width && length < 20) digits[length++] = L'0';
    for (i = length - 1; i >= 0; --i) {
        if (used + 1 >= capacity) break;
        out[used++] = persian_digits ? ks_persian_digit(digits[i]) : digits[i];
    }
    return used;
}

static int macro_is(const wchar_t *name, size_t length, const wchar_t *candidate) {
    return wcslen(candidate) == length && wcsncmp(name, candidate, length) == 0;
}

size_t ks_expand_macros(const wchar_t *template_text, const KS_DATE_INFO *now,
                        wchar_t *output, size_t capacity) {
    size_t used = 0;
    const wchar_t *cursor;
    int jy = 0, jm = 0, jd = 0;
    if (!output || capacity == 0) return 0;
    output[0] = 0;
    if (!template_text) return 0;
    if (now) ks_gregorian_to_jalali(now->year, now->month, now->day, &jy, &jm, &jd);
    for (cursor = template_text; *cursor && used + 1 < capacity;) {
        if (cursor[0] == L'\\' && cursor[1] == L'n') {
            output[used++] = L'\n';
            cursor += 2;
            continue;
        }
        if (cursor[0] == L'\\' && cursor[1] == L't') {
            output[used++] = L'\t';
            cursor += 2;
            continue;
        }
        if (cursor[0] == L'{' && cursor[1] == L'{') {
            output[used++] = L'{';
            cursor += 2;
            continue;
        }
        if (*cursor == L'{') {
            const wchar_t *end = wcschr(cursor + 1, L'}');
            const wchar_t *name = cursor + 1;
            size_t length;
            int handled = 1;
            if (!end || !now) {
                output[used++] = *cursor++;
                continue;
            }
            length = (size_t)(end - name);
            if (macro_is(name, length, L"jdate")) {
                used = put_number(output, capacity, used, jy, 4, 1);
                used = put_text(output, capacity, used, L"/", 0);
                used = put_number(output, capacity, used, jm, 2, 1);
                used = put_text(output, capacity, used, L"/", 0);
                used = put_number(output, capacity, used, jd, 2, 1);
            } else if (macro_is(name, length, L"jdate:en")) {
                used = put_number(output, capacity, used, jy, 4, 0);
                used = put_text(output, capacity, used, L"/", 0);
                used = put_number(output, capacity, used, jm, 2, 0);
                used = put_text(output, capacity, used, L"/", 0);
                used = put_number(output, capacity, used, jd, 2, 0);
            } else if (macro_is(name, length, L"jdate:long")) {
                used = put_number(output, capacity, used, jd, 0, 1);
                used = put_text(output, capacity, used, L" ", 0);
                used = put_text(output, capacity, used, ks_jalali_month_name(jm), 0);
                used = put_text(output, capacity, used, L" ", 0);
                used = put_number(output, capacity, used, jy, 0, 1);
            } else if (macro_is(name, length, L"jweekday")) {
                used = put_text(output, capacity, used, ks_persian_weekday_name(now->weekday), 0);
            } else if (macro_is(name, length, L"jyear")) {
                used = put_number(output, capacity, used, jy, 0, 1);
            } else if (macro_is(name, length, L"jmonth")) {
                used = put_text(output, capacity, used, ks_jalali_month_name(jm), 0);
            } else if (macro_is(name, length, L"jday")) {
                used = put_number(output, capacity, used, jd, 0, 1);
            } else if (macro_is(name, length, L"date")) {
                used = put_number(output, capacity, used, now->year, 4, 0);
                used = put_text(output, capacity, used, L"-", 0);
                used = put_number(output, capacity, used, now->month, 2, 0);
                used = put_text(output, capacity, used, L"-", 0);
                used = put_number(output, capacity, used, now->day, 2, 0);
            } else if (macro_is(name, length, L"date:long")) {
                used = put_number(output, capacity, used, now->day, 0, 0);
                used = put_text(output, capacity, used, L" ", 0);
                used = put_text(output, capacity, used, ks_english_month_name(now->month), 0);
                used = put_text(output, capacity, used, L" ", 0);
                used = put_number(output, capacity, used, now->year, 0, 0);
            } else if (macro_is(name, length, L"weekday")) {
                used = put_text(output, capacity, used, ks_english_weekday_name(now->weekday), 0);
            } else if (macro_is(name, length, L"time") || macro_is(name, length, L"time:fa")) {
                int persian = length == 7;
                used = put_number(output, capacity, used, now->hour, 2, persian);
                used = put_text(output, capacity, used, L":", 0);
                used = put_number(output, capacity, used, now->minute, 2, persian);
            } else if (macro_is(name, length, L"year")) {
                used = put_number(output, capacity, used, now->year, 0, 0);
            } else if (macro_is(name, length, L"month")) {
                used = put_number(output, capacity, used, now->month, 2, 0);
            } else if (macro_is(name, length, L"day")) {
                used = put_number(output, capacity, used, now->day, 2, 0);
            } else if (macro_is(name, length, L"hour")) {
                used = put_number(output, capacity, used, now->hour, 2, 0);
            } else if (macro_is(name, length, L"minute")) {
                used = put_number(output, capacity, used, now->minute, 2, 0);
            } else if (macro_is(name, length, L"n")) {
                output[used++] = L'\n';
            } else if (macro_is(name, length, L"t")) {
                output[used++] = L'\t';
            } else {
                handled = 0;
            }
            if (handled) {
                cursor = end + 1;
                continue;
            }
            output[used++] = *cursor++;
            continue;
        }
        output[used++] = *cursor++;
    }
    output[used] = 0;
    return used;
}

/* ---- Snippets ------------------------------------------------------------ */

static int snippet_key_valid(const wchar_t *key) {
    size_t length = wcslen(key);
    size_t i;
    if (length == 0 || length > KS_SNIPPET_KEY_MAX) return 0;
    for (i = 0; i < length; ++i) {
        wchar_t c = key[i];
        if (c == L' ' || c == L'\t' || c == L'=' || c == L'{' || c == L'}' ||
            c == L'\r' || c == L'\n' || c < 0x20) return 0;
    }
    return 1;
}

static void trim(wchar_t *text) {
    size_t length = wcslen(text);
    size_t start = 0;
    while (length > 0 && (text[length - 1] == L' ' || text[length - 1] == L'\t' ||
                          text[length - 1] == L'\r' || text[length - 1] == L'\n'))
        text[--length] = 0;
    while (text[start] == L' ' || text[start] == L'\t') ++start;
    if (start) memmove(text, text + start, (length - start + 1) * sizeof(wchar_t));
}

int ks_snippets_parse(KS_SNIPPET_TABLE *table, const wchar_t *content) {
    const wchar_t *line;
    if (!table) return 0;
    table->count = 0;
    if (!content) return 0;
    line = content;
    if (*line == 0xFEFF) ++line;
    while (*line) {
        const wchar_t *end = line;
        const wchar_t *separator;
        size_t length;
        wchar_t key[KS_SNIPPET_KEY_MAX + 2];
        wchar_t text[KS_SNIPPET_TEXT_MAX + 1];
        size_t key_length;
        size_t text_length;
        int i;
        KS_SNIPPET *slot = NULL;
        while (*end && *end != L'\n') ++end;
        length = (size_t)(end - line);
        if (length == 0 || line[0] == L'#') goto next;
        separator = NULL;
        for (i = 0; (size_t)i < length; ++i) {
            if (line[i] == L'\t' || line[i] == L'=') {
                separator = line + i;
                break;
            }
        }
        if (!separator) goto next;
        key_length = (size_t)(separator - line);
        if (key_length > KS_SNIPPET_KEY_MAX + 1) goto next;
        memcpy(key, line, key_length * sizeof(wchar_t));
        key[key_length] = 0;
        trim(key);
        if (!snippet_key_valid(key)) goto next;
        text_length = length - key_length - 1;
        if (text_length > KS_SNIPPET_TEXT_MAX) text_length = KS_SNIPPET_TEXT_MAX;
        memcpy(text, separator + 1, text_length * sizeof(wchar_t));
        text[text_length] = 0;
        trim(text);
        if (!text[0]) goto next;
        for (i = 0; i < table->count; ++i) {
            if (wcscmp(table->items[i].key, key) == 0) {
                slot = &table->items[i];
                break;
            }
        }
        if (!slot) {
            if (table->count >= KS_SNIPPET_MAX) goto next;
            slot = &table->items[table->count++];
        }
        wcscpy(slot->key, key);
        wcscpy(slot->text, text);
    next:
        line = *end ? end + 1 : end;
    }
    return table->count;
}

const KS_SNIPPET *ks_snippet_find(const KS_SNIPPET_TABLE *table, const wchar_t *key) {
    int i;
    if (!table || !key || !*key) return NULL;
    for (i = 0; i < table->count; ++i)
        if (wcscmp(table->items[i].key, key) == 0) return &table->items[i];
    return NULL;
}

/* ---- Statistics --------------------------------------------------------- */

void ks_stats_roll_day(KS_STATS *stats, int year, int month, int day) {
    if (!stats) return;
    if (stats->today_year == year && stats->today_month == month && stats->today_day == day) return;
    stats->today_year = year;
    stats->today_month = month;
    stats->today_day = day;
    stats->today_layout = 0;
    stats->today_spelling = 0;
    stats->today_keys = 0;
    stats->days_active += 1;
}

void ks_stats_observe_correction(KS_STATS *stats, const wchar_t *original, int spelling) {
    int i;
    int weakest = 0;
    if (!stats) return;
    if (spelling) {
        stats->total_spelling += 1;
        stats->today_spelling += 1;
    } else {
        stats->total_layout += 1;
        stats->today_layout += 1;
    }
    if (!original || !*original || wcslen(original) > KS_MAX_WORD) return;
    for (i = 0; i < stats->word_count; ++i) {
        if (wcscmp(stats->words[i].word, original) == 0) {
            stats->words[i].count += 1;
            return;
        }
        if (stats->words[i].count < stats->words[weakest].count) weakest = i;
    }
    if (stats->word_count < KS_STATS_WORDS) {
        i = stats->word_count++;
    } else {
        i = weakest;
    }
    wcscpy(stats->words[i].word, original);
    stats->words[i].count = 1;
}

int ks_stats_top(const KS_STATS *stats, int rank, const wchar_t **word, unsigned *count) {
    int order[KS_STATS_WORDS];
    int i, j;
    if (!stats || rank < 0 || rank >= stats->word_count) return 0;
    for (i = 0; i < stats->word_count; ++i) order[i] = i;
    /* Insertion sort by count, descending; 64 entries at most. */
    for (i = 1; i < stats->word_count; ++i) {
        int key = order[i];
        for (j = i - 1; j >= 0 && stats->words[order[j]].count < stats->words[key].count; --j)
            order[j + 1] = order[j];
        order[j + 1] = key;
    }
    if (word) *word = stats->words[order[rank]].word;
    if (count) *count = stats->words[order[rank]].count;
    return 1;
}

long ks_stats_seconds_saved(const KS_STATS *stats) {
    if (!stats) return 0;
    return (stats->total_layout + stats->total_spelling) * 4;
}
