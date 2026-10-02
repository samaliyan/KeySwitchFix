/*
 * Language packs and language pairs other than English/Persian: the pack
 * parser (valid, truncated and corrupted files), the language profiles, and
 * the engine on Russian/English and German/English with small fixture packs
 * (tests/fixtures/ru-mini.kslang and de-mini.kslang, built by tools/build_language_pack.py from
 * the *-mini.txt word lists).
 */
#include "../src/core.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value, message) do { if (!(value)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)

static unsigned char *g_loaded[16];
static int g_loaded_count;

static void free_loaded(void) {
    while (g_loaded_count > 0) free(g_loaded[--g_loaded_count]);
}

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
    if (g_loaded_count == 0) atexit(free_loaded);
    if (g_loaded_count < 16) g_loaded[g_loaded_count++] = data;
    return data;
}

/* Physical keys -> characters of the US English layout and of a second
   layout given as a table of (scan code, unshifted character). */
typedef struct KEY { unsigned scan; wchar_t english; wchar_t other; } KEY;

static const KEY RUSSIAN[] = {
    {0x10, L'q', 0x0439}, {0x11, L'w', 0x0446}, {0x12, L'e', 0x0443}, {0x13, L'r', 0x043A},
    {0x14, L't', 0x0435}, {0x15, L'y', 0x043D}, {0x16, L'u', 0x0433}, {0x17, L'i', 0x0448},
    {0x18, L'o', 0x0449}, {0x19, L'p', 0x0437}, {0x1A, L'[', 0x0445}, {0x1B, L']', 0x044A},
    {0x1E, L'a', 0x0444}, {0x1F, L's', 0x044B}, {0x20, L'd', 0x0432}, {0x21, L'f', 0x0430},
    {0x22, L'g', 0x043F}, {0x23, L'h', 0x0440}, {0x24, L'j', 0x043E}, {0x25, L'k', 0x043B},
    {0x26, L'l', 0x0434}, {0x27, L';', 0x0436}, {0x28, L'\'', 0x044D}, {0x2C, L'z', 0x044F},
    {0x2D, L'x', 0x0447}, {0x2E, L'c', 0x0441}, {0x2F, L'v', 0x043C}, {0x30, L'b', 0x0438},
    {0x31, L'n', 0x0442}, {0x32, L'm', 0x044C}, {0x33, L',', 0x0431}, {0x34, L'.', 0x044E},
    {0x29, L'`', 0x0451}
};

/* German QWERTZ differs from US English on y/z and the umlaut keys. */
static const KEY GERMAN[] = {
    {0x10, L'q', L'q'}, {0x11, L'w', L'w'}, {0x12, L'e', L'e'}, {0x13, L'r', L'r'},
    {0x14, L't', L't'}, {0x15, L'y', L'z'}, {0x16, L'u', L'u'}, {0x17, L'i', L'i'},
    {0x18, L'o', L'o'}, {0x19, L'p', L'p'}, {0x1A, L'[', 0x00FC}, {0x1E, L'a', L'a'},
    {0x1F, L's', L's'}, {0x20, L'd', L'd'}, {0x21, L'f', L'f'}, {0x22, L'g', L'g'},
    {0x23, L'h', L'h'}, {0x24, L'j', L'j'}, {0x25, L'k', L'k'}, {0x26, L'l', L'l'},
    {0x27, L';', 0x00F6}, {0x28, L'\'', 0x00E4}, {0x2C, L'z', L'y'}, {0x2D, L'x', L'x'},
    {0x2E, L'c', L'c'}, {0x2F, L'v', L'v'}, {0x30, L'b', L'b'}, {0x31, L'n', L'n'},
    {0x32, L'm', L'm'}, {0x0C, L'-', 0x00DF}
};

/* Tokens for `word` written in `layout` (0: English side, 1: other side). */
static int tokens_for(const KEY *keys, size_t key_count, const wchar_t *word, int other,
                      KS_TOKEN *tokens) {
    int count = 0;
    for (; *word; ++word) {
        size_t i;
        int found = 0;
        for (i = 0; i < key_count && !found; ++i) {
            if ((other ? keys[i].other : keys[i].english) == *word) {
                tokens[count].a = keys[i].english;
                tokens[count].b = keys[i].other;
                ++count;
                found = 1;
            }
        }
        if (!found || count >= KS_MAX_WORD) return 0;
    }
    return count;
}

static int load_bloom(const char *path, KS_BLOOM *bloom) {
    size_t size;
    unsigned char *data = load_file(path, &size);
    return data && ks_bloom_init(bloom, data, size);
}

int main(void) {
    KS_LANG_PACK russian;
    KS_LANG_PACK german;
    KS_LANG_PACK broken;
    KS_BLOOM en_words, en_common, en_frequent, en_prefixes, en_common_prefixes;
    KS_LEXICONS lexicons;
    KS_DECISION decision;
    KS_TOKEN tokens[KS_MAX_WORD];
    unsigned char *data;
    size_t size;
    int count;
    size_t i;
    wchar_t form[KS_MAX_WORD + 1];

    setlocale(LC_ALL, "C.UTF-8");

    /* ---- The pack format ---- */
    data = load_file("tests/fixtures/ru-mini.kslang", &size);
    CHECK(data && ks_pack_parse(data, size, &russian), "the Russian fixture pack loads");
    CHECK(strcmp(russian.profile.code, "ru") == 0 && wcscmp(russian.english_name, L"Russian") == 0 &&
          wcscmp(russian.native_name, L"\x0420\x0443\x0441\x0441\x043A\x0438\x0439") == 0,
          "code and names are read");
    CHECK(russian.langid_count == 1 && russian.langids[0] == 0x19, "the Windows language id is read");
    CHECK((russian.profile.flags & KS_PROFILE_CASED) && russian.profile.model == KS_LANG_OTHER,
          "flags and model are read");
    CHECK(ks_profile_short_word(&russian.profile, L"\x0438") &&
          ks_profile_short_word(&russian.profile, L"\x043D\x0435"), "short words (и, не) are read");
    CHECK(ks_bloom_contains(&russian.words, L"\x043F\x0440\x0438\x0432\x0435\x0442") &&
          ks_bloom_contains(&russian.prefixes, L"\x043F\x0440\x0438"), "words and prefixes are present");
    {
        /* Every truncation and every single-byte corruption of the header
           and section table is either rejected or still in bounds. */
        static unsigned char copy[32768];
        size_t cut;
        CHECK(size <= sizeof(copy), "fixture fits the scratch buffer");
        for (cut = 0; cut < size; cut += (cut < 300 ? 1 : 997))
            CHECK(!ks_pack_parse(data, cut, &broken) || cut >= size, "a truncated pack is rejected");
        for (i = 0; i < 132 + 6 * 12; ++i) {
            int bit;
            for (bit = 0; bit < 8; ++bit) {
                memcpy(copy, data, size);
                copy[i] ^= (unsigned char)(1u << bit);
                if (ks_pack_parse(copy, size, &broken)) {
                    /* Accepted: then every Bloom lies inside the buffer and
                       has whole bytes to read. */
                    const KS_BLOOM *blooms[5];
                    int b;
                    blooms[0] = &broken.words;
                    blooms[1] = &broken.common;
                    blooms[2] = &broken.frequent;
                    blooms[3] = &broken.prefixes;
                    blooms[4] = &broken.common_prefixes;
                    for (b = 0; b < 5; ++b) {
                        CHECK(!blooms[b]->valid ||
                              (blooms[b]->bit_count >= 64 && blooms[b]->bits >= copy &&
                               blooms[b]->bits + blooms[b]->bit_count / 8 <= copy + size),
                              "an accepted corrupted pack stays in bounds");
                        (void)ks_bloom_contains(blooms[b], L"\x043F\x0440\x0438");
                    }
                }
            }
        }
        memcpy(copy, data, size);
        copy[0] = 'X';
        CHECK(!ks_pack_parse(copy, size, &broken), "a wrong magic is rejected");
        memcpy(copy, data, size);
        copy[8] = 'R';   /* upper-case code */
        CHECK(!ks_pack_parse(copy, size, &broken), "a malformed language code is rejected");
    }

    /* ---- Profiles ---- */
    CHECK(ks_lookup_form(&russian.profile, L"\x041F\x0420\x0418\x0412\x0415\x0422", form) == 6 &&
          wcscmp(form, L"\x043F\x0440\x0438\x0432\x0435\x0442") == 0, "Cyrillic is lower-cased");
    CHECK(!ks_lookup_form(&russian.profile, L"\x043F\x0440\x0438;", form), "punctuation is not a word");
    CHECK(ks_lookup_form(ks_profile_english(), L"Don't", form) == 5 && wcscmp(form, L"don't") == 0,
          "English keeps one inner apostrophe");
    CHECK(!ks_lookup_form(ks_profile_english(), L"caf\x00E9", form), "English is ASCII only");
    CHECK(ks_to_lower(0x00C4) == 0x00E4 && ks_to_lower(0x0130) == L'i' && ks_to_lower(0x0391) == 0x03B1 &&
          ks_to_lower(0x0401) == 0x0451, "lower case for Latin-1, Turkish, Greek and Cyrillic");
    CHECK(ks_is_letter(0x05D0) && ks_is_letter(0x0628) && !ks_is_letter(L'.') && !ks_is_letter(L'7') &&
          !ks_is_letter(0x061F), "letters and punctuation are told apart");

    /* ---- Russian / English ---- */
    CHECK(load_bloom("resources/en.bloom", &en_words) && load_bloom("resources/en-common.bloom", &en_common) &&
          load_bloom("resources/en-frequent.bloom", &en_frequent) &&
          load_bloom("resources/en-prefix.bloom", &en_prefixes) &&
          load_bloom("resources/en-common-prefix.bloom", &en_common_prefixes), "English dictionaries load");
    memset(&lexicons, 0, sizeof(lexicons));
    lexicons.profile[KS_SLOT_A] = ks_profile_english();
    lexicons.profile[KS_SLOT_B] = &russian.profile;
    lexicons.words[KS_SLOT_A] = &en_words;
    lexicons.common[KS_SLOT_A] = &en_common;
    lexicons.frequent[KS_SLOT_A] = &en_frequent;
    lexicons.prefixes[KS_SLOT_A] = &en_prefixes;
    lexicons.common_prefixes[KS_SLOT_A] = &en_common_prefixes;
    lexicons.words[KS_SLOT_B] = &russian.words;
    lexicons.common[KS_SLOT_B] = &russian.common;
    lexicons.frequent[KS_SLOT_B] = &russian.frequent;
    lexicons.prefixes[KS_SLOT_B] = &russian.prefixes;
    lexicons.common_prefixes[KS_SLOT_B] = &russian.common_prefixes;

    count = tokens_for(RUSSIAN, sizeof(RUSSIAN) / sizeof(RUSSIAN[0]), L"\x043F\x0440\x0438\x0432\x0435\x0442", 1, tokens);
    CHECK(count == 6, "привет maps to keys");
    CHECK(ks_evaluate_contextual(tokens, count, KS_SLOT_A, 1, KS_SLOT_NONE, 0, 0, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.original, L"ghbdtn") == 0 &&
          wcscmp(decision.replacement, L"\x043F\x0440\x0438\x0432\x0435\x0442") == 0 &&
          decision.target_slot == KS_SLOT_B,
          "ghbdtn typed on the English layout becomes привет");
    CHECK(ks_evaluate_contextual(tokens, count, KS_SLOT_B, 1, KS_SLOT_NONE, 0, 0, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE,
          "привет on the Russian layout is left alone");
    count = tokens_for(RUSSIAN, sizeof(RUSSIAN) / sizeof(RUSSIAN[0]), L"hello", 0, tokens);
    CHECK(count == 5 && ks_evaluate_contextual(tokens, count, KS_SLOT_B, 1, KS_SLOT_NONE, 0, 0,
                                               KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.original, L"\x0440\x0443\x0434\x0434\x0449") == 0 &&
          wcscmp(decision.replacement, L"hello") == 0,
          "руддщ typed on the Russian layout becomes hello");
    count = tokens_for(RUSSIAN, sizeof(RUSSIAN) / sizeof(RUSSIAN[0]), L"hello", 0, tokens);
    CHECK(ks_evaluate_contextual(tokens, count, KS_SLOT_A, 1, KS_SLOT_NONE, 0, 0, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE, "hello on the English layout stays");
    /* Letters on punctuation keys: б and ю are on , and . in Russian. */
    count = tokens_for(RUSSIAN, sizeof(RUSSIAN) / sizeof(RUSSIAN[0]),
                       L"\x043B\x044E\x0431\x043E\x0432\x044C", 1, tokens);   /* любовь */
    CHECK(count == 6 && ks_evaluate_contextual(tokens, count, KS_SLOT_A, 1, KS_SLOT_NONE, 0, 0,
                                               KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.original, L"k.,jdm") == 0, "k.,jdm becomes любовь");
    /* English words ending with a period are not taken for Russian. */
    count = tokens_for(RUSSIAN, sizeof(RUSSIAN) / sizeof(RUSSIAN[0]), L"end.", 0, tokens);
    CHECK(count == 4 && ks_evaluate_contextual(tokens, count, KS_SLOT_A, 1, KS_SLOT_NONE, 0, 0,
                                               KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_NONE,
          "end. on the English layout stays English");
    /* Sentence evidence works for any pair. */
    {
        KS_TOKEN first[KS_MAX_WORD], second[KS_MAX_WORD];
        KS_SEQUENCE_WORD words[2];
        KS_SEQUENCE_RESULT result;
        words[0].tokens = first;
        words[0].count = tokens_for(RUSSIAN, sizeof(RUSSIAN) / sizeof(RUSSIAN[0]),
                                    L"\x044D\x0442\x043E", 1, first);          /* это */
        words[1].tokens = second;
        words[1].count = tokens_for(RUSSIAN, sizeof(RUSSIAN) / sizeof(RUSSIAN[0]),
                                    L"\x0440\x0430\x0431\x043E\x0442\x0430", 1, second);   /* работа */
        CHECK(ks_evaluate_sequence(words, 2, 1, KS_SLOT_NONE, 0, &lexicons, &result) &&
              result.slot == KS_SLOT_B, "a run of Russian words typed on the English layout is Russian");
    }

    /* ---- German / English (both Latin) ---- */
    data = load_file("tests/fixtures/de-mini.kslang", &size);
    CHECK(data && ks_pack_parse(data, size, &german), "the German fixture pack loads");
    CHECK(ks_lookup_form(&german.profile, L"Stra\x00DF" L"e", form) && wcscmp(form, L"stra\x00DF" L"e") == 0,
          "ß is a German letter");
    lexicons.profile[KS_SLOT_B] = &german.profile;
    lexicons.words[KS_SLOT_B] = &german.words;
    lexicons.common[KS_SLOT_B] = &german.common;
    lexicons.frequent[KS_SLOT_B] = &german.frequent;
    lexicons.prefixes[KS_SLOT_B] = &german.prefixes;
    lexicons.common_prefixes[KS_SLOT_B] = &german.common_prefixes;
    count = tokens_for(GERMAN, sizeof(GERMAN) / sizeof(GERMAN[0]), L"zeitung", 1, tokens);
    CHECK(count == 7 && ks_evaluate_contextual(tokens, count, KS_SLOT_A, 1, KS_SLOT_NONE, 0, 0,
                                               KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.original, L"yeitung") == 0 && wcscmp(decision.replacement, L"zeitung") == 0,
          "yeitung (Zeitung typed on the US layout) becomes zeitung");
    count = tokens_for(GERMAN, sizeof(GERMAN) / sizeof(GERMAN[0]), L"m\x00E4" L"dchen", 1, tokens);
    CHECK(count == 7 && ks_evaluate_contextual(tokens, count, KS_SLOT_A, 1, KS_SLOT_NONE, 0, 0,
                                               KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.replacement, L"m\x00E4" L"dchen") == 0, "m'dchen becomes mädchen");
    count = tokens_for(GERMAN, sizeof(GERMAN) / sizeof(GERMAN[0]), L"haus", 1, tokens);
    CHECK(ks_evaluate_contextual(tokens, count, KS_SLOT_A, 1, KS_SLOT_NONE, 0, 0, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE,
          "a word that reads the same in both layouts is never touched");
    count = tokens_for(GERMAN, sizeof(GERMAN) / sizeof(GERMAN[0]), L"yes", 0, tokens);
    CHECK(count == 3 && ks_evaluate_contextual(tokens, count, KS_SLOT_B, 1, KS_SLOT_NONE, 0, 0,
                                               KS_PHASE_BOUNDARY, &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.original, L"zes") == 0 && wcscmp(decision.replacement, L"yes") == 0,
          "zes (yes typed on the German layout) becomes yes");

    /* ---- A Bloom too small to hold one byte is refused ---- */
    {
        static const unsigned char tiny[16] = {'K', 'S', 'W', 'B', 1, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0};
        KS_BLOOM bloom;
        CHECK(!ks_bloom_init(&bloom, tiny, sizeof(tiny)), "a 4-bit Bloom filter is refused");
    }

    puts("All language pack tests passed.");
    return 0;
}
