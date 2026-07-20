#include "../src/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return data;
}

static int make_tokens(const uint32_t *scans, int count, KS_TOKEN *tokens) {
    int i;
    for (i = 0; i < count; ++i) {
        if (!ks_map_scancode(scans[i], 0, 0, &tokens[i])) return 0;
    }
    return 1;
}

#define CHECK(value, message) do { if (!(value)) { fprintf(stderr, "FAIL: %s\n", message); return 1; } } while (0)

int main(void) {
    size_t en_size = 0, fa_size = 0;
    unsigned char *en_data = load_file("resources/en.bloom", &en_size);
    unsigned char *fa_data = load_file("resources/fa.bloom", &fa_size);
    KS_BLOOM en, fa;
    KS_TOKEN tokens[KS_MAX_WORD];
    KS_DECISION decision;
    wchar_t mapped[KS_MAX_WORD + 1];
    const uint32_t password[] = {0x19,0x1E,0x1F,0x1F,0x11,0x18,0x13,0x20};
    const uint32_t salam[] = {0x1F,0x22,0x23,0x26};
    const uint32_t ketab[] = {0x27,0x24,0x23,0x21};
    const uint32_t hello[] = {0x23,0x12,0x26,0x26,0x18};

    CHECK(en_data && fa_data, "dictionary files load");
    CHECK(ks_bloom_init(&en, en_data, en_size), "English Bloom validates");
    CHECK(ks_bloom_init(&fa, fa_data, fa_size), "Persian Bloom validates");
    CHECK(ks_bloom_contains(&en, L"password"), "English dictionary contains password");
    CHECK(ks_bloom_contains(&fa, L"سلام"), "Persian dictionary contains salam");

    CHECK(make_tokens(password, 8, tokens), "password scan codes map");
    ks_tokens_to_persian(tokens, 8, mapped);
    CHECK(wcscmp(mapped, L"حشسسصخقی") == 0, "password maps to Persian mistype");
    CHECK(ks_evaluate(tokens, 8, KS_LANG_PERSIAN, 4, &en, &fa, &decision), "Persian-to-English decision");
    CHECK(wcscmp(decision.replacement, L"password") == 0, "replacement is password");
    CHECK(!ks_evaluate(tokens, 8, KS_LANG_ENGLISH, 4, &en, &fa, &decision),
          "correct English word remains unchanged");

    CHECK(make_tokens(salam, 4, tokens), "salam scan codes map");
    CHECK(ks_evaluate(tokens, 4, KS_LANG_ENGLISH, 4, &en, &fa, &decision), "English-to-Persian decision");
    CHECK(wcscmp(decision.replacement, L"سلام") == 0, "replacement is salam");
    CHECK(!ks_evaluate(tokens, 4, KS_LANG_PERSIAN, 4, &en, &fa, &decision),
          "correct Persian word remains unchanged");

    CHECK(make_tokens(ketab, 4, tokens), "ketab OEM scan codes map");
    CHECK(ks_evaluate(tokens, 4, KS_LANG_ENGLISH, 4, &en, &fa, &decision), "OEM English-to-Persian decision");
    CHECK(wcscmp(decision.replacement, L"کتاب") == 0, "replacement is ketab");

    CHECK(make_tokens(hello, 5, tokens), "hello scan codes map");
    ks_tokens_to_persian(tokens, 5, mapped);
    CHECK(wcscmp(mapped, L"اثممخ") == 0, "hello maps to Persian mistype");
    CHECK(ks_evaluate(tokens, 5, KS_LANG_PERSIAN, 4, &en, &fa, &decision),
          "Persian-to-English hello decision");
    CHECK(wcscmp(decision.replacement, L"hello") == 0, "replacement is hello");

    free(en_data);
    free(fa_data);
    puts("All native core tests passed.");
    return 0;
}
