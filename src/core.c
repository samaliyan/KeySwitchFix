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
    en_known = english_word_shape(en_lower) && ks_bloom_contains(english, en_lower);
    fa_known = ks_bloom_contains(persian, fa);

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
