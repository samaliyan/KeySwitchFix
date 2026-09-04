#include "../src/core.h"

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

static int make_tokens(const uint32_t *scans, int count, KS_TOKEN *tokens) {
    int i;
    for (i = 0; i < count; ++i) {
        if (!ks_map_scancode(scans[i], 0, 0, &tokens[i])) return 0;
    }
    return 1;
}

static uint32_t ascii_scan(char character) {
    static const uint32_t scans[26] = {
        0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c
    };
    if (character >= 'a' && character <= 'z')
        return scans[character - 'a'];
    if (character == ';') return 0x27;
    if (character == '\'') return 0x28;
    if (character == ',') return 0x33;
    if (character == '[') return 0x1a;
    if (character == ']') return 0x1b;
    return 0;
}

static int make_ascii_tokens(const char *keys, KS_TOKEN *tokens) {
    int count = 0;
    while (*keys) {
        uint32_t scan = ascii_scan(*keys++);
        if (!scan || count >= KS_MAX_WORD ||
            !ks_map_scancode(scan, 0, 0, &tokens[count])) return 0;
        ++count;
    }
    return count;
}

#define CHECK(value, message) do { if (!(value)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)

int main(void) {
    size_t en_size = 0, fa_size = 0;
    unsigned char *en_data = load_file("resources/en.bloom", &en_size);
    unsigned char *fa_data = load_file("resources/fa.bloom", &fa_size);
    size_t en_prefix_size = 0, fa_prefix_size = 0;
    size_t en_common_size = 0, fa_common_size = 0;
    size_t en_frequent_size = 0, fa_frequent_size = 0;
    size_t en_common_prefix_size = 0, fa_common_prefix_size = 0;
    unsigned char *en_prefix_data = load_file("resources/en-prefix.bloom", &en_prefix_size);
    unsigned char *fa_prefix_data = load_file("resources/fa-prefix.bloom", &fa_prefix_size);
    unsigned char *en_common_data = load_file("resources/en-common.bloom", &en_common_size);
    unsigned char *fa_common_data = load_file("resources/fa-common.bloom", &fa_common_size);
    unsigned char *en_frequent_data =
        load_file("resources/en-frequent.bloom", &en_frequent_size);
    unsigned char *fa_frequent_data =
        load_file("resources/fa-frequent.bloom", &fa_frequent_size);
    unsigned char *en_common_prefix_data =
        load_file("resources/en-common-prefix.bloom", &en_common_prefix_size);
    unsigned char *fa_common_prefix_data =
        load_file("resources/fa-common-prefix.bloom", &fa_common_prefix_size);
    KS_BLOOM en, fa, en_prefix, fa_prefix, en_common, fa_common;
    KS_BLOOM en_frequent, fa_frequent, en_common_prefix, fa_common_prefix;
    KS_LEXICONS lexicons;
    KS_LANGUAGE_CONTEXT context;
    KS_TOKEN tokens[KS_MAX_WORD];
    KS_TOKEN sequence_tokens[6][KS_MAX_WORD];
    KS_SEQUENCE_WORD sequence[6];
    KS_SEQUENCE_RESULT sequence_result;
    KS_DECISION decision;
    int english_known = 0, persian_known = 0;
    wchar_t mapped[KS_MAX_WORD + 1];
    const uint32_t password[] = {0x19,0x1E,0x1F,0x1F,0x11,0x18,0x13,0x20};
    const uint32_t salam[] = {0x1F,0x22,0x23,0x26};
    const uint32_t ketab[] = {0x27,0x24,0x23,0x21};
    const uint32_t hello[] = {0x23,0x12,0x26,0x26,0x18};
    const uint32_t behsa[] = {0x21,0x17,0x1F,0x23};
    const uint32_t behsazi[] = {0x21,0x17,0x1F,0x23,0x2E,0x20};
    const uint32_t komak[] = {0x27,0x26,0x27};
    const uint32_t kon[] = {0x27,0x25};
    const uint32_t beyn[] = {0x21,0x20,0x25};
    const uint32_t ta[] = {0x24,0x23};
    const uint32_t khob[] = {0x18,0x21};
    const uint32_t peyda[] = {0x32,0x20,0x31,0x23};
    const uint32_t aya[] = {0x23,0x20,0x23};
    const uint32_t agha[] = {0x23,0x13,0x23};
    const uint32_t ab[] = {0x23,0x21};
    const uint32_t mesl[] = {0x26,0x12,0x22};
    const uint32_t really[] = {0x13,0x12,0x1e,0x26,0x26,0x15};
    const uint32_t merci[] = {0x26,0x2f,0x1f,0x20};
    static const char *const common_english_suite[] = {
        "hello", "thanks", "please", "really", "looking",
        "because", "people", "should", "would", "could",
        "about", "today", "tomorrow", "support", "working",
        "problem", "help", "find", "good", "example"
    };
    static const struct {
        const char *keys;
        const wchar_t *expected;
    } common_persian_suite[] = {
        {"sghl", L"سلام"},
        {"lvsd", L"مرسی"},
        {"llk,k", L"ممنون"},
        {"o,hia", L"خواهش"},
        {"v,sjhihd", L"روستاهای"},
        {";l;", L"کمک"},
        {"mdnh", L"پیدا"},
        {"hdh", L"ایا"}
    };
    size_t suite_index;

    CHECK(en_data && fa_data && en_prefix_data && fa_prefix_data &&
          en_common_data && fa_common_data &&
          en_frequent_data && fa_frequent_data &&
          en_common_prefix_data && fa_common_prefix_data,
          "dictionary files load");
    CHECK(ks_bloom_init(&en, en_data, en_size), "English Bloom validates");
    CHECK(ks_bloom_init(&fa, fa_data, fa_size), "Persian Bloom validates");
    CHECK(ks_bloom_init(&en_prefix, en_prefix_data, en_prefix_size),
          "English prefix Bloom validates");
    CHECK(ks_bloom_init(&fa_prefix, fa_prefix_data, fa_prefix_size),
          "Persian prefix Bloom validates");
    CHECK(ks_bloom_init(&en_common, en_common_data, en_common_size),
          "common English Bloom validates");
    CHECK(ks_bloom_init(&fa_common, fa_common_data, fa_common_size),
          "common Persian Bloom validates");
    CHECK(ks_bloom_init(&en_frequent, en_frequent_data, en_frequent_size),
          "frequent English Bloom validates");
    CHECK(ks_bloom_init(&fa_frequent, fa_frequent_data, fa_frequent_size),
          "frequent Persian Bloom validates");
    CHECK(ks_bloom_init(&en_common_prefix,
                        en_common_prefix_data, en_common_prefix_size),
          "common English prefix Bloom validates");
    CHECK(ks_bloom_init(&fa_common_prefix,
                        fa_common_prefix_data, fa_common_prefix_size),
          "common Persian prefix Bloom validates");
    memset(&lexicons, 0, sizeof(lexicons));
    lexicons.english_words = &en;
    lexicons.persian_words = &fa;
    lexicons.english_common = &en_common;
    lexicons.persian_common = &fa_common;
    lexicons.english_frequent = &en_frequent;
    lexicons.persian_frequent = &fa_frequent;
    lexicons.english_prefixes = &en_prefix;
    lexicons.persian_prefixes = &fa_prefix;
    lexicons.english_common_prefixes = &en_common_prefix;
    lexicons.persian_common_prefixes = &fa_common_prefix;
    CHECK(ks_bloom_contains(&en, L"password"), "English dictionary contains password");
    CHECK(ks_bloom_contains(&fa, L"سلام"), "Persian dictionary contains salam");
    CHECK(ks_bloom_contains(&fa_prefix, L"بهسا"), "Persian prefix guard contains behsa");
    CHECK(!ks_bloom_contains(&fa_prefix, L"حشسسصخقی"),
          "Persian prefix guard rejects the password mistype");
    CHECK(!ks_bloom_contains(&en_prefix, L"sghl"),
          "English prefix guard rejects the salam mistype");
    CHECK(ks_bloom_contains(&en_common, L"really"),
          "spoken-English supplement contains really");
    CHECK(ks_bloom_contains(&fa_common, L"مرسی"),
          "curated Persian supplement contains merci");
    CHECK(ks_bloom_contains(&en_common, L"looking"),
          "expanded English frequency supplement contains looking");
    CHECK(ks_bloom_contains(&fa_common, L"روستاهای"),
          "expanded Persian frequency supplement contains common inflections");
    CHECK(ks_bloom_contains(&fa_common_prefix, L"بهسا"),
          "common-prefix supplement protects behsazi during a pause");
    CHECK(!ks_is_word_scancode(0x39) &&
          !ks_map_scancode(0x39, 0, 0, tokens),
          "Space is a boundary and can never become a word token");
    CHECK(ks_is_word_scancode(0x2B),
          "runtime mapping preserves the alternate Windows Persian letter key");

    CHECK(make_tokens(password, 8, tokens), "password scan codes map");
    ks_tokens_to_persian(tokens, 8, mapped);
    CHECK(wcscmp(mapped, L"حشسسصخقی") == 0, "password maps to Persian mistype");
    CHECK(ks_evaluate(tokens, 8, KS_LANG_PERSIAN, 4, &en, &fa, &decision), "Persian-to-English decision");
    CHECK(wcscmp(decision.replacement, L"password") == 0, "replacement is password");
    CHECK(ks_evaluate_live(tokens, 8, KS_LANG_PERSIAN, 4, &en, &fa,
                           &en_prefix, &fa_prefix, &decision) == KS_LIVE_CORRECT_NOW,
          "password mismatch corrects before a delimiter");
    CHECK(!ks_evaluate(tokens, 8, KS_LANG_ENGLISH, 4, &en, &fa, &decision),
          "correct English word remains unchanged");

    CHECK(make_tokens(salam, 4, tokens), "salam scan codes map");
    CHECK(ks_evaluate(tokens, 4, KS_LANG_ENGLISH, 4, &en, &fa, &decision), "English-to-Persian decision");
    CHECK(wcscmp(decision.replacement, L"سلام") == 0, "replacement is salam");
    CHECK(ks_evaluate_live(tokens, 4, KS_LANG_ENGLISH, 4, &en, &fa,
                           &en_prefix, &fa_prefix, &decision) == KS_LIVE_CORRECT_NOW,
          "salam mismatch corrects before a delimiter");
    CHECK(!ks_evaluate(tokens, 4, KS_LANG_PERSIAN, 4, &en, &fa, &decision),
          "correct Persian word remains unchanged");

    CHECK(make_tokens(ketab, 4, tokens), "ketab OEM scan codes map");
    CHECK(ks_evaluate(tokens, 4, KS_LANG_ENGLISH, 4, &en, &fa, &decision), "OEM English-to-Persian decision");
    CHECK(wcscmp(decision.replacement, L"کتاب") == 0, "replacement is ketab");

    CHECK(make_tokens(hello, 5, tokens), "hello scan codes map");
    ks_tokens_to_persian(tokens, 5, mapped);
    CHECK(wcscmp(mapped, L"اثممخ") == 0, "hello maps to Persian mistype");
    CHECK(ks_evaluate(tokens, 5, KS_LANG_PERSIAN, 4, &en, &fa, &decision),
          "Persian-to-English hello decision");
    CHECK(wcscmp(decision.replacement, L"hello") == 0, "replacement is hello");
    CHECK(ks_evaluate_contextual(tokens, 5, KS_LANG_PERSIAN, 1,
                                 KS_LANG_PERSIAN, 5, 0, KS_PHASE_LIVE,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW,
          "Prefer Persian never suppresses the unambiguous hello correction");
    CHECK(wcscmp(decision.replacement, L"hello") == 0,
          "contextual hello replacement is English");
    CHECK(decision.confidence == 100,
          "unambiguous hello has maximum confidence");

    CHECK(make_tokens(behsa, 4, tokens), "behsazi prefix scan codes map");
    ks_tokens_to_persian(tokens, 4, mapped);
    CHECK(wcscmp(mapped, L"بهسا") == 0, "behsazi prefix maps to behsa");
    CHECK(ks_evaluate(tokens, 4, KS_LANG_PERSIAN, 4, &en, &fa, &decision),
          "incomplete Persian prefix demonstrates the fish false positive");
    CHECK(wcscmp(decision.replacement, L"fish") == 0, "incomplete prefix maps to fish");
    CHECK(ks_evaluate_live(tokens, 4, KS_LANG_PERSIAN, 4, &en, &fa,
                           &en_prefix, &fa_prefix, &decision) == KS_LIVE_WAIT_FOR_IDLE,
          "behsazi prefix is protected while typing continues");

    CHECK(make_tokens(behsazi, 6, tokens), "behsazi scan codes map");
    ks_tokens_to_persian(tokens, 6, mapped);
    CHECK(wcscmp(mapped, L"بهسازی") == 0, "completed word maps to behsazi");
    CHECK(!ks_evaluate(tokens, 6, KS_LANG_PERSIAN, 4, &en, &fa, &decision),
          "completed Persian word remains unchanged at the word boundary");
    CHECK(ks_evaluate_live(tokens, 6, KS_LANG_PERSIAN, 4, &en, &fa,
                           &en_prefix, &fa_prefix, &decision) == KS_LIVE_NONE,
          "completed behsazi never triggers a live correction");

    CHECK(make_tokens(komak, 3, tokens), "komak scan codes map");
    CHECK(ks_evaluate_smart(tokens, 3, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "balanced mode corrects komak on the third key");
    CHECK(wcscmp(decision.replacement, L"کمک") == 0,
          "komak replacement is Persian");
    CHECK(decision.confidence >= 80, "komak confidence clears balanced threshold");

    CHECK(make_tokens(beyn, 3, tokens), "beyn scan codes map");
    CHECK(ks_evaluate_smart(tokens, 3, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "balanced mode corrects beyn on the third key");
    CHECK(wcscmp(decision.replacement, L"بین") == 0,
          "beyn replacement is Persian");

    CHECK(make_tokens(peyda, 4, tokens), "peyda scan codes map");
    CHECK(ks_evaluate_smart(tokens, 4, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "balanced mode corrects peyda without a delimiter");
    CHECK(wcscmp(decision.replacement, L"پیدا") == 0,
          "peyda replacement is Persian");
    tokens[0].english = L'\\';
    tokens[0].persian = L'پ';
    CHECK(make_ascii_tokens("dnh", tokens + 1) == 3,
          "legacy Windows Persian peyda suffix maps");
    CHECK(ks_evaluate_contextual(tokens, 4, KS_LANG_ENGLISH, 1,
                                 KS_LANG_ENGLISH, 5, 0,
                                 KS_PHASE_BOUNDARY, &lexicons,
                                 &decision) == KS_LIVE_CORRECT_NOW,
          "default Windows Persian layout recognizes backslash-dnh as peyda");
    CHECK(wcscmp(decision.replacement, L"پیدا") == 0,
          "default Windows Persian layout replacement is peyda");

    CHECK(make_tokens(aya, 3, tokens), "unshifted aya scan codes map");
    ks_tokens_to_persian(tokens, 3, mapped);
    CHECK(wcscmp(mapped, L"ایا") == 0, "unshifted aya keeps the user's spelling");
    CHECK(ks_evaluate_smart(tokens, 3, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "canonical alef lookup recognizes unshifted aya immediately");
    CHECK(wcscmp(decision.replacement, L"ایا") == 0,
          "aya correction preserves unshifted spelling");
    CHECK(ks_evaluate_smart(tokens, 3, KS_LANG_PERSIAN, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_BOUNDARY,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_NONE,
          "correct Persian aya is never converted to English");

    CHECK(make_tokens(agha, 3, tokens), "unshifted agha scan codes map");
    CHECK(ks_evaluate_smart(tokens, 3, KS_LANG_ENGLISH, 1,
                            KS_LANG_PERSIAN, 3, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "initial alef alias generalizes to agha with Persian context");
    CHECK(wcscmp(decision.replacement, L"اقا") == 0,
          "agha correction preserves unshifted spelling");

    CHECK(make_tokens(ab, 2, tokens), "unshifted ab scan codes map");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_IDLE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "expanded short-word list recognizes unshifted ab");
    CHECK(wcscmp(decision.replacement, L"اب") == 0,
          "ab correction preserves unshifted spelling");

    CHECK(make_tokens(ta, 2, tokens), "ta scan codes map");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_WAIT_FOR_IDLE,
          "two-key ta waits for a natural pause");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_IDLE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "two-key ta corrects after the adaptive pause");
    CHECK(wcscmp(decision.replacement, L"تا") == 0,
          "ta replacement is Persian");
    CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_ENGLISH, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW,
          "full contextual path recognizes ta at a word boundary");

    CHECK(make_tokens(kon, 2, tokens), "kon scan codes map");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_IDLE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "two-key kon corrects after the adaptive pause");
    CHECK(wcscmp(decision.replacement, L"کن") == 0,
          "kon replacement is Persian");

    CHECK(make_tokens(khob, 2, tokens), "khob scan codes map");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_BOUNDARY,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_NONE,
          "standalone English of is protected when context is unknown");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_PERSIAN, 3, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_WAIT_FOR_IDLE,
          "contextual khob waits until the two-key word is complete");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_PERSIAN, 3, KS_PHASE_IDLE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_CORRECT_NOW,
          "Persian sentence context resolves of to khob");
    CHECK(wcscmp(decision.replacement, L"خب") == 0,
          "contextual khob replacement is Persian");
    CHECK(ks_evaluate_smart(tokens, 2, KS_LANG_ENGLISH, 1,
                            KS_LANG_ENGLISH, 3, KS_PHASE_BOUNDARY,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_NONE,
          "English context keeps the genuine word of unchanged");
    CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_ENGLISH, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE,
          "sentence-start corpus prior does not guess between of and khob");
    CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_PERSIAN, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE,
          "correct sentence-start khob is protected from an of rewrite");
    CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_ENGLISH, 1,
                                 KS_LANG_PERSIAN, 5, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW,
          "Prefer Persian explicitly resolves sentence-start of to khob");
    CHECK(wcscmp(decision.replacement, L"خب") == 0,
          "explicit sentence-start khob replacement is Persian");

    CHECK(make_tokens(mesl, 3, tokens), "mesl scan codes map");
    CHECK(ks_word_membership(tokens, 3, &en, &fa, &en_common, &fa_common,
                             &english_known, &persian_known),
          "mesl collision membership evaluates");
    CHECK(english_known && persian_known,
          "both leg and mesl are recognized as real words");
    CHECK(ks_evaluate_smart_common(tokens, 3, KS_LANG_ENGLISH, 1,
                                   KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                                   &en, &fa, &en_common, &fa_common,
                                   &en_prefix, &fa_prefix,
                                   &decision) == KS_LIVE_NONE,
          "Auto mode does not destroy a standalone genuine English leg");
    CHECK(ks_evaluate_smart_common(tokens, 3, KS_LANG_ENGLISH, 1,
                                   KS_LANG_PERSIAN, 3, KS_PHASE_LIVE,
                                   &en, &fa, &en_common, &fa_common,
                                   &en_prefix, &fa_prefix,
                                   &decision) == KS_LIVE_WAIT_FOR_IDLE,
          "Auto mode cautiously pauses on the leg/mesl prefix collision");
    CHECK(ks_evaluate_smart_common(tokens, 3, KS_LANG_ENGLISH, 1,
                                   KS_LANG_PERSIAN, 3, KS_PHASE_IDLE,
                                   &en, &fa, &en_common, &fa_common,
                                   &en_prefix, &fa_prefix,
                                   &decision) == KS_LIVE_CORRECT_NOW,
          "Persian sentence context resolves leg/mesl after the adaptive pause");
    CHECK(wcscmp(decision.replacement, L"مثل") == 0,
          "mesl collision replacement is Persian");
    CHECK(ks_evaluate_smart_common(tokens, 3, KS_LANG_ENGLISH, 1,
                                   KS_LANG_PERSIAN, 4, KS_PHASE_LIVE,
                                   &en, &fa, &en_common, &fa_common,
                                   &en_prefix, &fa_prefix,
                                   &decision) == KS_LIVE_CORRECT_NOW,
          "explicit Prefer Persian resolves leg/mesl on the third key");
    CHECK(ks_evaluate_smart_common(tokens, 3, KS_LANG_ENGLISH, 1,
                                   KS_LANG_ENGLISH, 3, KS_PHASE_BOUNDARY,
                                   &en, &fa, &en_common, &fa_common,
                                   &en_prefix, &fa_prefix,
                                   &decision) == KS_LIVE_NONE,
          "English context preserves the genuine word leg");
    CHECK(ks_collision_prior_points(L"leg") >= 30,
          "frequency prior makes Persian mesl likelier than English leg");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_ENGLISH, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_LIVE,
                                 &lexicons, &decision) == KS_LIVE_WAIT_FOR_IDLE,
          "sentence-start frequency resolves mesl but honors the leg prefix");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_ENGLISH, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_IDLE,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW,
          "sentence-start mesl corrects after the adaptive pause");
    CHECK(wcscmp(decision.replacement, L"مثل") == 0,
          "sentence-start prior chooses Persian mesl");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_PERSIAN, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE,
          "same prior preserves correctly typed Persian mesl");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_ENGLISH, 1,
                                 KS_LANG_PERSIAN, 4, 0, KS_PHASE_LIVE,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW,
          "strong Persian sentence context corrects mesl immediately");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_ENGLISH, 1,
                                 KS_LANG_ENGLISH, 5, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE,
          "Prefer English overrides the sentence-start Persian prior");

    CHECK(make_tokens(really, 6, tokens), "really scan codes map");
    CHECK(ks_evaluate_smart_common(tokens, 6, KS_LANG_PERSIAN, 1,
                                   KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                                   &en, &fa, &en_common, &fa_common,
                                   &en_prefix, &fa_prefix,
                                   &decision) == KS_LIVE_CORRECT_NOW,
          "common spoken-English supplement corrects really");
    CHECK(wcscmp(decision.replacement, L"really") == 0,
          "really replacement is English");

    CHECK(make_tokens(merci, 4, tokens), "merci scan codes map");
    CHECK(ks_evaluate_smart_common(tokens, 4, KS_LANG_ENGLISH, 1,
                                   KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                                   &en, &fa, &en_common, &fa_common,
                                   &en_prefix, &fa_prefix,
                                   &decision) == KS_LIVE_CORRECT_NOW,
          "common Persian supplement corrects merci");
    CHECK(wcscmp(decision.replacement, L"مرسی") == 0,
          "merci replacement is Persian");

    CHECK(make_ascii_tokens("how", tokens) == 3, "how physical keys map");
    CHECK(ks_classify_word(tokens, 3, &lexicons,
                           &english_known, &persian_known, NULL, NULL),
          "how collision classifies");
    CHECK(english_known && persian_known,
          "how and its Persian-layout output are both dictionary words");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_PERSIAN, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW,
          "frequent tier resolves Persian-layout how to English");
    CHECK(wcscmp(decision.replacement, L"how") == 0,
          "how replacement is English");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_PERSIAN, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_LIVE,
                                 &lexicons, &decision) != KS_LIVE_NONE,
          "how is recognized before the user presses a delimiter");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_PERSIAN, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_IDLE,
                                 &lexicons, &decision) == KS_LIVE_CORRECT_NOW,
          "how resolves no later than the adaptive pause");
    CHECK(ks_evaluate_contextual(tokens, 3, KS_LANG_ENGLISH, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) == KS_LIVE_NONE,
          "correct English how remains unchanged");

    sequence[0].tokens = sequence_tokens[0];
    sequence[1].tokens = sequence_tokens[1];
    sequence[2].tokens = sequence_tokens[2];
    sequence[3].tokens = sequence_tokens[3];
    sequence[4].tokens = sequence_tokens[4];
    sequence[5].tokens = sequence_tokens[5];
    sequence[0].count = make_ascii_tokens("nv", sequence_tokens[0]);
    sequence[1].count = make_ascii_tokens("clhkd", sequence_tokens[1]);
    sequence[2].count = make_ascii_tokens(";i", sequence_tokens[2]);
    CHECK(sequence[0].count == 2 && sequence[1].count == 5 &&
          sequence[2].count == 2,
          "Persian phrase physical keys map");
    CHECK(ks_evaluate_contextual(sequence[0].tokens, sequence[0].count,
                                 KS_LANG_ENGLISH, 1,
                                 KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) ==
              KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.replacement, L"در") == 0,
          "nv independently resolves to Persian dar");
    CHECK(ks_evaluate_contextual(sequence[1].tokens, sequence[1].count,
                                 KS_LANG_ENGLISH, 1,
                                 KS_LANG_OTHER, 0, 0, KS_PHASE_LIVE,
                                 &lexicons, &decision) ==
              KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.replacement, L"زمانی") == 0,
          "clhkd independently resolves live to Persian zamani");
    CHECK(ks_evaluate_contextual(sequence[2].tokens, sequence[2].count,
                                 KS_LANG_ENGLISH, 1,
                                 KS_LANG_PERSIAN, 3, 0, KS_PHASE_BOUNDARY,
                                 &lexicons, &decision) ==
              KS_LIVE_CORRECT_NOW &&
          wcscmp(decision.replacement, L"که") == 0,
          "semicolon-i resolves to Persian keh with phrase context");
    CHECK(ks_evaluate_sequence(sequence, 3, 1, KS_LANG_OTHER, 0,
                               &lexicons, &sequence_result),
          "sequence model recognizes a coherent Persian phrase");
    CHECK(sequence_result.language == KS_LANG_PERSIAN &&
          sequence_result.persian_known_words == 3,
          "nv clhkd semicolon-i resolves to Persian");

    sequence[0].count = make_ascii_tokens("how", sequence_tokens[0]);
    sequence[1].count = make_ascii_tokens("are", sequence_tokens[1]);
    sequence[2].count = make_ascii_tokens("you", sequence_tokens[2]);
    CHECK(ks_evaluate_sequence(sequence, 3, 1, KS_LANG_OTHER, 0,
                               &lexicons, &sequence_result),
          "sequence model recognizes a coherent English phrase");
    CHECK(sequence_result.language == KS_LANG_ENGLISH &&
          sequence_result.english_known_words == 3,
          "how are you resolves to English");

    sequence[0].count = make_ascii_tokens("this", sequence_tokens[0]);
    sequence[1].count = make_ascii_tokens("is", sequence_tokens[1]);
    sequence[2].count = make_ascii_tokens("a", sequence_tokens[2]);
    sequence[3].count = make_ascii_tokens("very", sequence_tokens[3]);
    sequence[4].count = make_ascii_tokens("good", sequence_tokens[4]);
    sequence[5].count = make_ascii_tokens("test", sequence_tokens[5]);
    CHECK(ks_evaluate_sequence(sequence, 6, 1, KS_LANG_OTHER, 0,
                               &lexicons, &sequence_result),
          "sentence model is not limited to the previous four words");
    CHECK(sequence_result.language == KS_LANG_ENGLISH &&
          sequence_result.english_known_words == 6,
          "six-word sentence resolves coherently from its beginning");

    sequence[0].count = make_ascii_tokens("sghl", sequence_tokens[0]);
    sequence[1].count = make_ascii_tokens("test", sequence_tokens[1]);
    CHECK(!ks_evaluate_sequence(sequence, 2, 1, KS_LANG_OTHER, 0,
                                &lexicons, &sequence_result),
          "mixed Persian-English text is not flattened into one language");

    /*
     * Adversarial common-word matrix. A tray preference is allowed to
     * resolve genuine two-language collisions, but it must never suppress a
     * one-sided wrong-layout correction in either direction.
     */
    for (suite_index = 0;
         suite_index < sizeof(common_english_suite) /
                       sizeof(common_english_suite[0]);
         ++suite_index) {
        int token_count =
            make_ascii_tokens(common_english_suite[suite_index], tokens);
        CHECK(token_count >= 3, "common English suite maps to physical keys");
        CHECK(ks_evaluate_contextual(tokens, token_count, KS_LANG_PERSIAN, 1,
                                     KS_LANG_PERSIAN, 5, 0,
                                     KS_PHASE_BOUNDARY, &lexicons,
                                     &decision) == KS_LIVE_CORRECT_NOW,
              "Prefer Persian cannot suppress a common English correction");
        mbstowcs(mapped, common_english_suite[suite_index], KS_MAX_WORD);
        mapped[KS_MAX_WORD] = 0;
        CHECK(wcscmp(decision.replacement, mapped) == 0,
              "common English suite produces the intended spelling");
        CHECK(ks_evaluate_contextual(tokens, token_count, KS_LANG_ENGLISH, 1,
                                     KS_LANG_PERSIAN, 5, 0,
                                     KS_PHASE_BOUNDARY, &lexicons,
                                     &decision) == KS_LIVE_NONE,
              "correct common English remains unchanged in Persian context");
    }
    for (suite_index = 0;
         suite_index < sizeof(common_persian_suite) /
                       sizeof(common_persian_suite[0]);
         ++suite_index) {
        int token_count =
            make_ascii_tokens(common_persian_suite[suite_index].keys, tokens);
        CHECK(token_count >= 3, "common Persian suite maps to physical keys");
        CHECK(ks_evaluate_contextual(tokens, token_count, KS_LANG_ENGLISH, 1,
                                     KS_LANG_ENGLISH, 5, 0,
                                     KS_PHASE_BOUNDARY, &lexicons,
                                     &decision) == KS_LIVE_CORRECT_NOW,
              "Prefer English cannot suppress a common Persian correction");
        CHECK(wcscmp(decision.replacement,
                     common_persian_suite[suite_index].expected) == 0,
              "common Persian suite produces the intended spelling");
        CHECK(ks_evaluate_contextual(tokens, token_count, KS_LANG_PERSIAN, 1,
                                     KS_LANG_ENGLISH, 5, 0,
                                     KS_PHASE_BOUNDARY, &lexicons,
                                     &decision) == KS_LIVE_NONE,
              "correct common Persian remains unchanged in English context");
    }

    CHECK(make_tokens(behsa, 4, tokens), "behsazi smart-prefix scan codes map");
    CHECK(ks_evaluate_smart(tokens, 4, KS_LANG_PERSIAN, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_LIVE,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_WAIT_FOR_IDLE,
          "smart scoring still protects a valid active-language prefix");
    CHECK(ks_evaluate_contextual(tokens, 4, KS_LANG_PERSIAN, 1,
                                 KS_LANG_OTHER, 0, 0, KS_PHASE_IDLE,
                                 &lexicons, &decision) == KS_LIVE_WAIT_FOR_IDLE,
          "common Persian prefix is not changed to fish during a thinking pause");
    CHECK(make_tokens(behsazi, 6, tokens), "completed behsazi smart scan codes map");
    CHECK(ks_evaluate_smart(tokens, 6, KS_LANG_PERSIAN, 1,
                            KS_LANG_OTHER, 0, KS_PHASE_BOUNDARY,
                            &en, &fa, &en_prefix, &fa_prefix,
                            &decision) == KS_LIVE_NONE,
          "smart scoring keeps completed behsazi in Persian");

    /*
     * Genuine English two-key tokens whose physical keys also spell a listed
     * Persian word must be collisions, never one-sided Persian evidence.
     * Before 2.8 "user id " became "user هی ".
     */
    {
        static const struct {
            const char *keys;
            const wchar_t *persian;
        } english_token_suite[] = {
            {"id", L"هی"}, {"ms", L"پس"}, {"pr", L"حق"}
        };
        for (suite_index = 0;
             suite_index < sizeof(english_token_suite) /
                           sizeof(english_token_suite[0]);
             ++suite_index) {
            CHECK(make_ascii_tokens(english_token_suite[suite_index].keys,
                                    tokens) == 2,
                  "English two-key token maps");
            CHECK(ks_classify_word(tokens, 2, &lexicons,
                                   &english_known, &persian_known,
                                   NULL, NULL) &&
                      english_known && persian_known,
                  "English two-key token is a collision on both sides");
            CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_ENGLISH, 1,
                                         KS_LANG_OTHER, 0, 0,
                                         KS_PHASE_BOUNDARY, &lexicons,
                                         &decision) == KS_LIVE_NONE,
                  "English two-key token survives Space without context");
            CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_ENGLISH, 1,
                                         KS_LANG_ENGLISH, 3, 0,
                                         KS_PHASE_IDLE, &lexicons,
                                         &decision) == KS_LIVE_NONE,
                  "English two-key token survives a pause in English text");
            CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_ENGLISH, 1,
                                         KS_LANG_PERSIAN, 3, 0,
                                         KS_PHASE_IDLE, &lexicons,
                                         &decision) == KS_LIVE_CORRECT_NOW &&
                      wcscmp(decision.replacement,
                             english_token_suite[suite_index].persian) == 0,
                  "Persian context still resolves the collision to Persian");
        }
    }

    /*
     * ZWNJ halves: Shift+Space splits می‌خواهم into می and خواهم. The first
     * half has to be a known Persian word on its own or "ld خواهم" is
     * never repaired. None of these key sequences spells English.
     */
    {
        static const struct {
            const char *keys;
            const wchar_t *expected;
        } zwnj_half_suite[] = {
            {"ld", L"می"}, {"ih", L"ها"}, {"jv", L"تر"},
            {"hl", L"ام"}, {"hj", L"ات"}
        };
        for (suite_index = 0;
             suite_index < sizeof(zwnj_half_suite) /
                           sizeof(zwnj_half_suite[0]);
             ++suite_index) {
            CHECK(make_ascii_tokens(zwnj_half_suite[suite_index].keys,
                                    tokens) == 2,
                  "ZWNJ half maps to physical keys");
            CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_ENGLISH, 1,
                                         KS_LANG_OTHER, 0, 1,
                                         KS_PHASE_BOUNDARY, &lexicons,
                                         &decision) == KS_LIVE_CORRECT_NOW &&
                      wcscmp(decision.replacement,
                             zwnj_half_suite[suite_index].expected) == 0,
                  "ZWNJ half is corrected to Persian at Shift+Space");
            CHECK(ks_evaluate_contextual(tokens, 2, KS_LANG_PERSIAN, 1,
                                         KS_LANG_OTHER, 0, 1,
                                         KS_PHASE_BOUNDARY, &lexicons,
                                         &decision) == KS_LIVE_NONE,
                  "correctly typed ZWNJ half is left alone");
        }
        sequence[0].count = make_ascii_tokens("ld", sequence_tokens[0]);
        sequence[1].count = make_ascii_tokens("o,hil", sequence_tokens[1]);
        CHECK(ks_evaluate_sequence(sequence, 2, 1, KS_LANG_OTHER, 0,
                                   &lexicons, &sequence_result) &&
                  sequence_result.language == KS_LANG_PERSIAN,
              "mi-khaham resolves as a coherent Persian phrase");
    }

    CHECK(ks_canonical_persian(0x064A) == 0x06CC &&
          ks_canonical_persian(0x0649) == 0x06CC &&
          ks_canonical_persian(0x0643) == 0x06A9 &&
          ks_canonical_persian(L'س') == L'س' &&
          ks_canonical_persian(L'k') == L'k',
          "Arabic yeh and kaf normalize to their Persian code points");
    CHECK(ks_is_persian_diacritic(0x0651) && ks_is_persian_diacritic(0x064E) &&
          ks_is_persian_diacritic(0x064B) && ks_is_persian_diacritic(0x0670) &&
          !ks_is_persian_diacritic(0x06DD) && !ks_is_persian_diacritic(L'ی') &&
          !ks_is_persian_diacritic(L'a'),
          "diacritic detection covers Arabic-script combining marks only");
    {
        /* حتماً and مدرّس: correct Persian typed with a tanwin/shadda must stay
           Persian-known, or the English candidate would win by default. */
        CHECK(make_ascii_tokens("pjlh", tokens) == 4, "hatman keys map");
        tokens[4].english = L'R';      /* Shift+R is fathatan on the Persian layout */
        tokens[4].persian = 0x064B;
        CHECK(ks_classify_word(tokens, 5, &lexicons,
                               &english_known, &persian_known, NULL, NULL) &&
                  persian_known && !english_known,
              "hatman with tanwin is recognized as Persian");
        CHECK(ks_evaluate_contextual(tokens, 5, KS_LANG_PERSIAN, 1,
                                     KS_LANG_OTHER, 0, 0, KS_PHASE_BOUNDARY,
                                     &lexicons, &decision) == KS_LIVE_NONE,
              "hatman with tanwin is never rewritten");
        CHECK(make_ascii_tokens("lnvs", tokens) == 4, "modarres keys map");
        tokens[4] = tokens[3];
        tokens[3].english = L'I';      /* Shift+I is shadda on the Persian layout */
        tokens[3].persian = 0x0651;
        CHECK(ks_classify_word(tokens, 5, &lexicons,
                               &english_known, &persian_known, NULL, NULL) &&
                  persian_known,
              "modarres with shadda is recognized as Persian");
        /* The reverse direction still works: a capitalized English word
           mistyped on the Persian layout produces a diacritic token. */
        CHECK(make_ascii_tokens("xcel", tokens + 1) == 4, "Excel keys map");
        tokens[0].english = L'E';
        tokens[0].persian = 0x064D;    /* Shift+E is kasratan */
        CHECK(ks_evaluate_contextual(tokens, 5, KS_LANG_PERSIAN, 1,
                                     KS_LANG_OTHER, 0, 1, KS_PHASE_BOUNDARY,
                                     &lexicons, &decision) == KS_LIVE_CORRECT_NOW &&
                  wcscmp(decision.replacement, L"Excel") == 0,
              "capitalized Excel mistyped on the Persian layout is corrected");
    }
    {
        wchar_t legacy[KS_MAX_WORD + 1];
        int index;
        CHECK(make_ascii_tokens("mdnh", tokens) == 4, "legacy peyda keys map");
        /* Simulate a layout that emits Arabic yeh, then canonicalize. */
        tokens[1].persian = 0x064A;
        ks_tokens_to_persian(tokens, 4, legacy);
        CHECK(!ks_bloom_contains(&fa, legacy),
              "Arabic-yeh spelling is unknown to the normalized dictionary");
        for (index = 0; index < 4; ++index)
            tokens[index].persian = ks_canonical_persian(tokens[index].persian);
        CHECK(ks_evaluate_contextual(tokens, 4, KS_LANG_PERSIAN, 1,
                                     KS_LANG_OTHER, 0, 0, KS_PHASE_BOUNDARY,
                                     &lexicons, &decision) == KS_LIVE_NONE,
              "canonicalized legacy Persian peyda is not rewritten");
    }

    CHECK(ks_idle_delay_ms(1, 100) == 450, "balanced fast-typing delay is clamped");
    CHECK(ks_idle_delay_ms(1, 250) == 750, "balanced delay adapts to typing speed");
    CHECK(ks_idle_delay_ms(1, 400) == 900, "balanced slow-typing delay is capped");
    CHECK(ks_idle_delay_ms(2, 100) == 280, "sensitive mode responds faster");
    CHECK(ks_idle_delay_ms(0, 100) == 650, "conservative mode waits longer");
    CHECK(ks_update_key_interval_ms(150, 100) == 137,
          "typing cadence uses a stable exponential average");
    CHECK(ks_update_key_interval_ms(150, 10) == 150,
          "key bounce does not distort typing cadence");
    CHECK(ks_update_key_interval_ms(150, 2000) == 150,
          "long pauses do not distort typing cadence");
    CHECK(ks_update_key_interval_ms(0, 200) == 162,
          "invalid cadence state returns to a safe baseline");

    ks_context_reset(&context);
    CHECK(ks_context_current(&context, NULL) == KS_LANG_OTHER,
          "new sentence context starts neutral");
    ks_context_observe(&context, KS_LANG_PERSIAN, 3);
    CHECK(ks_context_current(&context, &english_known) == KS_LANG_PERSIAN &&
          english_known == 3,
          "first strong Persian word establishes Persian context");
    ks_context_observe(&context, KS_LANG_PERSIAN, 3);
    CHECK(ks_context_current(&context, &english_known) == KS_LANG_PERSIAN &&
          english_known == 4,
          "repeated Persian evidence strengthens sentence context");
    ks_context_observe(&context, KS_LANG_ENGLISH, 2);
    CHECK(ks_context_current(&context, &english_known) == KS_LANG_PERSIAN &&
          english_known == 2,
          "one English term does not flip an established Persian sentence");
    ks_context_observe(&context, KS_LANG_ENGLISH, 3);
    CHECK(ks_context_current(&context, &english_known) == KS_LANG_ENGLISH &&
          english_known == 1,
          "strong contrary evidence can change sentence language gradually");

    free(en_data);
    free(fa_data);
    free(en_prefix_data);
    free(fa_prefix_data);
    free(en_common_data);
    free(fa_common_data);
    free(en_frequent_data);
    free(fa_frequent_data);
    free(en_common_prefix_data);
    free(fa_common_prefix_data);
    puts("All native core tests passed.");
    return 0;
}
