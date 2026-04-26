/**
 * @file ht.h
 * @brief Generic hash table implementation
 *
 * Integrated from https://github.com/berrym/libhashtable (originally MIT).
 * Provides type-safe hash tables for string-to-string, string-to-int,
 * string-to-float, and string-to-double mappings.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2023-2026 Michael Berry
 * @license MIT (relicensed for Lush integration)
 */

#ifndef __HT_H__
#define __HT_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque generic hash table type
 */
typedef struct ht ht_t;

/**
 * @brief Opaque hash table enumerator type for iteration
 */
typedef struct ht_enum ht_enum_t;

/**
 * @brief Opaque string-to-double hash table type
 */
typedef struct ht_strdouble ht_strdouble_t;

/**
 * @brief Opaque string-to-float hash table type
 */
typedef struct ht_strfloat ht_strfloat_t;

/**
 * @brief Opaque string-to-int hash table type
 */
typedef struct ht_strint ht_strint_t;

/**
 * @brief Opaque string-to-string hash table type
 */
typedef struct ht_strstr ht_strstr_t;

/**
 * @brief Opaque string-to-blob hash table type
 *
 * Maps a string key to an arbitrary length-prefixed binary value. Use
 * this in place of ht_strstr_t when the value can contain bytes that
 * are not safe under C-string semantics (e.g., embedded NULs).
 */
typedef struct ht_strblob ht_strblob_t;

/**
 * @brief Hash table configuration flags
 */
typedef enum {
    HT_STR_NONE = 0, /**< No special flags */
    HT_STR_CASECMP,  /**< Use case-insensitive string comparison */
    HT_SEED_RANDOM,  /**< Use random seed for hashing */
} ht_flags_enum_t;

/**
 * @brief Hash function pointer type
 * @param key Pointer to the key to hash
 * @param seed Seed value for the hash function
 * @return 64-bit hash value
 */
typedef uint64_t (*ht_hash)(const void *, uint64_t);

/**
 * @brief Key equality function pointer type
 * @param key1 Pointer to first key
 * @param key2 Pointer to second key
 * @return true if keys are equal, false otherwise
 */
typedef bool (*ht_keyeq)(const void *, const void *);

/**
 * @brief Key copy function pointer type
 * @param key Pointer to key to copy
 * @return Pointer to copied key
 */
typedef void *(*ht_kcopy)(const void *);

/**
 * @brief Key free function pointer type
 * @param key Pointer to key to free
 */
typedef void (*ht_kfree)(const void *);

/**
 * @brief Value copy function pointer type
 * @param val Pointer to value to copy
 * @return Pointer to copied value
 */
typedef void *(*ht_vcopy)(const void *);

/**
 * @brief Value free function pointer type
 * @param val Pointer to value to free
 */
typedef void (*ht_vfree)(const void *);

/**
 * @brief Hash table callback functions structure
 *
 * Contains function pointers for copying and freeing keys and values.
 */
typedef struct {
    ht_kcopy key_copy; /**< Function to copy keys */
    ht_kfree key_free; /**< Function to free keys */
    ht_vcopy val_copy; /**< Function to copy values */
    ht_vfree val_free; /**< Function to free values */
} ht_callbacks_t;

/** @brief FNV-1a hash prime constant (64-bit) */
#define FNV1A_PRIME (0x00000100000001B3) // 1099511628211 (64 bit)

/** @brief FNV-1a hash offset basis (64-bit) */
#define FNV1A_OFFSET (0xCBF29CE484222325) // 14695981039346656037 (64 bit)

/**
 * @brief Compute FNV-1a hash of a string
 * @param key Pointer to null-terminated string to hash
 * @param seed Seed value for the hash
 * @return 64-bit hash value
 */
uint64_t fnv1a_hash_str(const void *, uint64_t);

/**
 * @brief Compute case-insensitive FNV-1a hash of a string
 * @param key Pointer to null-terminated string to hash
 * @param seed Seed value for the hash
 * @return 64-bit hash value
 */
uint64_t fnv1a_hash_str_casecmp(const void *, uint64_t);

/**
 * @brief Compare two strings for equality
 * @param s1 Pointer to first null-terminated string
 * @param s2 Pointer to second null-terminated string
 * @return true if strings are equal, false otherwise
 */
bool str_eq(const void *, const void *);

/**
 * @brief Compare two strings for equality (case-insensitive)
 * @param s1 Pointer to first null-terminated string
 * @param s2 Pointer to second null-terminated string
 * @return true if strings are equal ignoring case, false otherwise
 */
bool str_caseeq(const void *, const void *);

/*
 * Creation and destruction
 */

/**
 * @brief Create a generic hash table
 * @param hash Hash function for keys
 * @param keyeq Key equality comparison function
 * @param callbacks Pointer to callback functions for key/value management
 * @param flags Configuration flags
 * @return Pointer to new hash table, or NULL on failure
 */
ht_t *ht_create(const ht_hash, const ht_keyeq, const ht_callbacks_t *,
                const unsigned int);

/**
 * @brief Destroy a generic hash table and free all resources
 * @param ht Pointer to hash table to destroy
 */
void ht_destroy(ht_t *);

/**
 * @brief Create a string-to-double hash table
 * @param flags Configuration flags
 * @return Pointer to new hash table, or NULL on failure
 */
ht_strdouble_t *ht_strdouble_create(unsigned int);

/**
 * @brief Destroy a string-to-double hash table
 * @param ht Pointer to hash table to destroy
 */
void ht_strdouble_destroy(ht_strdouble_t *);

/**
 * @brief Create a string-to-float hash table
 * @param flags Configuration flags
 * @return Pointer to new hash table, or NULL on failure
 */
ht_strfloat_t *ht_strfloat_create(unsigned int);

/**
 * @brief Destroy a string-to-float hash table
 * @param ht Pointer to hash table to destroy
 */
void ht_strfloat_destroy(ht_strfloat_t *);

/**
 * @brief Create a string-to-int hash table
 * @param flags Configuration flags
 * @return Pointer to new hash table, or NULL on failure
 */
ht_strint_t *ht_strint_create(unsigned int);

/**
 * @brief Destroy a string-to-int hash table
 * @param ht Pointer to hash table to destroy
 */
void ht_strint_destroy(ht_strint_t *);

/**
 * @brief Create a string-to-string hash table
 * @param flags Configuration flags
 * @return Pointer to new hash table, or NULL on failure
 */
ht_strstr_t *ht_strstr_create(unsigned int);

/**
 * @brief Destroy a string-to-string hash table
 * @param ht Pointer to hash table to destroy
 */
void ht_strstr_destroy(ht_strstr_t *);

/**
 * @brief Create a string-to-blob hash table
 *
 * Maps string keys to arbitrary length-prefixed binary values. The
 * table makes a deep copy of every value on insert, including bytes
 * after any embedded NUL.
 *
 * @param flags Configuration flags
 * @return Pointer to new hash table, or NULL on failure
 */
ht_strblob_t *ht_strblob_create(unsigned int);

/**
 * @brief Destroy a string-to-blob hash table
 * @param ht Pointer to hash table to destroy
 */
void ht_strblob_destroy(ht_strblob_t *);

/*
 * Insertion and removal
 */

/**
 * @brief Insert a key-value pair into a generic hash table
 * @param ht Pointer to hash table
 * @param key Pointer to key
 * @param value Pointer to value
 */
void ht_insert(ht_t *, const void *, const void *);

/**
 * @brief Remove a key-value pair from a generic hash table
 * @param ht Pointer to hash table
 * @param key Pointer to key to remove
 */
void ht_remove(ht_t *, const void *);

/**
 * @brief Insert a key-value pair into a string-to-double hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key
 * @param value Pointer to double value
 */
void ht_strdouble_insert(ht_strdouble_t *, const char *, const double *);

/**
 * @brief Remove a key-value pair from a string-to-double hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to remove
 */
void ht_strdouble_remove(ht_strdouble_t *, const char *);

/**
 * @brief Insert a key-value pair into a string-to-float hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key
 * @param value Pointer to float value
 */
void ht_strfloat_insert(ht_strfloat_t *, const char *, const float *);

/**
 * @brief Remove a key-value pair from a string-to-float hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to remove
 */
void ht_strfloat_remove(ht_strfloat_t *, const char *);

/**
 * @brief Insert a key-value pair into a string-to-int hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key
 * @param value Pointer to int value
 */
void ht_strint_insert(ht_strint_t *, const char *, const int *);

/**
 * @brief Remove a key-value pair from a string-to-int hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to remove
 */
void ht_strint_remove(ht_strint_t *, const char *);

/**
 * @brief Insert a key-value pair into a string-to-string hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key
 * @param value Null-terminated string value
 */
void ht_strstr_insert(ht_strstr_t *, const char *, const char *);

/**
 * @brief Remove a key-value pair from a string-to-string hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to remove
 */
void ht_strstr_remove(ht_strstr_t *, const char *);

/**
 * @brief Insert a key/blob pair into a string-to-blob hash table
 *
 * The hash table makes a deep copy of both the key and the blob bytes.
 * Caller retains ownership of the input data buffer.
 *
 * @param ht Pointer to hash table
 * @param key Null-terminated string key
 * @param data Pointer to blob bytes (may contain embedded NULs)
 * @param size Number of bytes at @p data; may be 0
 */
void ht_strblob_insert(ht_strblob_t *, const char *, const void *, size_t);

/**
 * @brief Remove a key from a string-to-blob hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to remove
 */
void ht_strblob_remove(ht_strblob_t *, const char *);

/*
 * Getting
 */

/**
 * @brief Get a value from a generic hash table
 * @param ht Pointer to hash table
 * @param key Pointer to key to look up
 * @return Pointer to value, or NULL if not found
 */
void *ht_get(const ht_t *, const void *);

/**
 * @brief Get a value from a string-to-double hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to look up
 * @return Pointer to double value, or NULL if not found
 */
void *ht_strdouble_get(ht_strdouble_t *, const char *);

/**
 * @brief Get a value from a string-to-float hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to look up
 * @return Pointer to float value, or NULL if not found
 */
void *ht_strfloat_get(ht_strfloat_t *, const char *);

/**
 * @brief Get a value from a string-to-int hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to look up
 * @return Pointer to int value, or NULL if not found
 */
void *ht_strint_get(ht_strint_t *, const char *);

/**
 * @brief Get a value from a string-to-string hash table
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to look up
 * @return Pointer to string value, or NULL if not found
 */
const char *ht_strstr_get(ht_strstr_t *, const char *);

/**
 * @brief Look up the blob stored under a key in a string-to-blob table
 *
 * The returned pointer aliases the hash table's internal storage and
 * remains valid until the entry is removed or the table is destroyed.
 * The caller must not free it.
 *
 * @param ht Pointer to hash table
 * @param key Null-terminated string key to look up
 * @param out_size If non-NULL, receives the blob length on success or
 *                 0 if the key is absent
 * @return Pointer to the stored blob bytes, or NULL if @p key is absent
 */
const void *ht_strblob_get(ht_strblob_t *, const char *, size_t *);

/*
 * Enumeration
 */

/**
 * @brief Create an enumerator for a generic hash table
 * @param ht Pointer to hash table to enumerate
 * @return Pointer to enumerator, or NULL on failure
 */
ht_enum_t *ht_enum_create(ht_t *);

/**
 * @brief Get the next key-value pair from a generic hash table enumerator
 * @param e Pointer to enumerator
 * @param key Output pointer for current key
 * @param value Output pointer for current value
 * @return true if a pair was retrieved, false if enumeration complete
 */
bool ht_enum_next(ht_enum_t *, const void **, const void **);

/**
 * @brief Destroy a generic hash table enumerator
 * @param e Pointer to enumerator to destroy
 */
void ht_enum_destroy(ht_enum_t *);

/**
 * @brief Create an enumerator for a string-to-double hash table
 * @param ht Pointer to hash table to enumerate
 * @return Pointer to enumerator, or NULL on failure
 */
ht_enum_t *ht_strdouble_enum_create(ht_strdouble_t *);

/**
 * @brief Get the next key-value pair from a string-to-double enumerator
 * @param e Pointer to enumerator
 * @param key Output pointer for current key string
 * @param value Output pointer for current double value
 * @return true if a pair was retrieved, false if enumeration complete
 */
bool ht_strdouble_enum_next(ht_enum_t *, const char **, const double **);

/**
 * @brief Destroy a string-to-double hash table enumerator
 * @param e Pointer to enumerator to destroy
 */
void ht_strdouble_enum_destroy(ht_enum_t *);

/**
 * @brief Create an enumerator for a string-to-float hash table
 * @param ht Pointer to hash table to enumerate
 * @return Pointer to enumerator, or NULL on failure
 */
ht_enum_t *ht_strfloat_enum_create(ht_strfloat_t *);

/**
 * @brief Get the next key-value pair from a string-to-float enumerator
 * @param e Pointer to enumerator
 * @param key Output pointer for current key string
 * @param value Output pointer for current float value
 * @return true if a pair was retrieved, false if enumeration complete
 */
bool ht_strfloat_enum_next(ht_enum_t *, const char **, const float **);

/**
 * @brief Destroy a string-to-float hash table enumerator
 * @param e Pointer to enumerator to destroy
 */
void ht_strfloat_enum_destroy(ht_enum_t *);

/**
 * @brief Create an enumerator for a string-to-int hash table
 * @param ht Pointer to hash table to enumerate
 * @return Pointer to enumerator, or NULL on failure
 */
ht_enum_t *ht_strint_enum_create(ht_strint_t *);

/**
 * @brief Get the next key-value pair from a string-to-int enumerator
 * @param e Pointer to enumerator
 * @param key Output pointer for current key string
 * @param value Output pointer for current int value
 * @return true if a pair was retrieved, false if enumeration complete
 */
bool ht_strint_enum_next(ht_enum_t *, const char **, const int **);

/**
 * @brief Destroy a string-to-int hash table enumerator
 * @param e Pointer to enumerator to destroy
 */
void ht_strint_enum_destroy(ht_enum_t *);

/**
 * @brief Create an enumerator for a string-to-string hash table
 * @param ht Pointer to hash table to enumerate
 * @return Pointer to enumerator, or NULL on failure
 */
ht_enum_t *ht_strstr_enum_create(ht_strstr_t *);

/**
 * @brief Get the next key-value pair from a string-to-string enumerator
 * @param e Pointer to enumerator
 * @param key Output pointer for current key string
 * @param value Output pointer for current value string
 * @return true if a pair was retrieved, false if enumeration complete
 */
bool ht_strstr_enum_next(ht_enum_t *, const char **, const char **);

/**
 * @brief Destroy a string-to-string hash table enumerator
 * @param e Pointer to enumerator to destroy
 */
void ht_strstr_enum_destroy(ht_enum_t *);

/**
 * @brief Create an enumerator for a string-to-blob hash table
 * @param ht Pointer to hash table to enumerate
 * @return Pointer to enumerator, or NULL on failure
 */
ht_enum_t *ht_strblob_enum_create(ht_strblob_t *);

/**
 * @brief Get the next key/blob triple from a string-to-blob enumerator
 *
 * Pointers written through @p key, @p data, and @p size alias the hash
 * table's storage and must not be freed by the caller. Pass NULL for
 * any output parameter that is not needed.
 *
 * @param e Pointer to enumerator
 * @param key Out-pointer for the current key, or NULL
 * @param data Out-pointer for the current blob bytes, or NULL
 * @param size Out-pointer for the current blob length, or NULL
 * @return true if a triple was retrieved, false if enumeration complete
 */
bool ht_strblob_enum_next(ht_enum_t *, const char **, const void **, size_t *);

/**
 * @brief Destroy a string-to-blob hash table enumerator
 * @param e Pointer to enumerator to destroy
 */
void ht_strblob_enum_destroy(ht_enum_t *);

#ifdef __cplusplus
}
#endif

#endif // __HT_H__
