/**
 * @file test_lle_syntax_highlighting.c
 * @brief Unit tests for spec-compliant LLE Syntax Highlighting
 */

#include "lle/syntax_highlighting.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_framework.h"

#define TEST_START(name) ((void)0)
#define TEST_PASS() ((void)0)
#define TEST_FAIL(msg) TEST_FAIL_MSG(msg)

/* Stubs for lush core functions */
bool is_builtin(const char *name) {
    static const char *builtins[] = {
        "cd",       "echo", "exit",  "export", "alias",   "unalias",
        "history",  "jobs", "fg",    "bg",     "kill",    "pwd",
        "source",   "set",  "unset", "config", "display", "theme",
        "ehistory", "type", "help",  "read",   "test",    "true",
        "false",    "exec", "eval",  "shift",  "return",  "break",
        "continue", "wait", "trap",  "umask",  NULL};
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(name, builtins[i]) == 0)
            return true;
    }
    return false;
}

char *lookup_alias(const char *name) {
    (void)name;
    return NULL; /* No aliases in test */
}

/* Token type to string for debugging */
static const char *token_type_str(lle_syntax_token_type_t type) {
    switch (type) {
    case LLE_TOKEN_COMMAND_VALID:
        return "COMMAND_VALID";
    case LLE_TOKEN_COMMAND_INVALID:
        return "COMMAND_INVALID";
    case LLE_TOKEN_COMMAND_BUILTIN:
        return "COMMAND_BUILTIN";
    case LLE_TOKEN_COMMAND_ALIAS:
        return "COMMAND_ALIAS";
    case LLE_TOKEN_KEYWORD:
        return "KEYWORD";
    case LLE_TOKEN_STRING_SINGLE:
        return "STRING_SINGLE";
    case LLE_TOKEN_STRING_DOUBLE:
        return "STRING_DOUBLE";
    case LLE_TOKEN_VARIABLE:
        return "VARIABLE";
    case LLE_TOKEN_VARIABLE_SPECIAL:
        return "VARIABLE_SPECIAL";
    case LLE_TOKEN_PIPE:
        return "PIPE";
    case LLE_TOKEN_REDIRECT:
        return "REDIRECT";
    case LLE_TOKEN_COMMENT:
        return "COMMENT";
    case LLE_TOKEN_OPTION:
        return "OPTION";
    case LLE_TOKEN_ARGUMENT:
        return "ARGUMENT";
    case LLE_TOKEN_WHITESPACE:
        return "WHITESPACE";
    default:
        return "OTHER";
    }
}

/* Test helper: get first non-whitespace token type */
static lle_syntax_token_type_t
get_first_command_type(lle_syntax_highlighter_t *h, const char *input) {
    lle_syntax_highlight(h, input, strlen(input));
    size_t count;
    const lle_syntax_token_t *tokens = lle_syntax_get_tokens(h, &count);
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type != LLE_TOKEN_WHITESPACE) {
            return tokens[i].type;
        }
    }
    return LLE_TOKEN_UNKNOWN;
}

/* Test: Highlighter creation */
TEST(highlighter_create) {
    TEST_START("highlighter_create");
    lle_syntax_highlighter_t *h = NULL;
    int rc = lle_syntax_highlighter_create(&h);
    if (rc == 0 && h != NULL) {
        lle_syntax_highlighter_destroy(h);
        TEST_PASS();
    } else {
        TEST_FAIL("failed to create highlighter");
    }
}

/* Test: Builtin detection */
TEST(builtins) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    /* Classic builtins */
    TEST_START("builtin: cd");
    if (get_first_command_type(h, "cd") == LLE_TOKEN_COMMAND_BUILTIN)
        TEST_PASS();
    else
        TEST_FAIL("cd not detected as builtin");

    TEST_START("builtin: echo");
    if (get_first_command_type(h, "echo hello") == LLE_TOKEN_COMMAND_BUILTIN)
        TEST_PASS();
    else
        TEST_FAIL("echo not detected as builtin");

    /* Lush-specific builtins */
    TEST_START("builtin: config");
    if (get_first_command_type(h, "config") == LLE_TOKEN_COMMAND_BUILTIN)
        TEST_PASS();
    else
        TEST_FAIL("config not detected as builtin");

    TEST_START("builtin: display");
    if (get_first_command_type(h, "display") == LLE_TOKEN_COMMAND_BUILTIN)
        TEST_PASS();
    else
        TEST_FAIL("display not detected as builtin");

    TEST_START("builtin: theme");
    if (get_first_command_type(h, "theme") == LLE_TOKEN_COMMAND_BUILTIN)
        TEST_PASS();
    else
        TEST_FAIL("theme not detected as builtin");

    TEST_START("builtin: ehistory");
    if (get_first_command_type(h, "ehistory") == LLE_TOKEN_COMMAND_BUILTIN)
        TEST_PASS();
    else
        TEST_FAIL("ehistory not detected as builtin");

    lle_syntax_highlighter_destroy(h);
}

/* Test: External commands */
TEST(external_commands) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    TEST_START("external: ls");
    lle_syntax_token_type_t type = get_first_command_type(h, "ls");
    if (type == LLE_TOKEN_COMMAND_VALID)
        TEST_PASS();
    else {
        printf("(got %s) ", token_type_str(type));
        TEST_FAIL("ls not detected as valid command");
    }

    TEST_START("external: grep");
    type = get_first_command_type(h, "grep foo");
    if (type == LLE_TOKEN_COMMAND_VALID)
        TEST_PASS();
    else {
        printf("(got %s) ", token_type_str(type));
        TEST_FAIL("grep not detected as valid command");
    }

    lle_syntax_highlighter_destroy(h);
}

/* Test: Invalid commands */
TEST(invalid_commands) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    TEST_START("invalid: notarealcmd123");
    lle_syntax_token_type_t type = get_first_command_type(h, "notarealcmd123");
    if (type == LLE_TOKEN_COMMAND_INVALID)
        TEST_PASS();
    else {
        printf("(got %s) ", token_type_str(type));
        TEST_FAIL("fake command not detected as invalid");
    }

    lle_syntax_highlighter_destroy(h);
}

/* Test: Keywords */
TEST(keywords) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    TEST_START("keyword: if");
    if (get_first_command_type(h, "if") == LLE_TOKEN_KEYWORD)
        TEST_PASS();
    else
        TEST_FAIL("if not detected as keyword");

    TEST_START("keyword: for");
    if (get_first_command_type(h, "for") == LLE_TOKEN_KEYWORD)
        TEST_PASS();
    else
        TEST_FAIL("for not detected as keyword");

    TEST_START("keyword: while");
    if (get_first_command_type(h, "while") == LLE_TOKEN_KEYWORD)
        TEST_PASS();
    else
        TEST_FAIL("while not detected as keyword");

    lle_syntax_highlighter_destroy(h);
}

/* Test: Pipes and operators */
TEST(operators) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    TEST_START("pipe in 'ls | grep'");
    lle_syntax_highlight(h, "ls | grep foo", 13);
    size_t count;
    const lle_syntax_token_t *tokens = lle_syntax_get_tokens(h, &count);
    bool found_pipe = false;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == LLE_TOKEN_PIPE)
            found_pipe = true;
    }
    if (found_pipe)
        TEST_PASS();
    else
        TEST_FAIL("pipe not detected");

    lle_syntax_highlighter_destroy(h);
}

/* Test: Variables */
TEST(variables) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    TEST_START("variable: $HOME");
    lle_syntax_highlight(h, "echo $HOME", 10);
    size_t count;
    const lle_syntax_token_t *tokens = lle_syntax_get_tokens(h, &count);
    bool found_var = false;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == LLE_TOKEN_VARIABLE)
            found_var = true;
    }
    if (found_var)
        TEST_PASS();
    else
        TEST_FAIL("variable not detected");

    TEST_START("special variable: $?");
    lle_syntax_highlight(h, "echo $?", 7);
    tokens = lle_syntax_get_tokens(h, &count);
    bool found_special = false;
    for (size_t i = 0; i < count; i++) {
        if (tokens[i].type == LLE_TOKEN_VARIABLE_SPECIAL)
            found_special = true;
    }
    if (found_special)
        TEST_PASS();
    else
        TEST_FAIL("special variable not detected");

    lle_syntax_highlighter_destroy(h);
}

/* Find the first path-shaped token (any of the seven PATH_* types).
 * Used by the path-classification tests below to inspect the slot the
 * classifier assigned without depending on argv positioning. */
static lle_syntax_token_type_t find_path_token(lle_syntax_highlighter_t *h) {
    size_t count;
    const lle_syntax_token_t *tokens = lle_syntax_get_tokens(h, &count);
    for (size_t i = 0; i < count; i++) {
        switch (tokens[i].type) {
        case LLE_TOKEN_PATH_FILE_ABSOLUTE:
        case LLE_TOKEN_PATH_FILE_RELATIVE:
        case LLE_TOKEN_PATH_FILE_HOME:
        case LLE_TOKEN_PATH_DIR_ABSOLUTE:
        case LLE_TOKEN_PATH_DIR_RELATIVE:
        case LLE_TOKEN_PATH_DIR_HOME:
        case LLE_TOKEN_PATH_INVALID:
            return tokens[i].type;
        default:
            break;
        }
    }
    return LLE_TOKEN_UNKNOWN;
}

/* Resolved color for the first path-shaped token in the most recent
 * highlight pass. Used to verify the kind-only/shape-specific fallback
 * chain. Returns 0 if no path token was emitted. */
static uint32_t find_path_color(lle_syntax_highlighter_t *h) {
    size_t count;
    const lle_syntax_token_t *tokens = lle_syntax_get_tokens(h, &count);
    for (size_t i = 0; i < count; i++) {
        switch (tokens[i].type) {
        case LLE_TOKEN_PATH_FILE_ABSOLUTE:
        case LLE_TOKEN_PATH_FILE_RELATIVE:
        case LLE_TOKEN_PATH_FILE_HOME:
        case LLE_TOKEN_PATH_DIR_ABSOLUTE:
        case LLE_TOKEN_PATH_DIR_RELATIVE:
        case LLE_TOKEN_PATH_DIR_HOME:
        case LLE_TOKEN_PATH_INVALID:
            return tokens[i].color;
        default:
            break;
        }
    }
    return 0;
}

/* Make a unique tmp dir for path-classification fixtures. Returns a
 * heap-allocated path the caller frees, or NULL on failure. The dir
 * is deletable with rmdir() once empty. */
static char *make_test_dir(void) {
    char tmpl[] = "/tmp/lush_hl_XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        return NULL;
    }
    return strdup(dir);
}

/* Test: directory-shaped argument resolves to PATH_DIR_ABSOLUTE.
 * Regression catch: dropping S_ISDIR or routing all valid paths to
 * a single token would collapse this into FILE_ABSOLUTE. */
TEST(path_directory_absolute) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    if (!dir) {
        TEST_FAIL("could not create test directory");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    char input[256];
    snprintf(input, sizeof(input), "ls %s", dir);
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);
    if (type == LLE_TOKEN_PATH_DIR_ABSOLUTE) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected PATH_DIR_ABSOLUTE, got token type %d", (int)type);
        TEST_FAIL(msg);
    }

    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: regular-file argument resolves to PATH_FILE_ABSOLUTE.
 * Regression catch: failing to distinguish files from directories
 * (e.g., reverting to the old PATH_VALID single-bucket model) would
 * silently produce DIR_ABSOLUTE here. */
TEST(path_file_absolute) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    if (!dir) {
        TEST_FAIL("could not create test directory");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    char file[256];
    snprintf(file, sizeof(file), "%s/regular_file.txt", dir);
    FILE *fp = fopen(file, "w");
    if (!fp) {
        TEST_FAIL("could not create test file");
        rmdir(dir);
        free(dir);
        lle_syntax_highlighter_destroy(h);
        return;
    }
    fclose(fp);

    char input[512];
    snprintf(input, sizeof(input), "cat %s", file);
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);
    if (type == LLE_TOKEN_PATH_FILE_ABSOLUTE) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected PATH_FILE_ABSOLUTE, got token type %d", (int)type);
        TEST_FAIL(msg);
    }

    unlink(file);
    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: missing path resolves to PATH_INVALID, regardless of shape. */
TEST(path_invalid_missing) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    /* This path can never collide with a real fixture -- the random
     * suffix is chosen at compile time. */
    const char *input = "cat /tmp/lush_definitely_not_present_z9q3pq/x";
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);
    if (type == LLE_TOKEN_PATH_INVALID) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected PATH_INVALID, got token type %d",
                 (int)type);
        TEST_FAIL(msg);
    }

    lle_syntax_highlighter_destroy(h);
}

/* Test: backslash-escaped spaces in path-shaped argument dequote
 * before stat. Without dequote_unquoted_path this fails -- stat sees
 * the literal `\ ` and returns ENOENT, classifying a valid file as
 * PATH_INVALID. This is the regression #90 carries into the
 * highlighter. */
TEST(path_dequote_backslash_space) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    if (!dir) {
        TEST_FAIL("could not create test directory");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    /* Create a file whose name contains spaces, exactly the case
     * users hit when invoking `cat /tmp/foo/a\ test\ file.txt`. */
    char file[512];
    snprintf(file, sizeof(file), "%s/a test file.txt", dir);
    FILE *fp = fopen(file, "w");
    if (!fp) {
        TEST_FAIL("could not create file with spaces in name");
        rmdir(dir);
        free(dir);
        lle_syntax_highlighter_destroy(h);
        return;
    }
    fclose(fp);

    /* Build the highlight input with backslash-escaped spaces. */
    char input[512];
    snprintf(input, sizeof(input), "cat %s/a\\ test\\ file.txt", dir);
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);
    if (type == LLE_TOKEN_PATH_FILE_ABSOLUTE) {
        TEST_PASS();
    } else {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "expected PATH_FILE_ABSOLUTE for `a\\ test\\ file.txt`, "
                 "got %d (likely missing dequote)",
                 (int)type);
        TEST_FAIL(msg);
    }

    unlink(file);
    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: relative-shape directory keeps the RELATIVE bucket, even
 * though the resolved target may match an absolute one. The shape
 * dimension is anchored to what the user typed.
 *
 * `./` triggers the path-shape detector (which keys on `has_slash`);
 * the highlighter today ignores slashless `.` / `~`, so the test
 * uses a slash-bearing form to exercise the classifier. */
TEST(path_shape_relative_dir) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    if (!dir) {
        TEST_FAIL("could not create test directory");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    /* chdir into the parent of the test dir so `./<basename>` is a
     * valid relative path to a real directory. */
    char *saved_cwd = getcwd(NULL, 0);
    if (chdir("/tmp") != 0) {
        free(saved_cwd);
        rmdir(dir);
        free(dir);
        TEST_FAIL("could not chdir to /tmp");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    const char *basename = strrchr(dir, '/');
    basename = basename ? basename + 1 : dir;

    char input[256];
    snprintf(input, sizeof(input), "ls ./%s", basename);
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);

    /* Restore cwd before any assertion path that would skip cleanup. */
    if (saved_cwd) {
        chdir(saved_cwd);
        free(saved_cwd);
    }

    if (type == LLE_TOKEN_PATH_DIR_RELATIVE) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected PATH_DIR_RELATIVE for `./<dir>`, got %d", (int)type);
        TEST_FAIL(msg);
    }

    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: home-shape directory (`~/sub`) preserves the HOME shape
 * bucket even after expansion against $HOME. Like the relative test,
 * uses a slash-bearing form -- `~` alone is not flagged as path-shape
 * by the highlighter's `has_slash`-keyed detector. */
TEST(path_shape_home_dir) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    if (!dir) {
        TEST_FAIL("could not create test directory");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    /* Create a subdir inside the temp dir, then point HOME at the
     * temp dir so `~/sub` resolves to it. */
    char sub[512];
    snprintf(sub, sizeof(sub), "%s/sub", dir);
    if (mkdir(sub, 0755) != 0) {
        rmdir(dir);
        free(dir);
        TEST_FAIL("could not create subdir under test dir");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    const char *saved_home = getenv("HOME");
    char *saved_copy = saved_home ? strdup(saved_home) : NULL;
    setenv("HOME", dir, 1);

    const char *input = "ls ~/sub";
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);

    if (saved_copy) {
        setenv("HOME", saved_copy, 1);
        free(saved_copy);
    } else {
        unsetenv("HOME");
    }

    if (type == LLE_TOKEN_PATH_DIR_HOME) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected PATH_DIR_HOME for `~/sub`, got %d",
                 (int)type);
        TEST_FAIL(msg);
    }

    rmdir(sub);
    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: bare `.` (no slash, no expansion) is classified as a path. The
 * default `has_slash` heuristic skips it; is_implicit_path_word lets
 * it through and the classifier resolves it to PATH_DIR_RELATIVE
 * because cwd always exists as a directory. */
TEST(path_implicit_dot) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    const char *input = "ls .";
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);
    if (type == LLE_TOKEN_PATH_DIR_RELATIVE) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected PATH_DIR_RELATIVE for bare `.`, got %d", (int)type);
        TEST_FAIL(msg);
    }

    lle_syntax_highlighter_destroy(h);
}

/* Test: bare `..` resolves to PATH_DIR_RELATIVE. `..` is always a
 * directory (parent of cwd; equals cwd when cwd is `/`). */
TEST(path_implicit_dotdot) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    const char *input = "ls ..";
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);
    if (type == LLE_TOKEN_PATH_DIR_RELATIVE) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected PATH_DIR_RELATIVE for bare `..`, got %d", (int)type);
        TEST_FAIL(msg);
    }

    lle_syntax_highlighter_destroy(h);
}

/* Test: bare `~` resolves to PATH_DIR_HOME. Uses a known temp dir as
 * $HOME so the test is independent of the runner's actual home. */
TEST(path_implicit_tilde_alone) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    if (!dir) {
        TEST_FAIL("could not create test directory");
        lle_syntax_highlighter_destroy(h);
        return;
    }

    const char *saved_home = getenv("HOME");
    char *saved_copy = saved_home ? strdup(saved_home) : NULL;
    setenv("HOME", dir, 1);

    const char *input = "ls ~";
    lle_syntax_highlight(h, input, strlen(input));

    lle_syntax_token_type_t type = find_path_token(h);

    if (saved_copy) {
        setenv("HOME", saved_copy, 1);
        free(saved_copy);
    } else {
        unsetenv("HOME");
    }

    if (type == LLE_TOKEN_PATH_DIR_HOME) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected PATH_DIR_HOME for bare `~`, got %d", (int)type);
        TEST_FAIL(msg);
    }

    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: shape-specific color knob takes precedence over the kind-only
 * fallback. Sets both path_file and path_file_absolute and verifies
 * an absolute file path resolves to the specific knob's value. */
TEST(path_color_shape_specific_overrides_kind) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    char file[256];
    snprintf(file, sizeof(file), "%s/x", dir);
    FILE *fp = fopen(file, "w");
    fclose(fp);

    /* Apply distinct colors so we can tell which knob won the chain. */
    lle_syntax_colors_t override = {0};
    override.path_file = 0x00111111;          /* kind-only fallback */
    override.path_file_absolute = 0x00222222; /* shape-specific */
    lle_syntax_highlighter_set_colors(h, &override);

    char input[512];
    snprintf(input, sizeof(input), "cat %s", file);
    lle_syntax_highlight(h, input, strlen(input));

    uint32_t color = find_path_color(h);
    if (color == 0x00222222) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected shape-specific color 0x222222, got 0x%06X", color);
        TEST_FAIL(msg);
    }

    unlink(file);
    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: when the shape-specific knob is unset, the kind-only knob
 * resolves the color. */
TEST(path_color_falls_to_kind_only) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    char *dir = make_test_dir();
    char file[256];
    snprintf(file, sizeof(file), "%s/x", dir);
    FILE *fp = fopen(file, "w");
    fclose(fp);

    lle_syntax_colors_t override = {0};
    override.path_file = 0x00111111;
    /* path_file_absolute deliberately left at 0 (unset) */
    lle_syntax_highlighter_set_colors(h, &override);

    char input[512];
    snprintf(input, sizeof(input), "cat %s", file);
    lle_syntax_highlight(h, input, strlen(input));

    uint32_t color = find_path_color(h);
    if (color == 0x00111111) {
        TEST_PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected kind-only color 0x111111, got 0x%06X", color);
        TEST_FAIL(msg);
    }

    unlink(file);
    rmdir(dir);
    free(dir);
    lle_syntax_highlighter_destroy(h);
}

/* Test: ANSI rendering */
TEST(ansi_render) {
    lle_syntax_highlighter_t *h = NULL;
    lle_syntax_highlighter_create(&h);

    TEST_START("ANSI render output");
    lle_syntax_highlight(h, "echo hello", 10);
    char output[1024];
    int len = lle_syntax_render_ansi(h, "echo hello", output, sizeof(output));
    if (len > 0 && strlen(output) > 0) {
        printf("(rendered %d bytes) ", len);
        TEST_PASS();
    } else {
        TEST_FAIL("render returned no output");
    }

    lle_syntax_highlighter_destroy(h);
}

int main(void) {
    printf("=== LLE Syntax Highlighting Unit Tests ===\n\n");

    RUN_TEST(highlighter_create);
    RUN_TEST(builtins);
    RUN_TEST(external_commands);
    RUN_TEST(invalid_commands);
    RUN_TEST(keywords);
    RUN_TEST(operators);
    RUN_TEST(variables);
    RUN_TEST(path_directory_absolute);
    RUN_TEST(path_file_absolute);
    RUN_TEST(path_invalid_missing);
    RUN_TEST(path_dequote_backslash_space);
    RUN_TEST(path_shape_relative_dir);
    RUN_TEST(path_shape_home_dir);
    RUN_TEST(path_implicit_dot);
    RUN_TEST(path_implicit_dotdot);
    RUN_TEST(path_implicit_tilde_alone);
    RUN_TEST(path_color_shape_specific_overrides_kind);
    RUN_TEST(path_color_falls_to_kind_only);
    RUN_TEST(ansi_render);
    return TEST_RESULT();
}
