#include "../src/typing.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value, message) do { if (!(value)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)

static int check_jalali(int gy, int gm, int gd, int jy, int jm, int jd) {
    int y, m, d;
    int gy2, gm2, gd2;
    ks_gregorian_to_jalali(gy, gm, gd, &y, &m, &d);
    if (y != jy || m != jm || d != jd) {
        fprintf(stderr, "FAIL: %04d-%02d-%02d -> %d/%d/%d, expected %d/%d/%d\n",
                gy, gm, gd, y, m, d, jy, jm, jd);
        return 0;
    }
    ks_jalali_to_gregorian(jy, jm, jd, &gy2, &gm2, &gd2);
    if (gy2 != gy || gm2 != gm || gd2 != gd) {
        fprintf(stderr, "FAIL: %d/%d/%d -> %04d-%02d-%02d, expected %04d-%02d-%02d\n",
                jy, jm, jd, gy2, gm2, gd2, gy, gm, gd);
        return 0;
    }
    return 1;
}

int main(void) {
    wchar_t text[64];
    wchar_t out[256];
    KS_DATE_INFO now;
    KS_SNIPPET_TABLE table;
    KS_STATS stats;
    const KS_SNIPPET *snippet;
    const wchar_t *word;
    unsigned count;
    int i;

    /* Character shaping */
    CHECK(ks_persian_form(0x064A) == 0x06CC && ks_persian_form(0x0643) == 0x06A9,
          "Arabic yeh/kaf map to Persian");
    CHECK(ks_persian_form(0x0665) == 0x06F5, "Arabic-Indic digit maps to Persian digit");
    CHECK(ks_persian_form(L'a') == L'a' && ks_persian_form(0x06CC) == 0x06CC, "other letters unchanged");
    CHECK(ks_persian_digit(L'7') == 0x06F7 && ks_latin_digit(0x06F7) == L'7', "digit conversions");
    CHECK(ks_persian_punctuation(L'?') == 0x061F && ks_latin_punctuation(0x060C) == L',',
          "punctuation conversions");
    CHECK(ks_shape_digit(L'3', KS_DIGITS_BY_LAYOUT, KS_LANG_PERSIAN) == 0x06F3, "by-layout Persian digit");
    CHECK(ks_shape_digit(0x06F3, KS_DIGITS_BY_LAYOUT, KS_LANG_ENGLISH) == L'3', "by-layout Latin digit");
    CHECK(ks_shape_digit(0x06F3, KS_DIGITS_OFF, KS_LANG_ENGLISH) == 0x06F3, "digits off leaves them");
    CHECK(ks_shape_digit(L'x', KS_DIGITS_PERSIAN, KS_LANG_PERSIAN) == L'x', "non-digit untouched");

    wcscpy(text, L"\x0643\x062A\x0627\x0628 \x064A\x06A9 123?");
    CHECK(ks_text_is_persian(text), "Persian text detected");
    CHECK(ks_clean_text(text, 1, KS_DIGITS_BY_LAYOUT, 1), "clean-up reports a change");
    CHECK(wcscmp(text, L"\x06A9\x062A\x0627\x0628 \x06CC\x06A9 \x06F1\x06F2\x06F3\x061F") == 0,
          "clean-up rewrites letters, digits and punctuation");
    wcscpy(text, L"hello, world? 42");
    CHECK(!ks_text_is_persian(text), "English text detected");
    CHECK(!ks_clean_text(text, 1, KS_DIGITS_BY_LAYOUT, 1), "English text untouched by clean-up");
    CHECK(wcscmp(text, L"hello, world? 42") == 0, "English text intact");
    wcscpy(text, L"\x0633\x0644\x0627\x0645 \x06F1\x06F2");
    CHECK(ks_clean_text(text, 1, KS_DIGITS_LATIN, 0) && wcscmp(text, L"\x0633\x0644\x0627\x0645 12") == 0,
          "always-Latin digits policy converts Persian digits");

    /* Abbreviations */
    CHECK(ks_is_abbreviation(L"dr") && ks_is_abbreviation(L"etc") && ks_is_abbreviation(L"e"),
          "abbreviations recognised");
    CHECK(!ks_is_abbreviation(L"house") && !ks_is_abbreviation(L"done") && !ks_is_abbreviation(L"no"),
          "words are not abbreviations");
    CHECK(ks_persian_form(0x06C0) == 0x06C0, "heh with hamza above (Shift+G) is left alone");
    wcscpy(text, L"\x0633\x0627\x06CC\x062A site.com/page?id=123 \x0648 \x06F1\x06F2 12?");
    CHECK(ks_clean_text(text, 1, KS_DIGITS_BY_LAYOUT, 1) &&
          wcscmp(text, L"\x0633\x0627\x06CC\x062A site.com/page?id=123 \x0648 \x06F1\x06F2 \x06F1\x06F2\x061F") == 0,
          "URL token keeps its digits and marks, plain tokens are shaped");

    /* Jalali calendar: known dates */
    CHECK(check_jalali(2026, 3, 21, 1405, 1, 1), "Nowruz 1405");
    CHECK(check_jalali(2026, 9, 12, 1405, 6, 21), "12 September 2026");
    CHECK(check_jalali(2024, 3, 20, 1403, 1, 1), "Nowruz 1403");
    CHECK(check_jalali(2025, 3, 20, 1403, 12, 30), "leap Esfand 1403 has 30 days");
    CHECK(check_jalali(2000, 1, 1, 1378, 10, 11), "1 January 2000");
    CHECK(check_jalali(1979, 2, 11, 1357, 11, 22), "11 February 1979");
    CHECK(check_jalali(2021, 3, 20, 1399, 12, 30), "leap Esfand 1399");
    CHECK(check_jalali(2030, 12, 31, 1409, 10, 10), "31 December 2030");
    /* Round trip over three centuries, one day at a time, via the Julian day. */
    {
        int gy = 1900, gm = 1, gd = 1;
        int days = 0;
        static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        while (gy < 2200) {
            int jy, jm, jd, gy2, gm2, gd2;
            int leap = (gy % 4 == 0 && gy % 100 != 0) || gy % 400 == 0;
            int limit = month_days[gm - 1] + (gm == 2 && leap);
            ks_gregorian_to_jalali(gy, gm, gd, &jy, &jm, &jd);
            CHECK(jm >= 1 && jm <= 12 && jd >= 1 && jd <= 31, "Jalali month/day in range");
            CHECK(jm > 6 ? jd <= 30 : 1, "second-half months have at most 30 days");
            ks_jalali_to_gregorian(jy, jm, jd, &gy2, &gm2, &gd2);
            CHECK(gy2 == gy && gm2 == gm && gd2 == gd, "round trip Gregorian -> Jalali -> Gregorian");
            ++days;
            if (++gd > limit) { gd = 1; if (++gm > 12) { gm = 1; ++gy; } }
        }
        CHECK(days > 100000, "iterated the full range");
    }

    /* Macros */
    memset(&now, 0, sizeof(now));
    now.year = 2026; now.month = 9; now.day = 12; now.hour = 14; now.minute = 5; now.weekday = 6;
    ks_expand_macros(L"{jdate}", &now, out, 256);
    CHECK(wcscmp(out, L"\x06F1\x06F4\x06F0\x06F5/\x06F0\x06F6/\x06F2\x06F1") == 0, "{jdate} Persian digits");
    ks_expand_macros(L"{jdate:en}", &now, out, 256);
    CHECK(wcscmp(out, L"1405/06/21") == 0, "{jdate:en}");
    ks_expand_macros(L"{jdate:long}", &now, out, 256);
    CHECK(wcscmp(out, L"\x06F2\x06F1 \x0634\x0647\x0631\x06CC\x0648\x0631 \x06F1\x06F4\x06F0\x06F5") == 0,
          "{jdate:long} 21 Shahrivar 1405");
    ks_expand_macros(L"{jweekday} {weekday}", &now, out, 256);
    CHECK(wcscmp(out, L"\x0634\x0646\x0628\x0647 Saturday") == 0, "weekday names");
    ks_expand_macros(L"{date} {date:long} {time} {time:fa}", &now, out, 256);
    CHECK(wcscmp(out, L"2026-09-12 12 September 2026 14:05 \x06F1\x06F4:\x06F0\x06F5") == 0,
          "Gregorian date and time macros");
    ks_expand_macros(L"a\\nb{n}c{t}d{{x}{unknown}", &now, out, 256);
    CHECK(wcscmp(out, L"a\nb\nc\td{x}{unknown}") == 0, "escapes, braces, unknown macro kept");
    CHECK(ks_expand_macros(L"0123456789", &now, out, 5) == 4 && wcscmp(out, L"0123") == 0,
          "expansion respects capacity");

    /* Snippets */
    CHECK(ks_snippets_parse(&table,
        L"\xFEFF# comment\n"
        L"sig = Best regards,\\nSiavash\n"
        L"tdate\t{jdate}\n"
        L"bad key = x\n"
        L"sig=replaced\n"
        L"\n"
        L"empty=\n"
        L"\x0645\x062E = \x0645\x062A\x0634\x06A9\x0631\x0645\r\n") == 3,
          "three valid snippets parsed");
    snippet = ks_snippet_find(&table, L"sig");
    CHECK(snippet && wcscmp(snippet->text, L"replaced") == 0, "later duplicate replaces earlier");
    snippet = ks_snippet_find(&table, L"tdate");
    CHECK(snippet && wcscmp(snippet->text, L"{jdate}") == 0, "tab separator accepted");
    snippet = ks_snippet_find(&table, L"\x0645\x062E");
    CHECK(snippet && wcscmp(snippet->text, L"\x0645\x062A\x0634\x06A9\x0631\x0645") == 0,
          "Persian key with CRLF line");
    CHECK(!ks_snippet_find(&table, L"bad key") && !ks_snippet_find(&table, L"empty"),
          "invalid lines rejected");
    CHECK(!ks_snippet_find(&table, L"") && !ks_snippet_find(NULL, L"sig"), "empty lookups are safe");
    {
        wchar_t many[KS_SNIPPET_MAX * 12 + 64];
        size_t used = 0;
        for (i = 0; i < KS_SNIPPET_MAX + 10; ++i)
            used += (size_t)swprintf(many + used, 32, L"k%d=v%d\n", i, i);
        CHECK(ks_snippets_parse(&table, many) == KS_SNIPPET_MAX, "table capped at the maximum");
    }

    /* Statistics */
    memset(&stats, 0, sizeof(stats));
    ks_stats_roll_day(&stats, 2026, 9, 12);
    CHECK(stats.days_active == 1, "first day counted");
    ks_stats_observe_correction(&stats, L"teh", 1);
    ks_stats_observe_correction(&stats, L"sghl", 0);
    ks_stats_observe_correction(&stats, L"sghl", 0);
    ks_stats_roll_day(&stats, 2026, 9, 12);
    CHECK(stats.today_layout == 2 && stats.today_spelling == 1 && stats.total_layout == 2,
          "same day keeps counters");
    ks_stats_roll_day(&stats, 2026, 9, 13);
    CHECK(stats.today_layout == 0 && stats.total_layout == 2 && stats.days_active == 2,
          "new day resets today, keeps totals");
    CHECK(ks_stats_top(&stats, 0, &word, &count) && wcscmp(word, L"sghl") == 0 && count == 2,
          "most corrected word first");
    CHECK(ks_stats_top(&stats, 1, &word, &count) && wcscmp(word, L"teh") == 0, "second word");
    CHECK(!ks_stats_top(&stats, 2, &word, &count), "no third word");
    for (i = 0; i < KS_STATS_WORDS + 5; ++i) {
        wchar_t w[8];
        swprintf(w, 8, L"w%d", i);
        ks_stats_observe_correction(&stats, w, 0);
    }
    CHECK(stats.word_count == KS_STATS_WORDS, "word ring bounded");
    CHECK(ks_stats_top(&stats, 0, &word, &count) && wcscmp(word, L"sghl") == 0,
          "frequent word survives eviction");
    CHECK(ks_stats_seconds_saved(&stats) == (long)(stats.total_layout + stats.total_spelling) * 4,
          "time saved estimate");

    printf("All typing helper tests passed.\n");
    return 0;
}
