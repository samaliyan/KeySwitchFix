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

typedef struct KS_LEXICONS {
    const KS_BLOOM *english_words;
    const KS_BLOOM *persian_words;
    const KS_BLOOM *english_common;
    const KS_BLOOM *persian_common;
    const KS_BLOOM *english_frequent;
    const KS_BLOOM *persian_frequent;
    const KS_BLOOM *english_prefixes;
    const KS_BLOOM *persian_prefixes;
    const KS_BLOOM *english_common_prefixes;
    const KS_BLOOM *persian_common_prefixes;
} KS_LEXICONS;

typedef struct KS_LANGUAGE_CONTEXT {
    KS_LANGUAGE language;
    int strength;
    unsigned int observed_words;
} KS_LANGUAGE_CONTEXT;

typedef struct KS_SEQUENCE_WORD {
    const KS_TOKEN *tokens;
    int count;
} KS_SEQUENCE_WORD;

typedef struct KS_SEQUENCE_RESULT {
    KS_LANGUAGE language;
    int confidence;
    int english_score;
    int persian_score;
    int english_known_words;
    int persian_known_words;
} KS_SEQUENCE_RESULT;

typedef struct KS_DECISION {
    int should_correct;
    int key_count;
    int confidence;
    KS_LANGUAGE source_language;
    KS_LANGUAGE target_language;
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

int ks_bloom_init(KS_BLOOM *bloom, const unsigned char *data, size_t size);
int ks_bloom_contains(const KS_BLOOM *bloom, const wchar_t *value);
wchar_t ks_canonical_persian(wchar_t character);
int ks_is_persian_diacritic(wchar_t character);
int ks_is_word_scancode(uint32_t scan_code);
int ks_map_scancode(uint32_t scan_code, int shift_down, int caps_lock, KS_TOKEN *token);
void ks_tokens_to_english(const KS_TOKEN *tokens, int count, wchar_t *output);
void ks_tokens_to_persian(const KS_TOKEN *tokens, int count, wchar_t *output);
int ks_word_membership(const KS_TOKEN *tokens, int count,
                       const KS_BLOOM *english, const KS_BLOOM *persian,
                       const KS_BLOOM *english_common,
                       const KS_BLOOM *persian_common,
                       int *english_known, int *persian_known);
int ks_classify_word(const KS_TOKEN *tokens, int count,
                     const KS_LEXICONS *lexicons,
                     int *english_known, int *persian_known,
                     int *english_frequent, int *persian_frequent);
int ks_collision_prior_points(const wchar_t *english_word);
int ks_evaluate_sequence(const KS_SEQUENCE_WORD *words, int word_count,
                         int sensitivity,
                         KS_LANGUAGE context_language, int context_strength,
                         const KS_LEXICONS *lexicons,
                         KS_SEQUENCE_RESULT *result);
void ks_context_reset(KS_LANGUAGE_CONTEXT *context);
void ks_context_observe(KS_LANGUAGE_CONTEXT *context,
                        KS_LANGUAGE language, int evidence);
KS_LANGUAGE ks_context_current(const KS_LANGUAGE_CONTEXT *context,
                               int *strength);
KS_LIVE_RESULT ks_evaluate_contextual(
                                 const KS_TOKEN *tokens, int count,
                                 KS_LANGUAGE active_language, int sensitivity,
                                 KS_LANGUAGE context_language, int context_strength,
                                 int sentence_start,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_LEXICONS *lexicons,
                                 KS_DECISION *decision);
KS_LIVE_RESULT ks_evaluate_smart(const KS_TOKEN *tokens, int count,
                                 KS_LANGUAGE active_language, int sensitivity,
                                 KS_LANGUAGE context_language, int context_strength,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_BLOOM *english, const KS_BLOOM *persian,
                                 const KS_BLOOM *english_prefixes,
                                 const KS_BLOOM *persian_prefixes,
                                 KS_DECISION *decision);
KS_LIVE_RESULT ks_evaluate_smart_common(
                                 const KS_TOKEN *tokens, int count,
                                 KS_LANGUAGE active_language, int sensitivity,
                                 KS_LANGUAGE context_language, int context_strength,
                                 KS_EVALUATION_PHASE phase,
                                 const KS_BLOOM *english, const KS_BLOOM *persian,
                                 const KS_BLOOM *english_common,
                                 const KS_BLOOM *persian_common,
                                 const KS_BLOOM *english_prefixes,
                                 const KS_BLOOM *persian_prefixes,
                                 KS_DECISION *decision);
int ks_evaluate(const KS_TOKEN *tokens, int count, KS_LANGUAGE active_language,
                int minimum_length, const KS_BLOOM *english, const KS_BLOOM *persian,
                KS_DECISION *decision);
KS_LIVE_RESULT ks_evaluate_live(const KS_TOKEN *tokens, int count,
                                KS_LANGUAGE active_language, int minimum_length,
                                const KS_BLOOM *english, const KS_BLOOM *persian,
                                const KS_BLOOM *english_prefixes,
                                const KS_BLOOM *persian_prefixes,
                                KS_DECISION *decision);
uint32_t ks_update_key_interval_ms(uint32_t current_average_ms,
                                   uint32_t observed_interval_ms);
uint32_t ks_idle_delay_ms(int sensitivity, uint32_t average_key_interval_ms);

#ifdef __cplusplus
}
#endif

#endif
