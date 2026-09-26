#ifndef KEYSWITCHFIX_DOMAIN_H
#define KEYSWITCHFIX_DOMAIN_H

/*
 * Vocabulary packs: specialist words compiled into the application, so they
 * count as real words for layout repair and spelling (never "corrected",
 * and a typo of one of them is repaired to it). Generated from the lists in
 * tools/domains/ by tools/generate_domain_words.py.
 */

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KS_DOMAIN_IT 1u   /* IT, computing, networks, databases, security */

int ks_domain_contains(unsigned packs, KS_LANGUAGE language, const wchar_t *word);
/* 1 when `prefix` is the beginning of (or equal to) a pack word. */
int ks_domain_has_prefix(unsigned packs, KS_LANGUAGE language, const wchar_t *prefix);
int ks_domain_word_count(unsigned packs, KS_LANGUAGE language);
/* Test hook: 1 when every pack list is strictly sorted (binary search). */
int ks_domain_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
