#ifndef KEYSWITCHFIX_SPELL_H
#define KEYSWITCHFIX_SPELL_H

/*
 * Offline spelling correction for Persian and English.
 *
 * Model: a noisy channel. Every candidate within Damerau-Levenshtein distance
 * one of the typed word is scored as
 *
 *     score = 10 * zipf(candidate) + error_model(typed -> candidate)
 *
 * where zipf comes from a compact exact frequency table (KSRT resource,
 * derived from wordfreq) and the error model rewards the mistakes people
 * actually make: adjacent-letter transposition, a neighbouring key on the
 * physical keyboard, a doubled or dropped repeated letter, and the Persian
 * homophone confusions (ح/ه, ت/ط, س/ص/ث, ز/ذ/ض/ظ, ق/غ, ا/آ/ع). The best
 * candidate must clear a frequency floor and beat the runner-up by a margin
 * that depends on the chosen level. Everything runs in microseconds inside
 * the keyboard hook: a few hundred hash probes, no allocation.
 */

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KS_SPELL_OFF 0
#define KS_SPELL_CONSERVATIVE 1
#define KS_SPELL_BALANCED 2
#define KS_SPELL_AGGRESSIVE 3

#define KS_IGNORE_LIST_CAPACITY 64
#define KS_VOCAB_CAPACITY 1024
/* A word the user has typed this many times is treated as intentional. */
#define KS_VOCAB_TRUST_COUNT 2

/*
 * KSRT rank table: "KSRT", u32 version (1), u32 slot_count (power of two),
 * u32 word_count, then slot_count * 4 bytes. Each slot holds a 24-bit
 * fingerprint and one byte of rank (round(zipf * 10)); an empty slot is all
 * zero. Open addressing with linear probing.
 */
typedef struct KS_RANK_TABLE {
    const unsigned char *slots;
    uint32_t slot_count;
    uint32_t slot_mask;
    uint32_t word_count;
    int valid;
} KS_RANK_TABLE;

/*
 * Words the user has shown to be intentional: typed repeatedly this session,
 * restored with Backspace, or loaded from the optional personal dictionary.
 * A bounded ring; the oldest entry is recycled when it is full.
 */
typedef struct KS_VOCAB_ENTRY {
    wchar_t text[KS_MAX_WORD + 1];
    int count;
} KS_VOCAB_ENTRY;

typedef struct KS_VOCAB {
    KS_VOCAB_ENTRY entries[KS_VOCAB_CAPACITY];
    int count;
    int next;
} KS_VOCAB;

typedef struct KS_SPELL_LEXICON {
    KS_LANGUAGE language;
    const KS_RANK_TABLE *ranks;
    const KS_BLOOM *words;
    const KS_BLOOM *common;
    const KS_VOCAB *vocabulary;   /* optional: words seen this session */
    const KS_VOCAB *personal;     /* optional: the user's saved dictionary */
} KS_SPELL_LEXICON;

typedef enum KS_SPELL_KIND {
    KS_SPELL_KIND_EDIT = 0,       /* one-letter repair */
    KS_SPELL_KIND_ZWNJ = 1,       /* Persian prefix/suffix joined with a ZWNJ */
    KS_SPELL_KIND_SPLIT = 2       /* two words missing the space between them */
} KS_SPELL_KIND;

typedef struct KS_SPELL_RESULT {
    int should_correct;
    int confidence;          /* winning margin, capped at 100 */
    int candidate_count;     /* distinct real-word candidates examined */
    int best_rank;           /* zipf * 10 of the winner */
    KS_SPELL_KIND kind;
    wchar_t original[KS_MAX_WORD + 1];
    wchar_t replacement[KS_MAX_WORD + 2];
} KS_SPELL_RESULT;

typedef struct KS_IGNORE_LIST {
    wchar_t words[KS_IGNORE_LIST_CAPACITY][KS_MAX_WORD + 1];
    int count;
    int next;
} KS_IGNORE_LIST;

void ks_rank_hash(const wchar_t *word, uint32_t *fingerprint, uint32_t *index_hash);
int ks_rank_table_init(KS_RANK_TABLE *table, const unsigned char *data, size_t size);
/* Returns zipf * 10 (0..255) or -1 when the word is not in the table. */
int ks_rank_lookup(const KS_RANK_TABLE *table, const wchar_t *word);

/* 1 when two characters sit on adjacent physical keys (same key map for
   both languages, since Persian letters live on the same QWERTY keys). */
int ks_keys_adjacent(wchar_t first, wchar_t second);
/* 1 when two Persian letters are commonly confused for each other. */
int ks_persian_confusable(wchar_t first, wchar_t second);

/* 1 when the exact typed text is an accepted word for the language. */
int ks_spell_known(const wchar_t *word, const KS_SPELL_LEXICON *lexicon);

int ks_spell_correct(const wchar_t *word, int level,
                     const KS_SPELL_LEXICON *lexicon,
                     const KS_IGNORE_LIST *ignore,
                     KS_SPELL_RESULT *result);

void ks_vocab_reset(KS_VOCAB *vocab);
/* Records one sighting; returns the new count. */
int ks_vocab_observe(KS_VOCAB *vocab, const wchar_t *word);
/* Marks a word as trusted immediately (undo, personal dictionary). */
void ks_vocab_trust(KS_VOCAB *vocab, const wchar_t *word);
int ks_vocab_trusted(const KS_VOCAB *vocab, const wchar_t *word);

void ks_ignore_list_reset(KS_IGNORE_LIST *list);
void ks_ignore_list_add(KS_IGNORE_LIST *list, const wchar_t *word);
int ks_ignore_list_contains(const KS_IGNORE_LIST *list, const wchar_t *word);

#ifdef __cplusplus
}
#endif

#endif
