/*
 * Stub implementations for lush dependencies
 *
 * These stubs provide minimal implementations of functions needed by
 * continuation_prompt_layer tests but not directly relevant to testing
 * the layer's functionality.
 */

#include <stdlib.h>
#include <string.h>

/// ============================================================================
/// Global Variables Stubs
/// ============================================================================

int last_exit_status = 0;

/// ============================================================================
/// Hashtable Stubs (for symtable)
/// ============================================================================

/// Minimal hashtable implementation for testing
typedef struct ht_strstr {
    char **keys;
    char **values;
    int count;
    int capacity;
} ht_strstr_t;

typedef struct ht_strstr_enum {
    ht_strstr_t *ht;
    int index;
} ht_strstr_enum_t;

ht_strstr_t *ht_strstr_create(void) {
    ht_strstr_t *ht = malloc(sizeof(ht_strstr_t));
    if (!ht)
        return NULL;

    ht->capacity = 16;
    ht->count = 0;
    ht->keys = calloc(ht->capacity, sizeof(char *));
    ht->values = calloc(ht->capacity, sizeof(char *));

    return ht;
}

void ht_strstr_destroy(ht_strstr_t *ht) {
    if (!ht)
        return;

    for (int i = 0; i < ht->count; i++) {
        free(ht->keys[i]);
        free(ht->values[i]);
    }

    free(ht->keys);
    free(ht->values);
    free(ht);
}

int ht_strstr_insert(ht_strstr_t *ht, const char *key, const char *value) {
    if (!ht || !key || !value)
        return -1;
    if (ht->count >= ht->capacity)
        return -1;

    ht->keys[ht->count] = strdup(key);
    ht->values[ht->count] = strdup(value);
    ht->count++;

    return 0;
}

const char *ht_strstr_get(ht_strstr_t *ht, const char *key) {
    if (!ht || !key)
        return NULL;

    for (int i = 0; i < ht->count; i++) {
        if (strcmp(ht->keys[i], key) == 0) {
            return ht->values[i];
        }
    }

    return NULL;
}

ht_strstr_enum_t *ht_strstr_enum_create(ht_strstr_t *ht) {
    if (!ht)
        return NULL;

    ht_strstr_enum_t *e = malloc(sizeof(ht_strstr_enum_t));
    if (!e)
        return NULL;

    e->ht = ht;
    e->index = 0;

    return e;
}

int ht_strstr_enum_next(ht_strstr_enum_t *e, const char **key,
                        const char **value) {
    if (!e || !e->ht)
        return 0;

    if (e->index >= e->ht->count) {
        return 0;
    }

    if (key)
        *key = e->ht->keys[e->index];
    if (value)
        *value = e->ht->values[e->index];

    e->index++;
    return 1;
}

void ht_strstr_enum_destroy(ht_strstr_enum_t *e) { free(e); }
