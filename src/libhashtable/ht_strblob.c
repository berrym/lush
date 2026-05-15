/**
 * @file ht_strblob.c
 * @brief String to Binary Blob Hash Table Implementation
 *
 * Project: libhashtable
 * URL: https://github.com/berrym/libhashtable
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2023 Michael Berry
 * @license MIT
 *
 * Stores arbitrary binary data (including embedded NUL bytes) under a
 * string key, with explicit length tracking. Use this in place of
 * ht_strstr_t when the value can contain bytes that are not safe for
 * C-string semantics (e.g., serialized structures, ANSI escape
 * sequences, terminal control bytes, image data).
 */

#include "ht.h"

#include <stdlib.h>
#include <string.h>

/* Internal value type — owns a heap copy of the caller's bytes plus
 * the length. Kept private to this translation unit; callers interact
 * via the (data, size) API surface. */
typedef struct {
    void *data;
    size_t size;
} ht_blob_t;

/**
 * @brief Allocate a heap copy of a blob (deep copy of bytes)
 * @param src Source blob to copy
 * @return Pointer to copied blob, or NULL on allocation failure
 */
static ht_blob_t *blob_copy(const ht_blob_t *src) {
    ht_blob_t *dst = malloc(sizeof(*dst));
    if (!dst) {
        return NULL;
    }
    dst->size = src->size;
    if (src->size == 0) {
        dst->data = NULL;
        return dst;
    }
    dst->data = malloc(src->size);
    if (!dst->data) {
        free(dst);
        return NULL;
    }
    memcpy(dst->data, src->data, src->size);
    return dst;
}

/**
 * @brief Free a blob and its owned bytes
 * @param blob Blob to free (may be NULL)
 */
static void blob_free(const ht_blob_t *blob) {
    if (!blob) {
        return;
    }
    free(blob->data);
    free((void *)blob);
}

/**
 * @brief Create a string to blob hash table
 *
 * Wrapper around ht_create that creates a string->blob hash table.
 *
 * @param flags Configuration flags (e.g., HT_STR_CASECMP, HT_SEED_RANDOM)
 * @return Pointer to the new hash table, or NULL on failure
 */
ht_strblob_t *ht_strblob_create(unsigned int flags) {
    ht_hash hash = fnv1a_hash_str;
    ht_keyeq keyeq = str_eq;
    const ht_callbacks_t callbacks = {
        (void *(*)(const void *))strdup, (void (*)(const void *))free,
        (void *(*)(const void *))blob_copy, (void (*)(const void *))blob_free};

    if (flags & HT_STR_CASECMP) {
        hash = fnv1a_hash_str_casecmp;
        keyeq = str_caseeq;
    }

    return (ht_strblob_t *)ht_create(hash, keyeq, &callbacks, flags);
}

/**
 * @brief Destroy a string to blob hash table
 * @param ht Pointer to the hash table to destroy
 */
void ht_strblob_destroy(ht_strblob_t *ht) { ht_destroy((ht_t *)ht); }

/**
 * @brief Insert a key/blob pair into the hash table
 *
 * The hash table makes a deep copy of both the key and the blob bytes.
 * Caller retains ownership of the input data buffer.
 *
 * @param ht Pointer to the hash table
 * @param key Null-terminated string key
 * @param data Pointer to the blob bytes (may contain embedded NULs)
 * @param size Number of bytes at @p data; may be 0
 */
void ht_strblob_insert(ht_strblob_t *ht, const char *key, const void *data,
                       size_t size) {
    ht_blob_t blob = {(void *)data, size};
    ht_insert((ht_t *)ht, (void *)key, &blob);
}

/**
 * @brief Remove a key from the hash table
 * @param ht Pointer to the hash table
 * @param key Null-terminated string key to remove
 */
void ht_strblob_remove(ht_strblob_t *ht, const char *key) {
    ht_remove((ht_t *)ht, (void *)key);
}

/**
 * @brief Look up the blob stored under a key
 *
 * The returned pointer aliases the hash table's internal storage and
 * remains valid until the entry is removed or the table is destroyed.
 * Do not free it.
 *
 * @param ht Pointer to the hash table
 * @param key Null-terminated string key
 * @param out_size If non-NULL, receives the blob length on success or 0
 *                 if the key is not found
 * @return Pointer to the stored blob bytes, or NULL if @p key is absent
 */
const void *ht_strblob_get(ht_strblob_t *ht, const char *key,
                           size_t *out_size) {
    const ht_blob_t *blob = ht_get((ht_t *)ht, (void *)key);
    if (!blob) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }
    if (out_size) {
        *out_size = blob->size;
    }
    return blob->data;
}

/**
 * @brief Create an enumeration object for a string to blob hash table
 * @param ht Pointer to the hash table
 * @return Pointer to the enumeration object, or NULL on failure
 */
ht_enum_t *ht_strblob_enum_create(ht_strblob_t *ht) {
    return ht_enum_create((ht_t *)ht);
}

/**
 * @brief Get the next key/blob pair from an enumeration
 *
 * The pointers written through @p key, @p data, and @p size alias the
 * hash table's storage and must not be freed by the caller.
 *
 * @param he Pointer to the enumeration object
 * @param key Out-pointer for the string key (may be NULL to skip)
 * @param data Out-pointer for the blob bytes (may be NULL to skip)
 * @param size Out-pointer for the blob length (may be NULL to skip)
 * @return true if a pair was retrieved, false if enumeration is complete
 */
bool ht_strblob_enum_next(ht_enum_t *he, const char **key, const void **data,
                          size_t *size) {
    const void *raw_val = NULL;
    if (!ht_enum_next(he, (const void **)key, &raw_val)) {
        return false;
    }
    const ht_blob_t *blob = raw_val;
    if (data) {
        *data = blob ? blob->data : NULL;
    }
    if (size) {
        *size = blob ? blob->size : 0;
    }
    return true;
}

/**
 * @brief Destroy a string to blob hash table enumeration object
 * @param he Pointer to the enumeration object to destroy
 */
void ht_strblob_enum_destroy(ht_enum_t *he) { ht_enum_destroy(he); }
