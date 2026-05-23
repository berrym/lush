/**
 * @file test_hashtable.c
 * @brief Unit tests for hash table library
 *
 * Tests the hash table library including:
 * - String-to-string hash tables
 * - String-to-int hash tables
 * - String-to-float hash tables
 * - String-to-double hash tables
 * - Collision handling
 * - Edge cases
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "libhashtable/ht.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pre-existing local ASSERT(cond, msg) used a 2-arg signature
 * that conflicts with the framework's 1-arg ASSERT(cond). Alias it to
 * the framework's ASSERT_TRUE(cond, msg) which has matching semantics. */
#undef ASSERT
#define ASSERT(cond, msg) ASSERT_TRUE(cond, msg)

// Test framework macros

/* ============================================================================
 * STRING-TO-STRING HASH TABLE TESTS
 * ============================================================================
 */

TEST(strstr_create_destroy) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "ht_strstr_create should return non-NULL");
    ht_strstr_destroy(ht);
}

TEST(strstr_insert_get) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "Hash table creation should succeed");

    ht_strstr_insert(ht, "key1", "value1");

    const char *value = ht_strstr_get(ht, "key1");
    ASSERT_NOT_NULL(value, "Value should not be NULL");
    ASSERT_STR_EQ(value, "value1", "Value should match");

    ht_strstr_destroy(ht);
}

TEST(strstr_overwrite) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    ht_strstr_insert(ht, "key", "original");
    ht_strstr_insert(ht, "key", "updated");

    const char *value = ht_strstr_get(ht, "key");
    ASSERT_STR_EQ(value, "updated", "Value should be updated");

    ht_strstr_destroy(ht);
}

TEST(strstr_remove) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    ht_strstr_insert(ht, "key", "value");
    ASSERT_NOT_NULL(ht_strstr_get(ht, "key"), "Key should exist before remove");

    ht_strstr_remove(ht, "key");
    ASSERT_NULL(ht_strstr_get(ht, "key"), "Key should not exist after remove");

    ht_strstr_destroy(ht);
}

TEST(strstr_not_found) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    const char *value = ht_strstr_get(ht, "nonexistent");
    ASSERT_NULL(value, "Non-existent key should return NULL");

    ht_strstr_destroy(ht);
}

TEST(strstr_case_sensitive) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    ht_strstr_insert(ht, "Key", "value1");
    ht_strstr_insert(ht, "key", "value2");

    ASSERT_STR_EQ(ht_strstr_get(ht, "Key"), "value1", "Case should matter");
    ASSERT_STR_EQ(ht_strstr_get(ht, "key"), "value2", "Case should matter");

    ht_strstr_destroy(ht);
}

TEST(strstr_case_insensitive) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_CASECMP);

    ht_strstr_insert(ht, "Key", "value1");

    // Case-insensitive lookup should work
    const char *value = ht_strstr_get(ht, "KEY");
    ASSERT_NOT_NULL(value, "Case-insensitive lookup should work");

    ht_strstr_destroy(ht);
}

/* ============================================================================
 * STRING-TO-INT HASH TABLE TESTS
 * ============================================================================
 */

TEST(strint_create_destroy) {
    ht_strint_t *ht = ht_strint_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "ht_strint_create should return non-NULL");
    ht_strint_destroy(ht);
}

TEST(strint_insert_get) {
    ht_strint_t *ht = ht_strint_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "Hash table creation should succeed");

    int value = 42;
    ht_strint_insert(ht, "answer", &value);

    int *result = ht_strint_get(ht, "answer");
    ASSERT_NOT_NULL(result, "Value should not be NULL");
    ASSERT_EQ(*result, 42, "Value should match");

    ht_strint_destroy(ht);
}

TEST(strint_negative_value) {
    ht_strint_t *ht = ht_strint_create(HT_STR_NONE);

    int value = -123;
    ht_strint_insert(ht, "neg", &value);

    int *result = ht_strint_get(ht, "neg");
    ASSERT_NOT_NULL(result, "Value should not be NULL");
    ASSERT_EQ(*result, -123, "Negative value should be correct");

    ht_strint_destroy(ht);
}

TEST(strint_zero_value) {
    ht_strint_t *ht = ht_strint_create(HT_STR_NONE);

    int value = 0;
    ht_strint_insert(ht, "zero", &value);

    int *result = ht_strint_get(ht, "zero");
    ASSERT_NOT_NULL(result, "Zero should be found");
    ASSERT_EQ(*result, 0, "Zero value should be correct");

    ht_strint_destroy(ht);
}

/* ============================================================================
 * STRING-TO-FLOAT HASH TABLE TESTS
 * ============================================================================
 */

TEST(strfloat_create_destroy) {
    ht_strfloat_t *ht = ht_strfloat_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "ht_strfloat_create should return non-NULL");
    ht_strfloat_destroy(ht);
}

TEST(strfloat_insert_get) {
    ht_strfloat_t *ht = ht_strfloat_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "Hash table creation should succeed");

    float value = 3.14159f;
    ht_strfloat_insert(ht, "pi", &value);

    float *result = ht_strfloat_get(ht, "pi");
    ASSERT_NOT_NULL(result, "Value should not be NULL");
    ASSERT(*result > 3.14f && *result < 3.15f,
           "Value should be approximately pi");

    ht_strfloat_destroy(ht);
}

TEST(strfloat_negative) {
    ht_strfloat_t *ht = ht_strfloat_create(HT_STR_NONE);

    float value = -2.5f;
    ht_strfloat_insert(ht, "neg", &value);

    float *result = ht_strfloat_get(ht, "neg");
    ASSERT_NOT_NULL(result, "Value should not be NULL");
    ASSERT(*result < -2.4f && *result > -2.6f,
           "Negative value should be correct");

    ht_strfloat_destroy(ht);
}

TEST(strfloat_zero) {
    ht_strfloat_t *ht = ht_strfloat_create(HT_STR_NONE);

    float value = 0.0f;
    ht_strfloat_insert(ht, "zero", &value);

    float *result = ht_strfloat_get(ht, "zero");
    ASSERT_NOT_NULL(result, "Zero should be found");
    ASSERT(*result == 0.0f, "Zero value should be correct");

    ht_strfloat_destroy(ht);
}

/* ============================================================================
 * STRING-TO-DOUBLE HASH TABLE TESTS
 * ============================================================================
 */

TEST(strdouble_create_destroy) {
    ht_strdouble_t *ht = ht_strdouble_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "ht_strdouble_create should return non-NULL");
    ht_strdouble_destroy(ht);
}

TEST(strdouble_insert_get) {
    ht_strdouble_t *ht = ht_strdouble_create(HT_STR_NONE);
    ASSERT_NOT_NULL(ht, "Hash table creation should succeed");

    double value = 3.14159265358979;
    ht_strdouble_insert(ht, "pi", &value);

    double *result = ht_strdouble_get(ht, "pi");
    ASSERT_NOT_NULL(result, "Value should not be NULL");
    ASSERT(*result > 3.14159 && *result < 3.14160,
           "Value should be approximately pi");

    ht_strdouble_destroy(ht);
}

TEST(strdouble_precision) {
    ht_strdouble_t *ht = ht_strdouble_create(HT_STR_NONE);

    double value = 1.23456789012345678901234567890;
    ht_strdouble_insert(ht, "precise", &value);

    double *result = ht_strdouble_get(ht, "precise");
    ASSERT_NOT_NULL(result, "Value should not be NULL");
    // Double has about 15-17 significant digits
    ASSERT(*result > 1.234567890123 && *result < 1.234567890124,
           "Double precision should be maintained");

    ht_strdouble_destroy(ht);
}

/* ============================================================================
 * EDGE CASES
 * ============================================================================
 */

TEST(collision_handling) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    // Insert many keys to force collisions
    char key[32];
    char val[32];
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(val, sizeof(val), "value_%d", i);
        ht_strstr_insert(ht, key, val);
    }

    // Verify all values can be retrieved
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(val, sizeof(val), "value_%d", i);
        const char *value = ht_strstr_get(ht, key);
        ASSERT_NOT_NULL(value, "Value should be found");
        ASSERT_STR_EQ(value, val, "Value should match");
    }

    ht_strstr_destroy(ht);
}

TEST(empty_key) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    ht_strstr_insert(ht, "", "empty_key_value");

    const char *value = ht_strstr_get(ht, "");
    ASSERT_NOT_NULL(value, "Empty key should work");
    ASSERT_STR_EQ(value, "empty_key_value", "Empty key value should match");

    ht_strstr_destroy(ht);
}

TEST(long_key) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    char key[1024];
    memset(key, 'a', sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';

    ht_strstr_insert(ht, key, "long_key_value");

    const char *value = ht_strstr_get(ht, key);
    ASSERT_NOT_NULL(value, "Long key should work");
    ASSERT_STR_EQ(value, "long_key_value", "Long key value should match");

    ht_strstr_destroy(ht);
}

TEST(special_chars_in_key) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    ht_strstr_insert(ht, "key\twith\nspecial", "special_value");

    const char *value = ht_strstr_get(ht, "key\twith\nspecial");
    ASSERT_NOT_NULL(value, "Special chars in key should work");
    ASSERT_STR_EQ(value, "special_value", "Special chars value should match");

    ht_strstr_destroy(ht);
}

/* ============================================================================
 * ENUMERATION TESTS
 * ============================================================================
 */

TEST(strstr_enumeration) {
    ht_strstr_t *ht = ht_strstr_create(HT_STR_NONE);

    ht_strstr_insert(ht, "key1", "value1");
    ht_strstr_insert(ht, "key2", "value2");
    ht_strstr_insert(ht, "key3", "value3");

    ht_enum_t *e = ht_strstr_enum_create(ht);
    ASSERT_NOT_NULL(e, "Enumerator creation should succeed");

    int count = 0;
    const char *key, *value;
    while (ht_strstr_enum_next(e, &key, &value)) {
        ASSERT_NOT_NULL(key, "Key should not be NULL");
        ASSERT_NOT_NULL(value, "Value should not be NULL");
        count++;
    }

    ASSERT_EQ(count, 3, "Should enumerate 3 items");

    ht_strstr_enum_destroy(e);
    ht_strstr_destroy(ht);
}

TEST(strint_enumeration) {
    ht_strint_t *ht = ht_strint_create(HT_STR_NONE);

    int v1 = 1, v2 = 2, v3 = 3;
    ht_strint_insert(ht, "one", &v1);
    ht_strint_insert(ht, "two", &v2);
    ht_strint_insert(ht, "three", &v3);

    ht_enum_t *e = ht_strint_enum_create(ht);
    ASSERT_NOT_NULL(e, "Enumerator creation should succeed");

    int count = 0;
    const char *key;
    const int *value;
    while (ht_strint_enum_next(e, &key, &value)) {
        ASSERT_NOT_NULL(key, "Key should not be NULL");
        ASSERT_NOT_NULL(value, "Value should not be NULL");
        count++;
    }

    ASSERT_EQ(count, 3, "Should enumerate 3 items");

    ht_strint_enum_destroy(e);
    ht_strint_destroy(ht);
}

/* ============================================================================
 * HASH FUNCTION TESTS
 * ============================================================================
 */

TEST(fnv1a_hash_basic) {
    // Test that hash produces consistent results
    uint64_t hash1 = fnv1a_hash_str("test", 0);
    uint64_t hash2 = fnv1a_hash_str("test", 0);
    ASSERT(hash1 == hash2, "Same string should produce same hash");

    uint64_t hash3 = fnv1a_hash_str("other", 0);
    ASSERT(hash1 != hash3, "Different strings should produce different hashes");
}

TEST(fnv1a_hash_case_insensitive) {
    uint64_t hash1 = fnv1a_hash_str_casecmp("Test", 0);
    uint64_t hash2 = fnv1a_hash_str_casecmp("TEST", 0);
    uint64_t hash3 = fnv1a_hash_str_casecmp("test", 0);

    ASSERT(hash1 == hash2, "Case-insensitive hash should be equal");
    ASSERT(hash2 == hash3, "Case-insensitive hash should be equal");
}

/* ============================================================================
 * MAIN
 * ============================================================================
 */

int main(void) {
    printf("Running hash table library tests...\n\n");

    printf("String-to-String Hash Table Tests:\n");
    RUN_TEST(strstr_create_destroy);
    RUN_TEST(strstr_insert_get);
    RUN_TEST(strstr_overwrite);
    RUN_TEST(strstr_remove);
    RUN_TEST(strstr_not_found);
    RUN_TEST(strstr_case_sensitive);
    RUN_TEST(strstr_case_insensitive);

    printf("\nString-to-Int Hash Table Tests:\n");
    RUN_TEST(strint_create_destroy);
    RUN_TEST(strint_insert_get);
    RUN_TEST(strint_negative_value);
    RUN_TEST(strint_zero_value);

    printf("\nString-to-Float Hash Table Tests:\n");
    RUN_TEST(strfloat_create_destroy);
    RUN_TEST(strfloat_insert_get);
    RUN_TEST(strfloat_negative);
    RUN_TEST(strfloat_zero);

    printf("\nString-to-Double Hash Table Tests:\n");
    RUN_TEST(strdouble_create_destroy);
    RUN_TEST(strdouble_insert_get);
    RUN_TEST(strdouble_precision);

    printf("\nEdge Cases:\n");
    RUN_TEST(collision_handling);
    RUN_TEST(empty_key);
    RUN_TEST(long_key);
    RUN_TEST(special_chars_in_key);

    printf("\nEnumeration Tests:\n");
    RUN_TEST(strstr_enumeration);
    RUN_TEST(strint_enumeration);

    printf("\nHash Function Tests:\n");
    RUN_TEST(fnv1a_hash_basic);
    RUN_TEST(fnv1a_hash_case_insensitive);

    return TEST_RESULT();
}
