/**
 * @file fuzz_tokenizer.c
 * @brief Fuzz target for lush shell tokenizer
 *
 * This file provides fuzz testing for the tokenizer using either libFuzzer
 * (Clang) or AFL++ (GCC/Clang). The tokenizer is the first stage of parsing
 * and handles lexical analysis of shell input.
 *
 * Build with libFuzzer (Clang only):
 *   CC=clang meson setup build -Denable_fuzzing=true -Dfuzzer=libfuzzer
 *   meson compile -C build fuzz_tokenizer
 *   ./build/fuzz_tokenizer tests/fuzz/corpus/tokenizer/ -max_len=4096
 *
 * Build with AFL++ (GCC or Clang):
 *   CC=afl-clang-fast meson setup build -Denable_fuzzing=true -Dfuzzer=afl
 *   meson compile -C build fuzz_tokenizer_afl
 *   afl-fuzz -i tests/fuzz/corpus/tokenizer/ -o findings/ --
 * ./build/fuzz_tokenizer_afl
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "shell_mode.h"
#include "tokenizer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief libFuzzer entry point
 *
 * Called by libFuzzer with mutated input data. We tokenize the entire
 * input, consuming all tokens until EOF. The fuzzer will detect crashes,
 * infinite loops (via timeout), and memory errors.
 *
 * @param data Raw input bytes from fuzzer
 * @param size Size of input data
 * @return 0 (required by libFuzzer API)
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Limit input size to prevent OOM on huge inputs */
    if (size > 65536) {
        return 0;
    }

    /* Ensure shell mode system is initialized */
    static int initialized = 0;
    if (!initialized) {
        shell_mode_init();
        initialized = 1;
    }

    /* Create null-terminated string from input */
    char *input = malloc(size + 1);
    if (!input) {
        return 0;
    }
    memcpy(input, data, size);
    input[size] = '\0';

    /* Tokenize the input - consume all tokens */
    tokenizer_t *tokenizer = tokenizer_new(input);
    if (tokenizer) {
        token_t *token;
        int max_tokens = 100000; /* Prevent infinite loops */

        while (max_tokens-- > 0) {
            token = tokenizer_peek(tokenizer);
            if (!token || token->type == TOK_EOF) {
                break;
            }
            tokenizer_advance(tokenizer);
        }

        tokenizer_free(tokenizer);
    }

    free(input);
    return 0;
}

#ifdef FUZZ_AFL_MAIN
/**
 * @brief AFL++ main entry point
 *
 * When building for AFL++, we need a main() that reads from stdin
 * and calls the fuzz target. AFL++ uses fork server mode for efficiency.
 */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    /* Read input from stdin (AFL++ feeds input this way) */
    uint8_t *buf = NULL;
    size_t capacity = 0;
    size_t size = 0;

    /* Read all input */
    while (1) {
        if (size >= capacity) {
            capacity = capacity ? capacity * 2 : 4096;
            uint8_t *new_buf = realloc(buf, capacity);
            if (!new_buf) {
                free(buf);
                return 1;
            }
            buf = new_buf;
        }

        size_t bytes_read = fread(buf + size, 1, capacity - size, stdin);
        if (bytes_read == 0) {
            break;
        }
        size += bytes_read;
    }

    if (buf) {
        LLVMFuzzerTestOneInput(buf, size);
        free(buf);
    }

    return 0;
}
#endif /* FUZZ_AFL_MAIN */
