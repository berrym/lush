/**
 * @file ht.c
 * @brief Generic Hash Table Implementation
 *
 * Project: libhashtable
 * URL: https://github.com/berrym/libhashtable
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (c) 2023 Michael Berry
 * @license MIT
 */

#include "ht.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#define INITIAL_BUCKETS (16) // Initial table size
#define MAX_LOAD_FACTOR                                                        \
    (0.75) // Capacity point at which a table needs to grow and rehash
#define MAX_CAPACITY                                                           \
    ((size_t)1 << 31) // Maximum capacity of table when it should not grow and
                      // rehash (2147483648)
#define GROWTH_FACTOR (2) // Factor by which a table's capacity should grow

typedef struct ht_bucket {
    const void *key;
    const void *val;
    struct ht_bucket *next;
} ht_bucket_t;

struct ht { // typedefed to ht_t in ht.h for external scope
    ht_hash hfunc;
    ht_keyeq keyeq;
    ht_callbacks_t callbacks;
    ht_bucket_t *buckets;
    size_t capacity;
    size_t used_buckets;
    uint64_t seed;
};

struct ht_enum { // typedefed to ht_enum_t in ht.h for external scope
    ht_t *ht;
    ht_bucket_t *cur;
    size_t idx;
};

/**
 * @brief Generate a random hash offset
 * @param ht Pointer to the hash table
 */
static void __random_seed(ht_t *ht) {
    uint64_t seed = (uint64_t)time(NULL);
    seed ^= ((uint64_t)ht_create << 32) | (uint64_t)&ht;
    seed ^= (uint64_t)&ht;
    ht->seed = seed;
}

/**
 * @brief Use a default hash offset for FNV1A algorithm
 * @param ht Pointer to the hash table
 */
static void __default_seed(ht_t *ht) { ht->seed = FNV1A_OFFSET; }

/**
 * @brief Default copy callback
 * @param v Pointer to the value to copy
 * @return Pointer to the copied value (passthrough)
 */
static void *__ht_passthrough_copy(const void *v) { return (void *)v; }

/**
 * @brief Default destroy callback
 * @param v Pointer to the value to destroy (unused)
 */
static void __ht_passthrough_destroy(const void *v __attribute__((unused))) {
    return;
}

/**
 * @brief Return the table index of a bucket given its key
 * @param ht Pointer to the hash table
 * @param key Pointer to the key
 * @return The bucket index for the given key
 */
static size_t __ht_bucket_index(const ht_t *ht, const void *key) {
    return ht->hfunc(key, ht->seed) % ht->capacity;
}

/**
 * @brief Fill a bucket with a key and value
 *
 * If part of a rehash operation do not make copies of the key value pair.
 * - Case 1: Check if the index of the bucket has something already.
 *           If not then we add the key and value to the bucket.
 * - Case 2: If that index already has a key value pair,
 *           check if the key is a match and if so replace the value.
 * - Case 3: We've determined the key isn't in a bucket,
 *           but there is something already at that index.
 *           Traverse the chain checking each node if we have the key already.
 *           If yes, replace the value. If not, create a new node for the chain.
 *
 * @param ht Pointer to the hash table
 * @param key Pointer to the key
 * @param val Pointer to the value
 * @param rehash True if this is part of a rehash operation
 */
static void __ht_add_to_bucket(ht_t *ht, const void *key, const void *val,
                               bool rehash) {
    ht_bucket_t *cur = NULL, *prev = NULL;
    const size_t idx = __ht_bucket_index(ht, key);

    if (!ht->buckets[idx].key) {
        if (!rehash) {
            key = ht->callbacks.key_copy(key);

            if (val) {
                val = ht->callbacks.val_copy(val);
            }
        }

        ht->buckets[idx].key = key;
        ht->buckets[idx].val = val;

        if (!rehash) {
            ht->used_buckets++;
        }
    } else {
        cur = ht->buckets + idx;

        do {
            if (ht->keyeq(key, cur->key)) {
                if (cur->val) {
                    ht->callbacks.val_free(cur->val);
                }

                if (!rehash && val) {
                    val = ht->callbacks.val_copy(val);
                }

                cur->val = val;
                prev = NULL;
                break;
            }

            prev = cur;
            cur = cur->next;
        } while (cur);

        if (prev) {
            cur = calloc(1, sizeof(*cur->next));
            if (!cur) {
                perror("__ht_add_to_bucket");
                return;
            }

            if (!rehash) {
                key = ht->callbacks.key_copy(key);

                if (val) {
                    val = ht->callbacks.val_copy(val);
                }
            }

            cur->key = key;
            cur->val = val;
            prev->next = cur;

            if (!rehash) {
                ht->used_buckets++;
            }
        }
    }
}

/**
 * @brief Rehash a table growing its capacity by GROWTH_FACTOR
 *
 * Rehashes the table if it has reached MAX_LOAD_FACTOR, but does not
 * grow the table if its capacity has reached MAX_CAPACITY.
 *
 * @param ht Pointer to the hash table
 */
static void __ht_rehash(ht_t *ht) {
    ht_bucket_t *buckets = NULL, *cur = NULL, *next = NULL;
    size_t capacity;

    if (ht->used_buckets + 1 < (size_t)(ht->capacity * MAX_LOAD_FACTOR) ||
        ht->capacity >= MAX_CAPACITY) {
        return;
    }

    capacity = ht->capacity;
    buckets = ht->buckets;
    ht->capacity *= GROWTH_FACTOR;
    ht->buckets = calloc(ht->capacity, sizeof(*buckets));
    if (!ht->buckets) {
        perror("__ht_rehash");
        return;
    }

    for (size_t i = 0; i < capacity; i++) {
        if (!buckets[i].key) {
            continue;
        }

        __ht_add_to_bucket(ht, buckets[i].key, buckets[i].val, true);

        if (buckets[i].next) {
            cur = buckets[i].next;

            do {
                __ht_add_to_bucket(ht, cur->key, cur->val, true);
                next = cur->next;
                free(cur);
                cur = next;
            } while (cur);
        }
    }

    free(buckets);
    buckets = NULL;
}

/**
 * @brief Create a new hash table
 *
 * Creates a new hash table of INITIAL_CAPACITY. It requires a hash
 * function, a key equality comparison function, and optionally bucket
 * operations function callbacks structure.
 *
 * @param hfunc Hash function to use for key hashing
 * @param keyeq Key equality comparison function
 * @param callbacks Optional callbacks for key/value copy and free operations
 * @param flags Configuration flags (e.g., HT_SEED_RANDOM)
 * @return Pointer to the new hash table, or NULL on failure
 */
ht_t *ht_create(const ht_hash hfunc, const ht_keyeq keyeq,
                const ht_callbacks_t *callbacks, const unsigned int flags) {
    ht_t *ht = NULL;

    if (!hfunc || !keyeq) {
        return NULL;
    }

    ht = calloc(1, sizeof(*ht));
    if (!ht) {
        perror("ht_create");
        return NULL;
    }

    ht->hfunc = hfunc;
    ht->keyeq = keyeq;

    ht->callbacks.key_copy = __ht_passthrough_copy;
    ht->callbacks.key_free = __ht_passthrough_destroy;
    ht->callbacks.val_copy = __ht_passthrough_copy;
    ht->callbacks.val_free = __ht_passthrough_destroy;

    if (callbacks) {
        if (callbacks->key_copy) {
            ht->callbacks.key_copy = callbacks->key_copy;
        }
        if (callbacks->key_free) {
            ht->callbacks.key_free = callbacks->key_free;
        }
        if (callbacks->val_copy) {
            ht->callbacks.val_copy = callbacks->val_copy;
        }
        if (callbacks->val_free) {
            ht->callbacks.val_free = callbacks->val_free;
        }
    }

    ht->capacity = INITIAL_BUCKETS;
    ht->buckets = calloc(ht->capacity, sizeof(*ht->buckets));
    if (!ht->buckets) {
        perror("ht_create");
        return NULL;
    }

    if (flags & HT_SEED_RANDOM) {
        __random_seed(ht);
    } else {
        __default_seed(ht);
    }

    return ht;
}

/**
 * @brief Destroy a hash table
 *
 * Destroys a hash table by first freeing all buckets then the table itself.
 *
 * @param ht Pointer to the hash table to destroy
 */
void ht_destroy(ht_t *ht) {
    ht_bucket_t *next = NULL, *cur = NULL;

    if (!ht) {
        return;
    }

    for (size_t idx = 0; idx < ht->capacity; idx++) {
        if (!ht->buckets[idx].key) {
            continue;
        }

        ht->callbacks.key_free(ht->buckets[idx].key);
        if (ht->buckets[idx].val) {
            ht->callbacks.val_free(ht->buckets[idx].val);
        }

        next = ht->buckets[idx].next;
        while (next) {
            cur = next;
            ht->callbacks.key_free(cur->key);
            if (cur->val) {
                ht->callbacks.val_free(cur->val);
            }
            next = cur->next;
            free(cur);
            cur = NULL;
        }
    }

    free(ht->buckets);
    ht->buckets = NULL;
    free(ht);
    ht = NULL;
}

/**
 * @brief Insert a key value pair into a table bucket
 * @param ht Pointer to the hash table
 * @param key Pointer to the key
 * @param val Pointer to the value
 */
void ht_insert(ht_t *ht, const void *key, const void *val) {
    if (!ht || !key) {
        return;
    }

    __ht_rehash(ht);
    __ht_add_to_bucket(ht, key, val, false);
}

/**
 * @brief Remove a bucket from the table
 *
 * Removes a bucket from the table by:
 * 1. Getting the bucket index using its hash
 * 2. Checking the bucket and chains for a key match
 * 3. Removing the entry if match is made
 * 4. Relinking the chain if necessary
 *
 * @param ht Pointer to the hash table
 * @param key Pointer to the key to remove
 */
void ht_remove(ht_t *ht, const void *key) {
    if (!ht || !key) {
        return;
    }

    ht_bucket_t *cur = NULL, *prev = NULL;
    const size_t idx = __ht_bucket_index(ht, key);
    ;

    if (!ht->buckets[idx].key) {
        return;
    }

    if (ht->keyeq(ht->buckets[idx].key, key)) {
        ht->callbacks.key_free(ht->buckets[idx].key);
        if (ht->buckets[idx].val) {
            ht->callbacks.val_free(ht->buckets[idx].val);
        }
        ht->buckets[idx].key = NULL;
        ht->buckets[idx].val = NULL;

        cur = ht->buckets[idx].next;
        if (cur) {
            ht->buckets[idx].key = ht->callbacks.key_copy(cur->key);
            if (cur->val) {
                ht->buckets[idx].val = ht->callbacks.val_copy(cur->val);
            }
            ht->buckets[idx].next = cur->next;
            ht->callbacks.key_free(cur->key);
            if (cur->val) {
                ht->callbacks.val_free(cur->val);
            }
            cur->key = NULL;
            cur->val = NULL;
            free(cur);
            cur = NULL;
        }

        ht->used_buckets--;

        return;
    }

    prev = ht->buckets + idx;
    cur = prev->next;

    while (cur) {
        if (ht->keyeq(key, cur->key)) {
            prev->next = cur->next;
            ht->callbacks.key_free(cur->key);
            if (cur->val) {
                ht->callbacks.val_free(cur->val);
            }
            cur->key = NULL;
            cur->val = NULL;
            free(cur);
            cur = NULL;
            ht->used_buckets--;
            break;
        }

        prev = cur;
        cur = cur->next;
    }
}

/**
 * @brief Get a table bucket value given its key
 * @param ht Pointer to the hash table
 * @param key Pointer to the key
 * @param val Pointer to store the retrieved value
 * @return true if the key was found, false otherwise
 */
static bool __ht_get(const ht_t *ht, const void *key, void **val) {
    if (!ht || !key) {
        return false;
    }

    const ht_bucket_t *cur = NULL;
    const size_t idx = __ht_bucket_index(ht, key);

    if (!ht->buckets[idx].key) {
        return false;
    }

    cur = ht->buckets + idx;
    while (cur) {
        if (ht->keyeq(key, cur->key)) {
            *val = (void *)cur->val;
            return true;
        }
        cur = cur->next;
    }

    return false;
}

/**
 * @brief Get a table bucket value given its key
 *
 * Wrapper around __ht_get for external use.
 *
 * @param ht Pointer to the hash table
 * @param key Pointer to the key
 * @return Pointer to the value, or NULL if not found
 */
void *ht_get(const ht_t *ht, const void *key) {
    void *val = NULL;
    __ht_get(ht, key, &val);
    return val;
}

/**
 * @brief Create a table enumeration object
 * @param ht Pointer to the hash table
 * @return Pointer to the enumeration object, or NULL on failure
 */
ht_enum_t *ht_enum_create(ht_t *ht) {
    ht_enum_t *he = NULL;

    if (!ht) {
        return NULL;
    }

    he = calloc(1, sizeof(*he));
    if (!he) {
        perror("ht_enum_create");
        return NULL;
    }
    he->ht = ht;

    return he;
}

/**
 * @brief Get the key value information of the next bucket in a table
 *
 * Iterates through each bucket and returns the bucket data. Handles
 * chained buckets by traversing the chain until done, then moving
 * to the next bucket.
 *
 * @param he Pointer to the enumeration object
 * @param key Pointer to store the key (can be NULL)
 * @param val Pointer to store the value (can be NULL)
 * @return true if a next bucket was found, false if enumeration is complete
 */
bool ht_enum_next(ht_enum_t *he, const void **key, const void **val) {
    const void *mykey = NULL, *myval = NULL;

    if (!he || he->idx >= he->ht->capacity) {
        return false;
    }

    if (!key) {
        key = &mykey;
    }

    if (!val) {
        val = &myval;
    }

    if (!he->cur) {
        while (he->idx < he->ht->capacity && !he->ht->buckets[he->idx].key) {
            he->idx++;
        }

        if (he->idx >= he->ht->capacity) {
            return false;
        }

        he->cur = he->ht->buckets + he->idx;
        he->idx++;
    }

    *key = he->cur->key;
    *val = he->cur->val;
    he->cur = he->cur->next;

    return true;
}

/**
 * @brief Destroy an enumeration object
 * @param he Pointer to the enumeration object to destroy
 */
void ht_enum_destroy(ht_enum_t *he) {
    if (!he) {
        return;
    }

    free(he);
    he = NULL;
}
