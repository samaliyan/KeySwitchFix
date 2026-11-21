#include "spell.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Rank table                                                                */
/* ------------------------------------------------------------------------ */

#define KS_RANK_MAX_PROBES 128u

static uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ks_rank_hash(const wchar_t *word, uint32_t *fingerprint, uint32_t *index_hash) {
    uint32_t first;
    uint32_t second;
    ks_hash_text(word, &first, &second);
    /* 24-bit fingerprint; zero is reserved for an empty slot. */
    *fingerprint = first & 0xFFFFFFu;
    if (*fingerprint == 0) *fingerprint = 1;
    *index_hash = (second * 0x9E3779B9u) ^ (first >> 7);
}

int ks_rank_table_init(KS_RANK_TABLE *table, const unsigned char *data, size_t size) {
    uint32_t version;
    uint32_t slot_count;
    uint32_t word_count;

    if (!table) return 0;
    memset(table, 0, sizeof(*table));
    if (!data || size < 16 || memcmp(data, "KSRT", 4) != 0) return 0;
    version = read_u32_le(data + 4);
    slot_count = read_u32_le(data + 8);
    word_count = read_u32_le(data + 12);
    if (version != 1 || slot_count < 256 || slot_count > (1u << 24) ||
        (slot_count & (slot_count - 1)) != 0 ||
        word_count == 0 || word_count > slot_count) return 0;
    if (size != 16u + (size_t)slot_count * 4u) return 0;
    table->slots = data + 16;
    table->slot_count = slot_count;
    table->slot_mask = slot_count - 1;
    table->word_count = word_count;
    table->valid = 1;
    return 1;
}

int ks_rank_lookup(const KS_RANK_TABLE *table, const wchar_t *word) {
    uint32_t fingerprint;
    uint32_t index;
    uint32_t probe;

    if (!table || !table->valid || !word || !*word) return -1;
    ks_rank_hash(word, &fingerprint, &index);
    index &= table->slot_mask;
    for (probe = 0; probe < KS_RANK_MAX_PROBES; ++probe) {
        uint32_t value = read_u32_le(table->slots + (size_t)index * 4u);
        if (value == 0) return -1;
        if ((value & 0xFFFFFFu) == fingerprint) return (int)(value >> 24);
        index = (index + 1) & table->slot_mask;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Keyboard geometry and confusion sets                                      */
/* ------------------------------------------------------------------------ */

static const wchar_t *const KEY_ROWS[3] = {
    L"qwertyuiop[]",
    L"asdfghjkl;'",
    L"zxcvbnm,./"
};

/* Persian letters on the legacy Windows Persian layout, mapped to the
   English key at the same physical position. */
static const wchar_t PERSIAN_KEYS[][2] = {
    {L'ش', L'a'}, {L'ذ', L'b'}, {L'ز', L'c'}, {L'ژ', L'c'}, {L'ی', L'd'},
    {L'ث', L'e'}, {L'ب', L'f'}, {L'ل', L'g'}, {L'ا', L'h'}, {L'آ', L'h'},
    {L'ه', L'i'}, {L'ت', L'j'}, {L'ن', L'k'}, {L'م', L'l'}, {L'پ', L'm'},
    {L'د', L'n'}, {L'خ', L'o'}, {L'ح', L'p'}, {L'ض', L'q'}, {L'ق', L'r'},
    {L'س', L's'}, {L'ف', L't'}, {L'ع', L'u'}, {L'ر', L'v'}, {L'ص', L'w'},
    {L'ط', L'x'}, {L'غ', L'y'}, {L'ظ', L'z'}, {L'ج', L'['}, {L'چ', L']'},
    {L'ک', L';'}, {L'گ', L'\''}, {L'و', L','},
    /* Shift variants on the legacy layout share the key of the base letter. */
    {L'ئ', L's'}, {L'ؤ', L'a'}, {L'أ', L'g'}, {L'إ', L'f'}, {L'ء', L'm'}
};

static wchar_t physical_key(wchar_t character) {
    size_t i;
    if (character >= L'A' && character <= L'Z') return character - L'A' + L'a';
    if (character < 0x80) return character;
    for (i = 0; i < sizeof(PERSIAN_KEYS) / sizeof(PERSIAN_KEYS[0]); ++i) {
        if (PERSIAN_KEYS[i][0] == character) return PERSIAN_KEYS[i][1];
    }
    return 0;
}

static int key_position(wchar_t key, int *row, int *column) {
    int r;
    for (r = 0; r < 3; ++r) {
        const wchar_t *found = wcschr(KEY_ROWS[r], key);
        if (found && key) {
            *row = r;
            *column = (int)(found - KEY_ROWS[r]);
            return 1;
        }
    }
    return 0;
}

int ks_keys_adjacent(wchar_t first, wchar_t second) {
    int row_a, column_a, row_b, column_b;
    wchar_t key_a = physical_key(first);
    wchar_t key_b = physical_key(second);
    if (!key_a || !key_b || key_a == key_b) return 0;
    if (!key_position(key_a, &row_a, &column_a) ||
        !key_position(key_b, &row_b, &column_b)) return 0;
    if (row_a == row_b) return column_a - column_b == 1 || column_b - column_a == 1;
    /* Each lower row is offset half a key to the right, so a key touches the
       key straight below it and the one below-left. */
    if (row_a + 1 == row_b) return column_b == column_a || column_b == column_a - 1;
    if (row_b + 1 == row_a) return column_a == column_b || column_a == column_b - 1;
    return 0;
}

/* Letters confused by sound, or by shape (ک/گ: خوشکل for خوشگل). */
static const wchar_t *const CONFUSION_SETS[] = {
    L"حه", L"تط", L"سصث", L"زذضظ", L"قغ", L"اآأإع", L"یئ", L"وؤ", L"کگ"
};

int ks_persian_confusable(wchar_t first, wchar_t second) {
    size_t i;
    if (first == second) return 0;
    for (i = 0; i < sizeof(CONFUSION_SETS) / sizeof(CONFUSION_SETS[0]); ++i) {
        if (wcschr(CONFUSION_SETS[i], first) && wcschr(CONFUSION_SETS[i], second))
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Ignore list                                                               */
/* ------------------------------------------------------------------------ */

void ks_ignore_list_reset(KS_IGNORE_LIST *list) {
    if (list) memset(list, 0, sizeof(*list));
}

void ks_ignore_list_add(KS_IGNORE_LIST *list, const wchar_t *word) {
    if (!list || !word || !*word || wcslen(word) > KS_MAX_WORD) return;
    if (ks_ignore_list_contains(list, word)) return;
    wcscpy(list->words[list->next], word);
    list->next = (list->next + 1) % KS_IGNORE_LIST_CAPACITY;
    if (list->count < KS_IGNORE_LIST_CAPACITY) ++list->count;
}

int ks_ignore_list_contains(const KS_IGNORE_LIST *list, const wchar_t *word) {
    int i;
    if (!list || !word) return 0;
    for (i = 0; i < list->count; ++i) {
        if (wcscmp(list->words[i], word) == 0) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Learned vocabulary                                                        */
/* ------------------------------------------------------------------------ */

static KS_VOCAB_ENTRY *vocab_find(KS_VOCAB *vocab, const wchar_t *word) {
    int i;
    for (i = 0; i < vocab->count; ++i) {
        if (wcscmp(vocab->entries[i].text, word) == 0) return &vocab->entries[i];
    }
    return NULL;
}

void ks_vocab_reset(KS_VOCAB *vocab) {
    if (vocab) memset(vocab, 0, sizeof(*vocab));
}

int ks_vocab_observe(KS_VOCAB *vocab, const wchar_t *word) {
    KS_VOCAB_ENTRY *entry;
    if (!vocab || !word || !*word || wcslen(word) > KS_MAX_WORD) return 0;
    entry = vocab_find(vocab, word);
    if (entry) {
        if (entry->count < 1000) ++entry->count;
        return entry->count;
    }
    entry = &vocab->entries[vocab->next];
    wcscpy(entry->text, word);
    entry->count = 1;
    vocab->next = (vocab->next + 1) % KS_VOCAB_CAPACITY;
    if (vocab->count < KS_VOCAB_CAPACITY) ++vocab->count;
    return 1;
}

void ks_vocab_trust(KS_VOCAB *vocab, const wchar_t *word) {
    KS_VOCAB_ENTRY *entry;
    if (!vocab || !word || !*word || wcslen(word) > KS_MAX_WORD) return;
    ks_vocab_observe(vocab, word);
    entry = vocab_find(vocab, word);
    if (entry && entry->count < KS_VOCAB_TRUST_COUNT) entry->count = KS_VOCAB_TRUST_COUNT;
}

int ks_vocab_trusted(const KS_VOCAB *vocab, const wchar_t *word) {
    int i;
    if (!vocab || !word) return 0;
    for (i = 0; i < vocab->count; ++i) {
        if (vocab->entries[i].count >= KS_VOCAB_TRUST_COUNT &&
            wcscmp(vocab->entries[i].text, word) == 0) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Correction                                                                */
/* ------------------------------------------------------------------------ */

static const wchar_t ENGLISH_ALPHABET[] = L"abcdefghijklmnopqrstuvwxyz";
static const wchar_t PERSIAN_ALPHABET[] =
    L"ابپتثجچحخدذرزژسشصضطظعغفقکگلمنوهیآئؤأإء";

/*
 * Error-model weights on the zipf * 10 scale of the ranks: a bonus of 10
 * means "this slip is ten times likelier than an arbitrary one". Measured on
 * typos generated from the 4,000 most frequent words of each language
 * (tests/spell_eval, see docs/SPELLING.md); every value can be overridden at
 * compile time for that evaluation.
 */
#ifndef KS_BONUS_TRANSPOSE
#define KS_BONUS_TRANSPOSE 24
#endif
#ifndef KS_BONUS_HOMOPHONE
#define KS_BONUS_HOMOPHONE 30
#endif
#ifndef KS_BONUS_ADJACENT
#define KS_BONUS_ADJACENT 24
#endif
#ifndef KS_BONUS_OTHER_LETTER
#define KS_BONUS_OTHER_LETTER 6
#endif
#ifndef KS_BONUS_EXTRA_DOUBLED
#define KS_BONUS_EXTRA_DOUBLED 22
#endif
#ifndef KS_BONUS_EXTRA_ADJACENT
#define KS_BONUS_EXTRA_ADJACENT 18
#endif
#ifndef KS_BONUS_EXTRA_OTHER
#define KS_BONUS_EXTRA_OTHER 8
#endif
#ifndef KS_BONUS_MISSING_DOUBLED
#define KS_BONUS_MISSING_DOUBLED 24
#endif
#ifndef KS_BONUS_MISSING_VOWEL
#define KS_BONUS_MISSING_VOWEL 14
#endif
#ifndef KS_BONUS_MISSING_OTHER
#define KS_BONUS_MISSING_OTHER 12
#endif
/* Two slips are rarer than one. */
#ifndef KS_TWO_SLIP_PENALTY
#define KS_TWO_SLIP_PENALTY 28
#endif
/* A two-slip repair must reach this rank (zipf * 10) at Balanced... */
#ifndef KS_TWO_SLIP_MIN_RANK
#define KS_TWO_SLIP_MIN_RANK 40
#endif
/* ...beat the runner-up by this much more than a one-slip repair must... */
#ifndef KS_TWO_SLIP_EXTRA_MARGIN
#define KS_TWO_SLIP_EXTRA_MARGIN 6
#endif
#ifndef KS_EDGE_SLIP
#define KS_EDGE_SLIP 1
#endif
#ifndef KS_BALANCED_MARGIN
#define KS_BALANCED_MARGIN 12
#endif
/* ...and the typed word must have at least this many letters, and at most
   KS_TWO_SLIP_MAX_LENGTH: the search grows with the square of the length
   (up to about 5 ms at 12 letters), and a long word with two slips is rare. */
#ifndef KS_TWO_SLIP_MIN_LENGTH
#define KS_TWO_SLIP_MIN_LENGTH 4
#endif
#ifndef KS_TWO_SLIP_INSERT_ANY
#define KS_TWO_SLIP_INSERT_ANY 0
#endif
#ifndef KS_TWO_SLIP_MAX_LENGTH
#define KS_TWO_SLIP_MAX_LENGTH 12
#endif
/* A weakly known word yields only to a word this frequent... */
#ifndef KS_REAL_WORD_MIN_RANK
#define KS_REAL_WORD_MIN_RANK 45
#endif
/* ...whose score beats leaving the word as typed (worth this much). */
#ifndef KS_REAL_WORD_KEEP
#define KS_REAL_WORD_KEEP 60
#endif
/* Context (4.2): a candidate gains this many tenths of the pair score with
   the word before it and with the word after it. */
#ifndef KS_CONTEXT_WEIGHT
#define KS_CONTEXT_WEIGHT 10
#endif
/* ...at most this much in all. */
#ifndef KS_CONTEXT_CAP
#define KS_CONTEXT_CAP 40
#endif

/* The writing memory ranks a word it knows (seen three times) at 30 plus a
   lift of 2 per doubling of its count: 32 at three uses, 34 at four to
   seven, 36 from eight. Up to this rank (fewer than eight uses) it is weak. */
#define KS_MEMORY_WEAK_RANK 34

typedef struct KS_LEVEL_POLICY {
    int minimum_rank;      /* zipf * 10 the winner must reach */
    int margin;            /* winner must beat the runner-up by this */
    int minimum_length;    /* shorter typed words are never touched */
    int short_word_bonus;  /* 3-letter words need this much error-model support */
} KS_LEVEL_POLICY;

static KS_LEVEL_POLICY level_policy(int level) {
    KS_LEVEL_POLICY policy;
    if (level >= KS_SPELL_AGGRESSIVE) {
        policy.minimum_rank = 22;
        policy.margin = 6;
        policy.minimum_length = 3;
        policy.short_word_bonus = 0;
    } else if (level <= KS_SPELL_CONSERVATIVE) {
        policy.minimum_rank = 40;
        policy.margin = 20;
        policy.minimum_length = 4;
        policy.short_word_bonus = 18;
    } else {
        policy.minimum_rank = 30;
        policy.margin = KS_BALANCED_MARGIN;
        policy.minimum_length = 3;
        policy.short_word_bonus = 18;
    }
    return policy;
}

typedef struct KS_CANDIDATE {
    wchar_t text[KS_MAX_WORD + 2];
    int rank;
    int bonus;
    int score;
    int valid;
    int slips;
    KS_SPELL_KIND kind;
} KS_CANDIDATE;

typedef struct KS_SEARCH {
    const KS_SPELL_LEXICON *lexicon;
    const wchar_t *typed;
    int typed_length;
    int level;
    int zwnj_only;
    KS_CANDIDATE best;
    KS_CANDIDATE second;
    int distinct;
    /* Two-slip search: each slip from a short list of likely ones. */
    int slips;               /* slips in the candidate being scored (1 or 2) */
    int bonus_floor;         /* real-word search: only slips worth this much */
    const wchar_t *alphabet;
    /* The words around the typed one (NULL: none). */
    const wchar_t *before;
    const wchar_t *after;
    /* Keyboard neighbours and homophones of each alphabet letter, found
       once per call. */
    wchar_t near[48][12];
} KS_SEARCH;

int ks_spell_rank(const KS_SPELL_LEXICON *lexicon, const wchar_t *word) {
    int rank;
    if (!lexicon || !word) return -1;
    rank = ks_rank_lookup(lexicon->ranks, word);
    if (lexicon->rank_adjust) rank = lexicon->rank_adjust(lexicon->rank_adjust_context, word, rank);
    if (rank > 255) rank = 255;
    return rank;
}

int ks_spell_pair_score(const KS_SPELL_LEXICON *lexicon, const wchar_t *first, const wchar_t *second) {
    wchar_t key[2 * KS_MAX_WORD + 4];   /* two words of up to 33, a space, the end */
    size_t a, b;
    int score;
    if (!lexicon || !first || !second) return 0;
    a = wcslen(first);
    b = wcslen(second);
    if (!a || !b || a > KS_MAX_WORD + 1 || b > KS_MAX_WORD + 1) return 0;
    score = 0;
    if (lexicon->pairs && lexicon->pairs->valid) {
        memcpy(key, first, a * sizeof(wchar_t));
        key[a] = L' ';
        memcpy(key + a + 1, second, (b + 1) * sizeof(wchar_t));
        score = ks_rank_lookup(lexicon->pairs, key);
        if (score < 0) score = 0;
    }
    if (lexicon->pair_adjust) score = lexicon->pair_adjust(lexicon->pair_adjust_context, first, second, score);
    return score > 0 ? score : 0;
}

/* How well a candidate fits the words around it. A split candidate ("در
   خانه") meets the word before with its first half, the word after with
   its second. */
static int context_bonus(const KS_SEARCH *search, const wchar_t *text, KS_SPELL_KIND kind) {
    wchar_t left[KS_MAX_WORD + 2];
    const wchar_t *right = text;
    int total = 0;
    if ((!search->lexicon->pairs && !search->lexicon->pair_adjust) || (!search->before && !search->after)) return 0;
    if (kind == KS_SPELL_KIND_SPLIT) {
        const wchar_t *space = wcschr(text, L' ');
        size_t n = space ? (size_t)(space - text) : wcslen(text);
        if (n > KS_MAX_WORD) return 0;
        memcpy(left, text, n * sizeof(wchar_t));
        left[n] = 0;
        if (space) right = space + 1;
    } else {
        size_t n = wcslen(text);
        if (n > KS_MAX_WORD + 1) return 0;
        memcpy(left, text, (n + 1) * sizeof(wchar_t));
    }
    if (search->before) total += ks_spell_pair_score(search->lexicon, search->before, left);
    if (search->after) total += ks_spell_pair_score(search->lexicon, right, search->after);
    total = total * KS_CONTEXT_WEIGHT / 10;
    return total > KS_CONTEXT_CAP ? KS_CONTEXT_CAP : total;
}

static void consider_ranked(KS_SEARCH *search, const wchar_t *text, int rank,
                            int bonus, KS_SPELL_KIND kind) {
    int score;

    if (rank < 0 || wcscmp(text, search->typed) == 0) return;
    if (kind != KS_SPELL_KIND_ZWNJ) bonus += context_bonus(search, text, kind);
    score = rank + bonus;

    if (search->best.valid && wcscmp(search->best.text, text) == 0) {
        if (score > search->best.score) {
            search->best.score = score;
            search->best.bonus = bonus;
            search->best.kind = kind;
            search->best.slips = search->slips;
        }
        return;
    }
    if (search->second.valid && wcscmp(search->second.text, text) == 0) {
        if (score > search->second.score) {
            search->second.score = score;
            search->second.bonus = bonus;
            search->second.kind = kind;
            search->second.slips = search->slips;
        }
        if (search->second.score > search->best.score) {
            KS_CANDIDATE swap = search->best;
            search->best = search->second;
            search->second = swap;
        }
        return;
    }
    ++search->distinct;
    if (!search->best.valid || score > search->best.score) {
        search->second = search->best;
        wcscpy(search->best.text, text);
        search->best.rank = rank;
        search->best.bonus = bonus;
        search->best.score = score;
        search->best.kind = kind;
        search->best.slips = search->slips;
        search->best.valid = 1;
        return;
    }
    if (!search->second.valid || score > search->second.score) {
        wcscpy(search->second.text, text);
        search->second.rank = rank;
        search->second.bonus = bonus;
        search->second.score = score;
        search->second.kind = kind;
        search->second.slips = search->slips;
        search->second.valid = 1;
    }
}

static void consider(KS_SEARCH *search, const wchar_t *text, int bonus) {
    if (bonus < search->bonus_floor) return;
    consider_ranked(search, text, ks_spell_rank(search->lexicon, text),
                    bonus, KS_SPELL_KIND_EDIT);
}

/*
 * Persian prefixes and suffixes that are written with a zero-width
 * non-joiner. میخواهم is the most common Persian "spelling mistake" of all:
 * the word is right, only the ZWNJ is missing.
 */
static const wchar_t *const ZWNJ_PREFIXES[] = { L"می", L"نمی" };
static const wchar_t *const ZWNJ_SUFFIXES[] = {
    L"ها", L"های", L"هایی", L"تر", L"ترین", L"ام", L"ات", L"اش",
    L"مان", L"تان", L"شان"
};

static int known_rank(const KS_SEARCH *search, const wchar_t *text) {
    return ks_spell_rank(search->lexicon, text);
}

static void enumerate_joins(KS_SEARCH *search) {
    const wchar_t *typed = search->typed;
    int length = search->typed_length;
    KS_LANGUAGE language = search->lexicon->language;
    wchar_t left[KS_MAX_WORD + 1];
    wchar_t candidate[KS_MAX_WORD + 2];
    int split;
    size_t i;

    if (length < 4 || length + 1 > KS_MAX_WORD) return;
    for (split = 1; split <= length - 2; ++split) {
        const wchar_t *right = typed + split;
        int left_rank;
        int right_rank;
        memcpy(left, typed, (size_t)split * sizeof(wchar_t));
        left[split] = 0;

        if (language == KS_LANG_PERSIAN) {
            int is_prefix = 0;
            int is_suffix = 0;
            for (i = 0; i < sizeof(ZWNJ_PREFIXES) / sizeof(ZWNJ_PREFIXES[0]); ++i)
                if (wcscmp(left, ZWNJ_PREFIXES[i]) == 0) is_prefix = 1;
            for (i = 0; i < sizeof(ZWNJ_SUFFIXES) / sizeof(ZWNJ_SUFFIXES[0]); ++i)
                if (wcscmp(right, ZWNJ_SUFFIXES[i]) == 0) is_suffix = 1;
            /* Suffix joins (کتابها) are riskier than verb prefixes: many
               nouns end in مان/شان/ات; they are Aggressive-only. */
            if (is_suffix && search->level < KS_SPELL_AGGRESSIVE) is_suffix = 0;
            /* Never on a word a vocabulary pack or the writing memory knows
               (کلاستر is not کلاس‌تر). The possessive and personal endings
               (مان تان شان ام ات اش) also end ordinary nouns, so they join
               only a word the dictionary does not know: ساختمان is not
               ساخت‌مان, and کتابهایشان is not کتابهای‌شان. The plural and comparative endings (ها، تر) join
               dictionary words too, whose stored form lacks the joiner. */
            if (is_suffix && ks_spell_rank(search->lexicon, typed) >= 0) is_suffix = 0;
            if (is_suffix && wcscmp(right, L"ها") != 0 && wcscmp(right, L"های") != 0 &&
                wcscmp(right, L"هایی") != 0 && wcscmp(right, L"تر") != 0 &&
                wcscmp(right, L"ترین") != 0 &&
                (ks_text_known(typed, language, search->lexicon->words, NULL) ||
                 ks_text_known(typed, language, search->lexicon->forms, NULL)))
                is_suffix = 0;
            /*
             * After می/نمی the stem must look like a verb: Persian verb
             * forms end in a personal ending (م ی د ند یم ید) or a past-stem
             * ت/د. This keeps nouns that merely start with می (میهمان,
             * میدان, میخانه) from being cut in two.
             */
            if (is_prefix) {
                size_t stem_length = wcslen(right);
                wchar_t last = stem_length ? right[stem_length - 1] : 0;
                if (!wcschr(L"مید" L"ت", last)) is_prefix = 0;
            }
            if (is_prefix || is_suffix) {
                const wchar_t *stem = is_prefix ? right : left;
                int stem_rank = wcslen(stem) >= 3 ? known_rank(search, stem) : -1;
                if (stem_rank >= 40) {
                    memcpy(candidate, left, (size_t)split * sizeof(wchar_t));
                    candidate[split] = 0x200C;
                    wcscpy(candidate + split + 1, right);
                    consider_ranked(search, candidate, stem_rank, 20, KS_SPELL_KIND_ZWNJ);
                }
            }
        }

        if (search->zwnj_only) continue;
        /*
         * Two everyday words typed without the space between them (درخانه,
         * inthe). Both halves must be very common; the pair is scored by the
         * rarer half so that a frequent function word cannot carry a stray
         * fragment.
         */
        left_rank = known_rank(search, left);
        /* A one-letter left half is only "a"/"I" (alot -> a lot). In
           Conservative mode both halves must be very common words. */
        if (left_rank < (split == 1 ? 60 : search->level <= KS_SPELL_CONSERVATIVE ? 50 : 40)) continue;
        right_rank = known_rank(search, right);
        if (right_rank < (search->level <= KS_SPELL_CONSERVATIVE ? 50 : 40)) continue;
        memcpy(candidate, left, (size_t)split * sizeof(wchar_t));
        candidate[split] = L' ';
        wcscpy(candidate + split + 1, right);
        /* Bonus below every genuine error-model bonus, so a split never
           outranks a one-letter repair that has real evidence behind it. */
        consider_ranked(search, candidate,
                        left_rank < right_rank ? left_rank : right_rank,
                        12, KS_SPELL_KIND_SPLIT);
    }
}

/*
 * Adjacent-key substitution and a missed doubled letter carry the same
 * weight, so between equally plausible repairs the more frequent word wins
 * (helo -> help rather than hello under Aggressive; Balanced's margin leaves
 * such ties alone).
 */
static int substitution_bonus(KS_LANGUAGE language, wchar_t typed, wchar_t intended) {
    if (language == KS_LANG_PERSIAN && ks_persian_confusable(typed, intended)) return KS_BONUS_HOMOPHONE;
    if (ks_keys_adjacent(typed, intended)) return KS_BONUS_ADJACENT;
    return KS_BONUS_OTHER_LETTER;
}

/* The letters a writer leaves out most: vowel letters. */
static int vowel_letter(KS_LANGUAGE language, wchar_t letter) {
    return language == KS_LANG_PERSIAN ? wcschr(L"اویه", letter) != NULL
                                       : wcschr(L"aeiou", letter) != NULL;
}

static void enumerate_candidates(KS_SEARCH *search) {
    const wchar_t *typed = search->typed;
    int length = search->typed_length;
    KS_LANGUAGE language = search->lexicon->language;
    const wchar_t *alphabet =
        language == KS_LANG_PERSIAN ? PERSIAN_ALPHABET : ENGLISH_ALPHABET;
    wchar_t candidate[KS_MAX_WORD + 2];
    int i;
    const wchar_t *letter;

    /* Transposition of two adjacent letters: teh -> the, نسهخ -> نسخه. */
    for (i = 0; i + 1 < length; ++i) {
        if (typed[i] == typed[i + 1]) continue;
        wcscpy(candidate, typed);
        candidate[i] = typed[i + 1];
        candidate[i + 1] = typed[i];
        consider(search, candidate, KS_BONUS_TRANSPOSE);
    }

    /* One letter too many: helllo -> hello, thje -> the. */
    if (length >= 3) {
        for (i = 0; i < length; ++i) {
            int bonus = KS_BONUS_EXTRA_OTHER;
            wchar_t removed = typed[i];
            int doubled = (i > 0 && typed[i - 1] == removed) ||
                          (i + 1 < length && typed[i + 1] == removed);
            if (doubled)
                bonus = KS_BONUS_EXTRA_DOUBLED;
            else if ((i > 0 && ks_keys_adjacent(typed[i - 1], removed)) ||
                     (i + 1 < length && ks_keys_adjacent(typed[i + 1], removed)))
                bonus = KS_BONUS_EXTRA_ADJACENT;
            if (i > 0 && typed[i - 1] == removed) continue; /* same string as i-1 */
            /*
             * Dropping the last letter is how a valid inflection or a name
             * that is missing from the lexicon "becomes" its stem (kayaks ->
             * kayak, webs -> web); dropping the first letter turns "alot"
             * into "lot" instead of "a lot" and flips a Persian verb's
             * negation (نخوب). That is affix stripping, not typo repair:
             * only Aggressive mode may try it, and never with a bonus. (A
             * doubled edge letter was handled by the run start above.)
             */
            if ((i == length - 1 || i == 0) && !doubled) {
                /* A real word losing its first or last letter is a different
                   word, often the opposite one (نخرید -> خرید): never in the
                   rare-word search. */
                if (search->bonus_floor > 0) continue;
#if KS_EDGE_SLIP
                /* A neighbouring key struck with the first or last letter
                   (پاکح, طورز) is a slip, not an ending: valid endings are in
                   the word-form Bloom and never reach this search. */
                int edge_slip = i == 0 ? ks_keys_adjacent(typed[1], removed)
                                       : ks_keys_adjacent(typed[length - 2], removed);
                if (edge_slip && search->lexicon->forms) {
                    bonus = KS_BONUS_EXTRA_ADJACENT;
                } else
#endif
                {
                    if (search->level < KS_SPELL_AGGRESSIVE) continue;
                    bonus = 0;
                }
            }
            memcpy(candidate, typed, (size_t)i * sizeof(wchar_t));
            memcpy(candidate + i, typed + i + 1, (size_t)(length - i - 1) * sizeof(wchar_t));
            candidate[length - 1] = 0;
            consider(search, candidate, bonus);
        }
    }

    /* One wrong letter: نسحه -> نسخه, wprd -> word. */
    for (i = 0; i < length; ++i) {
        wcscpy(candidate, typed);
        for (letter = alphabet; *letter; ++letter) {
            if (*letter == typed[i]) continue;
            candidate[i] = *letter;
            consider(search, candidate, substitution_bonus(language, typed[i], *letter));
        }
    }

    /* One letter missing: wrd -> word, سلم -> سلام. */
    if (length + 1 <= KS_MAX_WORD) {
        for (i = 0; i <= length; ++i) {
            memcpy(candidate, typed, (size_t)i * sizeof(wchar_t));
            memcpy(candidate + i + 1, typed + i, (size_t)(length - i) * sizeof(wchar_t));
            candidate[length + 1] = 0;
            for (letter = alphabet; *letter; ++letter) {
                int bonus = vowel_letter(language, *letter) ? KS_BONUS_MISSING_VOWEL
                                                            : KS_BONUS_MISSING_OTHER;
                /* Inserting before an identical letter equals inserting after
                   it; skip the duplicate string. */
                if (i < length && typed[i] == *letter) continue;
                if (i > 0 && typed[i - 1] == *letter) bonus = KS_BONUS_MISSING_DOUBLED;
                candidate[i] = *letter;
                consider(search, candidate, bonus);
            }
        }
    }
}

/* ---- Two slips ----------------------------------------------------------- */

static void find_near_letters(KS_SEARCH *search) {
    KS_LANGUAGE language = search->lexicon->language;
    const wchar_t *a;
    const wchar_t *b;
    int index = 0;
    for (a = search->alphabet; *a && index < 48; ++a, ++index) {
        int count = 0;
        for (b = search->alphabet; *b && count < 11; ++b) {
            if (*a != *b && (ks_keys_adjacent(*a, *b) ||
                             (language == KS_LANG_PERSIAN && ks_persian_confusable(*a, *b))))
                search->near[index][count++] = *b;
        }
        search->near[index][count] = 0;
    }
}

static const wchar_t *near_letters(const KS_SEARCH *search, wchar_t letter) {
    const wchar_t *found = wcschr(search->alphabet, letter);
    return found && found - search->alphabet < 48 ? search->near[found - search->alphabet] : L"";
}

static void slip_step(KS_SEARCH *search, const wchar_t *text, int bonus, int depth);

/* 1 when a and b are at most one edit apart (Damerau: one swap of neighbours,
   one letter changed, added or removed). */
static int within_one_edit(const wchar_t *a, const wchar_t *b) {
    size_t la = wcslen(a), lb = wcslen(b), i = 0;
    if (la == lb) {
        while (i < la && a[i] == b[i]) ++i;
        if (i == la) return 1;
        if (wcscmp(a + i + 1, b + i + 1) == 0) return 1;   /* one letter changed */
        return i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i] &&
               wcscmp(a + i + 2, b + i + 2) == 0;          /* one swap */
    }
    if (la + 1 == lb) { const wchar_t *t = a; a = b; b = t; la = lb; }
    else if (lb + 1 != la) return 0;
    /* a is one letter longer than b. */
    while (i < la - 1 && a[i] == b[i]) ++i;
    return wcscmp(a + i + 1, b + i) == 0;
}

/* One more slip undone: score the result after the second, or undo another. */
static void slip_found(KS_SEARCH *search, const wchar_t *text, int bonus, int depth) {
    if (depth == 1) {
        slip_step(search, text, bonus, 2);
    } else {
        /* Never the typed word without its first or last letter by a
           detour (webs -> wesb -> web): cutting an ending or a prefix is
           grammar, not repair (see enumerate_candidates). */
        int typed_length = search->typed_length;
        int rank;
        if ((int)wcslen(text) == typed_length - 1 &&
            (wcsncmp(text, search->typed, (size_t)typed_length - 1) == 0 ||
             wcscmp(text, search->typed + 1) == 0))
            return;
        rank = ks_spell_rank(search->lexicon, text);
        /* A word one edit away was scored by the one-slip search with the
           right bonus; two hops at one place (a -> b -> c) must not give it
           a better one. */
        if (rank < 0 || within_one_edit(text, search->typed)) return;
        search->slips = 2;
        consider_ranked(search, text, rank, bonus - KS_TWO_SLIP_PENALTY, KS_SPELL_KIND_EDIT);
    }
}

/*
 * Undoes one likely slip in every possible place: two neighbouring letters
 * swapped, a neighbouring key or a homophone for the right letter, a doubled
 * or neighbouring extra letter, a letter not doubled. The first step may also
 * restore any missing letter (missing letters are common; their position and
 * the second slip's are independent, so one order covers both).
 */
static void slip_step(KS_SEARCH *search, const wchar_t *text, int bonus, int depth) {
    KS_LANGUAGE language = search->lexicon->language;
    int length = (int)wcslen(text);
    wchar_t candidate[KS_MAX_WORD + 2];
    const wchar_t *letter;
    int i;

    if (length < 2) return;
    for (i = 0; i + 1 < length; ++i) {
        if (text[i] == text[i + 1]) continue;
        wcscpy(candidate, text);
        candidate[i] = text[i + 1];
        candidate[i + 1] = text[i];
        slip_found(search, candidate, bonus + KS_BONUS_TRANSPOSE, depth);
    }
    for (i = 0; i < length; ++i) {
        wcscpy(candidate, text);
        for (letter = near_letters(search, text[i]); *letter; ++letter) {
            candidate[i] = *letter;
            slip_found(search, candidate, bonus + substitution_bonus(language, text[i], *letter), depth);
        }
    }
    if (length >= 4) {
        for (i = 1; i < length; ++i) {
            int doubled = text[i - 1] == text[i];
            int near = ks_keys_adjacent(text[i - 1], text[i]) ||
                       (i + 1 < length && ks_keys_adjacent(text[i + 1], text[i]));
            /* The last letter only when doubled: dropping an ending is
               grammar, not repair (see enumerate_candidates). */
            if ((!doubled && !near) || (i == length - 1 && !doubled)) continue;
            memcpy(candidate, text, (size_t)i * sizeof(wchar_t));
            wcscpy(candidate + i, text + i + 1);
            slip_found(search, candidate,
                       bonus + (doubled ? KS_BONUS_EXTRA_DOUBLED : KS_BONUS_EXTRA_ADJACENT), depth);
        }
    }
    if (length + 1 > KS_MAX_WORD) return;
    for (i = 0; i < length; ++i) {
        /* A letter typed once that should be doubled. */
        memcpy(candidate, text, (size_t)(i + 1) * sizeof(wchar_t));
        wcscpy(candidate + i + 1, text + i);
        slip_found(search, candidate, bonus + KS_BONUS_MISSING_DOUBLED, depth);
    }
    if (depth != 1) return;
    for (i = 0; i <= length; ++i) {
        memcpy(candidate, text, (size_t)i * sizeof(wchar_t));
        wcscpy(candidate + i + 1, text + i);
        for (letter = search->alphabet; *letter; ++letter) {
            if ((i < length && text[i] == *letter) || (i > 0 && text[i - 1] == *letter)) continue;
#if !KS_TWO_SLIP_INSERT_ANY
            /* Only the letters most often left out: vowel letters, and keys
               next to the letters around the gap (a key pressed too
               lightly beside its neighbour). */
            if (!vowel_letter(language, *letter) &&
                !(i < length && ks_keys_adjacent(text[i], *letter)) &&
                !(i > 0 && ks_keys_adjacent(text[i - 1], *letter)))
                continue;
#endif
            candidate[i] = *letter;
            slip_found(search, candidate,
                       bonus + (vowel_letter(language, *letter) ? KS_BONUS_MISSING_VOWEL
                                                                : KS_BONUS_MISSING_OTHER), depth);
        }
    }
}

static int english_all_lowercase(const wchar_t *word) {
    while (*word) {
        if (*word < L'a' || *word > L'z') return 0;
        ++word;
    }
    return 1;
}

static int persian_letters_only(const wchar_t *word) {
    while (*word) {
        if (!wcschr(PERSIAN_ALPHABET, *word)) return 0;
        ++word;
    }
    return 1;
}

int ks_spell_known(const wchar_t *word, const KS_SPELL_LEXICON *lexicon) {
    if (!word || !*word || !lexicon) return 0;
    if (ks_spell_rank(lexicon, word) >= 0) return 1;
    if (ks_vocab_trusted(lexicon->vocabulary, word) ||
        ks_vocab_trusted(lexicon->personal, word)) return 1;
    /*
     * Deliberately not the common-word Bloom: it is raw corpus frequency and
     * contains joined typos such as "alot" and "thankyou". The rank table
     * already admits everyday casual vocabulary through a filter that rejects
     * those; here only the curated dictionary is trusted, stems and the forms
     * its affix rules allow.
     */
    return ks_text_known(word, lexicon->language, lexicon->words, NULL) ||
           ks_text_known(word, lexicon->language, lexicon->forms, NULL);
}

int ks_spell_weakly_known(const wchar_t *word, const KS_SPELL_LEXICON *lexicon) {
    int rank;
    if (!word || !*word || !lexicon) return 0;
    if (ks_vocab_trusted(lexicon->vocabulary, word) ||
        ks_vocab_trusted(lexicon->personal, word)) return 0;
    if (ks_rank_lookup(lexicon->ranks, word) >= 0) return 0;
    rank = ks_spell_rank(lexicon, word);
    if (rank > KS_MEMORY_WEAK_RANK) return 0;
    return rank >= 0 || ks_spell_known(word, lexicon);
}

static void finish(KS_SPELL_RESULT *result, const KS_SEARCH *search, int runner_up_score) {
    result->should_correct = 1;
    result->kind = search->best.kind;
    result->best_rank = search->best.rank;
    result->edits = search->best.slips > 1 ? 2 : 1;
    result->confidence = search->best.score - runner_up_score;
    if (result->confidence > 100) result->confidence = 100;
    wcscpy(result->replacement, search->best.text);
}

/* Whether the search found a winner the level accepts. */
static int convincing(const KS_SEARCH *search, const KS_LEVEL_POLICY *policy, size_t length,
                      int *runner_up_score) {
    const KS_CANDIDATE *best = &search->best;
    *runner_up_score = search->second.valid ? search->second.score : -1000;

    if (!best->valid || best->rank < policy->minimum_rank) return 0;
    if (best->slips > 1 && best->rank < KS_TWO_SLIP_MIN_RANK) return 0;
    if (length <= 3 && best->bonus < policy->short_word_bonus) return 0;
    return best->score - *runner_up_score >=
           policy->margin + (best->slips > 1 ? KS_TWO_SLIP_EXTRA_MARGIN : 0);
}

int ks_spell_correct(const wchar_t *word, int level,
                     const KS_SPELL_LEXICON *lexicon,
                     const KS_IGNORE_LIST *ignore,
                     KS_SPELL_RESULT *result) {
    return ks_spell_correct_in_context(word, NULL, NULL, level, lexicon, ignore, result);
}

int ks_spell_correct_in_context(const wchar_t *word, const wchar_t *before, const wchar_t *after,
                                int level, const KS_SPELL_LEXICON *lexicon,
                                const KS_IGNORE_LIST *ignore, KS_SPELL_RESULT *result) {
    KS_SEARCH search;
    KS_LEVEL_POLICY policy;
    size_t length;
    int runner_up_score;

    if (!result) return 0;
    memset(result, 0, sizeof(*result));
    if (!word || !lexicon || !lexicon->ranks || !lexicon->ranks->valid ||
        level <= KS_SPELL_OFF) return 0;
    if (lexicon->language != KS_LANG_ENGLISH &&
        lexicon->language != KS_LANG_PERSIAN) return 0;

    length = wcslen(word);
    policy = level_policy(level);
    if (length < (size_t)policy.minimum_length || length > KS_MAX_WORD) return 0;
    /*
     * Capitalised or mixed-case English is a name, an acronym, or code; a
     * Persian token with anything but letters (digits, ZWNJ, diacritics) is
     * outside the model. Both are left exactly as typed.
     */
    if (lexicon->language == KS_LANG_ENGLISH && !english_all_lowercase(word)) return 0;
    if (lexicon->language == KS_LANG_PERSIAN && !persian_letters_only(word)) return 0;
    if (ks_ignore_list_contains(ignore, word)) return 0;
    if (ks_vocab_trusted(lexicon->vocabulary, word) ||
        ks_vocab_trusted(lexicon->personal, word)) return 0;

    /*
     * ZWNJ normalisation. The base dictionary stores ZWNJ-free spellings
     * (the generator strips the joiner), so میپرسیدند is "known" there even
     * though the standard orthography is می‌پرسیدند. When the joined form is
     * not itself an everyday word in the rank table, restore the joiner.
     */
    if (lexicon->language == KS_LANG_PERSIAN && level >= KS_SPELL_BALANCED &&
        ks_rank_lookup(lexicon->ranks, word) < 0) {
        /* The corpus table alone decides here: vocabulary packs and the
           writing memory store joined forms (سرورها) and must not stop the
           joiner from being restored. */
        memset(&search, 0, sizeof(search));
        search.lexicon = lexicon;
        search.typed = word;
        search.typed_length = (int)length;
        search.level = level;
        search.zwnj_only = 1;
        enumerate_joins(&search);
        if (search.best.valid && search.best.kind == KS_SPELL_KIND_ZWNJ) {
            result->should_correct = 1;
            result->kind = KS_SPELL_KIND_ZWNJ;
            result->best_rank = search.best.rank;
            result->confidence = 100;
            result->candidate_count = search.distinct;
            wcscpy(result->original, word);
            wcscpy(result->replacement, search.best.text);
            return 1;
        }
    }
    memset(&search, 0, sizeof(search));
    search.lexicon = lexicon;
    search.typed = word;
    search.typed_length = (int)length;
    search.level = level;
    search.slips = 1;
    search.alphabet = lexicon->language == KS_LANG_PERSIAN ? PERSIAN_ALPHABET : ENGLISH_ALPHABET;
    search.before = before && *before ? before : NULL;
    search.after = after && *after ? after : NULL;
    wcscpy(result->original, word);

    if (ks_spell_known(word, lexicon)) {
        /*
         * A real word, but perhaps only a rare one that a keyboard slip
         * produced: خرفه (purslane) for حرفه (profession), or a typo the
         * writing memory has seen a few times. It yields only to a very
         * frequent word one strong slip away (a neighbouring key, a
         * homophone, swapped or doubled letters) that clearly beats leaving
         * it as typed. Never at Conservative, never for short words.
         */
        if (level < KS_SPELL_BALANCED || length < 4 || !ks_spell_weakly_known(word, lexicon))
            return 0;
        search.bonus_floor = KS_BONUS_EXTRA_ADJACENT;
        enumerate_candidates(&search);
        result->candidate_count = search.distinct;
        if (!search.best.valid || search.best.rank < KS_REAL_WORD_MIN_RANK) return 0;
        {
            /* Leaving the word as typed is worth more where it fits the
               words around it. */
            int keep = KS_REAL_WORD_KEEP + context_bonus(&search, word, KS_SPELL_KIND_EDIT);
            runner_up_score = search.second.valid && search.second.score > keep
                                  ? search.second.score : keep;
        }
        if (search.best.score - runner_up_score < policy.margin) return 0;
        finish(result, &search, runner_up_score);
        result->real_word = 1;
        return 1;
    }

    /*
     * Not a dictionary word, but a token real text contains: a name, a
     * brand, an abbreviation (zach, binance, kaggle). The nearest
     * dictionary word is not what the user meant. Corpus misspellings
     * (recieve, teh) are kept out of this lexicon when it is built.
     */
    if (lexicon->attested && length >= 3 && ks_bloom_contains(lexicon->attested, word)) return 0;

    enumerate_candidates(&search);
    enumerate_joins(&search);
    if (!convincing(&search, &policy, length, &runner_up_score) &&
        level >= KS_SPELL_BALANCED && length >= KS_TWO_SLIP_MIN_LENGTH &&
        length <= KS_TWO_SLIP_MAX_LENGTH) {
        /* No single slip explains the word: try two. */
        find_near_letters(&search);
        slip_step(&search, word, 0, 1);
    }
    result->candidate_count = search.distinct;
    if (!convincing(&search, &policy, length, &runner_up_score)) return 0;
    finish(result, &search, runner_up_score);
    return 1;
}
