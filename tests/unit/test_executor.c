/**
 * @file test_executor.c
 * @brief Integration tests for the shell executor
 *
 * These tests exercise the executor through actual command execution,
 * covering builtins, pipelines, control structures, and variable expansion.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "alias.h"
#include "executor.h"
#include "identifier.h"
#include "lle/buffer_management.h"
#include "lle/completion/builtin_completions.h"
#include "lle/completion/completion_types.h"
#include "lle/completion/word_context.h"
#include "lle/lle_editor.h"
#include "lle/syntax_highlighting.h"
#include "shell_mode.h"
#include "symtable.h"
#include "test_shell_harness.h"

extern lle_result_t lush_inspect_widget_callback(lle_editor_t *editor,
                                                 void *user_data);

/// Side-table resets for bindkey/zle tests; the side tables are
/// process-global so leaking state across tests breaks reproducibility.
extern void bindkey_table_reset(void);
extern void zle_widget_table_reset(void);
extern void zstyle_table_reset(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * LIFECYCLE TESTS
 * ============================================================================
 */

TEST(executor_new_free) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new should return non-NULL");
    ASSERT_EQ(exec->exit_status, 0, "Initial exit status should be 0");
    ASSERT(!exec->has_error, "Should not have error initially");
    executor_free(exec);
}

TEST(executor_with_symtable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    executor_t *exec = executor_new_with_symtable(mgr);
    ASSERT_NOT_NULL(exec, "executor_new_with_symtable failed");
    ASSERT(exec->symtable == mgr, "Symtable should be set");

    executor_free(exec);
    symtable_manager_free(mgr);
}

/* ============================================================================
 * SIMPLE COMMAND TESTS
 * ============================================================================
 */

TEST(execute_true) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "true");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_EQ(exec->exit_status, 0, "Exit status should be 0");

    executor_free(exec);
}

TEST(execute_false) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "false");
    ASSERT_EXIT_STATUS(r, 1);
    ASSERT_EQ(exec->exit_status, 1, "Exit status should be 1");

    executor_free(exec);
}

TEST(execute_colon) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, ":");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(execute_exit_status) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "true", 1);
    (void)executor_execute_command_line(exec, "echo $?", 1);
    ASSERT_EQ(exec->exit_status, 0, "echo should succeed");

    executor_free(exec);
}

/* ============================================================================
 * VARIABLE TESTS
 * ============================================================================
 */

TEST(variable_assignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "FOO=bar");
    ASSERT_EXIT_STATUS(r, 0);

    /// Verify variable was set
    char *value = symtable_get_var(exec->symtable, "FOO");
    ASSERT_NOT_NULL(value, "Variable should be set");
    ASSERT_STR_EQ(value, "bar", "Variable value mismatch");
    free(value);

    executor_free(exec);
}

TEST(variable_expansion) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "MYVAR=hello", 1);

    /// Test that variable exists
    char *value = symtable_get_var(exec->symtable, "MYVAR");
    ASSERT_NOT_NULL(value, "Variable should be set");
    ASSERT_STR_EQ(value, "hello", "Variable value mismatch");
    free(value);

    executor_free(exec);
}

TEST(multiple_assignments) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "A=1", 1);
    executor_execute_command_line(exec, "B=2", 1);
    executor_execute_command_line(exec, "C=3", 1);

    char *a = symtable_get_var(exec->symtable, "A");
    char *b = symtable_get_var(exec->symtable, "B");
    char *c = symtable_get_var(exec->symtable, "C");

    ASSERT_STR_EQ(a, "1", "A should be 1");
    ASSERT_STR_EQ(b, "2", "B should be 2");
    ASSERT_STR_EQ(c, "3", "C should be 3");

    free(a);
    free(b);
    free(c);
    executor_free(exec);
}

/* ============================================================================
 * CONTROL STRUCTURE TESTS
 * ============================================================================
 */

TEST(if_true_branch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "if true; then RESULT=yes; else RESULT=no; fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "yes", "Should take true branch");
    free(result);

    executor_free(exec);
}

TEST(if_false_branch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "if false; then RESULT=yes; else RESULT=no; fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "no", "Should take false branch");
    free(result);

    executor_free(exec);
}

TEST(for_loop_basic) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "COUNT=0; for i in 1 2 3; do COUNT=$((COUNT+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_NOT_NULL(count, "COUNT should be set");
    ASSERT_STR_EQ(count, "3", "Should iterate 3 times");
    free(count);

    executor_free(exec);
}

TEST(for_loop_no_in) {
    /// Tests Issue #55 fix - for without 'in' iterates over $@
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Set positional parameters and iterate
    run_result_t r = run_shell_with_executor(
        exec, "set -- a b c; COUNT=0; for arg; do COUNT=$((COUNT+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_NOT_NULL(count, "COUNT should be set");
    ASSERT_STR_EQ(count, "3", "Should iterate over 3 positional params");
    free(count);

    executor_free(exec);
}

TEST(for_loop_quoted_empty_word_posix) {
    /// A quoted empty "" is a real empty field and must be iterated, not
    /// dropped during for-list word splitting (posix mode). Issue #127.
    run_result_t r = run_shell("mode posix\n"
                               "for w in a \"\" b; do echo \"[$w]\"; done\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "[a]\n[]\n[b]\n");
}

TEST(for_loop_quoted_string_not_split_posix) {
    /// A quoted "$x" stays one word even when it holds IFS characters;
    /// word splitting applies only to unquoted expansions. Issue #127.
    run_result_t r = run_shell("mode posix\n"
                               "x=\"a b c\"\n"
                               "for w in \"$x\"; do echo \"[$w]\"; done\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "[a b c]\n");
}

TEST(for_loop_unquoted_var_splits_posix) {
    /// Regression guard: an unquoted $x still field-splits on IFS.
    run_result_t r = run_shell("mode posix\n"
                               "IFS=:\n"
                               "x=a:b:c\n"
                               "for w in $x; do echo \"[$w]\"; done\n");
    /// run_shell shares global shell state; restore the mode and the
    /// default IFS so this test does not leak `:` splitting into later
    /// tests.
    run_shell("mode lush\nunset IFS\n");
    ASSERT_STDOUT_EQ(r, "[a]\n[b]\n[c]\n");
}

TEST(for_loop_unquoted_empty_no_iteration_posix) {
    /// Regression guard: an unquoted empty expansion produces zero
    /// fields (zero iterations), not one empty word.
    run_result_t r = run_shell("mode posix\n"
                               "x=\n"
                               "for w in $x; do echo iter; done\n"
                               "echo done\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "done\n");
}

TEST(while_loop) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "N=0; while [ $N -lt 5 ]; do N=$((N+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "5", "Should count to 5");
    free(n);

    executor_free(exec);
}

TEST(until_loop) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "N=0; until [ $N -ge 3 ]; do N=$((N+1)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "3", "Should count to 3");
    free(n);

    executor_free(exec);
}

TEST(case_statement) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "X=foo; case $X in foo) RESULT=matched;; bar) RESULT=bar;; esac");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "matched", "Should match foo pattern");
    free(result);

    executor_free(exec);
}

TEST(rt_case_continue_polyglot_spellings) {
    /// The "test the remaining patterns" case terminator has two spellings --
    /// bash's `;;&` and zsh's `;|` -- and lush accepts both for the one
    /// canonical behavior (spelling is polyglot; behavior is canonical lush).
    /// Both must fall through to re-test the next pattern.
    run_result_t bash_spelling =
        run_shell("case x in x) echo one;;& x) echo two;; esac\n");
    ASSERT_STDOUT_EQ(bash_spelling, "one\ntwo\n");

    run_result_t zsh_spelling =
        run_shell("case x in x) echo one;| x) echo two;; esac\n");
    ASSERT_STDOUT_EQ(zsh_spelling, "one\ntwo\n");

    /// The plain `;;` terminator still stops after the first match.
    run_result_t plain =
        run_shell("case x in x) echo one;; x) echo two;; esac\n");
    ASSERT_STDOUT_EQ(plain, "one\n");
}

/// #631 Phase 3 C1: an associative-array key is canonicalized identically on
/// the write store and the read extractor, so a key written with an escaped or
/// quoted space is read back by the space-bearing form. Before the shared
/// subscript_normalize_key, the write stored the raw bytes (`a\ b`) while the
/// read looked up the space-bearing form, so the round-trip never matched.
/// Distinct array names per case: the process-global symtable persists arrays
/// across run_shell() calls within this test binary.
TEST(subscript_c1_assoc_escaped_key_roundtrip) {
    /// Escaped-space write, plain-space read -- the reported #631 failure.
    run_result_t r =
        run_shell("declare -A mA631; mA631[a\\ b]=9; echo \"${mA631[a b]}\"\n");
    ASSERT_STDOUT_EQ(r, "9\n");
}

TEST(subscript_c1_assoc_escaped_write_escaped_read) {
    /// Escaped write, escaped read: canonicalizes on both sides to the same
    /// `a b`, so this keeps matching (regression guard).
    run_result_t r = run_shell(
        "declare -A mB631; mB631[a\\ b]=9; echo \"${mB631[a\\ b]}\"\n");
    ASSERT_STDOUT_EQ(r, "9\n");
}

TEST(subscript_c1_assoc_singlequoted_read_matches) {
    /// A single-quoted read key normalizes to the same `a b` the escaped write
    /// stored -- the read-side quote stripping the old path lacked.
    run_result_t r = run_shell(
        "declare -A mC631; mC631[a\\ b]=9; echo \"${mC631['a b']}\"\n");
    ASSERT_STDOUT_EQ(r, "9\n");
}

TEST(subscript_c1_assoc_key_stored_canonical_length) {
    /// The stored key is the canonical 3-byte `a b`, not the 4-byte raw `a\ b`.
    /// Byte-length equality is the guard the atomic write+read fix must hold.
    run_result_t r = run_shell("declare -A mD631; mD631[a\\ b]=9; "
                               "for k in \"${!mD631[@]}\"; do echo \"${#k}\"; "
                               "done\n");
    ASSERT_STDOUT_EQ(r, "3\n");
}

TEST(subscript_c1_assoc_plain_key_unchanged) {
    /// A key with no quote/escape/$ normalizes to itself: no regression on the
    /// common case.
    run_result_t r =
        run_shell("declare -A mE631; mE631[key]=v; echo \"${mE631[key]}\"\n");
    ASSERT_STDOUT_EQ(r, "v\n");
}

TEST(subscript_c1_assoc_dollar_key_roundtrip) {
    /// `$`-expansion of the key survives on both sides (the normalizer subsumes
    /// the old expand_variable): the write keys on `foo`, and both a `$k` read
    /// and a literal `foo` read hit it.
    run_result_t r =
        run_shell("declare -A mF631; k=foo; mF631[$k]=v; "
                  "echo \"${mF631[foo]}\"; echo \"${mF631[$k]}\"\n");
    ASSERT_STDOUT_EQ(r, "v\nv\n");
}

TEST(subscript_c1_indexed_roundtrip_untouched) {
    /// The indexed path is untouched by C1 (only the associative branch routes
    /// through the normalizer): a variable index still round-trips.
    run_result_t r = run_shell(
        "declare -a nG631; i=1; nG631[$i]=z; echo \"${nG631[$i]}\"\n");
    ASSERT_STDOUT_EQ(r, "z\n");
}

TEST(subscript_c1_assoc_bracket_in_key_roundtrip) {
    /// A key containing an escaped `]` must round-trip: the write span
    /// (parser) and the read span (executor) both use scan_subscript_bounds,
    /// so neither truncates at the escaped `]`. Before the read-span migration
    /// the write stored `x]y` while the read's strchr truncated to `x\`.
    run_result_t r = run_shell(
        "declare -A mH631; mH631[x\\]y]=9; echo \"${mH631[x\\]y]}\"\n");
    ASSERT_STDOUT_EQ(r, "9\n");
}

TEST(subscript_c1_assoc_dollar_anywhere) {
    /// The normalizer expands `$`-units anywhere in the subscript (not only a
    /// whole-string `$name`, which is all the old expand_variable did). Write
    /// and read agree because both route through the one normalizer, so the
    /// key `pre$x` is stored and read as `prefoo`.
    run_result_t r = run_shell("declare -A mI631; x=foo; mI631[pre$x]=v; "
                               "echo \"${mI631[prefoo]}\"; "
                               "echo \"${mI631[pre$x]}\"\n");
    ASSERT_STDOUT_EQ(r, "v\nv\n");
}

TEST(subscript_c1_assoc_tilde_key_literal) {
    /// A subscript keys on a LITERAL `~` -- key normalization is not a word
    /// context, so no tilde expansion (the Phase-1 fold onto the shared
    /// expander suppresses it). Write and read agree on the literal `~x` key.
    run_result_t r = run_shell("declare -A mJ631; mJ631[~x]=v; "
                               "echo \"${mJ631[~x]}\"; "
                               "for k in \"${!mJ631[@]}\"; do echo \"$k\"; "
                               "done\n");
    ASSERT_STDOUT_EQ(r, "v\n~x\n");
}

/// #631 Phase 2c: a QUOTED subscript in command/assignment position now
/// tokenizes as one word (the tokenizer absorbs the `[...]` span), so the
/// quoted-write forms reach the C1 normalizer, while an argument-position
/// quoted subscript is dequoted and glob-suppressed by the parser.
TEST(subscript_2c_quoted_write_roundtrip) {
    run_result_t r =
        run_shell("declare -A m2A; m2A[\"a b\"]=9; echo \"${m2A[a b]}\"\n");
    ASSERT_STDOUT_EQ(r, "9\n");
}

TEST(subscript_2c_singlequoted_write_roundtrip) {
    run_result_t r =
        run_shell("declare -A m2B; m2B['a b']=9; echo \"${m2B[a b]}\"\n");
    ASSERT_STDOUT_EQ(r, "9\n");
}

TEST(subscript_2c_quoted_append) {
    run_result_t r =
        run_shell("declare -A m2C; m2C[\"a b\"]=1; m2C[\"a b\"]+=2; "
                  "echo \"${m2C[a b]}\"\n");
    ASSERT_STDOUT_EQ(r, "12\n");
}

TEST(subscript_2c_arg_glob_suppressed) {
    /// A quoted subscript in argument position is a quoted word: it stays
    /// literal (no globbing). If globbing were NOT suppressed, the `[a b]`
    /// character class with no on-disk match would vanish under NULLGLOB.
    run_result_t r = run_shell("echo m[\"a b\"]\n");
    ASSERT_STDOUT_EQ(r, "m[a b]\n");
    run_result_t r2 = run_shell("echo m['a b']\n");
    ASSERT_STDOUT_EQ(r2, "m[a b]\n");
}

TEST(subscript_2c_unquoted_arg_stays_broken) {
    /// Curation: an UNQUOTED spaced subscript is NOT absorbed (has_ws), so it
    /// splits at the space exactly as before -- quoting is required.
    run_result_t r = run_shell("echo m[a b]=v\n");
    ASSERT_STDOUT_EQ(r, "b]=v\n");
}

TEST(subscript_2c_undeclared_no_phantom) {
    /// Companion fix A: a quoted key on an UNDECLARED name reaches indexed
    /// assignment, the non-integer index is a targeted error, and no phantom
    /// empty array is left behind. Detect via declare -p's output (a phantom
    /// would print `declare -a mK2c=(...)`); the failing assignment's own error
    /// goes to stderr and does not affect the asserted stdout.
    run_result_t r = run_shell(
        "unset mK2c; mK2c[\"a b\"]=9; d=$(declare -p mK2c 2>/dev/null); "
        "if [ -n \"$d\" ]; then echo PHANTOM; else echo clean; fi\n");
    ASSERT_STDOUT_EQ(r, "clean\n");
}

TEST(subscript_2c_promoted_scalar_survives_bad_index) {
    /// The phantom rollback must roll back only a truly-fresh binding, never a
    /// scalar being promoted to an array (relaxed mode): unsetting the name
    /// would destroy the user's scalar on the error path. Subshell isolates the
    /// mode change from other tests.
    run_result_t r = run_shell(
        "( mode bash; mS2c=keepme; mS2c[\"a b\"]=9; echo \"[${mS2c[0]}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[keepme]\n");
}

TEST(rt_read_array_polyglot_spellings) {
    /// read into an indexed array has two spellings -- bash's `-a` and zsh's
    /// `-A` -- and lush accepts both for the one canonical behavior (spelling
    /// is polyglot; behavior is canonical lush). Both fill the same 0-indexed
    /// array; the combined raw forms `-rA` / `-Ar` also enable raw mode.
    run_result_t bash_spelling = run_shell(
        "printf 'a b c' | { read -a arr; echo \"${arr[0]}-${arr[1]}-${arr[2]} "
        "n=${#arr[@]}\"; }\n");
    ASSERT_STDOUT_EQ(bash_spelling, "a-b-c n=3\n");

    run_result_t zsh_spelling = run_shell(
        "printf 'a b c' | { read -A arr; echo \"${arr[0]}-${arr[1]}-${arr[2]} "
        "n=${#arr[@]}\"; }\n");
    ASSERT_STDOUT_EQ(zsh_spelling, "a-b-c n=3\n");

    /// -rA keeps the backslash raw.
    run_result_t raw = run_shell("printf 'x\\\\y z' | { read -rA arr; echo "
                                 "\"${arr[0]}-${arr[1]}\"; }\n");
    ASSERT_STDOUT_EQ(raw, "x\\y-z\n");
}

TEST(for_loop_ifs_nonws_empty_fields_posix) {
    /// POSIX two-class field splitting: a non-whitespace IFS delimiter yields
    /// empty fields for adjacent/leading occurrences (`a::b` -> a, "", b;
    /// `:a:b` -> "", a, b), while a trailing non-whitespace delimiter does not
    /// add a trailing empty field (`a:b:` -> a, b). Issue #566.
    run_result_t mid = run_shell("mode posix\nIFS=:\nx=a::b\n"
                                 "for w in $x; do printf '[%s]' \"$w\"; done\n"
                                 "echo\n");
    ASSERT_STDOUT_EQ(mid, "[a][][b]\n");

    run_result_t lead = run_shell("IFS=:\nx=:a:b\n"
                                  "for w in $x; do printf '[%s]' \"$w\"; done\n"
                                  "echo\n");
    ASSERT_STDOUT_EQ(lead, "[][a][b]\n");

    run_result_t trail =
        run_shell("IFS=:\nx=a:b:\n"
                  "for w in $x; do printf '[%s]' \"$w\"; done\n"
                  "echo\n");
    ASSERT_STDOUT_EQ(trail, "[a][b]\n");

    /// run_shell shares global state; restore the mode and default IFS.
    run_shell("mode lush\nunset IFS\n");
}

TEST(rt_read_ifs_nonws_empty_fields) {
    /// read with a non-whitespace IFS produces empty fields for adjacent
    /// delimiters; the final variable keeps the raw remainder (internal
    /// delimiters preserved). Issue #566.
    run_result_t mid =
        run_shell("printf 'a::b' | { IFS=: read x y z; "
                  "printf '[%s]' \"$x\" \"$y\" \"$z\"; echo; }\n");
    ASSERT_STDOUT_EQ(mid, "[a][][b]\n");

    run_result_t trail =
        run_shell("printf 'a:b:' | { IFS=: read x y z; "
                  "printf '[%s]' \"$x\" \"$y\" \"$z\"; echo; }\n");
    ASSERT_STDOUT_EQ(trail, "[a][b][]\n");

    run_result_t rest =
        run_shell("printf 'a::b:c' | { IFS=: read x y z; "
                  "printf '[%s]' \"$x\" \"$y\" \"$z\"; echo; }\n");
    ASSERT_STDOUT_EQ(rest, "[a][][b:c]\n");

    /// With a mixed IFS, a trailing `<whitespace><non-ws-delim>` unit that
    /// merely terminates the final field is dropped (the whitespace belongs to
    /// the delimiter unit, not a separate delimiter): `foo, bar ,` -> foo, bar.
    run_result_t mixed_tail =
        run_shell("printf 'foo, bar ,' | { IFS=', ' read k v; "
                  "printf '[%s]' \"$k\" \"$v\"; echo; }\n");
    ASSERT_STDOUT_EQ(mixed_tail, "[foo][bar]\n");

    run_shell("unset IFS\n");
}

TEST(rt_read_array_ifs_nonws_empty_fields) {
    /// read -a produces empty array elements for adjacent non-whitespace IFS
    /// delimiters. Issue #566.
    run_result_t r =
        run_shell("printf 'a::b' | { IFS=: read -a arr; "
                  "printf '[%s]' \"${arr[@]}\"; echo \" n=${#arr[@]}\"; }\n");
    ASSERT_STDOUT_EQ(r, "[a][][b] n=3\n");

    run_shell("unset IFS\n");
}

TEST(extglob_gated_by_feature_extended_glob) {
    /// The bash extglob operators ?( *( +( @( !( match only when
    /// FEATURE_EXTENDED_GLOB is enabled; when it is off they are ordinary
    /// literal characters, so the pattern does not match. The unsetopt/setopt
    /// knob -- previously a dead no-op -- now toggles it. Issue #567.
    ///
    /// The pattern lives in a function body parsed once (extglob on at
    /// definition), so the gate is exercised at match time as the option is
    /// toggled; the tokenizer's separate parse-time gating of `@(` is not what
    /// is under test here.
    run_result_t r = run_shell(
        "mode lush\nsetopt extended_glob\n"
        "em() { case \"$1\" in @(foo|bar)) echo M;; *) echo n;; esac; }\n"
        "em foo\n" /// extglob on  -> M
        "unsetopt extended_glob\n"
        "em foo\n" /// extglob off -> literal, no match -> n
        "setopt extended_glob\n"
        "em foo\n"); /// extglob on  -> M
    ASSERT_STDOUT_EQ(r, "M\nn\nM\n");
}

TEST(case_wildcard) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec,
        "X=unknown; case $X in foo) RESULT=foo;; *) RESULT=default;; esac");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "default", "Should match wildcard");
    free(result);

    executor_free(exec);
}

/* POSIX character classes ([[:space:]], [[:upper:]], ...) parse correctly
 * inside case patterns. The tokenizer greedily lexes "[[" as
 * TOK_DOUBLE_LBRACKET (the extended-test opener); the case-pattern
 * collector now accepts that token as literal "[[" text so the resulting
 * pattern string "[[:class:]]" reaches match_pattern intact. */
/* The bash-style ERR pseudo-signal trap fires after a command's
 * non-zero exit, before set -e would otherwise abort. Per issue #108,
 * the errtrace option (inheritance into functions and subshells) is
 * not yet implemented; this asserts only that the
 * trap registers and fires for a top-level non-zero exit. */
TEST(trap_err_fires_on_nonzero_exit) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// trap_list is global state shared across executors. Clear it
    /// at the end of the test so subsequent tests don't observe a
    /// stale ERR handler firing on their own non-zero exits.
    run_result_t r = run_shell_with_executor(
        exec, "trap 'echo ERR_HIT' ERR\nfalse\necho continuing\ntrap - ERR\n");
    ASSERT_EXIT_STATUS(r, 0);
    /// The trap action runs after `false` returns 1, then execution
    /// continues with the next command.
    ASSERT_STDOUT_CONTAINS(r, "ERR_HIT");
    ASSERT_STDOUT_CONTAINS(r, "continuing");

    executor_free(exec);
}

/* Issue #108: the errtrace option controls whether the ERR
 * trap is inherited into function bodies. Without errtrace, lush
 * matches bash's default: the trap fires only at the function's call
 * site (where the function returns non-zero), not inside the body.
 * With `set -o errtrace`, the trap also fires inside the body. */
TEST(trap_err_not_inherited_in_function_by_default) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "trap 'echo HIT' ERR\n"
                                                   "f() { false; }\n"
                                                   "f\n"
                                                   "trap - ERR\n");
    ASSERT_EXIT_STATUS(r, 0);
    /// Exactly one HIT -- the top-level fire on f's non-zero return.
    /// The inside-f false was suppressed by the default-off errtrace.
    const char *p = r.out;
    int hits = 0;
    while ((p = strstr(p, "HIT")) != NULL) {
        hits++;
        p++;
    }
    ASSERT_EQ(hits, 1, "exactly one ERR fire without errtrace");

    executor_free(exec);
}

TEST(trap_err_inherited_in_function_with_errtrace) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "trap 'echo HIT' ERR\n"
                                                   "set -o errtrace\n"
                                                   "f() { false; }\n"
                                                   "f\n"
                                                   "set +o errtrace\n"
                                                   "trap - ERR\n");
    ASSERT_EXIT_STATUS(r, 0);
    /// Two HITs -- inside f at false, and at the top-level on f's
    /// non-zero return.
    const char *p = r.out;
    int hits = 0;
    while ((p = strstr(p, "HIT")) != NULL) {
        hits++;
        p++;
    }
    ASSERT_EQ(hits, 2, "two ERR fires with errtrace -- inside f + top");

    executor_free(exec);
}

/* Issue #109: the functrace option (`set -o functrace` / `-T`)
 * controls whether the DEBUG and RETURN traps are inherited into
 * function bodies. Without functrace, both traps are suppressed for
 * commands executed inside a function scope (bash's default).
 * With functrace, they fire normally inside the body. */
TEST(trap_debug_not_inherited_in_function_by_default) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "trap 'echo D' DEBUG\n"
                                                   "f() { echo a; echo b; }\n"
                                                   "f\n"
                                                   "trap - DEBUG\n");
    ASSERT_EXIT_STATUS(r, 0);
    /// Between `a` and `b` (both inside f's body), no DEBUG line
    /// should appear. If DEBUG were inherited, we'd see "a\nD\nb";
    /// with the default-off behavior we should see "a\nb" with no
    /// interruption.
    ASSERT_NULL(strstr(r.out, "a\nD\nb"),
                "DEBUG must not fire between commands inside f");

    executor_free(exec);
}

TEST(trap_debug_inherited_in_function_with_functrace) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "trap 'echo D' DEBUG\n"
                                                   "set -o functrace\n"
                                                   "f() { echo a; echo b; }\n"
                                                   "f\n"
                                                   "set +o functrace\n"
                                                   "trap - DEBUG\n");
    ASSERT_EXIT_STATUS(r, 0);
    /// With functrace, between a and b a DEBUG line should appear
    /// (`a\nD\nb`), proving the trap fired inside the body.
    ASSERT_NOT_NULL(strstr(r.out, "a\nD\nb"),
                    "DEBUG must fire between commands inside f with functrace");

    executor_free(exec);
}

TEST(trap_return_not_inherited_in_function_by_default) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "trap 'echo RET' RETURN\n"
                                                   "f() { echo inside; }\n"
                                                   "f\n"
                                                   "trap - RETURN\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_NULL(strstr(r.out, "RET"),
                "RETURN trap must NOT fire without functrace");

    executor_free(exec);
}

TEST(trap_return_inherited_in_function_with_functrace) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "trap 'echo RET' RETURN\n"
                                                   "set -o functrace\n"
                                                   "f() { echo inside; }\n"
                                                   "f\n"
                                                   "set +o functrace\n"
                                                   "trap - RETURN\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_NOT_NULL(strstr(r.out, "RET"),
                    "RETURN trap must fire when functrace is set");

    executor_free(exec);
}

TEST(trap_err_silent_on_zero_exit) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "trap 'echo ERR_HIT' ERR\ntrue\necho continuing\ntrap - ERR\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_TRUE(strstr(r.out, "ERR_HIT") == NULL,
                "no ERR trap on a zero-exit command");
    ASSERT_STDOUT_CONTAINS(r, "continuing");

    executor_free(exec);
}

TEST(case_posix_character_class) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "case \" \" in [[:space:]]) R=ws;; *) R=other;; esac");
    ASSERT_EXIT_STATUS(r, 0);
    char *r1 = symtable_get_var(exec->symtable, "R");
    ASSERT_NOT_NULL(r1, "R should be set");
    ASSERT_STR_EQ(r1, "ws", "[[:space:]] matched whitespace");
    free(r1);

    r = run_shell_with_executor(
        exec, "case \"A\" in [[:upper:]]) R=up;; *) R=other;; esac");
    ASSERT_EXIT_STATUS(r, 0);
    char *r2 = symtable_get_var(exec->symtable, "R");
    ASSERT_NOT_NULL(r2, "R should be set");
    ASSERT_STR_EQ(r2, "up", "[[:upper:]] matched uppercase");
    free(r2);

    /// Non-match falls through to the wildcard arm without misparsing
    /// the bracket class.
    r = run_shell_with_executor(
        exec, "case \"x\" in [[:space:]]) R=ws;; *) R=other;; esac");
    ASSERT_EXIT_STATUS(r, 0);
    char *r3 = symtable_get_var(exec->symtable, "R");
    ASSERT_NOT_NULL(r3, "R should be set");
    ASSERT_STR_EQ(r3, "other", "non-whitespace falls through to default");
    free(r3);

    executor_free(exec);
}

TEST(rt_case_extglob_alternation) {
    /// Extended-glob @(a|b) in a case pattern: the inner `|` is part of
    /// the extglob group, not a case-alternative separator, and the
    /// canonical matcher (lush_pattern_match) matches it. The duplicate
    /// hand-rolled matcher that lacked extglob was retired here (#148).
    /// zsh mode has extglob on at parse time; the bash-mode parse-time
    /// `shopt -s extglob` enablement is a separate, deeper issue.
    run_result_t r = run_shell("mode zsh\n"
                               "for w in cat dog bird; do\n"
                               "  case \"$w\" in\n"
                               "    @(cat|dog)) echo \"pet:$w\" ;;\n"
                               "    +([a-z])) echo \"word:$w\" ;;\n"
                               "  esac\n"
                               "done\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "pet:cat\npet:dog\nword:bird\n");
}

TEST(rt_bash_shopt_extglob_takes_effect_next_batch) {
    /// Issue #151: bash-mode `shopt -s extglob` on its own line must
    /// flip FEATURE_EXTENDED_GLOB before the next batch is tokenized,
    /// so an extglob shape in a following case statement parses
    /// instead of erroring at the tokenizer. Drives the per-batch
    /// driver in executor_execute_command_line: each top-level
    /// statement runs to completion (executing any feature flips)
    /// before the next is parsed, matching bash's parse-then-execute
    /// model line by line.
    ///
    /// The same-line form (`shopt -s extglob; case ... esac` on one
    /// line) intentionally still errors -- bash itself errors there
    /// too, since the shopt hasn't run by the time the case is
    /// tokenized. Mode-accurate defaults stay intact.
    run_result_t r = run_shell("mode bash\n"
                               "shopt -s extglob\n"
                               "case cat in @(cat|dog)) echo pet;; esac\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "pet\n");
}

TEST(rt_set_e_aborts_remaining_batches) {
    /// `set -e` must abort subsequent batches in a non-interactive
    /// shell, matching bash. The per-batch driver respects errexit
    /// across batch boundaries -- a failing batch returns non-zero
    /// to the loop and the loop stops feeding more lines. Without
    /// this, multi-line scripts kept running past failing commands.
    run_result_t r = run_shell("set -e\n"
                               "false\n"
                               "echo should-not-print\n");
    ASSERT_EXIT_STATUS(r, 1);
    ASSERT_STDOUT_EQ(r, "");
    /// Harness shares shell_opts globally across run_shell calls
    /// (see project-shell-harness-global-state); clear errexit so
    /// subsequent tests don't observe it.
    run_shell("set +e\n");
}

TEST(rt_err_trap_fires_across_batch_boundaries) {
    /// The ERR trap must fire when a command in a later batch returns
    /// non-zero. Previously the trap-firing logic lived only inside
    /// the command-list walker; a single-command AST (each batch in
    /// the per-batch driver) bypassed it. Routing executor_execute
    /// through the command chain unconditionally fixed this.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r =
        run_shell_with_executor(exec, "trap 'echo TRAP_FIRED' ERR\n"
                                      "false\n"
                                      "echo continuing\n"
                                      "trap - ERR\n");
    ASSERT_STDOUT_CONTAINS(r, "TRAP_FIRED");
    ASSERT_STDOUT_CONTAINS(r, "continuing");
    executor_free(exec);
}

TEST(rt_case_plain_alternation_unregressed) {
    /// The paren/bracket-aware separator must still split ordinary
    /// top-level `|` alternation, including an empty alternative.
    run_result_t r =
        run_shell("case b in a|b|c) echo abc;; *) echo no;; esac\n"
                  "x=''\n"
                  "case \"$x\" in ''|z) echo empty-or-z;; *) echo no;; esac\n");
    ASSERT_STDOUT_EQ(r, "abc\nempty-or-z\n");
}

TEST(rt_case_numeric_range_pattern) {
    /// zsh numeric-range case patterns (#205): <->, <lo-hi>, <lo->. A digit
    /// word matches when its value is in range; a non-digit word and an
    /// out-of-range value fall through to *).
    run_result_t r = run_shell("for n in 7 abc 5 12 150 50 30 99; do\n"
                               "  case $n in\n"
                               "    <100->) echo \"$n:big\";;\n"
                               "    <1-9>) echo \"$n:single\";;\n"
                               "    <->) echo \"$n:digits\";;\n"
                               "    *) echo \"$n:other\";;\n"
                               "  esac\n"
                               "done\n");
    ASSERT_STDOUT_EQ(r, "7:single\nabc:other\n5:single\n12:digits\n"
                        "150:big\n50:digits\n30:digits\n99:digits\n");
}

TEST(rt_case_numeric_range_upper_bound_and_expansion) {
    /// <-hi> is an upper-bounded range; leading zeros are numeric; the case
    /// word may come from an expansion.
    run_result_t r =
        run_shell("x=42\n"
                  "case $x in <-50>) echo le50;; *) echo no;; esac\n"
                  "case 60 in <-50>) echo le50;; *) echo gt50;; esac\n"
                  "case 007 in <1-9>) echo z;; *) echo no;; esac\n");
    ASSERT_STDOUT_EQ(r, "le50\ngt50\nz\n");
}

TEST(rt_case_numeric_range_alternation) {
    /// A bounded range as a non-final |-alternative with no surrounding spaces
    /// (the lexer merges the range's `>` with the `|` into a single `>|`
    /// token), mixed with a glob alternative. A non-numeric `<abc>` (no dash)
    /// falls through to the glob matcher and does not match a bare word.
    run_result_t r =
        run_shell("case 5 in <1-9>|<20-30>) echo hit;; *) echo miss;; esac\n"
                  "case 25 in <1-9>|<20-30>) echo hit;; *) echo miss;; esac\n"
                  "case 99 in <1-9>|<20-30>) echo hit;; *) echo miss;; esac\n"
                  "case cat in <1-9>|cat) echo hit;; *) echo miss;; esac\n"
                  "case abc in <abc>) echo lit;; *) echo other;; esac\n");
    ASSERT_STDOUT_EQ(r, "hit\nhit\nmiss\nhit\nother\n");
}

TEST(rt_glob_qualifier_mtime) {
    /// zsh modification-time qualifier m[Mwhms][+-]n (#206). A brand-new
    /// file is younger than a day; a file stamped in 2020 is older. m-1
    /// keeps the recent one, m+1 the old one; the `.` type filter combines,
    /// and mh+1 switches the unit to hours.
    run_result_t r = run_shell("OLD=$(pwd)\n"
                               "d=$(mktemp -d)\n"
                               "cd \"$d\"\n"
                               "touch a.txt\n"
                               "touch -t 202001010000 b.txt\n"
                               "echo recent: *(m-1)\n"
                               "echo old: *(m+1)\n"
                               "echo regrecent: *(.m-1)\n"
                               "echo hourold: *(mh+1)\n"
                               "cd \"$OLD\"; rm -rf \"$d\"\n");
    ASSERT_STDOUT_EQ(r, "recent: a.txt\nold: b.txt\n"
                        "regrecent: a.txt\nhourold: b.txt\n");
}

TEST(rt_glob_qualifier_quoted_element) {
    /// A glob qualifier fused onto a quoted word (#206): "$f"(N) filters the
    /// expanded value in command-argument, array-literal, and for-list
    /// position, and the m time filter still applies. Because the quote fixed
    /// the value as a scalar (SEMANTICS 3.6), the qualifier is an existence
    /// test on the LITERAL value -- a metacharacter value is not re-globbed
    /// (G). A malformed group stays literal, never dropped (H). Parens inside
    /// the quotes ("abc(N)") stay literal -- the distinction rides on a token
    /// flag, not the concatenated text (E).
    run_result_t r =
        run_shell("OLD=$(pwd)\n"
                  "d=$(mktemp -d)\n"
                  "cd \"$d\"\n"
                  "touch keep.txt\n"
                  "touch -t 202001010000 stale.txt\n"
                  "f=keep.txt\n"
                  "echo A: \"$f\"(N)\n"
                  "g=missing.txt\n"
                  "echo B: \"$g\"(N)\n"
                  "arr=(\"$f\"(Nm-1)); echo C: ${#arr[@]} ${arr[@]}\n"
                  "old=stale.txt\n"
                  "arr2=(\"$old\"(Nm-1)); echo D: ${#arr2[@]}\n"
                  "echo E: \"abc(N)\"\n"
                  "for i in \"$f\"(N); do echo F: $i; done\n"
                  "p='k*.txt'; echo G: \"$p\"(N)\n"
                  "echo H: \"$f\"(5)\n"
                  "cd \"$OLD\"; rm -rf \"$d\"\n");
    ASSERT_STDOUT_EQ(r, "A: keep.txt\nB:\nC: 1 keep.txt\nD: 0\nE: abc(N)\n"
                        "F: keep.txt\nG:\nH: keep.txt(5)\n");
}

TEST(rt_pushd_popd_rotation) {
    /// pushd/popd +N/-N is a circular rotation of the full directory list
    /// shown by `dirs` (D0 = cwd, D1.. = stack top-to-bottom), #216. +N
    /// counts from the left, -N from the right; the target becomes the new
    /// cwd and the rest keep circular order (no duplicate, cwd never lost).
    /// popd removes the indexed entry. Each op runs in a subshell so the
    /// baseline is unchanged between checks; db prints the basenames.
    /// run_shell shares cwd + the dirstack across tests; run the whole body
    /// in a subshell so its cd/pushd never leak to (or inherit dirt from)
    /// other tests. Each rotation runs in its own nested subshell so the
    /// baseline is unchanged between checks; db prints the basenames.
    run_result_t r = run_shell(
        "( d=$(mktemp -d); mkdir -p \"$d\"/a \"$d\"/b \"$d\"/c \"$d\"/e\n"
        "  cd \"$d/a\"; pushd \"$d/b\" >/dev/null; "
        "pushd \"$d/c\" >/dev/null; pushd \"$d/e\" >/dev/null\n"
        "  db() { dl=( $(dirs) ); for x in \"${dl[@]}\"; do printf '%s ' "
        "\"${x##*/}\"; done; echo; "
        "}\n"
        "  printf 'A '; ( pushd +1 >/dev/null; db )\n"
        "  printf 'B '; ( pushd +2 >/dev/null; db )\n"
        "  printf 'C '; ( pushd +3 >/dev/null; db )\n"
        "  printf 'D '; ( pushd -1 >/dev/null; db )\n"
        "  printf 'E '; ( pushd -2 >/dev/null; db )\n"
        "  printf 'F '; ( popd +1 >/dev/null; db )\n"
        "  printf 'G '; ( popd -1 >/dev/null; db )\n"
        "  printf 'H '; ( popd +0 >/dev/null; db )\n"
        "  printf 'I '; ( popd >/dev/null; db )\n"
        "  rm -rf \"$d\" )\n");
    ASSERT_STDOUT_EQ(r, "A c b a e \n"
                        "B b a e c \n"
                        "C a e c b \n"
                        "D b a e c \n"
                        "E c b a e \n"
                        "F e b a \n"
                        "G e c a \n"
                        "H c b a \n"
                        "I c b a \n");
}

TEST(rt_pushdminus_option) {
    /// setopt pushdminus is a real, off-by-default option (#216) that
    /// inverts the +N / -N sign convention: with it set, `pushd +N` behaves
    /// like the default `pushd -N`. Verifies the setopt/unsetopt/[[ -o ]]
    /// wiring and the inversion symmetry.
    run_result_t r = run_shell(
        "( [[ -o pushdminus ]] || echo default-off\n"
        "  setopt pushdminus; [[ -o pushdminus ]] && echo set-on\n"
        "  unsetopt pushdminus; [[ -o pushdminus ]] || echo unset-off\n"
        "  d=$(mktemp -d); mkdir -p \"$d\"/a \"$d\"/b \"$d\"/c\n"
        "  cd \"$d/a\"; pushd \"$d/b\" >/dev/null; pushd \"$d/c\" >/dev/null\n"
        "  db() { dl=( $(dirs) ); for x in \"${dl[@]}\"; do printf '%s ' "
        "\"${x##*/}\"; done; echo; "
        "}\n"
        "  printf 'pm '; ( setopt pushdminus; pushd +1 >/dev/null; db )\n"
        "  printf 'df '; ( pushd -1 >/dev/null; db )\n"
        "  rm -rf \"$d\" )\n");
    ASSERT_STDOUT_EQ(r, "default-off\nset-on\nunset-off\n"
                        "pm b a c \n"
                        "df b a c \n");
}

TEST(rt_pushd_silent_option) {
    /// setopt pushd_silent is a real, off-by-default option (#460) that
    /// suppresses the automatic directory-stack echo after pushd and popd.
    /// The explicit `dirs` command is never gated. Each pushd/popd runs in a
    /// command substitution so the auto-report is the captured output and the
    /// cd/setopt never leak to sibling tests.
    run_result_t r = run_shell(
        "( [[ -o pushd_silent ]] || echo default-off\n"
        "  setopt pushd_silent; [[ -o pushd_silent ]] && echo set-on\n"
        "  unsetopt pushd_silent; [[ -o pushd_silent ]] || echo unset-off\n"
        "  d=$(mktemp -d); mkdir -p \"$d\"/a \"$d\"/b\n"
        "  o=$( cd \"$d/a\"; pushd \"$d/b\" ); [ -n \"$o\" ] && echo "
        "off-prints\n"
        "  o=$( cd \"$d/a\"; setopt pushd_silent; pushd \"$d/b\" ); "
        "[ -z \"$o\" ] && echo on-silent\n"
        "  o=$( cd \"$d/a\"; setopt pushd_silent; pushd \"$d/b\" >/dev/null; "
        "popd ); [ -z \"$o\" ] && echo popd-silent\n"
        "  o=$( cd \"$d/a\"; setopt pushd_silent; pushd \"$d/b\" >/dev/null; "
        "dirs ); [ -n \"$o\" ] && echo dirs-prints\n"
        "  rm -rf \"$d\" )\n");
    ASSERT_STDOUT_EQ(r, "default-off\nset-on\nunset-off\n"
                        "off-prints\non-silent\npopd-silent\ndirs-prints\n");
}

TEST(rt_pushd_to_home_option) {
    /// setopt pushd_to_home is a real, off-by-default option (#460): bare
    /// `pushd` acts like `pushd $HOME` instead of exchanging the top two
    /// entries. Default bare pushd on an empty stack still errors.
    run_result_t r = run_shell(
        "( [[ -o pushd_to_home ]] || echo default-off\n"
        "  setopt pushd_to_home; [[ -o pushd_to_home ]] && echo set-on\n"
        "  unsetopt pushd_to_home; [[ -o pushd_to_home ]] || echo unset-off\n"
        "  d=$(mktemp -d); mkdir -p \"$d\"/a \"$d\"/b; export HOME=\"$d/b\"\n"
        "  ( cd \"$d/a\"; pushd 2>/dev/null; echo \"df-rc=$?\" )\n"
        "  ( cd \"$d/a\"; setopt pushd_to_home; pushd >/dev/null; "
        "[ \"$(pwd -P)\" = \"$(cd \"$d/b\" && pwd -P)\" ] && echo at-home )\n"
        "  rm -rf \"$d\" )\n");
    ASSERT_STDOUT_EQ(r, "default-off\nset-on\nunset-off\n"
                        "df-rc=1\nat-home\n");
}

TEST(rt_pushd_ignore_dups_option) {
    /// setopt pushd_ignore_dups is a real, off-by-default option (#460): the
    /// stack keeps only one copy of a directory. Pushing a directory that is
    /// already on the stack drops the older occurrence (zsh deduplicates the
    /// directory moved to). Default keeps the duplicate.
    run_result_t r = run_shell(
        "( [[ -o pushd_ignore_dups ]] || echo default-off\n"
        "  setopt pushd_ignore_dups; [[ -o pushd_ignore_dups ]] && echo "
        "set-on\n"
        "  unsetopt pushd_ignore_dups; [[ -o pushd_ignore_dups ]] || "
        "echo unset-off\n"
        "  d=$(mktemp -d); mkdir -p \"$d\"/a \"$d\"/b \"$d\"/c\n"
        "  db() { dl=( $(dirs) ); for x in \"${dl[@]}\"; do printf '%s ' "
        "\"${x##*/}\"; done; echo; "
        "}\n"
        "  printf 'df '; ( cd \"$d/a\"; pushd \"$d/b\" >/dev/null; "
        "pushd \"$d/c\" >/dev/null; pushd \"$d/b\" >/dev/null; db )\n"
        "  printf 'ig '; ( cd \"$d/a\"; setopt pushd_ignore_dups; "
        "pushd \"$d/b\" >/dev/null; pushd \"$d/c\" >/dev/null; "
        "pushd \"$d/b\" >/dev/null; db )\n"
        "  rm -rf \"$d\" )\n");
    ASSERT_STDOUT_EQ(r, "default-off\nset-on\nunset-off\n"
                        "df b c b a \n"
                        "ig b c a \n");
}

TEST(rt_magic_equal_subst) {
    /// zsh magic_equal_subst (#219): the RHS of an unquoted assignment-style
    /// argument word `name=~/path` is tilde-expanded like a real assignment
    /// value, including after each colon. Off by default (word stays
    /// literal); a quoted word or a non-identifier left side is untouched.
    /// Wrapped in a subshell so the HOME export and the setopt do not leak to
    /// other run_shell tests.
    run_result_t r = run_shell("( export HOME=/tmp/meq_home\n"
                               "  echo A: foo=~/bar\n"
                               "  setopt magic_equal_subst\n"
                               "  echo B: foo=~/bar\n"
                               "  echo C: p=x:~/y\n"
                               "  echo D: \"q=~/keep\"\n"
                               "  echo E: bad/name=~/no\n"
                               "  x=~/z; echo F: $x\n"
                               "  [[ abc =~ ^a ]] && echo G: regex )\n");
    ASSERT_STDOUT_EQ(r, "A: foo=~/bar\n"
                        "B: foo=/tmp/meq_home/bar\n"
                        "C: p=x:/tmp/meq_home/y\n"
                        "D: q=~/keep\n"
                        "E: bad/name=~/no\n"
                        "F: /tmp/meq_home/z\n"
                        "G: regex\n");
}

TEST(rt_assignment_tilde_expansion) {
    /// Real assignment RHS tilde expansion (#219): a tilde-prefix at the value
    /// start and after every UNQUOTED colon is expanded (bash and zsh agree);
    /// a double-quoted or single-quoted tilde stays literal; a tilde produced
    /// by a later $var expansion is not itself re-expanded. Subshell-wrapped so
    /// the HOME export does not leak to other run_shell tests.
    run_result_t r = run_shell("( export HOME=/tmp/meq_home\n"
                               "  a=~/a:~/b;         echo \"1:$a\"\n"
                               "  b=\"~/a\";           echo \"2:$b\"\n"
                               "  c=~/a:\"~/b\";       echo \"3:$c\"\n"
                               "  d='a:~';           echo \"4:$d\"\n"
                               "  e=~/a:'~/b';       echo \"5:$e\"\n"
                               "  v=\"~\"; f=$v/bar;    echo \"6:$f\"\n"
                               "  g=$(echo a:~/b);   echo \"7:$g\"\n"
                               "  h=${HOME}:~/b;     echo \"8:$h\" )\n");
    ASSERT_STDOUT_EQ(r, "1:/tmp/meq_home/a:/tmp/meq_home/b\n"
                        "2:~/a\n"
                        "3:/tmp/meq_home/a:~/b\n"
                        "4:a:~\n"
                        "5:/tmp/meq_home/a:~/b\n"
                        "6:~/bar\n"
                        "7:a:~/b\n"
                        "8:/tmp/meq_home:/tmp/meq_home/b\n");
}

TEST(rt_declaration_util_tilde_expansion) {
    /// The assignment-aware builtins (export/local/declare/typeset/readonly)
    /// tilde-expand their name=value arguments like a real assignment (#466),
    /// independent of magic_equal_subst; a double-quoted value stays literal.
    /// Subshell-wrapped so the HOME export does not leak to other run_shell
    /// tests.
    run_result_t r =
        run_shell("( export HOME=/tmp/meq_home\n"
                  "  export E=~/a:~/b;   echo \"1:$E\"\n"
                  "  declare D=~/d;      echo \"2:$D\"\n"
                  "  readonly R=~/r;     echo \"3:$R\"\n"
                  "  typeset T=~/t;      echo \"4:$T\"\n"
                  "  f() { local L=~/l; echo \"5:$L\"; }; f\n"
                  "  export Q=\"~/keep\"; echo \"6:$Q\"\n"
                  "  u=\"~/b\"; export W=~/a:$u; echo \"7:$W\" )\n");
    ASSERT_STDOUT_EQ(r, "1:/tmp/meq_home/a:/tmp/meq_home/b\n"
                        "2:/tmp/meq_home/d\n"
                        "3:/tmp/meq_home/r\n"
                        "4:/tmp/meq_home/t\n"
                        "5:/tmp/meq_home/l\n"
                        "6:~/keep\n"
                        /// A tilde that emerges from $u is NOT re-expanded: the
                        /// leading ~/a expands, the ~/b from $u stays literal.
                        "7:/tmp/meq_home/a:~/b\n");
}

TEST(rt_declaration_util_mixed_quote_tilde) {
    /// #488: an assignment-aware builtin tilde-expands a MIXED-quote value
    /// exactly like a plain assignment -- the unquoted tilde segment expands,
    /// the double- or single-quoted one stays literal. Previously the whole
    /// word was skipped when it contained any quoted span (both segments
    /// literal). The in-a-function cases exercise the deep-copied body: the
    /// parser's provenance value must survive node_copy (the deep-copy path).
    run_result_t r =
        run_shell("( export HOME=/tmp/h\n"
                  "  export A=~/a:\"~/b\";        echo \"1:$A\"\n"
                  "  declare B=~/a:'~/b';         echo \"2:$B\"\n"
                  "  export C=\"~/a:~/b\";        echo \"3:$C\"\n"
                  "  export D=~/a:~/b;            echo \"4:$D\"\n"
                  "  f() { declare E=~/e:\"~/q\"; echo \"5:$E\"; }; f\n"
                  "  g() { local L=~/l:\"~/q\";   echo \"6:$L\"; }; g\n"
                  "  echo 7: E=~/a:\"~/b\" )\n");
    ASSERT_STDOUT_EQ(r,
                     /// double-quoted second segment stays literal
                     "1:/tmp/h/a:~/b\n"
                     /// single-quoted second segment stays literal
                     "2:/tmp/h/a:~/b\n"
                     /// fully double-quoted value: entirely literal
                     "3:~/a:~/b\n"
                     /// fully unquoted: both segments expand
                     "4:/tmp/h/a:/tmp/h/b\n"
                     /// declaration inside a function: provenance survives copy
                     "5:/tmp/h/e:~/q\n"
                     "6:/tmp/h/l:~/q\n"
                     /// a non-assignment command (echo) does not tilde-expand
                     /// its assignment-shaped argument (magic_equal_subst off)
                     "7: E=~/a:~/b\n");
}

TEST(rt_mixed_quote_provenance_assignment) {
    /// #495: an assignment value that fuses quote contexts re-encodes per
    /// character from the tokenizer's quote-provenance map, so only the quoted
    /// characters are held literal. Before this, the tokenizer flattened a
    /// fused word (`"b":~/c`) into one wholesale-typed token and the re-encoder
    /// escaped the unquoted `~` too (leaving it literal). Covers: a quoted
    /// segment BEFORE an unquoted tilde (the reversed case), a quoted segment
    /// in the middle, single-quote expansion protection, an escaped literal
    /// quote (the ESCAPED provenance state), an assignment builtin, and a
    /// declaration inside a deep-copied function body. Subshell-wrapped so the
    /// HOME export does not leak to other run_shell tests.
    run_result_t r = run_shell(
        "( export HOME=/tmp/h; y=Y\n"
        "  x=\"~/a\":~/b;   echo \"1:$x\"\n"
        "  x=~/a:\"b\":~/c; echo \"2:$x\"\n"
        "  x='$y'z;         echo \"3:$x\"\n"
        "  x='it'\\''s';    echo \"4:$x\"\n"
        "  export E=\"~/a\":~/b;            echo \"5:$E\"\n"
        "  f() { declare D=\"~/m\":~/n; echo \"6:$D\"; }; f\n"
        "  x=v:~/b:'lit';   echo \"7:$x\"\n"
        /// a single-quoted value that is exactly a kind sigil
        /// (@ident / %ident, lush default) must stay literal, not be
        /// sigil-expanded: the re-encoding escapes every single-quoted
        /// character so the run never begins with @ or %.
        "  x='@foo';        echo \"8:$x\"\n"
        "  x='%s';          echo \"9:$x\" )\n");
    ASSERT_STDOUT_EQ(r,
                     /// quoted-first, then an unquoted after-colon tilde: only
                     /// the second segment expands
                     "1:~/a:/tmp/h/b\n"
                     /// quoted middle segment: the two unquoted tildes expand
                     "2:/tmp/h/a:b:/tmp/h/c\n"
                     /// single-quoted $y stays literal (not expanded to Y)
                     "3:$yz\n"
                     /// \' escaped literal quote survives (ESCAPED provenance)
                     "4:it's\n"
                     /// assignment builtin, same per-character semantics
                     "5:~/a:/tmp/h/b\n"
                     /// inside a deep-copied function body
                     "6:~/m:/tmp/h/n\n"
                     /// unquoted tilde segment then a single-quoted literal
                     "7:v:/tmp/h/b:lit\n"
                     /// single-quoted kind sigils stay literal
                     "8:@foo\n"
                     "9:%s\n");
}

TEST(rt_command_word_mixed_quote_provenance) {
    /// #498: a COMMAND-ARGUMENT (and array-element) word that fuses quote
    /// contexts expands each character by its own quote context -- an unquoted
    /// leading `~` expands, a single-quoted `$var` stays literal, and a
    /// `"$var"` abutting text bounds the name at the quote edge rather than
    /// greedily swallowing it. The command-word half of the #495 provenance
    /// arc, consumed by expand_quoted_string_prov via node_t.quote_prov.
    /// Subshell-wrapped for the HOME export.
    run_result_t r =
        run_shell("( export HOME=/tmp/h; y=Y; unset x\n"
                  "  echo ~/a\"b\"\n"
                  "  echo '$x'y\n"
                  "  echo x\"$y\"z\n"
                  "  echo pre'$y'post\n"
                  "  echo '$y'~/b\n"
                  "  a=(~/x \"~/y\" ~/z); echo \"${a[0]} ${a[1]} ${a[2]}\"\n"
                  "  f() { echo x\"$y\"z; }; f )\n");
    ASSERT_STDOUT_EQ(r,
                     /// unquoted leading ~ expands though the word is fused
                     "/tmp/h/ab\n"
                     /// single-quoted $x stays literal
                     "$xy\n"
                     /// "$y" bounds at the quote edge; z stays literal
                     "xYz\n"
                     "pre$ypost\n"
                     "$y~/b\n"
                     /// array: unquoted ~ expands, quoted "~/y" stays literal
                     "/tmp/h/x ~/y /tmp/h/z\n"
                     /// inside a deep-copied function body (node_copy)
                     "xYz\n");
}

TEST(rt_ansi_c_arg_is_mode_gated) {
    /// A standalone $'...' command argument decodes ANSI-C escapes only under
    /// FEATURE_ANSI_QUOTING (on in lush/bash/zsh, OFF in POSIX mode). The Word
    /// CST decodes $'...' at parse time, so its dual-routing path must honour
    /// the same mode gate as the legacy expander: decode when the feature is
    /// on, leave $'...' literal under `set -o posix`. Regression guard for the
    /// CST default-on flip -- run under `--setup lush:audit`, a divergence here
    /// aborts. $'\t' == a tab.
    run_result_t on = run_shell("printf '[%s]' $'\\t'\n");
    ASSERT_STDOUT_EQ(on, "[\t]");
    /// Subshell-isolated so POSIX mode does not leak into later tests
    /// (run_shell shares mode state across tests).
    run_result_t off = run_shell("( set -o posix; printf '[%s]' $'\\t' )\n");
    ASSERT_STDOUT_EQ(off, "[$'\\t']");
}

TEST(rt_special_params_scalar_covered) {
    /// The scalar specials $?/$$/$# expand on the CST backbone (covered, not
    /// deferred), sourced through the same expander legacy uses so the value
    /// matches by construction -- validated byte-for-byte under the CST audit
    /// soak. $? = last exit status, $# = positional count, $$ = a numeric PID.
    /// Subshell-isolated so `set --` positionals do not leak across tests.
    run_result_t r = run_shell(
        "( false; echo $?; set -- a b c; echo $#; echo \"s=$? n=$#\"; "
        "case $$ in ''|*[!0-9]*) echo BADPID;; *) echo pidnum;; esac )\n");
    ASSERT_STDOUT_EQ(r, "1\n3\ns=0 n=3\npidnum\n");
}

TEST(rt_unquoted_param_glob_brace_value_defers) {
    /// An unquoted $var whose VALUE carries a glob/brace metacharacter is
    /// re-expanded by the legacy path (it globs / brace-expands the expansion
    /// result, per bash/POSIX). The CST covers scalars but not that later pass,
    /// so it must defer such a value rather than emit it literally. A quoted
    /// expansion is glob/brace-inert and stays literal. Brace form is used (not
    /// glob) so the assertion is filesystem-independent. Runs under the CST
    /// audit soak, where a divergence would abort.
    run_result_t unq = run_shell("v='{a,b}'\nprintf '[%s]' $v\necho\n");
    ASSERT_STDOUT_EQ(unq, "[a][b]\n");
    run_result_t q = run_shell("v='{a,b}'\nprintf '[%s]' \"$v\"\necho\n");
    ASSERT_STDOUT_EQ(q, "[{a,b}]\n");
}

TEST(rt_positional_params_covered) {
    /// Single-digit positionals $0..$9 expand on the CST backbone (covered, not
    /// deferred), sourced through the same expander legacy uses so the value
    /// matches by construction -- validated byte-for-byte under the audit soak.
    /// $10 is ${1}0 (single-digit positional then a literal 0). Positional
    /// count and values track function scope and shift. Subshell-isolated so
    /// `set --` does not leak across tests.
    run_result_t r =
        run_shell("( set -- A B C; echo $1 $2 $3 $10 x$1y; "
                  "f() { echo in-fn $1 $#; }; f P Q R; echo back $1 $#; "
                  "shift; echo shifted $1 $# )\n");
    ASSERT_STDOUT_EQ(r, "A B C A0 xAy\nin-fn P 3\nback A 3\nshifted B 2\n");
}

TEST(rt_bg_pid_and_option_flags_covered) {
    /// $! (last background PID) and $- (current option flags) are covered
    /// scalars on the CST backbone, sourced through the same expander legacy
    /// uses; exact values are validated byte-for-byte under the audit soak.
    /// Here we assert $! is a numeric PID after a background job, and that $-
    /// reflects `set -f` (contains `f`) -- robust whether or not the harness
    /// starts with other flags. Subshell-isolated so `set -f` does not leak.
    run_result_t r = run_shell(
        "( sleep 0.1 & case $! in ''|*[!0-9]*) echo BADPID;; *) echo bgok;; "
        "esac; set -f; case $- in *f*) echo fset;; *) echo fnotset;; esac )\n");
    ASSERT_STDOUT_EQ(r, "bgok\nfset\n");
}

TEST(rt_pe_alternation_operators_covered) {
    /// The four alternation parameter-expansion operators expand on the CST
    /// backbone via the shared operator primitive: ${var:-x}/${var:+x} treat an
    /// empty value like unset; ${var-x}/${var+x} distinguish unset from empty.
    /// Nested ($set) and empty operands are covered; a space-containing default
    /// (${un:-a b}) defers to legacy and still matches. Validated byte-for-byte
    /// under --setup lush:audit. Subshell-isolated.
    run_result_t r =
        run_shell("( set=x; empty=; unset -v un\n"
                  "  echo \"[${set:-D}][${empty:-D}][${un:-D}]\"\n"
                  "  echo \"[${set:+A}][${empty:+A}][${un:+A}]\"\n"
                  "  echo \"[${set-D}][${empty-D}][${un-D}]\"\n"
                  "  echo \"[${set+A}][${empty+A}][${un+A}]\"\n"
                  "  echo \"[${un:-$set}][${un:-}][${un:-a b}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[x][D][D]\n"
                        "[A][][]\n"
                        "[x][][D]\n"
                        "[A][A][]\n"
                        "[x][][a b]\n");
}

TEST(rt_pe_operand_with_quotes_defers) {
    /// Legacy expands the PE operand through a $-only pass that does NOT remove
    /// quotes or backslashes (${un:-'x'} keeps the quotes literal -- a lush
    /// quirk vs bash). The CST would dequote the operand via parse_word, so it
    /// defers a quoted/backslash operand to match legacy. Regression for the
    /// operand quote-context divergence (the audit soak would abort otherwise).
    run_result_t r =
        run_shell("( unset -v un\n  printf '[%s]\\n' \"${un:-'lit'}\" )\n");
    ASSERT_STDOUT_EQ(r, "['lit']\n");
}

TEST(rt_pe_pattern_strip_operators_covered) {
    /// Pattern-strip operators ${var#p}/${var##p}/${var%p}/${var%%p} expand on
    /// the CST backbone: the operand is a glob PATTERN matched via the shared
    /// apply_param_operator (# shortest, ## longest prefix; % / %% suffix).
    /// Covers literal glob patterns; a quoted pattern (kept literal by legacy)
    /// or a $-pattern (expanded by legacy) defers and still matches. Validated
    /// under --setup lush:audit. Subshell-isolated.
    run_result_t r =
        run_shell("( f=a.b.c.txt; q=ab; p=abcdef\n"
                  "  echo \"[${f#*.}][${f##*.}][${f%.*}][${f%%.*}]\"\n"
                  "  echo \"[${p#abc}][${p%def}][${p#xyz}][${p#}]\"\n"
                  "  echo \"[${p#'ab'}][${p#$q}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[b.c.txt][txt][a.b.c][a]\n"
                        "[def][abc][abcdef][abcdef]\n"
                        "[abcdef][cdef]\n");
}

TEST(rt_pe_operand_edge_whitespace_defers) {
    /// An operator operand with unquoted leading/trailing whitespace is part of
    /// the pattern/default (${v#a } strips "a ", ${u:-def } defaults to "def
    /// "). The tokenizer drops edge whitespace, so the CST defers such an
    /// operand to match legacy (the audit soak would abort otherwise). Covers
    /// both pattern-strip and alternation. Subshell-isolated.
    run_result_t r =
        run_shell("( v='a b c'; u=\n"
                  "  printf '[%s]' \"${v#a }\" \"${v# a}\" \"${u:-def }\" "
                  "\"${u:- x}\"; echo )\n");
    ASSERT_STDOUT_EQ(r, "[b c][a b c][def ][ x]\n");
}

TEST(rt_pe_case_conversion_operators_covered) {
    /// Case-conversion operators ${var^^}/${var,,} (all characters) and
    /// ${var^}/${var,} (first character) expand on the CST backbone via the
    /// shared apply_param_operator (lush_case_all_* / convert first). Covers
    /// the NO-pattern form; a pattern-restricted form (${var^^a}) defers to
    /// legacy and still matches. Validated under --setup lush:audit.
    /// Subshell-isolated.
    run_result_t r =
        run_shell("( s=heLLo; b=banana; c=caf\xc3\xa9; unset -v u\n"
                  "  echo \"[${s^^}][${s,,}][${s^}][${s,}]\"\n"
                  "  echo \"[${b^^a}][${u^^}][${s^^}x][pre${s,,}]\"\n"
                  "  echo \"[${c^^}][${c,,}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[HELLO][hello][HeLLo][heLLo]\n"
                        "[bAnAnA][][HELLOx][prehello]\n"
                        "[CAF\xc3\x89][caf\xc3\xa9]\n");
}

TEST(rt_pe_substring_operators_covered) {
    /// Substring ${var:off} / ${var:off:len} expands on the CST backbone via
    /// the shared apply_param_operator (lush_substring_extract,
    /// grapheme-aware). Covers the simple non-negative numeric spec, incl.
    /// UTF-8 live (cafe-acute -> caf / e-acute by grapheme, not byte);
    /// offset/length past the end clamp; an unset var is empty. A negative
    /// length (${s:2:-1}) is NOT covered and defers to legacy, still matching.
    /// Validated under
    /// --setup lush:audit. Subshell-isolated.
    run_result_t r =
        run_shell("( s=abcdef; c=caf\xc3\xa9; unset -v u\n"
                  "  echo \"[${s:2}][${s:2:2}][${s:0:3}][${s:4:99}][${s:9}]\"\n"
                  "  echo \"[${c:0:3}][${c:3}][${u:0:2}][${s:2:-1}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[cdef][cd][abc][ef][]\n"
                        "[caf][\xc3\xa9][][cde]\n");
}

TEST(rt_pe_substitution_operators_covered) {
    /// Substitution ${var/pat/repl} (first) and ${var//pat/repl} (all) expand
    /// on the CST backbone via the shared apply_param_operator (pattern_
    /// substitute). Covered: literal + glob patterns (o*b) + UTF-8 (e-acute),
    /// incl. empty replacement (remove), no-match (unchanged) and a `%` anchor
    /// whose pattern does not start an identifier (${n/%1/X}). Deferring to
    /// legacy (still matching): the \/ escaped-slash idiom, a /# anchor (a
    /// leading `#` opens a comment) and a /%name kind-sigil anchor. Validated
    /// under --setup lush:audit. Subshell-isolated.
    run_result_t r = run_shell(
        "( s=abcabc; f=foobar; g=foofoo; p=path/to/x; c=caf\xc3\xa9; n=ab1\n"
        "  echo \"[${s/a/X}][${s//a/X}][${s//a/}][${s/zzz/Q}]\"\n"
        "  echo \"[${f/o*b/Z}][${g/#foo/X}][${g/%foo/X}][${n/%1/X}]\"\n"
        "  echo \"[${p//\\//.}][${c/\xc3\xa9/e}][${c//o/0}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[Xbcabc][XbcXbc][bcbc][abcabc]\n"
                        "[fZar][Xfoo][fooX][abX]\n"
                        "[path.to.x][cafe][caf\xc3\xa9]\n");
}

TEST(rt_nounset_errors_on_covered_words) {
    /// #686: `set -u` was inert for any word the Word CST covers -- which is
    /// the DEFAULT route -- so an unbound reference expanded to empty and the
    /// command ran. The CST is a value producer with no error channel, so it
    /// now DEFERS such a read and the legacy expander reports E1122 and fails.
    /// Every shape is covered by the CST in lush mode (quoted, unquoted, mid-
    /// word, braced), so every one regressed before the fix.
    /// Subshell-isolated (`set -u` must not leak into later tests) and mode-
    /// pinned: mode is process-global across tests, and the unquoted case only
    /// exercises the CST route in a mode where word splitting does not already
    /// defer it.
    run_result_t r =
        run_shell("( mode lush; set -u; unset U\n"
                  "  ( echo \"[$U]\" ) 2>/dev/null; echo \"a=$?\"\n"
                  "  ( echo [$U] ) 2>/dev/null; echo \"b=$?\"\n"
                  "  ( echo \"[pre${U}post]\" ) 2>/dev/null; echo \"c=$?\"\n"
                  "  ( echo \"[${U}]\" ) 2>/dev/null; echo \"d=$?\" )\n");
    ASSERT_STDOUT_EQ(r, "a=1\nb=1\nc=1\nd=1\n");
}

TEST(rt_nounset_exempt_forms_still_expand) {
    /// The nounset defer must not over-fire. Legacy exempts the specials it
    /// lists (`? $ # 0 @ * - !`) and every operator form that supplies a value
    /// for an unset name, so those must still expand on the CST route. An
    /// unset POSITIONAL is NOT exempt ($0 is), and positionals defer
    /// unconditionally under nounset because this layer cannot tell an unset
    /// positional from an empty one. Subshell-isolated.
    run_result_t r =
        run_shell("( set -u; unset U; set -- p\n"
                  "  echo \"[${U:-d}][${U-d}][${U:+a}][${U+a}][${U#p}]\"\n"
                  "  echo \"[${U^^}][${U:0:1}][${U/a/b}][${U:=asg}][$U]\"\n"
                  "  echo \"[$#][$?][$1]\"\n"
                  "  echo \"$0\" >/dev/null; echo \"zero=$?\"\n"
                  "  ( echo \"[$2]\" ) 2>/dev/null; echo \"unset_pos=$?\" )\n");
    ASSERT_STDOUT_EQ(r, "[d][d][][][]\n"
                        "[][][][asg][asg]\n"
                        "[1][0][p]\n"
                        "zero=0\n"
                        "unset_pos=1\n");
}

TEST(rt_pe_assign_operators_covered) {
    /// Assign ${var:=x} (when unset OR empty) and ${var=x} (when unset) expand
    /// on the CST backbone AND write the value back through the same
    /// symtable_set_var the legacy scalar path uses. Also pins within-word
    /// visibility: a later part of the SAME word sees the assigned value
    /// (${w:=x}${w} -> xx), which the pending-write buffer must preserve.
    /// Validated under --setup lush:audit. Subshell-isolated.
    run_result_t r =
        run_shell("( unset u; e=; s=set\n"
                  "  echo \"[${u:=A}][$u][${e:=B}][$e][${s:=C}][$s]\"\n"
                  "  unset v; f=\n"
                  "  echo \"[${v=D}][$v][${f=E}][$f]\"\n"
                  "  unset w; echo \"[${w:=x}${w}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[A][A][B][B][set][set]\n"
                        "[D][D][][]\n"
                        "[xx]\n");
}

TEST(rt_pe_assign_write_is_transactional) {
    /// The assign write is BUFFERED until the whole word is known covered.
    /// Here the word assigns and THEN defers on a brace-carrying value, so it
    /// falls back to the legacy expander, which re-expands the word from the
    /// start. Had the write already landed, the earlier `$u` would see the
    /// assigned value and print `Ax-Bx` instead of legacy's `A-Bx`. The second
    /// line is the same hazard where a LATER assign feeds an earlier one's
    /// operand: `b` must stay unset-then-empty, not pick up `a`'s new value.
    run_result_t r =
        run_shell("( unset u; g=\"{1,2}\"\n"
                  "  echo A$u-B${u:=x}-C$g\n"
                  "  unset a b\n"
                  "  echo X${b:=$a}Y${a:=1}Z$g; echo \"a=<$a> b=<$b>\" )\n");
    ASSERT_STDOUT_EQ(r, "A-Bx-C1 A-Bx-C2\n"
                        "XY1Z1 XY1Z2\n"
                        "a=<1> b=<>\n");
}

TEST(rt_pe_assign_operators_defer_forms) {
    /// The assign operators inherit the family's restrictions: a quoted or
    /// space-carrying operand defers at parse, and a non-plain-identifier name
    /// (a positional, a special) is never covered, so it can never be assigned
    /// through this route -- the positional case uses `set --` so the assign
    /// branch is actually live. A READONLY target defers too: the pending-write
    /// buffer models a store that succeeds, but symtable_set_var refuses this
    /// one, so legacy's later read still sees the old value (`[v]`, not
    /// `[vv]`). Deferring is invisible in the OUTPUT -- legacy handles all of
    /// these identically -- so this pins the behavior, and the audit soak
    /// proves the two paths agree.
    run_result_t r =
        run_shell("( unset u; echo \"[${u:=a b}][$u]\"\n"
                  "  unset v; echo \"[${v:='q'}][$v]\"\n"
                  "  set --; echo \"[${1:=z}][$1]\"\n"
                  "  unset r; readonly r; echo \"[${r:=v}${r}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[a b][a b]\n[\'q\'][\'q\']\n[z][z]\n[v]\n");
}

TEST(rt_dq_backtick_runs_the_substitution) {
    /// #693: a backtick inside a DOUBLE-QUOTED string is an expansion
    /// introducer, not a literal byte. parse_double_interior treated only `$`
    /// as one, so the backticks landed in a literal leaf while the word was
    /// still reported fully handled, and the default route emitted the source
    /// text instead of running the command. Both routes must agree, and both
    /// must match what lush's own legacy expander (and bash and zsh) do.
    run_result_t r = run_shell("( echo \"v: `echo HI`\"\n"
                               "  echo \"pre`echo A`post\"\n"
                               "  x=Q; echo \"[$x`echo B`]\" )\n");
    ASSERT_STDOUT_EQ(r, "v: HI\npreApost\n[QB]\n");
}

TEST(rt_pe_operand_is_evaluated_only_when_consumed) {
    /// #692: a conditional operator uses its operand on ONE branch. The operand
    /// is a word -- it can run a command substitution, move a variable through
    /// an arithmetic side effect, or raise a diagnostic -- so evaluating it on
    /// the branch that discards the result performs work the expansion never
    /// asked for. Each line below pairs the discarding branch (side effect must
    /// NOT happen) with the consuming branch (it must). Verified against bash
    /// and zsh across all eight operators and all three value states.
    run_result_t r =
        run_shell("( v=hi; unset -v u; e=\n"
                  "  i=0; : \"${v:-$((i+=5))}\"; echo \"a=$i\"\n"
                  "  i=0; : \"${u:-$((i+=5))}\"; echo \"b=$i\"\n"
                  "  i=0; : \"${v:+$((i+=5))}\"; echo \"c=$i\"\n"
                  "  i=0; : \"${e:+$((i+=5))}\"; echo \"d=$i\"\n"
                  "  i=0; : \"${e-$((i+=5))}\"; echo \"e=$i\"\n"
                  "  i=0; : \"${u-$((i+=5))}\"; echo \"f=$i\"\n"
                  "  i=0; : \"${v+$((i+=5))}\"; echo \"g=$i\" )\n");
    ASSERT_STDOUT_EQ(r, "a=0\nb=5\nc=5\nd=0\ne=0\nf=5\ng=5\n");
}

TEST(rt_pe_unused_operand_lazy_on_both_engines) {
    /// The laziness has to hold on BOTH expansion routes. The other tests here
    /// use `$((...))` / `$(...)` operands, which word_eval DEFERS -- so they
    /// measure the legacy expander only and would pass even if the Word CST
    /// evaluated operands eagerly. A nested `${u:=X}` is a side-effecting
    /// operand form the CST COVERS, so it pins the CST path specifically: the
    /// discarded branch must not assign. Both routes must agree, because the
    /// audit gate compares field strings and is blind to a side effect.
    run_result_t r =
        run_shell("( v=hi; e=; unset -v n\n"
                  "  unset -v u; echo \"1 ${v:-${u:=X}} u=[$u]\"\n"
                  "  unset -v u; echo \"2 ${e:+${u:=X}} u=[$u]\"\n"
                  "  unset -v u; echo \"3 ${e-${u:=X}} u=[$u]\"\n"
                  "  unset -v u; echo \"4 ${n+${u:=X}} u=[$u]\"\n"
                  "  unset -v u; echo \"5 ${v:=${u:=X}} u=[$u]\"\n"
                  "  unset -v u; echo \"6 ${e=${u:=X}} u=[$u]\" )\n");
    ASSERT_STDOUT_EQ(r, "1 hi u=[]\n2  u=[]\n3  u=[]\n4  u=[]\n"
                        "5 hi u=[]\n6  u=[]\n");

    /// And the consuming branch still evaluates and assigns.
    run_result_t c =
        run_shell("( unset -v u v; echo \"[${v:-${u:=X}}] u=[$u]\" )\n");
    ASSERT_STDOUT_EQ(c, "[X] u=[X]\n");
}

TEST(rt_pe_unused_operand_raises_no_diagnostic) {
    /// The sharpest form: an unused operand must not report the error of a
    /// branch never taken. Eagerly expanding `${u:?boom}` inside the default of
    /// an operator that does not need it made lush diagnose an expansion its
    /// own semantics say never applies -- and fail the command.
    run_result_t r =
        run_shell("( v=hi; unset -v u\n"
                  "  echo \"[${v:-${u:?boom}}]\"; echo \"rc=$?\" )\n");
    ASSERT_STDOUT_EQ(r, "[hi]\nrc=0\n");
    ASSERT_STDERR_EQ(r, "");
}

TEST(rt_pe_assign_operand_lazy_but_still_assigns) {
    /// The assign operators must stay lazy WITHOUT losing the write: the
    /// operand is evaluated (and stored) exactly on the branch that assigns.
    run_result_t r =
        run_shell("( v=hi; unset -v u; i=0\n"
                  "  : \"${v:=$((i+=5))}\"; echo \"kept=$v i=$i\"\n"
                  "  i=0; : \"${u:=$((i+=5))}\"; echo \"set=$u i=$i\" )\n");
    ASSERT_STDOUT_EQ(r, "kept=hi i=0\nset=5 i=5\n");
}

TEST(rt_pe_required_operators_covered) {
    /// The required-parameter operators ${var:?msg} / ${var?msg} expand on the
    /// CST backbone whenever the parameter passes its set-ness test: the
    /// result is the value and the message is never consulted, so a message
    /// carrying spaces or glob metacharacters is covered too (it is captured
    /// raw, exactly as the legacy expander keeps it); a message carrying a
    /// QUOTING byte defers instead, because the CST and the legacy
    /// double-quoted-word expander disagree on where a `${...}` containing one
    /// ends -- the third line is exactly that shape and must still print what
    /// legacy prints. `?` treats an
    /// EMPTY value as set; `:?` does not. The last line is the cross-slice
    /// case: an assign operator earlier in the SAME word makes the name set,
    /// and the required operator must see that pending value (as legacy sees
    /// the committed one) rather than firing. Validated under
    /// --setup lush:audit. Subshell-isolated.
    run_result_t r =
        run_shell("( v=hello; e=\n"
                  "  echo \"[${v:?msg}][${v?msg}][${v:?}][${v?}]\"\n"
                  "  echo \"[${v:?must be set}][${v:?a-b+c*d}]\"\n"
                  "  echo \"[${v:?a'}'b}]\"\n"
                  "  echo \"[${e?empty is set}]\"\n"
                  "  echo a${v:?m}b\n"
                  "  unset q; echo \"[${q:=x}${q:?m}]\" )\n");
    ASSERT_STDOUT_EQ(r, "[hello][hello][hello][hello]\n"
                        "[hello][hello]\n"
                        "[hello'b}]\n"
                        "[]\n"
                        "ahellob\n"
                        "[xx]\n");
}

TEST(rt_pe_required_operators_error_forms) {
    /// A FIRING expansion is never covered: word_eval defers it so the legacy
    /// expander raises E1307 and requests the POSIX exit (the statement after
    /// it must not run). Deferring is invisible in the output -- that is the
    /// point -- so this pins the diagnostic and the exit status, and that
    /// `${e?}` on an empty value does NOT fire while `${e:?}` does. Run in a
    /// subprocess: the POSIX exit request is a real shell-level abort.
    run_result_t r = run_shell_subprocess("unset pe_u\n"
                                          "echo \"[${pe_u:?nope}]\"\n"
                                          "echo after\n");
    ASSERT_EXIT_STATUS(r, 1);
    ASSERT_STDOUT_EQ(r, "");
    ASSERT_STDERR_CONTAINS(r, "pe_u: nope");

    run_result_t r2 = run_shell_subprocess("pe_e=\n"
                                           "echo \"[${pe_e:?empty}]\"\n"
                                           "echo after\n");
    ASSERT_EXIT_STATUS(r2, 1);
    ASSERT_STDOUT_EQ(r2, "");
    ASSERT_STDERR_CONTAINS(r2, "pe_e: empty");

    /// No message -> the built-in default text, and the `?` form's trigger is
    /// unset-only.
    run_result_t r3 = run_shell_subprocess("unset pe_u\necho \"[${pe_u?}]\"\n");
    ASSERT_EXIT_STATUS(r3, 1);
    ASSERT_STDERR_CONTAINS(r3, "parameter not set");

    /// Deferring BEFORE the operator is applied is load-bearing, not tidiness.
    /// If the CST applied a firing operator and only then deferred on a later
    /// part of the same word (here `$g`, whose brace-carrying value defers),
    /// the legacy re-expansion of the whole word would raise the diagnostic a
    /// SECOND time. Exactly one must reach the user.
    run_result_t r4 = run_shell_subprocess("unset pe_u; g=\"{1,2}\"\n"
                                           "echo ${pe_u:?dup}$g\n");
    ASSERT_EXIT_STATUS(r4, 1);
    int seen = 0;
    for (const char *scan = r4.err; (scan = strstr(scan, "pe_u: dup")) != NULL;
         scan += 9) {
        seen++;
    }
    ASSERT_TRUE(seen == 1, "the diagnostic is reported exactly once");
}

TEST(rt_pe_required_dollar_message_is_covered) {
    /// The `$`-carrying message used to defer the whole `${...}` because the
    /// legacy expander built it EAGERLY, so covering it unevaluated would have
    /// dropped a side effect that had already happened. Since #692 legacy
    /// evaluates a conditional operand only on the consuming branch, so on the
    /// non-firing branch NEITHER route evaluates the message -- they agree by
    /// construction and the defer is obsolete. wordtool reports
    /// `${v:?$(echo X)}` as covered (rc 0) where it previously reported a
    /// defer (rc 2).
    ///
    /// Behaviour must be identical either way, which is the point: this pins
    /// that widening coverage changed nothing observable. The marker proves the
    /// message is not built when the parameter passes its test, and the firing
    /// case proves it still is when the diagnostic needs it.
    run_result_t r = run_shell_subprocess(
        "m=${TMPDIR:-/tmp}/lush_req_dollar_$$\n"
        "rm -f \"$m\"\n"
        "v=hi; echo \"[${v:?$(touch $m)x}]\"\n"
        "if [ -f \"$m\" ]; then echo BUILT; else echo NOT-BUILT; fi\n"
        "rm -f \"$m\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "[hi]\nNOT-BUILT\n");

    /// Firing: the message IS built, expanded, and reported once.
    run_result_t f =
        run_shell_subprocess("unset u\necho \"[${u:?$(echo EXPANDED)}]\"\n");
    ASSERT_EXIT_STATUS(f, 1);
    ASSERT_STDERR_CONTAINS(f, "u: EXPANDED");
}

TEST(rt_pe_required_message_is_not_evaluated_when_unused) {
    /// The `:?` message is consumed only when the diagnostic fires. This test
    /// previously pinned the OPPOSITE -- the legacy expander evaluated the
    /// operand eagerly, so `${set_var:?$(cmd)}` ran cmd even though the error
    /// never fired -- and said so, flagging it as issue #692 and noting the
    /// expectation would flip when that was fixed. It is fixed: the marker must
    /// NOT exist, matching bash. (zsh does not expand the message at all, even
    /// when firing; lush curates bash's reading that the message IS expanded,
    /// and now shares its laziness.)
    ///
    /// word_eval still defers a `$`-carrying message. That defer is now
    /// CONSERVATIVE rather than required -- with the operand lazy, covering it
    /// unevaluated would match legacy -- so the required-parameter family is
    /// newly widenable. Deferring more than necessary is always safe.
    run_result_t r =
        run_shell_subprocess("m=${TMPDIR:-/tmp}/lush_pe_req_marker_$$\n"
                             "rm -f \"$m\"\n"
                             "v=hi; echo \"[${v:?$(touch $m)x}]\"\n"
                             "if [ -f \"$m\" ]; then echo SIDE-EFFECT; "
                             "else echo NO-SIDE-EFFECT; fi\n"
                             "rm -f \"$m\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "[hi]\nNO-SIDE-EFFECT\n");
}

TEST(rt_map_in_argv_forms) {
    /// #222: a map in command-argument (vector) position. Bare ${m[@]} / $m /
    /// @m / ${(v)m} contribute the VALUES in insertion order (like ${arr[@]});
    /// ${(k)m} gives keys; ${(kv)m} and the pair sigil %m give interleaved
    /// `key value ...` pairs; ${m[*]} joins the values to a scalar. Subshell
    /// wrapped so the global map does not leak to other run_shell tests.
    run_result_t r = run_shell("( typeset -A m; m[a]=1; m[b]=2\n"
                               "  echo \"at:$(echo ${m[@]})\"\n"
                               "  echo \"bare:$(echo $m)\"\n"
                               "  echo \"v:$(echo ${(v)m})\"\n"
                               "  echo \"k:$(echo ${(k)m})\"\n"
                               "  echo \"kv:$(echo ${(kv)m})\"\n"
                               "  echo \"vec:$(echo @m)\"\n"
                               "  echo \"pair:$(echo %m)\"\n"
                               "  echo \"star:${m[*]}\"\n"
                               "  typeset -A e\n"
                               "  echo \"empty:[$(echo ${e[@]})]\" )\n");
    ASSERT_STDOUT_EQ(r, "at:1 2\n"
                        "bare:1 2\n"
                        "v:1 2\n"
                        "k:a b\n"
                        "kv:a 1 b 2\n"
                        "vec:1 2\n"
                        "pair:a 1 b 2\n"
                        "star:1 2\n"
                        "empty:[]\n");
}

/* ============================================================================
 * SEMANTICS section 3.4/3.9 conformance: ${arr[@]} in scalar context
 *
 * Per SEMANTICS.md section 3.9, a list value (${arr[@]}, etc.) reaching
 * a scalar-requiring position is a runtime type error. Vector-accepting
 * positions (argv, for-loop word list) continue to receive the array's
 * elements; the explicit-join subscript ${arr[*]} stays a scalar.
 * ============================================================================
 */

TEST(arr_at_in_scalar_assignment_raises_type_mismatch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "arr=(a b c)\nx=${arr[@]}\necho saw_post_assign\n");

    /// The offending command aborts execution -- "saw_post_assign"
    /// must not reach stdout.
    ASSERT_TRUE(strstr(r.out, "saw_post_assign") == NULL,
                "execution aborted after type mismatch");
    /// The diagnostic must reach stderr with the canonical phrasing.
    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_STDERR_CONTAINS(r, "${arr[@]}");
    ASSERT_TRUE(r.exit_status != 0, "non-zero exit on type-mismatch abort");

    executor_free(exec);
}

/* SEMANTICS section 3.9: bare unsubscripted ${arr} on a list value.
 * - In a vector-accepting slot (argv, for-loop word list, array
 *   init), contributes elements one-to-one with [@].
 * - In a scalar-requiring slot (assignment RHS, case word, here-
 *   string, arithmetic operand, conditional-expression operand,
 *   or a within-word glued position), the engine raises a runtime
 *   type-mismatch error and the script aborts before the bad value
 *   reaches a downstream command. */
TEST(bare_arr_in_scalar_assignment_raises_type_mismatch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "arr=(a b c)\ny=${arr}\necho saw_post_assign\n");

    ASSERT_TRUE(strstr(r.out, "saw_post_assign") == NULL,
                "execution aborted after type mismatch");
    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_STDERR_CONTAINS(r, "${arr}");
    ASSERT_TRUE(r.exit_status != 0, "non-zero exit on type-mismatch abort");

    executor_free(exec);
}

TEST(bare_arr_in_for_loop_iterates) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "arr=(a b c)\nfor i in ${arr}; do echo iter=$i; done\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "iter=a");
    ASSERT_STDOUT_CONTAINS(r, "iter=b");
    ASSERT_STDOUT_CONTAINS(r, "iter=c");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no error in vector position");

    executor_free(exec);
}

TEST(bare_arr_glued_to_text_raises_type_mismatch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Whole-word constraint per section 3.9: a list value glued to
    /// other text within a single word has no coherent meaning and is
    /// a runtime type error.
    run_result_t r = run_shell_with_executor(
        exec, "arr=(a b c)\necho \"x${arr}y\"\necho after\n");

    ASSERT_TRUE(strstr(r.out, "after") == NULL,
                "execution aborted on glued list");
    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_TRUE(r.exit_status != 0, "non-zero exit on glued list");

    executor_free(exec);
}

/* SEMANTICS section 3.7: the (@) parameter flag is a spelling alias
 * for vector presentation. It is redundant with the [@] subscript --
 * ${(@)arr} must yield exactly what ${arr[@]} yields. */
TEST(at_paren_flag_alias_yields_vector) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "arr=(a 'x y' c)\nfor i in ${(@)arr}; do echo iter=$i; done\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "iter=a");
    ASSERT_STDOUT_CONTAINS(r, "iter=x y");
    ASSERT_STDOUT_CONTAINS(r, "iter=c");

    executor_free(exec);
}

TEST(at_paren_flag_composes_with_sort) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// (o@) -- sort ascending and yield vector. (@) is the no-op
    /// presentation alias; (o) carries the transformation per
    /// SEMANTICS section 3.5 (transformation and presentation
    /// orthogonal).
    run_result_t r = run_shell_with_executor(
        exec, "arr=(c a b)\nfor i in ${(o@)arr}; do echo iter=$i; done\n");

    ASSERT_EXIT_STATUS(r, 0);
    const char *p_a = strstr(r.out, "iter=a");
    const char *p_b = strstr(r.out, "iter=b");
    const char *p_c = strstr(r.out, "iter=c");
    ASSERT_TRUE(p_a != NULL && p_b != NULL && p_c != NULL,
                "all three elements emitted");
    ASSERT_TRUE(p_a < p_b && p_b < p_c, "sort applied via (o)");

    executor_free(exec);
}

TEST(bare_arr_argv_yields_elements_one_to_one) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// argv slot is vector-accepting. printf '<%s>\n' ${arr} must
    /// produce one <element> line per element, preserving the
    /// original boundaries even for elements containing spaces.
    run_result_t r = run_shell_with_executor(
        exec, "arr=(a 'x y' c)\nprintf '<%s>\\n' ${arr}\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "<a>");
    ASSERT_STDOUT_CONTAINS(r, "<x y>");
    ASSERT_STDOUT_CONTAINS(r, "<c>");

    executor_free(exec);
}

TEST(arr_at_in_for_loop_iterates) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The for-loop word list is vector-accepting -- ${arr[@]} feeds it
    /// elements, no error.
    run_result_t r = run_shell_with_executor(
        exec, "arr=(a b c)\nfor i in ${arr[@]}; do echo iter=$i; done\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "iter=a");
    ASSERT_STDOUT_CONTAINS(r, "iter=b");
    ASSERT_STDOUT_CONTAINS(r, "iter=c");
    /// No spurious type-mismatch diagnostic.
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no error in vector position");

    executor_free(exec);
}

TEST(arr_star_in_scalar_assignment_joins) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// ${arr[*]} is the explicit scalar-join operator -- legal in a
    /// scalar-requiring position; per SEMANTICS section 3.5.
    run_result_t r = run_shell_with_executor(
        exec, "arr=(a b c)\nx=${arr[*]}\necho \"x=$x\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "x=a b c");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "[*] join is conformant in scalar context");

    executor_free(exec);
}

/* ============================================================================
 * SEMANTICS section 3.9 mode gating: FEATURE_STRICT_VALUE_TYPING
 *
 * The strict list/scalar type error (E1134) is lush mode's flagship
 * default. The bash/zsh/posix compatibility modes relax the boundary
 * policy to the oracle's silent flatten. The value model is uniform in
 * every mode; only the crossing policy (error vs flatten) moves.
 *
 * Shell mode is process-global, and the harness parses a whole script
 * buffer before executing it -- so the array literal `arr=(...)` is
 * recognized at PARSE time under whatever global mode is current when
 * the buffer is submitted, while the mid-buffer `mode X` only takes
 * effect at EXECUTION time (the expansion, where the gate is checked).
 * run_shell_mode_gated() pins the parse-time mode to lush (so the array
 * always parses as a list) and restores lush afterward (so no mode
 * leaks into the strict tests that follow); the in-script `mode X`
 * selects the execution-time boundary policy under test.
 * ============================================================================
 */

static run_result_t run_shell_mode_gated(executor_t *exec, const char *src) {
    /// executor_new() shares one process-global symbol table, so IFS set
    /// by one test would otherwise leak into the next (and into the read
    /// tests that assume the default IFS). Unset it before and after so
    /// each script starts from the default and leaves nothing behind; a
    /// script that needs a custom IFS assigns it inline.
    apply_mode_preset(SHELL_MODE_LUSH); /// deterministic parse-time mode
    symtable_unset_var(exec->symtable, "IFS");
    run_result_t r = run_shell_with_executor(exec, src);
    apply_mode_preset(SHELL_MODE_LUSH); /// restore; no global mode leak
    symtable_unset_var(exec->symtable, "IFS");
    return r;
}

TEST(rt_cmdsub_no_split_in_lush_mode) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// lush mode: an unquoted command substitution does NOT implicitly split
    /// (SEMANTICS section 4.1), matching the mental model of an unquoted $var.
    /// Both $(...) and a backtick, in argv and in a for-list, stay one word.
    run_result_t r =
        run_shell_mode_gated(exec, "set -- $(echo a b c)\necho $#\n");
    ASSERT_STDOUT_EQ(r, "1\n");

    run_result_t bt =
        run_shell_mode_gated(exec, "set -- `echo a b c`\necho $#\n");
    ASSERT_STDOUT_EQ(bt, "1\n");

    run_result_t forl = run_shell_mode_gated(
        exec, "for w in $(echo a b c); do printf '[%s]' \"$w\"; done\necho\n");
    ASSERT_STDOUT_EQ(forl, "[a b c]\n");

    executor_free(exec);
}

TEST(rt_cmdsub_split_in_compat_modes) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The compat modes restore the traditional command-sub split. zsh is the
    /// asymmetric case that proves the two flags are genuinely distinct: it
    /// splits $(cmd) but NOT bare $var, matching real zsh.
    run_result_t bash = run_shell_mode_gated(
        exec, "mode bash\nset -- $(echo a b c)\necho $#\n");
    ASSERT_STDOUT_EQ(bash, "3\n");

    run_result_t posix = run_shell_mode_gated(
        exec, "mode posix\nset -- $(echo a b c)\necho $#\n");
    ASSERT_STDOUT_EQ(posix, "3\n");

    run_result_t zsh_cs =
        run_shell_mode_gated(exec, "mode zsh\nset -- $(echo a b c)\necho $#\n");
    ASSERT_STDOUT_EQ(zsh_cs, "3\n");

    run_result_t zsh_var = run_shell_mode_gated(
        exec, "mode zsh\nx=\"a b c\"\nset -- $x\necho $#\n");
    ASSERT_STDOUT_EQ(zsh_var, "1\n");

    executor_free(exec);
}

TEST(rt_cmdsub_split_select_matches_for) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// `select` word-splits a command substitution under the same per-mode
    /// rule as `for` / argv. Feeding the selection `1` and reading $w tells the
    /// two apart: in lush mode `$(echo a b c)` is one menu item (its whole
    /// text), in the compat modes it splits so item 1 is `a`.
    run_result_t lush = run_shell_mode_gated(
        exec, "printf '1\\n' | { select w in $(echo a b c); do echo \"[$w]\"; "
              "break; done; }\n");
    ASSERT_STDOUT_CONTAINS(lush, "[a b c]");

    run_result_t bash = run_shell_mode_gated(
        exec, "mode bash\nprintf '1\\n' | { select w in $(echo a b c); do echo "
              "\"[$w]\"; break; done; }\n");
    ASSERT_STDOUT_CONTAINS(bash, "[a]");

    run_result_t zsh = run_shell_mode_gated(
        exec, "mode zsh\nprintf '1\\n' | { select w in $(echo a b c); do echo "
              "\"[$w]\"; break; done; }\n");
    ASSERT_STDOUT_CONTAINS(zsh, "[a]");

    executor_free(exec);
}

TEST(rt_cmdsub_explicit_split_via_array_literal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The explicit-split escape hatch still works in lush mode: an array
    /// literal splits the command output; a quoted "$(...)" never splits.
    run_result_t arr =
        run_shell_mode_gated(exec, "arr=( $(echo a b c) )\necho ${#arr[@]}\n");
    ASSERT_STDOUT_EQ(arr, "3\n");

    run_result_t q =
        run_shell_mode_gated(exec, "set -- \"$(echo a b c)\"\necho $#\n");
    ASSERT_STDOUT_EQ(q, "1\n");

    executor_free(exec);
}

TEST(gate_bash_mode_arr_at_scalar_flattens_on_space) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// bash mode relaxes E1134: ${arr[@]} in a scalar slot flattens on a
    /// literal space, ignoring IFS (bash oracle behavior).
    run_result_t r = run_shell_mode_gated(
        exec,
        "mode bash\nIFS=,\narr=(w x y)\nv=\"${arr[@]}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w x y]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 in bash mode");

    executor_free(exec);
}

TEST(gate_zsh_mode_arr_at_scalar_flattens_on_ifs) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// zsh mode relaxes E1134 but joins on IFS[0], not a literal space.
    run_result_t r = run_shell_mode_gated(
        exec, "mode zsh\nIFS=,\narr=(w x y)\nv=\"${arr[@]}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w,x,y]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL, "no E1134 in zsh mode");

    executor_free(exec);
}

TEST(gate_bash_mode_bare_arr_scalar_yields_element_zero) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Bare ${arr} in a scalar slot: bash yields element 0.
    run_result_t r = run_shell_mode_gated(
        exec, "mode bash\narr=(w x y)\nv=\"${arr}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 in bash mode");

    executor_free(exec);
}

TEST(gate_zsh_mode_bare_arr_scalar_yields_whole_join) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Bare ${arr} in a scalar slot: zsh yields the whole space-joined
    /// list (its bare-array-name rule differs from bash's element 0).
    run_result_t r = run_shell_mode_gated(
        exec, "mode zsh\narr=(w x y)\nv=\"${arr}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w x y]");

    executor_free(exec);
}

TEST(gate_bash_mode_keys_at_scalar_flattens) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// ${!arr[@]} keys in a scalar slot: bash flattens the index list on
    /// a space rather than raising the strict type error.
    run_result_t r = run_shell_mode_gated(
        exec, "mode bash\narr=(w x y)\nv=\"${!arr[@]}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[0 1 2]");

    executor_free(exec);
}

TEST(gate_posix_mode_arr_at_scalar_flattens_like_bash) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// posix mode has no array literal of its own -- the harness parses
    /// incrementally, so `arr=(w x y)` under an active `mode posix` would
    /// run `w` as a command. The array is therefore built first (under the
    /// default lush mode), then the mode is switched: the point under test
    /// is that posix EXECUTION flattens a list in a scalar slot to the
    /// bash-family literal space (IFS-independent) rather than raising
    /// E1134.
    run_result_t r = run_shell_mode_gated(
        exec,
        "arr=(w x y)\nmode posix\nIFS=,\nv=\"${arr[@]}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w x y]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 in posix mode");

    executor_free(exec);
}

TEST(gate_arr_star_scalar_joins_on_ifs_every_mode) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// ${arr[*]} is the explicit scalar join and is never gated: it joins
    /// on IFS[0] in every mode, including the relaxed compat modes.
    run_result_t r = run_shell_mode_gated(
        exec,
        "mode bash\nIFS=,\narr=(w x y)\nv=\"${arr[*]}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w,x,y]");

    executor_free(exec);
}

TEST(gate_lush_mode_still_strict_after_gating) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Regression: gating the compat modes must not weaken lush mode --
    /// ${arr[@]} in a scalar slot is still the E1134 type error.
    run_result_t r = run_shell_mode_gated(
        exec, "arr=(a b c)\nx=${arr[@]}\necho saw_post_assign\n");

    ASSERT_TRUE(strstr(r.out, "saw_post_assign") == NULL,
                "lush mode still aborts on type mismatch");
    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_TRUE(r.exit_status != 0, "non-zero exit in lush mode");

    executor_free(exec);
}

/* ============================================================================
 * SEMANTICS section 3.9 mode gating -- named list/map in COMMAND position
 *
 * Under strict value typing (lush mode) a named array/map used as the
 * command word is a scalar-slot type error (positionals are argv, a named
 * list is not). The compat modes relax it to the oracle: bash and zsh
 * spread `${arr[@]}` into command words; bash reads a bare `${arr}` as
 * element 0 while zsh spreads it. Uses run_shell_mode_gated so the arrays
 * parse under lush and no mode/IFS leaks into later tests.
 * ============================================================================
 */

TEST(gate_bash_mode_array_at_command_position_spreads) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// `${cmd[@]}` in command position runs `echo hi there` -> "hi there".
    run_result_t r = run_shell_mode_gated(
        exec, "cmd=(echo hi there)\nmode bash\n${cmd[@]}\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "hi there");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 in bash mode");

    executor_free(exec);
}

TEST(gate_bash_mode_array_command_with_trailing_args) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The spread command word composes with literal arguments that follow.
    run_result_t r = run_shell_mode_gated(
        exec, "cmd=(echo one)\nmode bash\n${cmd[@]} two three\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "one two three");

    executor_free(exec);
}

TEST(gate_bash_mode_bare_array_command_is_element_zero) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Bare ${cmd} in command position: bash reads element 0, so the
    /// command word is `echo` and it runs with no arguments.
    run_result_t r = run_shell_mode_gated(
        exec, "cmd=(echo hi there)\nmode bash\n${cmd} && echo ran_elem0\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "ran_elem0");
    /// element 0 is `echo`; running it with no args prints only a newline,
    /// never the later elements.
    ASSERT_TRUE(strstr(r.out, "hi there") == NULL,
                "bare array is element 0, not the spread");

    executor_free(exec);
}

TEST(gate_zsh_mode_bare_array_command_spreads) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// zsh spreads a bare array in command position (not element 0).
    run_result_t r =
        run_shell_mode_gated(exec, "cmd=(echo hi there)\nmode zsh\n${cmd}\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "hi there");

    executor_free(exec);
}

TEST(gate_bash_mode_map_at_command_position_spreads_values) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// A map in command position contributes its values (insertion order);
    /// `${m[@]}` here is `echo hi`, which runs.
    run_result_t r = run_shell_mode_gated(
        exec, "declare -A m\nm[k]=echo\nm[j]=hi\nmode bash\n${m[@]}\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "hi");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 for map command in bash mode");

    executor_free(exec);
}

TEST(gate_bash_mode_empty_array_command_is_null_command) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// An empty array in command position is a null command (exit 0), the
    /// same as an empty "$@", not a type error or a command-not-found.
    run_result_t r =
        run_shell_mode_gated(exec, "e=()\nmode bash\n${e[@]}\necho rc=$?\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "rc=0");
    ASSERT_TRUE(strstr(r.err, "not found") == NULL,
                "empty array command is a null command");

    executor_free(exec);
}

TEST(gate_lush_mode_array_command_still_strict) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Regression: lush mode still rejects a named array in command
    /// position with the E1134 type error.
    run_result_t r =
        run_shell_mode_gated(exec, "cmd=(echo hi)\n${cmd[@]}\necho saw_post\n");

    ASSERT_TRUE(strstr(r.out, "saw_post") == NULL,
                "lush mode aborts before the next command");
    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_TRUE(r.exit_status != 0, "non-zero exit in lush mode");

    executor_free(exec);
}

/* ============================================================================
 * SEMANTICS section 3.9 mode gating -- scalar operator on a bare collection
 *
 * A scalar parameter operator (:-  :+  :=  #  ##  %  /  substring  ^^  @Q ...)
 * applied to a bare array/map name is a scalar-slot type error in lush mode.
 * The compat modes relax it: the collection is flattened to its mode scalar
 * (bash/posix element 0, zsh whole-join on IFS[0]) and the existing operator
 * engine applies to that scalar -- exact bash element-0 parity, and zsh
 * scalar-slot parity via the join. zsh's element-slicing ${arr:o:l} and its
 * := array-collapse are curated to the flatten-then-apply reading (documented
 * divergences). Uses run_shell_mode_gated (arrays parse under lush; no leak).
 * ============================================================================
 */

TEST(gate_bash_mode_bare_op_uses_element_zero) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// bash applies every scalar operator to element 0: ${arr#a} strips the
    /// prefix of "apple"; ${arr^^} uppercases only element 0.
    run_result_t r = run_shell_mode_gated(
        exec, "arr=(apple berry cherry)\nmode bash\n"
              "echo \"[${arr#a}][${arr^^}][${arr:1:2}]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[pple][APPLE][pp]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 in bash mode");

    executor_free(exec);
}

TEST(gate_bash_mode_bare_default_operator_element_zero) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// :- yields element 0 when the array is non-empty; the default when the
    /// array (element 0) is empty/unset.
    run_result_t r = run_shell_mode_gated(
        exec, "arr=(apple berry)\nmode bash\n"
              "echo \"[${arr:-DEF}]\"\ne=()\necho \"[${e:-DEF}]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[apple]");
    ASSERT_STDOUT_CONTAINS(r, "[DEF]");

    executor_free(exec);
}

TEST(gate_bash_mode_assign_operator_preserves_array) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// := on a bare array writes element 0 and preserves the array shape --
    /// it must not clobber the binding into a scalar.
    run_result_t r = run_shell_mode_gated(
        exec, "arr=(a b c)\nmode bash\n: \"${arr:=X}\"\n"
              "echo \"n=${#arr[@]} zero=${arr[0]} all=${arr[*]}\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    /// arr[0] was already set ("a"), so := does not change it; the array
    /// stays three elements.
    ASSERT_STDOUT_CONTAINS(r, "n=3 zero=a all=a b c");

    executor_free(exec);
}

TEST(gate_bash_mode_assign_operator_on_empty_sets_element_zero) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// := on an empty array assigns the default to element 0.
    run_result_t r =
        run_shell_mode_gated(exec, "arr=()\nmode bash\n: \"${arr:=X}\"\n"
                                   "echo \"n=${#arr[@]} zero=${arr[0]}\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "n=1 zero=X");

    executor_free(exec);
}

TEST(gate_zsh_mode_bare_op_uses_whole_join) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// zsh's scalar-slot behavior is join-then-apply-once: ${arr#a} strips one
    /// leading "a" from the joined "apple berry cherry"; ${arr##*e} removes the
    /// longest *e prefix of the join.
    run_result_t r = run_shell_mode_gated(
        exec, "arr=(apple berry cherry)\nmode zsh\n"
              "echo \"[${arr#a}]\"\necho \"[${arr##*e}]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[pple berry cherry]");
    ASSERT_STDOUT_CONTAINS(r, "[rry]");

    executor_free(exec);
}

TEST(gate_zsh_mode_substring_is_curated_scalar_substr) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Documented divergence: zsh reads bare ${arr:1:2} as element slicing
    /// (-> "berry cherry"); lush curates it to a scalar substring of the join
    /// ("apple berry cherry"[1:2] -> "pp"). The explicit ${arr[@]:1:2} vector
    /// form remains the way to slice elements.
    run_result_t r = run_shell_mode_gated(
        exec, "arr=(apple berry cherry)\nmode zsh\necho \"[${arr:1:2}]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[pp]");
    ASSERT_TRUE(
        strstr(r.out, "berry cherry") == NULL,
        "lush curates zsh substring to scalar substr, not element slice");

    executor_free(exec);
}

TEST(gate_bare_op_numeric_operand_not_slice) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Regression: ${arr:-5} / ${arr:+3} must route as the default/alternate
    /// operators, not be mis-read as a :5 / :3 slice.
    run_result_t r = run_shell_mode_gated(
        exec,
        "mode bash\ne=()\necho \"[${e:-5}]\"\nx=(y)\necho \"[${x:+3}]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[5]");
    ASSERT_STDOUT_CONTAINS(r, "[3]");

    executor_free(exec);
}

TEST(gate_bare_op_attribute_query_not_type_error) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The @a attribute query is metadata-only and must work on a collection
    /// in every mode, including lush -- it is never the section 3.9 type
    /// error.
    run_result_t r =
        run_shell_mode_gated(exec, "declare -a arr=(x)\necho \"[${arr@a}]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[a]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "@a query is not a type error");

    executor_free(exec);
}

TEST(gate_lush_mode_bare_op_still_strict) {
    /// Regression: lush mode still rejects a scalar operator on a bare
    /// collection with E1134, for every operator class. A fresh executor per
    /// operator -- the E1134 path calls executor_request_posix_exit, whose
    /// abort/error state would otherwise leak into the next iteration on a
    /// shared executor.
    const char *ops[] = {"${arr:-D}",  "${arr#a}",   "${arr^^}",
                         "${arr:1:2}", "${arr/a/b}", "${arr:=X}"};
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        executor_t *exec = executor_new();
        ASSERT_NOT_NULL(exec, "executor_new failed");
        char src[128];
        snprintf(src, sizeof(src), "arr=(a b c)\nx=%s\necho saw_post\n",
                 ops[i]);
        run_result_t r = run_shell_mode_gated(exec, src);
        ASSERT_TRUE(strstr(r.out, "saw_post") == NULL,
                    "lush mode aborts on scalar-op-on-collection");
        ASSERT_STDERR_CONTAINS(r, "type mismatch");
        executor_free(exec);
    }
}

/* ============================================================================
 * SEMANTICS section 3.9 mode gating -- export / readonly of an array literal
 *
 * `export a=(x y z)` / `readonly a=(x y z)` bind a list into an
 * assignment-aware builtin. In lush mode that is the scalar-slot type
 * error. The compat modes relax it to the oracle: bash binds the indexed
 * array (marking it exported / readonly). A later bare $a read follows the
 * mode section 3.9 flatten (element 0 for bash, whole-join for zsh) --
 * already covered by the read-site gate.
 * ============================================================================
 */

/// The shared global symbol table persists bindings across the
/// fresh-executor tests, so each of these uses a unique variable name --
/// especially the readonly cases, whose attribute cannot be unset and would
/// otherwise poison a common name for the rest of the suite.

TEST(gate_bash_mode_export_binds_array) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// bash creates the indexed array; $eb is element 0, ${eb[1]} the second,
    /// and the count is three.
    run_result_t r = run_shell_mode_gated(
        exec, "mode bash\nexport pr2c_eb=(x y z)\n"
              "echo \"0=$pr2c_eb 1=${pr2c_eb[1]} n=${#pr2c_eb[@]}\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "0=x 1=y n=3");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 for export array in bash mode");

    executor_free(exec);
}

TEST(gate_zsh_mode_export_binds_array_whole_join_read) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// zsh binds the array too; a bare $name reads the whole space-join.
    run_result_t r =
        run_shell_mode_gated(exec, "mode zsh\nexport pr2c_ez=(x y z)\n"
                                   "echo \"bare=$pr2c_ez n=${#pr2c_ez[@]}\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "bare=x y z n=3");

    executor_free(exec);
}

TEST(gate_bash_mode_readonly_binds_array_and_enforces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// readonly binds the indexed array and the readonly attribute is
    /// enforced -- a later element write is rejected and the value is
    /// unchanged.
    run_result_t r = run_shell_mode_gated(
        exec, "mode bash\nreadonly pr2c_rb=(p q r)\npr2c_rb[0]=CHANGED\n"
              "echo \"after=${pr2c_rb[0]} n=${#pr2c_rb[@]}\"\n");

    ASSERT_STDOUT_CONTAINS(r, "after=p n=3");
    ASSERT_STDERR_CONTAINS(r, "readonly");

    executor_free(exec);
}

TEST(gate_lush_mode_export_array_still_strict) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Regression: lush mode still rejects export of an array literal with
    /// the type error and binds nothing. (A failed builtin sets $? and the
    /// script continues -- unlike the expansion-time aborts -- so the check
    /// is the diagnostic plus the absence of a binding, not an abort.) The
    /// unique name proves non-binding: it is empty only if the export did
    /// not create the array.
    run_result_t r = run_shell_mode_gated(
        exec, "export pr2c_le=(x y z)\necho \"check=[${pr2c_le[0]}]\"\n");

    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_STDOUT_CONTAINS(r, "check=[]");

    executor_free(exec);
}

TEST(gate_lush_mode_readonly_array_still_strict) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Regression: lush mode still rejects readonly of an array literal with
    /// the type error and binds nothing.
    run_result_t r = run_shell_mode_gated(
        exec, "readonly pr2c_lr=(p q r)\necho \"check=[${pr2c_lr[0]}]\"\n");

    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_STDOUT_CONTAINS(r, "check=[]");

    executor_free(exec);
}

/* ============================================================================
 * SEMANTICS section 3.9 mode gating -- vector-producing parameter-flag forms
 *
 * The (v)/(k)/(kv)/(@)/(s)-on-collection flag forms produce a list; in a
 * scalar slot they are the same list-in-scalar crossing as ${arr[@]}: strict
 * E1134 in lush mode, oracle flatten (bash/posix space, zsh IFS[0]) in the
 * compat modes. Vector-accepting slots still spread (try_expand_vector_arg).
 * The explicit (j:..:) join is scalar by design and never gated; (s:..:) on a
 * genuine scalar is a legitimate list-from-scalar and is not gated. The
 * kind-preserving transforms ((o)/(u)/(U)/(L)/(C)) are out of scope and keep
 * their existing behavior.
 * ============================================================================
 */

TEST(gate_lush_mode_flag_vector_forms_scalar_type_error) {
    /// Each list-producing flag form in a scalar slot is E1134 in lush mode.
    /// Fresh executor per form (the E1134 path posix-exits, whose state would
    /// leak on a shared executor).
    const char *srcs[] = {
        "a=(echo hi there)\nv=\"${(v)a}\"\necho X\n",
        "a=(x y z)\nv=\"${(k)a}\"\necho X\n",
        "a=(x y z)\nv=\"${(@)a}\"\necho X\n",
        "a=(x y z)\nv=\"${(s: :)a}\"\necho X\n",
        "declare -A m=([x]=1 [y]=2)\nv=\"${(kv)m}\"\necho X\n",
    };
    for (size_t i = 0; i < sizeof(srcs) / sizeof(srcs[0]); i++) {
        executor_t *exec = executor_new();
        ASSERT_NOT_NULL(exec, "executor_new failed");
        run_result_t r = run_shell_mode_gated(exec, srcs[i]);
        ASSERT_STDERR_CONTAINS(r, "type mismatch");
        executor_free(exec);
    }
}

TEST(gate_bash_mode_flag_vector_flattens_on_space) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// bash mode: (v) in a scalar slot flattens on a literal space,
    /// ignoring IFS.
    run_result_t r = run_shell_mode_gated(
        exec, "mode bash\nIFS=,\na=(w x y)\nv=\"${(v)a}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w x y]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "no E1134 in bash mode");

    executor_free(exec);
}

TEST(gate_zsh_mode_flag_vector_flattens_on_ifs) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// zsh mode: (v) and (@) in a scalar slot flatten on IFS[0].
    run_result_t r = run_shell_mode_gated(
        exec, "mode zsh\nIFS=:\na=(w x y)\nv=\"${(@)a}\"\necho \"[$v]\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "[w:x:y]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL, "no E1134 in zsh mode");

    executor_free(exec);
}

TEST(gate_flag_j_explicit_join_not_gated) {
    /// (j:..:) is the sanctioned list->scalar collapse and must never be the
    /// type error, in any mode.
    run_result_t r =
        run_shell("a=(echo hi there)\nv=\"${(j: :)a}\"\necho \"[$v]\"\n");

    ASSERT_STDOUT_CONTAINS(r, "[echo hi there]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "(j) is not a type error");
}

TEST(gate_flag_s_on_scalar_not_gated) {
    /// (s:..:) on a genuine scalar is a legitimate list-from-scalar, not a
    /// collection-in-scalar crossing; it must not be gated.
    run_result_t r =
        run_shell("str=a.b.c\nv=\"${(s:.:)str}\"\necho \"[$v]\"\n");

    ASSERT_STDOUT_CONTAINS(r, "[a b c]");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "(s) on scalar not gated");
}

TEST(gate_flag_vector_slot_still_spreads) {
    /// Regression: the flag forms still spread in vector-accepting slots
    /// (argv, for-loop) -- the gate is scalar-slot only.
    run_result_t r =
        run_shell("a=(x y z)\nc(){ echo \"n=$#\"; }\nc ${(v)a}\n"
                  "for w in ${(v)a}; do printf '<%s>' \"$w\"; done\necho\n");

    ASSERT_STDOUT_CONTAINS(r, "n=3");
    ASSERT_STDOUT_CONTAINS(r, "<x><y><z>");
    ASSERT_TRUE(strstr(r.err, "type mismatch") == NULL,
                "vector slot spreads, no type error");
}

/* ============================================================================
 * LOGICAL OPERATOR TESTS
 * ============================================================================
 */

TEST(and_operator_success) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "true && RESULT=yes");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "yes", "Second command should run");
    free(result);

    executor_free(exec);
}

TEST(and_operator_fail) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Set RESULT first, then verify it's NOT changed
    executor_execute_command_line(exec, "RESULT=initial", 1);
    run_result_t r = run_shell_with_executor(exec, "false && RESULT=changed");
    ASSERT_EXIT_STATUS(r, 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should still be set");
    ASSERT_STR_EQ(result, "initial", "Second command should NOT run");
    free(result);

    executor_free(exec);
}

TEST(or_operator_success) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// First succeeds, second should not run
    executor_execute_command_line(exec, "RESULT=initial", 1);
    run_result_t r = run_shell_with_executor(exec, "true || RESULT=changed");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "initial", "Second command should NOT run");
    free(result);

    executor_free(exec);
}

TEST(or_operator_fail) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "false || RESULT=yes");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "yes", "Second command should run");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * FUNCTION TESTS
 * ============================================================================
 */

TEST(function_definition_posix) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "myfunc() { CALLED=yes; }");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "myfunc");
    ASSERT_EXIT_STATUS(r, 0);

    char *called = symtable_get_var(exec->symtable, "CALLED");
    ASSERT_NOT_NULL(called, "CALLED should be set");
    ASSERT_STR_EQ(called, "yes", "Function should have been called");
    free(called);

    executor_free(exec);
}

TEST(function_definition_ksh) {
    /// Tests Issue #56 fix - function without parentheses
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "function myfunc { CALLED=yes; }");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "myfunc");
    ASSERT_EXIT_STATUS(r, 0);

    char *called = symtable_get_var(exec->symtable, "CALLED");
    ASSERT_NOT_NULL(called, "CALLED should be set");
    ASSERT_STR_EQ(called, "yes", "Function should have been called");
    free(called);

    executor_free(exec);
}

TEST(function_with_args) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "setarg() { ARG1=$1; ARG2=$2; }", 1);
    run_result_t r = run_shell_with_executor(exec, "setarg hello world");
    ASSERT_EXIT_STATUS(r, 0);

    char *arg1 = symtable_get_var(exec->symtable, "ARG1");
    char *arg2 = symtable_get_var(exec->symtable, "ARG2");
    ASSERT_STR_EQ(arg1, "hello", "ARG1 should be hello");
    ASSERT_STR_EQ(arg2, "world", "ARG2 should be world");
    free(arg1);
    free(arg2);

    executor_free(exec);
}

TEST(function_return) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "retfunc() { return 42; }", 1);
    run_result_t r = run_shell_with_executor(exec, "retfunc");
    ASSERT_EXIT_STATUS(r, 42);

    executor_free(exec);
}

/* ============================================================================
 * REGRESSION TESTS -- Issue #47
 * Variables declared with local/declare/typeset must honor subsequent
 * assignments using POSIX scope-chain semantics: an unprefixed `name=value`
 * in a function should update the existing local, not silently create or
 * update a global. The same applies to += append, for-loop variables, and
 * other implicit-assignment forms -- they all share the same root cause
 * (unconditional global write at the executor level).
 *
 * Each test below stores the post-assignment value in a global RESULT
 * variable so the test can inspect it after the function returns. Until
 * the fix lands, RESULT reflects the *initial* local value rather than
 * the value the assignment specified.
 * ============================================================================
 */

/* Parser-internal sentinel discrimination: `local arr=(a b c)` is an
 * array literal (parser emits the bundled form prefixed with \x1F),
 * while `local data="(scoped)"` is a scalar (regular argument path,
 * no sentinel). These tests pin the discrimination so a future
 * refactor of the parser path or the builtin-side sentinel-stripping
 * shows up as a regression. Each test uses unique function and
 * variable names because the global manager persists across
 * executor_free calls in this test binary, so reusing names risks
 * cross-test interference. */
TEST(local_array_literal_builds_indexed_array) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "lalf() { local larr=(a b c); echo len=${#larr[@]} "
              "first=${larr[0]}; }\nlalf\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "len=3");
    ASSERT_STDOUT_CONTAINS(r, "first=a");

    executor_free(exec);
}

TEST(rt_declare_array_quoted_element_keeps_spaces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// A quoted array-literal element that contains spaces must survive as one
    /// element through the declare sentinel channel (issue #546). The old
    /// space-joined sentinel re-split the quoted value into separate words.
    run_result_t r =
        run_shell_with_executor(exec, "declare -a p546a=(\"x y\" z)\n"
                                      "printf '(%s)' \"${p546a[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(x y)(z)\n");

    executor_free(exec);
}

TEST(rt_declare_assoc_quoted_value_keeps_spaces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The same boundary loss corrupted an associative `[key]="a b"` slot into
    /// key `a` plus a stray index; the value must stay intact.
    run_result_t r = run_shell_with_executor(
        exec, "declare -A p546m=([k]=\"a b\" [j]=c)\n"
              "printf '(%s)' \"${p546m[k]}\" \"${p546m[j]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(a b)(c)\n");

    executor_free(exec);
}

TEST(rt_declare_array_plain_elements_regression) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Guard: ordinary space-separated elements still become distinct slots.
    run_result_t r =
        run_shell_with_executor(exec, "declare -a p546p=(one two three)\n"
                                      "printf '(%s)' \"${p546p[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(one)(two)(three)\n");

    executor_free(exec);
}

TEST(rt_scalar_value_with_unit_separator_no_crash) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// A scalar holding a literal 0x1F -- the byte lush uses internally as
    /// the symtable value/metadata separator -- must store and expand without
    /// corrupting the binding. Previously the value was mis-typed as an array
    /// and dereferenced as a pointer, crashing on expansion and on exit
    /// (issue #550). $'\x1f' produces the byte; length 3 proves it survived
    /// the round-trip, and the trailing marker proves expansion did not crash.
    run_result_t r = run_shell_with_executor(
        exec, "p550us=$'1\\x1f2'\necho \"len=${#p550us}\"\necho \"[$p550us]\" "
              ">/dev/null\necho survived\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "len=3");
    ASSERT_STDOUT_CONTAINS(r, "survived");

    executor_free(exec);
}

TEST(rt_array_literal_word_to_nonassignment_command_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// An array literal `name=(...)` is only valid as an assignment or as an
    /// operand to an assignment-aware builtin. As an argument to any other
    /// command the unquoted `(` is a syntax error -- bash, zsh, and POSIX sh
    /// all reject it, and lush reports why rather than silently flattening the
    /// word into a string (which is what it used to do).
    run_result_t r = run_shell_with_executor(
        exec, "f546n() { echo \"$1\"; }\nf546n a=(1 2)\n");

    ASSERT_STDERR_CONTAINS(r, "not valid as an argument");
    ASSERT_TRUE(strstr(r.out, "a=(1 2)") == NULL,
                "the invalid word must not be flattened and executed");

    executor_free(exec);
}

TEST(rt_array_literal_word_quoted_is_a_literal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The escape hatch: a quoted `"name=(...)"` is an ordinary literal word
    /// and is passed through unchanged (no array meaning, no rejection).
    run_result_t r = run_shell_with_executor(
        exec, "f546q() { echo \"$1\"; }\nf546q \"a=(1 2)\"\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a=(1 2)\n");

    executor_free(exec);
}

TEST(rt_array_literal_in_command_sub_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The same rejection applies inside `$(...)`: the invalid construct fails
    /// to parse, so the capture is empty rather than a flattened `q=(1 2)`.
    run_result_t r = run_shell_with_executor(
        exec, "p546s=$(echo q=(1 2))\necho \"val=[$p546s]\"\n");

    ASSERT_STDOUT_CONTAINS(r, "val=[]");

    executor_free(exec);
}

TEST(rt_declare_array_quoted_paren_in_element) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// A ')' inside a quoted element is ordinary text, not the array
    /// terminator; the \x1F-delimited channel matches bash/zsh and the shell's
    /// own bare-assignment path (which never truncated here).
    run_result_t r =
        run_shell_with_executor(exec, "declare -a p546c=(\"a )\" b)\n"
                                      "printf '(%s)' \"${p546c[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(a ))(b)\n");

    executor_free(exec);
}

TEST(rt_declare_array_append_extends) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// `declare NAME+=(...)` extends the existing array rather than being
    /// rejected as an invalid identifier `NAME+` (issue #549): the sentinel
    /// name split lands on the `=` of `+=`, which the consumer now detects.
    run_result_t r = run_shell_with_executor(
        exec, "declare -a p549a=(x y)\ndeclare p549a+=(p q)\n"
              "printf '(%s)' \"${p549a[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(x)(y)(p)(q)\n");

    executor_free(exec);
}

TEST(rt_declare_array_append_to_absent_creates) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Appending to a name with no array yet creates one (bash parity).
    run_result_t r = run_shell_with_executor(
        exec, "declare p549b+=(m n)\nprintf '(%s)' \"${p549b[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(m)(n)\n");

    executor_free(exec);
}

TEST(rt_declare_array_plain_assign_replaces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// A plain `NAME=(...)` still replaces; only `+=` appends.
    run_result_t r = run_shell_with_executor(
        exec, "declare -a p549c=(1 2 3)\ndeclare p549c=(9)\n"
              "printf '(%s)' \"${p549c[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(9)\n");

    executor_free(exec);
}

/* ==========================================================================
 * #614: a bare / declare -g array assignment inside a function resolves scope
 * the same way a scalar assignment does -- an existing outer binding is
 * updated in place, an undeclared name persists to the enclosing/global scope
 * (not the function frame), and `local -a` still stays local. Distinct array
 * names per test because the symbol table is process-global here. Element
 * cases use index 1 so the scope behavior is independent of the array's
 * 0-vs-1 index base. Verified identical in bash 5.3 and zsh 5.9.
 * ========================================================================== */

TEST(rt_array_fn_scope_bare_whole) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "f(){ s614a=(x y z); }\nf\necho \"${s614a[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "x y z\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_bare_element) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "f(){ s614b[1]=7; }\nf\necho \"${s614b[1]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "7\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_bare_append) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "f(){ s614c+=(x y); }\nf\necho \"${s614c[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "x y\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_arith_element) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The arithmetic element writer (( a[i]=v )) routes through the same
    /// element-create path, so it must persist to global too.
    run_result_t r = run_shell_with_executor(
        exec, "f(){ (( s614d[1] = 5 )); }\nf\necho \"${s614d[1]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_declare_g_indexed) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// declare -g -a forces the array global; the later element write finds it.
    run_result_t r = run_shell_with_executor(
        exec, "f(){ declare -g -a s614g; s614g[1]=7; }\nf\n"
              "echo \"${s614g[1]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "7\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_declare_g_assoc) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "f(){ declare -gA s614h; s614h[k]=v; }\nf\n"
              "echo \"${s614h[k]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "v\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_scalar_promotion) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A bare element write to an existing global SCALAR promotes it to an
    /// array in the scope the scalar lives in (global), not the frame. Under
    /// the #621 kind-transition policy this promotion is a relaxed-mode
    /// behavior (lush mode refuses the implicit re-kind), so this runs in bash
    /// mode: the former scalar is preserved as the base element (#621) and the
    /// promoted array lands in the owning/global scope (#614).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns614p=hello\nf(){ s614p[1]=7; }\nf\n"
              "echo \"${s614p[0]}:${s614p[1]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "hello:7\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_whole_replace_global) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "s614r=(x y z)\nf(){ s614r=(P Q); }\nf\necho \"${s614r[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "P Q\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_nested_outer_local) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The load-bearing resolver case: a bare array assignment in an INNER
    /// function updates the OUTER function's local (dynamic scope), it does
    /// NOT fall through to global. A naive current-scope-miss-to-global fix
    /// would wrongly create a global here. Mirrors the scalar resolver.
    run_result_t r = run_shell_with_executor(
        exec, "inner(){ s614n=(P Q); }\n"
              "g(){ local -a s614n; s614n=(init); inner; "
              "echo \"outer:${s614n[*]}\"; }\n"
              "g\necho \"global-n:${#s614n[@]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "outer:P Q\nglobal-n:0\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_scalar_control_unchanged) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: the scalar path is untouched -- a bare scalar assignment in a
    /// function still persists to global exactly as before.
    run_result_t r = run_shell_with_executor(
        exec, "f(){ s614s=hi; }\nf\necho \"${s614s}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "hi\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_local_stays_local) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: an explicit `local -a` (no -g) still stays frame-local and is
    /// gone after the function returns.
    run_result_t r =
        run_shell_with_executor(exec, "f(){ local -a s614l; s614l[1]=7; }\nf\n"
                                      "echo \"n:${#s614l[@]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "n:0\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_inplace_mutation) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: mutating a PRE-EXISTING global array in place from a function
    /// still persists (read side already walked the chain; unchanged).
    run_result_t r = run_shell_with_executor(
        exec, "s614m=(x y z)\nf(){ s614m[2]=CHG; }\nf\necho \"${s614m[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "x y CHG\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_local_shadow_not_clobbered) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: a `local -a` shadow of a global array keeps element writes
    /// local; the global is not clobbered (proves the shadow/double-free
    /// protection in symtable_set_array is preserved at the owning scope).
    run_result_t r = run_shell_with_executor(
        exec, "s614q=(x y z)\nf(){ local -a s614q; s614q[1]=Z; }\nf\n"
              "echo \"${s614q[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "x y z\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_readonly_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// No-regression: reassigning a readonly array from a function is still
    /// refused (up-front guard), the array is unchanged, and the resolver's
    /// chain readonly check does not weaken this.
    run_result_t r = run_shell_with_executor(
        exec, "declare -ar s614ro=(x y z)\nf(){ s614ro=(P Q); }\nf\n"
              "echo \"${s614ro[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "x y z\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_readonly_scalar_not_clobbered) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The resolver must not let a bare array assignment in a function clobber
    /// a readonly SCALAR in an enclosing scope (its readonly bit is in the
    /// symvar flags, not array->flags). Whole-replace form.
    run_result_t r = run_shell_with_executor(
        exec, "readonly s614rs=5\nf(){ s614rs=(9 9); }\nf\necho \"$s614rs\"\n");
    ASSERT_STDOUT_EQ(r, "5\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_readonly_scalar_element_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Element-write form of the same: readonly scalar is not promoted.
    run_result_t r = run_shell_with_executor(
        exec, "readonly s614re=5\nf(){ s614re[0]=9; }\nf\necho \"$s614re\"\n");
    ASSERT_STDOUT_EQ(r, "5\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_readonly_array_append_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A bare `arr+=(...)` onto a readonly array is refused (the append path
    /// gained an up-front readonly guard); the array is unchanged.
    run_result_t r = run_shell_with_executor(
        exec, "declare -ar s614ra=(x y)\ns614ra+=(z)\necho \"${s614ra[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "x y\n");
    executor_free(exec);
}

TEST(rt_array_fn_scope_read_a_readonly_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// `read -a` onto a readonly array is refused via the resolver's readonly
    /// return surfaced by bin_read's targeted diagnostic; array unchanged.
    run_result_t r = run_shell_with_executor(
        exec, "declare -ar s614rr=(1 2)\nread -a s614rr <<< \"p q\"\n"
              "echo \"${s614rr[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "1 2\n");
    executor_free(exec);
}

/* ==========================================================================
 * #620: `unset` inside a function must honor the scope that owns the binding.
 * Previously it freed a parent-owned array while stamping the UNSET tombstone
 * into the function frame, leaving the owning scope's serialized entry
 * dangling -- a use-after-free on the next read (the array cases here would
 * SIGABRT under ASan). Fixed by resolving the owning scope for both the free
 * and the tombstone. Distinct names (process-global symbol table).
 * ========================================================================== */

TEST(rt_unset_array_from_fn_no_uaf) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The regression case: unset a global array from a function, then read it
    /// -- must unset the global (0) and NOT use-after-free (caught under ASan).
    run_result_t r = run_shell_with_executor(
        exec, "arr620=(g1 g2 g3)\nf(){ unset arr620; }\nf\n"
              "echo \"n=${#arr620[@]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "n=0\n");
    executor_free(exec);
}

TEST(rt_unset_scalar_from_fn_unsets_global) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "x620=v\nf(){ unset x620; }\nf\necho \"[${x620-UNSET}]\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "[UNSET]\n");
    executor_free(exec);
}

TEST(rt_unset_then_reassign_from_fn) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// After unsetting the global from a function, a bare reassignment resolves
    /// back to the global (the tombstone lives in the owning scope now).
    run_result_t r = run_shell_with_executor(
        exec, "s620=hi\nf(){ unset s620; s620=bye; }\nf\necho \"$s620\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "bye\n");
    executor_free(exec);
}

TEST(rt_mapfile_from_fn_persists) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// mapfile unsets the target first; with the owning-scope fix that unset no
    /// longer mis-plants a tombstone in the frame, so the created array reaches
    /// the enclosing/global scope.
    run_result_t r = run_shell_with_executor(
        exec, "f(){ mapfile -t m620 < <(printf 'a\\nb\\nc\\n'); "
              "echo \"in=${#m620[@]}\"; }\nf\necho \"out=${#m620[@]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "in=3\nout=3\n");
    executor_free(exec);
}

TEST(rt_unset_local_global_survives) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: unsetting a LOCAL array does not touch the global binding
    /// (the free/tombstone target the function scope, which pops).
    run_result_t r = run_shell_with_executor(
        exec, "arr620s=(x y z)\nf(){ local -a arr620s=(1 2); unset arr620s; }\n"
              "f\necho \"${arr620s[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "x y z\n");
    executor_free(exec);
}

TEST(rt_unset_nonexistent_ok) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: unsetting an unbound name is a silent no-op success and does
    /// not plant a spurious tombstone.
    run_result_t r =
        run_shell_with_executor(exec, "unset NEVERSET620\necho \"rc=$?\"\n");
    ASSERT_STDOUT_EQ(r, "rc=0\n");
    executor_free(exec);
}

TEST(rt_unset_repeated_local_preserves_global) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A repeated `unset` of a locally-shadowed name must NOT reach through the
    /// local tombstone and destroy the outer/global binding: the second unset
    /// is a no-op (bash/zsh consensus). Scalar and array forms.
    run_result_t r = run_shell_with_executor(
        exec, "g620d=CFG\nf(){ local g620d=L; unset g620d; unset g620d; }\nf\n"
              "echo \"[${g620d-U}]\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "[CFG]\n");
    executor_free(exec);

    exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r2 = run_shell_with_executor(
        exec, "arr620d=(x y z)\nf(){ local -a arr620d=(1 2); unset arr620d; "
              "unset arr620d; }\nf\necho \"[${arr620d[*]}]\"\n");
    ASSERT_EXIT_STATUS(r2, 0);
    ASSERT_STDOUT_EQ(r2, "[x y z]\n");
    executor_free(exec);
}

/* ==========================================================================
 * #626: `unset` honors the readonly immutability invariant. A readonly binding
 * may be neither reassigned nor destroyed; the write surfaces already refuse
 * with E1117 (#614/#620/#621), so `unset` -- also a mutation surface -- must
 * refuse a readonly scalar, indexed array, associative, or an element of a
 * readonly array, leaving the binding intact and returning non-zero. The
 * invariant is mode-independent (readonly is enforced in every mode), so one
 * case is exercised under `mode bash` to prove universality.
 * ========================================================================== */

TEST(rt_unset_readonly_scalar_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Was silently protected with rc=0 and no diagnostic; now E1117 + non-zero
    /// and the scalar survives.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nreadonly ro626s=5\nunset ro626s\necho \"rc=$? "
              "v=[${ro626s}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "rc=1 v=[5]\n");
    executor_free(exec);
}

TEST(rt_unset_readonly_array_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Was silently DESTROYED (freed outright); now refused and intact.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\ndeclare -ar ro626a=(1 2 3)\nunset ro626a\n"
              "echo \"rc=$? n=${#ro626a[@]} v=[${ro626a[*]}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "rc=1 n=3 v=[1 2 3]\n");
    executor_free(exec);
}

TEST(rt_unset_readonly_assoc_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Associative was silently destroyed; now refused and intact.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\ndeclare -Ar ro626m=([k]=v)\nunset ro626m\n"
              "echo \"rc=$? v=[${ro626m[k]}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "rc=1 v=[v]\n");
    executor_free(exec);
}

TEST(rt_unset_readonly_element_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Removing one element mutates the readonly array -- refused, all elements
    /// intact.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\ndeclare -ar ro626e=(a b c)\nunset \"ro626e[0]\"\n"
              "echo \"rc=$? n=${#ro626e[@]} v=[${ro626e[*]}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "rc=1 n=3 v=[a b c]\n");
    executor_free(exec);
}

TEST(rt_unset_readonly_global_from_fn_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The readonly check is scope-chain aware: unsetting a readonly GLOBAL
    /// from within a function scope is refused and the global survives.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nreadonly ro626g=7\nf(){ unset ro626g; }\nf\n"
              "echo \"v=[${ro626g}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "v=[7]\n");
    executor_free(exec);
}

TEST(rt_unset_readonly_universal_bash_mode) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The immutability invariant is mode-independent: a readonly unset is
    /// refused under `mode bash` exactly as under lush mode.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nreadonly ro626b=9\nunset ro626b\necho \"rc=$? "
              "v=[${ro626b}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "rc=1 v=[9]\n");
    executor_free(exec);
}

TEST(rt_unset_readonly_partial_continues) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// `unset ro plain` refuses the readonly name (non-zero) but still unsets
    /// the remaining writable name -- builtin argument-loop discipline.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nreadonly ro626p=5\nplain626=keep\n"
              "unset ro626p plain626\n"
              "echo \"rc=$? ro=[${ro626p}] plain=[${plain626-U}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "rc=1 ro=[5] plain=[U]\n");
    executor_free(exec);
}

TEST(rt_unset_writable_still_works_control) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: without the readonly attribute the whole matrix still unsets
    /// cleanly (scalar, whole array, single element) with rc=0.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nw626s=v\nw626a=(a b c)\nw626e=(x y z)\n"
              "unset w626s\nunset w626a\nunset \"w626e[1]\"\n"
              "echo \"rc=$? s=[${w626s-U}] a=${#w626a[@]} e=[${w626e[*]}]\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "rc=0 s=[U] a=0 e=[x z]\n");
    executor_free(exec);
}

TEST(rt_unset_readonly_nameref_target_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// `unset` resolves a nameref to its target, so a nameref pointing at a
    /// readonly binding is refused on the resolved TARGET (the diagnostic names
    /// the target, not the nameref) and the target survives. This is the only
    /// path that reaches the readonly branch after the nameref-resolution
    /// allocation, so it also guards that owned string's ownership.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nreadonly ro626n=5\ndeclare -n ref626=ro626n\n"
              "unset ref626\necho \"rc=$? v=[${ro626n}]\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDERR_CONTAINS(r, "ro626n");
    ASSERT_STDOUT_EQ(r, "rc=1 v=[5]\n");
    executor_free(exec);
}

/* ==========================================================================
 * #625: the child environment (the flat process environ handed to execvp) is a
 * mirror of the exported-scalar bindings. Any operation that removes a name
 * from that set -- unset, or a scalar being replaced by an array (which has no
 * exported-scalar form: element promotion, whole s=(...), read -a, mapfile) --
 * must drop the stale entry from the mirror, the DROP counterpart to export's
 * setenv ADD. symtable_environ_resync re-materializes the entry from the
 * currently-visible binding, so a shadowed exported binding is re-exposed
 * rather than lost. Verified against a real child: `env` is external, so it
 * reads the process environ through execvp. Distinct names per test (the
 * process environ is shared across the whole test binary).
 * ========================================================================== */

TEST(rt_env625_unset_drops_export) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The reported bug: an unset exported variable lingered in the child
    /// environ. Now the child sees no e625u.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nexport e625u=hi\nunset e625u\n"
              "env | grep -c '^e625u=' || true\n");
    ASSERT_STDOUT_EQ(r, "0\n");
    executor_free(exec);
}

TEST(rt_env625_unset_shadow_preserves_global) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The row that must NOT regress: unsetting a non-exported local shadow
    /// re-exposes the exported global, which stays in the child environ with
    /// its original value. resync reads the visible binding to get this right.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nexport e625sh=gv\n"
              "f(){ local e625sh=lv; unset e625sh; }\nf\n"
              "env | grep '^e625sh=' || echo NONE\n");
    ASSERT_STDOUT_EQ(r, "e625sh=gv\n");
    executor_free(exec);
}

TEST(rt_env625_promote_element_drops) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A relaxed-mode element promotion turns the exported scalar into an
    /// array; the stale scalar entry is dropped from the child environ.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nexport e625pe=hi\ne625pe[1]=x\n"
              "env | grep -c '^e625pe=' || true\n");
    ASSERT_STDOUT_EQ(r, "0\n");
    executor_free(exec);
}

TEST(rt_env625_promote_whole_assign_drops) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Whole-array reassignment over an exported scalar drops it too (same
    /// chokepoint, symtable_assign_array).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nexport e625pw=hi\ne625pw=(a b)\n"
              "env | grep -c '^e625pw=' || true\n");
    ASSERT_STDOUT_EQ(r, "0\n");
    executor_free(exec);
}

TEST(rt_env625_promote_reada_drops) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// read -a materializes an array through the same chokepoint, so an
    /// exported scalar target is dropped from the child environ.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nexport e625pr=hi\nread -a e625pr <<< 'x y'\n"
              "env | grep -c '^e625pr=' || true\n");
    ASSERT_STDOUT_EQ(r, "0\n");
    executor_free(exec);
}

TEST(rt_env625_lush_refused_keeps_export) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// In lush mode the kind transition is refused (#621), so no array is
    /// stored, resync never fires, and the exported scalar is untouched in the
    /// child environ. The arithmetic form is non-fatal (its E1134 goes to
    /// stderr), so the script continues to the env check.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nexport e625lr=hi\n(( e625lr[1]=7 ))\n"
              "env | grep '^e625lr=' || echo NONE\n");
    ASSERT_STDOUT_EQ(r, "e625lr=hi\n");
    executor_free(exec);
}

TEST(rt_env625_reexport_after_unset) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The mirror is not left in a broken state: re-exporting a name after
    /// unsetting it makes the new value visible to the child.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nexport e625re=1\nunset e625re\nexport e625re=2\n"
              "env | grep '^e625re=' || echo NONE\n");
    ASSERT_STDOUT_EQ(r, "e625re=2\n");
    executor_free(exec);
}

TEST(rt_env625_nonexported_unset_noop) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: unsetting a never-exported variable neither errors nor adds a
    /// spurious child environ entry.
    run_result_t r =
        run_shell_with_executor(exec, "mode lush\ne625ne=v\nunset e625ne\n"
                                      "env | grep -c '^e625ne=' || true\n");
    ASSERT_STDOUT_EQ(r, "0\n");
    executor_free(exec);
}

TEST(rt_env625_local_array_shadow_preserves_global_element) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A LOCAL array shadowing an exported global must NOT touch the mirror:
    /// the resync is gated to global-scope stores, so after the frame pops the
    /// exported global is still visible to children (it was never dropped).
    /// Element form.
    run_result_t r =
        run_shell_with_executor(exec, "mode bash\nexport e625ls=g\n"
                                      "f(){ local e625ls=l; e625ls[0]=x; }\nf\n"
                                      "env | grep '^e625ls=' || echo NONE\n");
    ASSERT_STDOUT_EQ(r, "e625ls=g\n");
    executor_free(exec);
}

TEST(rt_env625_local_array_shadow_preserves_global_reada) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Same, via read -a into a local shadow of an exported global.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nexport e625lr2=g\n"
              "f(){ local e625lr2=l; read -a e625lr2 <<< 'p q'; }\nf\n"
              "env | grep '^e625lr2=' || echo NONE\n");
    ASSERT_STDOUT_EQ(r, "e625lr2=g\n");
    executor_free(exec);
}

TEST(rt_env625_promote_mapfile_drops) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// mapfile materializes an array through the same global-scope chokepoint,
    /// so an exported global scalar target is dropped from the child environ.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nexport e625mf=hi\n"
              "mapfile -t e625mf < <(printf 'a\\nb\\n')\n"
              "env | grep -c '^e625mf=' || true\n");
    ASSERT_STDOUT_EQ(r, "0\n");
    executor_free(exec);
}

/* ==========================================================================
 * #627: the aggregate selector [@]/[*] denotes the whole array (all elements)
 * in read contexts and is not a single writable element address, so it is
 * rejected as an assignment target -- uniformly for indexed and associative
 * arrays, in every mode. The check is syntactic (the literal @ or *), fires
 * any value-expand or #621 scalar->array promotion, and is non-fatal (the
 * assignment fails with a non-zero status; the script continues). A readonly
 * target reports readonly first (#621 precedence). A dynamic subscript
 * (name[$k]=v with k=@) is a distinct literal-key write and is left alone.
 * ========================================================================== */

TEST(rt_sub627_indexed_at_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Distinct names per test: symtable_assign_array uses the process-global
    /// manager, so array bindings (esp. the readonly one below) leak across the
    /// shared test binary.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627a=(1 2 3)\ns627a[@]=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627a[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 [1 2 3]\n");
    executor_free(exec);
}

TEST(rt_sub627_indexed_star_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627b=(1 2 3)\ns627b[*]=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627b[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 [1 2 3]\n");
    executor_free(exec);
}

TEST(rt_sub627_indexed_append_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The `+=` element form routes through the same path; also rejected.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627c=(1 2 3)\ns627c[@]+=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627c[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 [1 2 3]\n");
    executor_free(exec);
}

TEST(rt_sub627_assoc_at_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Uniform: rejected for associative too (lush declines bash's reinterpret-
    /// as-literal-key, which would overload [@] read-vs-write). Map unchanged.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ndeclare -A m627d\nm627d[k]=v\nm627d[@]=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${m627d[k]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 [v]\n");
    executor_free(exec);
}

TEST(rt_sub627_scalar_not_promoted) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Rejected before the #621 promote path, so the scalar is left a scalar.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627e=hi\ns627e[@]=x\n"
              "printf 'rc=%s s=[%s]\\n' \"$?\" \"$s627e\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 s=[hi]\n");
    executor_free(exec);
}

TEST(rt_sub627_uniform_in_lush_mode) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Target validity is not gated by FEATURE_STRICT_VALUE_TYPING: rejected in
    /// lush mode too.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\ns627f=(1 2 3)\ns627f[@]=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627f[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 [1 2 3]\n");
    executor_free(exec);
}

TEST(rt_sub627_readonly_precedence) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A readonly target reports readonly (E1117) first, before the subscript
    /// check -- consistent with #621 readonly precedence. Array unchanged.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nreadonly s627g=(1 2 3)\ns627g[@]=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627g[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_EQ(r, "rc=1 [1 2 3]\n");
    executor_free(exec);
}

TEST(rt_sub627_control_normal_index_ok) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: a normal numeric index still assigns.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627h=(1 2 3)\ns627h[1]=x\nprintf '[%s]\\n' "
              "\"${s627h[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "[1 x 3]\n");
    executor_free(exec);
}

TEST(rt_sub627_control_normal_key_and_varkey_ok) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: a normal associative key assigns, and the variable escape hatch
    /// for a literal @ key (k=@; m[$k]=x) works -- the dynamic subscript is not
    /// the syntactic aggregate.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ndeclare -A m627i\nm627i[k]=v\nk=@\nm627i[$k]=x\n"
              "printf '[%s][%s]\\n' \"${m627i[k]}\" \"${m627i[$k]}\"\n");
    ASSERT_STDOUT_EQ(r, "[v][x]\n");
    executor_free(exec);
}

TEST(rt_sub627_control_aggregate_read_ok) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: [@]/[*] READS are untouched.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627j=(1 2 3)\n"
              "printf 'star=[%s] n=%s\\n' \"${s627j[*]}\" \"${#s627j[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "star=[1 2 3] n=3\n");
    executor_free(exec);
}

TEST(rt_sub627_array_literal_at_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// a=([@]=x) is the array-literal spelling of a[@]=x: rejected in every
    /// context, so the array is not created.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627k=([@]=x)\n"
              "printf 'rc=%s n=%s\\n' \"$?\" \"${#s627k[@]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 n=0\n");
    executor_free(exec);
}

TEST(rt_sub627_array_literal_star_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627l=([*]=x)\n"
              "printf 'rc=%s n=%s\\n' \"$?\" \"${#s627l[@]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 n=0\n");
    executor_free(exec);
}

TEST(rt_sub627_array_literal_mixed_rejected_atomic) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A bad aggregate element fails the whole literal atomically: the leading
    /// `z` is not partially stored (the fresh array is discarded).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns627m=(z [@]=x)\n"
              "printf 'rc=%s n=%s\\n' \"$?\" \"${#s627m[@]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 n=0\n");
    executor_free(exec);
}

TEST(rt_sub627_assoc_append_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The associative `+=` element form is rejected too; map unchanged.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ndeclare -A s627n\ns627n[k]=v\ns627n[@]+=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627n[k]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 [v]\n");
    executor_free(exec);
}

TEST(rt_sub627_declare_assoc_literal_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// declare -A m=([@]=v): the compound-literal spelling through
    /// builtin_bind_array_literal is rejected too -- the associative form is
    /// where bash silently creates the @-key overload lush declines.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ndeclare -A s627o=([@]=v)\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627o[@]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 []\n");
    executor_free(exec);
}

TEST(rt_sub627_declare_indexed_literal_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ndeclare -a s627p=([*]=x [1]=y)\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627p[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 []\n");
    executor_free(exec);
}

TEST(rt_sub627_local_assoc_literal_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// local -A m=([@]=v) routes through the same helper.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nf(){ local -A s627q=([@]=v); "
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s627q[@]}\"; }\nf\n");
    ASSERT_STDERR_CONTAINS(r, "invalid array subscript");
    ASSERT_STDOUT_EQ(r, "rc=1 []\n");
    executor_free(exec);
}

TEST(rt_sub627_declare_control_literal_ok) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: normal declare compound literals still bind (indexed + assoc).
    run_result_t r = run_shell_with_executor(
        exec,
        "mode bash\ndeclare -a s627s=([2]=x)\ndeclare -A s627t=([kk]=vv)\n"
        "printf '[%s][%s]\\n' \"${s627s[2]}\" \"${s627t[kk]}\"\n");
    ASSERT_STDOUT_EQ(r, "[x][vv]\n");
    executor_free(exec);
}

/* ==========================================================================
 * #618: an array subscript is a native 64-bit sparse key. The index store
 * (int64_t *indices) and the set/get/unset/append primitives are int64, and
 * every user-subscript call site passes the full width -- so a subscript like
 * 2^32 or 2^40 is stored and read back at its true index, never truncated by an
 * (int) cast to alias a wrong element. A from-end negative that resolves below
 * 0 is out of range and reported (E1116) on both the plain and arithmetic
 * surfaces. Distinct names (process-global symtable). 4294967296 = 2^32,
 * 4294967301 = 2^32+5, 2147483648 = 2^31, 1099511627776 = 2^40.
 * ========================================================================== */

TEST(rt_sub618_no_alias_plain) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The reported bug: a[2^32] used to alias a[0]. Now a[0] is untouched and
    /// the 2^32 index round-trips.
    run_result_t r = run_shell_with_executor(
        exec,
        "mode bash\ns618a=(1 2 3)\ns618a[0]=zero\ns618a[4294967296]=big\n"
        "printf '[%s][%s]\\n' \"${s618a[0]}\" \"${s618a[4294967296]}\"\n");
    ASSERT_STDOUT_EQ(r, "[zero][big]\n");
    executor_free(exec);
}

TEST(rt_sub618_no_alias_arith) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The #603 arithmetic subscript path routes through the same store.
    run_result_t r = run_shell_with_executor(
        exec,
        "mode bash\ns618b=(1 2 3)\n(( s618b[4294967296] = 777 ))\n"
        "printf '[%s][%s]\\n' \"${s618b[0]}\" \"${s618b[4294967296]}\"\n");
    ASSERT_STDOUT_EQ(r, "[1][777]\n");
    executor_free(exec);
}

TEST(rt_sub618_2p40_roundtrip) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618c[1099511627776]=huge\n"
              "printf '[%s] n=%s\\n' \"${s618c[1099511627776]}\" "
              "\"${#s618c[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "[huge] n=1\n");
    executor_free(exec);
}

TEST(rt_sub618_2p31_native_not_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// 2^31 truncated to INT_MIN under the old (int) cast and was wrongly
    /// rejected as negative; now it is a native index.
    run_result_t r = run_shell_with_executor(
        exec,
        "mode bash\ns618d[2147483648]=x\n"
        "printf '[%s] n=%s\\n' \"${s618d[2147483648]}\" \"${#s618d[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "[x] n=1\n");
    executor_free(exec);
}

TEST(rt_sub618_distinct_big_no_alias) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Two large indices in the former aliasing band stay distinct, and the
    /// low-32-bit alias target (5) is untouched.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618e[4294967296]=x\ns618e[4294967301]=y\n"
              "printf '[%s][%s][%s]\\n' \"${s618e[4294967296]}\" "
              "\"${s618e[4294967301]}\" \"${s618e[5]-unset}\"\n");
    ASSERT_STDOUT_EQ(r, "[x][y][unset]\n");
    executor_free(exec);
}

TEST(rt_sub618_keys_and_length_full_width) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// ${!a[@]} lists the full-width index and ${#a[idx]} measures the element.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618f[4294967296]=hello\n"
              "printf 'key=%s len=%s\\n' \"${!s618f[@]}\" "
              "\"${#s618f[4294967296]}\"\n");
    ASSERT_STDOUT_EQ(r, "key=4294967296 len=5\n");
    executor_free(exec);
}

TEST(rt_sub618_neg_oob_plain_reports) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A from-end negative resolving below 0 is out of range: reported on the
    /// plain path (was a silent no-op), array unchanged, non-zero status.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618h=(p q)\ns618h[-99]=x\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s618h[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "out of range");
    ASSERT_STDOUT_EQ(r, "rc=1 [p q]\n");
    executor_free(exec);
}

TEST(rt_sub618_neg_oob_arith_reports) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Parity: the arithmetic path also reports out of range.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618i=(p q)\n(( s618i[-99] = x ))\n"
              "printf 'rc=%s [%s]\\n' \"$?\" \"${s618i[*]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "out of range");
    ASSERT_STDOUT_EQ(r, "rc=1 [p q]\n");
    executor_free(exec);
}

TEST(rt_sub618_from_end_control) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: valid from-end negatives still resolve (unchanged by the
    /// widening).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618g=(p q r)\ns618g[-1]=Z\ns618g[-3]=A\n"
              "printf '[%s]\\n' \"${s618g[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "[A q Z]\n");
    executor_free(exec);
}

TEST(rt_sub618_declare_and_literal_big) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The declare compound literal (strtoll, not atoi) and the bare array
    /// literal both store a big explicit index at full width without aliasing.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ndeclare -a s618j=([4294967296]=lit)\n"
              "s618k=(z0 [4294967296]=big)\n"
              "printf '[%s][%s][%s]\\n' \"${s618j[4294967296]}\" "
              "\"${s618k[0]}\" \"${s618k[4294967296]}\"\n");
    ASSERT_STDOUT_EQ(r, "[lit][z0][big]\n");
    executor_free(exec);
}

TEST(rt_sub618_unset_big_index) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// unset of a big element (bin_unset now uses strtoll, not (int)strtol).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618l[4294967296]=x\nunset \"s618l[4294967296]\"\n"
              "printf 'n=%s [%s]\\n' \"${#s618l[@]}\" "
              "\"${s618l[4294967296]-gone}\"\n");
    ASSERT_STDOUT_EQ(r, "n=0 [gone]\n");
    executor_free(exec);
}

TEST(rt_sub618_int64max_append_no_corruption) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Appending past INT64_MAX would overflow max_index+1; it is refused, not
    /// wrapped to index 0 (which used to silently corrupt element 0).
    /// 9223372036854775807 = INT64_MAX.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618m[9223372036854775807]=max\ns618m+=(ovf)\n"
              "printf 'n=%s a0=[%s] max=[%s]\\n' \"${#s618m[@]}\" "
              "\"${s618m[0]-unset}\" \"${s618m[9223372036854775807]}\"\n");
    ASSERT_STDOUT_EQ(r, "n=1 a0=[unset] max=[max]\n");
    executor_free(exec);
}

TEST(rt_sub618_empty_from_end_rejects) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// From-end on an empty array (the count==0 base arm) is out of range: no
    /// element to count back from.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns618n=()\ns618n[-1]=x\n"
              "printf 'rc=%s n=%s\\n' \"$?\" \"${#s618n[@]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "out of range");
    ASSERT_STDOUT_EQ(r, "rc=1 n=0\n");
    executor_free(exec);
}

/* ==========================================================================
 * #642: an array subscript is an arithmetic-expression context, and lush's
 * arithmetic engine resolves $-parameters in its Layer-0 pre-pass -- but that
 * pre-pass needs the executor. The ${a[SUB]} / ${#a[SUB]} reads and the
 * a[SUB]=v writes called the executor-less arithm_expand, so a $-bearing
 * subscript reached the arith lexer raw and failed, while $((...)) / (( ))
 * (which carry the executor) resolved it -- lush inconsistent with itself.
 * Threading the executor through the subscript arithmetic context makes every
 * spelling of the same index resolve identically. Distinct names.
 * ========================================================================== */

TEST(rt_sub642_read_dollar_forms_parity) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The $-bearing subscript spellings now read the same element as the bare
    /// and $((...)) spellings that already worked (the internal invariant).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns642a=(x y z)\ni=1\n"
              "printf '[%s][%s][%s][%s][%s]\\n' \"${s642a[$i]}\" "
              "\"${s642a[${i}]}\" \"${s642a[$i+0]}\" \"${s642a[i]}\" "
              "\"${s642a[$((i))]}\"\n");
    ASSERT_STDOUT_EQ(r, "[y][y][y][y][y]\n");
    executor_free(exec);
}

TEST(rt_sub642_length_dollar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r =
        run_shell_with_executor(exec, "mode bash\ns642b=(xx yyy z)\ni=1\n"
                                      "printf 'len=%s\\n' \"${#s642b[$i]}\"\n");
    ASSERT_STDOUT_EQ(r, "len=3\n");
    executor_free(exec);
}

TEST(rt_sub642_write_dollar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns642c=(x y z)\ni=1\ns642c[$i]=W\n"
              "printf '[%s]\\n' \"${s642c[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "[x W z]\n");
    executor_free(exec);
}

TEST(rt_sub642_write_braces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns642d=(x y z)\ni=1\ns642d[${i}]=W\n"
              "printf '[%s]\\n' \"${s642d[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "[x W z]\n");
    executor_free(exec);
}

TEST(rt_sub642_write_computed_and_cmdsub) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A $-arith and a command-substitution subscript both resolve through the
    /// same pre-pass.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns642e=(x y z)\ni=1\ns642e[$((i + 1))]=P\n"
              "s642e[$(printf 0)]=Q\nprintf '[%s]\\n' \"${s642e[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "[Q y P]\n");
    executor_free(exec);
}

TEST(rt_sub642_append_dollar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns642f=(x y z)\ni=1\ns642f[$i]+=X\n"
              "printf '[%s]\\n' \"${s642f[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "[x yX z]\n");
    executor_free(exec);
}

TEST(rt_sub642_array_literal_dollar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The array-literal [SUB]=value element resolves a $-subscript too.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ni=2\ns642g=([$i]=W)\n"
              "printf '2=[%s] n=%s\\n' \"${s642g[2]}\" \"${#s642g[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "2=[W] n=1\n");
    executor_free(exec);
}

TEST(rt_sub642_control_no_dollar_unchanged) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: a subscript with no $ is unaffected (the pre-pass has nothing
    /// to expand), and the already-working (( )) / $(( )) surfaces still work.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns642h=(x y z)\ni=1\n(( s642h[$i] = 99 ))\n"
              "printf '[%s] bare=[%s]\\n' \"${s642h[*]}\" \"${s642h[1]}\"\n");
    ASSERT_STDOUT_EQ(r, "[x 99 z] bare=[99]\n");
    executor_free(exec);
}

TEST(rt_sub642_element_modify_dollar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The element-modify read paths (pattern substitution, default, trim) also
    /// resolve a $-subscript.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ns642i=(x yy zZ)\ni=1\n"
              "printf '[%s][%s][%s]\\n' \"${s642i[$i]/y/Y}\" "
              "\"${s642i[$i]:-D}\" \"${s642i[2]#z}\"\n");
    ASSERT_STDOUT_EQ(r, "[Yy][yy][Z]\n");
    executor_free(exec);
}

TEST(rt_sub642_large_index_dollar_write) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A $-subscript that expands to a 64-bit index writes and reads at the
    /// full index (#642 resolution composing with the #618 int64 store).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\ni=4294967296\ns642k[$i]=big\n"
              "printf '[%s] n=%s\\n' \"${s642k[$i]}\" \"${#s642k[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "[big] n=1\n");
    executor_free(exec);
}

/* ==========================================================================
 * #621: scalar -> array kind transition. A list operation (s[i]=v, s+=(...),
 * (( s[i]=v ))) on a variable currently holding a SCALAR is a kind change --
 * the mirror of the S3.9 list->scalar E1134, gated by
 * FEATURE_STRICT_VALUE_ TYPING. Strict (lush mode) REFUSES the implicit
 * re-kind; a relaxed mode (bash/zsh) PRESERVE-PROMOTEs, keeping the former
 * scalar as the base element. An unbound name is a fresh array (no kind
 * change); whole-assign replaces. POSIX has arrays disabled at the parser, so
 * it never reaches these paths. Distinct names (process-global symbol table);
 * index 1 keeps the array tests decoupled from lush's 0-index default where
 * possible.
 * ========================================================================== */

// --- lush mode (strict): refuse the implicit re-kind ---

TEST(rt_promote_lush_element_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nsp_a=hello\nsp_a[1]=7\necho POST\n");
    ASSERT_TRUE(strstr(r.out, "POST") == NULL,
                "execution aborted after the kind mismatch");
    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_TRUE(r.exit_status != 0, "non-zero exit on kind mismatch");
    char *v = symtable_get_var(exec->symtable, "sp_a");
    ASSERT_STR_EQ(v, "hello", "scalar unchanged after refused re-kind");
    free(v);
    executor_free(exec);
}

TEST(rt_promote_lush_append_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nsp_b=hello\nsp_b+=(x)\necho POST\n");
    ASSERT_TRUE(strstr(r.out, "POST") == NULL, "aborted after kind mismatch");
    ASSERT_STDERR_CONTAINS(r, "type mismatch");
    ASSERT_TRUE(r.exit_status != 0, "non-zero exit on kind mismatch");
    char *v = symtable_get_var(exec->symtable, "sp_b");
    ASSERT_STR_EQ(v, "hello", "scalar unchanged after refused append re-kind");
    free(v);
    executor_free(exec);
}

TEST(rt_promote_lush_arith_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The arithmetic writer reports the kind mismatch through the arithmetic
    /// error channel (which fails the command but does not abort, like other
    /// arith errors), leaving the scalar unchanged.
    run_result_t r =
        run_shell_with_executor(exec, "mode lush\nsp_c=hello\n(( sp_c[2]=9 "
                                      "))\nprintf 's=[%s]\\n' \"$sp_c\"\n");
    /// The arithmetic error channel renders "arithmetic: <message>" rather than
    /// the executor's "type mismatch: <message>"; both share this phrase.
    ASSERT_STDERR_CONTAINS(r, "cannot apply an array subscript");
    ASSERT_STDOUT_CONTAINS(r, "s=[hello]");
    executor_free(exec);
}

TEST(rt_promote_lush_unbound_creates_fresh) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: an UNBOUND name is not a kind change -- a fresh array is
    /// created in all modes, no error.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nunset sp_d\nsp_d[1]=7\necho \"n=${#sp_d[@]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "n=1\n");
    executor_free(exec);
}

TEST(rt_promote_lush_declared_array_ok) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: an explicitly-declared array accepts element writes (no scalar
    /// to re-kind).
    run_result_t r = run_shell_with_executor(
        exec,
        "mode lush\ndeclare -a sp_e\nsp_e[1]=7\necho \"n=${#sp_e[@]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "n=1\n");
    executor_free(exec);
}

TEST(rt_promote_lush_whole_assign_replaces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: whole-array assignment is an explicit re-declaration, not an
    /// implicit index/append re-kind -- it replaces, no error, in lush mode.
    run_result_t r =
        run_shell_with_executor(exec, "mode lush\nsp_f=hello\nsp_f=(a b)\necho "
                                      "\"n=${#sp_f[@]} i0=${sp_f[0]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "n=2 i0=a\n");
    executor_free(exec);
}

// --- bash mode (relaxed): preserve-promote ---

TEST(rt_promote_bash_element) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_g=hello\nsp_g[1]=7\n"
              "printf 'i0=[%s] i1=[%s] n=%s\\n' \"${sp_g[0]}\" \"${sp_g[1]}\" "
              "\"${#sp_g[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[hello] i1=[7] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_element_index0_overwrites) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// An explicit base-index write overwrites the seed (no stacking).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_h=hello\nsp_h[0]=7\n"
              "printf 'i0=[%s] n=%s\\n' \"${sp_h[0]}\" \"${#sp_h[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[7] n=1\n");
    executor_free(exec);
}

TEST(rt_promote_bash_sparse) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_i=hello\nsp_i[5]=7\n"
              "printf 'i0=[%s] i5=[%s] n=%s\\n' \"${sp_i[0]}\" \"${sp_i[5]}\" "
              "\"${#sp_i[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[hello] i5=[7] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_append) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_j=hello\nsp_j+=(x)\n"
              "printf 'i0=[%s] i1=[%s] n=%s\\n' \"${sp_j[0]}\" \"${sp_j[1]}\" "
              "\"${#sp_j[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[hello] i1=[x] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_element_plus_equal_reads_seed) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Strongest check: s[0]+=x reads the seed BACK through the array API and
    /// appends to it, proving the promoted value is retrievable.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_k=hello\nsp_k[0]+=x\n"
              "printf 'i0=[%s] n=%s\\n' \"${sp_k[0]}\" \"${#sp_k[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[hellox] n=1\n");
    executor_free(exec);
}

TEST(rt_promote_bash_empty_scalar_seeds) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A bound-but-empty scalar seeds "" at the base index (count parity).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_l=\nsp_l[1]=7\n"
              "printf 'i0=[%s] i1=[%s] n=%s\\n' \"${sp_l[0]}\" \"${sp_l[1]}\" "
              "\"${#sp_l[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[] i1=[7] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_arith) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_m=hello\n(( sp_m[2]=9 ))\n"
              "printf 'i0=[%s] i2=[%s] n=%s\\n' \"${sp_m[0]}\" \"${sp_m[2]}\" "
              "\"${#sp_m[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[hello] i2=[9] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_negative_index) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The seed sets max_index=0, so s[-1] resolves to the base slot and
    /// overwrites the seed.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_n=hi\nsp_n[-1]=7\n"
              "printf 'i0=[%s] n=%s\\n' \"${sp_n[0]}\" \"${#sp_n[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[7] n=1\n");
    executor_free(exec);
}

TEST(rt_promote_bash_whole_replace_control) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: whole-assign replaces (seed must NOT leak to the shared store).
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_o=hello\nsp_o=(a b)\n"
              "printf 'i0=[%s] n=%s\\n' \"${sp_o[0]}\" \"${#sp_o[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[a] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_whole_empty_clears_control) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: s=() clears to n=0; the seed must not live in the shared store.
    run_result_t r = run_shell_with_executor(
        exec,
        "mode bash\nsp_p=hello\nsp_p=()\nprintf 'n=%s\\n' \"${#sp_p[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "n=0\n");
    executor_free(exec);
}

TEST(rt_promote_bash_read_a_replaces_control) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: read -a has replace semantics; the seed must no-op (it clears
    /// the name first), so a pre-existing scalar is not stacked in.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_q=hello\nread -a sp_q <<< \"a b\"\n"
              "printf 'i0=[%s] n=%s\\n' \"${sp_q[0]}\" \"${#sp_q[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[a] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_rematch_control) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Control: BASH_REMATCH (a site-3 co-user) writes index 0 first, so a
    /// pre-bound scalar is clobbered, not seeded -- no regression.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nBASH_REMATCH=hello\n[[ abcd =~ (b)(c) ]]\n"
              "printf 'n=%s i0=[%s]\\n' \"${#BASH_REMATCH[@]}\" "
              "\"${BASH_REMATCH[0]}\"\n");
    ASSERT_STDOUT_EQ(r, "n=3 i0=[bc]\n");
    executor_free(exec);
}

TEST(rt_promote_bash_function_scope) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A global scalar promoted from inside a function (#614 resolver) reads
    /// the seed from and lands in the same owning scope.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nsp_r=hello\nf(){ sp_r[1]=7; }\nf\n"
              "printf 'i0=[%s] i1=[%s] n=%s\\n' \"${sp_r[0]}\" \"${sp_r[1]}\" "
              "\"${#sp_r[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i0=[hello] i1=[7] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_bash_readonly_scalar_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Readonly precedence (#614/#620): a readonly scalar is refused with the
    /// readonly diagnostic BEFORE the kind check; the scalar is unchanged.
    run_result_t r = run_shell_with_executor(
        exec, "mode bash\nreadonly sp_s=hi\nsp_s[1]=7\nprintf 'after=[%s]\\n' "
              "\"$sp_s\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_CONTAINS(r, "after=[hi]");
    executor_free(exec);
}

// --- zsh mode (relaxed, lush kinds: preserve at base, NOT char-subst) ---

TEST(rt_promote_zsh_index2_preserves_base) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// zsh mode is 1-indexed: user index 2 maps to internal 1, so the seed at
    /// internal 0 (user index 1) survives -- preserve-at-base, not char-subst.
    run_result_t r = run_shell_with_executor(
        exec, "mode zsh\nsp_t=hello\nsp_t[2]=7\n"
              "printf 'i1=[%s] i2=[%s] n=%s\\n' \"${sp_t[1]}\" \"${sp_t[2]}\" "
              "\"${#sp_t[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i1=[hello] i2=[7] n=2\n");
    executor_free(exec);
}

TEST(rt_promote_zsh_index1_overwrites_base) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// zsh mode: user index 1 IS the base slot, so the write overwrites the
    /// seed (n=1).
    run_result_t r = run_shell_with_executor(
        exec, "mode zsh\nsp_u=hello\nsp_u[1]=7\n"
              "printf 'i1=[%s] n=%s\\n' \"${sp_u[1]}\" \"${#sp_u[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "i1=[7] n=1\n");
    executor_free(exec);
}

// --- policy is confined to the user surfaces (internal writers bypass it) ---

TEST(rt_promote_strict_rematch_no_regress) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Shell-internal special-array writers (BASH_REMATCH, coproc, mapfile) go
    /// through the low-level element primitive, which does NOT apply the user
    /// kind policy -- so a regex match onto a name that currently holds a
    /// scalar still replaces it, even in lush (strict) mode. The policy governs
    /// only the user s[i]= / s+=(...) / (( s[i]=v )) surfaces.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nBASH_REMATCH=foo\n[[ abcd =~ (a)(bc) ]]\n"
              "printf '0=%s 1=%s n=%s\\n' \"${BASH_REMATCH[0]}\" "
              "\"${BASH_REMATCH[1]}\" \"${#BASH_REMATCH[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "0=abc 1=a n=3\n");
    executor_free(exec);
}

TEST(rt_promote_readonly_arith_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Readonly precedence on the arithmetic writer: a readonly scalar re-kind
    /// is refused with the readonly diagnostic (E1117), NOT the kind error, and
    /// the scalar is unchanged. Non-fatal (arith error channel), so the
    /// following command runs.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nreadonly sp_rr=5\n(( sp_rr[0]=9 ))\n"
              "printf 'r=[%s]\\n' \"$sp_rr\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_CONTAINS(r, "r=[5]");
    executor_free(exec);
}

/* ==========================================================================
 * #616: negative array subscripts in arithmetic. The arith engine evaluates
 * a[-1] to -1; the symtable element wrappers previously rejected a negative
 * in 0-indexed mode before the from-end-aware index primitives. With the
 * rejection removed, the arithmetic surface resolves negatives from the end,
 * consistent with lush's own ${a[-1]} and plain a[-1]=v surfaces (and the
 * bash/zsh consensus). zsh 1-indexed mode still rejects native negatives.
 * ========================================================================== */

TEST(rt_arith_neg_read_in_range) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "an1=(10 20 30)\nprintf '%s %s\\n' \"$(( an1[-1] ))\" "
              "\"$(( an1[-2] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "30 20\n");
    executor_free(exec);
}

TEST(rt_arith_neg_write_in_range) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "an2=(10 20 30)\n(( an2[-1]=99 ))\necho \"${an2[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "10 20 99\n");
    executor_free(exec);
}

TEST(rt_arith_neg_compound_and_incr) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// compound += and post-increment both fold through lvalue_read+write on
    /// the resolved-once negative subscript.
    run_result_t r = run_shell_with_executor(
        exec, "an3=(10 20 30)\n(( an3[-1]+=5 ))\necho \"${an3[*]}\"\n"
              "an4=(10 20 30)\nr=$(( an4[-1]++ ))\necho \"$r ${an4[*]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "10 20 35\n30 10 20 31\n");
    executor_free(exec);
}

TEST(rt_arith_neg_oob_read_zero) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// An out-of-range negative reads 0, matching lush's unset-element model
    /// and the plain ${a[-9]} surface (both empty/0).
    run_result_t r = run_shell_with_executor(
        exec, "an5=(10 20 30)\nprintf 'a=%s b=[%s]\\n' \"$(( an5[-9] ))\" "
              "\"${an5[-9]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "a=0 b=[]\n");
    executor_free(exec);
}

TEST(rt_arith_neg_surface_consistency) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A plain negative write is read back by the arithmetic surface: the two
    /// surfaces agree.
    run_result_t r = run_shell_with_executor(
        exec, "an6=(10 20 30)\nan6[-1]=77\necho \"$(( an6[-1] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "77\n");
    executor_free(exec);
}

TEST(rt_arith_neg_zsh_mode_still_rejected) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Scope boundary / no regression: zsh 1-indexed mode still rejects a
    /// native negative subscript (the retained zsh guard), matching the plain
    /// executor path.
    run_result_t r = run_shell_with_executor(
        exec, "mode zsh\nan7=(10 20 30)\n(( an7[-1]=1 ))\necho POST\n");
    ASSERT_STDERR_CONTAINS(r, "out of range");
    ASSERT_STDOUT_CONTAINS(r, "POST");
    executor_free(exec);
}

TEST(rt_arith_neg_blast_radius_rematch) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Blast-radius control: a non-negative wrapper caller (BASH_REMATCH) is
    /// unaffected by removing the negative-rejection branch.
    run_result_t r = run_shell_with_executor(
        exec, "[[ abcd =~ (b)(c) ]]\n"
              "echo \"${BASH_REMATCH[1]}${BASH_REMATCH[2]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "bc\n");
    executor_free(exec);
}

/* ==========================================================================
 * #615: associative-array element keys in arithmetic. The lexer captures the
 * whole [...] subscript raw; the evaluator uses it as a LITERAL key for an
 * associative array (never arithmetic-evaluated), so any key works -- matching
 * lush's own ${m[...]} surface and the bash/zsh consensus. Associative keys
 * are index-base-agnostic, so behavior is mode-independent (a zsh-mode case
 * confirms it). Indexed arrays are unaffected (the raw subscript is re-lexed
 * and arithmetic-evaluated), including single-evaluation of a[i++]++.
 * ========================================================================== */

TEST(rt_arith_assoc_literal_key) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "declare -A m1\nm1[foo]=5\necho \"$(( m1[foo] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_compound_assign) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec,
        "declare -A m2\nm2[foo]=5\n(( m2[foo]+=1 ))\necho \"${m2[foo]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "6\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_key_not_arithmetic) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The key 'a+b' is literal, not arithmetic: it is stable after a/b change.
    run_result_t r = run_shell_with_executor(
        exec, "declare -A m3\na=1\nb=2\nm3[a+b]=7\na=100\nb=200\n"
              "echo \"$(( m3[a+b] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "7\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_nonlexable_key) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Keys with characters the arithmetic lexer rejects (., @, /) work,
    /// because the subscript is captured raw and used verbatim -- full parity
    /// with the plain ${m[foo.bar]} surface.
    run_result_t r = run_shell_with_executor(
        exec, "declare -A m4\nm4[foo.bar]=5\nm4[a@b]=3\nm4[a/b]=9\n"
              "echo \"$(( m4[foo.bar] )) $(( m4[a@b] )) $(( m4[a/b] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5 3 9\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_numeric_key_literal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A numeric-looking associative key is the literal string "10", NOT index
    /// 10; m[0] and m[10] are distinct keys.
    run_result_t r =
        run_shell_with_executor(exec, "declare -A m5\nm5[10]=42\nm5[0]=99\n"
                                      "echo \"$(( m5[10] )) $(( m5[0] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "42 99\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_negativelike_key_literal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A negative-looking associative key is the literal string "-1", NOT a
    /// from-end index (from-end resolution is indexed-only).
    run_result_t r = run_shell_with_executor(
        exec, "declare -A m6\nm6[-1]=7\necho \"$(( m6[-1] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "7\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_missing_key_zero) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A missing key reads 0 and is NOT created (no autovivification on read).
    run_result_t r = run_shell_with_executor(
        exec, "declare -A m7\nm7[foo]=5\n"
              "printf 'r=%s n=%s\\n' \"$(( m7[bar] ))\" \"${#m7[@]}\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "r=0 n=1\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_expanded_key) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Layer-0 expands $k before the lexer, so m[$k] keys on the value of k.
    run_result_t r = run_shell_with_executor(
        exec, "declare -A m8\nk=foo\nm8[foo]=5\necho \"$(( m8[$k] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_readonly_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A readonly associative array refuses an arithmetic element write with
    /// the readonly diagnostic; the element is unchanged.
    run_result_t r =
        run_shell_with_executor(exec, "declare -Ar m9=([k]=v)\n(( m9[k]=9 ))\n"
                                      "printf 'after=[%s]\\n' \"${m9[k]}\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_CONTAINS(r, "after=[v]");
    executor_free(exec);
}

TEST(rt_arith_assoc_zsh_mode) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Associative keys are index-base-agnostic: the literal-key path is not
    /// affected by the 1-indexed guard.
    run_result_t r = run_shell_with_executor(
        exec,
        "mode zsh\ndeclare -A m10\nm10[foo]=5\necho \"$(( m10[foo] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5\n");
    executor_free(exec);
}

TEST(rt_arith_indexed_unaffected_by_lex_capture) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// Regression: an indexed array still arithmetic-evaluates the (now
    /// lex-captured) subscript, including an expression index, a nested
    /// subscript, and single-evaluation of a[i++]++.
    run_result_t r = run_shell_with_executor(
        exec, "mode lush\nai=(10 20 30)\nix=1\nprintf '%s %s\\n' "
              "\"$(( ai[ix+1] ))\" \"$(( ai[ai[0]-8] ))\"\n"
              "ac=(100)\nj=0\n(( ac[j++]++ ))\n"
              "printf 'a0=%s j=%s\\n' \"${ac[0]}\" \"$j\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "30 30\na0=101 j=1\n");
    executor_free(exec);
}

TEST(rt_arith_assoc_empty_key) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// The empty string is a valid associative key in lush's value model (a
    /// bare m[$k] with k="" stores and reads a value under it), so the
    /// arithmetic surface reads that key consistently -- it is not an error.
    run_result_t r = run_shell_with_executor(
        exec, "declare -A me\nk=\nme[$k]=9\necho \"$(( me[$k] ))\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "9\n");
    executor_free(exec);
}

TEST(rt_arith_subscript_depth_capped) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    /// A pathologically deep subscript nesting (a[a[a[...]]]) raises a clean
    /// stack-overflow error and continues (the arithmetic error channel is
    /// non-fatal), rather than crashing -- mirroring lush's recursive-variable
    /// depth cap. This is a regression guard against a SIGSEGV.
    char sub[2048];
    size_t off = 0;
    const int depth = 300; /// well past ARITH_MAX_SUBSCRIPT_DEPTH (128)
    for (int i = 0; i < depth; i++) {
        sub[off++] = 'a';
        sub[off++] = '[';
    }
    sub[off++] = '0';
    for (int i = 0; i < depth; i++) {
        sub[off++] = ']';
    }
    sub[off] = '\0';
    char script[4096];
    snprintf(script, sizeof(script),
             "ad=(0 1 2)\necho \"$(( %s ))\"\necho POST\n", sub);
    run_result_t r = run_shell_with_executor(exec, script);
    ASSERT_STDOUT_CONTAINS(r, "POST"); /// survived: no crash, error was clean
    executor_free(exec);
}

TEST(rt_local_array_append_extends) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// local delegates to declare, so the append form works function-scoped;
    /// a quoted spaced element stays one slot (shared with the #546 channel).
    run_result_t r = run_shell_with_executor(
        exec, "f549() { local q=(a); local q+=(\"b c\" d); "
              "printf '(%s)' \"${q[@]}\"; echo; }\nf549\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(a)(b c)(d)\n");

    executor_free(exec);
}

TEST(rt_readonly_array_append_relaxed) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The relaxed-mode readonly path shares the binder; append extends.
    run_result_t r = run_shell_mode_gated(
        exec, "mode bash\ndeclare -a p549r=(x y)\nreadonly p549r+=(z)\n"
              "printf '(%s)' \"${p549r[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "(x)(y)(z)");

    executor_free(exec);
}

TEST(rt_declare_array_append_readonly_refused) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The array binder refuses to mutate an existing readonly array, for
    /// both the append and the replace forms (issue #549 review): bash rejects
    /// `declare a=(...)` and `a+=(...)` on a readonly `a`. The array is left
    /// intact. Unique name -- readonly poisons the shared global symtable.
    run_result_t r = run_shell_mode_gated(
        exec, "mode bash\ndeclare -ar p549ro=(x y)\ndeclare p549ro+=(z)\n"
              "printf '(%s)' \"${p549ro[@]}\"\necho\n");

    ASSERT_STDERR_CONTAINS(r, "readonly");
    ASSERT_STDOUT_CONTAINS(r, "(x)(y)");
    ASSERT_TRUE(strstr(r.out, "(z)") == NULL, "readonly array must not append");

    executor_free(exec);
}

TEST(rt_local_array_quoted_element_keeps_spaces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// local delegates to declare, so the boundary fix flows through the same
    /// sentinel channel for a function-scoped array.
    run_result_t r = run_shell_with_executor(
        exec,
        "f546() { local q=(\"p q\" r); printf '(%s)' \"${q[@]}\"; echo; }\n"
        "f546\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "(p q)(r)\n");

    executor_free(exec);
}

TEST(rt_readonly_relaxed_quoted_element_keeps_spaces) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// The relaxed-mode readonly/export paths share builtin_bind_array_literal,
    /// so the fix applies there too: a spaced quoted element stays one slot.
    run_result_t r =
        run_shell_mode_gated(exec, "mode bash\nreadonly p546r=(\"x y\" z)\n"
                                   "printf '(%s)' \"${p546r[@]}\"\necho\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "(x y)(z)");

    executor_free(exec);
}

TEST(local_quoted_paren_value_stays_scalar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec,
        "lqpv() { local sdata=\"(scoped)\"; echo \"sdata=$sdata\"; }\nlqpv\n");

    ASSERT_EXIT_STATUS(r, 0);
    /// Sentinel correctly absent on quoted scalar; the (...) is just a
    /// literal substring of a scalar value, NOT an array literal.
    ASSERT_STDOUT_CONTAINS(r, "sdata=(scoped)");

    executor_free(exec);
}

TEST(local_array_dies_with_function_scope) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// bash conformance: a local array declared inside a function
    /// does not outlive that function. Pre-storage-unification, lush
    /// leaked such arrays out via the global array side-table; the
    /// symtable refactor fixed that, but the test pins the contract.
    run_result_t r = run_shell_with_executor(
        exec,
        "ladwfs() { local farr=(a b c); }\nladwfs\necho len=${#farr[@]}\n");

    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "len=0");

    executor_free(exec);
}

TEST(local_plain_string_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec, "f() { local x=initial; x=updated; RESULT=$x; }", 1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "updated",
                  "local then plain reassignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(local_integer_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "f() { local x=0; x=42; RESULT=$x; }",
                                  1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "42",
        "local then integer literal reassignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(local_arith_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec, "f() { local x=0; x=$((1+1)); RESULT=$x; }", 1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "2",
        "local then arithmetic substitution assignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(local_command_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec, "f() { local x=initial; x=$(echo updated); RESULT=$x; }", 1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "updated",
        "local then command substitution assignment must update the local");
    free(result);
    executor_free(exec);
}

TEST(declare_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "f() { declare x=0; x=42; RESULT=$x; }",
                                  1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "42",
        "declare then reassignment must update the declared variable");
    free(result);
    executor_free(exec);
}

TEST(typeset_reassignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "f() { typeset x=0; x=42; RESULT=$x; }",
                                  1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "42",
                  "typeset then reassignment must update the typeset variable");
    free(result);
    executor_free(exec);
}

TEST(local_two_step_no_initial) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "f() { local x; x=hello; RESULT=$x; }",
                                  1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "hello",
        "local without initial value then assign must produce the new value");
    free(result);
    executor_free(exec);
}

TEST(local_does_not_leak_to_global) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "X_LEAK=outside-initial", 1);
    executor_execute_command_line(
        exec, "f() { local X_LEAK=inside-local; X_LEAK=inside-updated; }", 1);
    executor_execute_command_line(exec, "f", 1);

    char *outside = symtable_get_var(exec->symtable, "X_LEAK");
    ASSERT_STR_EQ(
        outside, "outside-initial",
        "global with same name as local must not be touched by reassignment "
        "to the local");
    free(outside);
    executor_free(exec);
}

TEST(local_for_loop_variable) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec,
        "f() { local i=startval; for i in 1 2 3; do :; done; RESULT=$i; }", 1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "3",
                  "for loop variable must update the local, not silently write "
                  "to global");
    free(result);
    executor_free(exec);
}

TEST(local_plus_equals_append) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(
        exec, "f() { local s=hello; s+=\" world\"; RESULT=$s; }", 1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "hello world",
                  "local += append must concatenate to the local, not silently "
                  "write to global");
    free(result);
    executor_free(exec);
}

TEST(local_while_loop_counter) {
    /// The original symptomatic case from issue #47 -- a while-loop counter
    /// with `local` infinite-looped before the fix. The test framework
    /// exits on first failure, so earlier simpler tests stop the run before
    /// this one ever executes in the broken state. After the fix, this
    /// confirms the loop terminates correctly.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec,
                                  "f() { local i=0; while [ $i -lt 3 ]; "
                                  "do i=$((i+1)); done; RESULT=$i; }",
                                  1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "3",
                  "while-loop counter using local must terminate correctly");
    free(result);
    executor_free(exec);
}

/* ============================================================================
 * REGRESSION TESTS -- Issue #48
 * Trailing redirections attached to a function definition must be applied
 * when the function is called. The parser side landed in #43 and these
 * inputs now produce a correct AST; the executor needs to honor it.
 *
 * Each test redirects to /tmp/lush_test_48, then reads back via
 * `RESULT=$(cat /tmp/lush_test_48)` and compares against the expected
 * file contents. Tests clean up their tmp file before and after to
 * avoid cross-contamination.
 * ============================================================================
 */

TEST(function_redir_out_applied_at_call) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_execute_command_line(exec,
                                  "f() { echo HELLO; } > /tmp/lush_test_48", 1);
    executor_execute_command_line(exec, "f", 1);
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "HELLO",
        "function trailing > redirect must write body output to the file");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_free(exec);
}

TEST(function_redir_stderr_applied_at_call) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_execute_command_line(
        exec, "f() { echo OOPS 1>&2; } 2> /tmp/lush_test_48", 1);
    executor_execute_command_line(exec, "f", 1);
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "OOPS",
        "function trailing 2> redirect must write body stderr to the file");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_free(exec);
}

TEST(function_redir_append_accumulates_across_calls) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_execute_command_line(exec,
                                  "f() { echo line; } >> /tmp/lush_test_48", 1);
    executor_execute_command_line(exec, "f", 1);
    executor_execute_command_line(exec, "f", 1);
    executor_execute_command_line(exec, "f", 1);
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "line\nline\nline",
                  "function trailing >> redirect must append on every call");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_free(exec);
}

TEST(function_redir_keyword_form_applied_at_call) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_execute_command_line(
        exec, "function f { echo from-keyword; } > /tmp/lush_test_48", 1);
    executor_execute_command_line(exec, "f", 1);
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "from-keyword",
        "function-keyword form trailing > redirect must apply at call");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_free(exec);
}

TEST(function_redir_input_applied_at_call) {
    /// Verifies the function's stdin is the redirected file. The captured
    /// value is stored in a global so the test does not need command
    /// substitution to read it back.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48_in", 1);
    executor_execute_command_line(exec,
                                  "echo hello-input > /tmp/lush_test_48_in", 1);
    executor_execute_command_line(
        exec,
        "f() { read line; CAPTURED=\"got: $line\"; } < /tmp/lush_test_48_in",
        1);
    executor_execute_command_line(exec, "f", 1);

    char *result = symtable_get_var(exec->symtable, "CAPTURED");
    ASSERT_STR_EQ(
        result, "got: hello-input",
        "function trailing < redirect must feed body stdin from the file");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48_in", 1);
    executor_free(exec);
}

TEST(function_redir_does_not_break_normal_call) {
    /// Sanity: calling a function with a trailing redirect must not leak
    /// the redirection across to subsequent unrelated commands.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_execute_command_line(
        exec, "f() { echo from-f; } > /tmp/lush_test_48", 1);
    executor_execute_command_line(exec, "f", 1);
    /// This echo must NOT also be captured by f's redirect -- its output
    /// should be discarded as normal (we don't capture stdout here).
    executor_execute_command_line(exec, "echo from-second-call", 1);
    executor_execute_command_line(
        exec, "RESULT=$(cat /tmp/lush_test_48 2>/dev/null)", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(
        result, "from-f",
        "function trailing redirect must not leak to subsequent commands");
    free(result);
    executor_execute_command_line(exec, "rm -f /tmp/lush_test_48", 1);
    executor_free(exec);
}

/* ============================================================================
 * ARITHMETIC TESTS
 * ============================================================================
 */

TEST(arithmetic_basic) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "RESULT=$((2 + 3))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "5", "2 + 3 should equal 5");
    free(result);

    executor_free(exec);
}

TEST(arithmetic_multiply) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "RESULT=$((4 * 5))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "20", "4 * 5 should equal 20");
    free(result);

    executor_free(exec);
}

TEST(arithmetic_variable) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "X=10", 1);
    executor_execute_command_line(exec, "Y=20", 1);
    executor_execute_command_line(exec, "RESULT=$((X + Y))", 1);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    ASSERT_STR_EQ(result, "30", "10 + 20 should equal 30");
    free(result);

    executor_free(exec);
}

TEST(arithmetic_increment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "N=5", 1);
    executor_execute_command_line(exec, "N=$((N + 1))", 1);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "6", "5 + 1 should equal 6");
    free(n);

    executor_free(exec);
}

TEST(arithmetic_brace_param_echo) {
    /// `${var}` parameter expansion inside arithmetic. The `$((`
    /// disambiguation must skip the `${...}` braces rather than
    /// mistaking them for an anonymous-function body (issue #118).
    run_result_t r = run_shell("a=10\n"
                               "echo $((${a}))\n");
    ASSERT_STDOUT_EQ(r, "10\n");
}

TEST(arithmetic_brace_param_add) {
    run_result_t r = run_shell("a=10\n"
                               "echo $((${a}+1))\n");
    ASSERT_STDOUT_EQ(r, "11\n");
}

TEST(arithmetic_brace_param_two_operands) {
    run_result_t r = run_shell("a=10\n"
                               "b=5\n"
                               "echo $((${a}+${b}))\n");
    ASSERT_STDOUT_EQ(r, "15\n");
}

TEST(arithmetic_brace_param_assignment) {
    /// Assignment RHS routes through executor string expansion, a
    /// different `$((` detection site than the echo argument path;
    /// both share the disambiguation helper (issue #118).
    run_result_t r = run_shell("a=10\n"
                               "x=$((${a}+1))\n"
                               "echo $x\n");
    ASSERT_STDOUT_EQ(r, "11\n");
}

TEST(arithmetic_brace_param_nested_parens) {
    run_result_t r = run_shell("a=10\n"
                               "b=5\n"
                               "echo $(( ${a} * (${b} + 2) ))\n");
    ASSERT_STDOUT_EQ(r, "70\n");
}

TEST(anon_function_cmdsub_still_routes) {
    /// Guard the other side of the disambiguation: `$(() {...})` is
    /// command substitution of an anonymous function, not arithmetic.
    /// The brace skip added for issue #118 must not swallow the
    /// anonymous-function body braces (issue #99).
    run_result_t r = run_shell("echo \"$( () { echo anonret; } )\"\n");
    ASSERT_STDOUT_EQ(r, "anonret\n");
}

/* ============================================================================
 * SUBSHELL AND GROUPING TESTS
 * ============================================================================
 */

TEST(subshell_isolation) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "OUTER=yes", 1);
    /// Variable set in subshell should not affect parent
    executor_execute_command_line(exec, "(INNER=subshell)", 1);

    char *outer = symtable_get_var(exec->symtable, "OUTER");
    ASSERT_NOT_NULL(outer, "OUTER should be set");
    ASSERT_STR_EQ(outer, "yes", "OUTER should be yes");
    free(outer);

    /// INNER should not exist in parent
    char *inner = symtable_get_var(exec->symtable, "INNER");
    ASSERT(inner == NULL, "INNER should NOT be set in parent");

    executor_free(exec);
}

TEST(brace_group) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Brace group runs in current shell
    run_result_t r = run_shell_with_executor(exec, "{ A=1; B=2; }");
    ASSERT_EXIT_STATUS(r, 0);

    char *a = symtable_get_var(exec->symtable, "A");
    char *b = symtable_get_var(exec->symtable, "B");
    ASSERT_STR_EQ(a, "1", "A should be 1");
    ASSERT_STR_EQ(b, "2", "B should be 2");
    free(a);
    free(b);

    executor_free(exec);
}

/* ============================================================================
 * TEST COMMAND ([) TESTS
 * ============================================================================
 */

TEST(test_string_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[ foo = foo ]");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "[ foo = bar ]");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(test_string_not_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[ foo != bar ]");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "[ foo != foo ]");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(test_numeric_compare) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -eq 5 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -ne 3 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -gt 3 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 5 -ge 5 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 3 -lt 5 ]"), 0);
    ASSERT_EXIT_STATUS(run_shell_with_executor(exec, "[ 3 -le 3 ]"), 0);

    executor_free(exec);
}

TEST(test_string_empty) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[ -z '' ]");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "[ -z 'notempty' ]");
    ASSERT_EXIT_STATUS(r, 1);

    r = run_shell_with_executor(exec, "[ -n 'notempty' ]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

/* ============================================================================
 * BUILTIN TESTS
 * ============================================================================
 */

TEST(builtin_export) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "export MYEXPORT=value");
    ASSERT_EXIT_STATUS(r, 0);

    char *value = symtable_get_var(exec->symtable, "MYEXPORT");
    ASSERT_NOT_NULL(value, "MYEXPORT should be set");
    ASSERT_STR_EQ(value, "value", "Value should be 'value'");
    free(value);

    executor_free(exec);
}

TEST(builtin_logout_outside_login_errors) {
    /// `logout` from a non-login shell must error out with a clear
    /// diagnostic instead of terminating the shell (matches bash and
    /// zsh; protects users from accidental logout in a script).
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "logout");
    ASSERT_TRUE(r.exit_status != 0,
                "logout outside login shell must return non-zero");
    ASSERT_STDERR_CONTAINS(r, "not login shell");

    executor_free(exec);
}

TEST(builtin_logout_known_to_type_builtin) {
    /// `type logout` should report it as a shell builtin even outside
    /// a login shell -- the builtin must be registered so users can
    /// discover it.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "type logout");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_CONTAINS(r, "builtin");

    executor_free(exec);
}

TEST(builtin_unset) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "TOUNSET=exists", 1);
    char *before = symtable_get_var(exec->symtable, "TOUNSET");
    ASSERT_NOT_NULL(before, "Variable should exist before unset");
    free(before);

    executor_execute_command_line(exec, "unset TOUNSET", 1);
    char *after = symtable_get_var(exec->symtable, "TOUNSET");
    ASSERT(after == NULL, "Variable should not exist after unset");

    executor_free(exec);
}

TEST(builtin_readonly) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "readonly MYCONST=constant");
    ASSERT_EXIT_STATUS(r, 0);

    char *value = symtable_get_var(exec->symtable, "MYCONST");
    ASSERT_NOT_NULL(value, "MYCONST should be set");
    ASSERT_STR_EQ(value, "constant", "Value should be 'constant'");
    free(value);

    executor_free(exec);
}

TEST(rt_read_strips_leading_trailing_ifs_whitespace) {
    /// POSIX read trims leading and trailing IFS white space, even for a
    /// single variable (the line is otherwise assigned verbatim). #129.
    run_result_t r = run_shell("read -r x <<'EOF'\n"
                               "  a b  \n"
                               "EOF\n"
                               "echo \"[$x]\"\n");
    ASSERT_STDOUT_EQ(r, "[a b]\n");
}

TEST(rt_read_multi_var_trims_trailing_ws) {
    /// The last variable keeps internal IFS chars but has trailing IFS
    /// white space stripped.
    run_result_t r = run_shell("read -r x y <<'EOF'\n"
                               "  a b c  \n"
                               "EOF\n"
                               "echo \"x=[$x] y=[$y]\"\n");
    ASSERT_STDOUT_EQ(r, "x=[a] y=[b c]\n");
}

TEST(rt_read_non_whitespace_ifs_not_trimmed) {
    /// Non-whitespace IFS chars (here ':') delimit fields but are not
    /// trimmed from the ends of a single-variable read.
    run_result_t r = run_shell("IFS=:\n"
                               "read -r x <<'EOF'\n"
                               ":a:b:\n"
                               "EOF\n"
                               "echo \"[$x]\"\n"
                               "unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[:a:b:]\n");
}

TEST(rt_printf_v_assigns_variable) {
    /// printf -v VAR assigns the formatted output to VAR and prints
    /// nothing to stdout; the value is visible to a later command (#141).
    run_result_t r = run_shell("printf -v out '%05d-%s' 42 hello\n"
                               "echo \"[$out]\"\n");
    ASSERT_STDOUT_EQ(r, "[00042-hello]\n");
}

TEST(rt_printf_without_v_still_prints) {
    /// Regression guard: plain printf (no -v) still writes to stdout.
    run_result_t r = run_shell("printf '%s-%d\\n' hi 7\n");
    ASSERT_STDOUT_EQ(r, "hi-7\n");
}

TEST(builtin_eval) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "eval 'EVALED=yes'");
    ASSERT_EXIT_STATUS(r, 0);

    char *value = symtable_get_var(exec->symtable, "EVALED");
    ASSERT_NOT_NULL(value, "EVALED should be set");
    ASSERT_STR_EQ(value, "yes", "Value should be 'yes'");
    free(value);

    executor_free(exec);
}

TEST(builtin_shift) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Test shift with positional parameters
    executor_execute_command_line(exec, "set -- a b c d e", 1);
    executor_execute_command_line(exec, "FIRST=$1", 1);
    executor_execute_command_line(exec, "shift", 1);
    executor_execute_command_line(exec, "AFTER=$1", 1);

    char *first = symtable_get_var(exec->symtable, "FIRST");
    char *after = symtable_get_var(exec->symtable, "AFTER");

    ASSERT_STR_EQ(first, "a", "First should be 'a'");
    ASSERT_STR_EQ(after, "b", "After shift, $1 should be 'b'");

    free(first);
    free(after);
    executor_free(exec);
}

TEST(builtin_set_positional) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "set -- one two three", 1);
    executor_execute_command_line(exec, "P1=$1; P2=$2; P3=$3", 1);

    char *p1 = symtable_get_var(exec->symtable, "P1");
    char *p2 = symtable_get_var(exec->symtable, "P2");
    char *p3 = symtable_get_var(exec->symtable, "P3");

    ASSERT_STR_EQ(p1, "one", "$1 should be 'one'");
    ASSERT_STR_EQ(p2, "two", "$2 should be 'two'");
    ASSERT_STR_EQ(p3, "three", "$3 should be 'three'");

    free(p1);
    free(p2);
    free(p3);
    executor_free(exec);
}

TEST(rt_set_dashdash_function_scope) {
    /// `set --` inside a function rebinds the function's local positionals, so
    /// $#/$*/$1.. reflect the new list and `shift` works; it does NOT leak to
    /// the caller's positionals after the function returns (issue #562).
    /// Wrapped in a subshell so the baseline `set --` does not pollute the
    /// shared global positionals of other tests.
    run_result_t r = run_shell(
        "( f() { set -- a b c; echo \"$# $*\"; shift; echo \"$# $*\"; }\n"
        "  set -- G1 G2; f x y; echo \"after $# $1 $2\" )\n");
    ASSERT_STDOUT_EQ(r, "3 a b c\n2 b c\nafter 2 G1 G2\n");
}

TEST(rt_set_dashdash_loop_scope_consistent) {
    /// A loop body is a transparent scope for positionals: `set --` inside it
    /// writes the enclosing function's (or the shell's) positionals, so $#,
    /// $*, $1.. and "$@" stay in agreement after the loop -- they must not
    /// desync (the loop-scope regression of the first #562 fix). And an
    /// in-function loop `set --` persists after the loop, as in bash.
    run_result_t g =
        run_shell("( set -- a b c; for i in 1; do set -- x y; done\n"
                  "  printf '%s|' \"$# $*\"; printf '<%s>' \"$@\"; echo )\n");
    ASSERT_STDOUT_EQ(g, "2 x y|<x><y>\n");

    run_result_t f = run_shell(
        "f() { for i in 1; do set -- a b; done; echo \"$# $*\"; }\nf x y z\n");
    ASSERT_STDOUT_EQ(f, "2 a b\n");
}

/* ============================================================================
 * COMMAND LIST TESTS
 * ============================================================================
 */

TEST(command_list_semicolon) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "A=1; B=2; C=3");
    ASSERT_EXIT_STATUS(r, 0);

    char *a = symtable_get_var(exec->symtable, "A");
    char *b = symtable_get_var(exec->symtable, "B");
    char *c = symtable_get_var(exec->symtable, "C");

    ASSERT_STR_EQ(a, "1", "A should be 1");
    ASSERT_STR_EQ(b, "2", "B should be 2");
    ASSERT_STR_EQ(c, "3", "C should be 3");

    free(a);
    free(b);
    free(c);
    executor_free(exec);
}

/* ============================================================================
 * BREAK/CONTINUE TESTS
 * ============================================================================
 */

TEST(loop_break) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "N=0; for i in 1 2 3 4 5; do N=$((N+1)); if [ $N -eq 3 ]; then "
              "break; fi; done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "3", "Loop should break at 3");
    free(n);

    executor_free(exec);
}

TEST(loop_continue) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Count only odd numbers: skip even iterations
    run_result_t r = run_shell_with_executor(
        exec, "SUM=0; for i in 1 2 3 4 5; do "
              "if [ $((i % 2)) -eq 0 ]; then continue; fi; "
              "SUM=$((SUM + i)); done");
    ASSERT_EXIT_STATUS(r, 0);

    char *sum = symtable_get_var(exec->symtable, "SUM");
    ASSERT_NOT_NULL(sum, "SUM should be set");
    ASSERT_STR_EQ(sum, "9", "Sum of 1+3+5 should be 9");
    free(sum);

    executor_free(exec);
}

TEST(loop_break_outside_loop_errors) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(exec, "break");
    ASSERT_EXIT_STATUS(r, 1);
    executor_free(exec);
}

TEST(loop_break_2_unwinds_two_frames) {
    /// `break 2` in a doubly-nested loop fires exactly once at the
    /// inner break point and the outer loop must NOT keep iterating
    /// (regression test for #169 -- the level argument was parsed but
    /// discarded, so the outer loop ran every iteration).
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "N=0; for i in 1 2; do for j in a b; do N=$((N+1)); "
              "if [ $j = a ]; then break 2; fi; done; done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "1", "break 2 must exit both loops on first hit");
    free(n);
    executor_free(exec);
}

TEST(loop_continue_2_skips_to_outer_next) {
    /// `continue 2` from the inner loop must skip the rest of BOTH
    /// loops on this iteration and advance the outer loop to its
    /// next iteration. With three outer iterations and a guaranteed
    /// `continue 2` on the first inner step, exactly three
    /// "outer-tail" markers should fire and zero inner-tail markers.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "OUTER=0; INNER=0; "
              "for i in 1 2 3; do "
              "  for j in a b c; do continue 2; INNER=$((INNER+1)); done; "
              "  OUTER=$((OUTER+1)); "
              "done");
    ASSERT_EXIT_STATUS(r, 0);

    char *inner = symtable_get_var(exec->symtable, "INNER");
    ASSERT_NOT_NULL(inner, "INNER should be set");
    ASSERT_STR_EQ(inner, "0", "continue 2 must skip the inner-tail entirely");
    free(inner);
    char *outer = symtable_get_var(exec->symtable, "OUTER");
    ASSERT_NOT_NULL(outer, "OUTER should be set");
    ASSERT_STR_EQ(outer, "0", "continue 2 must also skip the outer-tail");
    free(outer);
    executor_free(exec);
}

TEST(loop_break_3_unwinds_three_frames) {
    /// Triple-nested loop with `break 3` must unwind all three frames.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "N=0; for i in 1 2; do for j in 1 2; do for k in 1 2; do "
              "N=$((N+1)); break 3; done; done; done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_NOT_NULL(n, "N should be set");
    ASSERT_STR_EQ(n, "1", "break 3 must exit all three loops on first hit");
    free(n);
    executor_free(exec);
}

TEST(loop_break_1_is_same_as_plain_break) {
    /// `break 1` is the POSIX-explicit form of plain `break`.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "N=0; for i in 1 2 3; do N=$((N+1)); break 1; done");
    ASSERT_EXIT_STATUS(r, 0);

    char *n = symtable_get_var(exec->symtable, "N");
    ASSERT_STR_EQ(n, "1", "break 1 behaves like plain break");
    free(n);
    executor_free(exec);
}

TEST(loop_continue_outside_loop_errors) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(exec, "continue");
    ASSERT_EXIT_STATUS(r, 1);
    executor_free(exec);
}

TEST(loop_break_non_numeric_level_errors) {
    /// `break abc` inside a loop is a numeric-argument error.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec,
        "RC=0; for i in 1; do break abc || RC=$?; done; echo done; exit $RC");
    ASSERT_EXIT_STATUS(r, 1);
    executor_free(exec);
}

TEST(loop_break_level_exceeds_depth_errors) {
    /// `break 5` with only 1 loop nested is an over-deep-level error.
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec,
        "RC=0; for i in 1; do break 5 || RC=$?; done; echo done; exit $RC");
    ASSERT_EXIT_STATUS(r, 1);
    executor_free(exec);
}

TEST(loop_continue_non_numeric_level_errors) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");
    run_result_t r = run_shell_with_executor(
        exec, "RC=0; for i in 1; do continue abc || RC=$?; done; exit $RC");
    ASSERT_EXIT_STATUS(r, 1);
    executor_free(exec);
}

/* ============================================================================
 * PIPELINE TESTS
 * ============================================================================
 */

TEST(pipeline_simple) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Data must flow through the pipe: grep only succeeds if it sees the
    /// text echo wrote upstream.
    run_result_t r =
        run_shell_with_executor(exec, "echo hello | grep -q hello");
    ASSERT_EXIT_STATUS(r, 0);
    /// The piped content matters: a non-matching pattern makes grep exit 1.
    run_result_t r2 =
        run_shell_with_executor(exec, "echo hello | grep -q absent");
    ASSERT_EXIT_STATUS(r2, 1);

    executor_free(exec);
}

TEST(pipeline_exit_status) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Pipeline exit status is last command's status
    run_result_t r = run_shell_with_executor(exec, "true | false");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(pipeline_three_commands) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Data flows through all three stages onto grep.
    run_result_t r =
        run_shell_with_executor(exec, "echo hello | cat | grep -q hello");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

/* ============================================================================
 * EXTENDED TEST [[ ]] TESTS
 * ============================================================================
 */

TEST(extended_test_string_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ hello == hello ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_string_not_equal) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ hello != world ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_regex_match) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "[[ hello123 =~ ^hello[0-9]+$ ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_and) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ -n foo && -n bar ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_or) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ -z '' || -n foo ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(extended_test_pattern_match) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "[[ foobar == foo* ]]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

/* ============================================================================
 * Extended-test [[ ]] Conditional-AST: quoted-pattern literals (issue #515)
 * and the curated bash/zsh-consensus corrections that fall out of the AST.
 *
 * The engine builds a real conditional AST (operands are word-nodes carrying
 * quote_prov) and tree-walks it, so a quoted/escaped glob metacharacter in the
 * ==/!= RHS is literal while an unquoted one (including an unquoted $var
 * expansion) stays active. Exit 0 = true, non-zero = false.
 * ============================================================================
 */

/// Each entry is a [[ ]] command and its expected truth (true -> exit 0).
static void dbracket_expect(const char *src, bool truth) {
    executor_t *exec = executor_new();
    run_result_t r = run_shell_with_executor(exec, src);
    if (truth) {
        ASSERT_EXIT_STATUS(r, 0);
    } else {
        ASSERT_TRUE(r.exit_status != 0, src);
    }
    executor_free(exec);
}

TEST(dbracket_quoted_metachars_are_literal) {
    /// #515: quoted / escaped / single-quoted glob metacharacters in the RHS
    /// pattern match literally, so `abc` does NOT match a literal `a*`.
    dbracket_expect("[[ abc == \"a*\" ]]", false);
    dbracket_expect("[[ abc == a\\* ]]", false);
    dbracket_expect("[[ abc == 'a*' ]]", false);
    dbracket_expect("[[ axc == \"a?c\" ]]", false);
    dbracket_expect("[[ abc != \"a*\" ]]", true);
    /// A literal pattern still matches the identical literal subject.
    dbracket_expect("[[ \"a*\" == \"a*\" ]]", true);
    dbracket_expect("[[ \"a*b\" == \"a*b\" ]]", true);
    /// Quoted / escaped bracket class is a literal.
    dbracket_expect("[[ \"a[b]\" == \"a[b]\" ]]", true);
    dbracket_expect("[[ \"a[b]\" == a\\[b\\] ]]", true);
}

TEST(dbracket_quoted_var_pattern_is_literal) {
    /// A double-quoted variable expansion in the RHS is a literal pattern.
    dbracket_expect("y=\"a*\"; [[ abc == \"$y\" ]]", false);
    dbracket_expect("y=\"a*\"; [[ \"a*\" == \"$y\" ]]", true);
}

TEST(dbracket_unquoted_metachars_stay_active) {
    /// Unquoted metacharacters are an active glob; an unquoted $var expansion
    /// stays active too (the curated bash/zsh consensus).
    dbracket_expect("[[ abc == a* ]]", true);
    dbracket_expect("[[ axc == a?c ]]", true);
    dbracket_expect("[[ aXc == a[xX]c ]]", true);
    dbracket_expect("p=\"a*\"; [[ abc == $p ]]", true);
    /// Control: proves the unquoted-$var pattern is genuinely glob-active,
    /// not vacuously true.
    dbracket_expect("p=\"a*\"; [[ xyz == $p ]]", false);
}

TEST(dbracket_paren_patterns_and_regex) {
    /// Unquoted paren patterns / regex groups are active and preserved by the
    /// AST operand collection.
    dbracket_expect("[[ abc == @(abc|xyz) ]]", true);
    dbracket_expect("[[ abc =~ (abc|xyz) ]]", true);
    dbracket_expect("[[ abc =~ ^a.c$ ]]", true);
    dbracket_expect("[[ hello123 =~ ^([a-z]+)([0-9]+)$ ]]", true);
    dbracket_expect("[[ aXc == [a-z]X[a-z] ]]", true);
}

TEST(dbracket_curated_precedence_and_negation) {
    /// The AST gives && higher precedence than || (left-assoc), the bash/zsh
    /// consensus: `a || (f && f)` is the OR short-circuit -> true, where the
    /// old flat re-parse wrongly grouped `(a || f) && f` -> false.
    dbracket_expect("[[ a == a || f == x && f == x ]]", true);
    /// && binds tighter: `(t && f) || t` -> true.
    dbracket_expect("[[ t == t && f == x || t == t ]]", true);
    /// `!` composes over a parenthesized group (the old engine broke this).
    dbracket_expect("[[ ! ( -z x ) ]]", true);
    dbracket_expect("[[ ! ( -n x ) ]]", false);
    dbracket_expect("[[ ! ( a == b ) ]]", true);
}

TEST(dbracket_empty_is_false) {
    /// An empty [[ ]] is false (exit 1).
    executor_t *exec = executor_new();
    run_result_t r = run_shell_with_executor(exec, "[[ ]]");
    ASSERT_TRUE(r.exit_status != 0, "empty [[ ]] is false");
    executor_free(exec);
}

TEST(dbracket_no_word_split_of_operands) {
    /// Operands are scalar-expanded with no word splitting -- a spaced value
    /// stays one operand.
    dbracket_expect("x=\"a b\"; [[ $x == \"a b\" ]]", true);
    dbracket_expect("x=\"a b\"; [[ $x == a ]]", false);
}

/* ============================================================================
 * PARAMETER EXPANSION TESTS
 * ============================================================================
 */

TEST(param_default_value) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "RESULT=${UNDEFINED:-default}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "default",
                  "Should use default value for undefined var");
    free(result);

    executor_free(exec);
}

TEST(param_alternate_value) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=set; RESULT=${VAR:+alternate}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "alternate", "Should use alternate when var is set");
    free(result);

    executor_free(exec);
}

TEST(param_string_length) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "VAR=hello; LEN=${#VAR}");
    ASSERT_EXIT_STATUS(r, 0);

    char *len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "5", "Length of 'hello' should be 5");
    free(len);

    executor_free(exec);
}

TEST(param_string_length_counts_codepoints_not_bytes) {
    /// ${#var} returns CODEPOINT count (bash/zsh consensus), not byte
    /// count. Multi-byte strings used to byte-count, which broke
    /// real-world parity (#157 verification surfaced this).
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// 4 codepoints, 5 bytes.
    run_result_t r = run_shell_with_executor(exec, "V=caf\xc3\xa9; LEN=${#V}");
    ASSERT_EXIT_STATUS(r, 0);
    char *len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "4", "Length of 'caf\xc3\xa9' should be 4 (codepoints)");
    free(len);

    /// CJK: 2 codepoints, 6 bytes.
    r = run_shell_with_executor(exec, "V=\xe4\xb8\xad\xe6\x96\x87; LEN=${#V}");
    ASSERT_EXIT_STATUS(r, 0);
    len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(
        len, "2",
        "Length of '\xe4\xb8\xad\xe6\x96\x87' should be 2 (codepoints)");
    free(len);

    /// Emoji: abU+1F30Dc = 4 codepoints (a, b, U+1F30D, c), 7 bytes.
    r = run_shell_with_executor(exec, "V=ab\xf0\x9f\x8c\x8d"
                                      "c; LEN=${#V}");
    ASSERT_EXIT_STATUS(r, 0);
    len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "4",
                  "Length of 'ab\xf0\x9f\x8c\x8d"
                  "c' should be 4 (codepoints)");
    free(len);

    /// Empty string still returns 0.
    r = run_shell_with_executor(exec, "V=; LEN=${#V}");
    ASSERT_EXIT_STATUS(r, 0);
    len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "0", "Length of '' should be 0");
    free(len);

    executor_free(exec);
}

TEST(param_substring_removal_prefix) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=foobar; RESULT=${VAR#foo}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "bar", "Should remove 'foo' prefix");
    free(result);

    executor_free(exec);
}

TEST(param_substring_removal_suffix) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=foobar; RESULT=${VAR%bar}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "foo", "Should remove 'bar' suffix");
    free(result);

    executor_free(exec);
}

TEST(param_substring_offset_length) {
    /// ${var:offset:length} including a negative length, which is an
    /// offset from the end (bash semantics): the result ends |length|
    /// graphemes before the string end. Issue #131.
    run_result_t r = run_shell("s=abcdefgh\n"
                               "echo \"${s:2}\"\n"
                               "echo \"${s:2:3}\"\n"
                               "echo \"${s: -3}\"\n"
                               "echo \"${s:1:-2}\"\n"
                               "echo \"${s:1:-1}\"\n"
                               "echo \"${s: -4:-1}\"\n"
                               "echo \"[${s:2:0}]\"\n");
    ASSERT_STDOUT_EQ(r, "cdefgh\n"
                        "cde\n"
                        "fgh\n"
                        "bcdef\n"
                        "bcdefg\n"
                        "efg\n"
                        "[]\n");
}

TEST(rt_positional_param_slice) {
    /// ${@:offset:length} slices the positional parameters, counting $0
    /// at index 0 like bash; a negative offset counts from the end (#147).
    run_result_t r =
        run_shell("set -- a b c d e\n"
                  "for w in \"${@:2}\"; do echo \"f:$w\"; done\n"
                  "for w in \"${@:2:2}\"; do echo \"s:$w\"; done\n"
                  "for w in \"${@: -2}\"; do echo \"n:$w\"; done\n");
    ASSERT_STDOUT_EQ(r, "f:b\nf:c\nf:d\nf:e\n"
                        "s:b\ns:c\n"
                        "n:d\nn:e\n");
}

TEST(rt_zsh_modifier_path_parts) {
    /// zsh :h/:t/:r/:e path modifiers (issue #132).
    run_result_t r = run_shell("mode zsh\n"
                               "p=/usr/local/bin/script.tar.gz\n"
                               "echo \"${p:h}|${p:t}|${p:r}|${p:e}\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(
        r, "/usr/local/bin|script.tar.gz|/usr/local/bin/script.tar|gz\n");
}

TEST(rt_zsh_modifier_case_and_quote) {
    /// :l/:u case modifiers and :q (backslash quoting, zsh style).
    run_result_t r = run_shell("mode zsh\n"
                               "s=\"Hello World\"\n"
                               "echo \"${s:l}|${s:u}|${s:q}\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "hello world|HELLO WORLD|Hello\\ World\n");
}

TEST(rt_zsh_modifier_substitute) {
    /// :s/old/new/ (first) and :gs/old/new/ (global).
    run_result_t r = run_shell("mode zsh\n"
                               "p=/usr/local/bin\n"
                               "echo \"${p:s/local/LOCAL/}|${p:gs/o/0/}\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "/usr/LOCAL/bin|/usr/l0cal/bin\n");
}

TEST(rt_zsh_modifier_chain_and_nest) {
    /// Chained (:t:r) and nested (${${p:t}:r}) modifiers compose.
    run_result_t r = run_shell("mode zsh\n"
                               "p=/usr/local/bin/script.tar.gz\n"
                               "echo \"${p:t:r}|${${p:t}:r}|${${p:h}:t}\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "script.tar|script.tar|bin\n");
}

TEST(rt_zsh_modifier_off_in_bash_is_substring) {
    /// In bash mode ${var:h} is substring-offset (h parses as 0), not a
    /// modifier, so the whole value is returned -- matching bash.
    run_result_t r = run_shell("mode bash\n"
                               "p=/usr/local/bin\n"
                               "echo \"${p:h}\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "/usr/local/bin\n");
}

TEST(rt_zsh_modifier_numeric_arg_still_substring) {
    /// Even in zsh mode a numeric argument is substring, not a modifier.
    run_result_t r = run_shell("mode zsh\n"
                               "s=abcdef\n"
                               "echo \"${s:2}|${s:1:3}\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "cdef|bcd\n");
}

TEST(rt_zsh_param_indirect_P) {
    /// ${(P)ref} expands the variable NAMED by $ref (indirect), and
    /// composes with other flags, e.g. ${(PU)ref}. Issue #142.
    run_result_t r = run_shell("mode zsh\n"
                               "target=hello\n"
                               "ref=target\n"
                               "echo \"${(P)ref}|${(PU)ref}\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "hello|HELLO\n");
}

TEST(param_pattern_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=hello; RESULT=${VAR/l/L}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "heLlo", "Should replace first 'l' with 'L'");
    free(result);

    executor_free(exec);
}

TEST(param_global_substitution) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "VAR=hello; RESULT=${VAR//l/L}");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "heLLo", "Should replace all 'l' with 'L'");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * ARRAY TESTS
 * ============================================================================
 */

TEST(array_indexed_assignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Each element is individually addressable by index after assignment;
    /// the && chain only reaches exit 0 if all three indices resolve.
    run_result_t r = run_shell_with_executor(
        exec, "arr=(one two three); [ \"${arr[0]}\" = one ] && "
              "[ \"${arr[1]}\" = two ] && [ \"${arr[2]}\" = three ]");
    ASSERT_EXIT_STATUS(r, 0);

    executor_free(exec);
}

TEST(array_element_access) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "arr=(a b c); ELEM=${arr[1]}");
    ASSERT_EXIT_STATUS(r, 0);

    char *elem = symtable_get_var(exec->symtable, "ELEM");
    ASSERT_STR_EQ(elem, "b", "arr[1] should be 'b'");
    free(elem);

    executor_free(exec);
}

TEST(array_length) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "arr=(a b c d); LEN=${#arr[@]}");
    ASSERT_EXIT_STATUS(r, 0);

    char *len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "4", "Array should have 4 elements");
    free(len);

    executor_free(exec);
}

TEST(array_append) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "arr=(a b); arr+=(c d); LEN=${#arr[@]}");
    ASSERT_EXIT_STATUS(r, 0);

    char *len = symtable_get_var(exec->symtable, "LEN");
    ASSERT_STR_EQ(len, "4", "Array should have 4 elements after append");
    free(len);

    executor_free(exec);
}

/* ============================================================================
 * COMMAND SUBSTITUTION TESTS
 * Note: stdout capture from external commands in test environment is unreliable
 * due to file descriptor sharing with test harness. These tests verify the
 * syntax works; actual output capture works correctly in real shell usage.
 * ============================================================================
 */

TEST(command_substitution_syntax) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// Command substitution captures the inner command's stdout into X.
    run_result_t r = run_shell_with_executor(exec, "X=$(echo hello)");
    ASSERT_EXIT_STATUS(r, 0);

    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_STR_EQ(x, "hello", "$(...) captures the inner command output");
    free(x);

    executor_free(exec);
}

TEST(command_substitution_exit_status) {
    /// $? reflects the exit status of the command inside $() (issue #58,
    /// fixed).
    run_result_t r = run_shell("x=$(false); echo \"after_false=$?\"\n"
                               "y=$(true); echo \"after_true=$?\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "after_false=1\nafter_true=0\n");
}

/* ============================================================================
 * SPECIAL VARIABLE TESTS
 * ============================================================================
 */

TEST(special_var_question_mark) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    executor_execute_command_line(exec, "true", 1);
    run_result_t r = run_shell_with_executor(exec, "STATUS=$?");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "STATUS");
    ASSERT_STR_EQ(result, "0", "$? after true should be 0");
    free(result);

    executor_free(exec);
}

TEST(special_var_dollar_dollar) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "PID=$$");
    ASSERT_EXIT_STATUS(r, 0);

    char *pid = symtable_get_var(exec->symtable, "PID");
    ASSERT_NOT_NULL(pid, "$$ should be set");
    /// $$ must expand to a decimal PID, not the literal "$$" or an empty
    /// string. (The harness leaves shell_pid at its 0 default since lush.c's
    /// main does not run, so the value itself is "0" here; the contract being
    /// guarded is that the expansion produced digits.)
    ASSERT(pid[0] != '\0', "$$ expands to a non-empty value");
    for (const char *p = pid; *p; p++) {
        ASSERT(*p >= '0' && *p <= '9', "$$ expands to decimal digits only");
    }
    free(pid);

    executor_free(exec);
}

TEST(special_var_argc) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "set -- a b c; COUNT=$#");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_STR_EQ(count, "3", "$# should be 3");
    free(count);

    executor_free(exec);
}

/* ============================================================================
 * NESTED CONTROL STRUCTURE TESTS
 * ============================================================================
 */

TEST(nested_if) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "X=5; if [ $X -gt 0 ]; then "
              "  if [ $X -lt 10 ]; then RESULT=between; fi; "
              "fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "between", "Nested condition should set RESULT");
    free(result);

    executor_free(exec);
}

TEST(nested_loops) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(
        exec, "COUNT=0; for i in 1 2; do "
              "  for j in a b; do COUNT=$((COUNT+1)); done; "
              "done");
    ASSERT_EXIT_STATUS(r, 0);

    char *count = symtable_get_var(exec->symtable, "COUNT");
    ASSERT_STR_EQ(count, "4", "Should iterate 2*2=4 times");
    free(count);

    executor_free(exec);
}

/* ============================================================================
 * ELIF TESTS
 * ============================================================================
 */

TEST(elif_chain) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "X=2; "
                                      "if [ $X -eq 1 ]; then RESULT=one; "
                                      "elif [ $X -eq 2 ]; then RESULT=two; "
                                      "elif [ $X -eq 3 ]; then RESULT=three; "
                                      "else RESULT=other; fi");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "two", "Should match second elif");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * NEGATION TESTS
 * ============================================================================
 */

TEST(negation_command) {
    /// `! command` negates the exit status (issue #57, fixed).
    run_result_t r = run_shell("! false; echo \"not_false=$?\"\n"
                               "! true; echo \"not_true=$?\"\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "not_false=0\nnot_true=1\n");
}

/* ============================================================================
 * HERE STRING TESTS
 * ============================================================================
 */

TEST(here_string) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "RESULT=$(cat <<< 'hello')");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_NOT_NULL(result, "RESULT should be set");
    /// The here-string feeds "hello\n" to cat; command substitution strips the
    /// trailing newline, so RESULT is exactly "hello".
    ASSERT_STR_EQ(result, "hello", "here string delivers 'hello' to cat");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * MORE ARITHMETIC TESTS
 * ============================================================================
 */

TEST(arithmetic_comparison) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "(( 5 > 3 ))");
    ASSERT_EXIT_STATUS(r, 0);

    r = run_shell_with_executor(exec, "(( 3 > 5 ))");
    ASSERT_EXIT_STATUS(r, 1);

    executor_free(exec);
}

TEST(arithmetic_assignment) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r = run_shell_with_executor(exec, "(( X = 5 + 3 ))");
    ASSERT_EXIT_STATUS(r, 0);

    char *x = symtable_get_var(exec->symtable, "X");
    ASSERT_STR_EQ(x, "8", "X should be 8");
    free(x);

    executor_free(exec);
}

TEST(arithmetic_ternary) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "X=5; RESULT=$((X > 3 ? 1 : 0))");
    ASSERT_EXIT_STATUS(r, 0);

    char *result = symtable_get_var(exec->symtable, "RESULT");
    ASSERT_STR_EQ(result, "1", "Ternary should return 1 when true");
    free(result);

    executor_free(exec);
}

/* ============================================================================
 * LOCAL VARIABLE TESTS
 * ============================================================================
 */

TEST(local_variable_in_function) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    run_result_t r =
        run_shell_with_executor(exec, "GLOBAL=outer; "
                                      "f() { local GLOBAL=inner; }; "
                                      "f");
    ASSERT_EXIT_STATUS(r, 0);

    char *global = symtable_get_var(exec->symtable, "GLOBAL");
    ASSERT_STR_EQ(global, "outer",
                  "GLOBAL should remain 'outer' after function");
    free(global);

    executor_free(exec);
}

/* ============================================================================
 * REGRESSION TESTS -- 2026-05 real-world hardening wave (PR #113)
 *
 * Behavioral coverage for the defects the real-world script corpus
 * surfaced. Every test runs a shell snippet in-process and asserts on
 * observable behavior; each names the fix it guards.
 * ============================================================================
 */

/// Create an empty file -- fixture helper for the glob tests.
static void rt_touch(const char *path) {
    FILE *f = fopen(path, "w");
    if (f) {
        fclose(f);
    }
}

/// --- Deferred here-document collection --------------------------------

TEST(rt_heredoc_through_pipe) {
    /// The body must reach `tr`; the parser once collected the heredoc
    /// inline and lost the `| tr` that followed `<<EOF` on the line.
    run_result_t r = run_shell("cat <<EOF | tr a-z A-Z\nhello world\nEOF\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "HELLO WORLD\n");
}

TEST(rt_heredoc_trailing_command) {
    run_result_t r = run_shell("cat <<EOF; echo after\nbody\nEOF\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "body\nafter\n");
}

TEST(rt_heredoc_in_if_body) {
    run_result_t r = run_shell("if true; then\ncat <<EOF\ninside\nEOF\nfi\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "inside\n");
}

TEST(rt_heredoc_loop_redirection) {
    run_result_t r =
        run_shell("while read l; do echo \"got $l\"; done <<EOF\nx\ny\nEOF\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "got x\ngot y\n");
}

TEST(rt_heredoc_strip_tabs) {
    /// <<- strips leading tabs from body lines and the terminator.
    run_result_t r = run_shell("cat <<-EOF\n\t\tindented\n\t\tEOF\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "indented\n");
}

TEST(rt_heredoc_quoted_delimiter) {
    /// A quoted delimiter disables expansion of the body.
    run_result_t r = run_shell("cat <<'EOF'\nliteral $UNSET here\nEOF\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "literal $UNSET here\n");
}

TEST(rt_heredoc_multiline_var_body) {
    /// Body referencing a multi-line variable -- posix/105 regression.
    run_result_t r =
        run_shell("x=\"line1\nline2\"\ncat <<EOF\n$x\nEOF\necho done\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "line1\nline2\ndone\n");
}

TEST(rt_heredoc_unterminated_error) {
    run_result_t r = run_shell("cat <<EOF\nno terminator follows\n");
    ASSERT_TRUE(r.exit_status != 0, "unterminated heredoc must fail");
}

TEST(rt_heredoc_delimiter_nfc_vs_nfd) {
    /// NFC script delimiter "EOF_\xc3\xa9" (E O F _ + precomposed
    /// e-acute U+00E9 = 5 bytes) is terminated by an NFD line
    /// "EOF_e\xcc\x81" (E O F _ e + combining acute U+0301 = 6
    /// bytes). The heredoc body should be exactly "body\n".
    run_result_t r = run_shell("cat <<EOF_\xc3\xa9\n"
                               "body\n"
                               "EOF_e\xcc\x81\n");
    ASSERT_STDOUT_EQ(r, "body\n");
    ASSERT_EXIT_STATUS(r, 0);
}

TEST(rt_heredoc_delimiter_nfd_vs_nfc) {
    /// Mirror: NFD script delimiter, NFC terminator. Same outcome.
    run_result_t r = run_shell("cat <<EOF_e\xcc\x81\n"
                               "body\n"
                               "EOF_\xc3\xa9\n");
    ASSERT_STDOUT_EQ(r, "body\n");
    ASSERT_EXIT_STATUS(r, 0);
}

TEST(rt_heredoc_delimiter_unequal_when_actually_different) {
    /// NFC normalization does not paper over real differences.
    /// "EOF" terminator does not close a "DONE"-delimited heredoc;
    /// the script reaches EOF without finding the delimiter and
    /// the parser reports an unterminated heredoc.
    run_result_t r = run_shell("cat <<DONE\nEOF\n");
    ASSERT_TRUE(r.exit_status != 0, "DONE delimiter is not terminated by EOF");
}

TEST(rt_assoc_key_nfc_nfd_collapse) {
    /// declare -A m; m[NFC]=one; m[NFD]=two
    /// Both keys (NFC e-acute precomposed + NFD e + combining acute)
    /// must hash to the same entry. The second set overwrites the
    /// first; the resulting array has one entry and both lookup
    /// forms find it.
    run_result_t r = run_shell("declare -A m\n"
                               "nfc=$'caf\\xc3\\xa9'\n"
                               "nfd=$'cafe\\xcc\\x81'\n"
                               "m[$nfc]=one\n"
                               "m[$nfd]=two\n"
                               "echo len=${#m[@]}\n"
                               "echo via_nfc=${m[$nfc]}\n"
                               "echo via_nfd=${m[$nfd]}\n");
    ASSERT_STDOUT_EQ(r, "len=1\nvia_nfc=two\nvia_nfd=two\n");
}

TEST(rt_assoc_key_distinct_when_actually_different) {
    /// NFC normalization does not paper over real key differences.
    /// "cafe-acute" and "CAFE-acute" hash to separate entries (case folding is
    /// a separate axis from NFC normalization).
    run_result_t r = run_shell("declare -A m\n"
                               "m[caf\xc3\xa9]=lower\n"
                               "m[CAF\xc3\x89]=upper\n"
                               "echo len=${#m[@]}\n"
                               "echo lower=${m[caf\xc3\xa9]}\n"
                               "echo upper=${m[CAF\xc3\x89]}\n");
    ASSERT_STDOUT_EQ(r, "len=2\nlower=lower\nupper=upper\n");
}

TEST(rt_assoc_key_ascii_paths_unchanged) {
    /// ASCII keys hit the primitive's strdup fast path -- behavior
    /// must be byte-identical to the prior implementation.
    run_result_t r = run_shell("declare -A m\n"
                               "m[foo]=one\n"
                               "m[bar]=two\n"
                               "m[baz]=three\n"
                               "echo ${m[foo]}\n"
                               "echo ${m[bar]}\n"
                               "echo ${m[baz]}\n"
                               "echo len=${#m[@]}\n");
    ASSERT_STDOUT_EQ(r, "one\ntwo\nthree\nlen=3\n");
}

TEST(rt_unicode_ident_declare_and_expand_latin) {
    /// FEATURE_UNICODE_IDENTIFIERS is default-on in lush mode. A
    /// non-ASCII identifier (precomposed cafe-acute) round-trips through
    /// declare's name validation, symtable storage, tokenizer
    /// $var parsing, and the expand_variable name-extraction loop.
    run_result_t r =
        run_shell("declare caf\xc3\xa9=hello\necho $caf\xc3\xa9\n");
    ASSERT_STDOUT_EQ(r, "hello\n");
}

TEST(rt_unicode_ident_declare_and_expand_greek) {
    run_result_t r = run_shell("declare \xce\xa3=sigma\necho $\xce\xa3\n");
    ASSERT_STDOUT_EQ(r, "sigma\n");
}

TEST(rt_unicode_ident_declare_and_expand_cyrillic) {
    /// Cyrillic "U+0438 U+043C U+044F" (name) -- 3 codepoints, 6 UTF-8 bytes.
    run_result_t r = run_shell("declare \xd0\xb8\xd0\xbc\xd1\x8f=name\n"
                               "echo $\xd0\xb8\xd0\xbc\xd1\x8f\n");
    ASSERT_STDOUT_EQ(r, "name\n");
}

TEST(rt_unicode_ident_rejected_in_bash_mode) {
    /// `mode bash` flips FEATURE_UNICODE_IDENTIFIERS off; identifier
    /// validation falls back to POSIX ASCII-only. declare rejects
    /// the non-ASCII name.
    run_result_t r = run_shell("mode bash\ndeclare caf\xc3\xa9=hello\n");
    ASSERT_STDERR_CONTAINS(r, "not a valid identifier");
    /// Restore lush mode so subsequent tests in this in-process suite
    /// see the default feature matrix (shell_mode state is global).
    (void)run_shell("mode lush\n");
}

TEST(rt_unicode_ident_opt_in_from_bash_mode) {
    /// Feature matrix gate: the user can opt in via shopt from any
    /// mode, including bash, without leaving the preset.
    ///
    /// The mode-change + opt-in is performed in a separate run_shell
    /// call from the identifier-using script so the tokenizer sees
    /// the feature flag already set. Within a single run_shell, the
    /// tokenizer parses the entire input upfront before any
    /// statement executes; if `shopt -s` and `$caf<UTF-8>` lived in
    /// the same input, the tokenizer would still see the feature off
    /// when scanning `$caf<UTF-8>` because shopt has not run yet.
    /// Two separate run_shell calls share global shell_mode state,
    /// so the second call's tokenizer sees the override.
    run_result_t setup = run_shell("mode bash\nshopt -s unicode_identifiers\n");
    ASSERT_EXIT_STATUS(setup, 0);
    run_result_t r = run_shell("declare caf\xc3\xa9=hi\necho $caf\xc3\xa9\n");
    ASSERT_STDOUT_EQ(r, "hi\n");
    /// Restore lush-mode harness default for subsequent tests.
    (void)run_shell("mode lush\n");
}

TEST(rt_unicode_ident_ascii_paths_unchanged) {
    /// ASCII identifiers hit the fast path in both lush and bash
    /// modes; behavior must be byte-identical to the prior
    /// isalpha/isalnum implementation.
    run_result_t r = run_shell("X=hello\n"
                               "_y=world\n"
                               "Z9=ok\n"
                               "echo $X $_y $Z9\n");
    ASSERT_STDOUT_EQ(r, "hello world ok\n");
}

TEST(rt_unicode_ident_invalid_start_still_rejected) {
    /// Numeric-leading names are still invalid in both modes;
    /// FEATURE_UNICODE_IDENTIFIERS widens the LETTER set but does
    /// not relax the Start-vs-Continue distinction.
    run_result_t r = run_shell("declare 9bad=oops\n");
    ASSERT_STDERR_CONTAINS(r, "not a valid identifier");
}

TEST(rt_unicode_ident_arithmetic_bare) {
    /// Non-ASCII identifier as a bare arithmetic operand:
    /// $((cafe-acute+1)) reads cafe-acute from the symbol table.
    run_result_t r = run_shell("declare caf\xc3\xa9=41\n"
                               "echo $((caf\xc3\xa9+1))\n");
    ASSERT_STDOUT_EQ(r, "42\n");
}

TEST(rt_unicode_ident_arithmetic_dollar) {
    /// Non-ASCII identifier with $ sigil inside arithmetic.
    run_result_t r = run_shell("declare \xce\xa3=10\n"
                               "echo $(($\xce\xa3*3))\n");
    ASSERT_STDOUT_EQ(r, "30\n");
}

TEST(rt_unicode_ident_arithmetic_compound) {
    /// (( ... )) compound, increment with non-ASCII name.
    run_result_t r = run_shell("declare \xd0\xb8\xd0\xbc\xd1\x8f=7\n"
                               "(( \xd0\xb8\xd0\xbc\xd1\x8f += 5 ))\n"
                               "echo $\xd0\xb8\xd0\xbc\xd1\x8f\n");
    ASSERT_STDOUT_EQ(r, "12\n");
}

TEST(rt_unicode_ident_arithmetic_positional_unchanged) {
    /// Positional parameters in arithmetic (digit-leading) still work;
    /// my edit to get_var_name_with_context must not regress this.
    run_result_t r = run_shell("set -- 7 8\n"
                               "echo $(($1+$2))\n");
    ASSERT_STDOUT_EQ(r, "15\n");
}

TEST(rt_unicode_ident_function_name_lush) {
    /// Function definition with a non-ASCII name in lush mode.
    /// Parser was already permissive here (POSIX-only validation),
    /// but the lush_is_valid_identifier delegation must not regress
    /// the lush case.
    run_result_t r =
        run_shell("funci\xc3\xb3n() { echo from-funci\xc3\xb3n; }\n"
                  "funci\xc3\xb3n\n");
    ASSERT_STDOUT_EQ(r, "from-funci\xc3\xb3n\n");
}

TEST(rt_unicode_ident_function_name_posix_rejected) {
    /// In POSIX mode (with the feature off by default), a function
    /// name containing non-ASCII bytes must be rejected. Two separate
    /// run_shell calls so the parser sees posix_mode = true.
    (void)run_shell("set -o posix\n");
    run_result_t r = run_shell("funci\xc3\xb3n() { :; }\n");
    ASSERT_STDERR_CONTAINS(r, "invalid function name in POSIX mode");
    (void)run_shell("mode lush\n");
}

TEST(rt_unicode_ident_function_name_posix_opt_in) {
    /// POSIX mode plus `shopt -s unicode_identifiers` must accept
    /// the unicode name. Same multi-input pattern as the bash opt-in
    /// test: parser sees posix_mode + feature override at parse time.
    (void)run_shell("set -o posix\n"
                    "shopt -s unicode_identifiers\n");
    run_result_t r = run_shell("funci\xc3\xb3n() { echo posix-ok; }\n"
                               "funci\xc3\xb3n\n");
    ASSERT_STDOUT_EQ(r, "posix-ok\n");
    (void)run_shell("mode lush\n");
}

TEST(rt_unicode_ident_alias_name_lush) {
    /// Non-ASCII alias name accepted in lush mode (FEATURE_UNICODE_IDENTIFIERS
    /// default on). init_aliases() seeds the global alias table the in-process
    /// harness shares with set_alias; the real shell calls it during startup
    /// via src/init.c but the harness has no equivalent entry point.
    init_aliases();
    run_result_t r = run_shell("alias caf\xc3\xa9=\"echo from-caf\xc3\xa9\"\n"
                               "caf\xc3\xa9\n");
    ASSERT_STDOUT_EQ(r, "from-caf\xc3\xa9\n");
}

TEST(rt_unicode_ident_alias_name_bash_rejected) {
    /// In bash mode, the byte-level check rejects cafe-acute.
    (void)run_shell("mode bash\n");
    run_result_t r = run_shell("alias caf\xc3\xa9=\"echo nope\"\n");
    ASSERT_STDERR_CONTAINS(r, "invalid alias name");
    (void)run_shell("mode lush\n");
}

TEST(rt_unicode_ident_alias_digit_leading_unchanged) {
    /// Alias accepts digit-leading names (bash+zsh consensus); the
    /// migration must not regress this.
    init_aliases();
    run_result_t r = run_shell("alias 1=\"echo one\"\n"
                               "1\n");
    ASSERT_STDOUT_EQ(r, "one\n");
}

TEST(rt_unicode_ident_alias_extension_chars_unchanged) {
    /// Alias accepts .-+ as name characters; preserve.
    init_aliases();
    run_result_t r = run_shell("alias my.cmd=\"echo dotted\"\n"
                               "my.cmd\n");
    ASSERT_STDOUT_EQ(r, "dotted\n");
}

TEST(rt_unicode_ident_array_subscript) {
    /// ${arr[N]} subscript lookup with a unicode array name. Before
    /// the executor wave the name-prefix validator in
    /// parse_parameter_expansion rejected the non-ASCII bytes and
    /// silently fell through to the scalar branch, returning empty.
    run_result_t r = run_shell(
        "caf\xc3\xa9=(alpha beta gamma)\n"
        "echo \"${caf\xc3\xa9[0]}=${caf\xc3\xa9[1]}=${caf\xc3\xa9[2]}\"\n");
    ASSERT_STDOUT_EQ(r, "alpha=beta=gamma\n");
}

TEST(rt_unicode_ident_array_length) {
    /// ${#arr[@]} on a unicode-named array.
    run_result_t r = run_shell("caf\xc3\xa9=(one two three four)\n"
                               "echo \"${#caf\xc3\xa9[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "4\n");
}

TEST(rt_unicode_ident_array_vector_in_for) {
    /// "${arr[@]}" word-list expansion in a for-loop body. Before
    /// the wave, try_expand_vector_arg's byte-test name extraction
    /// declined the unicode name and the scalar-slot enforcement
    /// raised SHELL_ERR_TYPE_MISMATCH ("list value in scalar position").
    run_result_t r =
        run_shell("caf\xc3\xa9=(a b c)\n"
                  "for x in \"${caf\xc3\xa9[@]}\"; do echo \"[$x]\"; done\n");
    ASSERT_STDOUT_EQ(r, "[a]\n[b]\n[c]\n");
}

TEST(rt_unicode_ident_array_joined) {
    /// ${arr[*]} space-joined form.
    run_result_t r = run_shell("caf\xc3\xa9=(one two three)\n"
                               "echo \"[${caf\xc3\xa9[*]}]\"\n");
    ASSERT_STDOUT_EQ(r, "[one two three]\n");
}

TEST(rt_unicode_ident_assoc_array_subscript) {
    /// Associative array with a unicode name; key lookup honors the
    /// predicate-driven name prefix detection too.
    run_result_t r = run_shell("declare -A \xd0\xb8\xd0\xbc\xd1\x8f\n"
                               "\xd0\xb8\xd0\xbc\xd1\x8f[k1]=v1\n"
                               "\xd0\xb8\xd0\xbc\xd1\x8f[k2]=v2\n"
                               "echo "
                               "\"${\xd0\xb8\xd0\xbc\xd1\x8f[k1]}=${"
                               "\xd0\xb8\xd0\xbc\xd1\x8f[k2]}\"\n");
    ASSERT_STDOUT_EQ(r, "v1=v2\n");
}

TEST(rt_unicode_ident_kind_sigil) {
    /// Bare `@cafe-acute` kind sigil on a unicode-named array (lush default has
    /// FEATURE_KIND_SIGILS on). Sigils are bare-word-only; double quotes
    /// would suppress them (see rt_sigil_literal_in_double_quotes).
    run_result_t r = run_shell("caf\xc3\xa9=(a b c)\n"
                               "echo @caf\xc3\xa9\n");
    ASSERT_STDOUT_EQ(r, "a b c\n");
}

TEST(rt_unicode_ident_bare_array_expands_zsh_lush) {
    /// Bare $arr explodes to N words in zsh/lush mode -- now also
    /// works for unicode names via the migrated try_expand_vector_arg
    /// detection.
    run_result_t r =
        run_shell("caf\xc3\xa9=(a b c)\n"
                  "for x in $caf\xc3\xa9; do echo \"[$x]\"; done\n");
    ASSERT_STDOUT_EQ(r, "[a]\n[b]\n[c]\n");
}

TEST(rt_unicode_ident_indirect_expansion_target) {
    /// ${!ptr} resolves the value of $ptr as a variable name. Both
    /// the pointer and the target may now be unicode-named.
    run_result_t r = run_shell("declare ptr=caf\xc3\xa9\n"
                               "declare caf\xc3\xa9=hello\n"
                               "echo \"${!ptr}\"\n");
    ASSERT_STDOUT_EQ(r, "hello\n");
}

TEST(rt_unicode_ident_indirect_expansion_pointer) {
    /// Pointer itself uses a unicode identifier name.
    run_result_t r = run_shell("declare \xd0\xb8\xd0\xbc\xd1\x8f=caf\xc3\xa9\n"
                               "declare caf\xc3\xa9=world\n"
                               "echo \"${!\xd0\xb8\xd0\xbc\xd1\x8f}\"\n");
    ASSERT_STDOUT_EQ(r, "world\n");
}

TEST(rt_unicode_ident_zsh_bare_subscript) {
    /// $arr[0] (zsh bare subscript, no braces). The tokenizer's
    /// quoted-string $NAME scanner used to byte-test the name;
    /// now it walks lush_ident_match_continue.
    run_result_t r = run_shell("caf\xc3\xa9=(a b c)\n"
                               "echo $caf\xc3\xa9[0]\n");
    ASSERT_STDOUT_EQ(r, "a\n");
}

TEST(rt_unicode_ident_fd_allocation_var) {
    /// exec {varname}>FILE allocates a free fd into $varname.
    /// The tokenizer's {NAME} fd-allocation scanner must accept
    /// unicode names too.
    run_result_t r = run_shell("exec {caf\xc3\xa9}>/tmp/lush_ut_fd.out\n"
                               "[ $caf\xc3\xa9 -ge 10 ] && echo high-fd\n");
    ASSERT_STDOUT_EQ(r, "high-fd\n");
    unlink("/tmp/lush_ut_fd.out");
}

TEST(rt_unicode_ident_nfd_passes_validation) {
    /// NFD-encoded "cafe + combining-acute" (U+0301) validates as a
    /// single canonicalized identifier "cafe-acute" after the validator's
    /// NFC normalization. Before this wave, declare rejected NFD with
    /// "not a valid identifier". Lookup uses the precomposed NFC name
    /// "caf<e-acute>" (4 codepoints) -- the canonical storage form.
    run_result_t r = run_shell("declare cafe\xCC\x81=NFD_FORM\n"
                               "echo \"$caf\xC3\xA9\"\n");
    ASSERT_STDOUT_EQ(r, "NFD_FORM\n");
}

TEST(rt_unicode_ident_nfd_reference_resolves) {
    /// The bidirectional lookup: store the binding under the NFC form,
    /// then reference it with NFD bytes (cafe + U+0301). The tokenizer
    /// now pulls the combining mark into the identifier token (UAX #31
    /// Continue accepts GB_EXTEND), so expand_variable receives the
    /// full NFD name, canonicalizes it to NFC, and resolves the binding.
    /// Regression guard for the showstopper where the name scan stopped
    /// at the mark and truncated to "cafe".
    run_result_t r = run_shell("declare caf\xC3\xA9=NFC_STORED\n"
                               "echo \"$cafe\xCC\x81\"\n");
    ASSERT_STDOUT_EQ(r, "NFC_STORED\n");
}

TEST(rt_unicode_ident_leading_mark_not_identifier) {
    /// A bare leading combining mark must NOT start an identifier
    /// (lush_ident_match_start rejects marks). `$<U+0301>x` therefore
    /// does not form a variable reference; the `$` and the mark fall
    /// through to literal text. Asserts the Start position stays
    /// mark-rejecting after the Continue-position relaxation.
    run_result_t r = run_shell("echo \"[\xCC\x81x]\"\n");
    ASSERT_STDOUT_EQ(r, "[\xCC\x81x]\n");
}

TEST(rt_unicode_ident_nfc_nfd_storage_collision) {
    /// NFC-encoded `cafe-acute` and NFD-encoded `cafe + U+0301` MUST
    /// collide as one binding in the symtable. Set via NFC, overwrite
    /// via NFD, read via NFC: the NFC read sees the NFD-side write.
    run_result_t r = run_shell("declare caf\xC3\xA9=NFC_FIRST\n"
                               "declare cafe\xCC\x81=NFD_OVERWRITE\n"
                               "echo \"$caf\xC3\xA9\"\n");
    ASSERT_STDOUT_EQ(r, "NFD_OVERWRITE\n");
}

TEST(rt_unicode_ident_nfc_nfd_alias_collision) {
    /// Same NFC/NFD collision for aliases. valid_alias_name and
    /// set_alias both canonicalize.
    init_aliases();
    run_result_t r = run_shell("alias caf\xC3\xA9=\"echo NFC_DEF\"\n"
                               "alias cafe\xCC\x81=\"echo NFD_DEF\"\n"
                               "caf\xC3\xA9\n");
    ASSERT_STDOUT_EQ(r, "NFD_DEF\n");
}

TEST(rt_unicode_ident_nfc_nfd_function_collision) {
    /// Functions also collide on NFC. store_function and find_function
    /// share the canonicalizer. The unalias up front avoids name-
    /// resolution shadowing from the prior alias-collision test, which
    /// leaves a global alias of the same name behind (aliases beat
    /// functions in the dispatch order).
    init_aliases();
    (void)run_shell("unalias caf\xC3\xA9 2>/dev/null || true\n");
    run_result_t r = run_shell("caf\xC3\xA9() { echo NFC_BODY; }\n"
                               "cafe\xCC\x81() { echo NFD_BODY; }\n"
                               "caf\xC3\xA9\n");
    ASSERT_STDOUT_EQ(r, "NFD_BODY\n");
}

TEST(rt_unicode_ident_nfc_array_assoc_collision) {
    /// Same canonicalization for arrays via symtable_set_array /
    /// symtable_get_array.
    run_result_t r = run_shell("declare -A caf\xC3\xA9\n"
                               "caf\xC3\xA9[k]=v_nfc\n"
                               "echo \"${caf\xC3\xA9[k]}\"\n");
    ASSERT_STDOUT_EQ(r, "v_nfc\n");
}

TEST(rt_unicode_ident_highlight_spans_full_name) {
    /// Syntax highlighter must color the whole `$cafe-acute` as one VARIABLE
    /// token. The byte-test it replaced stopped at the multibyte `e-acute`,
    /// truncating the highlighted span to `$caf`. This binary links the
    /// strong, feature-flag-aware lush_ident_match_* (src/identifier.c),
    /// so it overrides the ASCII weak fallback in liblle and proves the
    /// real product highlights unicode identifiers. `$cafe-acute` is 6 bytes:
    /// `$` + `caf` + `e-acute` (0xC3 0xA9). FEATURE_UNICODE_IDENTIFIERS is the
    /// lush-mode default the harness runs under.
    (void)run_shell("mode lush\n");
    lle_syntax_highlighter_t *h = NULL;
    ASSERT_EQ(lle_syntax_highlighter_create(&h), 0, "create highlighter");
    const char *input = "echo $caf\xC3\xA9";
    lle_syntax_highlight(h, input, strlen(input));
    size_t count = 0;
    const lle_syntax_token_t *toks = lle_syntax_get_tokens(h, &count);
    size_t var_start = 0, var_end = 0;
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        if (toks[i].type == LLE_TOKEN_VARIABLE) {
            var_start = toks[i].start;
            var_end = toks[i].end;
            found = true;
            break;
        }
    }
    lle_syntax_highlighter_destroy(h);
    ASSERT(found, "a VARIABLE token was produced for $caf\xc3\xa9");
    /// `$cafe-acute` begins at offset 5 ("echo " is 5 bytes) and is 6 bytes.
    ASSERT_EQ(var_start, (size_t)5, "variable token starts at $");
    ASSERT_EQ(var_end - var_start, (size_t)6,
              "variable token spans $caf\xc3\xa9");
}

TEST(rt_unicode_ident_mixed_script_detected) {
    /// `pU+0430sswd` -- Latin p, then Cyrillic U+0430 (U+0430), then Latin sswd
    /// -- is the classic homograph: visually `passwd`. lush_ident_mixes_scripts
    /// reports the crossing and names both scripts.
    const char *a = NULL, *b = NULL;
    bool mixed = lush_ident_mixes_scripts("p\xD0\xB0sswd", &a, &b);
    ASSERT(mixed, "Latin+Cyrillic name flagged as mixed-script");
    ASSERT_NOT_NULL(a, "first script name set");
    ASSERT_NOT_NULL(b, "second script name set");
    ASSERT(strcmp(a, "latin") == 0, "first script is latin");
    ASSERT(strcmp(b, "cyrillic") == 0, "crossing script is cyrillic");
}

TEST(rt_unicode_ident_single_script_not_flagged) {
    /// Single-script names -- including ones with digits, underscores,
    /// and combining marks -- are not mixed-script.
    ASSERT(!lush_ident_mixes_scripts("caf\xc3\xa9", NULL, NULL),
           "all-Latin caf\xc3\xa9");
    ASSERT(!lush_ident_mixes_scripts("\xCE\xA3", NULL, NULL),
           "all-Greek \xce\xa3");
    ASSERT(!lush_ident_mixes_scripts("\xD0\xB8\xD0\xBC\xD1\x8F", NULL, NULL),
           "all-Cyrillic \xd0\xb8\xd0\xbc\xd1\x8f");
    ASSERT(!lush_ident_mixes_scripts("x1_y2", NULL, NULL),
           "ASCII with digits/underscore");
    /// NFD cafe-acute (cafe + combining acute): the mark is inherited, not a
    /// second script.
    ASSERT(!lush_ident_mixes_scripts("cafe\xCC\x81", NULL, NULL),
           "NFD caf\xc3\xa9 -- combining mark is inherited, not mixed");
}

TEST(rt_unicode_ident_mixed_script_allowed_by_default) {
    /// FEATURE_REJECT_MIXED_SCRIPT_IDENTS is off in every mode, lush
    /// included, so a mixed-script name is accepted and runs.
    run_result_t r = run_shell("declare p\xD0\xB0sswd=hi\n"
                               "echo \"$p\xD0\xB0sswd\"\n");
    ASSERT_STDOUT_EQ(r, "hi\n");
}

TEST(rt_unicode_ident_mixed_script_rejected_when_opted_in) {
    /// With the hardening flag on, declaring a Latin+Cyrillic name is a
    /// definition-time error. unsetopt at the end so the global feature
    /// override does not leak into later tests.
    run_result_t r = run_shell("setopt reject_mixed_script_idents\n"
                               "declare p\xD0\xB0sswd=hi\n");
    ASSERT_STDERR_CONTAINS(r, "mixes latin and cyrillic");
    (void)run_shell("unsetopt reject_mixed_script_idents\n");
}

TEST(rt_unicode_ident_mixed_script_single_script_ok_when_opted_in) {
    /// The flag rejects only cross-script names; an all-Latin unicode
    /// identifier is still accepted.
    run_result_t r = run_shell("setopt reject_mixed_script_idents\n"
                               "declare caf\xC3\xA9=hi\n"
                               "echo \"$caf\xC3\xA9\"\n");
    ASSERT_STDOUT_EQ(r, "hi\n");
    (void)run_shell("unsetopt reject_mixed_script_idents\n");
}

/// --- Glob expansion in for-loops, arrays, and zsh qualifiers ----------

TEST(rt_glob_for_array_qualifiers) {
    char saved_cwd[4096];
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof saved_cwd), "getcwd");

    char dir[] = "/tmp/lush_glob_test.XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(dir), "mkdtemp");
    ASSERT_EQ(chdir(dir), 0, "chdir into temp dir");

    /// Fixture: three regular files, one subdirectory, one dotfile.
    rt_touch("alpha.txt");
    rt_touch("beta.txt");
    rt_touch("gamma.log");
    mkdir("subdir", 0700);
    rt_touch(".hidden");

    /// for-loop word list globs (previously iterated the literal "*").
    run_result_t r = run_shell("n=0; for f in *; do n=$((n+1)); done; echo $n");
    ASSERT_STDOUT_EQ(r, "4\n");

    /// indexed-array initializer globs.
    r = run_shell("arr=(*.txt); echo ${#arr[@]}");
    ASSERT_STDOUT_EQ(r, "2\n");

    /// zsh (.N) qualifier -- regular files only.
    r = run_shell("arr=(*(.N)); echo ${#arr[@]}");
    ASSERT_STDOUT_EQ(r, "3\n");

    /// zsh (/N) qualifier -- directories only.
    r = run_shell("arr=(*(/N)); echo ${#arr[@]}");
    ASSERT_STDOUT_EQ(r, "1\n");

    /// zsh (DN) qualifier -- include dotfiles.
    r = run_shell("arr=(*(DN)); echo ${#arr[@]}");
    ASSERT_STDOUT_EQ(r, "5\n");

    /// qualifier on a prefixed pattern -- *.txt(N).
    r = run_shell("arr=(*.txt(N)); echo ${#arr[@]}");
    ASSERT_STDOUT_EQ(r, "2\n");

    /// Teardown: remove the fixture while still inside the temp dir.
    unlink("alpha.txt");
    unlink("beta.txt");
    unlink("gamma.log");
    unlink(".hidden");
    rmdir("subdir");
    ASSERT_EQ(chdir(saved_cwd), 0, "restore cwd");
    rmdir(dir);
}

/// --- return / exit propagation out of loops and case arms -------------

TEST(rt_return_from_for_loop) {
    run_result_t r = run_shell(
        "f() { for x in 1 2 3; do if [ \"$x\" = 2 ]; then return 5; fi; "
        "done; return 0; }\nf\necho $?\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "5\n");
}

TEST(rt_return_from_while_loop) {
    run_result_t r =
        run_shell("f() { while true; do return 3; done; }\nf\necho $?\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "3\n");
}

TEST(rt_case_arm_runs_all) {
    /// A case arm is a command list -- a non-zero command must not
    /// short-circuit the rest of the arm.
    run_result_t r =
        run_shell("case x in x) echo one; false; echo two ;; esac\n");
    ASSERT_STDOUT_EQ(r, "one\ntwo\n");
}

TEST(rt_case_arm_return_status) {
    /// posix/101 init-script shape: a function returning non-zero in a
    /// case arm, its status captured by a following command.
    run_result_t r =
        run_shell("do_status() { return 3; }\n"
                  "case status in status) do_status; rc=$?; esac\necho $rc\n");
    ASSERT_EXIT_STATUS(r, 0);
    ASSERT_STDOUT_EQ(r, "3\n");
}

/// --- printf flag forwarding -------------------------------------------

TEST(rt_printf_flags) {
    run_result_t r = run_shell("printf '%05d\\n' 42");
    ASSERT_STDOUT_EQ(r, "00042\n");
    r = run_shell("printf '%+d\\n' 42");
    ASSERT_STDOUT_EQ(r, "+42\n");
    r = run_shell("printf '%#x\\n' 255");
    ASSERT_STDOUT_EQ(r, "0xff\n");
    r = run_shell("printf '[%-5d]\\n' 42");
    ASSERT_STDOUT_EQ(r, "[42   ]\n");
    r = run_shell("printf '% d\\n' 42");
    ASSERT_STDOUT_EQ(r, " 42\n");
}

TEST(rt_echo_xsi_escapes) {
    executor_t *exec = executor_new();
    ASSERT_NOT_NULL(exec, "executor_new failed");

    /// echo interprets the full XSI set in lush mode: \xHH hex, \0NNN octal,
    /// \e -> ESC, and \c terminates (suppressing the trailing newline). A
    /// bare \NNN without the leading 0 stays literal (bash+zsh consensus).
    /// mode-gated so a leaked xpg_echo state from another test cannot flip
    /// echo's escape-interpretation default off.
    run_result_t hex = run_shell_mode_gated(exec, "echo \"\\x41\"\n");
    ASSERT_STDOUT_EQ(hex, "A\n");
    run_result_t oct = run_shell_mode_gated(exec, "echo \"\\0101\"\n");
    ASSERT_STDOUT_EQ(oct, "A\n");
    run_result_t lit = run_shell_mode_gated(exec, "echo \"\\101\"\n");
    ASSERT_STDOUT_EQ(lit, "\\101\n");
    run_result_t term = run_shell_mode_gated(exec, "echo \"a\\cbXY\"\n");
    ASSERT_STDOUT_EQ(term, "a");

    executor_free(exec);
}

TEST(rt_printf_b_conversion) {
    /// %b interprets echo-style escapes in the argument (was unimplemented and
    /// printed the literal "%b"): \xHH, \0NNN octal, and \c terminates.
    run_result_t r = run_shell("printf '%b' '\\x41\\0102'\n");
    ASSERT_STDOUT_EQ(r, "AB");
    run_result_t term = run_shell("printf '%b more' 'a\\cbX'\n");
    ASSERT_STDOUT_EQ(term, "a");
}

TEST(rt_printf_format_octal_and_no_reparse) {
    /// printf format uses \NNN octal (no leading 0 required); an octal escape
    /// that produces a '%' byte is emitted literally, never re-parsed as a
    /// conversion specifier.
    run_result_t oct = run_shell("printf '\\101\\n'\n");
    ASSERT_STDOUT_EQ(oct, "A\n");
    run_result_t pct = run_shell("printf '\\045d\\n'\n");
    ASSERT_STDOUT_EQ(pct, "%d\n");
}

/// --- brace expansion --------------------------------------------------

TEST(rt_brace_nested) {
    run_result_t r = run_shell("echo {{1..3},{a..c}}");
    ASSERT_STDOUT_EQ(r, "1 2 3 a b c\n");
}

TEST(rt_brace_cartesian) {
    run_result_t r = run_shell("echo file{1..2}.{log,txt}");
    ASSERT_STDOUT_EQ(r, "file1.log file1.txt file2.log file2.txt\n");
}

TEST(rt_brace_in_array_init) {
    run_result_t r =
        run_shell("arr=(x{1,2,3}y); echo ${#arr[@]} ${arr[0]} ${arr[2]}");
    ASSERT_STDOUT_EQ(r, "3 x1y x3y\n");
}

/// --- parameter expansion operators on array elements ------------------

TEST(rt_array_elem_default_missing) {
    run_result_t r =
        run_shell("declare -A m; m[a]=1; echo \"[${m[b]:-fallback}]\"");
    ASSERT_STDOUT_EQ(r, "[fallback]\n");
}

TEST(rt_array_elem_default_present) {
    run_result_t r =
        run_shell("declare -A m; m[a]=hi; echo \"[${m[a]:-fallback}]\"");
    ASSERT_STDOUT_EQ(r, "[hi]\n");
}

TEST(rt_array_elem_alt_present) {
    run_result_t r =
        run_shell("declare -A m; m[a]=hi; echo \"[${m[a]:+set}]\"");
    ASSERT_STDOUT_EQ(r, "[set]\n");
}

TEST(rt_array_elem_alt_missing) {
    run_result_t r =
        run_shell("declare -A m; m[a]=hi; echo \"[${m[b]:+set}]\"");
    ASSERT_STDOUT_EQ(r, "[]\n");
}

/// The full parameter-operator engine applies to a single element, not
/// only :- / :+ (issue #514). Prior behavior dropped every other
/// operator and returned the raw element value.

TEST(rt_array_elem_strip_prefix_longest) {
    run_result_t r = run_shell(
        "declare -A m; m[name]=HelloWorld; echo \"[${m[name]##He}]\"");
    ASSERT_STDOUT_EQ(r, "[lloWorld]\n");
}

TEST(rt_array_elem_strip_suffix) {
    run_result_t r = run_shell(
        "declare -A m; m[name]=HelloWorld; echo \"[${m[name]%rld}]\"");
    ASSERT_STDOUT_EQ(r, "[HelloWo]\n");
}

TEST(rt_array_elem_upper_all) {
    run_result_t r =
        run_shell("declare -A m; m[name]=HelloWorld; echo \"[${m[name]^^}]\"");
    ASSERT_STDOUT_EQ(r, "[HELLOWORLD]\n");
}

TEST(rt_array_elem_lower_all) {
    run_result_t r =
        run_shell("declare -A m; m[name]=HelloWorld; echo \"[${m[name],,}]\"");
    ASSERT_STDOUT_EQ(r, "[helloworld]\n");
}

TEST(rt_array_elem_replace_all) {
    run_result_t r = run_shell(
        "declare -A m; m[path]=a/b/c/d; echo \"[${m[path]//\\//.}]\"");
    ASSERT_STDOUT_EQ(r, "[a.b.c.d]\n");
}

TEST(rt_array_elem_substring) {
    run_result_t r = run_shell(
        "declare -A m; m[name]=HelloWorld; echo \"[${m[name]:2:3}]\"");
    ASSERT_STDOUT_EQ(r, "[llo]\n");
}

TEST(rt_array_elem_transform_quote) {
    run_result_t r =
        run_shell("declare -A m; m[name]=HelloWorld; echo \"[${m[name]@Q}]\"");
    ASSERT_STDOUT_EQ(r, "['HelloWorld']\n");
}

TEST(rt_array_elem_indexed_upper) {
    run_result_t r =
        run_shell("arr=(alpha beta gamma); echo \"[${arr[1]^^}]\"");
    ASSERT_STDOUT_EQ(r, "[BETA]\n");
}

TEST(rt_array_elem_assign_persists) {
    /// ${m[new]:=fresh} both yields and PERSISTS to the element.
    run_result_t r = run_shell("declare -A m; echo \"[${m[new]:=fresh}]\"; "
                               "echo \"[${m[new]}]\"");
    ASSERT_STDOUT_EQ(r, "[fresh]\n[fresh]\n");
}

TEST(rt_array_elem_assign_readonly_refused) {
    /// := on a readonly array must NOT mutate it: the low-level element
    /// setters bypass readonly enforcement, so the operator path guards it.
    run_result_t r =
        run_shell("declare -A m; readonly m; : \"${m[new]:=CHANGED}\""
                  "; echo \"[${m[new]}]\"");
    ASSERT_STDOUT_EQ(r, "[]\n");
}

/// Operators on an element of an entirely UNDECLARED array name (no prior
/// declare/assignment) must apply, not be dropped -- the same operator engine
/// the declared path uses (issue #527). Unique names for the persisting `:=`
/// cases so the shared global symbol table does not leak an array binding.

TEST(rt_undecl_elem_default) {
    run_result_t r = run_shell("echo \"[${pr527_a[0]:-fallback}]\"");
    ASSERT_STDOUT_EQ(r, "[fallback]\n");
}

TEST(rt_undecl_elem_alt_empty) {
    /// :+ on an unset element yields empty (the element is unset).
    run_result_t r = run_shell("echo \"[${pr527_b[0]:+alt}]\"");
    ASSERT_STDOUT_EQ(r, "[]\n");
}

TEST(rt_undecl_elem_strip_prefix_empty) {
    /// A pattern operator on an unset element yields empty.
    run_result_t r = run_shell("echo \"[${pr527_c[0]#x}]\"");
    ASSERT_STDOUT_EQ(r, "[]\n");
}

TEST(rt_undecl_elem_high_index_default) {
    run_result_t r = run_shell("echo \"[${pr527_f[7]:-x}]\"");
    ASSERT_STDOUT_EQ(r, "[x]\n");
}

TEST(rt_undecl_elem_assign_creates_array) {
    /// := yields the default AND creates the array with the element set.
    run_result_t r = run_shell("echo \"[${pr527_d[0]:=made}]\"; "
                               "echo \"[${pr527_d[0]}] n=${#pr527_d[@]}\"");
    ASSERT_STDOUT_EQ(r, "[made]\n[made] n=1\n");
}

TEST(rt_undecl_elem_assign_at_index) {
    /// := at a non-zero index creates the array with that element populated.
    run_result_t r = run_shell("echo \"[${pr527_g[2]:=z}]\"; "
                               "echo \"e2=[${pr527_g[2]}] n=${#pr527_g[@]}\"");
    ASSERT_STDOUT_EQ(r, "[z]\ne2=[z] n=1\n");
}

TEST(rt_scalar_slice_operator_out_of_range) {
    /// A grapheme slice past the end of a scalar is empty, so :- fires --
    /// the scalar-slice result feeds the same operator engine.
    run_result_t r = run_shell("str=hi; echo \"[${str[9]:-x}]\"");
    ASSERT_STDOUT_EQ(r, "[x]\n");
}

TEST(rt_scalar_slice_still_works) {
    /// Regression: the grapheme-slice fallback for a scalar name is unchanged
    /// by the operator unification -- ${str[N]} / ${str[N,M]} still slice.
    run_result_t r = run_shell("str=hello; echo \"[${str[0]}][${str[1,3]}]"
                               "[${str[-1]}]\"");
    ASSERT_STDOUT_EQ(r, "[h][ell][o]\n");
}

TEST(rt_array_elem_assign_zsh_index0_skipped) {
    /// zsh 1-based mode: index 0 is invalid; ${arr[0]:=Z} must not clobber
    /// physical index 0 (which is arr[1] there).
    run_result_t r = run_shell("mode zsh\narr=(a b c)\n: \"${arr[0]:=Z}\"\n"
                               "echo \"[${arr[1]}]\"\n");
    run_shell("mode lush\n");
    ASSERT_STDOUT_EQ(r, "[a]\n");
}

/// --- POSIX null-word removal (#517): an unquoted expansion that yields
/// the empty string contributes zero words; a quoted empty stays one word.

TEST(rt_nullword_empty_command) {
    /// An unquoted empty command name is a null command: exit 0, not 127.
    run_result_t r = run_shell("x=\"\"; $x; echo \"done=$?\"");
    ASSERT_STDOUT_EQ(r, "done=0\n");
}

TEST(rt_nullword_unset_command) {
    run_result_t r = run_shell("unset q; $q; echo \"done=$?\"");
    ASSERT_STDOUT_EQ(r, "done=0\n");
}

TEST(rt_nullword_quoted_empty_command_kept) {
    /// A quoted empty command name stays one empty word: command not found.
    run_result_t r = run_shell("x=\"\"; \"$x\"; echo \"done=$?\"");
    ASSERT_STDOUT_EQ(r, "done=127\n");
}

TEST(rt_nullword_command_promotion) {
    /// Dropping an empty command name promotes the first surviving word.
    run_result_t r = run_shell("x=\"\"; $x echo hi");
    ASSERT_STDOUT_EQ(r, "hi\n");
}

TEST(rt_nullword_arg_removed) {
    run_result_t r = run_shell("c(){ echo \"n=$#\"; }; x=\"\"; c $x");
    ASSERT_STDOUT_EQ(r, "n=0\n");
}

TEST(rt_nullword_quoted_arg_kept) {
    run_result_t r = run_shell("c(){ echo \"n=$#\"; }; x=\"\"; c \"$x\" $x");
    ASSERT_STDOUT_EQ(r, "n=1\n");
}

TEST(rt_nullword_partial_word_kept) {
    /// A fused word is not empty, so it is kept: a${x}b -> ab.
    run_result_t r = run_shell("c(){ echo \"n=$# [$1]\"; }; x=\"\"; c a${x}b");
    ASSERT_STDOUT_EQ(r, "n=1 [ab]\n");
}

TEST(rt_nullword_no_split_curation_preserved) {
    /// lush's no-word-split curation is unaffected: a non-empty value stays
    /// one word even unquoted (the predicate keys on the empty string only).
    run_result_t r = run_shell("c(){ echo \"n=$#\"; }; x=\"a b c\"; c $x");
    ASSERT_STDOUT_EQ(r, "n=1\n");
}

TEST(rt_nullword_for_zero_iterations) {
    run_result_t r =
        run_shell("x=\"\"; for i in $x; do echo hit; done; echo end");
    ASSERT_STDOUT_EQ(r, "end\n");
}

TEST(rt_nullword_for_quoted_empty_one_iter) {
    run_result_t r = run_shell("x=\"\"; for i in \"$x\"; do echo hit; done");
    ASSERT_STDOUT_EQ(r, "hit\n");
}

TEST(rt_nullword_array_zero_elements) {
    run_result_t r = run_shell("x=\"\"; arr=($x); echo \"${#arr[@]}\"");
    ASSERT_STDOUT_EQ(r, "0\n");
}

TEST(rt_nullword_array_mixed_kept) {
    run_result_t r = run_shell("arr=(\"\" b); echo \"${#arr[@]}\"");
    ASSERT_STDOUT_EQ(r, "2\n");
}

TEST(rt_nullword_in_function_body) {
    /// name_quoted must survive the function-body deep copy (#488 lesson):
    /// an unquoted empty command inside a function is still a null command.
    run_result_t r = run_shell("f(){ x=\"\"; $x; echo \"r=$?\"; }; f");
    ASSERT_STDOUT_EQ(r, "r=0\n");
}

/// --- quoted "$*" / "${a[*]}" join on the first char of IFS (#518) ------
/// A quoted star expansion is ONE word; the elements are joined by IFS[0].
/// Unquoted $* / ${a[*]} and every @ form are unaffected.

TEST(rt_star_join_is_one_word) {
    run_result_t r = run_shell("c(){ echo \"n=$#\"; }; set -- a b c; c \"$*\"");
    ASSERT_STDOUT_EQ(r, "n=1\n");
}

TEST(rt_star_join_default_ifs_space) {
    run_result_t r = run_shell("c(){ echo \"[$1]\"; }; set -- a b c; c \"$*\"");
    ASSERT_STDOUT_EQ(r, "[a b c]\n");
}

TEST(rt_star_join_custom_ifs) {
    run_result_t r = run_shell("IFS=,; set -- a b c; v=\"$*\"; echo \"[$v]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[a,b,c]\n");
}

TEST(rt_star_join_empty_ifs_concatenates) {
    run_result_t r = run_shell("IFS=; set -- a b c; v=\"$*\"; echo \"[$v]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[abc]\n");
}

TEST(rt_star_join_array_custom_ifs) {
    run_result_t r = run_shell("IFS=,; a=(a b c); echo \"[${a[*]}]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[a,b,c]\n");
}

TEST(rt_star_join_braced_positional) {
    run_result_t r = run_shell("IFS=,; set -- a b c; echo \"[${*}]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[a,b,c]\n");
}

TEST(rt_braced_at_function_scope) {
    /// #534: braced ${@} in a scalar slot must reach a function's local
    /// positional parameters, not the global argv (which is empty here).
    run_result_t r = run_shell("f(){ v=\"${@}\"; echo \"[$v]\"; }; f x y z\n");
    ASSERT_STDOUT_EQ(r, "[x y z]\n");
}

TEST(rt_braced_star_function_scope) {
    /// The braced ${*} peer, likewise scope-aware.
    run_result_t r = run_shell("f(){ v=\"${*}\"; echo \"[$v]\"; }; f x y z\n");
    ASSERT_STDOUT_EQ(r, "[x y z]\n");
}

TEST(rt_braced_at_function_scope_empty) {
    /// No positionals -> the empty string, matching bash.
    run_result_t r = run_shell("f(){ v=\"${@}\"; echo \"[$v]\"; }; f\n");
    ASSERT_STDOUT_EQ(r, "[]\n");
}

TEST(rt_braced_at_function_scope_nested) {
    /// Each frame resolves its own positionals; the inner frame does not
    /// leak the outer frame's arguments.
    run_result_t r = run_shell("g(){ echo \"g=[${@}]\"; }; f(){ g \"$@\"; "
                               "echo \"f=[${@}]\"; }; f a b\n");
    ASSERT_STDOUT_EQ(r, "g=[a b]\nf=[a b]\n");
}

TEST(rt_braced_at_function_scope_ifs_curated) {
    /// Scope-aware ${@} keeps the curated lush $@-scalar join on IFS[0]
    /// (the same behavior global-scope ${@} already has), so a function's
    /// braced ${@} under IFS=, joins on the comma.
    run_result_t r =
        run_shell("f(){ v=\"${@}\"; echo \"[$v]\"; }; IFS=,; f x y z\n");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[x,y,z]\n");
}

TEST(rt_braced_at_function_scope_vector_still_spreads) {
    /// Regression: the whole-word "${@}" vector form still spreads to N
    /// words in function scope (handled by the vector path, unchanged).
    run_result_t r = run_shell(
        "f(){ for w in \"${@}\"; do printf '<%s>' \"$w\"; done; echo; }; "
        "f a b c\n");
    ASSERT_STDOUT_EQ(r, "<a><b><c>\n");
}

TEST(rt_star_join_empty_is_one_word) {
    /// A quoted empty "$*" (no positionals) stays one empty word.
    run_result_t r = run_shell("c(){ echo \"n=$#\"; }; set --; c \"$*\"");
    ASSERT_STDOUT_EQ(r, "n=1\n");
}

TEST(rt_star_at_still_explodes) {
    /// Regression: "$@" keeps N separate words; IFS join is star-only.
    run_result_t r =
        run_shell("c(){ echo \"n=$#\"; }; IFS=,; set -- a b c; c \"$@\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "n=3\n");
}

TEST(rt_star_array_at_still_explodes) {
    run_result_t r =
        run_shell("c(){ echo \"n=$#\"; }; IFS=,; a=(a b c); c \"${a[@]}\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "n=3\n");
}

TEST(rt_star_unquoted_not_joined) {
    /// Regression: unquoted $* keeps the no-split explode (lush curation).
    run_result_t r = run_shell("c(){ echo \"n=$#\"; }; set -- a b c; c $*");
    ASSERT_STDOUT_EQ(r, "n=3\n");
}

/// --- array element slice ${a[*]:off:len} (#530) -----------------------
/// The numeric element slice must not be re-applied as a byte substring
/// operator (the double-slice bug); the :- / :+ operators must NOT be
/// mistaken for slices.

TEST(rt_slice_star_basic) {
    run_result_t r = run_shell("a=(w x y z); echo \"[${a[*]:0:2}]\"");
    ASSERT_STDOUT_EQ(r, "[w x]\n");
}

TEST(rt_slice_star_offset) {
    run_result_t r = run_shell("a=(w x y z); echo \"[${a[*]:1:2}]\"");
    ASSERT_STDOUT_EQ(r, "[x y]\n");
}

TEST(rt_slice_star_to_end) {
    run_result_t r = run_shell("a=(w x y z); echo \"[${a[*]:1}]\"");
    ASSERT_STDOUT_EQ(r, "[x y z]\n");
}

TEST(rt_slice_star_custom_ifs) {
    run_result_t r = run_shell("IFS=,; a=(w x y z); echo \"[${a[*]:1:2}]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[x,y]\n");
}

TEST(rt_slice_star_default_op_not_slice) {
    /// :- on a set array is the default operator (array is non-empty), NOT
    /// a slice -- it must yield the joined value, not a truncation.
    run_result_t r = run_shell("a=(w x y); echo \"[${a[*]:-def}]\"");
    ASSERT_STDOUT_EQ(r, "[w x y]\n");
}

TEST(rt_slice_star_alt_op_not_slice) {
    run_result_t r = run_shell("a=(w x y); echo \"[${a[*]:+alt}]\"");
    ASSERT_STDOUT_EQ(r, "[alt]\n");
}

TEST(rt_slice_star_plus_numeric_op) {
    /// ${a[*]:+2} is the :+ operator with a NUMERIC operand, not a slice --
    /// strtol must not consume the '+' sign and mis-read it as an offset.
    run_result_t r = run_shell("a=(w x y); echo \"[${a[*]:+2}]\"");
    ASSERT_STDOUT_EQ(r, "[2]\n");
}

TEST(rt_slice_star_minus_numeric_op) {
    /// ${a[*]:-2} on a set array is the :- default operator (array is
    /// non-empty -> the joined value), not a slice from offset -2.
    run_result_t r = run_shell("a=(w x y); echo \"[${a[*]:-2}]\"");
    ASSERT_STDOUT_EQ(r, "[w x y]\n");
}

TEST(rt_slice_at_numeric_still_explodes) {
    /// A numeric slice on the @ vector presentation still explodes to N
    /// words (it is vector-producing), unaffected by the operator guard.
    run_result_t r =
        run_shell("a=(w x y z); c(){ echo \"n=$#\"; }; c \"${a[@]:1:2}\"");
    ASSERT_STDOUT_EQ(r, "n=2\n");
}

TEST(rt_slice_at_scalar_op_is_type_error) {
    /// A scalar-producing operator (:+) on the @ vector presentation is a
    /// SEMANTICS 3.9 type mismatch (@ = vector; use ${a[*]:+word} for the
    /// scalar form) -- not a slice, not a silent flatten. No output.
    run_result_t r = run_shell("a=(w x y); echo \"[${a[@]:+alt}]\"");
    ASSERT_STDOUT_EQ(r, "");
}

/// --- positional vector in command position (#529) ---------------------
/// A positional-parameter vector as the command name expands: first word is
/// the command, the rest are arguments. A named array keeps the SEMANTICS
/// section 3.9 type error.

TEST(rt_cmdvec_positional_at_expands) {
    run_result_t r = run_shell("set -- echo hi; \"$@\"");
    ASSERT_STDOUT_EQ(r, "hi\n");
}

TEST(rt_cmdvec_positional_at_unquoted_expands) {
    run_result_t r = run_shell("set -- echo hi; $@");
    ASSERT_STDOUT_EQ(r, "hi\n");
}

TEST(rt_cmdvec_multiword_boundaries) {
    run_result_t r = run_shell("set -- echo A B; \"$@\"");
    ASSERT_STDOUT_EQ(r, "A B\n");
}

TEST(rt_cmdvec_braced_positional_expands) {
    run_result_t r = run_shell("set -- echo hi; \"${@}\"");
    ASSERT_STDOUT_EQ(r, "hi\n");
}

TEST(rt_cmdvec_empty_at_is_null_command) {
    /// Empty "$@" as a command is a null command: exit 0, not 127.
    run_result_t r = run_shell("set --; \"$@\"; echo \"rc=$?\"");
    ASSERT_STDOUT_EQ(r, "rc=0\n");
}

TEST(rt_cmdvec_star_joins_one_word) {
    /// "$*" joins to a single command name ("echo hi") -> not found (127).
    run_result_t r = run_shell("set -- echo hi; \"$*\"; echo \"rc=$?\"");
    ASSERT_STDOUT_EQ(r, "rc=127\n");
}

TEST(rt_cmdvec_named_array_keeps_type_error) {
    /// A named array as a command name keeps the SEMANTICS 3.9 type error
    /// (does not vector-expand as a command); no "hi" is printed.
    run_result_t r = run_shell("a=(echo hi); \"${a[@]}\"");
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rt_cmdvec_flag_form_not_spread) {
    /// A zsh parameter-flag vector form as a command name must NOT be
    /// spread as a command (it is a named list, not a positional vector);
    /// it joins to one word and is looked up whole, so no "hi" is printed.
    run_result_t r = run_shell("a=(echo hi); \"${(v)a}\"");
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rt_cmdvec_normal_command_unchanged) {
    run_result_t r = run_shell("echo hi");
    ASSERT_STDOUT_EQ(r, "hi\n");
}

/// --- scalar "$@" and keys join on the first char of IFS ---------------
/// The scalar $@ join and the *-keys join use IFS[0] (matching zsh/dash);
/// the @-keys form in a scalar slot is a type error (SEMANTICS 3.9).

TEST(rt_at_scalar_join_custom_ifs) {
    run_result_t r = run_shell("IFS=,; set -- x y z; v=\"$@\"; echo \"[$v]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[x,y,z]\n");
}

TEST(rt_at_scalar_join_empty_ifs) {
    run_result_t r = run_shell("IFS=; set -- x y z; v=\"$@\"; echo \"[$v]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[xyz]\n");
}

TEST(rt_at_scalar_join_default_ifs) {
    run_result_t r =
        run_shell("unset IFS; set -- x y z; v=\"$@\"; echo \"[$v]\"");
    ASSERT_STDOUT_EQ(r, "[x y z]\n");
}

TEST(rt_at_scalar_join_function_scope) {
    run_result_t r =
        run_shell("f(){ v=\"$@\"; echo \"[$v]\"; }; IFS=,; f x y z");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[x,y,z]\n");
}

TEST(rt_keys_star_join_custom_ifs) {
    run_result_t r = run_shell("IFS=,; a=(x y z); echo \"[${!a[*]}]\"");
    run_shell("unset IFS\n");
    ASSERT_STDOUT_EQ(r, "[0,1,2]\n");
}

TEST(rt_keys_at_scalar_slot_type_error) {
    /// @-keys in a scalar slot is a list value in a scalar position: a type
    /// error (SEMANTICS 3.9), not a join; no keys are printed.
    run_result_t r = run_shell("a=(x y z); echo \"[${!a[@]}]\"");
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rt_keys_at_whole_word_explodes) {
    /// Regression: whole-word "${!a[@]}" still explodes the keys as a
    /// vector (unchanged; handled by the vector path).
    run_result_t r =
        run_shell("a=(x y z); c(){ echo \"n=$#\"; }; c \"${!a[@]}\"");
    ASSERT_STDOUT_EQ(r, "n=3\n");
}

/// --- POSIX character classes in pattern matching ----------------------

TEST(rt_char_class_space) {
    /// [[:space:]] non-negated class -- strips trailing whitespace.
    run_result_t r = run_shell("s='a   '; echo \"[${s%%[[:space:]]*}]\"");
    ASSERT_STDOUT_EQ(r, "[a]\n");
}

TEST(rt_char_class_negated) {
    /// [![:space:]] -- the building block of the whitespace-trim idiom.
    run_result_t r = run_shell("s='  hi'; echo \"[${s%%[![:space:]]*}]\"");
    ASSERT_STDOUT_EQ(r, "[  ]\n");
}

TEST(rt_char_class_digit) {
    /// [[:digit:]] in a parameter-expansion suffix-removal pattern.
    run_result_t r = run_shell("s=abc123; echo \"[${s%%[[:digit:]]*}]\"");
    ASSERT_STDOUT_EQ(r, "[abc]\n");
}

TEST(rt_char_class_upper) {
    /// [![:upper:]] negated class.
    run_result_t r = run_shell("s=ABCdef; echo \"[${s%%[![:upper:]]*}]\"");
    ASSERT_STDOUT_EQ(r, "[ABC]\n");
}

/// --- typed-function form ---------------------------------------------

TEST(rt_typed_fn_scalar_return) {
    run_result_t r = run_shell("fn answer() -> scalar { return \"42\"; }\n"
                               "let r = answer()\n"
                               "echo \"[$r]\"\n");
    ASSERT_STDOUT_EQ(r, "[42]\n");
}

TEST(rt_typed_fn_scalar_param) {
    run_result_t r =
        run_shell("fn shout(s: scalar) -> scalar { return \"$s!\"; }\n"
                  "let r = shout(\"hi\")\n"
                  "echo \"[$r]\"\n");
    ASSERT_STDOUT_EQ(r, "[hi!]\n");
}

TEST(rt_typed_fn_list_param) {
    run_result_t r = run_shell("fn first(xs: list) -> scalar { return "
                               "\"${xs[0]}\"; }\n"
                               "arr=(alpha beta gamma)\n"
                               "let r = first($arr)\n"
                               "echo \"[$r]\"\n");
    ASSERT_STDOUT_EQ(r, "[alpha]\n");
}

TEST(rt_typed_fn_arity_mismatch) {
    run_result_t r =
        run_shell("fn one(a: scalar) -> scalar { return \"ok\"; }\n"
                  "let r = one()\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_typed_fn_kind_mismatch_arg) {
    run_result_t r = run_shell("fn ws(s: scalar) -> scalar { return $s; }\n"
                               "arr=(a b c)\n"
                               "let r = ws($arr)\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_typed_fn_void_called_via_let) {
    run_result_t r = run_shell("fn nop() { :; }\n"
                               "let r = nop()\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_typed_fn_void_no_return) {
    /// A typed fn lives in its own namespace, distinct from POSIX functions,
    /// so a bare call site does not resolve to it: invoking `nop` by name is
    /// command-not-found (127). Typed call sites use the parens form, which
    /// for a void function is rejected by rt_typed_fn_void_called_via_let.
    run_result_t r = run_shell("fn nop() { echo hello; }\n"
                               "nop\n");
    ASSERT_EXIT_STATUS(r, 127);
}

/// --- parameter-expansion catalog --------------------------------------
/// Operators have a defined input-kind to output-kind contract. When the
/// kinds disagree the engine raises a structured runtime type-mismatch
/// rather than silently no-op'ing or producing wrong output. These tests
/// pin the cells of that catalog.

TEST(rt_pe_catalog_join_flag_on_list) {
    /// Explicit list-to-scalar join with custom separator. Previously
    /// over-rejected by the bare-${arr[@]} list-in-scalar-slot check;
    /// (j:X:) IS the explicit join, so the runtime allows it.
    run_result_t r = run_shell("arr=(alpha beta gamma)\n"
                               "echo \"[${(j:+:)arr[@]}]\"\n"
                               "echo \"[${(j:+:)arr}]\"\n");
    ASSERT_STDOUT_EQ(r, "[alpha+beta+gamma]\n[alpha+beta+gamma]\n");
}

TEST(rt_pe_catalog_case_mod_flag_per_element) {
    /// (U) flag on a list per-elements; the result is the elements
    /// uppercased and space-joined.
    run_result_t r = run_shell("arr=(alpha beta)\n"
                               "echo \"[${(U)arr}]\"\n");
    ASSERT_STDOUT_EQ(r, "[ALPHA BETA]\n");
}

TEST(rt_pe_catalog_sort_flag_on_list) {
    run_result_t r = run_shell("arr=(c a b)\n"
                               "echo \"[${(o)arr}]\"\n");
    ASSERT_STDOUT_EQ(r, "[a b c]\n");
}

TEST(rt_pe_catalog_keys_flag_on_scalar) {
    /// Collection-only flag (k) applied to a scalar -> type mismatch.
    run_result_t r = run_shell("s=hello\n"
                               "echo \"[${(k)s}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_values_flag_on_scalar) {
    run_result_t r = run_shell("s=hello\n"
                               "echo \"[${(v)s}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_kv_flag_on_scalar) {
    run_result_t r = run_shell("s=hello\n"
                               "echo \"[${(kv)s}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_join_flag_on_scalar) {
    /// (j:X:) on a non-collection is meaningless: there is nothing to
    /// join. Type mismatch.
    run_result_t r = run_shell("s=hello\n"
                               "echo \"[${(j:+:)s}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_conditional_on_bare_list) {
    /// ${list:-default} silently collapses to the first-element-default
    /// form in legacy shells. lush rejects it; the user picks slice for
    /// a scalar element fallback, or assigns a list literal for a
    /// list-shaped fallback.
    run_result_t r = run_shell("arr=(a b c)\n"
                               "echo \"[${arr:-default}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_case_mod_on_bare_list) {
    /// ${arr^^} on a bare list name -- scalar operator on collection.
    /// The user must vectorize explicitly via ${arr[@]^^}.
    run_result_t r = run_shell("arr=(hello world)\n"
                               "echo \"[${arr^^}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_pattern_strip_on_bare_list) {
    run_result_t r = run_shell("arr=(file.txt other.log)\n"
                               "echo \"[${arr##*.}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_substring_on_bare_list) {
    run_result_t r = run_shell("arr=(alpha beta)\n"
                               "echo \"[${arr:0:2}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_at_transform_on_bare_list) {
    /// ${arr@Q} on bare list -- the @-transform is scalar-shaped on a
    /// bare name. Vectorize via ${arr[@]@Q}.
    run_result_t r = run_shell("arr=(a b c)\n"
                               "echo \"[${arr@Q}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_at_attr_query_on_map) {
    /// ${map@a} is the kind-agnostic attribute query: it reports the
    /// declared-attribute letters and must work on a collection, unlike
    /// the value-shaped @ transforms above which require [@] (#121).
    run_result_t r = run_shell("declare -A m=([x]=1)\n"
                               "echo \"[${m@a}]\"\n");
    ASSERT_STDOUT_EQ(r, "[A]\n");
}

TEST(rt_pe_at_attr_query_on_list) {
    run_result_t r = run_shell("declare -a idx=(a b c)\n"
                               "echo \"[${idx@a}]\"\n");
    ASSERT_STDOUT_EQ(r, "[a]\n");
}

TEST(rt_pe_at_attr_query_scalar_unaffected) {
    /// The bare-name guard exception is limited to @a; the scalar
    /// attribute paths are unchanged.
    run_result_t r = run_shell("declare -i n=5\n"
                               "declare -r ro=locked\n"
                               "echo \"[${n@a}][${ro@a}]\"\n");
    ASSERT_STDOUT_EQ(r, "[i][r]\n");
}

TEST(rt_declare_combined_flags_attributes) {
    /// declare with combined attribute flags including -i must record
    /// every attribute, not just integer: ${var@a} reflects all of them
    /// (issue #123). The integer branch previously dropped export and the
    /// other attributes.
    run_result_t r = run_shell("declare -ix ex=3\n"
                               "declare -xi xe=4\n"
                               "declare -ir ir=5\n"
                               "echo \"[${ex@a}][${xe@a}][${ir@a}]\"\n");
    ASSERT_STDOUT_EQ(r, "[ix][ix][ir]\n");
}

TEST(rt_lle_feature_bridge_enumerates_matrix) {
    /// setopt/unsetopt completion enumerates the feature matrix through
    /// this bridge instead of a hand-maintained list, so it can never
    /// drift (issue #126). Names the old static completion list omitted
    /// must be reachable through the bridge.
    extern int lle_shell_feature_count(void);
    extern const char *lle_shell_feature_name(int index);
    int n = lle_shell_feature_count();
    ASSERT_TRUE(n > 30, "feature matrix should expose many features");
    bool found_errexit = false;
    bool found_xpg = false;
    for (int i = 0; i < n; i++) {
        const char *nm = lle_shell_feature_name(i);
        if (nm && strcmp(nm, "errexit_in_loops") == 0) {
            found_errexit = true;
        }
        if (nm && strcmp(nm, "xpg_echo") == 0) {
            found_xpg = true;
        }
    }
    ASSERT_TRUE(found_errexit,
                "errexit_in_loops must be enumerable via the bridge");
    ASSERT_TRUE(found_xpg, "xpg_echo must be enumerable via the bridge");
}

TEST(rt_setopt_completion_lists_matrix_features) {
    /// End-to-end: setopt completion routes through the bridge and offers
    /// names from the live feature matrix, including ones the old static
    /// completion list omitted (issue #126). Drives the public builtin
    /// completion entry with a minimal "setopt err" context.
    lle_memory_pool_t *pool = (lle_memory_pool_t *)1; /// LLE pool sentinel
    lle_completion_result_t *result = NULL;
    lle_result_t r = lle_completion_result_create(pool, 8, &result);
    ASSERT_TRUE(r == LLE_SUCCESS, "completion result create");

    lle_word_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.command_name = "setopt";
    ctx.dequoted_filename_prefix = (char *)"err";

    r = lle_builtin_completions_generate(pool, &ctx, result);
    ASSERT_TRUE(r == LLE_SUCCESS, "builtin completions generate");

    bool found = false;
    for (size_t i = 0; i < result->count; i++) {
        if (result->items[i].text &&
            strcmp(result->items[i].text, "errexit_in_loops") == 0) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found, "setopt completion must offer errexit_in_loops");

    lle_completion_result_free(result);
}

TEST(rt_pe_at_value_transform_on_map_still_errors) {
    /// Only @a bypasses the guard; a value-shaped @ transform on a bare
    /// collection remains a type error (needs [@] to vectorize).
    run_result_t r = run_shell("declare -A m=([x]=1)\n"
                               "echo \"[${m@U}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_declare_p_named_assoc_prints_elements) {
    /// `declare -p NAME` for an associative array prints the full array with
    /// its elements in insertion order, the same as the no-argument listing
    /// (and as bash does), not just `declare -A NAME`. (Regression.)
    run_result_t r = run_shell("declare -A m=([zebra]=1 [apple]=2)\n"
                               "declare -p m\n");
    ASSERT_STDOUT_EQ(r, "declare -A m=([zebra]=\"1\" [apple]=\"2\")\n");
}

TEST(rt_declare_p_named_indexed_prints_elements) {
    /// `declare -p NAME` for an indexed array prints its elements too.
    run_result_t r = run_shell("declare -a a=(x y z)\n"
                               "declare -p a\n");
    ASSERT_STDOUT_EQ(r, "declare -a a=([0]=\"x\" [1]=\"y\" [2]=\"z\")\n");
}

TEST(rt_pe_catalog_conditional_on_map) {
    run_result_t r = run_shell("declare -A m=([a]=1 [b]=2)\n"
                               "echo \"[${m:-default}]\"\n");
    ASSERT_EXIT_STATUS(r, 1);
}

TEST(rt_pe_catalog_scalar_slice_still_works) {
    /// Regression: ${arr[0]:-default} is the scalar-element form and
    /// remains valid -- the slice produces a scalar and the
    /// conditional operates on that scalar.
    run_result_t r = run_shell("arr=(a b c)\n"
                               "echo \"[${arr[0]:-default}]\"\n");
    ASSERT_STDOUT_EQ(r, "[a]\n");
}

TEST(rt_pe_catalog_per_element_case_mod_via_slice) {
    run_result_t r = run_shell("arr=(hello world)\n"
                               "echo \"[${arr[@]^^}]\"\n");
    ASSERT_STDOUT_EQ(r, "[HELLO WORLD]\n");
}

TEST(rt_pe_catalog_per_element_prefix_strip_via_slice) {
    run_result_t r = run_shell("arr=(file.txt other.log)\n"
                               "echo \"[${arr[@]##*.}]\"\n");
    ASSERT_STDOUT_EQ(r, "[txt log]\n");
}

TEST(rt_pe_catalog_per_element_suffix_strip_via_slice) {
    run_result_t r = run_shell("arr=(file.txt other.log)\n"
                               "echo \"[${arr[@]%%.*}]\"\n");
    ASSERT_STDOUT_EQ(r, "[file other]\n");
}

TEST(rt_pe_catalog_per_element_replace_first_via_slice) {
    run_result_t r = run_shell("arr=(hello world)\n"
                               "echo \"[${arr[@]/l/L}]\"\n");
    ASSERT_STDOUT_EQ(r, "[heLlo worLd]\n");
}

TEST(rt_pe_catalog_per_element_replace_all_via_slice) {
    run_result_t r = run_shell("arr=(hello world)\n"
                               "echo \"[${arr[@]//l/L}]\"\n");
    ASSERT_STDOUT_EQ(r, "[heLLo worLd]\n");
}

TEST(rt_pe_catalog_per_element_at_q_via_slice) {
    run_result_t r = run_shell("arr=(hello world)\n"
                               "echo \"[${arr[@]@Q}]\"\n");
    ASSERT_STDOUT_EQ(r, "['hello' 'world']\n");
}

TEST(rt_pe_vectorized_case_transform) {
    /// ${arr[@]@U}/@u/@L apply the case transform per element (issue
    /// #124); previously only @Q was handled and these passed through.
    run_result_t r = run_shell("arr=(foo Bar baz)\n"
                               "echo \"[${arr[@]@U}]\"\n"
                               "echo \"[${arr[@]@u}]\"\n"
                               "echo \"[${arr[@]@L}]\"\n");
    ASSERT_STDOUT_EQ(r, "[FOO BAR BAZ]\n"
                        "[Foo Bar Baz]\n"
                        "[foo bar baz]\n");
}

TEST(rt_pipeline_three_stages) {
    /// Three-stage plumbing: producer | middle | terminator. The
    /// original form used `wc -c` to count bytes, but `wc -c`
    /// emits leading-whitespace-padded output on BSD coreutils
    /// (macOS) and unpadded output on GNU coreutils (Linux),
    /// which made the literal-output assertion platform-specific.
    /// `cat | cat` exercises the same N-stage pipeline plumbing
    /// (data flows through three connected pipes; the final stage
    /// must terminate the pipeline) with byte-identical output
    /// across platforms.
    run_result_t r = run_shell("echo hello | cat | cat\n");
    ASSERT_STDOUT_EQ(r, "hello\n");
}

TEST(rt_pipeline_four_stages) {
    run_result_t r = run_shell("echo abcde | cat | cat | rev\n");
    ASSERT_STDOUT_EQ(r, "edcba\n");
}

TEST(rt_pipeline_pipestatus_three_stages) {
    run_result_t r = run_shell(
        "false | true | false\n"
        "echo \"${PIPESTATUS[0]} ${PIPESTATUS[1]} ${PIPESTATUS[2]}\"\n");
    ASSERT_STDOUT_EQ(r, "1 0 1\n");
}

TEST(rt_pipeline_lush_pipestatus_three_stages) {
    run_result_t r = run_shell(
        "true | false | true\n"
        "echo \"${pipestatus[0]} ${pipestatus[1]} ${pipestatus[2]}\"\n");
    ASSERT_STDOUT_EQ(r, "0 1 0\n");
}

TEST(rt_pipestatus_simple_command_and_opacity) {
    /// #492: PIPESTATUS/pipestatus is refreshed for a simple command (a
    /// one-element pipeline), not only for a multi-stage pipeline -- bash and
    /// zsh both update the array for every executed pipeline, and a simple
    /// command is a pipeline of one. A function call and a subshell are opaque:
    /// their aggregate status is published as a one-element array, not the
    /// per-stage array of an inner pipeline (bash and zsh agree).
    run_result_t r = run_shell(
        "false;    echo \"simple:${PIPESTATUS[*]}\"\n"
        "true;     echo \"t:${PIPESTATUS[*]}\"\n"
        ":;        echo \"b:${PIPESTATUS[*]}\"\n"
        "myf() { false | true | false; }; myf; echo \"fn:${PIPESTATUS[*]}\"\n"
        "( exit 4 ); echo \"sub:${pipestatus[*]}\"\n");
    ASSERT_STDOUT_EQ(r, "simple:1\n"
                        "t:0\n"
                        "b:0\n"
                        /// function call opaque: [return], not the body's array
                        "fn:1\n"
                        /// subshell opaque: [exit status]
                        "sub:4\n");
}

TEST(rt_pipeline_pipefail_rightmost_nonzero) {
    run_result_t r = run_shell("set -o pipefail\n"
                               "true | false | true\n"
                               "echo exit=$?\n");
    ASSERT_STDOUT_EQ(r, "exit=1\n");
}

TEST(rt_pipeline_pipefail_all_succeed) {
    run_result_t r = run_shell("set -o pipefail\n"
                               "true | true | true\n"
                               "echo exit=$?\n");
    ASSERT_STDOUT_EQ(r, "exit=0\n");
}

TEST(rt_pipeline_diagnostic_per_stage_errors) {
    run_result_t r = run_shell("set -o pipeline-diagnostic\n"
                               "false | true | false\n"
                               "echo exit=$?\n");
    ASSERT_STDERR_CONTAINS(r, "pipeline stage 1 of 3 exited 1");
    ASSERT_STDERR_CONTAINS(r, "pipeline stage 3 of 3 exited 1");
    ASSERT_STDOUT_EQ(r, "exit=1\n");
}

TEST(rt_pipeline_diagnostic_silent_on_success) {
    run_result_t r = run_shell("set -o pipeline-diagnostic\n"
                               "true | true | true\n"
                               "echo exit=$?\n");
    ASSERT_STDOUT_EQ(r, "exit=0\n");
}

TEST(rt_pe_catalog_scalar_conditional_still_works) {
    run_result_t r = run_shell("s=hello\n"
                               "echo \"[${s:-default}]\"\n"
                               "unset s\n"
                               "echo \"[${s:-default}]\"\n");
    ASSERT_STDOUT_EQ(r, "[hello]\n[default]\n");
}

/// Helper for the inspect-widget tests.  Stages a synthetic editor with text +
/// cursor, runs the callback, and returns the captured LUSH_INSPECT_* values
/// via the symtable's exit-status-irrelevant get_var path.
static void run_inspect(const char *text, size_t cursor_pos) {
    lle_buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    buf.data = (char *)text;
    buf.length = text ? strlen(text) : 0;
    buf.cursor.byte_offset = cursor_pos;

    lle_editor_t editor;
    memset(&editor, 0, sizeof(editor));
    editor.buffer = &buf;

    lush_inspect_widget_callback(&editor, NULL);
}

static const char *inspect_var(const char *name) {
    /// symtable_get_var returns an owned copy. Copy it into a static buffer and
    /// free the original so the inline ASSERT_STR_EQ callers do not leak it.
    static char buf[512];
    char *owned = symtable_get_var(symtable_get_global_manager(), name);
    if (!owned) {
        return NULL;
    }
    snprintf(buf, sizeof(buf), "%s", owned);
    free(owned);
    return buf;
}

TEST(rt_inspect_scalar_at_cursor) {
    symtable_set_global("FOO", "hello");
    run_inspect("echo $FOO", 9); /// cursor at end of FOO
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "FOO", "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "scalar", "kind");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_VALUE"), "hello", "value");
}

TEST(rt_inspect_braced_name) {
    symtable_set_global("BAR", "world");
    run_inspect("echo ${BAR}!", 9); /// cursor mid-identifier in ${BAR}
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "BAR", "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "scalar", "kind");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_VALUE"), "world", "value");
}

TEST(rt_inspect_strict_past_closing_brace) {
    symtable_set_global("BAR", "world");
    run_inspect("echo ${BAR}!", 11); /// strict: past closing '}' is outside
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "", "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "none", "kind");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_VALUE"), "", "value");
}

TEST(rt_inspect_cursor_on_dollar) {
    symtable_set_global("BAR", "world");
    run_inspect("echo $BAR", 5); /// cursor on the `$`
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "BAR", "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "scalar", "kind");
}

TEST(rt_inspect_cursor_on_closing_brace) {
    symtable_set_global("BAR", "world");
    run_inspect("echo ${BAR}!", 10); /// cursor on the `}`
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "BAR", "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "scalar", "kind");
}

TEST(rt_inspect_list_quoted) {
    /// arr=(hello "two words" three) -- the inspector must @Q-quote each
    /// element so a space-containing entry stays unambiguous.
    run_shell("arr=(hello 'two words' three)\n");
    run_inspect("echo $arr", 9);
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "list", "kind");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_VALUE"),
                  "'hello' 'two words' 'three'", "value");
}

TEST(rt_inspect_unset_variable) {
    run_inspect("echo $UNDEFINED_VAR_NAME", 24);
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "UNDEFINED_VAR_NAME",
                  "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "unset", "kind");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_VALUE"), "", "value");
}

TEST(rt_inspect_no_ref_under_cursor) {
    run_inspect("plain text no dollar here", 5);
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "", "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "none", "kind");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_VALUE"), "", "value");
}

TEST(rt_inspect_cursor_mid_identifier) {
    symtable_set_global("LONGNAME", "v");
    run_inspect("echo $LONGNAME tail", 9); /// cursor mid 'LONGNAME'
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_NAME"), "LONGNAME", "name");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_KIND"), "scalar", "kind");
    ASSERT_STR_EQ(inspect_var("LUSH_INSPECT_VALUE"), "v", "value");
}

TEST(rt_sigil_at_on_scalar) {
    run_result_t r = run_shell("x=hello\necho @x\n");
    ASSERT_STDOUT_EQ(r, "hello\n");
}

TEST(rt_sigil_at_on_list) {
    run_result_t r = run_shell("arr=(a b c)\necho @arr\n");
    ASSERT_STDOUT_EQ(r, "a b c\n");
}

TEST(rt_sigil_at_on_map) {
    run_result_t r = run_shell("declare -A m\nm[k1]=v1\nm[k2]=v2\necho @m\n");
    ASSERT_STDOUT_EQ(r, "v1 v2\n");
}

TEST(rt_sigil_pair_on_list) {
    run_result_t r = run_shell("arr=(a b c)\necho %arr\n");
    ASSERT_STDOUT_EQ(r, "0 a 1 b 2 c\n");
}

TEST(rt_sigil_pair_on_map) {
    run_result_t r = run_shell("declare -A m\nm[k1]=v1\nm[k2]=v2\necho %m\n");
    ASSERT_STDOUT_EQ(r, "k1 v1 k2 v2\n");
}

TEST(rt_sigil_pair_on_scalar_type_mismatch) {
    run_result_t r = run_shell("x=hello\necho %x\n");
    ASSERT_STDERR_CONTAINS(r, "%x: pair sigil on scalar");
}

TEST(rt_sigil_unset_name_contributes_nothing) {
    /// A kind sigil on an undeclared name contributes nothing, exactly as an
    /// undeclared name does in every other expansion context ($q, "${q[@]}").
    /// The sigil presents a collection; an absent name is an empty collection.
    /// (This is lush's uniform unset=empty model, not the bash spelling: bash
    /// has no sigil at all.) Issue #545.
    run_result_t r = run_shell("echo %q end\n");
    ASSERT_STDOUT_EQ(r, "end\n");
    run_result_t r2 = run_shell("echo @nope end\n");
    ASSERT_STDOUT_EQ(r2, "end\n");
}

TEST(rt_sigil_unset_in_array_literal_contributes_nothing) {
    /// In an array literal an undeclared sigil operand contributes no element,
    /// exactly as `arr=(${q[@]} x)` yields just `x`.
    run_result_t r =
        run_shell("arr=(%q x)\nprintf '(%s)' \"${arr[@]}\"\necho\n");
    ASSERT_STDOUT_EQ(r, "(x)\n");
    /// Mid-position too.
    run_result_t r2 =
        run_shell("arr=(a @q c)\nprintf '(%s)' \"${arr[@]}\"\necho\n");
    ASSERT_STDOUT_EQ(r2, "(a)(c)\n");
}

TEST(rt_sigil_scalar_still_type_error) {
    /// The real type case is unchanged: a `%` on a declared scalar is E1134 --
    /// a value that exists but has no pair component, which is an error, not an
    /// absence.
    run_result_t r = run_shell("x=hi\necho %x\n");
    ASSERT_STDERR_CONTAINS(r, "pair sigil on scalar");
}

TEST(rt_sigil_compat_user_at_host) {
    /// `user@host` is a bare word -- `@` is mid-token, not at word start.
    run_result_t r = run_shell("echo user@host\n");
    ASSERT_STDOUT_EQ(r, "user@host\n");
}

TEST(rt_sigil_compat_at_invalid_identifier) {
    /// `@{-1}` post-sigil string `{-1}` fails identifier regex; bare word.
    run_result_t r = run_shell("echo '@{-1}'\n");
    ASSERT_STDOUT_EQ(r, "@{-1}\n");
}

TEST(rt_sigil_literal_in_double_quotes) {
    /// `@` and `%` sigils are bare-word-only: double quotes suppress them
    /// (like ~), so a quoted string is a literal. This is what keeps
    /// printf "%s", "user@host", and "100% off" working in lush mode.
    run_result_t r = run_shell("echo \"[@undefined]\"\n");
    ASSERT_STDOUT_EQ(r, "[@undefined]\n");
}

TEST(rt_printf_percent_s_lush_mode) {
    /// The canonical idiom must work in the default (lush) mode, not just
    /// under mode posix: %s inside double quotes is a printf conversion, not
    /// a map sigil.
    run_result_t r = run_shell("mode lush\nx=hello\nprintf \"%s\\n\" \"$x\"\n");
    ASSERT_STDOUT_EQ(r, "hello\n");
}

TEST(rt_user_at_host_quoted_literal) {
    /// `@` inside double quotes is literal -- email addresses and user@host
    /// survive in lush mode.
    run_result_t r = run_shell("mode lush\necho \"deploy to user@host now\"\n");
    ASSERT_STDOUT_EQ(r, "deploy to user@host now\n");
}

TEST(rt_sigil_off_in_bash_mode) {
    /// mode bash disables FEATURE_KIND_SIGILS -- @x stays a bare word so
    /// existing bash scripts that pass `@reboot` etc. don't break.
    run_result_t r = run_shell("mode bash\nx=hello\necho @x\n");
    ASSERT_STDOUT_EQ(r, "@x\n");
}

TEST(rt_test_o_errexit_reflects_set_e) {
    /// `[[ -o errexit ]]` must reflect the current state of `set -e`.
    /// Before lush/posix_opts shipped shell_is_option_set, the operator
    /// always returned false regardless of state -- a hard masking bug
    /// for any script that branches on shell-option state.
    run_result_t r = run_shell("[[ -o errexit ]] && echo before-on || echo "
                               "before-off\n"
                               "set -e\n"
                               "[[ -o errexit ]] && echo after-on || echo "
                               "after-off\n"
                               "set +e\n"
                               "[[ -o errexit ]] && echo clear-on || echo "
                               "clear-off\n");
    ASSERT_STDOUT_EQ(r, "before-off\nafter-on\nclear-off\n");
}

TEST(rt_test_o_noop_alias_records_state) {
    /// setopt on a noop-alias (prompt_subst etc.) must record state so
    /// `[[ -o name ]]` reports the right answer. The underlying behavior
    /// is always-on; this is purely about introspection-matching zsh.
    run_result_t r = run_shell("[[ -o prompt_subst ]] && echo default-on || "
                               "echo default-off\n"
                               "unsetopt prompt_subst\n"
                               "[[ -o prompt_subst ]] && echo unset-on || "
                               "echo unset-off\n"
                               "setopt prompt_subst\n"
                               "[[ -o prompt_subst ]] && echo set-on || echo "
                               "set-off\n");
    ASSERT_STDOUT_EQ(r, "default-on\nunset-off\nset-on\n");
}

TEST(rt_bindkey_bind_then_query_round_trips) {
    bindkey_table_reset();
    /// bindkey now records bindings in a side table so the query form
    /// returns what was set. Previously the silent no-op left every
    /// query empty -- a verified ~17% divergence in oh-my-zsh sites.
    run_result_t r = run_shell("bindkey '^X^E' some-widget\n"
                               "bindkey '^X^E'\n");
    ASSERT_STDOUT_EQ(r, "\"^X^E\" some-widget\n");
}

TEST(rt_bindkey_list_after_multiple_binds) {
    bindkey_table_reset();
    /// Listing prints every recorded binding in the active keymap.
    /// We only check that both bindings appear; order isn't contractual.
    run_result_t r = run_shell("bindkey '^A' alpha-widget\n"
                               "bindkey '^B' beta-widget\n"
                               "bindkey\n");
    ASSERT_STDOUT_CONTAINS(r, "\"^A\" alpha-widget");
    ASSERT_STDOUT_CONTAINS(r, "\"^B\" beta-widget");
}

TEST(rt_bindkey_query_unbound_key_returns_nonzero) {
    bindkey_table_reset();
    /// Query for a key that was never bound must return non-zero
    /// (matches zsh) and emit nothing.
    run_result_t r = run_shell("bindkey '^Z' && echo bound || echo unbound\n");
    ASSERT_STDOUT_EQ(r, "unbound\n");
}

TEST(rt_zle_register_then_list) {
    zle_widget_table_reset();
    /// zle -N records the widget; zle -l lists the names.
    run_result_t r = run_shell("my_widget() { echo hi; }\n"
                               "zle -N my_widget\n"
                               "zle -l\n");
    ASSERT_STDOUT_EQ(r, "my_widget\n");
}

TEST(rt_zle_register_with_body_then_L_form) {
    zle_widget_table_reset();
    /// zle -N WIDGET FUNCTION records both names; -L emits the
    /// re-runnable `zle -N WIDGET FUNCTION` form.
    run_result_t r = run_shell("fn_impl() { echo body; }\n"
                               "zle -N alias_widget fn_impl\n"
                               "zle -L\n");
    ASSERT_STDOUT_EQ(r, "zle -N alias_widget fn_impl\n");
}

TEST(rt_logical_op_at_eol_continues_statement) {
    /// `} &&` followed by newline used to break: the input layer didn't
    /// know `&&` at end of line meant "incomplete, keep reading", so the
    /// parser saw `} &&` as one statement and raised E1001 on the empty
    /// right-hand side.
    run_result_t r = run_shell("_f() {\n"
                               "    echo body\n"
                               "} &&\n"
                               "    echo done\n"
                               "_f\n");
    ASSERT_STDOUT_EQ(r, "done\nbody\n");
}

TEST(rt_pipe_at_eol_continues_statement) {
    /// Same fix covers a trailing `|` at end of line for multi-line
    /// pipelines.
    run_result_t r = run_shell("echo hello |\n"
                               "    tr a-z A-Z\n");
    ASSERT_STDOUT_EQ(r, "HELLO\n");
}

TEST(rt_extended_test_empty_lhs_inequality) {
    /// `[[ "$EMPTY" != true ]]` after $EMPTY expands to "" must return
    /// true (empty != "true"). Lush previously fell through to the "no
    /// operator -- non-empty string is true" branch because the single
    /// `=` / `!=` operator wasn't recognized for empty-LHS cases.
    run_result_t r = run_shell("v=\"\"\n"
                               "[[ \"$v\" != true ]] && echo unequal || echo "
                               "equal\n");
    ASSERT_STDOUT_EQ(r, "unequal\n");
}

TEST(rt_extended_test_single_equals_alias) {
    /// Bash/zsh accept `=` as an alias for `==` in [[ ]]. Lush now
    /// routes both through the same pattern-match path.
    run_result_t r = run_shell("[[ hello = hello ]] && echo eq || echo ne\n"
                               "[[ hello = world ]] && echo eq || echo ne\n");
    ASSERT_STDOUT_EQ(r, "eq\nne\n");
}

TEST(rt_test_equality_under_nfc_equivalence) {
    /// `[ a = b ]` (and the `[`-builtin form) now compares under NFC
    /// equivalence: precomposed "cafe-acute" (e-acute U+00E9, bytes
    /// C3 A9) equals decomposed "cafe-acute" (e + combining acute,
    /// bytes 65 CC 81) even though byte-level strcmp would diverge.
    /// Lush-superset divergence from bash/zsh; justified by the
    /// project's NFC-everywhere policy.
    run_result_t r =
        run_shell("nfc=$'caf\\xc3\\xa9'\n"
                  "nfd=$'cafe\\xcc\\x81'\n"
                  "[ \"$nfc\" = \"$nfd\" ] && echo eq || echo ne\n"
                  "[ \"$nfc\" != \"$nfd\" ] && echo ne2 || echo eq2\n");
    ASSERT_STDOUT_EQ(r, "eq\neq2\n");
}

TEST(rt_test_inequality_under_nfc_when_actually_different) {
    /// NFC equivalence does not paper over actually-different
    /// strings.  Lowercase cafe-acute vs uppercase CAFE-acute remain unequal
    /// (case folding is a separate axis from NFC).
    run_result_t r = run_shell(
        "[ \"caf\xc3\xa9\" = \"CAF\xc3\x89\" ] && echo eq || echo ne\n");
    ASSERT_STDOUT_EQ(r, "ne\n");
}

TEST(rt_test_ascii_paths_unchanged) {
    /// ASCII inputs hit the NFC fast path (LLE_UNICODE_COMPARE_DEFAULT
    /// detects ASCII-only and shortcircuits to byte compare); behavior
    /// must be byte-identical to the prior strcmp implementation.
    run_result_t r = run_shell("[ \"hello\" = \"hello\" ] && echo eq\n"
                               "[ \"hello\" = \"world\" ] && echo ouch || "
                               "echo correct-ne\n"
                               "[ \"\" = \"\" ] && echo empty-eq\n"
                               "[ \"hi\" != \"bye\" ] && echo ne-ok\n");
    ASSERT_STDOUT_EQ(r, "eq\ncorrect-ne\nempty-eq\nne-ok\n");
}

TEST(rt_dollar_plus_is_set_test) {
    /// zsh `${+NAME}` returns "1" if NAME is set, "0" otherwise. Both
    /// braced and unbraced (`$+NAME`) forms must work.
    run_result_t r = run_shell("v=hi\n"
                               "echo \"${+v}\"\n"
                               "echo \"${+nonexist}\"\n"
                               "(( $+v )) && echo set || echo unset\n");
    ASSERT_STDOUT_EQ(r, "1\n0\nset\n");
}

TEST(rt_dollar_plus_commands_path_lookup) {
    /// zsh's `${+commands[NAME]}` checks whether NAME is on $path.
    /// Lush implements it as a PATH lookup since there's no `$commands`
    /// hash.  `ls` is essentially universally available.
    run_result_t r = run_shell("(( $+commands[ls] )) && echo yes || echo no\n");
    ASSERT_STDOUT_EQ(r, "yes\n");
}

TEST(rt_unfunction_removes_function) {
    /// `unfunction NAME` removes a defined function. Subsequent call
    /// produces command-not-found behavior (caught by the test
    /// framework's exit-code check via the conditional).
    run_result_t r = run_shell("greet() { echo hello; }\n"
                               "greet\n"
                               "unfunction greet\n"
                               "greet 2>/dev/null && echo still-defined || "
                               "echo removed\n");
    ASSERT_STDOUT_EQ(r, "hello\nremoved\n");
}

TEST(rt_multi_name_function_definition) {
    /// zsh's `function NAME1 NAME2 { body }` defines all names as
    /// functions sharing the body. Each must be independently callable.
    run_result_t r = run_shell("function foo bar baz {\n"
                               "    echo args: \"$@\"\n"
                               "}\n"
                               "foo arg1\n"
                               "bar arg2\n"
                               "baz arg3\n");
    ASSERT_STDOUT_EQ(r, "args: arg1\nargs: arg2\nargs: arg3\n");
}

TEST(rt_zstyle_set_then_query_t_true) {
    zstyle_table_reset();
    /// zstyle PATTERN STYLE true; zstyle -t PATTERN STYLE returns 0
    /// (matches zsh).
    run_result_t r = run_shell("zstyle ':completion:*' menu true\n"
                               "zstyle -t ':completion:*' menu && echo on || "
                               "echo off\n");
    ASSERT_STDOUT_EQ(r, "on\n");
}

TEST(rt_zstyle_set_then_query_s_get) {
    zstyle_table_reset();
    /// zstyle -s PATTERN STYLE VAR populates VAR with the value.
    run_result_t r = run_shell("zstyle ':completion:*' format 'completing %d'\n"
                               "zstyle -s ':completion:*' format result\n"
                               "echo \"[$result]\"\n");
    ASSERT_STDOUT_EQ(r, "[completing %d]\n");
}

TEST(rt_zstyle_T_treats_unset_as_true) {
    zstyle_table_reset();
    /// zstyle -T returns 0 when the pattern+style is unset (matches zsh
    /// semantics: -T is "true unless explicitly false").
    run_result_t r =
        run_shell("zstyle -T ':never:set' style && echo yes || echo no\n");
    ASSERT_STDOUT_EQ(r, "yes\n");
}

TEST(rt_zstyle_delete_removes_entry) {
    zstyle_table_reset();
    run_result_t r = run_shell("zstyle ':x' k v\n"
                               "zstyle -d ':x' k\n"
                               "zstyle -t ':x' k && echo on || echo off\n");
    ASSERT_STDOUT_EQ(r, "off\n");
}

TEST(rt_typeset_H_flag_accepted) {
    /// zsh's typeset -H (hide from listing) is purely a display attribute;
    /// lush accepts it silently so `typeset -AHg name` (the spectrum.zsh
    /// idiom) declares the assoc array without tripping invalid-option.
    run_result_t r = run_shell("typeset -AHg M\n"
                               "M[k]=v\n"
                               "echo \"${M[k]}\"\n");
    ASSERT_STDOUT_EQ(r, "v\n");
}

TEST(rt_typeset_U_flag_accepted) {
    /// zsh's typeset -U (unique-element array). Lush accepts the flag but
    /// does not enforce dedup yet (documented limitation); script must
    /// still run without invalid-option error.
    run_result_t r = run_shell("typeset -aU arr\n"
                               "arr=(a b c)\n"
                               "echo \"${arr[@]}\"\n");
    ASSERT_STDOUT_EQ(r, "a b c\n");
}

TEST(rt_zle_delete_then_list_empty) {
    zle_widget_table_reset();
    /// zle -D removes the widget; subsequent -l prints nothing.
    run_result_t r = run_shell("zle -N w1\n"
                               "zle -N w2\n"
                               "zle -D w1\n"
                               "zle -l\n");
    ASSERT_STDOUT_EQ(r, "w2\n");
}

TEST(rt_bindkey_remove_then_query_returns_nonzero) {
    bindkey_table_reset();
    /// bindkey -r KEY removes the binding; subsequent query returns
    /// non-zero (matches zsh).
    run_result_t r = run_shell("bindkey '^X' some-widget\n"
                               "bindkey -r '^X'\n"
                               "bindkey '^X' && echo bound || echo unbound\n");
    ASSERT_STDOUT_EQ(r, "unbound\n");
}

TEST(rt_test_o_unknown_option_returns_false) {
    /// An option name lush has no opinion on must return false -- not
    /// crash, not error.
    run_result_t r =
        run_shell("[[ -o totally_made_up_option ]] && echo yes || echo no\n");
    ASSERT_STDOUT_EQ(r, "no\n");
}

TEST(rt_typed_fn_lexical_scope) {
    /// A typed-fn body resolves free names through its captured
    /// declaration-site scope, not through the dynamic caller's
    /// locals. The same outer caller invokes a POSIX function and a
    /// typed function -- the POSIX one sees the caller's `local v`,
    /// the typed one sees the global `v`.
    run_result_t r = run_shell(
        "v=GLOBAL\n"
        "posix_callee() { echo \"[$v]\"; }\n"
        "fn typed_callee() -> scalar { return \"$v\"; }\n"
        "outer() { local v=LOCAL; posix_callee; let r = typed_callee(); "
        "echo \"[$r]\"; }\n"
        "outer\n");
    ASSERT_STDOUT_EQ(r, "[LOCAL]\n[GLOBAL]\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

/// Forward declarations for initialization
extern void init_symtable(void);
extern void free_global_symtable(void);
extern void free_shell_argv(void);

/* ============================================================================
 * Regression: readonly enforcement
 *
 * Covers the contract that SYMVAR_READONLY actually blocks subsequent
 * writes: bare reassign, cross-function-scope reassign, declare -r and
 * readonly keyword routes, array-element reassign, and the listing.
 * ============================================================================
 */

/// Each test below uses a name unique to itself so the global
/// symbol table (which persists across run_shell calls in the
/// in-process harness) does not leak a readonly attribute from
/// the previous case into the next case's preamble.

TEST(rt_readonly_blocks_same_scope_reassign) {
    run_result_t r = run_shell("readonly RO_REASSIGN=1\n"
                               "RO_REASSIGN=2\n"
                               "echo \"final $RO_REASSIGN\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "final 1\n");
}

TEST(rt_readonly_assign_aborts_or_list) {
    /// A readonly assignment error must NOT feed `||`: bash, zsh, and
    /// dash all decline to run the right operand (#120). In a non-posix
    /// preset the script continues to the next statement with $?=1.
    run_result_t r = run_shell("readonly RO_OR=1\n"
                               "RO_OR=2 || echo SHOULD_NOT_RUN\n"
                               "echo \"after $?\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "after 1\n");
}

TEST(rt_readonly_assign_aborts_and_list) {
    /// Same for `&&`: the readonly error aborts the current AND-OR list,
    /// the right operand is skipped, and the script continues.
    run_result_t r = run_shell("readonly RO_AND=1\n"
                               "RO_AND=2 && echo SHOULD_NOT_RUN\n"
                               "echo \"after $?\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "after 1\n");
}

TEST(rt_readonly_assign_diag_bypasses_redirect) {
    /// The readonly diagnostic is a shell-level message reported on the
    /// real stderr after the command's transient redirection is torn
    /// down, so a `2>/dev/null` on the assignment does not swallow it
    /// (matching bash). The `||` right operand still does not run.
    run_result_t r = run_shell("readonly RO_RED=1\n"
                               "RO_RED=2 2>/dev/null || echo SHOULD_NOT_RUN\n"
                               "echo \"after $?\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "after 1\n");
}

TEST(rt_readonly_normal_or_still_fires) {
    /// Regression guard: an ordinary command failure must still feed
    /// `||`. The abort signal is specific to assignment errors.
    run_result_t r = run_shell("false || echo normal-or-ran\n");
    ASSERT_STDOUT_EQ(r, "normal-or-ran\n");
}

TEST(rt_readonly_assign_posix_mode_exits) {
    /// POSIX 2.8.1: a variable-assignment error exits a non-interactive
    /// shell. In posix mode the statement after the readonly error must
    /// not run.
    run_result_t r = run_shell("mode posix\n"
                               "readonly RO_PX=1\n"
                               "RO_PX=2\n"
                               "echo SHOULD_NOT_RUN\n");
    /// `mode` mutates global shell state the harness does not reset
    /// between tests; restore the native default before asserting (an
    /// assertion failure longjmps out and would otherwise leave every
    /// later test running under posix mode).
    run_shell("mode lush\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rt_readonly_assign_zsh_mode_exits) {
    /// zsh exits a non-interactive shell on a readonly assignment error
    /// (like dash); zsh mode matches it via FEATURE_ASSIGN_ERROR_EXITS.
    run_result_t r = run_shell("mode zsh\n"
                               "readonly RO_ZX=1\n"
                               "RO_ZX=2\n"
                               "echo SHOULD_NOT_RUN\n");
    run_shell("mode lush\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rt_readonly_assign_setopt_toggles_exit) {
    /// The exit policy is a feature gate reachable from the central
    /// config: enabling assign_error_exits in an otherwise-continue
    /// preset (bash) makes the readonly error exit the shell.
    run_result_t r = run_shell("mode bash\n"
                               "setopt assign_error_exits\n"
                               "readonly RO_TOG=1\n"
                               "RO_TOG=2\n"
                               "echo SHOULD_NOT_RUN\n");
    run_shell("mode lush\nunsetopt assign_error_exits\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "");
}

TEST(rt_readonly_blocks_via_readonly_keyword) {
    /// `readonly NAME` without a value promotes an existing scalar
    /// to readonly; later writes must be refused.
    run_result_t r = run_shell("RO_PROMOTE=initial\n"
                               "readonly RO_PROMOTE\n"
                               "RO_PROMOTE=overwrite\n"
                               "echo \"final $RO_PROMOTE\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "final initial\n");
}

TEST(rt_readonly_blocks_across_function_scope) {
    /// Cross-scope rule: an assignment inside a function body to a
    /// name that is readonly at any enclosing scope is refused.
    run_result_t r = run_shell("readonly RO_FUNC=outer\n"
                               "ro_func_fixture() { RO_FUNC=inner; }\n"
                               "ro_func_fixture\n"
                               "echo \"final $RO_FUNC\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "final outer\n");
}

TEST(rt_readonly_blocks_via_declare_r) {
    /// declare -r sets SYMVAR_READONLY the same way the readonly
    /// builtin does; enforcement applies through the same path.
    run_result_t r = run_shell("declare -r RO_DECLARE=fixed\n"
                               "RO_DECLARE=mutated\n"
                               "echo \"final $RO_DECLARE\"\n");
    ASSERT_STDERR_CONTAINS(r, "readonly variable");
    ASSERT_STDOUT_EQ(r, "final fixed\n");
}

TEST(rt_readonly_blocks_array_element_via_readonly_keyword) {
    /// `readonly NAME` against an existing array marks the array
    /// readonly via its own flags field. Subsequent element writes
    /// (NAME[idx]=value) are refused by symtable_set_array_element
    /// with SYMTABLE_ERR_READONLY; the array's prior contents are
    /// preserved.
    run_result_t r = run_shell("declare -a ROA_KW=(a b c)\n"
                               "readonly ROA_KW\n"
                               "ROA_KW[0]=clobber\n"
                               "echo \"[0]=${ROA_KW[0]}\"\n");
    ASSERT_STDOUT_EQ(r, "[0]=a\n");
}

TEST(rt_readonly_blocks_array_element_via_declare_ar) {
    /// declare -ar threads SYMVAR_READONLY onto the array record at
    /// declaration time; element writes are refused immediately.
    run_result_t r = run_shell("declare -ar ROA_DECL=(x y z)\n"
                               "ROA_DECL[1]=clobber\n"
                               "echo \"[1]=${ROA_DECL[1]}\"\n");
    ASSERT_STDOUT_EQ(r, "[1]=y\n");
}

TEST(rt_readonly_blocks_associative_array_element) {
    /// Associative arrays carry the same flags field; `declare -Ar`
    /// rejects subsequent key writes just like indexed arrays.
    run_result_t r = run_shell("declare -Ar ROA_ASSOC=([k]=v)\n"
                               "ROA_ASSOC[k]=overwrite\n"
                               "echo \"[k]=${ROA_ASSOC[k]}\"\n");
    ASSERT_STDOUT_EQ(r, "[k]=v\n");
}

TEST(rt_readonly_listing_includes_marked) {
    /// The no-arg `readonly` listing path now enumerates marked
    /// variables; entries set via `readonly NAME=VALUE` appear.
    run_result_t r = run_shell("readonly RO_LISTED=hello\nreadonly\n");
    ASSERT_STDOUT_CONTAINS(r, "readonly RO_LISTED='hello'");
}

TEST(rt_readonly_promotes_existing_var) {
    /// `readonly NAME` without `=` keeps the prior value and marks
    /// the variable readonly without clobbering its content.
    run_result_t r = run_shell("RO_KEEP=kept\n"
                               "readonly RO_KEEP\n"
                               "readonly | grep \"^readonly RO_KEEP=\"\n");
    ASSERT_STDOUT_CONTAINS(r, "readonly RO_KEEP='kept'");
}

/* ============================================================================
 * Regression: declare -l / declare -u case-attribute enforcement
 *
 * The SYMVAR_LOWERCASE / SYMVAR_UPPERCASE flags were previously set by
 * bin_declare but never read by any write path; values assigned to a
 * -l / -u variable were stored verbatim. Enforcement now lives in
 * execute_assignment (via symtable_apply_case_attr_alloc), and the
 * declare path itself folds the initial value at declaration time
 * including retroactive folding when promoting an existing variable.
 * ============================================================================
 */

TEST(rt_declare_l_initial_value_folded) {
    /// declare -l NAME=VALUE folds VALUE to lowercase at declaration.
    run_result_t r = run_shell("declare -l DL_INIT=HELLO\necho \"$DL_INIT\"\n");
    ASSERT_STDOUT_EQ(r, "hello\n");
}

TEST(rt_declare_l_subsequent_assignment_folded) {
    /// Subsequent writes to a -l variable also fold to lowercase.
    run_result_t r =
        run_shell("declare -l DL_LATER\nDL_LATER=WORLD\necho \"$DL_LATER\"\n");
    ASSERT_STDOUT_EQ(r, "world\n");
}

TEST(rt_declare_l_unicode_fold) {
    /// Case fold goes through lle_utf8_tolower so non-ASCII codepoints
    /// fold per the project's Unicode case table.
    run_result_t r =
        run_shell("declare -l DL_UNI=CAF\xc3\x89\necho \"$DL_UNI\"\n");
    ASSERT_STDOUT_EQ(r, "caf\xc3\xa9\n");
}

TEST(rt_declare_l_promotes_existing) {
    /// declare -l NAME (no value) on an existing variable folds the
    /// current value retroactively and adds the attribute so future
    /// assignments also fold.
    run_result_t r = run_shell("DL_PROMOTE=Mixed\n"
                               "declare -l DL_PROMOTE\n"
                               "echo \"$DL_PROMOTE\"\n"
                               "DL_PROMOTE=ANOTHER\n"
                               "echo \"$DL_PROMOTE\"\n");
    ASSERT_STDOUT_EQ(r, "mixed\nanother\n");
}

TEST(rt_declare_u_initial_value_folded) {
    /// declare -u NAME=VALUE folds VALUE to uppercase at declaration.
    run_result_t r = run_shell("declare -u DU_INIT=hello\necho \"$DU_INIT\"\n");
    ASSERT_STDOUT_EQ(r, "HELLO\n");
}

TEST(rt_declare_u_subsequent_assignment_folded) {
    /// Subsequent writes to a -u variable also fold to uppercase.
    run_result_t r =
        run_shell("declare -u DU_LATER\nDU_LATER=world\necho \"$DU_LATER\"\n");
    ASSERT_STDOUT_EQ(r, "WORLD\n");
}

TEST(rt_declare_u_unicode_fold) {
    /// Unicode fold via lle_utf8_toupper.
    run_result_t r =
        run_shell("declare -u DU_UNI=caf\xc3\xa9\necho \"$DU_UNI\"\n");
    ASSERT_STDOUT_EQ(r, "CAF\xc3\x89\n");
}

TEST(rt_declare_no_value_preserves_existing) {
    /// declare without an attribute or value on an existing variable
    /// must not clobber the existing value.
    run_result_t r = run_shell(
        "DECL_KEEP=preserved\ndeclare DECL_KEEP\necho \"$DECL_KEEP\"\n");
    ASSERT_STDOUT_EQ(r, "preserved\n");
}

int main(void) {
    printf("Running executor integration tests...\n\n");

    /// Initialize global symbol table - required for executor_new()
    init_symtable();

    printf("Lifecycle tests:\n");
    RUN_TEST(executor_new_free);
    RUN_TEST(executor_with_symtable);

    printf("\nSimple command tests:\n");
    RUN_TEST(execute_true);
    RUN_TEST(execute_false);
    RUN_TEST(execute_colon);
    RUN_TEST(execute_exit_status);

    printf("\nVariable tests:\n");
    RUN_TEST(variable_assignment);
    RUN_TEST(variable_expansion);
    RUN_TEST(multiple_assignments);

    printf("\nControl structure tests:\n");
    RUN_TEST(if_true_branch);
    RUN_TEST(if_false_branch);
    RUN_TEST(for_loop_basic);
    RUN_TEST(for_loop_no_in);
    RUN_TEST(for_loop_quoted_empty_word_posix);
    RUN_TEST(for_loop_quoted_string_not_split_posix);
    RUN_TEST(for_loop_unquoted_var_splits_posix);
    RUN_TEST(for_loop_unquoted_empty_no_iteration_posix);
    RUN_TEST(while_loop);
    RUN_TEST(until_loop);
    RUN_TEST(case_statement);
    RUN_TEST(rt_case_continue_polyglot_spellings);
    RUN_TEST(subscript_c1_assoc_escaped_key_roundtrip);
    RUN_TEST(subscript_c1_assoc_escaped_write_escaped_read);
    RUN_TEST(subscript_c1_assoc_singlequoted_read_matches);
    RUN_TEST(subscript_c1_assoc_key_stored_canonical_length);
    RUN_TEST(subscript_c1_assoc_plain_key_unchanged);
    RUN_TEST(subscript_c1_assoc_dollar_key_roundtrip);
    RUN_TEST(subscript_c1_indexed_roundtrip_untouched);
    RUN_TEST(subscript_c1_assoc_bracket_in_key_roundtrip);
    RUN_TEST(subscript_c1_assoc_dollar_anywhere);
    RUN_TEST(subscript_c1_assoc_tilde_key_literal);
    RUN_TEST(subscript_2c_quoted_write_roundtrip);
    RUN_TEST(subscript_2c_singlequoted_write_roundtrip);
    RUN_TEST(subscript_2c_quoted_append);
    RUN_TEST(subscript_2c_arg_glob_suppressed);
    RUN_TEST(subscript_2c_unquoted_arg_stays_broken);
    RUN_TEST(subscript_2c_undeclared_no_phantom);
    RUN_TEST(subscript_2c_promoted_scalar_survives_bad_index);
    RUN_TEST(rt_read_array_polyglot_spellings);
    RUN_TEST(for_loop_ifs_nonws_empty_fields_posix);
    RUN_TEST(rt_read_ifs_nonws_empty_fields);
    RUN_TEST(rt_read_array_ifs_nonws_empty_fields);
    RUN_TEST(extglob_gated_by_feature_extended_glob);
    RUN_TEST(case_wildcard);
    RUN_TEST(case_posix_character_class);
    RUN_TEST(rt_case_extglob_alternation);
    RUN_TEST(rt_bash_shopt_extglob_takes_effect_next_batch);
    RUN_TEST(rt_set_e_aborts_remaining_batches);
    RUN_TEST(rt_err_trap_fires_across_batch_boundaries);
    RUN_TEST(rt_case_plain_alternation_unregressed);
    RUN_TEST(rt_case_numeric_range_pattern);
    RUN_TEST(rt_case_numeric_range_upper_bound_and_expansion);
    RUN_TEST(rt_case_numeric_range_alternation);
    RUN_TEST(rt_glob_qualifier_mtime);
    RUN_TEST(rt_glob_qualifier_quoted_element);
    RUN_TEST(rt_pushd_popd_rotation);
    RUN_TEST(rt_pushdminus_option);
    RUN_TEST(rt_pushd_silent_option);
    RUN_TEST(rt_pushd_to_home_option);
    RUN_TEST(rt_pushd_ignore_dups_option);
    RUN_TEST(rt_magic_equal_subst);
    RUN_TEST(rt_assignment_tilde_expansion);
    RUN_TEST(rt_declaration_util_tilde_expansion);
    RUN_TEST(rt_declaration_util_mixed_quote_tilde);
    RUN_TEST(rt_mixed_quote_provenance_assignment);
    RUN_TEST(rt_command_word_mixed_quote_provenance);
    RUN_TEST(rt_ansi_c_arg_is_mode_gated);
    RUN_TEST(rt_special_params_scalar_covered);
    RUN_TEST(rt_unquoted_param_glob_brace_value_defers);
    RUN_TEST(rt_positional_params_covered);
    RUN_TEST(rt_bg_pid_and_option_flags_covered);
    RUN_TEST(rt_pe_alternation_operators_covered);
    RUN_TEST(rt_pe_operand_with_quotes_defers);
    RUN_TEST(rt_pe_pattern_strip_operators_covered);
    RUN_TEST(rt_pe_operand_edge_whitespace_defers);
    RUN_TEST(rt_pe_case_conversion_operators_covered);
    RUN_TEST(rt_pe_substring_operators_covered);
    RUN_TEST(rt_pe_substitution_operators_covered);
    RUN_TEST(rt_nounset_errors_on_covered_words);
    RUN_TEST(rt_nounset_exempt_forms_still_expand);
    RUN_TEST(rt_pe_assign_operators_covered);
    RUN_TEST(rt_pe_assign_write_is_transactional);
    RUN_TEST(rt_pe_assign_operators_defer_forms);
    RUN_TEST(rt_dq_backtick_runs_the_substitution);
    RUN_TEST(rt_pe_operand_is_evaluated_only_when_consumed);
    RUN_TEST(rt_pe_unused_operand_lazy_on_both_engines);
    RUN_TEST(rt_pe_unused_operand_raises_no_diagnostic);
    RUN_TEST(rt_pe_assign_operand_lazy_but_still_assigns);
    RUN_TEST(rt_pe_required_operators_covered);
    RUN_TEST(rt_pe_required_operators_error_forms);
    RUN_TEST(rt_pe_required_dollar_message_is_covered);
    RUN_TEST(rt_pe_required_message_is_not_evaluated_when_unused);
    RUN_TEST(rt_map_in_argv_forms);
    RUN_TEST(trap_err_fires_on_nonzero_exit);
    RUN_TEST(trap_err_silent_on_zero_exit);
    RUN_TEST(trap_err_not_inherited_in_function_by_default);
    RUN_TEST(trap_err_inherited_in_function_with_errtrace);
    RUN_TEST(trap_debug_not_inherited_in_function_by_default);
    RUN_TEST(trap_debug_inherited_in_function_with_functrace);
    RUN_TEST(trap_return_not_inherited_in_function_by_default);
    RUN_TEST(trap_return_inherited_in_function_with_functrace);

    printf("\nSEMANTICS 3.4/3.9 conformance tests:\n");
    RUN_TEST(arr_at_in_scalar_assignment_raises_type_mismatch);
    RUN_TEST(arr_at_in_for_loop_iterates);
    RUN_TEST(arr_star_in_scalar_assignment_joins);
    RUN_TEST(bare_arr_in_scalar_assignment_raises_type_mismatch);
    RUN_TEST(bare_arr_in_for_loop_iterates);
    RUN_TEST(bare_arr_glued_to_text_raises_type_mismatch);
    RUN_TEST(bare_arr_argv_yields_elements_one_to_one);
    RUN_TEST(at_paren_flag_alias_yields_vector);
    RUN_TEST(at_paren_flag_composes_with_sort);

    printf(
        "\nSEMANTICS 3.9 mode-gating tests (FEATURE_STRICT_VALUE_TYPING):\n");
    RUN_TEST(rt_cmdsub_no_split_in_lush_mode);
    RUN_TEST(rt_cmdsub_split_in_compat_modes);
    RUN_TEST(rt_cmdsub_split_select_matches_for);
    RUN_TEST(rt_cmdsub_explicit_split_via_array_literal);
    RUN_TEST(gate_bash_mode_arr_at_scalar_flattens_on_space);
    RUN_TEST(gate_zsh_mode_arr_at_scalar_flattens_on_ifs);
    RUN_TEST(gate_bash_mode_bare_arr_scalar_yields_element_zero);
    RUN_TEST(gate_zsh_mode_bare_arr_scalar_yields_whole_join);
    RUN_TEST(gate_bash_mode_keys_at_scalar_flattens);
    RUN_TEST(gate_posix_mode_arr_at_scalar_flattens_like_bash);
    RUN_TEST(gate_arr_star_scalar_joins_on_ifs_every_mode);
    RUN_TEST(gate_lush_mode_still_strict_after_gating);

    printf(
        "\nSEMANTICS 3.9 mode-gating: named array/map in command position:\n");
    RUN_TEST(gate_bash_mode_array_at_command_position_spreads);
    RUN_TEST(gate_bash_mode_array_command_with_trailing_args);
    RUN_TEST(gate_bash_mode_bare_array_command_is_element_zero);
    RUN_TEST(gate_zsh_mode_bare_array_command_spreads);
    RUN_TEST(gate_bash_mode_map_at_command_position_spreads_values);
    RUN_TEST(gate_bash_mode_empty_array_command_is_null_command);
    RUN_TEST(gate_lush_mode_array_command_still_strict);

    printf("\nSEMANTICS 3.9 mode-gating: scalar operator on a bare "
           "collection:\n");
    RUN_TEST(gate_bash_mode_bare_op_uses_element_zero);
    RUN_TEST(gate_bash_mode_bare_default_operator_element_zero);
    RUN_TEST(gate_bash_mode_assign_operator_preserves_array);
    RUN_TEST(gate_bash_mode_assign_operator_on_empty_sets_element_zero);
    RUN_TEST(gate_zsh_mode_bare_op_uses_whole_join);
    RUN_TEST(gate_zsh_mode_substring_is_curated_scalar_substr);
    RUN_TEST(gate_bare_op_numeric_operand_not_slice);
    RUN_TEST(gate_bare_op_attribute_query_not_type_error);
    RUN_TEST(gate_lush_mode_bare_op_still_strict);

    printf("\nSEMANTICS 3.9 mode-gating: export / readonly of an array "
           "literal:\n");
    RUN_TEST(gate_bash_mode_export_binds_array);
    RUN_TEST(gate_zsh_mode_export_binds_array_whole_join_read);
    RUN_TEST(gate_bash_mode_readonly_binds_array_and_enforces);
    RUN_TEST(gate_lush_mode_export_array_still_strict);
    RUN_TEST(gate_lush_mode_readonly_array_still_strict);

    printf("\nSEMANTICS 3.9 mode-gating: vector-producing parameter-flag "
           "forms:\n");
    RUN_TEST(gate_lush_mode_flag_vector_forms_scalar_type_error);
    RUN_TEST(gate_bash_mode_flag_vector_flattens_on_space);
    RUN_TEST(gate_zsh_mode_flag_vector_flattens_on_ifs);
    RUN_TEST(gate_flag_j_explicit_join_not_gated);
    RUN_TEST(gate_flag_s_on_scalar_not_gated);
    RUN_TEST(gate_flag_vector_slot_still_spreads);

    printf("\nLogical operator tests:\n");
    RUN_TEST(and_operator_success);
    RUN_TEST(and_operator_fail);
    RUN_TEST(or_operator_success);
    RUN_TEST(or_operator_fail);

    printf("\nFunction tests:\n");
    RUN_TEST(function_definition_posix);
    RUN_TEST(function_definition_ksh);
    RUN_TEST(function_with_args);
    RUN_TEST(function_return);

    printf("\nRegression tests \xe2\x80\x94 Issue #47 (local/declare/typeset "
           "assignment):\n");
    RUN_TEST(local_plain_string_reassignment);
    RUN_TEST(local_integer_reassignment);
    RUN_TEST(local_arith_substitution);
    RUN_TEST(local_command_substitution);
    RUN_TEST(declare_reassignment);
    RUN_TEST(typeset_reassignment);
    RUN_TEST(local_two_step_no_initial);
    RUN_TEST(local_does_not_leak_to_global);
    RUN_TEST(local_for_loop_variable);
    RUN_TEST(local_plus_equals_append);
    RUN_TEST(local_while_loop_counter);
    RUN_TEST(local_array_literal_builds_indexed_array);
    RUN_TEST(rt_declare_array_quoted_element_keeps_spaces);
    RUN_TEST(rt_declare_assoc_quoted_value_keeps_spaces);
    RUN_TEST(rt_declare_array_plain_elements_regression);
    RUN_TEST(rt_scalar_value_with_unit_separator_no_crash);
    RUN_TEST(rt_array_literal_word_to_nonassignment_command_rejected);
    RUN_TEST(rt_array_literal_word_quoted_is_a_literal);
    RUN_TEST(rt_array_literal_in_command_sub_rejected);
    RUN_TEST(rt_declare_array_quoted_paren_in_element);
    RUN_TEST(rt_declare_array_append_extends);
    RUN_TEST(rt_declare_array_append_to_absent_creates);
    RUN_TEST(rt_declare_array_plain_assign_replaces);
    RUN_TEST(rt_array_fn_scope_bare_whole);
    RUN_TEST(rt_array_fn_scope_bare_element);
    RUN_TEST(rt_array_fn_scope_bare_append);
    RUN_TEST(rt_array_fn_scope_arith_element);
    RUN_TEST(rt_array_fn_scope_declare_g_indexed);
    RUN_TEST(rt_array_fn_scope_declare_g_assoc);
    RUN_TEST(rt_array_fn_scope_scalar_promotion);
    RUN_TEST(rt_array_fn_scope_whole_replace_global);
    RUN_TEST(rt_array_fn_scope_nested_outer_local);
    RUN_TEST(rt_array_fn_scope_scalar_control_unchanged);
    RUN_TEST(rt_array_fn_scope_local_stays_local);
    RUN_TEST(rt_array_fn_scope_inplace_mutation);
    RUN_TEST(rt_array_fn_scope_local_shadow_not_clobbered);
    RUN_TEST(rt_array_fn_scope_readonly_refused);
    RUN_TEST(rt_array_fn_scope_readonly_scalar_not_clobbered);
    RUN_TEST(rt_array_fn_scope_readonly_scalar_element_refused);
    RUN_TEST(rt_array_fn_scope_readonly_array_append_refused);
    RUN_TEST(rt_array_fn_scope_read_a_readonly_refused);
    RUN_TEST(rt_unset_array_from_fn_no_uaf);
    RUN_TEST(rt_unset_scalar_from_fn_unsets_global);
    RUN_TEST(rt_unset_then_reassign_from_fn);
    RUN_TEST(rt_mapfile_from_fn_persists);
    RUN_TEST(rt_unset_local_global_survives);
    RUN_TEST(rt_unset_nonexistent_ok);
    RUN_TEST(rt_unset_repeated_local_preserves_global);
    RUN_TEST(rt_unset_readonly_scalar_refused);
    RUN_TEST(rt_unset_readonly_array_refused);
    RUN_TEST(rt_unset_readonly_assoc_refused);
    RUN_TEST(rt_unset_readonly_element_refused);
    RUN_TEST(rt_unset_readonly_global_from_fn_refused);
    RUN_TEST(rt_unset_readonly_universal_bash_mode);
    RUN_TEST(rt_unset_readonly_partial_continues);
    RUN_TEST(rt_unset_writable_still_works_control);
    RUN_TEST(rt_unset_readonly_nameref_target_refused);
    RUN_TEST(rt_env625_unset_drops_export);
    RUN_TEST(rt_env625_unset_shadow_preserves_global);
    RUN_TEST(rt_env625_promote_element_drops);
    RUN_TEST(rt_env625_promote_whole_assign_drops);
    RUN_TEST(rt_env625_promote_reada_drops);
    RUN_TEST(rt_env625_lush_refused_keeps_export);
    RUN_TEST(rt_env625_reexport_after_unset);
    RUN_TEST(rt_env625_nonexported_unset_noop);
    RUN_TEST(rt_env625_local_array_shadow_preserves_global_element);
    RUN_TEST(rt_env625_local_array_shadow_preserves_global_reada);
    RUN_TEST(rt_env625_promote_mapfile_drops);
    RUN_TEST(rt_sub627_indexed_at_rejected);
    RUN_TEST(rt_sub627_indexed_star_rejected);
    RUN_TEST(rt_sub627_indexed_append_rejected);
    RUN_TEST(rt_sub627_assoc_at_rejected);
    RUN_TEST(rt_sub627_scalar_not_promoted);
    RUN_TEST(rt_sub627_uniform_in_lush_mode);
    RUN_TEST(rt_sub627_readonly_precedence);
    RUN_TEST(rt_sub627_control_normal_index_ok);
    RUN_TEST(rt_sub627_control_normal_key_and_varkey_ok);
    RUN_TEST(rt_sub627_control_aggregate_read_ok);
    RUN_TEST(rt_sub627_array_literal_at_rejected);
    RUN_TEST(rt_sub627_array_literal_star_rejected);
    RUN_TEST(rt_sub627_array_literal_mixed_rejected_atomic);
    RUN_TEST(rt_sub627_assoc_append_rejected);
    RUN_TEST(rt_sub627_declare_assoc_literal_rejected);
    RUN_TEST(rt_sub627_declare_indexed_literal_rejected);
    RUN_TEST(rt_sub627_local_assoc_literal_rejected);
    RUN_TEST(rt_sub627_declare_control_literal_ok);
    RUN_TEST(rt_sub618_no_alias_plain);
    RUN_TEST(rt_sub618_no_alias_arith);
    RUN_TEST(rt_sub618_2p40_roundtrip);
    RUN_TEST(rt_sub618_2p31_native_not_rejected);
    RUN_TEST(rt_sub618_distinct_big_no_alias);
    RUN_TEST(rt_sub618_keys_and_length_full_width);
    RUN_TEST(rt_sub618_neg_oob_plain_reports);
    RUN_TEST(rt_sub618_neg_oob_arith_reports);
    RUN_TEST(rt_sub618_from_end_control);
    RUN_TEST(rt_sub618_declare_and_literal_big);
    RUN_TEST(rt_sub618_unset_big_index);
    RUN_TEST(rt_sub618_int64max_append_no_corruption);
    RUN_TEST(rt_sub618_empty_from_end_rejects);
    RUN_TEST(rt_sub642_read_dollar_forms_parity);
    RUN_TEST(rt_sub642_length_dollar);
    RUN_TEST(rt_sub642_write_dollar);
    RUN_TEST(rt_sub642_write_braces);
    RUN_TEST(rt_sub642_write_computed_and_cmdsub);
    RUN_TEST(rt_sub642_append_dollar);
    RUN_TEST(rt_sub642_array_literal_dollar);
    RUN_TEST(rt_sub642_control_no_dollar_unchanged);
    RUN_TEST(rt_sub642_element_modify_dollar);
    RUN_TEST(rt_sub642_large_index_dollar_write);
    RUN_TEST(rt_promote_lush_element_refused);
    RUN_TEST(rt_promote_lush_append_refused);
    RUN_TEST(rt_promote_lush_arith_refused);
    RUN_TEST(rt_promote_lush_unbound_creates_fresh);
    RUN_TEST(rt_promote_lush_declared_array_ok);
    RUN_TEST(rt_promote_lush_whole_assign_replaces);
    RUN_TEST(rt_promote_bash_element);
    RUN_TEST(rt_promote_bash_element_index0_overwrites);
    RUN_TEST(rt_promote_bash_sparse);
    RUN_TEST(rt_promote_bash_append);
    RUN_TEST(rt_promote_bash_element_plus_equal_reads_seed);
    RUN_TEST(rt_promote_bash_empty_scalar_seeds);
    RUN_TEST(rt_promote_bash_arith);
    RUN_TEST(rt_promote_bash_negative_index);
    RUN_TEST(rt_promote_bash_whole_replace_control);
    RUN_TEST(rt_promote_bash_whole_empty_clears_control);
    RUN_TEST(rt_promote_bash_read_a_replaces_control);
    RUN_TEST(rt_promote_bash_rematch_control);
    RUN_TEST(rt_promote_bash_function_scope);
    RUN_TEST(rt_promote_bash_readonly_scalar_refused);
    RUN_TEST(rt_promote_zsh_index2_preserves_base);
    RUN_TEST(rt_promote_zsh_index1_overwrites_base);
    RUN_TEST(rt_promote_strict_rematch_no_regress);
    RUN_TEST(rt_promote_readonly_arith_refused);
    RUN_TEST(rt_arith_neg_read_in_range);
    RUN_TEST(rt_arith_neg_write_in_range);
    RUN_TEST(rt_arith_neg_compound_and_incr);
    RUN_TEST(rt_arith_neg_oob_read_zero);
    RUN_TEST(rt_arith_neg_surface_consistency);
    RUN_TEST(rt_arith_neg_zsh_mode_still_rejected);
    RUN_TEST(rt_arith_neg_blast_radius_rematch);
    RUN_TEST(rt_arith_assoc_literal_key);
    RUN_TEST(rt_arith_assoc_compound_assign);
    RUN_TEST(rt_arith_assoc_key_not_arithmetic);
    RUN_TEST(rt_arith_assoc_nonlexable_key);
    RUN_TEST(rt_arith_assoc_numeric_key_literal);
    RUN_TEST(rt_arith_assoc_negativelike_key_literal);
    RUN_TEST(rt_arith_assoc_missing_key_zero);
    RUN_TEST(rt_arith_assoc_expanded_key);
    RUN_TEST(rt_arith_assoc_readonly_refused);
    RUN_TEST(rt_arith_assoc_zsh_mode);
    RUN_TEST(rt_arith_indexed_unaffected_by_lex_capture);
    RUN_TEST(rt_arith_assoc_empty_key);
    RUN_TEST(rt_arith_subscript_depth_capped);
    RUN_TEST(rt_local_array_append_extends);
    RUN_TEST(rt_readonly_array_append_relaxed);
    RUN_TEST(rt_declare_array_append_readonly_refused);
    RUN_TEST(rt_local_array_quoted_element_keeps_spaces);
    RUN_TEST(rt_readonly_relaxed_quoted_element_keeps_spaces);
    RUN_TEST(local_quoted_paren_value_stays_scalar);
    RUN_TEST(local_array_dies_with_function_scope);

    printf("\nRegression tests \xe2\x80\x94 Issue #48 (function trailing "
           "redirections at "
           "call):\n");
    RUN_TEST(function_redir_out_applied_at_call);
    RUN_TEST(function_redir_stderr_applied_at_call);
    RUN_TEST(function_redir_append_accumulates_across_calls);
    RUN_TEST(function_redir_keyword_form_applied_at_call);
    RUN_TEST(function_redir_input_applied_at_call);
    RUN_TEST(function_redir_does_not_break_normal_call);

    printf("\nArithmetic tests:\n");
    RUN_TEST(arithmetic_basic);
    RUN_TEST(arithmetic_multiply);
    RUN_TEST(arithmetic_variable);
    RUN_TEST(arithmetic_increment);
    RUN_TEST(arithmetic_brace_param_echo);
    RUN_TEST(arithmetic_brace_param_add);
    RUN_TEST(arithmetic_brace_param_two_operands);
    RUN_TEST(arithmetic_brace_param_assignment);
    RUN_TEST(arithmetic_brace_param_nested_parens);
    RUN_TEST(anon_function_cmdsub_still_routes);

    printf("\nSubshell and grouping tests:\n");
    RUN_TEST(subshell_isolation);
    RUN_TEST(brace_group);

    printf("\nTest command tests:\n");
    RUN_TEST(test_string_equal);
    RUN_TEST(test_string_not_equal);
    RUN_TEST(test_numeric_compare);
    RUN_TEST(test_string_empty);

    printf("\nBuiltin tests:\n");
    RUN_TEST(builtin_export);
    RUN_TEST(builtin_logout_outside_login_errors);
    RUN_TEST(builtin_logout_known_to_type_builtin);
    RUN_TEST(builtin_unset);
    RUN_TEST(builtin_readonly);
    RUN_TEST(rt_read_strips_leading_trailing_ifs_whitespace);
    RUN_TEST(rt_read_multi_var_trims_trailing_ws);
    RUN_TEST(rt_read_non_whitespace_ifs_not_trimmed);
    RUN_TEST(rt_printf_v_assigns_variable);
    RUN_TEST(rt_printf_without_v_still_prints);
    RUN_TEST(builtin_eval);
    RUN_TEST(builtin_shift);
    RUN_TEST(builtin_set_positional);
    RUN_TEST(rt_set_dashdash_function_scope);
    RUN_TEST(rt_set_dashdash_loop_scope_consistent);

    printf("\nCommand list tests:\n");
    RUN_TEST(command_list_semicolon);

    printf("\nBreak/continue tests:\n");
    RUN_TEST(loop_break);
    RUN_TEST(loop_continue);
    RUN_TEST(loop_break_outside_loop_errors);
    RUN_TEST(loop_continue_outside_loop_errors);
    RUN_TEST(loop_break_non_numeric_level_errors);
    RUN_TEST(loop_break_level_exceeds_depth_errors);
    RUN_TEST(loop_continue_non_numeric_level_errors);
    RUN_TEST(loop_break_2_unwinds_two_frames);
    RUN_TEST(loop_continue_2_skips_to_outer_next);
    RUN_TEST(loop_break_3_unwinds_three_frames);
    RUN_TEST(loop_break_1_is_same_as_plain_break);

    printf("\nPipeline tests:\n");
    RUN_TEST(pipeline_simple);
    RUN_TEST(pipeline_exit_status);
    RUN_TEST(pipeline_three_commands);

    printf("\nExtended test [[ ]] tests:\n");
    RUN_TEST(extended_test_string_equal);
    RUN_TEST(dbracket_quoted_metachars_are_literal);
    RUN_TEST(dbracket_quoted_var_pattern_is_literal);
    RUN_TEST(dbracket_unquoted_metachars_stay_active);
    RUN_TEST(dbracket_paren_patterns_and_regex);
    RUN_TEST(dbracket_curated_precedence_and_negation);
    RUN_TEST(dbracket_empty_is_false);
    RUN_TEST(dbracket_no_word_split_of_operands);
    RUN_TEST(extended_test_string_not_equal);
    RUN_TEST(extended_test_regex_match);
    RUN_TEST(extended_test_and);
    RUN_TEST(extended_test_or);
    RUN_TEST(extended_test_pattern_match);

    printf("\nParameter expansion tests:\n");
    RUN_TEST(param_default_value);
    RUN_TEST(param_alternate_value);
    RUN_TEST(param_string_length_counts_codepoints_not_bytes);
    RUN_TEST(param_string_length);
    RUN_TEST(param_substring_removal_prefix);
    RUN_TEST(param_substring_removal_suffix);
    RUN_TEST(param_substring_offset_length);
    RUN_TEST(rt_positional_param_slice);
    RUN_TEST(rt_zsh_modifier_path_parts);
    RUN_TEST(rt_zsh_modifier_case_and_quote);
    RUN_TEST(rt_zsh_modifier_substitute);
    RUN_TEST(rt_zsh_modifier_chain_and_nest);
    RUN_TEST(rt_zsh_modifier_off_in_bash_is_substring);
    RUN_TEST(rt_zsh_modifier_numeric_arg_still_substring);
    RUN_TEST(rt_zsh_param_indirect_P);
    RUN_TEST(param_pattern_substitution);
    RUN_TEST(param_global_substitution);

    printf("\nArray tests:\n");
    RUN_TEST(array_indexed_assignment);
    RUN_TEST(array_element_access);
    RUN_TEST(array_length);
    RUN_TEST(array_append);

    printf("\nCommand substitution tests:\n");
    RUN_TEST(command_substitution_syntax);
    RUN_TEST(command_substitution_exit_status);

    printf("\nSpecial variable tests:\n");
    RUN_TEST(special_var_question_mark);
    RUN_TEST(special_var_dollar_dollar);
    RUN_TEST(special_var_argc);

    printf("\nNested control structure tests:\n");
    RUN_TEST(nested_if);
    RUN_TEST(nested_loops);
    RUN_TEST(elif_chain);

    printf("\nNegation tests:\n");
    RUN_TEST(negation_command);

    printf("\nHere string tests:\n");
    RUN_TEST(here_string);

    printf("\nMore arithmetic tests:\n");
    RUN_TEST(arithmetic_comparison);
    RUN_TEST(arithmetic_assignment);
    RUN_TEST(arithmetic_ternary);

    printf("\nLocal variable tests:\n");
    RUN_TEST(local_variable_in_function);

    printf("\nRegression: deferred here-documents:\n");
    RUN_TEST(rt_heredoc_through_pipe);
    RUN_TEST(rt_heredoc_trailing_command);
    RUN_TEST(rt_heredoc_delimiter_nfc_vs_nfd);
    RUN_TEST(rt_heredoc_delimiter_nfd_vs_nfc);
    RUN_TEST(rt_heredoc_delimiter_unequal_when_actually_different);
    RUN_TEST(rt_assoc_key_nfc_nfd_collapse);
    RUN_TEST(rt_assoc_key_distinct_when_actually_different);
    RUN_TEST(rt_assoc_key_ascii_paths_unchanged);

    printf(
        "\nRegression: Unicode identifiers (FEATURE_UNICODE_IDENTIFIERS):\n");
    RUN_TEST(rt_unicode_ident_declare_and_expand_latin);
    RUN_TEST(rt_unicode_ident_declare_and_expand_greek);
    RUN_TEST(rt_unicode_ident_declare_and_expand_cyrillic);
    RUN_TEST(rt_unicode_ident_rejected_in_bash_mode);
    RUN_TEST(rt_unicode_ident_opt_in_from_bash_mode);
    RUN_TEST(rt_unicode_ident_ascii_paths_unchanged);
    RUN_TEST(rt_unicode_ident_invalid_start_still_rejected);
    RUN_TEST(rt_unicode_ident_arithmetic_bare);
    RUN_TEST(rt_unicode_ident_arithmetic_dollar);
    RUN_TEST(rt_unicode_ident_arithmetic_compound);
    RUN_TEST(rt_unicode_ident_arithmetic_positional_unchanged);
    RUN_TEST(rt_unicode_ident_function_name_lush);
    RUN_TEST(rt_unicode_ident_function_name_posix_rejected);
    RUN_TEST(rt_unicode_ident_function_name_posix_opt_in);
    RUN_TEST(rt_unicode_ident_alias_name_lush);
    RUN_TEST(rt_unicode_ident_alias_name_bash_rejected);
    RUN_TEST(rt_unicode_ident_alias_digit_leading_unchanged);
    RUN_TEST(rt_unicode_ident_alias_extension_chars_unchanged);
    RUN_TEST(rt_unicode_ident_array_subscript);
    RUN_TEST(rt_unicode_ident_array_length);
    RUN_TEST(rt_unicode_ident_array_vector_in_for);
    RUN_TEST(rt_unicode_ident_array_joined);
    RUN_TEST(rt_unicode_ident_assoc_array_subscript);
    RUN_TEST(rt_unicode_ident_kind_sigil);
    RUN_TEST(rt_unicode_ident_bare_array_expands_zsh_lush);
    RUN_TEST(rt_unicode_ident_indirect_expansion_target);
    RUN_TEST(rt_unicode_ident_indirect_expansion_pointer);
    RUN_TEST(rt_unicode_ident_zsh_bare_subscript);
    RUN_TEST(rt_unicode_ident_fd_allocation_var);
    RUN_TEST(rt_unicode_ident_nfd_passes_validation);
    RUN_TEST(rt_unicode_ident_nfd_reference_resolves);
    RUN_TEST(rt_unicode_ident_leading_mark_not_identifier);
    RUN_TEST(rt_unicode_ident_nfc_nfd_storage_collision);
    RUN_TEST(rt_unicode_ident_nfc_nfd_alias_collision);
    RUN_TEST(rt_unicode_ident_nfc_nfd_function_collision);
    RUN_TEST(rt_unicode_ident_nfc_array_assoc_collision);
    RUN_TEST(rt_unicode_ident_highlight_spans_full_name);
    RUN_TEST(rt_unicode_ident_mixed_script_detected);
    RUN_TEST(rt_unicode_ident_single_script_not_flagged);
    RUN_TEST(rt_unicode_ident_mixed_script_allowed_by_default);
    RUN_TEST(rt_unicode_ident_mixed_script_rejected_when_opted_in);
    RUN_TEST(rt_unicode_ident_mixed_script_single_script_ok_when_opted_in);
    RUN_TEST(rt_heredoc_in_if_body);
    RUN_TEST(rt_heredoc_loop_redirection);
    RUN_TEST(rt_heredoc_strip_tabs);
    RUN_TEST(rt_heredoc_quoted_delimiter);
    RUN_TEST(rt_heredoc_multiline_var_body);
    RUN_TEST(rt_heredoc_unterminated_error);

    printf("\nRegression: glob expansion and qualifiers:\n");
    RUN_TEST(rt_glob_for_array_qualifiers);

    printf("\nRegression: return/case control flow:\n");
    RUN_TEST(rt_return_from_for_loop);
    RUN_TEST(rt_typed_fn_scalar_return);
    RUN_TEST(rt_typed_fn_scalar_param);
    RUN_TEST(rt_typed_fn_list_param);
    RUN_TEST(rt_typed_fn_arity_mismatch);
    RUN_TEST(rt_typed_fn_kind_mismatch_arg);
    RUN_TEST(rt_typed_fn_void_called_via_let);
    RUN_TEST(rt_typed_fn_void_no_return);
    RUN_TEST(rt_pe_catalog_join_flag_on_list);
    RUN_TEST(rt_pe_catalog_case_mod_flag_per_element);
    RUN_TEST(rt_pe_catalog_sort_flag_on_list);
    RUN_TEST(rt_pe_catalog_keys_flag_on_scalar);
    RUN_TEST(rt_pe_catalog_values_flag_on_scalar);
    RUN_TEST(rt_pe_catalog_kv_flag_on_scalar);
    RUN_TEST(rt_pe_catalog_join_flag_on_scalar);
    RUN_TEST(rt_pe_catalog_conditional_on_bare_list);
    RUN_TEST(rt_pe_catalog_case_mod_on_bare_list);
    RUN_TEST(rt_pe_catalog_pattern_strip_on_bare_list);
    RUN_TEST(rt_pe_catalog_substring_on_bare_list);
    RUN_TEST(rt_pe_catalog_at_transform_on_bare_list);
    RUN_TEST(rt_pe_at_attr_query_on_map);
    RUN_TEST(rt_pe_at_attr_query_on_list);
    RUN_TEST(rt_pe_at_attr_query_scalar_unaffected);
    RUN_TEST(rt_declare_combined_flags_attributes);
    RUN_TEST(rt_lle_feature_bridge_enumerates_matrix);
    RUN_TEST(rt_setopt_completion_lists_matrix_features);
    RUN_TEST(rt_pe_at_value_transform_on_map_still_errors);
    RUN_TEST(rt_declare_p_named_assoc_prints_elements);
    RUN_TEST(rt_declare_p_named_indexed_prints_elements);
    RUN_TEST(rt_pe_catalog_conditional_on_map);
    RUN_TEST(rt_pe_catalog_scalar_slice_still_works);
    RUN_TEST(rt_pe_catalog_per_element_case_mod_via_slice);
    RUN_TEST(rt_pe_catalog_per_element_prefix_strip_via_slice);
    RUN_TEST(rt_pe_catalog_per_element_suffix_strip_via_slice);
    RUN_TEST(rt_pe_catalog_per_element_replace_first_via_slice);
    RUN_TEST(rt_pe_catalog_per_element_replace_all_via_slice);
    RUN_TEST(rt_pe_catalog_per_element_at_q_via_slice);
    RUN_TEST(rt_pe_vectorized_case_transform);
    RUN_TEST(rt_pipeline_three_stages);
    RUN_TEST(rt_pipeline_four_stages);
    RUN_TEST(rt_pipeline_pipestatus_three_stages);
    RUN_TEST(rt_pipeline_lush_pipestatus_three_stages);
    RUN_TEST(rt_pipestatus_simple_command_and_opacity);
    RUN_TEST(rt_pipeline_pipefail_rightmost_nonzero);
    RUN_TEST(rt_pipeline_pipefail_all_succeed);
    RUN_TEST(rt_pipeline_diagnostic_per_stage_errors);
    RUN_TEST(rt_pipeline_diagnostic_silent_on_success);
    RUN_TEST(rt_pe_catalog_scalar_conditional_still_works);
    RUN_TEST(rt_inspect_scalar_at_cursor);
    RUN_TEST(rt_inspect_braced_name);
    RUN_TEST(rt_inspect_strict_past_closing_brace);
    RUN_TEST(rt_inspect_cursor_on_dollar);
    RUN_TEST(rt_inspect_cursor_on_closing_brace);
    RUN_TEST(rt_inspect_list_quoted);
    RUN_TEST(rt_inspect_unset_variable);
    RUN_TEST(rt_inspect_no_ref_under_cursor);
    RUN_TEST(rt_inspect_cursor_mid_identifier);
    RUN_TEST(rt_sigil_at_on_scalar);
    RUN_TEST(rt_sigil_at_on_list);
    RUN_TEST(rt_sigil_at_on_map);
    RUN_TEST(rt_sigil_pair_on_list);
    RUN_TEST(rt_sigil_pair_on_map);
    RUN_TEST(rt_sigil_pair_on_scalar_type_mismatch);
    RUN_TEST(rt_sigil_unset_name_contributes_nothing);
    RUN_TEST(rt_sigil_unset_in_array_literal_contributes_nothing);
    RUN_TEST(rt_sigil_scalar_still_type_error);
    RUN_TEST(rt_sigil_compat_user_at_host);
    RUN_TEST(rt_sigil_compat_at_invalid_identifier);
    RUN_TEST(rt_sigil_literal_in_double_quotes);
    RUN_TEST(rt_printf_percent_s_lush_mode);
    RUN_TEST(rt_user_at_host_quoted_literal);
    RUN_TEST(rt_sigil_off_in_bash_mode);
    RUN_TEST(rt_test_o_errexit_reflects_set_e);
    RUN_TEST(rt_test_o_noop_alias_records_state);
    RUN_TEST(rt_bindkey_bind_then_query_round_trips);
    RUN_TEST(rt_bindkey_list_after_multiple_binds);
    RUN_TEST(rt_bindkey_query_unbound_key_returns_nonzero);
    RUN_TEST(rt_bindkey_remove_then_query_returns_nonzero);
    RUN_TEST(rt_zle_register_then_list);
    RUN_TEST(rt_zle_register_with_body_then_L_form);
    RUN_TEST(rt_zle_delete_then_list_empty);
    RUN_TEST(rt_logical_op_at_eol_continues_statement);
    RUN_TEST(rt_pipe_at_eol_continues_statement);
    RUN_TEST(rt_extended_test_empty_lhs_inequality);
    RUN_TEST(rt_extended_test_single_equals_alias);
    RUN_TEST(rt_test_equality_under_nfc_equivalence);
    RUN_TEST(rt_test_inequality_under_nfc_when_actually_different);
    RUN_TEST(rt_test_ascii_paths_unchanged);
    RUN_TEST(rt_dollar_plus_is_set_test);
    RUN_TEST(rt_dollar_plus_commands_path_lookup);
    RUN_TEST(rt_unfunction_removes_function);
    RUN_TEST(rt_multi_name_function_definition);
    RUN_TEST(rt_zstyle_set_then_query_t_true);
    RUN_TEST(rt_zstyle_set_then_query_s_get);
    RUN_TEST(rt_zstyle_T_treats_unset_as_true);
    RUN_TEST(rt_zstyle_delete_removes_entry);
    RUN_TEST(rt_typeset_H_flag_accepted);
    RUN_TEST(rt_typeset_U_flag_accepted);
    RUN_TEST(rt_test_o_unknown_option_returns_false);
    RUN_TEST(rt_typed_fn_lexical_scope);
    RUN_TEST(rt_return_from_while_loop);
    RUN_TEST(rt_case_arm_runs_all);
    RUN_TEST(rt_case_arm_return_status);

    printf("\nRegression: printf flags:\n");
    RUN_TEST(rt_printf_flags);
    RUN_TEST(rt_echo_xsi_escapes);
    RUN_TEST(rt_printf_b_conversion);
    RUN_TEST(rt_printf_format_octal_and_no_reparse);

    printf("\nRegression: brace expansion:\n");
    RUN_TEST(rt_brace_nested);
    RUN_TEST(rt_brace_cartesian);
    RUN_TEST(rt_brace_in_array_init);

    printf("\nRegression: array-element parameter expansion:\n");
    RUN_TEST(rt_array_elem_default_missing);
    RUN_TEST(rt_array_elem_default_present);
    RUN_TEST(rt_array_elem_alt_present);
    RUN_TEST(rt_array_elem_alt_missing);
    RUN_TEST(rt_array_elem_strip_prefix_longest);
    RUN_TEST(rt_array_elem_strip_suffix);
    RUN_TEST(rt_array_elem_upper_all);
    RUN_TEST(rt_array_elem_lower_all);
    RUN_TEST(rt_array_elem_replace_all);
    RUN_TEST(rt_array_elem_substring);
    RUN_TEST(rt_array_elem_transform_quote);
    RUN_TEST(rt_array_elem_indexed_upper);
    RUN_TEST(rt_array_elem_assign_persists);
    RUN_TEST(rt_array_elem_assign_readonly_refused);
    RUN_TEST(rt_undecl_elem_default);
    RUN_TEST(rt_undecl_elem_alt_empty);
    RUN_TEST(rt_undecl_elem_strip_prefix_empty);
    RUN_TEST(rt_undecl_elem_high_index_default);
    RUN_TEST(rt_undecl_elem_assign_creates_array);
    RUN_TEST(rt_undecl_elem_assign_at_index);
    RUN_TEST(rt_scalar_slice_operator_out_of_range);
    RUN_TEST(rt_scalar_slice_still_works);
    RUN_TEST(rt_array_elem_assign_zsh_index0_skipped);

    printf("\nRegression: POSIX null-word removal (#517):\n");
    RUN_TEST(rt_nullword_empty_command);
    RUN_TEST(rt_nullword_unset_command);
    RUN_TEST(rt_nullword_quoted_empty_command_kept);
    RUN_TEST(rt_nullword_command_promotion);
    RUN_TEST(rt_nullword_arg_removed);
    RUN_TEST(rt_nullword_quoted_arg_kept);
    RUN_TEST(rt_nullword_partial_word_kept);
    RUN_TEST(rt_nullword_no_split_curation_preserved);
    RUN_TEST(rt_nullword_for_zero_iterations);
    RUN_TEST(rt_nullword_for_quoted_empty_one_iter);
    RUN_TEST(rt_nullword_array_zero_elements);
    RUN_TEST(rt_nullword_array_mixed_kept);
    RUN_TEST(rt_nullword_in_function_body);

    printf("\nRegression: quoted star IFS-join (#518):\n");
    RUN_TEST(rt_star_join_is_one_word);
    RUN_TEST(rt_star_join_default_ifs_space);
    RUN_TEST(rt_star_join_custom_ifs);
    RUN_TEST(rt_star_join_empty_ifs_concatenates);
    RUN_TEST(rt_star_join_array_custom_ifs);
    RUN_TEST(rt_star_join_braced_positional);
    RUN_TEST(rt_braced_at_function_scope);
    RUN_TEST(rt_braced_star_function_scope);
    RUN_TEST(rt_braced_at_function_scope_empty);
    RUN_TEST(rt_braced_at_function_scope_nested);
    RUN_TEST(rt_braced_at_function_scope_ifs_curated);
    RUN_TEST(rt_braced_at_function_scope_vector_still_spreads);
    RUN_TEST(rt_star_join_empty_is_one_word);
    RUN_TEST(rt_star_at_still_explodes);
    RUN_TEST(rt_star_array_at_still_explodes);
    RUN_TEST(rt_star_unquoted_not_joined);

    printf("\nRegression: array element slice ${a[*]:off:len} (#530):\n");
    RUN_TEST(rt_slice_star_basic);
    RUN_TEST(rt_slice_star_offset);
    RUN_TEST(rt_slice_star_to_end);
    RUN_TEST(rt_slice_star_custom_ifs);
    RUN_TEST(rt_slice_star_default_op_not_slice);
    RUN_TEST(rt_slice_star_alt_op_not_slice);
    RUN_TEST(rt_slice_star_plus_numeric_op);
    RUN_TEST(rt_slice_star_minus_numeric_op);
    RUN_TEST(rt_slice_at_numeric_still_explodes);
    RUN_TEST(rt_slice_at_scalar_op_is_type_error);

    printf("\nRegression: positional vector in command position (#529):\n");
    RUN_TEST(rt_cmdvec_positional_at_expands);
    RUN_TEST(rt_cmdvec_positional_at_unquoted_expands);
    RUN_TEST(rt_cmdvec_multiword_boundaries);
    RUN_TEST(rt_cmdvec_braced_positional_expands);
    RUN_TEST(rt_cmdvec_empty_at_is_null_command);
    RUN_TEST(rt_cmdvec_star_joins_one_word);
    RUN_TEST(rt_cmdvec_named_array_keeps_type_error);
    RUN_TEST(rt_cmdvec_flag_form_not_spread);
    RUN_TEST(rt_cmdvec_normal_command_unchanged);

    printf("\nRegression: scalar $@ / keys join on IFS[0] (#518 tail):\n");
    RUN_TEST(rt_at_scalar_join_custom_ifs);
    RUN_TEST(rt_at_scalar_join_empty_ifs);
    RUN_TEST(rt_at_scalar_join_default_ifs);
    RUN_TEST(rt_at_scalar_join_function_scope);
    RUN_TEST(rt_keys_star_join_custom_ifs);
    RUN_TEST(rt_keys_at_scalar_slot_type_error);
    RUN_TEST(rt_keys_at_whole_word_explodes);

    printf("\nRegression: POSIX character classes:\n");
    RUN_TEST(rt_char_class_space);
    RUN_TEST(rt_char_class_negated);
    RUN_TEST(rt_char_class_digit);
    RUN_TEST(rt_char_class_upper);

    printf("\nRegression: readonly enforcement:\n");
    RUN_TEST(rt_readonly_blocks_same_scope_reassign);
    RUN_TEST(rt_readonly_assign_aborts_or_list);
    RUN_TEST(rt_readonly_assign_aborts_and_list);
    RUN_TEST(rt_readonly_assign_diag_bypasses_redirect);
    RUN_TEST(rt_readonly_normal_or_still_fires);
    RUN_TEST(rt_readonly_assign_posix_mode_exits);
    RUN_TEST(rt_readonly_assign_zsh_mode_exits);
    RUN_TEST(rt_readonly_assign_setopt_toggles_exit);
    RUN_TEST(rt_readonly_blocks_via_readonly_keyword);
    RUN_TEST(rt_readonly_blocks_across_function_scope);
    RUN_TEST(rt_readonly_blocks_via_declare_r);
    RUN_TEST(rt_readonly_blocks_array_element_via_readonly_keyword);
    RUN_TEST(rt_readonly_blocks_array_element_via_declare_ar);
    RUN_TEST(rt_readonly_blocks_associative_array_element);
    RUN_TEST(rt_readonly_listing_includes_marked);
    RUN_TEST(rt_readonly_promotes_existing_var);

    printf("\nRegression: declare -l / declare -u case attribute:\n");
    RUN_TEST(rt_declare_l_initial_value_folded);
    RUN_TEST(rt_declare_l_subsequent_assignment_folded);
    RUN_TEST(rt_declare_l_unicode_fold);
    RUN_TEST(rt_declare_l_promotes_existing);
    RUN_TEST(rt_declare_u_initial_value_folded);
    RUN_TEST(rt_declare_u_subsequent_assignment_folded);
    RUN_TEST(rt_declare_u_unicode_fold);
    RUN_TEST(rt_declare_no_value_preserves_existing);

    /// Cleanup global symbol table and positional-parameter vector
    free_global_symtable();
    free_shell_argv();

    return TEST_RESULT();
}
