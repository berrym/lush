/**
 * @file fuzz_parser.c
 * @brief Fuzz target for lush shell parser
 *
 * This file provides fuzz testing for the parser using either libFuzzer
 * (Clang) or AFL++ (GCC/Clang). The goal is to find crashes, memory errors,
 * and assertion failures in the parser by feeding it random/mutated input.
 *
 * Build with libFuzzer (Clang only):
 *   CC=clang meson setup build -Denable_fuzzing=true -Dfuzzer=libfuzzer
 *   meson compile -C build fuzz_parser
 *   ./build/fuzz_parser tests/fuzz/corpus/parser/ -max_len=4096
 *
 * Build with AFL++ (GCC or Clang):
 *   CC=afl-clang-fast meson setup build -Denable_fuzzing=true -Dfuzzer=afl
 *   meson compile -C build fuzz_parser_afl
 *   afl-fuzz -i tests/fuzz/corpus/parser/ -o findings/ --
 * ./build/fuzz_parser_afl
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"
#include "parser.h"
#include "shell_mode.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief libFuzzer entry point
 *
 * Called by libFuzzer with mutated input data. We parse the input
 * and free any resulting AST. The fuzzer will detect crashes, timeouts,
 * memory leaks (with ASan), and undefined behavior (with UBSan).
 *
 * @param data Raw input bytes from fuzzer
 * @param size Size of input data
 * @return 0 (required by libFuzzer API)
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Limit input size to prevent OOM on huge inputs
    if (size > 65536) {
        return 0;
    }

    // Ensure shell mode system is initialized
    static int initialized = 0;
    if (!initialized) {
        shell_mode_init();
        initialized = 1;
    }

    // Create null-terminated string from input
    char *input = malloc(size + 1);
    if (!input) {
        return 0;
    }
    memcpy(input, data, size);
    input[size] = '\0';

    // Parse the input - this is what we're fuzzing
    parser_t *parser = parser_new(input);
    if (parser) {
        node_t *ast = parser_parse(parser);
        if (ast) {
            free_node_tree(ast);
        }
        parser_free(parser);
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

    // Read input from stdin (AFL++ feeds input this way)
    uint8_t *buf = NULL;
    size_t capacity = 0;
    size_t size = 0;

    // Read all input
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
#endif // FUZZ_AFL_MAIN
