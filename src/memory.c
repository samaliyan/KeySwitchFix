#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_fix(const KS_WRITING_MEMORY *memory, const wchar_t *typo);

static uint32_t word_hash(const wchar_t *word) {
    uint32_t first = 0;
    uint32_t second = 0;
    ks_hash_text(word, &first, &second);
    return first;
}

static int valid_word(const wchar_t *word) {
    size_t length;
    size_t i;
    if (!word) return 0;
    length = wcslen(word);
    if (length < 2 || length > KS_MAX_WORD) return 0;
    for (i = 0; i < length; ++i) {
        wchar_t c = word[i];
        if (c <= L' ' || c == L'\t' || c == 0x200C || c == 0x00A0 || c == L'[' || c == L'#')
            return 0;
    }
    return 1;
}

void ks_memory_reset(KS_WRITING_MEMORY *memory) {
    if (memory) memset(memory, 0, sizeof(*memory));
}

static int find_word(const KS_WRITING_MEMORY *memory, const wchar_t *word, uint32_t *slot_out) {
    uint32_t slot = word_hash(word) & (KS_MEMORY_SLOTS - 1);
    int probes;
    for (probes = 0; probes < KS_MEMORY_SLOTS; ++probes) {
        unsigned short entry = memory->slots[slot];
        if (!entry) {
            if (slot_out) *slot_out = slot;
            return -1;
        }
        if (wcscmp(memory->words[entry - 1].text, word) == 0) {
            if (slot_out) *slot_out = slot;
            return entry - 1;
        }
        slot = (slot + 1) & (KS_MEMORY_SLOTS - 1);
    }
    if (slot_out) *slot_out = 0;
    return -1;
}

static void rebuild_slots(KS_WRITING_MEMORY *memory) {
    int i;
    memset(memory->slots, 0, sizeof(memory->slots));
    for (i = 0; i < memory->word_count; ++i) {
        uint32_t slot = word_hash(memory->words[i].text) & (KS_MEMORY_SLOTS - 1);
        while (memory->slots[slot]) slot = (slot + 1) & (KS_MEMORY_SLOTS - 1);
        memory->slots[slot] = (unsigned short)(i + 1);
    }
}

/* Most used first; among equals, most recently seen first (never by
   alphabet, which would always evict one script before the other). */
static int by_count_descending(const void *left, const void *right) {
    const KS_MEMORY_WORD *a = (const KS_MEMORY_WORD *)left;
    const KS_MEMORY_WORD *b = (const KS_MEMORY_WORD *)right;
    if (a->count != b->count) return a->count > b->count ? -1 : 1;
    if (a->last_seen != b->last_seen) return a->last_seen > b->last_seen ? -1 : 1;
    return 0;
}

/* Full: keep the most used seven eighths, forget the rest at once so the
   cost is paid rarely. */
static void evict_words(KS_WRITING_MEMORY *memory) {
    int i;
    qsort(memory->words, (size_t)memory->word_count, sizeof(memory->words[0]), by_count_descending);
    memory->word_count = KS_MEMORY_WORDS - KS_MEMORY_WORDS / 8;
    /* Age what stays by about a quarter at every eviction. A word seen
       since the previous eviction loses the smaller share (a known word in
       use stays known); a word not seen since loses the larger share, at
       least one, so words the user stopped writing fade out. */
    for (i = 0; i < memory->word_count; ++i) {
        unsigned count = memory->words[i].count;
        if (memory->words[i].last_seen > memory->aged_at) count -= count / 4;
        else if (count > 1) count -= (count + 3) / 4;
        memory->words[i].count = count;
    }
    memory->aged_at = memory->clock;
    rebuild_slots(memory);
}

unsigned ks_memory_word_count(const KS_WRITING_MEMORY *memory, const wchar_t *word) {
    int index;
    if (!memory || !word || !*word) return 0;
    index = find_word(memory, word, NULL);
    return index >= 0 ? memory->words[index].count : 0;
}

static void add_word_count(KS_WRITING_MEMORY *memory, const wchar_t *word, unsigned count) {
    uint32_t slot;
    int index;
    if (!valid_word(word) || count == 0) return;
    ++memory->clock;
    index = find_word(memory, word, &slot);
    if (index >= 0) {
        unsigned total = memory->words[index].count + count;
        memory->words[index].count = total > 1000000u ? 1000000u : total;
        memory->words[index].last_seen = memory->clock;
        memory->dirty = 1;
        return;
    }
    if (memory->word_count >= KS_MEMORY_WORDS) {
        evict_words(memory);
        find_word(memory, word, &slot);
    }
    index = memory->word_count++;
    wcscpy(memory->words[index].text, word);
    memory->words[index].count = count > 1000000u ? 1000000u : count;
    memory->words[index].last_seen = memory->clock;
    memory->slots[slot] = (unsigned short)(index + 1);
    memory->dirty = 1;
}

void ks_memory_observe_word(KS_WRITING_MEMORY *memory, const wchar_t *word) {
    if (memory) add_word_count(memory, word, 1);
}

void ks_memory_unobserve_word(KS_WRITING_MEMORY *memory, const wchar_t *word) {
    int index;
    if (!memory || !word || !*word) return;
    index = find_word(memory, word, NULL);
    if (index < 0 || memory->words[index].count == 0) return;
    /* A count of zero stays in the table (removing from open addressing
       would break probe chains); eviction and saving drop it. */
    --memory->words[index].count;
    memory->dirty = 1;
}

int ks_memory_rank_adjust(const KS_WRITING_MEMORY *memory, const wchar_t *word, int table_rank) {
    unsigned count;
    unsigned scaled;
    int lift = 0;
    if (!memory || !word) return table_rank;
    count = ks_memory_word_count(memory, word);
    for (scaled = count; scaled > 1 && lift < 16; scaled >>= 1) lift += 2;
    if (table_rank < 0) return count >= KS_MEMORY_KNOWN_COUNT ? 30 + lift : -1;
    return table_rank + (lift < 12 ? lift : 12);
}

int ks_memory_distance(const wchar_t *a, const wchar_t *b, int limit) {
    int rows[3][KS_MAX_WORD + 2];
    int la = (int)wcslen(a);
    int lb = (int)wcslen(b);
    int i;
    int j;
    if (la > KS_MAX_WORD || lb > KS_MAX_WORD) return limit + 1;
    if (la - lb > limit || lb - la > limit) return limit + 1;
    for (j = 0; j <= lb; ++j) rows[0][j] = j;
    for (i = 1; i <= la; ++i) {
        int *current = rows[i % 3];
        const int *previous = rows[(i - 1) % 3];
        const int *before = rows[(i + 1) % 3];   /* row i - 2 */
        int best = current[0] = i;
        for (j = 1; j <= lb; ++j) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int value = previous[j] + 1;
            if (current[j - 1] + 1 < value) value = current[j - 1] + 1;
            if (previous[j - 1] + cost < value) value = previous[j - 1] + cost;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                before[j - 2] + 1 < value)
                value = before[j - 2] + 1;
            current[j] = value;
            if (value < best) best = value;
        }
        if (best > limit) return limit + 1;
    }
    return rows[la % 3][lb] > limit ? limit + 1 : rows[la % 3][lb];
}

int ks_memory_is_fix_pair(const wchar_t *typo, const wchar_t *fix) {
    size_t typo_length;
    size_t fix_length;
    int limit;
    if (!valid_word(typo) || !valid_word(fix) || wcscmp(typo, fix) == 0) return 0;
    typo_length = wcslen(typo);
    fix_length = wcslen(fix);
    if (typo_length < 3 || fix_length < 3) return 0;
    /* An unfinished word ("عزی" then "عزیزم") is not a typo. */
    if (typo_length < fix_length && wcsncmp(fix, typo, typo_length) == 0) return 0;
    /* Dropping letters from the end is a repair only for one letter
       ("helloo" → "hello"); more is a change of mind ("کتابها" → "کتاب"). */
    if (fix_length + 1 < typo_length && wcsncmp(typo, fix, fix_length) == 0) return 0;
    limit = fix_length <= 4 ? 1 : 2;
    return ks_memory_distance(typo, fix, limit) <= limit;
}

int ks_memory_is_fix_pair_midword(const wchar_t *typo, const wchar_t *fix) {
    if (!typo || !fix || wcslen(typo) < wcslen(fix)) return 0;
    return ks_memory_is_fix_pair(typo, fix);
}

int ks_memory_fix_count(const KS_WRITING_MEMORY *memory, const wchar_t *typo) {
    int index;
    if (!memory || !typo) return 0;
    index = find_fix(memory, typo);
    return index >= 0 ? memory->fixes[index].count : 0;
}

static int find_fix(const KS_WRITING_MEMORY *memory, const wchar_t *typo) {
    int i;
    for (i = 0; i < memory->fix_count; ++i)
        if (wcscmp(memory->fixes[i].typo, typo) == 0) return i;
    return -1;
}

static int add_fix_count(KS_WRITING_MEMORY *memory, const wchar_t *typo, const wchar_t *fix, int count) {
    int index = find_fix(memory, typo);
    if (index >= 0) {
        KS_MEMORY_FIX *entry = &memory->fixes[index];
        if (wcscmp(entry->fix, fix) == 0) {
            entry->count = entry->count + count > 1000000 ? 1000000 : entry->count + count;
        } else {
            /* The user repairs this typo differently now: follow them. */
            wcscpy(entry->fix, fix);
            entry->count = count;
        }
        memory->dirty = 1;
        return entry->count;
    }
    if (memory->fix_count >= KS_MEMORY_FIXES) {
        int weakest = 0;
        int i;
        for (i = 1; i < memory->fix_count; ++i)
            if (memory->fixes[i].count < memory->fixes[weakest].count) weakest = i;
        index = weakest;
    } else {
        index = memory->fix_count++;
    }
    wcscpy(memory->fixes[index].typo, typo);
    wcscpy(memory->fixes[index].fix, fix);
    memory->fixes[index].count = count;
    memory->dirty = 1;
    return count;
}

int ks_memory_observe_fix(KS_WRITING_MEMORY *memory, const wchar_t *typo, const wchar_t *fix) {
    if (!memory || !ks_memory_is_fix_pair(typo, fix)) return 0;
    return add_fix_count(memory, typo, fix, 1);
}

const wchar_t *ks_memory_lookup_fix(const KS_WRITING_MEMORY *memory, const wchar_t *typo,
                                    int typo_is_known_word) {
    int index;
    if (!memory || !typo || !*typo) return NULL;
    index = find_fix(memory, typo);
    if (index < 0) return NULL;
    if (memory->fixes[index].count <
        (typo_is_known_word ? KS_MEMORY_FIX_COUNT_KNOWN : KS_MEMORY_FIX_COUNT)) return NULL;
    return memory->fixes[index].fix;
}

void ks_memory_reject_fix(KS_WRITING_MEMORY *memory, const wchar_t *typo) {
    int index;
    if (!memory || !typo) return;
    index = find_fix(memory, typo);
    if (index < 0) return;
    memory->fixes[index].count = 0;
    memory->dirty = 1;
}

int ks_memory_active_fix_count(const KS_WRITING_MEMORY *memory) {
    int i;
    int active = 0;
    if (!memory) return 0;
    for (i = 0; i < memory->fix_count; ++i)
        if (memory->fixes[i].count >= KS_MEMORY_FIX_COUNT) ++active;
    return active;
}

/* ---- Text form ------------------------------------------------------------ */

static size_t put(wchar_t *output, size_t capacity, size_t used, const wchar_t *text) {
    for (; *text; ++text, ++used)
        if (output && used + 1 < capacity) output[used] = *text;
    return used;
}

static size_t put_number(wchar_t *output, size_t capacity, size_t used, unsigned value) {
    wchar_t digits[16];
    swprintf(digits, 16, L"%u", value);
    return put(output, capacity, used, digits);
}

static const KS_WRITING_MEMORY *g_sort_memory;

static int order_by_count(const void *left, const void *right) {
    return by_count_descending(&g_sort_memory->words[*(const int *)left],
                               &g_sort_memory->words[*(const int *)right]);
}

size_t ks_memory_serialize(const KS_WRITING_MEMORY *memory, wchar_t *output, size_t capacity) {
    static int order[KS_MEMORY_WORDS];
    size_t used = 0;
    int i;
    int kept = 0;
    if (!memory) return 0;
    used = put(output, capacity, used,
               L"# KeySwitchFix writing memory. It never leaves this computer.\n"
               L"# [fixes]: typo, the correction you typed, times seen (applied from 2; 3 if the typo is a real word).\n"
               L"# [words]: words you have typed at least twice, and how often. Edit or delete lines freely; delete the file to forget everything.\n"
               L"[fixes]\n");
    for (i = 0; i < memory->fix_count; ++i) {
        used = put(output, capacity, used, memory->fixes[i].typo);
        used = put(output, capacity, used, L"\t");
        used = put(output, capacity, used, memory->fixes[i].fix);
        used = put(output, capacity, used, L"\t");
        used = put_number(output, capacity, used, (unsigned)memory->fixes[i].count);
        used = put(output, capacity, used, L"\n");
    }
    used = put(output, capacity, used, L"[words]\n");
    /* Only words typed at least twice reach the disk: a one-off token (a
       name typed once, or a password in a field Windows cannot identify as
       one) stays in memory for this session only. Most frequent first. */
    for (i = 0; i < memory->word_count; ++i)
        if (memory->words[i].count >= 2) order[kept++] = i;
    g_sort_memory = memory;
    qsort(order, (size_t)kept, sizeof(order[0]), order_by_count);
    for (i = 0; i < kept; ++i) {
        used = put(output, capacity, used, memory->words[order[i]].text);
        used = put(output, capacity, used, L"\t");
        used = put_number(output, capacity, used, memory->words[order[i]].count);
        used = put(output, capacity, used, L"\n");
    }
    if (output && capacity) output[used < capacity ? used : capacity - 1] = 0;
    return used;
}

/* Splits a line at tabs into up to three fields (in place). */
static int split_fields(wchar_t *line, wchar_t **fields, int maximum) {
    int count = 0;
    wchar_t *cursor = line;
    while (count < maximum) {
        wchar_t *tab = wcschr(cursor, L'\t');
        fields[count++] = cursor;
        if (!tab) break;
        *tab = 0;
        cursor = tab + 1;
    }
    return count;
}

static unsigned parse_count(const wchar_t *text) {
    unsigned long value = 0;
    int digits = 0;
    while (*text == L' ') ++text;
    while (*text >= L'0' && *text <= L'9' && digits < 9) {
        value = value * 10 + (unsigned long)(*text - L'0');
        ++text;
        ++digits;
    }
    return digits ? (unsigned)value : 0;
}

int ks_memory_parse(KS_WRITING_MEMORY *memory, const wchar_t *text) {
    enum { NONE, FIXES, WORDS } section = NONE;
    wchar_t line[3 * (KS_MAX_WORD + 1) + 32];
    const wchar_t *cursor;
    if (!memory || !text) return 0;
    ks_memory_reset(memory);
    cursor = text;
    if (*cursor == 0xFEFF) ++cursor;
    while (*cursor) {
        const wchar_t *end = cursor;
        size_t length;
        while (*end && *end != L'\n') ++end;
        length = (size_t)(end - cursor);
        if (length && cursor[length - 1] == L'\r') --length;
        if (length && length < sizeof(line) / sizeof(line[0])) {
            memcpy(line, cursor, length * sizeof(wchar_t));
            line[length] = 0;
            if (line[0] == L'#') {
                /* comment */
            } else if (wcscmp(line, L"[fixes]") == 0) {
                section = FIXES;
            } else if (wcscmp(line, L"[words]") == 0) {
                section = WORDS;
            } else {
                wchar_t *fields[3];
                int count = split_fields(line, fields, 3);
                if (section == FIXES && count == 3 && valid_word(fields[0]) && valid_word(fields[1]) &&
                    wcscmp(fields[0], fields[1]) != 0)
                    add_fix_count(memory, fields[0], fields[1], (int)parse_count(fields[2]));
                else if (section == WORDS && count >= 1)
                    add_word_count(memory, fields[0], count >= 2 ? parse_count(fields[1]) : 1);
            }
        }
        cursor = *end ? end + 1 : end;
    }
    /* The file keeps no recency: loaded words start equal, so the order of
       the lines does not decide which of two equal counts is forgotten. */
    {
        int i;
        /* Loaded words count as seen since the last eviction. */
        for (i = 0; i < memory->word_count; ++i) memory->words[i].last_seen = 1;
        memory->clock = 1;
        memory->aged_at = 0;
    }
    memory->dirty = 0;
    return 1;
}
