/**
 * @file debug_grapheme.c
 * @brief Manual grapheme manual test
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

/*
 * Debug grapheme cluster detection for failing test cases
 */

#include "../../include/lle/unicode_grapheme.h"
#include "../../include/lle/utf8_support.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void debug_string(const char *label, const char *text) {
    printf("\n=== %s ===\n", label);
    printf("Text: %s\n", text);

    size_t byte_len = strlen(text);
    printf("Byte length: %zu\n", byte_len);

    /// Show byte sequence
    printf("Bytes: ");
    for (size_t i = 0; i < byte_len; i++) {
        printf("%02X ", (unsigned char)text[i]);
    }
    printf("\n");

    /// Count graphemes
    size_t grapheme_count = lle_utf8_count_graphemes(text, byte_len);
    printf("Grapheme count: %zu\n", grapheme_count);

    /// Show each codepoint and whether it's a grapheme boundary
    printf("\nCodepoint analysis:\n");
    const char *ptr = text;
    const char *end = text + byte_len;
    int idx = 0;

    while (ptr < end) {
        uint32_t codepoint = 0;
        int len = lle_utf8_decode_codepoint(ptr, end - ptr, &codepoint);

        if (len <= 0) {
            printf("  [%d] Invalid UTF-8\n", idx);
            break;
        }

        bool is_boundary = lle_is_grapheme_boundary(ptr, text, end);

        printf("  [%d] U+%04X (%s) - len=%d bytes", idx, codepoint,
               is_boundary ? "BOUNDARY" : "extend", len);

        /// Show the character
        printf(" '");
        for (int i = 0; i < len; i++) {
            printf("%c", ptr[i]);
        }
        printf("'\n");

        ptr += len;
        idx++;
    }

    printf("Expected: 1 grapheme cluster\n");
    printf("Result: %s\n",
           grapheme_count == 1 ? "PASS \xe2\x9c\x93" : "FAIL \xe2\x9c\x97");
}

int main() {
    printf("Grapheme Cluster Detection Debug\n");
    printf("=================================\n");

    /// Test 4: Family emoji (ZWJ sequence)
    debug_string("Test 4: Family emoji ZWJ sequence",
                 "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0"
                 "\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6");

    /// Test 5: Flag emoji (Regional Indicators)
    debug_string("Test 5: Flag emoji", "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8");

    /// Test 7: Skin tone modifier
    debug_string("Test 7: Skin tone modifier",
                 "\xf0\x9f\x91\x8b\xf0\x9f\x8f\xbd");

    /// Working test for comparison
    debug_string("Working: Simple emoji", "\xf0\x9f\x8e\x89");

    return 0;
}
