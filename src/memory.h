#ifndef KEYSWITCHFIX_MEMORY_H
#define KEYSWITCHFIX_MEMORY_H

/*
 * Writing memory: what this user writes, learned while they type.
 *
 *  - Words: how often each word is typed and left as it is. Frequent words
 *    become known words (a name, jargon, the user's own spellings) and lift
 *    their rank in spelling suggestions, so ambiguous typos resolve toward
 *    the words this person actually uses.
 *  - Fixes: a typo the user repairs by hand ("عسیسم" deleted and retyped as
 *    "عزیزم"). After the same repair has been seen twice (three times when
 *    the typo is itself a dictionary word) it is applied automatically at the
 *    end of the word; one Backspace undoes it and makes it unlearn.
 *
 *  - Pairs (4.2): two words typed one after the other. They tell spelling
 *    which repair fits the user's own phrases, colloquial ones included,
 *    that a corpus of written Persian does not know.
 *
 * Pure C, no allocation, bounded: 4,096 words, 512 fixes, 2,048 pairs.
 * Persisting it is the caller's business (text serialisation below).
 */

#include <stddef.h>
#include <wchar.h>

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KS_MEMORY_WORDS 4096
#define KS_MEMORY_SLOTS 8192          /* hash slots, power of two */
#define KS_MEMORY_FIXES 512
#define KS_MEMORY_KNOWN_COUNT 3       /* a word typed this often is known */
/* ...and from this many uses it is the user's own: spelling never touches
   it, and one hand edit does not make the memory forget it. */
#define KS_MEMORY_OWN_WORD_COUNT 8
#define KS_MEMORY_PAIRS 2048
#define KS_MEMORY_PAIR_SLOTS 4096     /* hash slots, power of two */
#define KS_MEMORY_PAIR_GRACE 512      /* a new pair is safe this many sightings */
#define KS_MEMORY_FIX_COUNT 2         /* repairs needed before auto-applying */
#define KS_MEMORY_FIX_COUNT_KNOWN 3   /* ... when the typo is a real word */

typedef struct KS_MEMORY_WORD {
    wchar_t text[KS_MAX_WORD + 1];
    unsigned count;
    unsigned last_seen;   /* memory clock at the last sighting */
} KS_MEMORY_WORD;

typedef struct KS_MEMORY_FIX {
    wchar_t typo[KS_MAX_WORD + 1];
    wchar_t fix[KS_MAX_WORD + 1];
    int count;
} KS_MEMORY_FIX;

typedef struct KS_MEMORY_PAIR {
    wchar_t text[2 * KS_MAX_WORD + 2];   /* "first second" */
    unsigned count;
    unsigned last_seen;
} KS_MEMORY_PAIR;

typedef struct KS_WRITING_MEMORY {
    KS_MEMORY_WORD words[KS_MEMORY_WORDS];
    int word_count;
    unsigned short slots[KS_MEMORY_SLOTS];   /* word index + 1; 0 = empty */
    KS_MEMORY_FIX fixes[KS_MEMORY_FIXES];
    int fix_count;
    int fix_cursor;   /* where the next eviction starts looking */
    KS_MEMORY_PAIR pairs[KS_MEMORY_PAIRS];
    int pair_count;
    unsigned short pair_slots[KS_MEMORY_PAIR_SLOTS];   /* pair index + 1 */
    unsigned clock;                          /* sightings so far, for recency */
    unsigned aged_at;                        /* clock at the last eviction */
    int dirty;                               /* changed since last save */
} KS_WRITING_MEMORY;

void ks_memory_reset(KS_WRITING_MEMORY *memory);

unsigned ks_memory_word_count(const KS_WRITING_MEMORY *memory, const wchar_t *word);
/* Records one use of a word (2..32 characters, no spaces). */
void ks_memory_observe_word(KS_WRITING_MEMORY *memory, const wchar_t *word);
/* Takes one use back (the word was undone or reopened for editing). */
void ks_memory_unobserve_word(KS_WRITING_MEMORY *memory, const wchar_t *word);
/* The user repaired this word by hand: it was a typo, not one of their
   words. Its count drops to zero (it is no longer "known"). */
void ks_memory_forget_word(KS_WRITING_MEMORY *memory, const wchar_t *word);
/*
 * Spelling rank for a word given its dictionary rank (-1 = not in the
 * dictionary): a word the user types often ranks by how often (from
 * zipf 3.0 up), and a dictionary word the user favours is lifted by up to
 * 1.2 zipf, so ambiguous typos resolve toward this user's words.
 */
int ks_memory_rank_adjust(const KS_WRITING_MEMORY *memory, const wchar_t *word, int table_rank);

/* Records that `second` was typed right after `first` (both 2..32
   characters, no spaces). */
void ks_memory_observe_pair(KS_WRITING_MEMORY *memory, const wchar_t *first, const wchar_t *second);
unsigned ks_memory_pair_count(const KS_WRITING_MEMORY *memory, const wchar_t *first, const wchar_t *second);
/* Takes one sighting back (the fix that made the phrase was undone). */
void ks_memory_unobserve_pair(KS_WRITING_MEMORY *memory, const wchar_t *first, const wchar_t *second);
/* The pair's weight for spelling, on the scale of the corpus pair table
   (10 x PMI): 0 below two sightings, then 20, rising with use to 40. */
int ks_memory_pair_score(const KS_WRITING_MEMORY *memory, const wchar_t *first, const wchar_t *second);

/* Optimal-string-alignment distance, capped at `limit` + 1. */
int ks_memory_distance(const wchar_t *a, const wchar_t *b, int limit);
/* 1 when (typo -> fix) looks like a repair of a finished word rather than
   an unfinished word or a change of mind. */
int ks_memory_is_fix_pair(const wchar_t *typo, const wchar_t *fix);
/* As above, for a "typo" captured while the word was still being typed
   (the user deleted a letter or two mid-word): only a typo at least as long
   as the result is a repair; "help" on the way to "hello" is not. */
int ks_memory_is_fix_pair_midword(const wchar_t *typo, const wchar_t *fix);
/* Times the repair for this typo has been seen (0 when none). */
int ks_memory_fix_count(const KS_WRITING_MEMORY *memory, const wchar_t *typo);
/* Records a hand repair; returns how often it has now been seen (0 when the
   pair is not a plausible repair). */
int ks_memory_observe_fix(KS_WRITING_MEMORY *memory, const wchar_t *typo, const wchar_t *fix);
/* The learned repair for a typo, or NULL when it is not learned yet. */
const wchar_t *ks_memory_lookup_fix(const KS_WRITING_MEMORY *memory, const wchar_t *typo,
                                    int typo_is_known_word);
/* The user undid an automatic repair: start learning it from zero. */
void ks_memory_reject_fix(KS_WRITING_MEMORY *memory, const wchar_t *typo);
int ks_memory_active_fix_count(const KS_WRITING_MEMORY *memory);

/*
 * Text form (UTF-16 in memory; the caller writes it as UTF-8):
 *   [fixes]  typo<TAB>correction<TAB>count
 *   [words]  word<TAB>count          (most frequent first)
 *   [pairs]  first second<TAB>count  (typed at least twice)
 * Lines starting with '#' are comments. Returns the number of characters
 * written (excluding the terminator); with output == NULL, the size needed.
 */
size_t ks_memory_serialize(const KS_WRITING_MEMORY *memory, wchar_t *output, size_t capacity);
/* Replaces the memory with the parsed text; returns 1 on success. */
int ks_memory_parse(KS_WRITING_MEMORY *memory, const wchar_t *text);

#ifdef __cplusplus
}
#endif

#endif
