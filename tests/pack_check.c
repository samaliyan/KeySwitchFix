/* Loads every language pack named on the command line with the app's own
   parser (ks_pack_parse) and checks that each one is usable: the file name
   is "<code>.kslang", the dictionaries are there and a few of its own
   words are found again. build-native.sh runs it on the packs it built, so
   a builder that drifts from the parser can never ship unloadable packs. */
#include "../src/core.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *load(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    unsigned char *data;
    long length;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (unsigned char *)malloc(length > 0 ? (size_t)length : 1);
    if (!data || length <= 0 || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

int main(int argc, char **argv) {
    int i;
    int failed = 0;
    static KS_LANG_PACK pack;
    if (!setlocale(LC_ALL, "C.UTF-8")) setlocale(LC_ALL, "");
    for (i = 1; i < argc; ++i) {
        size_t size = 0;
        unsigned char *data = load(argv[i], &size);
        const char *base = argv[i];
        const char *cursor;
        char expected[32];
        for (cursor = argv[i]; *cursor; ++cursor)
            if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
        if (!data || !ks_pack_parse(data, size, &pack)) {
            fprintf(stderr, "FAIL: %s is not a loadable language pack\n", argv[i]);
            failed = 1;
            free(data);
            continue;
        }
        snprintf(expected, sizeof(expected), "%s.kslang", pack.profile.code);
        if (strcmp(base, expected) != 0) {
            fprintf(stderr, "FAIL: %s holds the language %s\n", argv[i], pack.profile.code);
            failed = 1;
        } else if (!pack.common.valid || !pack.frequent.valid || !pack.common_prefixes.valid ||
                   pack.profile.short_count < 1 || pack.langid_count < 1) {
            fprintf(stderr, "FAIL: %s lacks a section, its short words or its language id\n", argv[i]);
            failed = 1;
        } else if (!ks_profile_short_word(&pack.profile, pack.profile.short_words[0])) {
            fprintf(stderr, "FAIL: %s does not find its own short words\n", argv[i]);
            failed = 1;
        } else {
            printf("  %-12s %ls (%ls), %d short words\n", base, pack.english_name, pack.native_name,
                   pack.profile.short_count);
        }
        free(data);
    }
    if (failed) return 1;
    printf("All %d language pack(s) load.\n", argc - 1);
    return 0;
}
