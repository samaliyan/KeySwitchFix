/* Writing memory, vocabulary packs, and their hooks into core and spell. */
#include "../src/core.h"
#include "../src/domain.h"
#include "../src/memory.h"
#include "../src/spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value, message) do { if (!(value)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)

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

static uint32_t ascii_scan(char c) {
    static const uint32_t scans[26] = {
        0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c };
    return scans[c - 'a'];
}

static int tokens_for(const char *keys, KS_TOKEN *tokens) {
    int count = 0;
    for (; *keys; ++keys) ks_map_scancode(ascii_scan(*keys), 0, 0, &tokens[count++]);
    return count;
}

/* The app's hooks, reduced to what the tests need. */
static KS_WRITING_MEMORY g_memory;

static int extra_contains(const void *context, KS_LANGUAGE language, const wchar_t *word) {
    (void)context;
    return ks_domain_contains(KS_DOMAIN_IT, language, word) ||
           ks_memory_word_count(&g_memory, word) >= KS_MEMORY_KNOWN_COUNT;
}

static int extra_prefix(const void *context, KS_LANGUAGE language, const wchar_t *prefix) {
    (void)context;
    return ks_domain_has_prefix(KS_DOMAIN_IT, language, prefix);
}

/* Same composition as app.c's spell_rank_adjust. */
static int rank_adjust(const void *context, const wchar_t *word, int table_rank) {
    const KS_LANGUAGE *language = (const KS_LANGUAGE *)context;
    if (table_rank < 0 && ks_domain_contains(KS_DOMAIN_IT, *language, word)) table_rank = 40;
    return ks_memory_rank_adjust(&g_memory, word, table_rank);
}

int main(void) {
    static KS_WRITING_MEMORY copy;
    static wchar_t text[400000];
    KS_TOKEN tokens[KS_MAX_WORD];
    size_t size = 0;
    int i;

    /* ---- Vocabulary pack ---- */
    CHECK(ks_domain_self_check(), "pack lists are sorted for binary search");
    CHECK(ks_domain_word_count(KS_DOMAIN_IT, KS_LANG_ENGLISH) > 1000, "IT pack has 1000+ English terms");
    CHECK(ks_domain_word_count(KS_DOMAIN_IT, KS_LANG_PERSIAN) > 300, "IT pack has 300+ Persian terms");
    CHECK(ks_domain_contains(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"kubernetes") &&
          ks_domain_contains(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"tablespace") &&
          ks_domain_contains(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"rman") &&
          ks_domain_contains(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"nginx"),
          "IT terms are in the English pack");
    CHECK(ks_domain_contains(KS_DOMAIN_IT, KS_LANG_PERSIAN, L"\x06A9\x0627\x0646\x0641\x06CC\x06AF"),
          "کانفیگ is in the Persian pack");
    CHECK(ks_domain_contains(KS_DOMAIN_IT, KS_LANG_PERSIAN, L"\x06AF\x06CC\x062A\x0648\x06CC"),
          "a ZWNJ entry (گیت‌وی) is stored joined, like the base dictionary");
    CHECK(!ks_domain_contains(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"kubernete"), "exact match only");
    CHECK(!ks_domain_contains(0, KS_LANG_ENGLISH, L"kubernetes"), "a disabled pack is not consulted");
    CHECK(!ks_domain_contains(KS_DOMAIN_IT, KS_LANG_PERSIAN, L"kubernetes"), "languages are separate");
    CHECK(ks_domain_has_prefix(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"kuber") &&
          ks_domain_has_prefix(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"kubernetes") &&
          !ks_domain_has_prefix(KS_DOMAIN_IT, KS_LANG_ENGLISH, L"kubz"),
          "prefix search");

    /* ---- Distance and repair pairs ---- */
    CHECK(ks_memory_distance(L"abc", L"abc", 2) == 0, "distance 0");
    CHECK(ks_memory_distance(L"teh", L"the", 2) == 1, "transposition is one edit");
    CHECK(ks_memory_distance(L"kitten", L"sitting", 2) == 3, "capped above the limit");
    CHECK(ks_memory_distance(L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645", 2) == 2,
          "عسیسم → عزیزم is two substitutions");
    CHECK(ks_memory_is_fix_pair(L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645"),
          "عسیسم → عزیزم is a repair");
    CHECK(!ks_memory_is_fix_pair(L"\x0639\x0632\x06CC", L"\x0639\x0632\x06CC\x0632\x0645"),
          "an unfinished word is not a typo");
    CHECK(!ks_memory_is_fix_pair(L"\x06A9\x062A\x0627\x0628\x0647\x0627", L"\x06A9\x062A\x0627\x0628"),
          "dropping a suffix is a change of mind");
    CHECK(ks_memory_is_fix_pair(L"helloo", L"hello"), "one extra final letter is a repair");
    CHECK(!ks_memory_is_fix_pair(L"good", L"bad"), "a different word is not a repair");
    CHECK(!ks_memory_is_fix_pair(L"abcd", L"abxy"), "short words allow one edit only");
    CHECK(!ks_memory_is_fix_pair(L"ab", L"ba"), "two-letter words are never learned");
    CHECK(!ks_memory_is_fix_pair(L"same", L"same"), "identical text is not a repair");
    CHECK(ks_memory_is_fix_pair(L"help", L"hello") && !ks_memory_is_fix_pair_midword(L"help", L"hello"),
          "a shorter mid-word slip (hel-p on the way to hello) is not learned");
    CHECK(ks_memory_is_fix_pair_midword(L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645"),
          "a full-length word repaired mid-typing is learned");

    /* ---- Learning a repair ---- */
    ks_memory_reset(&g_memory);
    CHECK(ks_memory_observe_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645") == 1,
          "first sighting counted");
    CHECK(!ks_memory_lookup_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", 0), "not applied after one sighting");
    CHECK(ks_memory_observe_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645") == 2,
          "second sighting counted");
    CHECK(ks_memory_lookup_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", 0) &&
          wcscmp(ks_memory_lookup_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", 0),
                 L"\x0639\x0632\x06CC\x0632\x0645") == 0,
          "applied from the second sighting");
    CHECK(!ks_memory_lookup_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", 1),
          "a typo that is a real word needs a third sighting");
    CHECK(ks_memory_active_fix_count(&g_memory) == 1, "one active fix");
    CHECK(ks_memory_fix_count(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645") == 2 &&
          ks_memory_fix_count(&g_memory, L"nothing") == 0, "fix counts");
    ks_memory_observe_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645");
    CHECK(ks_memory_lookup_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", 1) != NULL,
          "a typo that is a real word is applied from the third sighting");
    ks_memory_reject_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645");
    CHECK(!ks_memory_lookup_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", 0), "undo unlearns");
    ks_memory_observe_fix(&g_memory, L"recieve", L"receive");
    ks_memory_observe_fix(&g_memory, L"recieve", L"relieve");
    CHECK(ks_memory_observe_fix(&g_memory, L"recieve", L"receive") == 1, "a different repair restarts the count");
    CHECK(ks_memory_observe_fix(&g_memory, L"abc", L"abcdef") == 0, "implausible pairs are not recorded");
    for (i = 0; i < KS_MEMORY_FIXES + 20; ++i) {
        wchar_t typo[16];
        wchar_t fix[16];
        swprintf(typo, 16, L"wordx%dq", i);
        swprintf(fix, 16, L"wordx%d", i);
        ks_memory_observe_fix(&g_memory, typo, fix);
    }
    CHECK(g_memory.fix_count == KS_MEMORY_FIXES, "fix table bounded");

    /* ---- Words ---- */
    ks_memory_reset(&g_memory);
    for (i = 0; i < 3; ++i) ks_memory_observe_word(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634");
    CHECK(ks_memory_word_count(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634") == 3, "word counted");
    CHECK(ks_memory_word_count(&g_memory, L"none") == 0, "absent word");
    ks_memory_observe_word(&g_memory, L"a");
    ks_memory_observe_word(&g_memory, L"has space");
    CHECK(ks_memory_word_count(&g_memory, L"a") == 0 && ks_memory_word_count(&g_memory, L"has space") == 0,
          "one-letter and spaced tokens ignored");
    for (i = 0; i < KS_MEMORY_WORDS + 100; ++i) {
        wchar_t word[16];
        swprintf(word, 16, L"w%05d", i);
        ks_memory_observe_word(&g_memory, word);
    }
    CHECK(g_memory.word_count <= KS_MEMORY_WORDS, "word table bounded");
    CHECK(ks_memory_word_count(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634") == 3,
          "a frequent word survives eviction");
    CHECK(ks_memory_word_count(&g_memory, L"w04099") == 1, "the newest word is present after eviction");
    ks_memory_unobserve_word(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634");
    CHECK(ks_memory_word_count(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634") == 2, "unobserve takes one back");
    ks_memory_observe_word(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634");
    /* Eviction is by count, then recency: not by alphabet. */
    {
        static KS_WRITING_MEMORY fair;
        int persian_left = 0;
        ks_memory_reset(&fair);
        for (i = 0; i < 2048; ++i) {
            wchar_t word[16];
            swprintf(word, 16, L"\x0633%05d", i);
            ks_memory_observe_word(&fair, word);
            swprintf(word, 16, L"e%05d", i);
            ks_memory_observe_word(&fair, word);
        }
        for (i = 0; i < 400; ++i) {
            wchar_t word[16];
            swprintf(word, 16, L"n%05d", i);
            ks_memory_observe_word(&fair, word);
        }
        for (i = 0; i < 2048; ++i) {
            wchar_t word[16];
            swprintf(word, 16, L"\x0633%05d", i);
            if (ks_memory_word_count(&fair, word)) ++persian_left;
        }
        CHECK(persian_left > 1500, "Persian words are not evicted before English ones");
        CHECK(ks_memory_word_count(&fair, L"n00399") == 1, "the most recent word survives");
    }
    /* The shipped rank formula. */
    CHECK(ks_memory_rank_adjust(&g_memory, L"unseen", -1) == -1, "unknown and unseen stays unknown");
    CHECK(ks_memory_rank_adjust(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634", -1) == 32,
          "a word typed three times ranks as zipf 3.2");
    CHECK(ks_memory_rank_adjust(&g_memory, L"unseen", 50) == 50, "an unseen dictionary word is unchanged");
    CHECK(ks_memory_rank_adjust(&g_memory, L"\x0633\x06CC\x0627\x0648\x0634", 50) == 52,
          "a favoured dictionary word is lifted");

    /* ---- Text round trip ---- */
    ks_memory_observe_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645");
    ks_memory_observe_fix(&g_memory, L"\x0639\x0633\x06CC\x0633\x0645", L"\x0639\x0632\x06CC\x0632\x0645");
    size = ks_memory_serialize(&g_memory, NULL, 0);
    CHECK(size > 0 && size < sizeof(text) / sizeof(text[0]), "serialised size fits");
    CHECK(ks_memory_serialize(&g_memory, text, sizeof(text) / sizeof(text[0])) == size, "size is exact");
    CHECK(wcsstr(text, L"[fixes]\n\x0639\x0633\x06CC\x0633\x0645\t\x0639\x0632\x06CC\x0632\x0645\t2\n") != NULL,
          "fix line format");
    CHECK(wcsstr(text, L"[words]\n\x0633\x06CC\x0627\x0648\x0634\t3\n") != NULL, "most frequent word first");
    CHECK(wcsstr(text, L"w00001\t") == NULL, "words typed once are not written to disk");
    CHECK(ks_memory_serialize(&g_memory, text, 50) == size && wcslen(text) == 49,
          "a small buffer is filled and terminated, the full size reported");
    ks_memory_serialize(&g_memory, text, sizeof(text) / sizeof(text[0]));
    CHECK(ks_memory_parse(&copy, text), "parse");
    CHECK(copy.fix_count == g_memory.fix_count && copy.word_count >= 1, "sizes after the round trip");
    CHECK(ks_memory_word_count(&copy, L"\x0633\x06CC\x0627\x0648\x0634") == 3 &&
          ks_memory_lookup_fix(&copy, L"\x0639\x0633\x06CC\x0633\x0645", 0) != NULL,
          "contents survive the round trip");
    CHECK(!copy.dirty, "a freshly loaded memory is clean");
    CHECK(ks_memory_parse(&copy, L"\xFEFF# c\r\n[fixes]\r\nbad\r\nx\ty\t\r\nteh\tthe\t4\r\n[words]\r\nhello\t7\r\nbye\r\n") &&
          copy.fix_count == 1 && ks_memory_lookup_fix(&copy, L"teh", 1) &&
          ks_memory_word_count(&copy, L"hello") == 7 && ks_memory_word_count(&copy, L"bye") == 1,
          "hand-edited file with CRLF, BOM and junk lines");

    /* ---- Core: extra words make a word known ---- */
    {
        size_t en_size, fa_size, enc_size, fac_size, enp_size, fap_size;
        unsigned char *en_data = load_file("resources/en.bloom", &en_size);
        unsigned char *fa_data = load_file("resources/fa.bloom", &fa_size);
        unsigned char *enc_data = load_file("resources/en-common.bloom", &enc_size);
        unsigned char *fac_data = load_file("resources/fa-common.bloom", &fac_size);
        unsigned char *enp_data = load_file("resources/en-prefix.bloom", &enp_size);
        unsigned char *fap_data = load_file("resources/fa-prefix.bloom", &fap_size);
        KS_BLOOM en, fa, enc, fac, enp, fap;
        KS_LEXICONS lexicons;
        KS_EXTRA_WORDS extra;
        KS_DECISION decision;
        int english_known = 0;
        int persian_known = 0;
        int count;
        CHECK(en_data && fa_data && enc_data && fac_data && enp_data && fap_data, "blooms load");
        ks_bloom_init(&en, en_data, en_size);
        ks_bloom_init(&fa, fa_data, fa_size);
        ks_bloom_init(&enc, enc_data, enc_size);
        ks_bloom_init(&fac, fac_data, fac_size);
        ks_bloom_init(&enp, enp_data, enp_size);
        ks_bloom_init(&fap, fap_data, fap_size);
        memset(&lexicons, 0, sizeof(lexicons));
        lexicons.english_words = &en;
        lexicons.persian_words = &fa;
        lexicons.english_common = &enc;
        lexicons.persian_common = &fac;
        lexicons.english_prefixes = &enp;
        lexicons.persian_prefixes = &fap;
        ks_memory_reset(&g_memory);

        count = tokens_for("kubectl", tokens);
        ks_classify_word(tokens, count, &lexicons, &english_known, &persian_known, NULL, NULL);
        CHECK(!english_known, "kubectl is not in the base dictionary");
        CHECK(ks_evaluate_contextual(tokens, count, KS_LANG_PERSIAN, 1, KS_LANG_OTHER, 0, 0,
                                     KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_NONE,
              "without the pack, kubectl typed on the Persian layout stays gibberish");
        extra.contains = extra_contains;
        extra.has_prefix = extra_prefix;
        extra.context = NULL;
        lexicons.extra = &extra;
        ks_classify_word(tokens, count, &lexicons, &english_known, &persian_known, NULL, NULL);
        CHECK(english_known, "with the pack, kubectl is an English word");
        CHECK(ks_evaluate_contextual(tokens, count, KS_LANG_PERSIAN, 1, KS_LANG_OTHER, 0, 0,
                                     KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
              wcscmp(decision.replacement, L"kubectl") == 0,
              "kubectl typed on the Persian layout is repaired to English");
        count = tokens_for("tablespace", tokens);
        CHECK(ks_evaluate_contextual(tokens, count, KS_LANG_ENGLISH, 1, KS_LANG_OTHER, 0, 0,
                                     KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_NONE,
              "tablespace on the English layout is left alone");
        for (i = 3; i < 7; ++i)
            CHECK(ks_evaluate_contextual(tokens_for("kubernetes", tokens) ? tokens : tokens, i,
                                         KS_LANG_ENGLISH, 1, KS_LANG_PERSIAN, 4, 0,
                                         KS_PHASE_LIVE, &lexicons, &decision) != KS_LIVE_CORRECT_NOW,
                  "no prefix of kubernetes is switched to Persian while typing");
        /* A word from the user's memory counts once it is frequent. */
        count = tokens_for("siavash", tokens);
        ks_classify_word(tokens, count, &lexicons, &english_known, &persian_known, NULL, NULL);
        CHECK(!english_known, "a name is unknown at first");
        for (i = 0; i < KS_MEMORY_KNOWN_COUNT; ++i) ks_memory_observe_word(&g_memory, L"siavash");
        ks_classify_word(tokens, count, &lexicons, &english_known, &persian_known, NULL, NULL);
        CHECK(english_known, "a name typed three times is known");
    }

    /* ---- Spell: pack terms are never corrected; their typos are ---- */
    {
        size_t rank_size, en_size;
        unsigned char *rank_data = load_file("tests/fixtures/rank-fixture-en.bin", &rank_size);
        unsigned char *en_data = load_file("resources/en.bloom", &en_size);
        KS_RANK_TABLE ranks;
        KS_BLOOM en;
        KS_SPELL_LEXICON english;
        KS_SPELL_RESULT result;
        static const KS_LANGUAGE language = KS_LANG_ENGLISH;
        CHECK(rank_data && en_data && ks_rank_table_init(&ranks, rank_data, rank_size) &&
              ks_bloom_init(&en, en_data, en_size), "spelling fixtures load");
        memset(&english, 0, sizeof(english));
        english.language = KS_LANG_ENGLISH;
        english.ranks = &ranks;
        english.words = &en;
        ks_memory_reset(&g_memory);
        CHECK(!ks_spell_correct(L"kubernets", KS_SPELL_BALANCED, &english, NULL, &result) ||
              !result.should_correct, "without the pack there is no candidate");
        english.rank_adjust = rank_adjust;
        english.rank_adjust_context = &language;
        CHECK(ks_spell_known(L"kubernetes", &english), "a pack term is known");
        CHECK(ks_spell_correct(L"kubernets", KS_SPELL_BALANCED, &english, NULL, &result) &&
              result.should_correct && wcscmp(result.replacement, L"kubernetes") == 0,
              "kubernets -> kubernetes");
        CHECK(ks_spell_correct(L"tablspace", KS_SPELL_BALANCED, &english, NULL, &result) &&
              result.should_correct && wcscmp(result.replacement, L"tablespace") == 0,
              "tablspace -> tablespace");
        CHECK(!ks_spell_correct(L"rman", KS_SPELL_AGGRESSIVE, &english, NULL, &result) ||
              !result.should_correct, "rman is left alone even when aggressive");
        for (i = 0; i < KS_MEMORY_KNOWN_COUNT; ++i) ks_memory_observe_word(&g_memory, L"siavash");
        CHECK(ks_spell_correct(L"siavahs", KS_SPELL_BALANCED, &english, NULL, &result) &&
              result.should_correct && wcscmp(result.replacement, L"siavash") == 0,
              "a typo of the user's own frequent word is repaired to it");
    }

    printf("All writing memory and vocabulary tests passed.\n");
    return 0;
}
