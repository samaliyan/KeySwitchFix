/*
 * Layout-decision regression corpus. Every word of each list is "typed" one
 * key at a time through ks_evaluate_contextual (live phase at every key,
 * the adaptive pause, then the word boundary), with the real Bloom
 * resources and the IT vocabulary pack, and the false-positive and recall
 * rates are asserted. A change that makes the engine rewrite correctly
 * typed words, or miss wrong-layout words, fails the build.
 */
#include "../src/core.h"
#include "../src/domain.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KS_BLOOM g_blooms[10];
static KS_LEXICONS g_lexicons;

static int extra_contains(const void *context, KS_LANGUAGE language, const wchar_t *word) {
    (void)context;
    return ks_domain_contains(KS_DOMAIN_IT, language, word);
}

static int extra_prefix(const void *context, KS_LANGUAGE language, const wchar_t *prefix) {
    (void)context;
    return ks_domain_has_prefix(KS_DOMAIN_IT, language, prefix);
}

static const KS_EXTRA_WORDS g_extra = {extra_contains, extra_prefix, NULL};

/* Loaded files live until exit; freed there so leak checkers stay quiet. */
static unsigned char *g_loaded[32];
static int g_loaded_count;

static void free_loaded(void) {
    while (g_loaded_count > 0) free(g_loaded[--g_loaded_count]);
}

static unsigned char *load_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long length;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    if (g_loaded_count == 0) atexit(free_loaded);
    if (g_loaded_count < 32) g_loaded[g_loaded_count++] = data;
    return data;
}

static uint32_t english_scan(wchar_t c, int *shift) {
    static const uint32_t letters[26] = {
        0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c };
    *shift = c >= L'A' && c <= L'Z';
    if (c >= L'a' && c <= L'z') return letters[c - L'a'];
    if (c >= L'A' && c <= L'Z') return letters[c - L'A'];
    if (c == L';') return 0x27;
    if (c == L'\'') return 0x28;
    if (c == L'"') { *shift = 1; return 0x28; }
    if (c == L',') return 0x33;
    if (c == L'[') return 0x1a;
    if (c == L']') return 0x1b;
    return 0;
}

static uint32_t persian_scan(wchar_t c, int *shift) {
    uint32_t scan;
    KS_TOKEN token;
    for (scan = 1; scan < 0x40; ++scan) {
        for (*shift = 0; *shift < 2; ++*shift)
            if (ks_map_scancode(scan, *shift, 0, &token) && token.persian == c) return scan;
    }
    *shift = 0;
    return 0;
}

/* Tokens for the keys that produce `word` on the English (persian = 0) or
   Persian (persian = 1) layout. */
static int tokens_for(const wchar_t *word, int persian, KS_TOKEN *tokens) {
    int count = 0;
    for (; *word; ++word) {
        int shift = 0;
        uint32_t scan = persian ? persian_scan(*word, &shift) : english_scan(*word, &shift);
        if (!scan || count >= KS_MAX_WORD || !ks_map_scancode(scan, shift, 0, &tokens[count]))
            return 0;
        ++count;
    }
    return count;
}

/* 1 when typing the word key by key (with a pause after every key, then
   Space) makes the engine replace it. */
static int would_rewrite(const KS_TOKEN *tokens, int count, KS_LANGUAGE layout,
                         KS_LANGUAGE context, int strength) {
    KS_DECISION decision;
    int k;
    for (k = 1; k <= count; ++k) {
        KS_LIVE_RESULT live = ks_evaluate_contextual(tokens, k, layout, 1, context, strength, 0,
                                                     KS_PHASE_LIVE, &g_lexicons, &decision);
        if (live == KS_LIVE_CORRECT_NOW) return 1;
        if (ks_evaluate_contextual(tokens, k, layout, 1, context, strength, 0,
                                   KS_PHASE_IDLE, &g_lexicons, &decision) == KS_LIVE_CORRECT_NOW)
            return 1;
    }
    return ks_evaluate_contextual(tokens, count, layout, 1, context, strength, 0,
                                  KS_PHASE_BOUNDARY, &g_lexicons, &decision) == KS_LIVE_CORRECT_NOW;
}

typedef struct RUN {
    const char *file;
    int persian_words;     /* the list holds Persian words */
    int wrong_layout;      /* typed on the other layout (recall) */
    KS_LANGUAGE context;
    int strength;
    double limit;          /* max false-positive % or min recall % */
    const char *label;
} RUN;

int main(void) {
    static const char *names[10] = {
        "en", "fa", "en-common", "fa-common", "en-frequent", "fa-frequent",
        "en-prefix", "fa-prefix", "en-common-prefix", "fa-common-prefix" };
    static const RUN runs[] = {
        {"tests/corpus/english-common.txt", 0, 0, KS_LANG_OTHER, 0, 0.0, "English, no context"},
        {"tests/corpus/english-common.txt", 0, 0, KS_LANG_PERSIAN, 4, 1.5, "English inside Persian text"},
        {"tests/corpus/english-punctuated.txt", 0, 0, KS_LANG_OTHER, 0, 0.1, "English with quotes/commas"},
        {"tests/corpus/english-tech.txt", 0, 0, KS_LANG_OTHER, 0, 1.0, "English tech words"},
        {"tests/corpus/persian-common.txt", 1, 0, KS_LANG_OTHER, 0, 0.0, "Persian, no context"},
        {"tests/corpus/persian-common.txt", 1, 0, KS_LANG_ENGLISH, 4, 2.5, "Persian inside English text"},
        /* One miss: vhd, whose Persian reading رای is a frequent word. */
        {"tests/corpus/english-abbrev.txt", 0, 0, KS_LANG_ENGLISH, 3, 1.0, "Abbreviations in English text"},
        /* Ratchet: the misses are two-key tokens (fi, il, mv) whose Persian
           readings are core words (به, هم, پر); with no context those are
           taken as Persian on purpose. */
        {"tests/corpus/english-abbrev.txt", 0, 0, KS_LANG_OTHER, 0, 16.0, "Abbreviations, no context"},
        {"tests/corpus/english-common.txt", 0, 1, KS_LANG_OTHER, 0, 98.5, "English typed on the Persian layout"},
        {"tests/corpus/english-common.txt", 0, 1, KS_LANG_PERSIAN, 3, 97.0, "English on the Persian layout, Persian text"},
        {"tests/corpus/english-tech.txt", 0, 1, KS_LANG_PERSIAN, 4, 95.0, "Tech words on the Persian layout, Persian text"},
        {"tests/corpus/persian-common.txt", 1, 1, KS_LANG_ENGLISH, 3, 92.0, "Persian on the English layout, English text"},
        {"tests/corpus/persian-common.txt", 1, 1, KS_LANG_OTHER, 0, 98.5, "Persian typed on the English layout"},
        {"tests/corpus/english-tech.txt", 0, 1, KS_LANG_OTHER, 0, 95.0, "Tech words typed on the Persian layout"},
    };
    size_t i;
    int failed = 0;

    setlocale(LC_ALL, "C.UTF-8");
    for (i = 0; i < 10; ++i) {
        char path[128];
        size_t size = 0;
        unsigned char *data;
        snprintf(path, sizeof(path), "resources/%s.bloom", names[i]);
        data = load_file(path, &size);
        if (!data || !ks_bloom_init(&g_blooms[i], data, size)) {
            fprintf(stderr, "FAIL: %s does not load\n", path);
            return 1;
        }
    }
    g_lexicons.english_words = &g_blooms[0];
    g_lexicons.persian_words = &g_blooms[1];
    g_lexicons.english_common = &g_blooms[2];
    g_lexicons.persian_common = &g_blooms[3];
    g_lexicons.english_frequent = &g_blooms[4];
    g_lexicons.persian_frequent = &g_blooms[5];
    g_lexicons.english_prefixes = &g_blooms[6];
    g_lexicons.persian_prefixes = &g_blooms[7];
    g_lexicons.english_common_prefixes = &g_blooms[8];
    g_lexicons.persian_common_prefixes = &g_blooms[9];
    g_lexicons.extra = &g_extra;

    for (i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
        const RUN *run = &runs[i];
        FILE *file = fopen(run->file, "r");
        char line[256];
        int total = 0;
        int rewritten = 0;
        double rate;
        if (!file) {
            fprintf(stderr, "FAIL: %s missing\n", run->file);
            return 1;
        }
        while (fgets(line, sizeof(line), file)) {
            wchar_t word[128];
            KS_TOKEN tokens[KS_MAX_WORD];
            int count;
            KS_LANGUAGE layout;
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0] || line[0] == '#' || mbstowcs(word, line, 128) == (size_t)-1) continue;
            count = tokens_for(word, run->persian_words, tokens);
            if (count < 1) continue;
            /* Typed on the intended layout, or on the other one. */
            layout = (run->persian_words != run->wrong_layout) ? KS_LANG_PERSIAN : KS_LANG_ENGLISH;
            ++total;
            if (would_rewrite(tokens, count, layout, run->context, run->strength)) ++rewritten;
        }
        fclose(file);
        rate = total ? 100.0 * rewritten / total : 0.0;
        if (run->wrong_layout) {
            printf("  %-40s recall %6.2f%% (%d/%d, minimum %.1f%%)\n", run->label, rate, rewritten, total, run->limit);
            if (rate < run->limit) failed = 1;
        } else {
            printf("  %-40s false positives %5.2f%% (%d/%d, maximum %.1f%%)\n", run->label, rate, rewritten, total, run->limit);
            if (rate > run->limit) failed = 1;
        }
    }
    if (failed) {
        fprintf(stderr, "FAIL: layout corpus thresholds\n");
        return 1;
    }
    printf("All layout corpus tests passed.\n");
    return 0;
}
