/**
 * @file dirstack.c
 * @brief Directory stack implementation
 *
 * Implements a stack of directories for pushd/popd functionality.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "dirstack.h"
#include "lle/lle_pager.h"
#include "lle/unicode_compare.h"
#include "symtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Directory stack entries */
static char *stack[DIRSTACK_MAX];

/** Current top of stack (-1 means empty) */
static int stack_top = -1;

/** Flag indicating if stack is initialized */
static bool initialized = false;

void dirstack_init(void) {
    if (initialized) {
        return;
    }

    for (int i = 0; i < DIRSTACK_MAX; i++) {
        stack[i] = NULL;
    }
    stack_top = -1;
    initialized = true;
}

void dirstack_cleanup(void) {
    if (!initialized) {
        return;
    }

    for (int i = 0; i <= stack_top; i++) {
        free(stack[i]);
        stack[i] = NULL;
    }
    stack_top = -1;
    initialized = false;
}

int dirstack_push(const char *dir) {
    if (!initialized) {
        dirstack_init();
    }

    if (!dir) {
        return -1;
    }

    if (stack_top >= DIRSTACK_MAX - 1) {
        fprintf(stderr, "pushd: directory stack full\n");
        return -1;
    }

    char *copy = strdup(dir);
    if (!copy) {
        return -1;
    }

    stack_top++;
    stack[stack_top] = copy;

    dirstack_sync_variable();
    return 0;
}

char *dirstack_pop(void) {
    if (!initialized || stack_top < 0) {
        return NULL;
    }

    char *dir = stack[stack_top];
    stack[stack_top] = NULL;
    stack_top--;

    dirstack_sync_variable();
    return dir;
}

const char *dirstack_peek(int n) {
    if (!initialized || stack_top < 0) {
        return NULL;
    }

    /// Convert index from top to array index
    int idx = stack_top - n;
    if (idx < 0 || idx > stack_top) {
        return NULL;
    }

    return stack[idx];
}

int dirstack_remove(int n) {
    if (!initialized || stack_top < 0) {
        return -1;
    }

    int idx = stack_top - n;
    if (idx < 0 || idx > stack_top) {
        return -1;
    }

    /// Free the entry being removed
    free(stack[idx]);

    /// Shift remaining entries down
    for (int i = idx; i < stack_top; i++) {
        stack[i] = stack[i + 1];
    }
    stack[stack_top] = NULL;
    stack_top--;

    dirstack_sync_variable();
    return 0;
}

int dirstack_remove_matching(const char *dir) {
    if (!initialized || stack_top < 0 || !dir) {
        return 0;
    }

    int removed = 0;
    /// Scan the array from the base upward. Paths compare with NFC
    /// normalization so a decomposed and a precomposed spelling of the same
    /// directory count as one. Removing an entry shifts the higher entries
    /// down, so the same index is re-tested after a removal.
    for (int idx = 0; idx <= stack_top;) {
        if (lle_unicode_strings_equal(stack[idx], dir,
                                      &LLE_UNICODE_COMPARE_DEFAULT)) {
            free(stack[idx]);
            for (int i = idx; i < stack_top; i++) {
                stack[i] = stack[i + 1];
            }
            stack[stack_top] = NULL;
            stack_top--;
            removed++;
        } else {
            idx++;
        }
    }

    if (removed > 0) {
        dirstack_sync_variable();
    }
    return removed;
}

void dirstack_clear(void) {
    if (!initialized) {
        return;
    }

    for (int i = 0; i <= stack_top; i++) {
        free(stack[i]);
        stack[i] = NULL;
    }
    stack_top = -1;

    dirstack_sync_variable();
}

int dirstack_size(void) {
    if (!initialized) {
        return 0;
    }
    return stack_top + 1;
}

int dirstack_rotate(int n) {
    if (!initialized || stack_top < 0) {
        return -1;
    }

    /// Handle negative indices (from bottom)
    int idx;
    if (n >= 0) {
        idx = stack_top - n;
    } else {
        idx = -n - 1;
    }

    if (idx < 0 || idx > stack_top) {
        return -1;
    }

    if (idx == stack_top) {
        /// Already at top, nothing to do
        return 0;
    }

    /// Save the entry to rotate
    char *entry = stack[idx];

    /// Shift entries down
    for (int i = idx; i < stack_top; i++) {
        stack[i] = stack[i + 1];
    }

    /// Put the rotated entry at top
    stack[stack_top] = entry;

    dirstack_sync_variable();
    return 0;
}

void dirstack_print(bool one_per_line, bool show_index) {
    /// Buffer the listing through open_memstream and hand it to
    /// lle_pager_present so deeply pushed stacks paginate in
    /// interactive shells. The single-line form is short by
    /// definition and will stream directly via the pager's
    /// fits-in-one-screen branch. On memstream allocation failure
    /// the per-iteration writes target stdout.
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *out = open_memstream(&buf, &buf_len);
    FILE *sink = out ? out : stdout;

    /// First print the current directory
    char *cwd = getcwd(NULL, 0);
    if (cwd) {
        if (show_index) {
            fprintf(sink, " 0  %s", cwd);
        } else {
            fprintf(sink, "%s", cwd);
        }
        free(cwd);
    }

    /// Then print the stack (top to bottom)
    for (int i = stack_top; i >= 0; i--) {
        if (one_per_line) {
            fputc('\n', sink);
            if (show_index) {
                fprintf(sink, "%2d  %s", stack_top - i + 1, stack[i]);
            } else {
                fprintf(sink, "%s", stack[i]);
            }
        } else {
            fprintf(sink, " %s", stack[i]);
        }
    }
    fputc('\n', sink);

    if (out) {
        fclose(out);
        lle_pager_present(NULL, buf);
        free(buf);
    }
}

void dirstack_sync_variable(void) {
    /// Clear existing DIRSTACK array
    symtable_unset_global("DIRSTACK");

    if (!initialized || stack_top < 0) {
        return;
    }

    /// Create DIRSTACK as an indexed array with stack contents
    /// Stack top is index 0, bottom is highest index
    for (int i = stack_top; i >= 0; i--) {
        int array_idx = stack_top - i;
        char idx_str[16];
        snprintf(idx_str, sizeof(idx_str), "%d", array_idx);
        symtable_set_array_element("DIRSTACK", idx_str, stack[i]);
    }
}
