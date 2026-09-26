#include "domain.h"

#include <string.h>

#include "domain_words.inc"

typedef struct DOMAIN_LIST {
    unsigned pack;
    KS_LANGUAGE language;
    const wchar_t *const *words;
    size_t count;
} DOMAIN_LIST;

static const DOMAIN_LIST LISTS[] = {
    {KS_DOMAIN_IT, KS_LANG_ENGLISH, DOMAIN_IT_EN, sizeof(DOMAIN_IT_EN) / sizeof(DOMAIN_IT_EN[0])},
    {KS_DOMAIN_IT, KS_LANG_PERSIAN, DOMAIN_IT_FA, sizeof(DOMAIN_IT_FA) / sizeof(DOMAIN_IT_FA[0])},
};

/* First index whose word is >= key (lists are sorted by code point, which
   is wcscmp order). */
static size_t lower_bound(const DOMAIN_LIST *list, const wchar_t *key) {
    size_t low = 0;
    size_t high = list->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (wcscmp(list->words[middle], key) < 0) low = middle + 1;
        else high = middle;
    }
    return low;
}

int ks_domain_contains(unsigned packs, KS_LANGUAGE language, const wchar_t *word) {
    size_t i;
    if (!word || !*word) return 0;
    for (i = 0; i < sizeof(LISTS) / sizeof(LISTS[0]); ++i) {
        const DOMAIN_LIST *list = &LISTS[i];
        size_t at;
        if (!(packs & list->pack) || list->language != language) continue;
        at = lower_bound(list, word);
        if (at < list->count && wcscmp(list->words[at], word) == 0) return 1;
    }
    return 0;
}

int ks_domain_has_prefix(unsigned packs, KS_LANGUAGE language, const wchar_t *prefix) {
    size_t i;
    size_t length;
    if (!prefix || !*prefix) return 0;
    length = wcslen(prefix);
    for (i = 0; i < sizeof(LISTS) / sizeof(LISTS[0]); ++i) {
        const DOMAIN_LIST *list = &LISTS[i];
        size_t at;
        if (!(packs & list->pack) || list->language != language) continue;
        at = lower_bound(list, prefix);
        if (at < list->count && wcsncmp(list->words[at], prefix, length) == 0) return 1;
    }
    return 0;
}

int ks_domain_word_count(unsigned packs, KS_LANGUAGE language) {
    size_t i;
    int total = 0;
    for (i = 0; i < sizeof(LISTS) / sizeof(LISTS[0]); ++i)
        if ((packs & LISTS[i].pack) && LISTS[i].language == language) total += (int)LISTS[i].count;
    return total;
}

int ks_domain_self_check(void) {
    size_t i;
    size_t j;
    for (i = 0; i < sizeof(LISTS) / sizeof(LISTS[0]); ++i)
        for (j = 1; j < LISTS[i].count; ++j)
            if (wcscmp(LISTS[i].words[j - 1], LISTS[i].words[j]) >= 0) return 0;
    return 1;
}
