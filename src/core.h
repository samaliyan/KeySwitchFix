#ifndef KEYSWITCHFIX_CORE_H
#define KEYSWITCHFIX_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KS_MAX_WORD 32
#define KS_MAX_SEQUENCE_WORDS 32
#define KS_MAX_SEQUENCE_CHARS 512

/*
 * Real languages that have their own models beyond a dictionary: spelling
 * correction (noisy-channel tables), typing helpers and the IT vocabulary.
 * Every other language of a language pack is KS_LANG_OTHER here.
 */
typedef enum KS_LANGUAGE {
    KS_LANG_OTHER = 0,
    KS_LANG_ENGLISH = 1,
    KS_LANG_PERSIAN = 2
} KS_LANGUAGE;

/*
 * The engine works on a PAIR of keyboard layouts chosen by the user:
 * slot A (the first language, English by default) and slot B (the second,
 * Persian by default). Every physical key has one reading in each layout.
 */
typedef enum KS_SLOT {
    KS_SLOT_NONE = 0,
    KS_SLOT_A = 1,
    KS_SLOT_B = 2
} KS_SLOT;

#define KS_OTHER_SLOT(slot) ((slot) == KS_SLOT_A ? KS_SLOT_B : KS_SLOT_A)

/* One physical key: the character it types in layout A and in layout B. */
typedef struct KS_TOKEN {
    wchar_t a;
    wchar_t b;
} KS_TOKEN;

typedef struct KS_BLOOM {
    const unsigned char *bits;
    uint32_t bit_count;
    uint32_t bit_mask;
    uint32_t hash_count;
    int valid;
} KS_BLOOM;

/*
 * How the words of one language are looked up. The dictionary generators
 * (tools/generate_blooms.py, tools/build_language_pack.py) apply exactly the
 * same rules before storing a word.
 */
#define KS_PROFILE_CASED          0x0001u  /* lower-case before lookup */
#define KS_PROFILE_ASCII_ONLY     0x0002u  /* only a-z (English) */
#define KS_PROFILE_APOSTROPHE     0x0004u  /* one apostrophe inside a word (don't, l'eau) */
#define KS_PROFILE_ARABIC_MARKS   0x0008u  /* Arabic-script diacritics are ignored */
#define KS_PROFILE_PERSIAN_ALEF   0x0010u  /* ایا is looked up as آیا as well */
#define KS_PROFILE_HEBREW_POINTS  0x0020u  /* niqqud is ignored */
#define KS_PROFILE_LATIN          0x0040u  /* Latin script: home of abbreviations (src, cfg) */
#define KS_PROFILE_NO_SHAPE       0x0080u  /* every reading of the layout is a letter */
#define KS_PROFILE_KNOWN_FLAGS    0x00FFu

#define KS_SHORT_WORDS_MAX 192

typedef struct KS_LANG_PROFILE {
    char code[8];          /* ISO 639-1 code: "en", "fa", "de" ... */
    KS_LANGUAGE model;     /* English / Persian when their special models apply */
    unsigned flags;
    int short_count;
    /* One- and two-letter words in lookup form. The Bloom dictionaries
       hold only words of three letters or more, so this exact list is the
       only evidence for them. */
    wchar_t short_words[KS_SHORT_WORDS_MAX][3];
} KS_LANG_PROFILE;

const KS_LANG_PROFILE *ks_profile_english(void);
const KS_LANG_PROFILE *ks_profile_persian(void);

/*
 * Words known outside the Bloom resources: vocabulary packs compiled into the
 * application (IT and computing terms) and the user's own writing memory.
 * Consulted only for words of three or more letters, after the Blooms. The
 * word arrives in lookup form. Either callback may be NULL.
 */
typedef struct KS_EXTRA_WORDS {
    int (*contains)(const void *context, KS_SLOT slot, const wchar_t *word);
    /* 1 when `prefix` begins (or equals) a known extra word. */
    int (*has_prefix)(const void *context, KS_SLOT slot, const wchar_t *prefix);
    const void *context;
} KS_EXTRA_WORDS;

/* Everything the engine knows about the two languages, indexed by slot
   ([KS_SLOT_A] and [KS_SLOT_B]; index 0 is unused). A NULL profile means
   English for slot A and Persian for slot B. */
typedef struct KS_LEXICONS {
    const KS_LANG_PROFILE *profile[3];
    const KS_BLOOM *words[3];
    const KS_BLOOM *common[3];          /* the 20,000 most frequent words */
    const KS_BLOOM *frequent[3];        /* the 2,000 most frequent words */
    const KS_BLOOM *prefixes[3];        /* proper prefixes of dictionary words */
    const KS_BLOOM *common_prefixes[3]; /* proper prefixes of common words */
    const KS_EXTRA_WORDS *extra;        /* optional */
} KS_LEXICONS;

typedef struct KS_LANGUAGE_CONTEXT {
    KS_SLOT slot;
    int strength;
    unsigned int observed_words;
} KS_LANGUAGE_CONTEXT;

typedef struct KS_SEQUENCE_WORD {
    const KS_TOKEN *tokens;
    int count;
} KS_SEQUENCE_WORD;

typedef struct KS_SEQUENCE_RESULT {
    KS_SLOT slot;            /* the layout the whole run belongs to */
    int confidence;
    int score[3];
    int known_words[3];
} KS_SEQUENCE_RESULT;

typedef struct KS_DECISION {
    int should_correct;
    int key_count;
    int confidence;
    KS_SLOT source_slot;     /* the layout the keys were typed in */
    KS_SLOT target_slot;     /* the layout they were meant for */
    wchar_t original[KS_MAX_WORD + 1];
    wchar_t replacement[KS_MAX_WORD + 1];
} KS_DECISION;

typedef enum KS_LIVE_RESULT {
    KS_LIVE_NONE = 0,
    KS_LIVE_WAIT_FOR_IDLE = 1,
    KS_LIVE_CORRECT_NOW = 2
} KS_LIVE_RESULT;

typedef enum KS_EVALUATION_PHASE {
    KS_PHASE_LIVE = 0,
    KS_PHASE_IDLE = 1,
    KS_PHASE_BOUNDARY = 2
} KS_EVALUATION_PHASE;

/* FNV-1a / djb2 pair over the UTF-8 encoding; shared by every resource. */
void ks_hash_text(const wchar_t *value, uint32_t *first, uint32_t *second);
int ks_bloom_init(KS_BLOOM *bloom, const unsigned char *data, size_t size);
int ks_bloom_contains(const KS_BLOOM *bloom, const wchar_t *value);
wchar_t ks_canonical_persian(wchar_t character);
int ks_is_persian_diacritic(wchar_t character);
/* A letter of an alphabet (Latin, Greek, Cyrillic, Armenian, Hebrew,
   Arabic, Georgian, Thai, Devanagari), not punctuation or a digit. */
int ks_is_letter(wchar_t character);
wchar_t ks_to_lower(wchar_t character);
int ks_is_word_scancode(uint32_t scan_code);
/* The built-in English/Persian key table, used when a layout is missing
   and by the tests: a = English, b = Persian. */
int ks_map_scancode(uint32_t scan_code, int shift_down, int caps_lock, KS_TOKEN *token);
void ks_tokens_to_slot(const KS_TOKEN *tokens, int count, KS_SLOT slot, wchar_t *output);
void ks_tokens_to_a(const KS_TOKEN *tokens, int count, wchar_t *output);
void ks_tokens_to_b(const KS_TOKEN *tokens, int count, wchar_t *output);
/* The dictionary form of a reading (lower case, marks removed). Returns its
   length, or 0 when the reading cannot be a word of the language. */
int ks_lookup_form(const KS_LANG_PROFILE *profile, const wchar_t *reading, wchar_t *output);
int ks_profile_short_word(const KS_LANG_PROFILE *profile, const wchar_t *form);
/* Text-level membership with the same rules as token membership. */
int ks_text_known_in(const KS_LANG_PROFILE *profile, const wchar_t *text,
                     const KS_BLOOM *words, const KS_BLOOM *common);
/* English / Persian shortcut used by the spelling model. */
int ks_text_known(const wchar_t *text, KS_LANGUAGE language,
                  const KS_BLOOM *words, const KS_BLOOM *common);
/* English (a) / Persian (b) membership without frequency tiers. */
int ks_word_membership(const KS_TOKEN *tokens, int count,
                       const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                       const KS_BLOOM *a_common, const KS_BLOOM *b_common,
                       int *a_known, int *b_known);
/* known[slot] and frequent[slot] for both readings of the keys. */
int ks_classify_word(const KS_TOKEN *tokens, int count,
                     const KS_LEXICONS *lexicons,
                     int known[3], int frequent[3]);
int ks_collision_prior_points(const wchar_t *english_word);
int ks_evaluate_sequence(const KS_SEQUENCE_WORD *words, int word_count,
                         int sensitivity,
                         KS_SLOT context_slot, int context_strength,
                         const KS_LEXICONS *lexicons,
                         KS_SEQUENCE_RESULT *result);
void ks_context_reset(KS_LANGUAGE_CONTEXT *context);
void ks_context_observe(KS_LANGUAGE_CONTEXT *context, KS_SLOT slot, int evidence);
KS_SLOT ks_context_current(const KS_LANGUAGE_CONTEXT *context, int *strength);
KS_LIVE_RESULT ks_evaluate_contextual(
                                 const KS_TOKEN *tokens, int count,
                                 KS_SLOT active_slot, int sensitivity,
                                 KS_SLOT context_slot, int context_strength,
                                 int sentence_start,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_LEXICONS *lexicons,
                                 KS_DECISION *decision);
/* English (a) / Persian (b) conveniences kept for the tests. */
KS_LIVE_RESULT ks_evaluate_smart(const KS_TOKEN *tokens, int count,
                                 KS_SLOT active_slot, int sensitivity,
                                 KS_SLOT context_slot, int context_strength,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                                 const KS_BLOOM *a_prefixes,
                                 const KS_BLOOM *b_prefixes,
                                 KS_DECISION *decision);
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
                                 KS_DECISION *decision);
int ks_evaluate(const KS_TOKEN *tokens, int count, KS_SLOT active_slot,
                int minimum_length, const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                KS_DECISION *decision);
KS_LIVE_RESULT ks_evaluate_live(const KS_TOKEN *tokens, int count,
                                KS_SLOT active_slot, int minimum_length,
                                const KS_BLOOM *a_words, const KS_BLOOM *b_words,
                                const KS_BLOOM *a_prefixes,
                                const KS_BLOOM *b_prefixes,
                                KS_DECISION *decision);
uint32_t ks_update_key_interval_ms(uint32_t current_average_ms,
                                   uint32_t observed_interval_ms);
uint32_t ks_idle_delay_ms(int sensitivity, uint32_t average_key_interval_ms);

/* ---- Language packs (.kslang) ------------------------------------------- */

#define KS_PACK_MAX_LANGIDS 4

typedef struct KS_LANG_PACK {
    KS_LANG_PROFILE profile;
    wchar_t english_name[48];
    wchar_t native_name[48];
    unsigned short langids[KS_PACK_MAX_LANGIDS];   /* Windows primary language ids */
    int langid_count;
    KS_BLOOM words;
    KS_BLOOM common;
    KS_BLOOM frequent;
    KS_BLOOM prefixes;
    KS_BLOOM common_prefixes;
} KS_LANG_PACK;

/* Validates a pack file held in memory and points the Blooms into it (the
   data must outlive the pack). Returns 0 for anything malformed. */
int ks_pack_parse(const unsigned char *data, size_t size, KS_LANG_PACK *pack);
/* Only the fixed header (code, flags, language ids, names): enough to list
   a pack without reading its dictionaries. Needs KS_PACK_HEADER_SIZE bytes. */
#define KS_PACK_HEADER_SIZE 132
int ks_pack_parse_header(const unsigned char *data, size_t size, KS_LANG_PACK *pack);

#ifdef __cplusplus
}
#endif

#endif
