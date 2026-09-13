#ifndef KEYSWITCHFIX_TYPING_H
#define KEYSWITCHFIX_TYPING_H

/*
 * Typing helpers: platform-free logic behind the 3.0 features — Persian
 * character clean-up, digit and punctuation shaping, snippets with date
 * macros (Jalali calendar), English auto-capitalisation rules, and usage
 * statistics. Everything here is pure C and unit-tested natively.
 */

#include <stddef.h>
#include <wchar.h>

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Character shaping ------------------------------------------------ */

/* Arabic yeh/kaf/alef-maksura and Arabic-Indic digits → their Persian forms;
   every other character unchanged. */
wchar_t ks_persian_form(wchar_t character);
/* ASCII or Arabic-Indic digit → Persian digit (U+06F0..U+06F9). */
wchar_t ks_persian_digit(wchar_t character);
/* Persian or Arabic-Indic digit → ASCII digit. */
wchar_t ks_latin_digit(wchar_t character);
/* ? , ; → ؟ ، ؛ */
wchar_t ks_persian_punctuation(wchar_t character);
/* ؟ ، ؛ → ? , ; */
wchar_t ks_latin_punctuation(wchar_t character);
int ks_is_digit_any(wchar_t character);
int ks_is_persian_letter(wchar_t character);
int ks_is_latin_letter(wchar_t character);

/* Digit policy: 0 = as the layout types, 1 = follow the layout (Persian
   layout → Persian digits, English layout → ASCII), 2 = always Persian,
   3 = always ASCII. */
#define KS_DIGITS_OFF 0
#define KS_DIGITS_BY_LAYOUT 1
#define KS_DIGITS_PERSIAN 2
#define KS_DIGITS_LATIN 3
wchar_t ks_shape_digit(wchar_t character, int policy, KS_LANGUAGE layout);

/* Whole-text clean-up (the Ctrl+Win+X hotkey). `digits` uses the policy
   above with the text's own dominant script as "layout". Punctuation is
   shaped only when the text is predominantly Persian. Returns 1 when the
   text changed. */
int ks_clean_text(wchar_t *text, int letters, int digits, int punctuation);
/* 1 when more letters are Persian than Latin. */
int ks_text_is_persian(const wchar_t *text);

/* ---- Auto-capitalisation ---------------------------------------------- */

/* Lower-case English word after which a period does not end a sentence. */
int ks_is_abbreviation(const wchar_t *word);

/* ---- Jalali calendar and macros --------------------------------------- */

typedef struct KS_DATE_INFO {
    int year, month, day;      /* Gregorian */
    int hour, minute, second;
    int weekday;               /* 0 = Sunday .. 6 = Saturday */
} KS_DATE_INFO;

void ks_gregorian_to_jalali(int gy, int gm, int gd, int *jy, int *jm, int *jd);
void ks_jalali_to_gregorian(int jy, int jm, int jd, int *gy, int *gm, int *gd);
const wchar_t *ks_jalali_month_name(int month);
const wchar_t *ks_persian_weekday_name(int weekday);
const wchar_t *ks_english_weekday_name(int weekday);
const wchar_t *ks_english_month_name(int month);

/*
 * Expands {macros} in a snippet: {jdate} ۱۴۰۵/۰۶/۲۱, {jdate:en} 1405/06/21,
 * {jdate:long} ۲۱ شهریور ۱۴۰۵, {jweekday} شنبه, {jyear} {jmonth} {jday},
 * {date} 2026-09-12, {date:long} 12 September 2026, {weekday} Saturday,
 * {time} 14:05, {time:fa} ۱۴:۰۵, {year} {month} {day} {hour} {minute},
 * {n} newline (also \n), {t} tab, {{ literal brace. Unknown macros are
 * kept verbatim. Returns the number of characters written.
 */
size_t ks_expand_macros(const wchar_t *template_text, const KS_DATE_INFO *now,
                        wchar_t *output, size_t capacity);

/* ---- Snippets ---------------------------------------------------------- */

#define KS_SNIPPET_MAX 256
#define KS_SNIPPET_KEY_MAX KS_MAX_WORD
#define KS_SNIPPET_TEXT_MAX 400

typedef struct KS_SNIPPET {
    wchar_t key[KS_SNIPPET_KEY_MAX + 1];
    wchar_t text[KS_SNIPPET_TEXT_MAX + 1];
} KS_SNIPPET;

typedef struct KS_SNIPPET_TABLE {
    KS_SNIPPET items[KS_SNIPPET_MAX];
    int count;
} KS_SNIPPET_TABLE;

/*
 * Parses "key<TAB>text" or "key = text" lines (first '=' or tab separates;
 * '#' starts a comment; blank lines ignored; later duplicates replace
 * earlier ones; keys are letters/digits only, no spaces). Returns the number
 * of snippets loaded.
 */
int ks_snippets_parse(KS_SNIPPET_TABLE *table, const wchar_t *content);
const KS_SNIPPET *ks_snippet_find(const KS_SNIPPET_TABLE *table, const wchar_t *key);

/* ---- Statistics --------------------------------------------------------- */

#define KS_STATS_WORDS 64

typedef struct KS_WORD_COUNT {
    wchar_t word[KS_MAX_WORD + 1];
    unsigned count;
} KS_WORD_COUNT;

typedef struct KS_STATS {
    long total_layout;
    long total_spelling;
    long total_keys;
    long today_layout;
    long today_spelling;
    long today_keys;
    long days_active;
    int today_year, today_month, today_day;
    KS_WORD_COUNT words[KS_STATS_WORDS];
    int word_count;
} KS_STATS;

/* Resets the daily counters when the date differs from the stored one. */
void ks_stats_roll_day(KS_STATS *stats, int year, int month, int day);
void ks_stats_observe_correction(KS_STATS *stats, const wchar_t *original, int spelling);
/* rank 0 = most corrected; returns 0 when there is no such entry. */
int ks_stats_top(const KS_STATS *stats, int rank, const wchar_t **word, unsigned *count);
/* Rough time saved: one correction ≈ 4 seconds (delete, switch, retype). */
long ks_stats_seconds_saved(const KS_STATS *stats);

#ifdef __cplusplus
}
#endif

#endif
