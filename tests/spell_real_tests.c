/*
 * Spelling correction with the real tables the program ships: the rank
 * tables generated from wordfreq (resources/en-rank.bin, fa-rank.bin), the dictionary
 * Blooms and the word-form Blooms.
 *
 * 1. Words users reported, colloquial Persian that must stay as written,
 *    and the rules for rare words, two slips and kept words.
 * 2. The fixed evaluation set (tests/fixtures/spell-eval.tsv, built by
 *    tools/make_spell_eval.py): 4,000 generated typos and 5,300 valid words.
 *    The test fails when a change fixes fewer typos, guesses wrong more
 *    often, or changes more valid words (a repair or a split; restoring a
 *    half-space is spelling normalisation, not a change) than the limits.
 * 3. Speed: the slowest words must stay far inside the hook's budget.
 */
#include "../src/spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(value, message) do { \
    if (!(value)) { fprintf(stderr, "FAIL: %s\n", message); ++failures; } \
} while (0)

static unsigned char *load_file(const char *path, size_t *size) {
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

static KS_RANK_TABLE ranks[2];
static KS_RANK_TABLE persian_pairs;
static KS_BLOOM words[2], common[2], forms[2];
static KS_SPELL_LEXICON lexicon[2];   /* 0 Persian, 1 English */
static KS_IGNORE_LIST ignore;

static int load_language(int index, const char *code) {
    char path[128];
    size_t size;
    unsigned char *data;
    snprintf(path, sizeof(path), "resources/%s-rank.bin", code);
    if (!(data = load_file(path, &size)) || !ks_rank_table_init(&ranks[index], data, size)) return 0;
    snprintf(path, sizeof(path), "resources/%s.bloom", code);
    if (!(data = load_file(path, &size)) || !ks_bloom_init(&words[index], data, size)) return 0;
    snprintf(path, sizeof(path), "resources/%s-common.bloom", code);
    if (!(data = load_file(path, &size)) || !ks_bloom_init(&common[index], data, size)) return 0;
    snprintf(path, sizeof(path), "resources/%s-forms.bloom", code);
    if (!(data = load_file(path, &size)) || !ks_bloom_init(&forms[index], data, size)) return 0;
    memset(&lexicon[index], 0, sizeof(lexicon[index]));
    lexicon[index].language = index ? KS_LANG_ENGLISH : KS_LANG_PERSIAN;
    lexicon[index].ranks = &ranks[index];
    lexicon[index].words = &words[index];
    lexicon[index].common = &common[index];
    lexicon[index].forms = &forms[index];
    return 1;
}

static const KS_SPELL_LEXICON *lexicon_for(const wchar_t *word) {
    return word[0] < 0x80 ? &lexicon[1] : &lexicon[0];
}

static int fixes(const wchar_t *typed, int level, const wchar_t *expected) {
    KS_SPELL_RESULT result;
    ks_spell_correct(typed, level, lexicon_for(typed), &ignore, &result);
    if (!result.should_correct || wcscmp(result.replacement, expected) != 0) {
        fprintf(stderr, "  %ls -> %ls (expected %ls)\n", typed,
                result.should_correct ? result.replacement : L"(kept)", expected);
        return 0;
    }
    return 1;
}

static int kept(const wchar_t *typed, int level) {
    KS_SPELL_RESULT result;
    ks_spell_correct(typed, level, lexicon_for(typed), &ignore, &result);
    if (result.should_correct) {
        fprintf(stderr, "  %ls -> %ls (expected it kept)\n", typed, result.replacement);
        return 0;
    }
    return 1;
}

/* The writing memory's view of a word, for the weak/strong rules. */
static const wchar_t *memory_word;
static int memory_rank;
static int memory_adjust(const void *context, const wchar_t *word, int table_rank) {
    (void)context;
    return memory_word && wcscmp(word, memory_word) == 0 ? memory_rank : table_rank;
}

static void reported_words(void) {
    static const wchar_t *const fixed[][2] = {
        /* Reported by users (4.1). */
        {L"اضلا", L"اصلا"},       /* a neighbouring key / homophone */
        {L"یغتی", L"یعنی"},       /* two neighbouring keys */
        {L"خرفه", L"حرفه"},       /* a rare word one key from a very frequent one */
        {L"خوشکل", L"خوشگل"},     /* ک for گ */
        /* Corpus misspellings and spelling-type slips. */
        {L"برگذار", L"برگزار"}, {L"تلوزیون", L"تلویزیون"}, {L"هشتیم", L"هستیم"},
        {L"خروشید", L"خورشید"}, {L"تامس", L"تماس"}, {L"اتراف", L"اطراف"},
        {L"طرفل", L"طرف"},         /* an extra neighbouring key after the last letter */
        {L"عهمگی", L"همگی"},       /* ...or before the first */
        {L"مقابلب", L"مقابل"}, {L"جکتابخانه", L"کتابخانه"},
        {L"teh", L"the"}, {L"recieve", L"receive"}, {L"wrold", L"world"},
        {L"realy", L"really"}, {L"becuase", L"because"},
    };
    static const wchar_t *const valid[] = {
        /* Colloquial Persian: endings, not typos (4.0 changed several). */
        L"نمیدونه", L"خونت", L"پیشش", L"مخالفم", L"قدیما", L"نیار", L"یادته",
        L"میخواید", L"چیزو", L"دوستاش", L"نکنین", L"شدین", L"گفتین", L"نمیدونیم",
        L"زندگیه", L"خودتونو", L"میخوام", L"اسیران", L"قلابی",
        /* Inflections the dictionary allows but stores only as rules. */
        L"کتابهایشان", L"hordes", L"neatly", L"certifies", L"proposes", L"kilns",
        L"running", L"kayaks",
    };
    size_t i;
    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
        CHECK(fixes(fixed[i][0], KS_SPELL_BALANCED, fixed[i][1]), "a reported or typical typo is fixed");
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
        CHECK(kept(valid[i], KS_SPELL_AGGRESSIVE), "a valid word is never changed, even at Aggressive");

    /* مثلل is مثل with a doubled letter or مثلا with a slip: no guess. */
    CHECK(kept(L"مثلل", KS_SPELL_BALANCED), "an ambiguous typo is left for the user (or a learned repair)");
    /* Conservative keeps to single, clear slips. */
    CHECK(kept(L"یغتی", KS_SPELL_CONSERVATIVE), "no two-slip repair at Conservative");
    CHECK(kept(L"خرفه", KS_SPELL_CONSERVATIVE), "no rare-word repair at Conservative");
    CHECK(fixes(L"اضلا", KS_SPELL_CONSERVATIVE, L"اصلا"), "a clear single slip is fixed at Conservative");
    /* Never cut an ending, not even by a two-slip detour. */
    CHECK(kept(L"webz", KS_SPELL_BALANCED) || fixes(L"webz", KS_SPELL_BALANCED, L"web"),
          "webz is a slip of web or left alone");

    {
        KS_SPELL_RESULT result;
        ks_spell_correct(L"یغتی", KS_SPELL_BALANCED, &lexicon[0], &ignore, &result);
        CHECK(result.edits == 2 && !result.real_word, "یغتی is reported as two slips");
        ks_spell_correct(L"خرفه", KS_SPELL_BALANCED, &lexicon[0], &ignore, &result);
        CHECK(result.edits == 1 && result.real_word, "خرفه is reported as a rare-word repair");
    }
}

/* ---- Context (4.2) ------------------------------------------------------- */

static int utf8_to_wide(const char *text, wchar_t *out, size_t capacity);
static double percent(long part, long whole);

static int fixes_in_context(const wchar_t *before, const wchar_t *typed, const wchar_t *after,
                            const wchar_t *expected) {
    KS_SPELL_RESULT result;
    ks_spell_correct_in_context(typed, before, after, KS_SPELL_BALANCED, &lexicon[0], &ignore, &result);
    if (!result.should_correct || wcscmp(result.replacement, expected) != 0) {
        fprintf(stderr, "  %ls [%ls] %ls -> %ls (expected %ls)\n", before ? before : L"", typed,
                after ? after : L"", result.should_correct ? result.replacement : L"(kept)", expected);
        return 0;
    }
    return 1;
}

static const wchar_t *own_first, *own_second;
static int own_pairs(const void *context, const wchar_t *first, const wchar_t *second, int table_score) {
    (void)context;
    return own_first && wcscmp(first, own_first) == 0 && wcscmp(second, own_second) == 0 ? 30 : table_score;
}

static void context(void) {
    KS_SPELL_LEXICON persian = lexicon[0];
    KS_SPELL_RESULT result;
    /* Word by word these stay; their neighbours settle them. */
    CHECK(kept(L"\u0628\u0632\u0627\u0646", KS_SPELL_BALANCED), "بزان alone is left as typed");
    CHECK(fixes_in_context(L"\u0628\u0647", L"\u0628\u0632\u0627\u0646", L"\u0645\u0627\u062F\u0631\u06CC",
                           L"\u0632\u0628\u0627\u0646"), "به بزان مادری -> زبان");
    CHECK(fixes_in_context(L"\u0641\u0631\u0642", L"\u062F\u0627\u062A\u0647", L"\u0628\u0627\u0634\u062F",
                           L"\u062F\u0627\u0634\u062A\u0647"), "فرق داته باشد -> داشته");
    CHECK(fixes_in_context(L"\u0634\u0627\u0645\u0644", L"\u0627\u0646\u0648\u0639",
                           L"\u0645\u062E\u062A\u0644\u0641\u06CC", L"\u0627\u0646\u0648\u0627\u0639"),
          "شامل انوع مختلفی -> انواع");
    /* Context never changes a word written correctly. */
    CHECK(!ks_spell_correct_in_context(L"\u0646\u0645\u06CC\u062F\u0648\u0646\u0647", L"\u0645\u0646", L"\u0686\u06CC",
                                       KS_SPELL_AGGRESSIVE, &lexicon[0], &ignore, &result),
          "a colloquial word stays in any context");
    /* The user's own phrase decides an ambiguous word: مثلل after من. */
    CHECK(kept(L"\u0645\u062B\u0644\u0644", KS_SPELL_BALANCED), "مثلل alone is ambiguous");
    persian.pair_adjust = own_pairs;
    own_first = L"\u0645\u0646";
    own_second = L"\u0645\u062B\u0644\u0627";
    CHECK(ks_spell_correct_in_context(L"\u0645\u062B\u0644\u0644", L"\u0645\u0646", NULL, KS_SPELL_BALANCED,
                                      &persian, &ignore, &result) &&
              wcscmp(result.replacement, L"\u0645\u062B\u0644\u0627") == 0,
          "the user's own phrase (من مثلا) settles مثلل");
    own_first = own_second = NULL;
    CHECK(ks_spell_pair_score(&lexicon[0], L"\u062D\u0631\u0641\u0647", L"\u0627\u06CC") > 0 &&
              ks_spell_pair_score(&lexicon[0], L"\u062E\u0631\u0641\u0647", L"\u0627\u06CC") == 0,
          "حرفه ای is a known pair, خرفه ای is not");
}

static void context_evaluation(void) {
    /* Balanced, Persian: no context, the word before, both neighbours. */
    static const double fixed_at_least[3] = {64, 74, 79};
    static const double wrong_at_most[3] = {5.5, 5.0, 5.0};
    static const char *const names[3] = {"no context", "word before", "both sides"};
    int mode;
    for (mode = 0; mode < 3; ++mode) {
        char line[512];
        long typos = 0, fixed = 0, wrong = 0, valid = 0, changed = 0;
        FILE *file = fopen("tests/fixtures/spell-context-eval.tsv", "r");
        if (!file) {
            CHECK(0, "tests/fixtures/spell-context-eval.tsv loads");
            return;
        }
        while (fgets(line, sizeof(line), file)) {
            char *kind, *before, *typed, *after, *intended;
            wchar_t b[64], t[64], a[64], w[64];
            KS_SPELL_RESULT result;
            if (line[0] == '#') continue;
            kind = strtok(line, "\t");
            before = strtok(NULL, "\t");
            typed = strtok(NULL, "\t");
            after = strtok(NULL, "\t");
            intended = kind && kind[0] == 'T' ? strtok(NULL, "\t") : "";
            if (!kind || !before || !typed || !after || !intended) continue;
            if (!utf8_to_wide(before, b, 64) || !utf8_to_wide(typed, t, 64) || !utf8_to_wide(after, a, 64) ||
                !utf8_to_wide(intended, w, 64)) continue;
            ks_spell_correct_in_context(t, mode >= 1 ? b : NULL, mode >= 2 ? a : NULL, KS_SPELL_BALANCED,
                                        &lexicon[0], &ignore, &result);
            if (kind[0] == 'T') {
                ++typos;
                if (result.should_correct) {
                    if (wcscmp(result.replacement, w) == 0) ++fixed;
                    else ++wrong;
                }
            } else {
                ++valid;
                if (result.should_correct && result.kind != KS_SPELL_KIND_ZWNJ) ++changed;
            }
        }
        fclose(file);
        printf("  context, %-11s typos fixed %5.1f%%  wrong %4.1f%%   valid words changed %ld of %ld\n",
               names[mode], percent(fixed, typos), percent(wrong, typos), changed, valid);
        CHECK(typos > 3000 && valid > 2000, "the context set loads");
        CHECK(percent(fixed, typos) >= fixed_at_least[mode] && percent(wrong, typos) <= wrong_at_most[mode] &&
                  percent(changed, valid) <= 0.2,
              "spelling in context within its limits");
    }
}

static void kept_words(void) {
    KS_VOCAB session;
    KS_SPELL_LEXICON persian = lexicon[0];
    KS_SPELL_RESULT result;
    /* A word the user kept (undid a fix of) is theirs. */
    ks_vocab_reset(&session);
    ks_vocab_trust(&session, L"خرفه");
    persian.vocabulary = &session;
    CHECK(!ks_spell_weakly_known(L"خرفه", &persian) &&
              !ks_spell_correct(L"خرفه", KS_SPELL_AGGRESSIVE, &persian, &ignore, &result),
          "a kept rare word stays");
    ks_vocab_trust(&session, L"اضلا");
    CHECK(!ks_spell_correct(L"اضلا", KS_SPELL_AGGRESSIVE, &persian, &ignore, &result),
          "a kept misspelling stays");
    ks_ignore_list_add(&ignore, L"یغتی");
    CHECK(kept(L"یغتی", KS_SPELL_AGGRESSIVE), "an undone word is not fixed again this session");
    ks_ignore_list_reset(&ignore);

    /* The writing memory: a typo it saw a few times (rank 30 + a little)
       yields to a confident fix; a word typed eight times (rank 36) and a
       vocabulary-pack term (40) do not. */
    persian = lexicon[0];
    persian.rank_adjust = memory_adjust;
    memory_word = L"اضلا";
    memory_rank = 32;
    CHECK(ks_spell_weakly_known(L"اضلا", &persian) &&
              ks_spell_correct(L"اضلا", KS_SPELL_BALANCED, &persian, &ignore, &result) &&
              wcscmp(result.replacement, L"اصلا") == 0 && result.real_word,
          "a typo the memory saw three times is still fixed");
    memory_rank = 34;   /* seen seven times */
    CHECK(ks_spell_weakly_known(L"اضلا", &persian), "seven uses are still weak");
    memory_rank = 36;   /* seen eight times */
    CHECK(!ks_spell_weakly_known(L"اضلا", &persian) &&
              !ks_spell_correct(L"اضلا", KS_SPELL_AGGRESSIVE, &persian, &ignore, &result),
          "a word typed often is the user's own");
    memory_word = NULL;
    CHECK(!ks_spell_weakly_known(L"حرفه", &lexicon[0]) && ks_spell_weakly_known(L"خرفه", &lexicon[0]),
          "a frequent word is strongly known, a rare dictionary word weakly");
}

/* ---- The evaluation set -------------------------------------------------- */

/* UTF-8 to UTF-16/32 without the C library's locale (Windows has no
   C.UTF-8). Returns 0 for malformed or overlong input. */
static int utf8_to_wide(const char *text, wchar_t *out, size_t capacity) {
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned code;
        int extra;
        if (*p < 0x80) { code = *p++; extra = 0; }
        else if ((*p & 0xE0) == 0xC0) { code = *p++ & 0x1Fu; extra = 1; }
        else if ((*p & 0xF0) == 0xE0) { code = *p++ & 0x0Fu; extra = 2; }
        else return 0;
        while (extra--) {
            if ((*p & 0xC0) != 0x80) return 0;
            code = (code << 6) | (*p++ & 0x3Fu);
        }
        if (used + 1 >= capacity) return 0;
        out[used++] = (wchar_t)code;
    }
    out[used] = 0;
    return 1;
}

typedef struct TALLY { long typos, fixed, wrong, valid_corpus, changed_corpus, valid_dictionary, changed_dictionary; } TALLY;

static int evaluation(int level, TALLY tally[2]) {
    char line[512];
    FILE *file = fopen("tests/fixtures/spell-eval.tsv", "r");
    if (!file) return 0;
    memset(tally, 0, 2 * sizeof(TALLY));
    while (fgets(line, sizeof(line), file)) {
        char *kind, *lang, *typed, *intended, *class_name;
        wchar_t typed_w[64], intended_w[64];
        KS_SPELL_RESULT result;
        int index;
        if (line[0] == '#') continue;
        kind = strtok(line, "\t");
        lang = strtok(NULL, "\t");
        typed = strtok(NULL, "\t");
        intended = kind && kind[0] == 'T' ? strtok(NULL, "\t") : "";
        class_name = strtok(NULL, "\t\n");
        if (!kind || !lang || !typed || !intended || !class_name) continue;
        index = strcmp(lang, "en") == 0;
        if (!utf8_to_wide(typed, typed_w, 64) || !utf8_to_wide(intended, intended_w, 64)) continue;
        ks_spell_correct(typed_w, level, &lexicon[index], &ignore, &result);
        if (kind[0] == 'T') {
            ++tally[index].typos;
            if (result.should_correct) {
                if (wcscmp(result.replacement, intended_w) == 0) ++tally[index].fixed;
                else ++tally[index].wrong;
            }
        } else if (strcmp(class_name, "corpus") == 0) {
            ++tally[index].valid_corpus;
            if (result.should_correct && result.kind != KS_SPELL_KIND_ZWNJ) ++tally[index].changed_corpus;
        } else {
            ++tally[index].valid_dictionary;
            if (result.should_correct && result.kind != KS_SPELL_KIND_ZWNJ) ++tally[index].changed_dictionary;
        }
    }
    fclose(file);
    return tally[0].typos > 0 && tally[1].typos > 0;
}

static double percent(long part, long whole) { return whole ? 100.0 * part / whole : 0.0; }

static void evaluate(void) {
    /* Limits per level and language: at least this many typos fixed, at
       most this many wrong, at most this share of valid words changed.
       Measured values (4.1.0) are a little better than every limit. */
    static const struct { int level; double fixed[2], wrong[2], corpus[2], dictionary[2]; } limits[] = {
        {KS_SPELL_CONSERVATIVE, {44, 57}, {2.5, 1.5}, {0.6, 1.2}, {0.1, 0.2}},
        {KS_SPELL_BALANCED,     {60, 79}, {6.0, 3.5}, {1.0, 3.5}, {1.0, 0.5}},
        {KS_SPELL_AGGRESSIVE,   {70, 84}, {10.5, 6.0}, {1.5, 7.0}, {2.0, 1.0}},
    };
    static const char *const names[] = {"Persian", "English"};
    size_t i;
    int language;
    for (i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
        TALLY tally[2];
        if (!evaluation(limits[i].level, tally)) {
            CHECK(0, "tests/fixtures/spell-eval.tsv loads");
            return;
        }
        for (language = 0; language < 2; ++language) {
            const TALLY *t = &tally[language];
            double fixed = percent(t->fixed, t->typos), wrong = percent(t->wrong, t->typos);
            double corpus = percent(t->changed_corpus, t->valid_corpus);
            double dictionary = percent(t->changed_dictionary, t->valid_dictionary);
            char message[160];
            printf("  level %d %-7s typos fixed %5.1f%%  wrong %4.1f%%   valid words changed: corpus %4.2f%%  rare %4.2f%%\n",
                   limits[i].level, names[language], fixed, wrong, corpus, dictionary);
            snprintf(message, sizeof(message), "level %d %s: spelling quality within its limits",
                     limits[i].level, names[language]);
            CHECK(fixed >= limits[i].fixed[language] && wrong <= limits[i].wrong[language] &&
                      corpus <= limits[i].corpus[language] && dictionary <= limits[i].dictionary[language],
                  message);
        }
    }
}

static void speed(void) {
    /* Words no slip explains are the slowest: every candidate is tried. */
    static const wchar_t *const slow[] = {
        L"ضصثقفغعهخحجچ", L"قخقخقخقخقخقخ", L"qwrtpsdfghjk", L"zxcvbnmlkjhg", L"ثقفغعهخحجچشس",
    };
    double worst = 0;
    size_t i;
    for (i = 0; i < sizeof(slow) / sizeof(slow[0]); ++i) {
        KS_SPELL_RESULT result;
        clock_t start = clock();
        int round;
        double ms;
        for (round = 0; round < 5; ++round)
            ks_spell_correct(slow[i], KS_SPELL_AGGRESSIVE, lexicon_for(slow[i]), &ignore, &result);
        ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC / 5;
        if (ms > worst) worst = ms;
    }
    printf("  slowest word: %.1f ms\n", worst);
    /* The hook may spend 150 ms on a key; sanitizer builds are ~10x slower. */
    CHECK(worst < 60.0, "the slowest word is corrected in well under the hook's budget");
}

int main(void) {
    if (!load_language(0, "fa") || !load_language(1, "en")) {
        fprintf(stderr, "FAIL: resources missing (generate the rank tables first)\n");
        return 1;
    }
    {
        size_t size;
        unsigned char *data = load_file("resources/fa-pairs.bin", &size);
        if (!data || !ks_rank_table_init(&persian_pairs, data, size)) {
            fprintf(stderr, "FAIL: resources/fa-pairs.bin missing or damaged\n");
            return 1;
        }
    }
    ks_ignore_list_reset(&ignore);
    reported_words();
    kept_words();
    evaluate();
    /* From here on the Persian lexicon has the pair table. */
    lexicon[0].pairs = &persian_pairs;
    context();
    context_evaluation();
    speed();
    if (failures) {
        fprintf(stderr, "%d spelling check(s) with the real tables FAILED\n", failures);
        return 1;
    }
    printf("All spelling tests with the real tables passed.\n");
    return 0;
}
