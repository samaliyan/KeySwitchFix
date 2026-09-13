#include "../src/spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *load_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long length;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

#define CHECK(value, message) do { if (!(value)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)

static int corrects_to(const wchar_t *typed, int level, const KS_SPELL_LEXICON *lexicon,
                       const KS_IGNORE_LIST *ignore, const wchar_t *expected) {
    KS_SPELL_RESULT result;
    if (!ks_spell_correct(typed, level, lexicon, ignore, &result)) return 0;
    return result.should_correct && wcscmp(result.replacement, expected) == 0;
}

static int untouched(const wchar_t *typed, int level, const KS_SPELL_LEXICON *lexicon,
                     const KS_IGNORE_LIST *ignore) {
    KS_SPELL_RESULT result;
    return !ks_spell_correct(typed, level, lexicon, ignore, &result) &&
           !result.should_correct;
}

int main(void) {
    size_t en_rank_size = 0, fa_rank_size = 0, en_size = 0, fa_size = 0;
    size_t en_common_size = 0, fa_common_size = 0;
    unsigned char *en_rank_data = load_file("tests/fixtures/rank-fixture-en.bin", &en_rank_size);
    unsigned char *fa_rank_data = load_file("tests/fixtures/rank-fixture-fa.bin", &fa_rank_size);
    unsigned char *en_data = load_file("resources/en.bloom", &en_size);
    unsigned char *fa_data = load_file("resources/fa.bloom", &fa_size);
    unsigned char *en_common_data = load_file("resources/en-common.bloom", &en_common_size);
    unsigned char *fa_common_data = load_file("resources/fa-common.bloom", &fa_common_size);
    KS_RANK_TABLE en_rank, fa_rank, broken;
    KS_BLOOM en, fa, en_common, fa_common;
    KS_SPELL_LEXICON english, persian;
    KS_IGNORE_LIST ignore;
    KS_SPELL_RESULT result;
    unsigned char corrupt[64];

    CHECK(en_rank_data && fa_rank_data && en_data && fa_data &&
          en_common_data && fa_common_data, "fixtures and resources load");
    CHECK(ks_rank_table_init(&en_rank, en_rank_data, en_rank_size), "English rank table validates");
    CHECK(ks_rank_table_init(&fa_rank, fa_rank_data, fa_rank_size), "Persian rank table validates");
    CHECK(en_rank.word_count == 95 && fa_rank.word_count == 74, "fixture word counts match the generator");
    CHECK(ks_bloom_init(&en, en_data, en_size) && ks_bloom_init(&fa, fa_data, fa_size) &&
          ks_bloom_init(&en_common, en_common_data, en_common_size) &&
          ks_bloom_init(&fa_common, fa_common_data, fa_common_size),
          "Bloom resources validate");

    memcpy(corrupt, en_rank_data, sizeof(corrupt));
    corrupt[0] = 'X';
    CHECK(!ks_rank_table_init(&broken, corrupt, en_rank_size), "bad magic is rejected");
    CHECK(!ks_rank_table_init(&broken, en_rank_data, en_rank_size - 4), "truncated table is rejected");
    CHECK(!ks_rank_table_init(&broken, en_rank_data, 15), "header-only buffer is rejected");

    /* Python builder and C reader agree on hash, slot, and rank encoding. */
    CHECK(ks_rank_lookup(&en_rank, L"the") == 77, "the has zipf 7.7");
    CHECK(ks_rank_lookup(&en_rank, L"hello") == 52, "hello has zipf 5.2");
    CHECK(ks_rank_lookup(&en_rank, L"thx") == 43, "casual thx is in the table");
    CHECK(ks_rank_lookup(&en_rank, L"teh") == -1, "teh is not a word");
    CHECK(ks_rank_lookup(&en_rank, L"") == -1, "empty string is not a word");
    CHECK(ks_rank_lookup(&fa_rank, L"نسخه") == 53, "noskhe has zipf 5.3");
    CHECK(ks_rank_lookup(&fa_rank, L"سلام") == 56, "salam has zipf 5.6");
    CHECK(ks_rank_lookup(&fa_rank, L"نسحه") == -1, "nosHe is not a word");

    CHECK(ks_keys_adjacent(L'h', L'n') && ks_keys_adjacent(L'n', L'h'),
          "h and n are neighbours on the keyboard");
    CHECK(ks_keys_adjacent(L'o', L'p') && ks_keys_adjacent(L'خ', L'ح'),
          "adjacent keys are adjacent in both languages");
    CHECK(!ks_keys_adjacent(L'h', L'a') && !ks_keys_adjacent(L'q', L'p') &&
          !ks_keys_adjacent(L'a', L'a'),
          "distant or identical keys are not adjacent");
    CHECK(ks_persian_confusable(L'ح', L'خ') == 0 && ks_persian_confusable(L'ح', L'ه') &&
          ks_persian_confusable(L'ت', L'ط') && ks_persian_confusable(L'ز', L'ظ') &&
          !ks_persian_confusable(L'ب', L'پ'),
          "confusion sets cover homophones only");

    memset(&english, 0, sizeof(english));
    english.language = KS_LANG_ENGLISH;
    english.ranks = &en_rank;
    english.words = &en;
    english.common = &en_common;
    memset(&persian, 0, sizeof(persian));
    persian.language = KS_LANG_PERSIAN;
    persian.ranks = &fa_rank;
    persian.words = &fa;
    persian.common = &fa_common;
    ks_ignore_list_reset(&ignore);

    /* --- English --- */
    CHECK(corrects_to(L"teh", KS_SPELL_BALANCED, &english, &ignore, L"the"),
          "teh -> the by transposition");
    CHECK(ks_spell_correct(L"teh", KS_SPELL_BALANCED, &english, &ignore, &result) &&
              result.candidate_count >= 3 && result.confidence >= 12,
          "teh sees ten/tea/tee as weaker candidates");
    CHECK(corrects_to(L"wrold", KS_SPELL_BALANCED, &english, &ignore, L"world"),
          "wrold -> world");
    CHECK(corrects_to(L"helllo", KS_SPELL_BALANCED, &english, &ignore, L"hello"),
          "helllo -> hello by dropping the doubled letter");
    CHECK(corrects_to(L"recieve", KS_SPELL_BALANCED, &english, &ignore, L"receive"),
          "recieve -> receive");
    CHECK(corrects_to(L"wprd", KS_SPELL_BALANCED, &english, &ignore, L"word"),
          "wprd -> word by neighbouring key");
    CHECK(corrects_to(L"becuase", KS_SPELL_BALANCED, &english, &ignore, L"because"),
          "becuase -> because");
    CHECK(corrects_to(L"definately", KS_SPELL_AGGRESSIVE, &english, &ignore, L"definitely"),
          "definately -> definitely");

    CHECK(untouched(L"ther", KS_SPELL_BALANCED, &english, &ignore),
          "ther is ambiguous between there and their and is left alone");
    CHECK(ks_spell_correct(L"ther", KS_SPELL_BALANCED, &english, &ignore, &result) == 0 &&
              result.candidate_count >= 3,
          "the ambiguous word still enumerated its candidates");
    CHECK(corrects_to(L"hallo", KS_SPELL_BALANCED, &english, &ignore, L"hello"),
          "hallo -> hello (hall is suffix stripping and not offered)");
    CHECK(untouched(L"webs", KS_SPELL_BALANCED, &english, &ignore) &&
              untouched(L"words", KS_SPELL_CONSERVATIVE, &english, &ignore),
          "suffix stripping (webs -> web) is refused below Aggressive; a known plural is untouched anyway");
    CHECK(untouched(L"thens", KS_SPELL_BALANCED, &english, &ignore),
          "an unknown inflection is not stripped to its stem");
    CHECK(untouched(L"خانها", KS_SPELL_BALANCED, &persian, &ignore),
          "Persian suffix stripping is refused below Aggressive");
    CHECK(untouched(L"نخوب", KS_SPELL_BALANCED, &persian, &ignore),
          "dropping a Persian negation prefix is refused below Aggressive");
    CHECK(corrects_to(L"worldd", KS_SPELL_BALANCED, &english, &ignore, L"world"),
          "a doubled final letter is still repaired");
    CHECK(corrects_to(L"hhello", KS_SPELL_BALANCED, &english, &ignore, L"hello") &&
              corrects_to(L"سسلام", KS_SPELL_BALANCED, &persian, &ignore, L"سلام"),
          "a doubled initial letter is repaired too");
    CHECK(untouched(L"thx", KS_SPELL_BALANCED, &english, &ignore),
          "casual thx is a known word and is not rewritten to the");
    CHECK(untouched(L"Teh", KS_SPELL_BALANCED, &english, &ignore),
          "capitalised words are treated as names");
    CHECK(untouched(L"TEH", KS_SPELL_AGGRESSIVE, &english, &ignore),
          "acronyms are never touched");
    CHECK(untouched(L"the", KS_SPELL_AGGRESSIVE, &english, &ignore),
          "a correct word is never changed");
    CHECK(untouched(L"government", KS_SPELL_AGGRESSIVE, &english, &ignore),
          "a correct long word is never changed");
    CHECK(untouched(L"keyboardd", KS_SPELL_OFF, &english, &ignore),
          "level Off never corrects");
    CHECK(untouched(L"teh", KS_SPELL_CONSERVATIVE, &english, &ignore),
          "conservative mode leaves three-letter tokens alone");
    CHECK(corrects_to(L"wrold", KS_SPELL_CONSERVATIVE, &english, &ignore, L"world"),
          "conservative mode still fixes an unambiguous long typo");
    CHECK(untouched(L"xqz", KS_SPELL_AGGRESSIVE, &english, &ignore),
          "gibberish with no candidate is untouched");
    CHECK(untouched(L"don't", KS_SPELL_AGGRESSIVE, &english, &ignore),
          "apostrophes are outside the model");
    CHECK(untouched(L"x", KS_SPELL_AGGRESSIVE, &english, &ignore),
          "single letters are untouched");

    /* Error model prefers a plausible typo over a random one-letter change. */
    CHECK(ks_spell_correct(L"teh", KS_SPELL_BALANCED, &english, &ignore, &result) &&
              result.best_rank == 77,
          "the winner is the frequent word");

    /* --- Persian --- */
    CHECK(corrects_to(L"نسحه", KS_SPELL_BALANCED, &persian, &ignore, L"نسخه"),
          "نسحه -> نسخه");
    CHECK(corrects_to(L"نسهخ", KS_SPELL_BALANCED, &persian, &ignore, L"نسخه"),
          "transposed نسهخ -> نسخه");
    CHECK(corrects_to(L"سلاام", KS_SPELL_BALANCED, &persian, &ignore, L"سلام"),
          "doubled letter in سلاام -> سلام");
    CHECK(untouched(L"حتماً", KS_SPELL_BALANCED, &persian, &ignore),
          "diacritics are outside the model (left to the layout logic)");
    CHECK(corrects_to(L"خانع", KS_SPELL_BALANCED, &persian, &ignore, L"خانه"),
          "خانع -> خانه by neighbouring key");
    CHECK(corrects_to(L"صلام", KS_SPELL_BALANCED, &persian, &ignore, L"سلام"),
          "homophone صلام -> سلام");
    CHECK(ks_persian_confusable(L'ی', L'ئ') && ks_persian_confusable(L'ا', L'أ') &&
              ks_keys_adjacent(L'ئ', L'ی'),
          "hamza forms are part of the alphabet, confusion sets, and key map");
    CHECK(untouched(L"سلم", KS_SPELL_BALANCED, &persian, &ignore),
          "سلم is a listed word and is never changed");
    CHECK(untouched(L"نسخه", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "correct Persian is never changed");
    CHECK(untouched(L"میخوام", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "casual Persian in the table is accepted");
    CHECK(untouched(L"ایا", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "unshifted alef spellings stay with the canonical-alef rule");
    CHECK(untouched(L"می‌خواهم", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "a token with ZWNJ is outside the model");
    CHECK(untouched(L"کتاب۲", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "digits are outside the model");
    CHECK(untouched(L"شهرداری", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "a dictionary word absent from the rank table is still known");

    /* --- joins: missing ZWNJ, missing space --- */
    CHECK(corrects_to(L"میپرسیدند", KS_SPELL_BALANCED, &persian, &ignore, L"می\u200Cپرسیدند"),
          "missing ZWNJ after می is inserted");
    CHECK(ks_spell_correct(L"میپرسیدند", KS_SPELL_BALANCED, &persian, &ignore, &result) &&
              result.kind == KS_SPELL_KIND_ZWNJ,
          "the ZWNJ repair is reported as such");
    CHECK(untouched(L"بزرگتر", KS_SPELL_BALANCED, &persian, &ignore) &&
              corrects_to(L"بزرگتر", KS_SPELL_AGGRESSIVE, &persian, &ignore, L"بزرگ\u200Cتر"),
          "suffix ZWNJ (بزرگ‌تر) is Aggressive-only");
    CHECK(corrects_to(L"کتابها", KS_SPELL_AGGRESSIVE, &persian, &ignore, L"کتاب\u200Cها"),
          "missing ZWNJ before ها is inserted in Aggressive");
    CHECK(untouched(L"میپرسیدند", KS_SPELL_CONSERVATIVE, &persian, &ignore),
          "conservative mode does not normalise ZWNJ");
    CHECK(corrects_to(L"نمیخواهم", KS_SPELL_BALANCED, &persian, &ignore, L"نمی\u200Cخواهم"),
          "missing ZWNJ after نمی is inserted");
    CHECK(untouched(L"میهمان", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "a noun that merely starts with می is left whole (in the table)");
    {
        /* Same noun absent from the table: the verb-shape rule must still
           refuse to cut it, since همان is a very frequent word. */
        KS_SPELL_LEXICON without = persian;
        KS_RANK_TABLE small;
        unsigned char *data = load_file("tests/fixtures/rank-fixture-fa-noun.bin", &fa_rank_size);
        if (data) {
            CHECK(ks_rank_table_init(&small, data, fa_rank_size), "noun fixture validates");
            without.ranks = &small;
            CHECK(untouched(L"میهمان", KS_SPELL_AGGRESSIVE, &without, &ignore),
                  "میهمان is not split into می‌همان even when unknown");
            CHECK(corrects_to(L"میپرسیدند", KS_SPELL_BALANCED, &without, &ignore, L"می\u200Cپرسیدند"),
                  "verb-shaped stems are still joined with the noun fixture");
            free(data);
        }
    }
    CHECK(untouched(L"میخوام", KS_SPELL_AGGRESSIVE, &persian, &ignore),
          "a casual joined form present in the table is left alone");
    CHECK(corrects_to(L"درخانه", KS_SPELL_BALANCED, &persian, &ignore, L"در خانه"),
          "two common Persian words without a space are split");
    CHECK(corrects_to(L"inthe", KS_SPELL_BALANCED, &english, &ignore, L"in the"),
          "two common English words without a space are split");
    CHECK(corrects_to(L"alot", KS_SPELL_BALANCED, &english, &ignore, L"a lot"),
          "alot -> a lot");
    CHECK(ks_spell_correct(L"alot", KS_SPELL_BALANCED, &english, &ignore, &result) &&
              result.kind == KS_SPELL_KIND_SPLIT,
          "the split repair is reported as such");
    CHECK(untouched(L"lottee", KS_SPELL_BALANCED, &english, &ignore),
          "a split needs both halves to be everyday words (tee is too rare)");
    CHECK(corrects_to(L"verymuch", KS_SPELL_CONSERVATIVE, &english, &ignore, L"very much"),
          "conservative mode still splits two very common words");

    /* --- learned vocabulary --- */
    {
        KS_VOCAB vocab;
        KS_SPELL_LEXICON learning = english;
        ks_vocab_reset(&vocab);
        learning.vocabulary = &vocab;
        CHECK(corrects_to(L"teh", KS_SPELL_BALANCED, &learning, &ignore, L"the"),
              "an unseen word is still corrected");
        CHECK(ks_vocab_observe(&vocab, L"teh") == 1 &&
                  corrects_to(L"teh", KS_SPELL_BALANCED, &learning, &ignore, L"the"),
              "one sighting is not yet trust");
        CHECK(ks_vocab_observe(&vocab, L"teh") == 2 &&
                  untouched(L"teh", KS_SPELL_AGGRESSIVE, &learning, &ignore),
              "a word typed twice is the user's word");
        ks_vocab_trust(&vocab, L"wrold");
        CHECK(untouched(L"wrold", KS_SPELL_AGGRESSIVE, &learning, &ignore),
              "a trusted word is never corrected");
        {
            int i;
            wchar_t filler[KS_MAX_WORD + 1];
            for (i = 0; i < KS_VOCAB_CAPACITY + 10; ++i) {
                swprintf(filler, KS_MAX_WORD + 1, L"v%d", i);
                ks_vocab_observe(&vocab, filler);
            }
            CHECK(vocab.count == KS_VOCAB_CAPACITY, "vocabulary is bounded");
            CHECK(!ks_vocab_trusted(&vocab, L"wrold"),
                  "the oldest vocabulary entries are recycled");
        }
        CHECK(ks_vocab_observe(NULL, L"x") == 0 && !ks_vocab_trusted(NULL, L"x"),
              "vocabulary is optional");
    }

    /* --- ignore list --- */
    ks_ignore_list_add(&ignore, L"teh");
    CHECK(untouched(L"teh", KS_SPELL_AGGRESSIVE, &english, &ignore),
          "an undone correction is not repeated");
    CHECK(corrects_to(L"wrold", KS_SPELL_BALANCED, &english, &ignore, L"world"),
          "the ignore list is exact-match only");
    {
        int i;
        wchar_t filler[KS_MAX_WORD + 1];
        for (i = 0; i < KS_IGNORE_LIST_CAPACITY + 5; ++i) {
            swprintf(filler, KS_MAX_WORD + 1, L"w%d", i);
            ks_ignore_list_add(&ignore, filler);
        }
        CHECK(ignore.count == KS_IGNORE_LIST_CAPACITY, "ignore list is bounded");
        CHECK(!ks_ignore_list_contains(&ignore, L"teh"),
              "the oldest entries are evicted first");
    }

    /* --- robustness --- */
    CHECK(!ks_spell_correct(NULL, KS_SPELL_BALANCED, &english, &ignore, &result),
          "NULL word is rejected");
    CHECK(!ks_spell_correct(L"teh", KS_SPELL_BALANCED, NULL, &ignore, &result),
          "NULL lexicon is rejected");
    CHECK(ks_spell_correct(L"teh", KS_SPELL_BALANCED, &english, NULL, &result) &&
              result.should_correct,
          "NULL ignore list is allowed");
    {
        KS_SPELL_LEXICON no_ranks = english;
        no_ranks.ranks = &broken;
        CHECK(!ks_spell_correct(L"teh", KS_SPELL_BALANCED, &no_ranks, &ignore, &result),
              "an invalid rank table disables correction safely");
    }
    {
        wchar_t longest[KS_MAX_WORD + 2];
        int i;
        for (i = 0; i < KS_MAX_WORD; ++i) longest[i] = L'a';
        longest[KS_MAX_WORD] = 0;
        CHECK(!ks_spell_correct(longest, KS_SPELL_AGGRESSIVE, &english, &ignore, &result),
              "a maximum-length word never overruns the candidate buffer");
    }

    free(en_rank_data);
    free(fa_rank_data);
    free(en_data);
    free(fa_data);
    free(en_common_data);
    free(fa_common_data);
    puts("All spelling tests passed.");
    return 0;
}
