/* Prints ks_is_letter / ks_to_lower for the whole BMP, and the lookup form
   of the words read from stdin under the profile flag set given on the
   command line, so tests/verify_language_rules.py can check that the pack
   builder (Python) normalises words exactly as the app (C) looks them up.
   Words travel as hexadecimal code points ("68 e9"), one per line, so no
   console encoding (Windows, Linux) can change them on the way. */
#include "../src/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    unsigned c;
    wchar_t line[256];
    if (argc > 1 && strcmp(argv[1], "table") == 0) {
        /* Every BMP code point except the surrogate halves. */
        for (c = 1; c <= 0xFFFF; ++c) {
            if (c >= 0xD800 && c <= 0xDFFF) continue;
            printf("%u %d %u\n", c, ks_is_letter((wchar_t)c), (unsigned)ks_to_lower((wchar_t)c));
        }
        return 0;
    }
    if (argc > 1) {
        KS_LANG_PROFILE profile;
        char text[1024];
        memset(&profile, 0, sizeof(profile));
        profile.flags = (unsigned)strtoul(argv[1], NULL, 0);
        while (fgets(text, sizeof(text), stdin)) {
            wchar_t form[KS_MAX_WORD + 1];
            int length = 0;
            char *cursor = text;
            char *end;
            for (;;) {
                unsigned long value = strtoul(cursor, &end, 16);
                if (end == cursor || length >= 255) break;
                line[length++] = (wchar_t)value;
                cursor = end;
            }
            line[length] = 0;
            if (ks_lookup_form(&profile, line, form)) {
                int i;
                for (i = 0; form[i]; ++i) printf(i ? " %x" : "%x", (unsigned)form[i]);
                printf("\n");
            } else {
                printf("-\n");
            }
        }
    }
    return 0;
}
