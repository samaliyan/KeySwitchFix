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
    /* At least one 64-bit word of bits: a smaller filter would have no
       whole byte to read (a hostile language pack could declare 4 bits). */
    if (version != 1 || bit_count < 64 || (bit_count & (bit_count - 1)) != 0 ||
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

void ks_hash_text(const wchar_t *value, uint32_t *first, uint32_t *second) {
    *first = 2166136261u;
    *second = 5381u;
    if (!value) return;
    while (*value) {
        hash_codepoint(first, second, (uint32_t)*value);
        ++value;
    }
}

int ks_bloom_contains(const KS_BLOOM *bloom, const wchar_t *value) {
    uint32_t first;
    uint32_t second;
    uint32_t i;

    if (!bloom || !bloom->valid || !value || !*value) return 0;
    ks_hash_text(value, &first, &second);

    second = (second << 1) | 1u;
    for (i = 0; i < bloom->hash_count; ++i) {
        uint32_t bit = (first + i * second + i * i * 0x9E3779B9u) & bloom->bit_mask;
        if ((bloom->bits[bit >> 3] & (1u << (bit & 7))) == 0) return 0;
    }
    return 1;
}

wchar_t ks_canonical_persian(wchar_t character) {
    /*
     * The dictionaries are normalized to Persian yeh (U+06CC) and kaf
     * (U+06A9). Some Windows Persian layouts and older systems emit the
     * Arabic code points instead; without this mapping every correctly typed
     * Persian word containing ي or ك would look unknown, and the English
     * candidate would win a correction it should never have won.
     */
    switch (character) {
        case 0x064A: /* ARABIC LETTER YEH */
        case 0x0649: /* ARABIC LETTER ALEF MAKSURA */
            return 0x06CC;
        case 0x0643: /* ARABIC LETTER KAF */
            return 0x06A9;
        default:
            return character;
    }
}

int ks_is_persian_diacritic(wchar_t character) {
    /*
     * Tanwin, fatha/damma/kasra, shadda, sukun, superscript alef, and the
     * Quranic marks: the Arabic-script combining marks (category Mn) that
     * the dictionary generator strips before storing a spelling.
     */
    return (character >= 0x064B && character <= 0x065F) ||
           character == 0x0670 ||
           (character >= 0x06D6 && character <= 0x06DC) ||
           (character >= 0x06DF && character <= 0x06E4) ||
           character == 0x06E7 || character == 0x06E8 ||
           (character >= 0x06EA && character <= 0x06ED);
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
    token->a = en;
    token->b = fa;
    return 1;
}

void ks_tokens_to_a(const KS_TOKEN *tokens, int count, wchar_t *output) {
    int i;
    if (!output) return;
    for (i = 0; i < count; ++i) output[i] = tokens[i].a;
    output[count] = 0;
}

void ks_tokens_to_b(const KS_TOKEN *tokens, int count, wchar_t *output) {
    int i;
    if (!output) return;
    for (i = 0; i < count; ++i) output[i] = tokens[i].b;
    output[count] = 0;
}

void ks_tokens_to_slot(const KS_TOKEN *tokens, int count, KS_SLOT slot, wchar_t *output) {
    if (slot == KS_SLOT_B) ks_tokens_to_b(tokens, count, output);
    else ks_tokens_to_a(tokens, count, output);
}

/* ---- Letters and case ----------------------------------------------------- */

int ks_is_letter(wchar_t c) {
    unsigned u = (unsigned)c;
    if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')) return 1;
    if (u >= 0x00C0 && u <= 0x024F) return u != 0x00D7 && u != 0x00F7;     /* Latin-1, Extended-A/B */
    if (u >= 0x0370 && u <= 0x03FF)                                        /* Greek */
        return u != 0x0375 && u != 0x037E && u != 0x0384 && u != 0x0385 && u != 0x0387 &&
               u != 0x03F6 && !(u >= 0x0378 && u <= 0x0379) && !(u >= 0x0380 && u <= 0x0383) &&
               u != 0x038B && u != 0x038D && u != 0x03A2;
    if (u >= 0x1F00 && u <= 0x1FFF) return 1;                               /* Greek Extended */
    if (u >= 0x0400 && u <= 0x052F) return !(u >= 0x0482 && u <= 0x0489);   /* Cyrillic */
    if (u >= 0x0531 && u <= 0x0556) return 1;                               /* Armenian */
    if (u >= 0x0561 && u <= 0x0587) return 1;
    if (u >= 0x05D0 && u <= 0x05EA) return 1;                               /* Hebrew */
    if (u >= 0x05F0 && u <= 0x05F2) return 1;
    if (u >= 0x0620 && u <= 0x064A) return 1;                               /* Arabic script */
    if (u == 0x066E || u == 0x066F || (u >= 0x0671 && u <= 0x06D3) || u == 0x06D5) return 1;
    if (u == 0x06EE || u == 0x06EF || (u >= 0x06FA && u <= 0x06FC) || u == 0x06FF) return 1;
    if (u >= 0x0900 && u <= 0x0963) return 1;                               /* Devanagari */
    if (u >= 0x0971 && u <= 0x097F) return 1;
    if (u >= 0x0E01 && u <= 0x0E3A) return 1;                               /* Thai */
    if (u >= 0x0E40 && u <= 0x0E4E) return 1;
    if (u >= 0x10A0 && u <= 0x10FF) return u != 0x10FB;                     /* Georgian */
    return 0;
}

wchar_t ks_to_lower(wchar_t c) {
    unsigned u = (unsigned)c;
    if (u >= 'A' && u <= 'Z') return (wchar_t)(u + 32);
    if (u >= 0x00C0 && u <= 0x00DE && u != 0x00D7) return (wchar_t)(u + 32);
    if (u == 0x0130) return L'i';   /* Turkish dotted capital I */
    if (u >= 0x0100 && u <= 0x0137 && !(u & 1)) return (wchar_t)(u + 1);
    if (u >= 0x0139 && u <= 0x0148 && (u & 1)) return (wchar_t)(u + 1);
    if (u >= 0x014A && u <= 0x0177 && !(u & 1)) return (wchar_t)(u + 1);
    if (u == 0x0178) return (wchar_t)0x00FF;
    if (u >= 0x0179 && u <= 0x017E && (u & 1)) return (wchar_t)(u + 1);
    if (u == 0x0386) return (wchar_t)0x03AC;
    if (u >= 0x0388 && u <= 0x038A) return (wchar_t)(u + 37);
    if (u == 0x038C) return (wchar_t)0x03CC;
    if (u == 0x038E || u == 0x038F) return (wchar_t)(u + 63);
    if (u >= 0x0391 && u <= 0x03AB && u != 0x03A2) return (wchar_t)(u + 32);
    if (u >= 0x0400 && u <= 0x040F) return (wchar_t)(u + 80);
    if (u >= 0x0410 && u <= 0x042F) return (wchar_t)(u + 32);
    if (u >= 0x0460 && u <= 0x0481 && !(u & 1)) return (wchar_t)(u + 1);
    if (u >= 0x048A && u <= 0x04BF && !(u & 1)) return (wchar_t)(u + 1);
    if (u >= 0x04D0 && u <= 0x052F && !(u & 1)) return (wchar_t)(u + 1);
    if (u >= 0x0531 && u <= 0x0556) return (wchar_t)(u + 48);
    return c;
}

static int is_hebrew_point(wchar_t c) {
    return c >= 0x0591 && c <= 0x05C7 && c != 0x05BE && c != 0x05C0 && c != 0x05C3 && c != 0x05C6;
}

/* ---- Language profiles ---------------------------------------------------- */

static KS_LANG_PROFILE g_profile_english;
static KS_LANG_PROFILE g_profile_persian;
static int g_profiles_ready;

static void profile_add_short(KS_LANG_PROFILE *profile, const wchar_t *word) {
    size_t length = wcslen(word);
    if (profile->short_count >= KS_SHORT_WORDS_MAX || length < 1 || length > 2) return;
    wcscpy(profile->short_words[profile->short_count++], word);
}

/*
 * Two-key words are deliberately excluded from the Bloom dictionaries, so
 * these exact lists are the only membership evidence for them. Every English
 * token whose physical keys also spell a listed Persian word must appear
 * here: otherwise the Persian side becomes "objective" one-sided evidence and
 * a genuine English token is rewritten. "id" (هی), "ms" (پس) and "pr" (حق)
 * are the common cases; keeping them on both lists turns them into ordinary
 * collisions that need sentence context or an explicit preference.
 *
 * می, ها, تر, ام and ات are the halves that surround a ZWNJ in everyday
 * Persian (می‌خواهم, کتاب‌ها, بزرگ‌تر, خانه‌ام, دست‌ات). Shift+Space
 * separates them into their own tokens, so they must be recognizable on
 * their own. None of their physical-key spellings (ld, ih, jv, hl, hj) is an
 * English word. Keep both lists identical to tools/generate_blooms.py.
 */
static void prepare_profiles(void) {
    static const wchar_t *const english[] = {
        L"a", L"i",
        L"ad", L"ah", L"ai", L"am", L"an", L"as", L"at", L"be",
        L"by", L"do", L"go", L"he", L"hi", L"id", L"if", L"in",
        L"is", L"it", L"me", L"ms", L"my", L"no", L"of", L"oh",
        L"ok", L"on", L"or", L"pr", L"so", L"to", L"up", L"us",
        L"we"
    };
    static const wchar_t *const persian[] = {
        L"و",
        L"آب", L"آن", L"آه", L"از", L"ام", L"او", L"ای", L"ات",
        L"با", L"بد", L"بر", L"به", L"بی", L"پا", L"پر", L"پس",
        L"تا", L"تب", L"تر", L"ته", L"تو", L"جا", L"جز", L"چه",
        L"خب", L"خط", L"در", L"دل", L"دم", L"ده", L"دو", L"را",
        L"رو", L"زن", L"سر", L"سن", L"سه", L"شب", L"شد", L"حق",
        L"حل", L"کم", L"کن", L"که", L"کل", L"کی", L"گل", L"لب",
        L"ما", L"من", L"می", L"نه", L"نو", L"ها", L"هم", L"هر",
        L"هی", L"یا", L"یک", L"وی", L"یخ", L"شی", L"شه"
    };
    size_t i;
    if (g_profiles_ready) return;
    memset(&g_profile_english, 0, sizeof(g_profile_english));
    strcpy(g_profile_english.code, "en");
    g_profile_english.model = KS_LANG_ENGLISH;
    g_profile_english.flags = KS_PROFILE_CASED | KS_PROFILE_ASCII_ONLY | KS_PROFILE_APOSTROPHE |
                              KS_PROFILE_LATIN;
    for (i = 0; i < sizeof(english) / sizeof(english[0]); ++i)
        profile_add_short(&g_profile_english, english[i]);
    memset(&g_profile_persian, 0, sizeof(g_profile_persian));
    strcpy(g_profile_persian.code, "fa");
    g_profile_persian.model = KS_LANG_PERSIAN;
    g_profile_persian.flags = KS_PROFILE_ARABIC_MARKS | KS_PROFILE_PERSIAN_ALEF | KS_PROFILE_NO_SHAPE;
    for (i = 0; i < sizeof(persian) / sizeof(persian[0]); ++i)
        profile_add_short(&g_profile_persian, persian[i]);
    g_profiles_ready = 1;
}

const KS_LANG_PROFILE *ks_profile_english(void) {
    prepare_profiles();
    return &g_profile_english;
}

const KS_LANG_PROFILE *ks_profile_persian(void) {
    prepare_profiles();
    return &g_profile_persian;
}

static const KS_LANG_PROFILE *slot_profile(const KS_LEXICONS *lexicons, KS_SLOT slot) {
    if (lexicons && (slot == KS_SLOT_A || slot == KS_SLOT_B) && lexicons->profile[slot])
        return lexicons->profile[slot];
    return slot == KS_SLOT_B ? ks_profile_persian() : ks_profile_english();
}

int ks_profile_short_word(const KS_LANG_PROFILE *profile, const wchar_t *form) {
    int i;
    if (!profile || !form) return 0;
    for (i = 0; i < profile->short_count; ++i)
        if (wcscmp(form, profile->short_words[i]) == 0) return 1;
    return 0;
}

/*
 * Dictionary spellings carry no diacritics, but users type them (حتماً,
 * مدرّس). Look words up without the marks so a correctly typed Persian
 * word with a tashdid or tanwin is still recognized as Persian instead of
 * becoming "unknown" and handing the decision to the English candidate.
 * Lower-case and the letters-only shape follow the language's profile.
 */
int ks_lookup_form(const KS_LANG_PROFILE *profile, const wchar_t *reading, wchar_t *output) {
    int length = 0;
    int index;
    int apostrophes = 0;
    if (!profile || !reading || !output) return 0;
    for (; *reading; ++reading) {
        wchar_t c = *reading;
        if ((profile->flags & KS_PROFILE_ARABIC_MARKS) && ks_is_persian_diacritic(c)) continue;
        if ((profile->flags & KS_PROFILE_HEBREW_POINTS) && is_hebrew_point(c)) continue;
        if (profile->flags & KS_PROFILE_CASED) {
            if (profile->flags & KS_PROFILE_ASCII_ONLY) {
                if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            } else {
                c = ks_to_lower(c);
            }
        }
        if (length >= KS_MAX_WORD) {   /* longer than any dictionary word */
            output[0] = 0;
            return 0;
        }
        output[length++] = c;
    }
    output[length] = 0;
    if (length < 1) return 0;
    if (profile->flags & KS_PROFILE_NO_SHAPE) return length;
    for (index = 0; index < length; ++index) {
        wchar_t c = output[index];
        int letter = (profile->flags & KS_PROFILE_ASCII_ONLY) ? (c >= L'a' && c <= L'z') : ks_is_letter(c);
        if (letter) continue;
        if ((profile->flags & KS_PROFILE_APOSTROPHE) && (c == L'\'' || c == 0x2019) &&
            !apostrophes && index > 0 && index < length - 1) {
            apostrophes = 1;
            continue;
        }
        return 0;
    }
    return length;
}

static int word_bloom_contains(const KS_BLOOM *primary,
                               const KS_BLOOM *supplemental,
                               const wchar_t *value) {
    return ks_bloom_contains(primary, value) ||
           ks_bloom_contains(supplemental, value);
}

/* Membership of a word already in lookup form. */
static int form_known(const KS_LANG_PROFILE *profile, const wchar_t *form, int length,
                      const KS_BLOOM *words, const KS_BLOOM *common) {
    wchar_t canonical[KS_MAX_WORD + 1];
    int known = length <= 2 ? ks_profile_short_word(profile, form)
                            : word_bloom_contains(words, common, form);
    if (!known && length >= 2 && form[length - 1] == 0x03C3) {
        /* Greek: a word typed in capitals ends in Σ, which lowers to σ; the
           dictionary spells the end of a word with final ς. */
        wcscpy(canonical, form);
        canonical[length - 1] = 0x03C2;
        known = length <= 2 ? ks_profile_short_word(profile, canonical)
                            : word_bloom_contains(words, common, canonical);
    }
    if (known || !(profile->flags & KS_PROFILE_PERSIAN_ALEF) || form[0] != 0x0627) return known;
    /*
     * Persian users commonly omit Shift for an initial alef-madda: ایا,
     * اقا, ارام, ... . Use the canonical form only for dictionary lookup;
     * the replacement keeps the exact spelling the user physically typed.
     */
    wcscpy(canonical, form);
    canonical[0] = 0x0622;
    return length <= 2 ? ks_profile_short_word(profile, canonical)
                       : word_bloom_contains(words, common, canonical);
}

int ks_text_known_in(const KS_LANG_PROFILE *profile, const wchar_t *text,
                     const KS_BLOOM *words, const KS_BLOOM *common) {
    wchar_t form[KS_MAX_WORD + 1];
    int length;
    if (!text || !*text || wcslen(text) > KS_MAX_WORD) return 0;
    length = ks_lookup_form(profile, text, form);
    return length > 0 && form_known(profile, form, length, words, common);
}

int ks_text_known(const wchar_t *text, KS_LANGUAGE language,
                  const KS_BLOOM *words, const KS_BLOOM *common) {
    if (language == KS_LANG_PERSIAN) return ks_text_known_in(ks_profile_persian(), text, words, common);
    if (language == KS_LANG_ENGLISH) return ks_text_known_in(ks_profile_english(), text, words, common);
    return 0;
}

/* The letters of a reading without the punctuation that text really puts
   around a word: an opening quote, and a closing quote, comma or semicolon
   (`'file'` → file, `no,` → no). Those keys are letters in some layouts
   (Persian گ ک و, Russian э ж б), so the other reading wins unopposed unless
   the word between them decides. Brackets are not stripped: ] and [ are the
   Persian letters چ and ج, which end common words (هیچ is typed id]).
   Returns the core's length; target may be the source. */
static size_t punctuation_core(const wchar_t *source, wchar_t *target) {
    size_t start = 0;
    size_t end = wcslen(source);
    while (start < end && (source[start] == L'\'' || source[start] == L'"')) ++start;
    /* A full stop at the end, too: in a pair where that key is a letter of
       the other language (Russian ю), "it." is one token, and the English
       reading must still count as "it". */
    while (end > start && (source[end - 1] == L'\'' || source[end - 1] == L'"' ||
                           source[end - 1] == L',' || source[end - 1] == L';' ||
                           source[end - 1] == L'.')) --end;
    memmove(target, source + start, (end - start) * sizeof(wchar_t));
    target[end - start] = 0;
    return end - start;
}

int ks_word_membership(const KS_TOKEN *tokens, int count,
                       const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                       const KS_BLOOM *a_common, const KS_BLOOM *b_common,
                       int *a_known, int *b_known) {
    KS_LEXICONS lexicons;
    int known[3] = {0, 0, 0};
    int frequent[3];
    memset(&lexicons, 0, sizeof(lexicons));
    lexicons.words[KS_SLOT_A] = a_words;
    lexicons.words[KS_SLOT_B] = b_words;
    lexicons.common[KS_SLOT_A] = a_common;
    lexicons.common[KS_SLOT_B] = b_common;
    if (!ks_classify_word(tokens, count, &lexicons, known, frequent)) return 0;
    if (a_known) *a_known = known[KS_SLOT_A];
    if (b_known) *b_known = known[KS_SLOT_B];
    return 1;
}

/* Known / frequent for one reading. `form` receives the lookup form that
   decided it (the punctuation core when that is what was known). */
static void classify_reading(const KS_TOKEN *tokens, int count, KS_SLOT slot,
                             const KS_LEXICONS *lexicons, int *known, int *frequent,
                             wchar_t *form) {
    const KS_LANG_PROFILE *profile = slot_profile(lexicons, slot);
    wchar_t reading[KS_MAX_WORD + 1];
    int length;
    int extra = 0;
    ks_tokens_to_slot(tokens, count, slot, reading);
    length = ks_lookup_form(profile, reading, form);
    *known = length > 0 && form_known(profile, form, length, lexicons->words[slot], lexicons->common[slot]);
    /*
     * Text with quote or comma keys at its edges: `no,` or `'file'`. The
     * letters between the punctuation decide (two letters at least, so a
     * Persian word such as چه, typed as `]i`, is not mistaken for English).
     */
    if (!*known) {
        wchar_t core[KS_MAX_WORD + 1];
        wchar_t core_form[KS_MAX_WORD + 1];
        size_t core_length = punctuation_core(reading, core);
        if (core_length >= 2 && core_length < (size_t)count) {
            int core_form_length = ks_lookup_form(profile, core, core_form);
            if (core_form_length >= 2 &&
                form_known(profile, core_form, core_form_length,
                           lexicons->words[slot], lexicons->common[slot])) {
                *known = 1;
                wcscpy(form, core_form);
                length = core_form_length;
            }
        }
    }
    if (length <= 0) {
        /* Keep a usable lookup form for the prefix checks: the raw reading
           lowered and without marks, even if it is not a word shape. */
        wcscpy(form, reading);
    }
    /* Pack and memory words are the user's everyday vocabulary: they count
       as frequent, so a collision never goes against them on frequency.
       Checked even for dictionary words: `int` or `svn` in the IT pack must
       count as frequent, or a collision goes against them. */
    if (count >= 3 && length > 0 && lexicons->extra && lexicons->extra->contains &&
        lexicons->extra->contains(lexicons->extra->context, slot, form)) {
        *known = 1;
        extra = 1;
    }
    *frequent = extra || (*known && ks_bloom_contains(lexicons->frequent[slot], form));
}

int ks_classify_word(const KS_TOKEN *tokens, int count,
                     const KS_LEXICONS *lexicons,
                     int known[3], int frequent[3]) {
    wchar_t form[KS_MAX_WORD + 1];
    int unused[3];
    if (!frequent) frequent = unused;   /* the caller needs only `known` */
    if (!tokens || !lexicons || !known || count < 1 || count > KS_MAX_WORD) return 0;
    known[0] = frequent[0] = 0;
    classify_reading(tokens, count, KS_SLOT_A, lexicons, &known[KS_SLOT_A], &frequent[KS_SLOT_A], form);
    classify_reading(tokens, count, KS_SLOT_B, lexicons, &known[KS_SLOT_B], &frequent[KS_SLOT_B], form);
    return 1;
}

void ks_context_reset(KS_LANGUAGE_CONTEXT *context) {
    if (!context) return;
    memset(context, 0, sizeof(*context));
    context->slot = KS_SLOT_NONE;
}

void ks_context_observe(KS_LANGUAGE_CONTEXT *context, KS_SLOT slot, int evidence) {
    if (!context || (slot != KS_SLOT_A && slot != KS_SLOT_B)) return;
    if (evidence < 1) evidence = 1;
    if (evidence > 4) evidence = 4;
    ++context->observed_words;
    if (context->slot == KS_SLOT_NONE || context->strength <= 0) {
        context->slot = slot;
        context->strength = evidence;
        return;
    }
    if (context->slot == slot) {
        if (context->strength < evidence) context->strength = evidence;
        else if (context->strength < 4) ++context->strength;
        return;
    }
    if (context->strength > evidence) {
        context->strength -= evidence;
        return;
    }
    if (context->strength == evidence) {
        context->slot = KS_SLOT_NONE;
        context->strength = 0;
        return;
    }
    context->slot = slot;
    context->strength = evidence - context->strength;
}

KS_SLOT ks_context_current(const KS_LANGUAGE_CONTEXT *context, int *strength) {
    if (strength) *strength = 0;
    if (!context || context->strength <= 0 ||
        (context->slot != KS_SLOT_A && context->slot != KS_SLOT_B)) return KS_SLOT_NONE;
    if (strength) *strength = context->strength;
    return context->slot;
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
                         KS_SLOT context_slot, int context_strength,
                         const KS_LEXICONS *lexicons,
                         KS_SEQUENCE_RESULT *result) {
    int index;
    int score[3] = {0, 0, 0};
    int known_words[3] = {0, 0, 0};
    int threshold;
    int margin;
    KS_SLOT slot;

    if (!result) return 0;
    memset(result, 0, sizeof(*result));
    result->slot = KS_SLOT_NONE;
    if (!words || !lexicons || word_count < 2 ||
        word_count > KS_MAX_SEQUENCE_WORDS) return 0;

    for (index = 0; index < word_count; ++index) {
        int known[3];
        int frequent[3];
        int unknown_penalty;
        if (!words[index].tokens || words[index].count < 1 ||
            words[index].count > KS_MAX_WORD ||
            !ks_classify_word(words[index].tokens, words[index].count,
                              lexicons, known, frequent))
            return 0;

        unknown_penalty = words[index].count <= 2 ? 16 : 26;
        for (slot = KS_SLOT_A; slot <= KS_SLOT_B; slot = (KS_SLOT)(slot + 1)) {
            if (known[slot]) {
                score[slot] += 30 + (frequent[slot] ? 18 : 0);
                ++known_words[slot];
            } else {
                score[slot] -= unknown_penalty;
            }
        }
    }

    /*
     * A complete same-language run is stronger than the sum of isolated
     * words. This is the local sentence evidence that turns
     * "nv clhkd ;i" into "در زمانی که" without sending text anywhere.
     */
    for (slot = KS_SLOT_A; slot <= KS_SLOT_B; slot = (KS_SLOT)(slot + 1))
        if (known_words[slot] == word_count) score[slot] += 12 + word_count * 8;

    /* Strength 5 is an explicit collision preference, not learned context. */
    if (context_strength > 0 && context_strength <= 4 &&
        (context_slot == KS_SLOT_A || context_slot == KS_SLOT_B))
        score[context_slot] += context_strength * 8;

    memcpy(result->score, score, sizeof(score));
    memcpy(result->known_words, known_words, sizeof(known_words));

    threshold = sensitivity <= 0 ? 55 : sensitivity >= 2 ? 30 : 40;
    margin = score[KS_SLOT_A] - score[KS_SLOT_B];
    if (margin >= threshold && known_words[KS_SLOT_A] == word_count &&
        known_words[KS_SLOT_A] >= 2) {
        result->slot = KS_SLOT_A;
        result->confidence = margin > 100 ? 100 : margin;
        return 1;
    }
    if (-margin >= threshold && known_words[KS_SLOT_B] == word_count &&
        known_words[KS_SLOT_B] >= 2) {
        result->slot = KS_SLOT_B;
        result->confidence = -margin > 100 ? 100 : -margin;
        return 1;
    }
    return 0;
}

static void fill_decision(const KS_TOKEN *tokens, int count,
                          KS_SLOT active_slot, int confidence,
                          KS_DECISION *decision) {
    KS_SLOT target_slot = KS_OTHER_SLOT(active_slot);
    memset(decision, 0, sizeof(*decision));
    decision->should_correct = 1;
    decision->key_count = count;
    decision->confidence = confidence;
    decision->source_slot = active_slot;
    decision->target_slot = target_slot;
    ks_tokens_to_slot(tokens, count, active_slot, decision->original);
    ks_tokens_to_slot(tokens, count, target_slot, decision->replacement);
}

/* Lower case and marks removed, without the word-shape test: what the
   prefix dictionaries hold for a word that is still being typed. */
static void prefix_form(const KS_LANG_PROFILE *profile, const wchar_t *reading, wchar_t *output) {
    int length = 0;
    for (; *reading && length < KS_MAX_WORD; ++reading) {
        wchar_t c = *reading;
        if ((profile->flags & KS_PROFILE_ARABIC_MARKS) && ks_is_persian_diacritic(c)) continue;
        if ((profile->flags & KS_PROFILE_HEBREW_POINTS) && is_hebrew_point(c)) continue;
        if (profile->flags & KS_PROFILE_CASED) {
            if (profile->flags & KS_PROFILE_ASCII_ONLY) {
                if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            } else {
                c = ks_to_lower(c);
            }
        }
        output[length++] = c;
    }
    output[length] = 0;
}

/* The target reading is among the 20,000 common words of its language. */
static int target_is_common(const KS_TOKEN *tokens, int count, KS_SLOT target_slot,
                            const KS_LEXICONS *lexicons) {
    wchar_t word[KS_MAX_WORD + 1];
    wchar_t normal[KS_MAX_WORD + 1];
    const KS_BLOOM *common = lexicons->common[target_slot];
    if (!common) return 1;   /* no frequency tiers loaded: nothing to judge by */
    ks_tokens_to_slot(tokens, count, target_slot, word);
    prefix_form(slot_profile(lexicons, target_slot), word, normal);
    return ks_bloom_contains(common, normal);
}

/* The active reading is a vocabulary-pack or writing-memory word. */
static int active_reading_is_extra(const KS_TOKEN *tokens, int count, KS_SLOT active_slot,
                                   const KS_LEXICONS *lexicons) {
    wchar_t word[KS_MAX_WORD + 1];
    wchar_t normal[KS_MAX_WORD + 1];
    if (count < 3 || !lexicons->extra || !lexicons->extra->contains) return 0;
    ks_tokens_to_slot(tokens, count, active_slot, word);
    if (!ks_lookup_form(slot_profile(lexicons, active_slot), word, normal)) return 0;
    return lexicons->extra->contains(lexicons->extra->context, active_slot, normal);
}

/* Corpus collision priors exist for the English/Persian pair only; they
   are keyed by the English reading and favour Persian when positive.
   Returns the points in favour of `target_slot`. */
static int collision_prior_for(const KS_TOKEN *tokens, int count, KS_SLOT target_slot,
                               const KS_LEXICONS *lexicons) {
    KS_SLOT english_slot;
    wchar_t reading[KS_MAX_WORD + 1];
    wchar_t lowered[KS_MAX_WORD + 1];
    int prior;
    if (slot_profile(lexicons, KS_SLOT_A)->model == KS_LANG_ENGLISH &&
        slot_profile(lexicons, KS_SLOT_B)->model == KS_LANG_PERSIAN)
        english_slot = KS_SLOT_A;
    else if (slot_profile(lexicons, KS_SLOT_B)->model == KS_LANG_ENGLISH &&
             slot_profile(lexicons, KS_SLOT_A)->model == KS_LANG_PERSIAN)
        english_slot = KS_SLOT_B;
    else
        return 0;
    ks_tokens_to_slot(tokens, count, english_slot, reading);
    prefix_form(ks_profile_english(), reading, lowered);
    prior = ks_collision_prior_points(lowered);
    return target_slot == english_slot ? -prior : prior;
}

KS_LIVE_RESULT ks_evaluate_contextual(
                                 const KS_TOKEN *tokens, int count,
                                 KS_SLOT active_slot, int sensitivity,
                                 KS_SLOT context_slot, int context_strength,
                                 int sentence_start,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_LEXICONS *lexicons,
                                 KS_DECISION *decision) {
    wchar_t reading[KS_MAX_WORD + 1];
    wchar_t active_word[KS_MAX_WORD + 1];
    const KS_LANG_PROFILE *active_profile;
    KS_SLOT target_slot;
    int known[3];
    int frequent[3];
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
        (active_slot != KS_SLOT_A && active_slot != KS_SLOT_B)) return KS_LIVE_NONE;
    /* Keys that type the same text in both layouts (most of a German word
       on a US keyboard) need no repair, whatever the dictionaries say. */
    {
        wchar_t other[KS_MAX_WORD + 1];
        ks_tokens_to_a(tokens, count, reading);
        ks_tokens_to_b(tokens, count, other);
        if (wcscmp(reading, other) == 0) return KS_LIVE_NONE;
    }
    if (!ks_classify_word(tokens, count, lexicons, known, frequent))
        return KS_LIVE_NONE;

    target_slot = KS_OTHER_SLOT(active_slot);
    active_profile = slot_profile(lexicons, active_slot);
    active_known = known[active_slot];
    target_known = known[target_slot];
    active_frequent = frequent[active_slot];
    target_frequent = frequent[target_slot];
    if (!target_known) return KS_LIVE_NONE;

    /*
     * A one-sided dictionary match is objective layout evidence. Language
     * preference and sentence context must never suppress it; this is the
     * critical distinction that keeps اثممخ -> hello working in Prefer
     * Persian mode.
     */
    if (!active_known) {
        /*
         * Up to three keys is where abbreviations live: src, mv, cfg, pg
         * are not dictionary words, but their readings in the other layout
         * are. Inside a clearly Latin-script sentence such a short reading
         * does not overrule the sentence, and without context a rare
         * target waits for the end of the word instead of being rewritten
         * while the user may still be typing mkdir or srv.
         */
        if ((count == 2 || (count == 3 && !target_frequent)) &&
            (active_profile->flags & KS_PROFILE_LATIN) && context_slot == active_slot &&
            context_strength >= 3 && context_strength < 5)
            return KS_LIVE_NONE;
        if (count <= 3 && !target_frequent && phase != KS_PHASE_BOUNDARY &&
            !target_is_common(tokens, count, target_slot, lexicons))
            return KS_LIVE_NONE;
        confidence = 90;
        if (count >= 5) confidence += 10;
        else if (count >= 3) confidence += 5;
        if (confidence > 100) confidence = 100;
        fill_decision(tokens, count, active_slot, confidence, decision);
    } else {
        /*
         * Both layouts produce real words. Only this branch uses a user
         * preference, sentence evidence, and corpus-normalized prior.
         * context_strength 5 is reserved for the explicit tray preference.
         */
        explicit_preference = context_strength >= 5;
        if (explicit_preference) {
            if (context_slot != target_slot) return KS_LIVE_NONE;
            evidence = 100;
        } else {
            if (context_strength < 0) context_strength = 0;
            if (context_strength > 4) context_strength = 4;
            evidence = 0;
            if (context_slot == target_slot) {
                int support = context_strength * 30;
                /* A vocabulary-pack or memory word (hdd, svn, int) is the
                   user's own word: the sentence alone may not turn it into
                   a rare word of the other language. */
                if (support > 30 && !target_frequent &&
                    active_reading_is_extra(tokens, count, active_slot, lexicons))
                    support = 30;
                /* Nor a frequent word into a rare one (آخر -> Hov). */
                if (active_frequent && !target_frequent &&
                    !target_is_common(tokens, count, target_slot, lexicons))
                    return KS_LIVE_NONE;
                evidence += support;
            } else if (context_slot == active_slot)
                evidence -= context_strength * 30;
            if (target_frequent && !active_frequent)
                evidence += 45;
            else if (active_frequent && !target_frequent)
                evidence -= 45;

            prior = collision_prior_for(tokens, count, target_slot, lexicons);
            /* A pack or memory word is the user's own vocabulary: corpus
               priors say nothing about how this user writes it. */
            if (active_reading_is_extra(tokens, count, active_slot, lexicons)) prior = 0;
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
        fill_decision(tokens, count, active_slot, confidence, decision);
    }

    if (phase == KS_PHASE_BOUNDARY) return KS_LIVE_CORRECT_NOW;
    /*
     * Two keys are the beginning of too many words ("fi" is به, and also
     * the start of first, find, file): a two-key word is repaired only when
     * the user ends it, never on a pause.
     */
    if (count == 2) return KS_LIVE_NONE;

    ks_tokens_to_slot(tokens, count, active_slot, reading);
    prefix_form(active_profile, reading, active_word);
    /* 'first' or "file": the quote keys are letters in the other layout,
       but here they are punctuation around the word being typed. Behind a
       quote, fewer than three letters say too little to act on before the
       word is finished. */
    {
        int opening_quote = active_word[0] == L'\'' || active_word[0] == L'"';
        size_t core_length = punctuation_core(active_word, active_word);
        if (opening_quote && core_length < 3) return KS_LIVE_NONE;
    }
    active_is_prefix = ks_bloom_contains(lexicons->prefixes[active_slot], active_word);
    /* "kuber" on the English layout is the start of "kubernetes": the
       vocabulary packs protect their words while they are being typed. */
    active_is_common_prefix =
        ks_bloom_contains(lexicons->common_prefixes[active_slot], active_word);
    /* Pack words are everyday words for their users: their beginnings are
       protected at a pause too, like common words. */
    if (lexicons->extra && lexicons->extra->has_prefix &&
        lexicons->extra->has_prefix(lexicons->extra->context, active_slot, active_word)) {
        active_is_prefix = 1;
        active_is_common_prefix = 1;
    }

    /*
     * A collision (both readings are words) is decided on the complete word.
     * While the letters typed so far can still become a longer word in the
     * active layout ("int" → internet, "col" → column), neither context nor
     * a pause may rewrite them.
     */
    if (active_known && (active_is_prefix || active_is_common_prefix))
        return KS_LIVE_NONE;

    if (phase == KS_PHASE_LIVE) {
        if (active_known && context_slot == target_slot &&
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
                                 KS_SLOT active_slot, int sensitivity,
                                 KS_SLOT context_slot, int context_strength,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                                 const KS_BLOOM *a_common,
                                 const KS_BLOOM *b_common,
                                 const KS_BLOOM *a_prefixes,
                                 const KS_BLOOM *b_prefixes,
                                 KS_DECISION *decision) {
    KS_LEXICONS lexicons;
    memset(&lexicons, 0, sizeof(lexicons));
    lexicons.words[KS_SLOT_A] = a_words;
    lexicons.words[KS_SLOT_B] = b_words;
    lexicons.common[KS_SLOT_A] = a_common;
    lexicons.common[KS_SLOT_B] = b_common;
    lexicons.prefixes[KS_SLOT_A] = a_prefixes;
    lexicons.prefixes[KS_SLOT_B] = b_prefixes;
    return ks_evaluate_contextual(
        tokens, count, active_slot, sensitivity,
        context_slot, context_strength, 0, phase, &lexicons, decision);
}

KS_LIVE_RESULT ks_evaluate_smart(const KS_TOKEN *tokens, int count,
                                 KS_SLOT active_slot, int sensitivity,
                                 KS_SLOT context_slot, int context_strength,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                                 const KS_BLOOM *a_prefixes,
                                 const KS_BLOOM *b_prefixes,
                                 KS_DECISION *decision) {
    return ks_evaluate_smart_common(
        tokens, count, active_slot, sensitivity,
        context_slot, context_strength, phase,
        a_words, b_words, NULL, NULL, a_prefixes, b_prefixes,
        decision);
}

int ks_evaluate(const KS_TOKEN *tokens, int count, KS_SLOT active_slot,
                int minimum_length, const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                KS_DECISION *decision) {
    int a_known = 0;
    int b_known = 0;
    int active_known;
    int target_known;

    if (!decision) return 0;
    memset(decision, 0, sizeof(*decision));
    if (!tokens || count < minimum_length || count > KS_MAX_WORD ||
        (active_slot != KS_SLOT_A && active_slot != KS_SLOT_B)) return 0;
    ks_word_membership(tokens, count, a_words, b_words, NULL, NULL, &a_known, &b_known);
    active_known = active_slot == KS_SLOT_A ? a_known : b_known;
    target_known = active_slot == KS_SLOT_A ? b_known : a_known;
    if (target_known && !active_known) {
        fill_decision(tokens, count, active_slot, 0, decision);
        if (wcscmp(decision->original, decision->replacement) == 0) {
            memset(decision, 0, sizeof(*decision));
            return 0;
        }
        return 1;
    }
    return 0;
}

KS_LIVE_RESULT ks_evaluate_live(const KS_TOKEN *tokens, int count,
                                KS_SLOT active_slot, int minimum_length,
                                const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                                const KS_BLOOM *a_prefixes,
                                const KS_BLOOM *b_prefixes,
                                KS_DECISION *decision) {
    wchar_t active_word[KS_MAX_WORD + 1];

    if (!ks_evaluate(tokens, count, active_slot, minimum_length,
                     a_words, b_words, decision)) return KS_LIVE_NONE;
    prefix_form(slot_profile(NULL, active_slot), decision->original, active_word);
    if (ks_bloom_contains(active_slot == KS_SLOT_A ? a_prefixes : b_prefixes, active_word))
        return KS_LIVE_WAIT_FOR_IDLE;
    return KS_LIVE_CORRECT_NOW;
}

/* ---- Language packs --------------------------------------------------------
 *
 * A .kslang file (little-endian), written by tools/build_language_pack.py:
 *
 *   0   "KSLP"            magic
 *   4   u32 version       1
 *   8   char code[8]      ISO 639-1, NUL padded
 *   16  u32 flags         KS_PROFILE_*
 *   20  u16 langid[4]     Windows primary language ids, 0 = unused
 *   28  u32 model         0 (only a dictionary), 1 English, 2 Persian
 *   32  char english[48]  UTF-8, NUL padded
 *   80  char native[48]   UTF-8, NUL padded
 *   128 u32 section_count
 *   132 sections: { u32 type, u32 offset, u32 size } x section_count
 *
 * Sections: 1 words, 2 common, 3 frequent, 4 prefixes, 5 common prefixes
 * (Bloom resources, "KSWB"), 6 short words (UTF-8, one per line).
 */

#define PACK_HEADER_SIZE 132u

static uint16_t read_u16_le(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* UTF-8 (no NUL inside) to wide characters; BMP only. Returns the number
   of characters, or -1 for invalid input or no room. */
static int decode_utf8(const unsigned char *source, size_t size, wchar_t *output, size_t capacity) {
    size_t i = 0;
    size_t used = 0;
    while (i < size && source[i]) {
        unsigned c = source[i];
        unsigned value;
        int extra;
        if (c < 0x80) { value = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { value = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { value = c & 0x0F; extra = 2; }
        else return -1;
        ++i;
        while (extra-- > 0) {
            if (i >= size || (source[i] & 0xC0) != 0x80) return -1;
            value = (value << 6) | (source[i] & 0x3F);
            ++i;
        }
        if (value >= 0xD800 && value <= 0xDFFF) return -1;
        if (used + 1 >= capacity) return -1;
        output[used++] = (wchar_t)value;
    }
    output[used] = 0;
    return (int)used;
}

int ks_pack_parse_header(const unsigned char *data, size_t size, KS_LANG_PACK *pack) {
    uint32_t model;
    int i;
    if (!pack) return 0;
    memset(pack, 0, sizeof(*pack));
    if (!data || size < PACK_HEADER_SIZE || memcmp(data, "KSLP", 4) != 0) return 0;
    if (read_u32_le(data + 4) != 1) return 0;
    for (i = 0; i < 8 && data[8 + i]; ++i) {
        char c = (char)data[8 + i];
        if (!((c >= 'a' && c <= 'z') || c == '-' || (c >= '0' && c <= '9'))) return 0;
        pack->profile.code[i] = c;
    }
    if (i < 2 || i > 7) return 0;
    pack->profile.flags = read_u32_le(data + 16) & KS_PROFILE_KNOWN_FLAGS;
    for (i = 0; i < KS_PACK_MAX_LANGIDS; ++i) {
        unsigned short id = read_u16_le(data + 20 + 2 * i);
        if (id) pack->langids[pack->langid_count++] = id;
    }
    model = read_u32_le(data + 28);
    pack->profile.model = model == 1 ? KS_LANG_ENGLISH : model == 2 ? KS_LANG_PERSIAN : KS_LANG_OTHER;
    if (decode_utf8(data + 32, 48, pack->english_name, 48) < 1) return 0;
    if (decode_utf8(data + 80, 48, pack->native_name, 48) < 0) return 0;
    return 1;
}

int ks_pack_parse(const unsigned char *data, size_t size, KS_LANG_PACK *pack) {
    uint32_t sections;
    uint32_t index;
    if (!ks_pack_parse_header(data, size, pack)) return 0;
    sections = read_u32_le(data + 128);
    if (sections > 16 || PACK_HEADER_SIZE + (size_t)sections * 12u > size) return 0;
    for (index = 0; index < sections; ++index) {
        const unsigned char *entry = data + PACK_HEADER_SIZE + index * 12u;
        uint32_t type = read_u32_le(entry);
        uint32_t offset = read_u32_le(entry + 4);
        uint32_t length = read_u32_le(entry + 8);
        KS_BLOOM *target = NULL;
        if ((size_t)offset > size || (size_t)length > size - (size_t)offset) return 0;
        switch (type) {
            case 1: target = &pack->words; break;
            case 2: target = &pack->common; break;
            case 3: target = &pack->frequent; break;
            case 4: target = &pack->prefixes; break;
            case 5: target = &pack->common_prefixes; break;
            case 6: {
                /* Short words: one per line, one or two letters each. */
                size_t start = offset;
                size_t end = (size_t)offset + length;
                while (start < end) {
                    size_t stop = start;
                    wchar_t word[4];
                    int characters;
                    while (stop < end && data[stop] != '\n') ++stop;
                    characters = stop - start <= 8 ? decode_utf8(data + start, stop - start, word, 4) : -1;
                    if (characters >= 1 && characters <= 2)
                        profile_add_short(&pack->profile, word);
                    start = stop + 1;
                }
                break;
            }
            default:
                break;   /* unknown sections are ignored: newer packs stay loadable */
        }
        if (target && !ks_bloom_init(target, data + offset, length)) return 0;
    }
    /* A pack must at least know its words and their beginnings. */
    return pack->words.valid && pack->prefixes.valid;
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
