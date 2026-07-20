#ifndef KEYSWITCHFIX_CORE_H
#define KEYSWITCHFIX_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KS_MAX_WORD 32

typedef enum KS_LANGUAGE {
    KS_LANG_OTHER = 0,
    KS_LANG_ENGLISH = 1,
    KS_LANG_PERSIAN = 2
} KS_LANGUAGE;

typedef struct KS_TOKEN {
    wchar_t english;
    wchar_t persian;
} KS_TOKEN;

typedef struct KS_BLOOM {
    const unsigned char *bits;
    uint32_t bit_count;
    uint32_t bit_mask;
    uint32_t hash_count;
    int valid;
} KS_BLOOM;

typedef struct KS_DECISION {
    int should_correct;
    int key_count;
    KS_LANGUAGE source_language;
    KS_LANGUAGE target_language;
    wchar_t original[KS_MAX_WORD + 1];
    wchar_t replacement[KS_MAX_WORD + 1];
} KS_DECISION;

int ks_bloom_init(KS_BLOOM *bloom, const unsigned char *data, size_t size);
int ks_bloom_contains(const KS_BLOOM *bloom, const wchar_t *value);
int ks_map_scancode(uint32_t scan_code, int shift_down, int caps_lock, KS_TOKEN *token);
void ks_tokens_to_english(const KS_TOKEN *tokens, int count, wchar_t *output);
void ks_tokens_to_persian(const KS_TOKEN *tokens, int count, wchar_t *output);
int ks_evaluate(const KS_TOKEN *tokens, int count, KS_LANGUAGE active_language,
                int minimum_length, const KS_BLOOM *english, const KS_BLOOM *persian,
                KS_DECISION *decision);

#ifdef __cplusplus
}
#endif

#endif
