/**
 * @file test_symtable.c
 * @brief Unit tests for symbol table
 *
 * Tests the symbol table including:
 * - Variable operations (set, get, unset)
 * - Scope management (push, pop, nesting)
 * - Arrays (indexed and associative)
 * - Namerefs and exports
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "symtable.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

/// Test framework macros

/* ============================================================================
 * MANAGER LIFECYCLE TESTS
 * ============================================================================
 */

TEST(manager_new) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new should return non-NULL");
    symtable_manager_free(mgr);
}

TEST(manager_initial_level) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    size_t level = symtable_current_level(mgr);
    ASSERT_EQ(level, 0, "Initial scope level should be 0 (global)");

    symtable_manager_free(mgr);
}

/* ============================================================================
 * BASIC VARIABLE TESTS
 * ============================================================================
 */

TEST(set_get_variable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    int result = symtable_set_var(mgr, "FOO", "bar", SYMVAR_NONE);
    ASSERT_EQ(result, 0, "symtable_set_var should succeed");

    char *value = symtable_get_var(mgr, "FOO");
    ASSERT_NOT_NULL(value, "symtable_get_var should return value");
    ASSERT_STR_EQ(value, "bar", "Variable value mismatch");
    free(value);

    symtable_manager_free(mgr);
}

TEST(value_with_unit_separator_roundtrips) {
    /// A shell value may legally contain 0x1F, the byte used internally as
    /// the value/metadata separator. Deserialization must locate the
    /// metadata trailer from the right so the value round-trips byte-for-
    /// byte. Left-scanning previously truncated the value at the first 0x1F
    /// and, when a digit followed, mis-typed the binding as SYMVAR_ARRAY and
    /// dereferenced the truncated value as a pointer (issue #550).
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    /// `1<US>2` -- the exact crash shape (the digit after <US> parsed as
    /// type 2 == SYMVAR_ARRAY).
    const char *raw = "1\x1f"
                      "2";
    ASSERT_EQ(symtable_set_var(mgr, "US1", raw, SYMVAR_NONE), 0,
              "set_var with a 0x1F value should succeed");
    char *v = symtable_get_var(mgr, "US1");
    ASSERT_NOT_NULL(v, "get_var should return the value");
    ASSERT_EQ((int)strlen(v), 3, "value length must be 3 (0x1F preserved)");
    ASSERT_EQ(memcmp(v, raw, 3), 0, "value bytes must round-trip exactly");
    free(v);

    /// Multiple and trailing separators.
    const char *raw2 = "p\x1f"
                       "q\x1f\x1f";
    ASSERT_EQ(symtable_set_var(mgr, "US2", raw2, SYMVAR_NONE), 0,
              "set_var US2 should succeed");
    char *v2 = symtable_get_var(mgr, "US2");
    ASSERT_NOT_NULL(v2, "get_var US2 should return the value");
    ASSERT_EQ((int)strlen(v2), 5, "US2 length must be 5");
    ASSERT_EQ(memcmp(v2, raw2, 5), 0, "US2 bytes must round-trip exactly");
    free(v2);

    symtable_manager_free(mgr);
}

TEST(set_overwrite_variable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_set_var(mgr, "FOO", "first", SYMVAR_NONE);
    symtable_set_var(mgr, "FOO", "second", SYMVAR_NONE);

    char *value = symtable_get_var(mgr, "FOO");
    ASSERT_STR_EQ(value, "second", "Variable should be overwritten");
    free(value);

    symtable_manager_free(mgr);
}

TEST(get_nonexistent_variable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    char *value = symtable_get_var(mgr, "NONEXISTENT");
    ASSERT_NULL(value, "Non-existent variable should return NULL");

    symtable_manager_free(mgr);
}

TEST(unset_variable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_set_var(mgr, "FOO", "bar", SYMVAR_NONE);

    int result = symtable_unset_var(mgr, "FOO");
    ASSERT_EQ(result, 0, "symtable_unset_var should succeed");

    char *value = symtable_get_var(mgr, "FOO");
    ASSERT_NULL(value, "Unset variable should return NULL");

    symtable_manager_free(mgr);
}

TEST(var_exists) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    ASSERT(!symtable_var_exists(mgr, "FOO"),
           "Variable should not exist initially");

    symtable_set_var(mgr, "FOO", "bar", SYMVAR_NONE);
    ASSERT(symtable_var_exists(mgr, "FOO"), "Variable should exist after set");

    symtable_unset_var(mgr, "FOO");
    ASSERT(!symtable_var_exists(mgr, "FOO"),
           "Variable should not exist after unset");

    symtable_manager_free(mgr);
}

/* ============================================================================
 * SCOPE MANAGEMENT TESTS
 * ============================================================================
 */

TEST(push_pop_scope) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    ASSERT_EQ(symtable_current_level(mgr), 0, "Initial level should be 0");

    int result = symtable_push_scope(mgr, SCOPE_FUNCTION, "test_func");
    ASSERT_EQ(result, 0, "symtable_push_scope should succeed");
    ASSERT_EQ(symtable_current_level(mgr), 1, "Level should be 1 after push");

    result = symtable_pop_scope(mgr);
    ASSERT_EQ(result, 0, "symtable_pop_scope should succeed");
    ASSERT_EQ(symtable_current_level(mgr), 0, "Level should be 0 after pop");

    symtable_manager_free(mgr);
}

TEST(nested_scopes) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_push_scope(mgr, SCOPE_FUNCTION, "outer");
    ASSERT_EQ(symtable_current_level(mgr), 1, "Level should be 1");

    symtable_push_scope(mgr, SCOPE_LOOP, "inner");
    ASSERT_EQ(symtable_current_level(mgr), 2, "Level should be 2");

    symtable_pop_scope(mgr);
    ASSERT_EQ(symtable_current_level(mgr), 1, "Level should be 1 after pop");

    symtable_pop_scope(mgr);
    ASSERT_EQ(symtable_current_level(mgr), 0,
              "Level should be 0 after second pop");

    symtable_manager_free(mgr);
}

TEST(local_variable_shadowing) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    /// Set global variable
    symtable_set_var(mgr, "X", "global", SYMVAR_NONE);

    /// Push function scope and set local
    symtable_push_scope(mgr, SCOPE_FUNCTION, "func");
    symtable_set_local_var(mgr, "X", "local");

    /// Local should shadow global
    char *value = symtable_get_var(mgr, "X");
    ASSERT_STR_EQ(value, "local", "Local should shadow global");
    free(value);

    /// Pop scope - global should be visible again
    symtable_pop_scope(mgr);
    value = symtable_get_var(mgr, "X");
    ASSERT_STR_EQ(value, "global", "Global should be visible after pop");
    free(value);

    symtable_manager_free(mgr);
}

TEST(in_function_scope) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    ASSERT(!symtable_in_function_scope(mgr),
           "Should not be in function scope initially");

    symtable_push_scope(mgr, SCOPE_FUNCTION, "func");
    ASSERT(symtable_in_function_scope(mgr), "Should be in function scope");

    symtable_push_scope(mgr, SCOPE_LOOP, "loop");
    ASSERT(symtable_in_function_scope(mgr),
           "Should still be in function scope (nested)");

    symtable_pop_scope(mgr);
    symtable_pop_scope(mgr);
    ASSERT(!symtable_in_function_scope(mgr),
           "Should not be in function scope after pop");

    symtable_manager_free(mgr);
}

/* ============================================================================
 * VARIABLE FLAGS TESTS
 * ============================================================================
 */

TEST(exported_variable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_set_var(mgr, "FOO", "bar", SYMVAR_NONE);

    int result = symtable_export_var(mgr, "FOO");
    ASSERT_EQ(result, 0, "symtable_export_var should succeed");

    symvar_flags_t flags = symtable_get_flags(mgr, "FOO");
    ASSERT(flags & SYMVAR_EXPORTED, "Variable should have EXPORTED flag");

    symtable_manager_free(mgr);
}

TEST(readonly_variable) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_set_var(mgr, "CONST", "value", SYMVAR_READONLY);

    symvar_flags_t flags = symtable_get_flags(mgr, "CONST");
    ASSERT(flags & SYMVAR_READONLY, "Variable should have READONLY flag");

    /// Overwriting a readonly variable is refused and leaves it unchanged.
    int result = symtable_set_var(mgr, "CONST", "new_value", SYMVAR_NONE);
    ASSERT_EQ(result, SYMTABLE_ERR_READONLY,
              "overwrite of a readonly variable should be rejected");

    char *value = symtable_get_var(mgr, "CONST");
    ASSERT_NOT_NULL(value, "CONST should still be set");
    ASSERT_STR_EQ(value, "value",
                  "readonly value unchanged after blocked write");
    free(value);

    symtable_manager_free(mgr);
}

TEST(get_environ) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_set_var(mgr, "VAR1", "value1", SYMVAR_EXPORTED);
    symtable_set_var(mgr, "VAR2", "value2", SYMVAR_EXPORTED);
    symtable_set_var(mgr, "VAR3", "not_exported", SYMVAR_NONE);

    char **env = symtable_get_environ(mgr);
    ASSERT_NOT_NULL(env, "symtable_get_environ should return non-NULL");

    /// Verify exported vars are present
    bool found_var1 = false, found_var2 = false, found_var3 = false;
    for (int i = 0; env[i] != NULL; i++) {
        if (strstr(env[i], "VAR1=value1"))
            found_var1 = true;
        if (strstr(env[i], "VAR2=value2"))
            found_var2 = true;
        if (strstr(env[i], "VAR3="))
            found_var3 = true;
    }

    ASSERT(found_var1, "VAR1 should be in environ");
    ASSERT(found_var2, "VAR2 should be in environ");
    ASSERT(!found_var3, "VAR3 should NOT be in environ (not exported)");

    symtable_free_environ(env);
    symtable_manager_free(mgr);
}

/* ============================================================================
 * NAMEREF TESTS
 * ============================================================================
 */

TEST(nameref_basic) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    /// Set target variable
    symtable_set_var(mgr, "TARGET", "hello", SYMVAR_NONE);

    /// Create nameref pointing to TARGET
    int result = symtable_set_nameref(mgr, "REF", "TARGET", SYMVAR_NONE);
    ASSERT_EQ(result, 0, "symtable_set_nameref should succeed");

    /// Accessing REF should give TARGET's value
    char *value = symtable_get_var(mgr, "REF");
    ASSERT_STR_EQ(value, "hello", "Nameref should resolve to target value");
    free(value);

    symtable_manager_free(mgr);
}

TEST(nameref_is_nameref) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_set_var(mgr, "NORMAL", "value", SYMVAR_NONE);
    symtable_set_nameref(mgr, "REF", "TARGET", SYMVAR_NONE);

    ASSERT(!symtable_is_nameref(mgr, "NORMAL"),
           "NORMAL should not be a nameref");
    ASSERT(symtable_is_nameref(mgr, "REF"), "REF should be a nameref");

    symtable_manager_free(mgr);
}

TEST(nameref_resolve) {
    symtable_manager_t *mgr = symtable_manager_new();
    ASSERT_NOT_NULL(mgr, "symtable_manager_new failed");

    symtable_set_nameref(mgr, "REF", "TARGET", SYMVAR_NONE);

    const char *resolved = symtable_resolve_nameref(mgr, "REF", 10);
    ASSERT_NOT_NULL(resolved,
                    "symtable_resolve_nameref should return target name");
    ASSERT_STR_EQ(resolved, "TARGET", "Should resolve to TARGET");
    free((char *)resolved); /// Resolved name is owned by the caller

    symtable_manager_free(mgr);
}

/* ============================================================================
 * ARRAY TESTS (if supported)
 * ============================================================================
 */

TEST(array_create) {
    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "symtable_array_create should succeed");
    ASSERT(!arr->is_associative, "Should be indexed array");

    symtable_array_free(arr);
}

TEST(array_indexed_operations) {
    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "symtable_array_create failed");

    int result = symtable_array_set_index(arr, 0, "first");
    ASSERT_EQ(result, 0, "Set index 0 should succeed");

    result = symtable_array_set_index(arr, 2, "third");
    ASSERT_EQ(result, 0, "Set index 2 should succeed (sparse)");

    const char *val = symtable_array_get_index(arr, 0);
    ASSERT_STR_EQ(val, "first", "Index 0 value mismatch");

    val = symtable_array_get_index(arr, 2);
    ASSERT_STR_EQ(val, "third", "Index 2 value mismatch");

    val = symtable_array_get_index(arr, 1);
    ASSERT_NULL(val, "Index 1 should be NULL (sparse)");

    symtable_array_free(arr);
}

TEST(array_index_int64_no_truncation) {
    /// #618: a subscript beyond INT_MAX is a native 64-bit sparse key -- stored
    /// and read back at its true index, never truncated to alias a low element.
    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "symtable_array_create failed");

    ASSERT_EQ(symtable_array_set_index(arr, 0, "zero"), 0, "set 0");
    ASSERT_EQ(symtable_array_set_index(arr, 4294967296LL, "big"), 0,
              "set 2^32 should succeed");
    ASSERT_EQ(symtable_array_set_index(arr, 1099511627776LL, "huge"), 0,
              "set 2^40 should succeed");

    ASSERT_STR_EQ(symtable_array_get_index(arr, 4294967296LL), "big",
                  "2^32 round-trips");
    ASSERT_STR_EQ(symtable_array_get_index(arr, 1099511627776LL), "huge",
                  "2^40 round-trips");
    /// No aliasing: index 0 keeps its own value (the old (int) cast of 2^32
    /// would have overwritten it).
    ASSERT_STR_EQ(symtable_array_get_index(arr, 0), "zero", "0 not aliased");
    ASSERT_NULL(symtable_array_get_index(arr, 5), "low alias target unset");

    symtable_array_free(arr);

    /// A from-end negative that resolves below 0 is out of range. Use a small
    /// array so the negative genuinely underflows (on the sparse array above,
    /// -999 would resolve to a large positive index off max_index).
    array_value_t *small = symtable_array_create(false);
    ASSERT_NOT_NULL(small, "symtable_array_create failed");
    ASSERT_EQ(symtable_array_set_index(small, 0, "a"), 0, "set 0");
    ASSERT_EQ(symtable_array_set_index(small, 1, "b"), 0, "set 1");
    ASSERT_EQ(symtable_array_set_index(small, -1, "Z"), 0,
              "from-end -1 resolves to last (valid)");
    ASSERT_STR_EQ(symtable_array_get_index(small, 1), "Z", "-1 hit index 1");
    ASSERT_EQ(symtable_array_set_index(small, -999, "x"), -1,
              "deep negative is out of range");
    /// From-end on an empty array is out of range (count==0 base arm).
    array_value_t *empty = symtable_array_create(false);
    ASSERT_NOT_NULL(empty, "symtable_array_create failed");
    ASSERT_EQ(symtable_array_set_index(empty, -1, "x"), -1,
              "from-end on empty is out of range");
    ASSERT_NULL(symtable_array_get_index(empty, -1),
                "from-end read on empty is NULL");
    symtable_array_free(empty);
    symtable_array_free(small);

    /// INT64_MAX is a valid key, but appending past it is refused (no
    /// max_index+1 overflow / wrap to a low index).
    array_value_t *edge = symtable_array_create(false);
    ASSERT_NOT_NULL(edge, "symtable_array_create failed");
    ASSERT_EQ(symtable_array_set_index(edge, INT64_MAX, "max"), 0,
              "INT64_MAX is a valid index");
    ASSERT_EQ(symtable_array_append(edge, "ovf"), -1,
              "append past INT64_MAX is refused");
    ASSERT_NULL(symtable_array_get_index(edge, 0),
                "no wrap-to-0 corruption on append past max");
    ASSERT_STR_EQ(symtable_array_get_index(edge, INT64_MAX), "max",
                  "INT64_MAX element intact");
    symtable_array_free(edge);
}

TEST(array_append) {
    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "symtable_array_create failed");

    symtable_array_append(arr, "a");
    symtable_array_append(arr, "b");
    symtable_array_append(arr, "c");

    size_t len = symtable_array_length(arr);
    ASSERT_EQ(len, 3, "Array length should be 3");

    symtable_array_free(arr);
}

TEST(array_associative) {
    array_value_t *arr = symtable_array_create(true);
    ASSERT_NOT_NULL(arr, "symtable_array_create failed");
    ASSERT(arr->is_associative, "Should be associative array");

    int result = symtable_array_set_assoc(arr, "key1", "value1");
    ASSERT_EQ(result, 0, "Set assoc key should succeed");

    result = symtable_array_set_assoc(arr, "key2", "value2");
    ASSERT_EQ(result, 0, "Set second assoc key should succeed");

    const char *val = symtable_array_get_assoc(arr, "key1");
    ASSERT_STR_EQ(val, "value1", "Assoc key1 value mismatch");

    val = symtable_array_get_assoc(arr, "key2");
    ASSERT_STR_EQ(val, "value2", "Assoc key2 value mismatch");

    val = symtable_array_get_assoc(arr, "nonexistent");
    ASSERT_NULL(val, "Non-existent key should return NULL");

    symtable_array_free(arr);
}

/* ============================================================================
 * UNIFIED VALUE VIEW TESTS (SEMANTICS section 3 first-class values)
 * ============================================================================
 */

/// Issue #163: scalar assignment over an existing array binding must
/// release the array_value_t backing memory. The previous serialized
/// value was a hex-encoded pointer string; ht_strstr_insert overwrites
/// it as ASCII text, leaving no C pointer to the array. Without the
/// hand-off in symtable_set_var the array becomes unreachable -- the
/// CI memcheck-linux job picks it up as "definitely lost".
///
/// This test exercises the type-switch path so a regression that
/// reintroduces the leak still crashes or misbehaves observably
/// (kind-flip stops working, lookup returns stale array, etc.). The
/// quantitative leak signal stays with the valgrind CI job.
TEST(array_scalar_overwrite_releases_backing) {
    init_symtable();
    symtable_manager_t *mgr = symtable_get_global_manager();
    if (!mgr) {
        printf(
            "    (Skipped - global manager not initialized in test context)\n");
        return;
    }

    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "array_create");
    symtable_array_append(arr, "one");
    symtable_array_append(arr, "two");
    symtable_array_append(arr, "three");
    int rc = symtable_set_array("lush_163_arr", arr);
    ASSERT_EQ(rc, 0, "set_array should store the binding");

    /// Confirm the array round-trips before we overwrite it.
    array_value_t *probe = symtable_get_array("lush_163_arr");
    ASSERT_NOT_NULL(probe, "array binding lookup before overwrite");
    ASSERT_EQ(symtable_array_length(probe), 3, "length 3 before overwrite");

    /// Scalar assignment over the same name -- the path that used to
    /// leak the array_value_t.
    rc = symtable_set_var(mgr, "lush_163_arr", "scalar_value", SYMVAR_NONE);
    ASSERT_EQ(rc, 0, "scalar overwrite should succeed");

    /// The binding is now a scalar; array lookup must return NULL.
    probe = symtable_get_array("lush_163_arr");
    ASSERT_NULL(probe, "array lookup after scalar overwrite returns NULL");

    char *scalar = symtable_get_var(mgr, "lush_163_arr");
    ASSERT_NOT_NULL(scalar, "scalar lookup after overwrite");
    ASSERT_STR_EQ(scalar, "scalar_value", "scalar value matches");
    free(scalar);

    symtable_unset_var(mgr, "lush_163_arr");
}

TEST(value_view_scalar_lookup) {
    /// symtable_lookup queries the global manager. Ensure it is up;
    /// init_symtable() is a no-op if already initialized.
    init_symtable();
    symtable_manager_t *mgr = symtable_get_global_manager();
    if (!mgr) {
        printf(
            "    (Skipped - global manager not initialized in test context)\n");
        return;
    }
    symtable_set_var(mgr, "lush_view_scalar", "hello", 0);

    lush_value_view_t view = {0};
    bool found = symtable_lookup("lush_view_scalar", &view);

    ASSERT(found, "scalar binding should be found");
    ASSERT_EQ(view.kind, LUSH_VALUE_SCALAR, "kind should be SCALAR");
    ASSERT_NOT_NULL(view.scalar_value, "scalar_value populated");
    ASSERT_STR_EQ(view.scalar_value, "hello", "value matches");
    ASSERT_NULL(view.array, "array NULL for scalar");

    lush_value_view_clear(&view);
    ASSERT_NULL(view.scalar_value, "clear zeroed scalar_value");
    ASSERT_EQ(view.kind, LUSH_VALUE_NONE, "clear zeroed kind");

    symtable_unset_var(mgr, "lush_view_scalar");
}

TEST(value_view_list_lookup) {
    array_value_t *arr = symtable_array_create(false);
    ASSERT_NOT_NULL(arr, "array_create");
    symtable_array_append(arr, "a");
    symtable_array_append(arr, "b");
    symtable_set_array("lush_view_list", arr);

    lush_value_view_t view = {0};
    bool found = symtable_lookup("lush_view_list", &view);

    ASSERT(found, "list binding should be found");
    ASSERT_EQ(view.kind, LUSH_VALUE_LIST, "kind should be LIST");
    ASSERT_NULL(view.scalar_value, "scalar_value NULL for list");
    ASSERT_NOT_NULL(view.array, "array populated");
    ASSERT_EQ(symtable_array_length(view.array), 2, "length matches");

    lush_value_view_clear(&view);
    /// The borrowed array is untouched by clear: still live in the
    /// symtable until we explicitly unset.
    symtable_unset_var(symtable_manager(), "lush_view_list");
}

TEST(value_view_map_lookup) {
    array_value_t *arr = symtable_array_create(true);
    ASSERT_NOT_NULL(arr, "array_create assoc");
    symtable_array_set_assoc(arr, "k1", "v1");
    symtable_set_array("lush_view_map", arr);

    lush_value_view_t view = {0};
    bool found = symtable_lookup("lush_view_map", &view);

    ASSERT(found, "map binding should be found");
    ASSERT_EQ(view.kind, LUSH_VALUE_MAP, "kind should be MAP");
    ASSERT_NOT_NULL(view.array, "array populated for map");
    ASSERT(view.array->is_associative, "is_associative on the borrowed array");

    lush_value_view_clear(&view);
    symtable_unset_var(symtable_manager(), "lush_view_map");
}

TEST(value_view_none_on_miss) {
    lush_value_view_t view = {
        .kind = LUSH_VALUE_SCALAR, .scalar_value = NULL, .array = NULL};
    bool found = symtable_lookup("lush_view_no_such_var", &view);

    ASSERT(!found, "unbound name should not be found");
    ASSERT_EQ(view.kind, LUSH_VALUE_NONE, "kind reset to NONE on miss");
    ASSERT_NULL(view.scalar_value, "scalar_value NULL on miss");
    ASSERT_NULL(view.array, "array NULL on miss");
    lush_value_view_clear(&view);
}

TEST(value_view_clear_idempotent) {
    lush_value_view_t view = {0};
    /// Repeated clears on a zero view are no-ops; safe to call after
    /// lookup whether or not a binding was found.
    lush_value_view_clear(&view);
    lush_value_view_clear(&view);
    lush_value_view_clear(NULL); /// NULL-safe
    ASSERT_EQ(view.kind, LUSH_VALUE_NONE, "remains NONE");
}

/* ============================================================================
 * GLOBAL CONVENIENCE API TESTS
 * ============================================================================
 */

TEST(global_convenience_api) {
    /// Note: These use the global manager, which may not be initialized in test
    /// context. The global convenience API is primarily for use within the
    /// shell runtime.

    symtable_manager_t *mgr = symtable_get_global_manager();
    if (mgr == NULL) {
        /// Global manager not initialized - this is expected in unit test
        /// context
        printf(
            "    (Skipped - global manager not initialized in test context)\n");
        return;
    }

    int result = symtable_set_global("TEST_VAR", "test_value");
    ASSERT_EQ(result, 0, "symtable_set_global should succeed");

    char *value = symtable_get_global("TEST_VAR");
    ASSERT_NOT_NULL(value, "symtable_get_global should return value");
    ASSERT_STR_EQ(value, "test_value", "Global value mismatch");
    free(value);

    ASSERT(symtable_exists_global("TEST_VAR"), "Variable should exist");

    /// Clean up
    symtable_unset_var(mgr, "TEST_VAR");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running symbol table unit tests...\n\n");

    printf("Manager lifecycle tests:\n");
    RUN_TEST(manager_new);
    RUN_TEST(manager_initial_level);

    printf("\nBasic variable tests:\n");
    RUN_TEST(set_get_variable);
    RUN_TEST(value_with_unit_separator_roundtrips);
    RUN_TEST(set_overwrite_variable);
    RUN_TEST(get_nonexistent_variable);
    RUN_TEST(unset_variable);
    RUN_TEST(var_exists);

    printf("\nScope management tests:\n");
    RUN_TEST(push_pop_scope);
    RUN_TEST(nested_scopes);
    RUN_TEST(local_variable_shadowing);
    RUN_TEST(in_function_scope);

    printf("\nVariable flags tests:\n");
    RUN_TEST(exported_variable);
    RUN_TEST(readonly_variable);
    RUN_TEST(get_environ);

    printf("\nNameref tests:\n");
    RUN_TEST(nameref_basic);
    RUN_TEST(nameref_is_nameref);
    RUN_TEST(nameref_resolve);

    printf("\nArray tests:\n");
    RUN_TEST(array_create);
    RUN_TEST(array_scalar_overwrite_releases_backing);
    RUN_TEST(value_view_scalar_lookup);
    RUN_TEST(value_view_list_lookup);
    RUN_TEST(value_view_map_lookup);
    RUN_TEST(value_view_none_on_miss);
    RUN_TEST(value_view_clear_idempotent);
    RUN_TEST(array_indexed_operations);
    RUN_TEST(array_index_int64_no_truncation);
    RUN_TEST(array_append);
    RUN_TEST(array_associative);

    printf("\nGlobal convenience API tests:\n");
    RUN_TEST(global_convenience_api);

    return TEST_RESULT();
}
