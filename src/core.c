#include "core.h"

#include <string.h>

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int ks_bloom_init(KS_BLOOM *bloom, const unsigned char *data, size_t size) {
    uint32_t version;
    uint32_t bit_count;
    uint32_t hash_count;
    size_t required;

    if (!bloom) return 0;
    memset(bloom, 0, sizeof(*bloom));
    if (!data || size < 16 || memcmp(data, "KSWB", 4) != 0) return 0;

    version = read_u32_le(data + 4);
    bit_count = read_u32_le(data + 8);
    hash_count = read_u32_le(data + 12);
    if (version != 1 || bit_count == 0 || (bit_count & (bit_count - 1)) != 0 ||
        hash_count == 0 || hash_count > 16) return 0;

    required = 16u + (size_t)bit_count / 8u;
    if (size != required) return 0;

    bloom->bits = data + 16;
    bloom->bit_count = bit_count;
    bloom->bit_mask = bit_count - 1;
    bloom->hash_count = hash_count;
    bloom->valid = 1;
    return 1;
}

static void hash_byte(uint32_t *first, uint32_t *second, unsigned char byte) {
    *first ^= byte;
    *first *= 16777619u;
    *second = ((*second << 5) + *second) ^ byte;
}

static void hash_codepoint(uint32_t *first, uint32_t *second, uint32_t cp) {
    if (cp < 0x80u) {
        hash_byte(first, second, (unsigned char)cp);
    } else if (cp < 0x800u) {
        hash_byte(first, second, (unsigned char)(0xC0u | (cp >> 6)));
        hash_byte(first, second, (unsigned char)(0x80u | (cp & 0x3Fu)));
    } else {
        hash_byte(first, second, (unsigned char)(0xE0u | (cp >> 12)));
        hash_byte(first, second, (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu)));
        hash_byte(first, second, (unsigned char)(0x80u | (cp & 0x3Fu)));
    }
}

int ks_bloom_contains(const KS_BLOOM *bloom, const wchar_t *value) {
    uint32_t first = 2166136261u;
    uint32_t second = 5381u;
    uint32_t i;

    if (!bloom || !bloom->valid || !value || !*value) return 0;
    while (*value) {
        hash_codepoint(&first, &second, (uint32_t)*value);
        ++value;
    }

    second = (second << 1) | 1u;
    for (i = 0; i < bloom->hash_count; ++i) {
        uint32_t bit = (first + i * second + i * i * 0x9E3779B9u) & bloom->bit_mask;
        if ((bloom->bits[bit >> 3] & (1u << (bit & 7))) == 0) return 0;
    }
    return 1;
}

int ks_is_word_scancode(uint32_t scan) {
    if ((scan >= 0x10 && scan <= 0x1B) ||
        (scan >= 0x1E && scan <= 0x28) ||
        (scan >= 0x2C && scan <= 0x33))
        return 1;
    /*
     * OEM backslash is used by one of the Windows Persian layout variants
     * for a Persian letter. It has no portable fallback mapping, but runtime
     * ToUnicodeEx translation can map it exactly.
     */
    return scan == 0x2B;
}

int ks_map_scancode(uint32_t scan, int shift, int caps, KS_TOKEN *token) {
    wchar_t en = 0;
    wchar_t fa = 0;
    wchar_t base = 0;

    if (!token) return 0;
    switch (scan) {
        case 0x1E: base = L'a'; fa = L'ش'; break;
        case 0x30: base = L'b'; fa = L'ذ'; break;
        case 0x2E: base = L'c'; fa = shift ? L'ژ' : L'ز'; break;
        case 0x20: base = L'd'; fa = L'ی'; break;
        case 0x12: base = L'e'; fa = L'ث'; break;
        case 0x21: base = L'f'; fa = L'ب'; break;
        case 0x22: base = L'g'; fa = L'ل'; break;
        case 0x23: base = L'h'; fa = shift ? L'آ' : L'ا'; break;
        case 0x17: base = L'i'; fa = L'ه'; break;
        case 0x24: base = L'j'; fa = L'ت'; break;
        case 0x25: base = L'k'; fa = L'ن'; break;
        case 0x26: base = L'l'; fa = L'م'; break;
        case 0x32: base = L'm'; fa = L'پ'; break;
        case 0x31: base = L'n'; fa = L'د'; break;
        case 0x18: base = L'o'; fa = L'خ'; break;
        case 0x19: base = L'p'; fa = L'ح'; break;
        case 0x10: base = L'q'; fa = L'ض'; break;
        case 0x13: base = L'r'; fa = L'ق'; break;
        case 0x1F: base = L's'; fa = L'س'; break;
        case 0x14: base = L't'; fa = L'ف'; break;
        case 0x16: base = L'u'; fa = L'ع'; break;
        case 0x2F: base = L'v'; fa = L'ر'; break;
        case 0x11: base = L'w'; fa = L'ص'; break;
        case 0x2D: base = L'x'; fa = L'ط'; break;
        case 0x15: base = L'y'; fa = L'غ'; break;
        case 0x2C: base = L'z'; fa = L'ظ'; break;
        case 0x1A: en = shift ? L'{' : L'['; fa = L'ج'; break;
        case 0x1B: en = shift ? L'}' : L']'; fa = L'چ'; break;
        case 0x27: en = shift ? L':' : L';'; fa = L'ک'; break;
        case 0x28: en = shift ? L'"' : L'\''; fa = L'گ'; break;
        case 0x33: en = shift ? L'<' : L','; fa = L'و'; break;
        default: return 0;
    }

    if (base) en = (shift ^ caps) ? (base - L'a' + L'A') : base;
    token->english = en;
    token->persian = fa;
    return 1;
}

void ks_tokens_to_english(const KS_TOKEN *tokens, int count, wchar_t *output) {
    int i;
    if (!output) return;
    for (i = 0; i < count; ++i) output[i] = tokens[i].english;
    output[count] = 0;
}

void ks_tokens_to_persian(const KS_TOKEN *tokens, int count, wchar_t *output) {
    int i;
    if (!output) return;
    for (i = 0; i < count; ++i) output[i] = tokens[i].persian;
    output[count] = 0;
}

static void english_lower(const wchar_t *source, wchar_t *target) {
    while (*source) {
        wchar_t c = *source++;
        if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
        *target++ = c;
    }
    *target = 0;
}

static int english_word_shape(const wchar_t *value) {
    int index = 0;
    int apostrophe = 0;
    int length = (int)wcslen(value);
    if (length < 1) return 0;
    while (value[index]) {
        wchar_t c = value[index];
        if (c >= L'a' && c <= L'z') {
            ++index;
            continue;
        }
        if (c == L'\'' && !apostrophe && index > 0 && index < length - 1) {
            apostrophe = 1;
            ++index;
            continue;
        }
        return 0;
    }
    return 1;
}

static int exact_word(const wchar_t *value, const wchar_t *const *words, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (wcscmp(value, words[index]) == 0) return 1;
    }
    return 0;
}

static int short_english_word(const wchar_t *value) {
    static const wchar_t *const words[] = {
        L"a", L"i",
        L"ad", L"ah", L"ai", L"am", L"an", L"as", L"at", L"be",
        L"by", L"do", L"go", L"he", L"hi", L"if", L"in", L"is",
        L"it", L"me", L"my", L"no", L"of", L"oh", L"ok", L"on",
        L"or", L"so", L"to", L"up", L"us", L"we"
    };
    return exact_word(value, words, sizeof(words) / sizeof(words[0]));
}

static int short_persian_word(const wchar_t *value) {
    static const wchar_t *const words[] = {
        L"و",
        L"آب", L"آن", L"آه", L"از", L"او", L"ای", L"با", L"بد",
        L"بر", L"به", L"بی", L"پا", L"پر", L"پس", L"تا", L"تب",
        L"ته", L"تو", L"جا", L"جز", L"چه", L"خب", L"خط", L"در",
        L"دل", L"دم", L"ده", L"دو", L"را", L"رو", L"زن", L"سر",
        L"سن", L"سه", L"شب", L"شد", L"حق", L"حل", L"کم", L"کن",
        L"که", L"کل", L"کی", L"گل", L"لب", L"ما", L"من", L"نه",
        L"نو", L"هم", L"هر", L"هی", L"یا", L"یک", L"وی"
    };
    return exact_word(value, words, sizeof(words) / sizeof(words[0]));
}

static int word_bloom_contains(const KS_BLOOM *primary,
                               const KS_BLOOM *supplemental,
                               const wchar_t *value) {
    return ks_bloom_contains(primary, value) ||
           ks_bloom_contains(supplemental, value);
}

static int persian_word_known(const wchar_t *value, int count,
                              const KS_BLOOM *persian,
                              const KS_BLOOM *persian_common) {
    wchar_t canonical[KS_MAX_WORD + 1];
    int known;

    known = count <= 2
        ? short_persian_word(value)
        : word_bloom_contains(persian, persian_common, value);
    if (known || value[0] != L'ا') return known;

    /*
     * Persian users commonly omit Shift for an initial alef-madda: ایا,
     * اقا, ارام, ... . Use the canonical form only for dictionary lookup;
     * the replacement keeps the exact spelling the user physically typed.
     */
    wcscpy(canonical, value);
    canonical[0] = L'آ';
    return count <= 2
        ? short_persian_word(canonical)
        : word_bloom_contains(persian, persian_common, canonical);
}

int ks_word_membership(const KS_TOKEN *tokens, int count,
                       const KS_BLOOM *english, const KS_BLOOM *persian,
                       const KS_BLOOM *english_common,
                       const KS_BLOOM *persian_common,
                       int *english_known, int *persian_known) {
    wchar_t en[KS_MAX_WORD + 1];
    wchar_t en_lower[KS_MAX_WORD + 1];
    wchar_t fa[KS_MAX_WORD + 1];
    int en_result;
    int fa_result;

    if (!tokens || count < 1 || count > KS_MAX_WORD) return 0;
    ks_tokens_to_english(tokens, count, en);
    ks_tokens_to_persian(tokens, count, fa);
    english_lower(en, en_lower);

    if (count <= 2) {
        en_result = english_word_shape(en_lower) && short_english_word(en_lower);
        fa_result = persian_word_known(fa, count, persian, persian_common);
    } else {
        en_result = english_word_shape(en_lower) &&
                    word_bloom_contains(english, english_common, en_lower);
        fa_result = persian_word_known(fa, count, persian, persian_common);
    }
    if (english_known) *english_known = en_result;
    if (persian_known) *persian_known = fa_result;
    return 1;
}

int ks_classify_word(const KS_TOKEN *tokens, int count,
                     const KS_LEXICONS *lexicons,
                     int *english_known, int *persian_known,
                     int *english_frequent, int *persian_frequent) {
    wchar_t en[KS_MAX_WORD + 1];
    wchar_t en_lower[KS_MAX_WORD + 1];
    wchar_t fa[KS_MAX_WORD + 1];
    int en_known = 0;
    int fa_known = 0;

    if (!lexicons ||
        !ks_word_membership(tokens, count,
                            lexicons->english_words, lexicons->persian_words,
                            lexicons->english_common, lexicons->persian_common,
                            &en_known, &fa_known)) return 0;
    ks_tokens_to_english(tokens, count, en);
    ks_tokens_to_persian(tokens, count, fa);
    english_lower(en, en_lower);
    if (english_known) *english_known = en_known;
    if (persian_known) *persian_known = fa_known;
    if (english_frequent) {
        *english_frequent =
            en_known && ks_bloom_contains(lexicons->english_frequent, en_lower);
    }
    if (persian_frequent) {
        *persian_frequent =
            fa_known && ks_bloom_contains(lexicons->persian_frequent, fa);
    }
    return 1;
}

void ks_context_reset(KS_LANGUAGE_CONTEXT *context) {
    if (!context) return;
    memset(context, 0, sizeof(*context));
    context->language = KS_LANG_OTHER;
}

void ks_context_observe(KS_LANGUAGE_CONTEXT *context,
                        KS_LANGUAGE language, int evidence) {
    if (!context ||
        (language != KS_LANG_ENGLISH && language != KS_LANG_PERSIAN)) return;
    if (evidence < 1) evidence = 1;
    if (evidence > 4) evidence = 4;
    ++context->observed_words;
    if (context->language == KS_LANG_OTHER || context->strength <= 0) {
        context->language = language;
        context->strength = evidence;
        return;
    }
    if (context->language == language) {
        if (context->strength < evidence) context->strength = evidence;
        else if (context->strength < 4) ++context->strength;
        return;
    }
    if (context->strength > evidence) {
        context->strength -= evidence;
        return;
    }
    if (context->strength == evidence) {
        context->language = KS_LANG_OTHER;
        context->strength = 0;
        return;
    }
    context->language = language;
    context->strength = evidence - context->strength;
}

KS_LANGUAGE ks_context_current(const KS_LANGUAGE_CONTEXT *context,
                               int *strength) {
    if (strength) *strength = 0;
    if (!context || context->strength <= 0 ||
        (context->language != KS_LANG_ENGLISH &&
         context->language != KS_LANG_PERSIAN)) return KS_LANG_OTHER;
    if (strength) *strength = context->strength;
    return context->language;
}

static int sensitivity_minimum_length(int sensitivity) {
    if (sensitivity <= 0) return 4;
    return 2;
}

static int ambiguity_threshold(int sensitivity) {
    if (sensitivity <= 0) return 45;
    if (sensitivity >= 2) return 25;
    return 35;
}

typedef struct KS_COLLISION_PRIOR {
    const wchar_t *english;
    int points;
} KS_COLLISION_PRIOR;

#include "collision_priors.inc"

int ks_collision_prior_points(const wchar_t *english_word) {
    size_t left = 0;
    size_t right = KS_COLLISION_PRIOR_COUNT;
    if (!english_word || !*english_word) return 0;
    while (left < right) {
        size_t middle = left + (right - left) / 2;
        int comparison = wcscmp(english_word,
                                KS_COLLISION_PRIORS[middle].english);
        if (comparison == 0) return KS_COLLISION_PRIORS[middle].points;
        if (comparison < 0) right = middle;
        else left = middle + 1;
    }
    return 0;
}

int ks_evaluate_sequence(const KS_SEQUENCE_WORD *words, int word_count,
                         int sensitivity,
                         KS_LANGUAGE context_language, int context_strength,
                         const KS_LEXICONS *lexicons,
                         KS_SEQUENCE_RESULT *result) {
    int index;
    int english_score = 0;
    int persian_score = 0;
    int english_known_words = 0;
    int persian_known_words = 0;
    int threshold;
    int margin;

    if (!result) return 0;
    memset(result, 0, sizeof(*result));
    result->language = KS_LANG_OTHER;
    if (!words || !lexicons || word_count < 2 ||
        word_count > KS_MAX_SEQUENCE_WORDS) return 0;

    for (index = 0; index < word_count; ++index) {
        int english_known = 0;
        int persian_known = 0;
        int english_frequent = 0;
        int persian_frequent = 0;
        int unknown_penalty;
        if (!words[index].tokens || words[index].count < 1 ||
            words[index].count > KS_MAX_WORD ||
            !ks_classify_word(words[index].tokens, words[index].count,
                              lexicons,
                              &english_known, &persian_known,
                              &english_frequent, &persian_frequent))
            return 0;

        unknown_penalty = words[index].count <= 2 ? 16 : 26;
        if (english_known) {
            english_score += 30 + (english_frequent ? 18 : 0);
            ++english_known_words;
        } else {
            english_score -= unknown_penalty;
        }
        if (persian_known) {
            persian_score += 30 + (persian_frequent ? 18 : 0);
            ++persian_known_words;
        } else {
            persian_score -= unknown_penalty;
        }
    }

    /*
     * A complete same-language run is stronger than the sum of isolated
     * words. This is the local sentence evidence that turns
     * "nv clhkd ;i" into "در زمانی که" without sending text anywhere.
     */
    if (english_known_words == word_count)
        english_score += 12 + word_count * 8;
    if (persian_known_words == word_count)
        persian_score += 12 + word_count * 8;

    /* Strength 5 is an explicit collision preference, not learned context. */
    if (context_strength > 0 && context_strength <= 4) {
        if (context_language == KS_LANG_ENGLISH)
            english_score += context_strength * 8;
        else if (context_language == KS_LANG_PERSIAN)
            persian_score += context_strength * 8;
    }

    result->english_score = english_score;
    result->persian_score = persian_score;
    result->english_known_words = english_known_words;
    result->persian_known_words = persian_known_words;

    threshold = sensitivity <= 0 ? 55 : sensitivity >= 2 ? 30 : 40;
    margin = english_score - persian_score;
    if (margin >= threshold && english_known_words == word_count &&
        english_known_words >= 2) {
        result->language = KS_LANG_ENGLISH;
        result->confidence = margin > 100 ? 100 : margin;
        return 1;
    }
    if (-margin >= threshold && persian_known_words == word_count &&
        persian_known_words >= 2) {
        result->language = KS_LANG_PERSIAN;
        result->confidence = -margin > 100 ? 100 : -margin;
        return 1;
    }
    return 0;
}

static void fill_decision(const KS_TOKEN *tokens, int count,
                          KS_LANGUAGE active_language, int confidence,
                          KS_DECISION *decision) {
    wchar_t en[KS_MAX_WORD + 1];
    wchar_t fa[KS_MAX_WORD + 1];
    ks_tokens_to_english(tokens, count, en);
    ks_tokens_to_persian(tokens, count, fa);
    memset(decision, 0, sizeof(*decision));
    decision->should_correct = 1;
    decision->key_count = count;
    decision->confidence = confidence;
    decision->source_language = active_language;
    decision->target_language =
        active_language == KS_LANG_ENGLISH ? KS_LANG_PERSIAN : KS_LANG_ENGLISH;
    if (active_language == KS_LANG_ENGLISH) {
        wcscpy(decision->original, en);
        wcscpy(decision->replacement, fa);
    } else {
        wcscpy(decision->original, fa);
        wcscpy(decision->replacement, en);
    }
}

KS_LIVE_RESULT ks_evaluate_contextual(
                                 const KS_TOKEN *tokens, int count,
                                 KS_LANGUAGE active_language, int sensitivity,
                                 KS_LANGUAGE context_language, int context_strength,
                                 int sentence_start,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_LEXICONS *lexicons,
                                 KS_DECISION *decision) {
    wchar_t en[KS_MAX_WORD + 1];
    wchar_t en_lower[KS_MAX_WORD + 1];
    wchar_t fa[KS_MAX_WORD + 1];
    const wchar_t *active_word;
    const KS_BLOOM *active_prefixes;
    const KS_BLOOM *active_common_prefixes;
    KS_LANGUAGE target_language;
    int english_known = 0;
    int persian_known = 0;
    int english_frequent = 0;
    int persian_frequent = 0;
    int active_known;
    int target_known;
    int confidence;
    int active_is_prefix;
    int active_is_common_prefix;
    int prior;
    int evidence;
    int explicit_preference;
    int active_frequent;
    int target_frequent;

    if (!decision) return KS_LIVE_NONE;
    memset(decision, 0, sizeof(*decision));
    if (!tokens || !lexicons ||
        count < sensitivity_minimum_length(sensitivity) ||
        count > KS_MAX_WORD ||
        (active_language != KS_LANG_ENGLISH &&
         active_language != KS_LANG_PERSIAN)) return KS_LIVE_NONE;
    if (!ks_classify_word(tokens, count, lexicons,
                          &english_known, &persian_known,
                          &english_frequent, &persian_frequent))
        return KS_LIVE_NONE;

    target_language =
        active_language == KS_LANG_ENGLISH ? KS_LANG_PERSIAN : KS_LANG_ENGLISH;
    active_known =
        active_language == KS_LANG_ENGLISH ? english_known : persian_known;
    target_known =
        target_language == KS_LANG_ENGLISH ? english_known : persian_known;
    active_frequent =
        active_language == KS_LANG_ENGLISH
            ? english_frequent : persian_frequent;
    target_frequent =
        target_language == KS_LANG_ENGLISH
            ? english_frequent : persian_frequent;
    if (!target_known) return KS_LIVE_NONE;

    /*
     * A one-sided dictionary match is objective layout evidence. Language
     * preference and sentence context must never suppress it; this is the
     * critical distinction that keeps اثممخ -> hello working in Prefer
     * Persian mode.
     */
    if (!active_known) {
        confidence = 90;
        if (count >= 5) confidence += 10;
        else if (count >= 3) confidence += 5;
        if (confidence > 100) confidence = 100;
        fill_decision(tokens, count, active_language, confidence, decision);
    } else {
        /*
         * Both layouts produce real words. Only this branch uses a user
         * preference, sentence evidence, and corpus-normalized prior.
         * context_strength 5 is reserved for the explicit tray preference.
         */
        explicit_preference = context_strength >= 5;
        if (explicit_preference) {
            if (context_language != target_language) return KS_LIVE_NONE;
            evidence = 100;
        } else {
            if (context_strength < 0) context_strength = 0;
            if (context_strength > 4) context_strength = 4;
            evidence = 0;
            if (context_language == target_language)
                evidence += context_strength * 30;
            else if (context_language == active_language)
                evidence -= context_strength * 30;
            if (target_frequent && !active_frequent)
                evidence += 45;
            else if (active_frequent && !target_frequent)
                evidence -= 45;

            ks_tokens_to_english(tokens, count, en);
            english_lower(en, en_lower);
            prior = ks_collision_prior_points(en_lower);
            if (target_language == KS_LANG_ENGLISH) prior = -prior;
            /*
             * Two-key collisions carry too little information for a
             * corpus-only sentence-start rewrite (of/خب is the canonical
             * example). They require sentence/document context or an
             * explicit preference. Longer words may use the full prior.
             */
            if (sentence_start && count >= 3) evidence += prior;
            else evidence += prior / 4;
            if (phase == KS_PHASE_IDLE) evidence += 5;
            else if (phase == KS_PHASE_BOUNDARY) evidence += 10;
            if (evidence < ambiguity_threshold(sensitivity)) {
                /*
                 * If the adaptive-idle bonus is the only missing evidence,
                 * arm the timer instead of abandoning the candidate. This is
                 * what lets a sentence-initial leg -> مثل collision resolve
                 * before Space without making a premature third-key edit.
                 */
                if (phase == KS_PHASE_LIVE &&
                    evidence + 5 >= ambiguity_threshold(sensitivity))
                    return KS_LIVE_WAIT_FOR_IDLE;
                return KS_LIVE_NONE;
            }
        }
        confidence = evidence;
        if (confidence < 0) confidence = 0;
        if (confidence > 100) confidence = 100;
        fill_decision(tokens, count, active_language, confidence, decision);
    }

    if (phase == KS_PHASE_BOUNDARY) return KS_LIVE_CORRECT_NOW;
    if (count == 2) {
        if (phase == KS_PHASE_LIVE) return KS_LIVE_WAIT_FOR_IDLE;
        return KS_LIVE_CORRECT_NOW;
    }

    ks_tokens_to_english(tokens, count, en);
    ks_tokens_to_persian(tokens, count, fa);
    if (active_language == KS_LANG_ENGLISH) {
        english_lower(en, en_lower);
        active_word = en_lower;
        active_prefixes = lexicons->english_prefixes;
        active_common_prefixes = lexicons->english_common_prefixes;
    } else {
        active_word = fa;
        active_prefixes = lexicons->persian_prefixes;
        active_common_prefixes = lexicons->persian_common_prefixes;
    }
    active_is_prefix = ks_bloom_contains(active_prefixes, active_word);
    active_is_common_prefix =
        ks_bloom_contains(active_common_prefixes, active_word);

    if (phase == KS_PHASE_LIVE) {
        if (active_known && context_language == target_language &&
            context_strength >= 4) return KS_LIVE_CORRECT_NOW;
        if (active_is_prefix) return KS_LIVE_WAIT_FOR_IDLE;
        return KS_LIVE_CORRECT_NOW;
    }
    if (!active_known && active_is_common_prefix)
        return KS_LIVE_WAIT_FOR_IDLE;
    return KS_LIVE_CORRECT_NOW;
}

KS_LIVE_RESULT ks_evaluate_smart_common(
                                 const KS_TOKEN *tokens, int count,
                                 KS_LANGUAGE active_language, int sensitivity,
                                 KS_LANGUAGE context_language, int context_strength,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_BLOOM *english, const KS_BLOOM *persian,
                                 const KS_BLOOM *english_common,
                                 const KS_BLOOM *persian_common,
                                 const KS_BLOOM *english_prefixes,
                                 const KS_BLOOM *persian_prefixes,
                                 KS_DECISION *decision) {
    KS_LEXICONS lexicons;
    memset(&lexicons, 0, sizeof(lexicons));
    lexicons.english_words = english;
    lexicons.persian_words = persian;
    lexicons.english_common = english_common;
    lexicons.persian_common = persian_common;
    lexicons.english_prefixes = english_prefixes;
    lexicons.persian_prefixes = persian_prefixes;
    return ks_evaluate_contextual(
        tokens, count, active_language, sensitivity,
        context_language, context_strength, 0, phase, &lexicons, decision);
}

KS_LIVE_RESULT ks_evaluate_smart(const KS_TOKEN *tokens, int count,
                                 KS_LANGUAGE active_language, int sensitivity,
                                 KS_LANGUAGE context_language, int context_strength,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_BLOOM *english, const KS_BLOOM *persian,
                                 const KS_BLOOM *english_prefixes,
                                 const KS_BLOOM *persian_prefixes,
                                 KS_DECISION *decision) {
    return ks_evaluate_smart_common(
        tokens, count, active_language, sensitivity,
        context_language, context_strength, phase,
        english, persian, NULL, NULL, english_prefixes, persian_prefixes,
        decision);
}

int ks_evaluate(const KS_TOKEN *tokens, int count, KS_LANGUAGE active_language,
                int minimum_length, const KS_BLOOM *english, const KS_BLOOM *persian,
                KS_DECISION *decision) {
    wchar_t en[KS_MAX_WORD + 1];
    wchar_t en_lower[KS_MAX_WORD + 1];
    wchar_t fa[KS_MAX_WORD + 1];
    int en_known;
    int fa_known;

    if (!decision) return 0;
    memset(decision, 0, sizeof(*decision));
    if (!tokens || count < minimum_length || count > KS_MAX_WORD ||
        (active_language != KS_LANG_ENGLISH && active_language != KS_LANG_PERSIAN)) return 0;

    ks_tokens_to_english(tokens, count, en);
    ks_tokens_to_persian(tokens, count, fa);
    english_lower(en, en_lower);
    ks_word_membership(tokens, count, english, persian, NULL, NULL,
                       &en_known, &fa_known);

    if (active_language == KS_LANG_PERSIAN && en_known && !fa_known) {
        decision->should_correct = 1;
        decision->key_count = count;
        decision->source_language = KS_LANG_PERSIAN;
        decision->target_language = KS_LANG_ENGLISH;
        wcscpy(decision->original, fa);
        wcscpy(decision->replacement, en);
        return 1;
    }

    if (active_language == KS_LANG_ENGLISH && fa_known && !en_known) {
        decision->should_correct = 1;
        decision->key_count = count;
        decision->source_language = KS_LANG_ENGLISH;
        decision->target_language = KS_LANG_PERSIAN;
        wcscpy(decision->original, en);
        wcscpy(decision->replacement, fa);
        return 1;
    }

    return 0;
}

KS_LIVE_RESULT ks_evaluate_live(const KS_TOKEN *tokens, int count,
                                KS_LANGUAGE active_language, int minimum_length,
                                const KS_BLOOM *english, const KS_BLOOM *persian,
                                const KS_BLOOM *english_prefixes,
                                const KS_BLOOM *persian_prefixes,
                                KS_DECISION *decision) {
    wchar_t active_word[KS_MAX_WORD + 1];
    const KS_BLOOM *prefixes;

    if (!ks_evaluate(tokens, count, active_language, minimum_length,
                     english, persian, decision)) return KS_LIVE_NONE;

    if (active_language == KS_LANG_ENGLISH) {
        english_lower(decision->original, active_word);
        prefixes = english_prefixes;
    } else {
        wcscpy(active_word, decision->original);
        prefixes = persian_prefixes;
    }

    if (ks_bloom_contains(prefixes, active_word)) return KS_LIVE_WAIT_FOR_IDLE;
    return KS_LIVE_CORRECT_NOW;
}

static uint32_t clamp_delay(uint32_t value, uint32_t minimum, uint32_t maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

uint32_t ks_update_key_interval_ms(uint32_t current_average_ms,
                                   uint32_t observed_interval_ms) {
    if (current_average_ms < 40u || current_average_ms > 500u) {
        current_average_ms = 150u;
    }
    if (observed_interval_ms < 20u || observed_interval_ms > 1500u) {
        return current_average_ms;
    }
    return (current_average_ms * 3u + observed_interval_ms) / 4u;
}

uint32_t ks_idle_delay_ms(int sensitivity, uint32_t average_key_interval_ms) {
    if (average_key_interval_ms < 40u) average_key_interval_ms = 40u;
    if (average_key_interval_ms > 500u) average_key_interval_ms = 500u;

    if (sensitivity == 2) {
        return clamp_delay(average_key_interval_ms * 2u, 280u, 650u);
    }
    if (sensitivity == 0) {
        return clamp_delay(average_key_interval_ms * 4u, 650u, 1200u);
    }
    return clamp_delay(average_key_interval_ms * 3u, 450u, 900u);
}
