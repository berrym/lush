/**
 * @file custom_source.c
 * @brief Custom Completion Source API Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Public API for registering custom completion sources programmatically.
 * This is Layer 2 of the completion extensibility architecture.
 *
 * Architecture:
 *   Layer 3: User Config (completions.toml) - uses this API
 *   Layer 2: Public C API (this file)
 *   Layer 1: Internal Source Manager (source_manager.c)
 */

#include "lle/completion/custom_source.h"
#include "lle/completion/source_manager.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * CUSTOM SOURCE REGISTRY
 *
 * Tracks registered custom sources for lifecycle management.
 * ============================================================================
 */

#define MAX_CUSTOM_SOURCES 32

/**
 * Custom source entry - wraps user source with internal state
 */
typedef struct custom_source_entry {
    lle_custom_completion_source_t
        source;             /* Copy of user's source definition */
    char *name_copy;        /* Owned copy of name string */
    char *description_copy; /* Owned copy of description */
    bool registered;        /* Currently registered in manager */
} custom_source_entry_t;

/**
 * Global custom source registry
 */
static struct {
    custom_source_entry_t entries[MAX_CUSTOM_SOURCES];
    size_t count;
    lle_source_manager_t *source_manager; /* Active source manager */
    lle_memory_pool_t *pool;              /* Memory pool for allocations */
    pthread_mutex_t mutex;                /* Thread safety */
    bool initialized;
} g_custom_registry = {.count = 0,
                       .source_manager = NULL,
                       .pool = NULL,
                       .mutex = PTHREAD_MUTEX_INITIALIZER,
                       .initialized = false};

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Find entry by name
 *
 * Searches the custom source registry for an entry with the given name.
 *
 * @param name Source name to search for
 * @return Pointer to entry if found, NULL otherwise
 */
static custom_source_entry_t *find_entry_by_name(const char *name) {
    for (size_t i = 0; i < g_custom_registry.count; i++) {
        if (g_custom_registry.entries[i].registered &&
            strcmp(g_custom_registry.entries[i].name_copy, name) == 0) {
            return &g_custom_registry.entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Wrapper generate function - adapts custom source callback to internal
 * format
 *
 * The internal source manager passes (pool, context, prefix, result) but
 * custom sources expect (user_data, context, prefix, result).
 *
 * @param pool Memory pool for allocations (available in result->pool)
 * @param context Context analyzer for command position analysis
 * @param prefix Current word prefix to complete
 * @param result Output result to populate with completions
 * @return LLE_SUCCESS always (individual source errors are logged but not
 * propagated)
 */
static lle_result_t
custom_generate_wrapper(lle_memory_pool_t *pool,
                        const lle_context_analyzer_t *context,
                        const char *prefix, lle_completion_result_t *result) {

    (void)pool; /* Pool is available in result->pool if needed */

    /*
     * We need to find which custom source this call is for.
     * The internal source manager doesn't pass user_data through its callback,
     * so we use a different approach: the custom source stores its user_data
     * in the lle_completion_source_t.user_data field.
     *
     * For now, we iterate through registered sources to find a match.
     * This is O(n) but n is small (max 32).
     */

    /* Find which custom source is being queried based on the context */
    pthread_mutex_lock(&g_custom_registry.mutex);

    for (size_t i = 0; i < g_custom_registry.count; i++) {
        custom_source_entry_t *entry = &g_custom_registry.entries[i];
        if (!entry->registered) {
            continue;
        }

        /* Check if this source is applicable */
        if (entry->source.is_applicable) {
            if (!entry->source.is_applicable(entry->source.user_data,
                                             context)) {
                continue;
            }
        }

        /* Generate completions from this source */
        lle_result_t res = entry->source.generate(entry->source.user_data,
                                                  context, prefix, result);

        if (res != LLE_SUCCESS) {
            /* Log but continue - don't fail the whole query */
        }
    }

    pthread_mutex_unlock(&g_custom_registry.mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Wrapper applicability function - custom sources are always checked
 *
 * Individual applicability is checked in custom_generate_wrapper.
 *
 * @param context Context analyzer (unused, individual sources filter
 * themselves)
 * @return true always - custom sources filter internally
 */
static bool custom_applicable_wrapper(const lle_context_analyzer_t *context) {
    (void)context;
    return true; /* Always query custom sources; they filter internally */
}

/* ============================================================================
 * PUBLIC API - INITIALIZATION
 * ============================================================================
 */

lle_result_t lle_custom_source_init(lle_source_manager_t *manager,
                                    lle_memory_pool_t *pool) {
    if (!manager || !pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_custom_registry.mutex);

    if (g_custom_registry.initialized) {
        /* Already initialized - update manager reference */
        g_custom_registry.source_manager = (lle_source_manager_t *)manager;
        g_custom_registry.pool = (lle_memory_pool_t *)pool;
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return LLE_SUCCESS;
    }

    g_custom_registry.source_manager = (lle_source_manager_t *)manager;
    g_custom_registry.pool = (lle_memory_pool_t *)pool;
    g_custom_registry.count = 0;
    g_custom_registry.initialized = true;

    /*
     * Register a single "custom" meta-source in the manager.
     * This source dispatches to all registered custom sources.
     */
    lle_result_t res = lle_source_manager_register(
        manager, LLE_SOURCE_CUSTOM, "custom", custom_generate_wrapper,
        custom_applicable_wrapper);

    pthread_mutex_unlock(&g_custom_registry.mutex);
    return res;
}

void lle_custom_source_shutdown(void) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    /* Call cleanup callbacks for all registered sources */
    for (size_t i = 0; i < g_custom_registry.count; i++) {
        custom_source_entry_t *entry = &g_custom_registry.entries[i];
        if (entry->registered) {
            if (entry->source.cleanup) {
                entry->source.cleanup(entry->source.user_data);
            }
            free(entry->name_copy);
            free(entry->description_copy);
            entry->registered = false;
        }
    }

    g_custom_registry.count = 0;
    g_custom_registry.source_manager = NULL;
    g_custom_registry.pool = NULL;
    g_custom_registry.initialized = false;

    pthread_mutex_unlock(&g_custom_registry.mutex);
}

/* ============================================================================
 * PUBLIC API - REGISTRATION
 * ============================================================================
 */

lle_result_t
lle_completion_register_source(const lle_custom_completion_source_t *source) {

    if (!source || !source->name || !source->generate) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_custom_registry.mutex);

    if (!g_custom_registry.initialized) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return LLE_ERROR_NOT_INITIALIZED;
    }

    /* Check for duplicate name */
    if (find_entry_by_name(source->name)) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return LLE_ERROR_ALREADY_EXISTS;
    }

    /* Check capacity */
    if (g_custom_registry.count >= MAX_CUSTOM_SOURCES) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    /* Create entry */
    custom_source_entry_t *entry =
        &g_custom_registry.entries[g_custom_registry.count];

    /* Copy strings */
    entry->name_copy = strdup(source->name);
    if (!entry->name_copy) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    if (source->description) {
        entry->description_copy = strdup(source->description);
        if (!entry->description_copy) {
            free(entry->name_copy);
            pthread_mutex_unlock(&g_custom_registry.mutex);
            return LLE_ERROR_OUT_OF_MEMORY;
        }
    } else {
        entry->description_copy = NULL;
    }

    /* Copy source definition */
    entry->source = *source;
    entry->source.name = entry->name_copy;
    entry->source.description = entry->description_copy;
    entry->source.priority = source->priority > 0 ? source->priority : 500;
    entry->registered = true;

    g_custom_registry.count++;

    pthread_mutex_unlock(&g_custom_registry.mutex);
    return LLE_SUCCESS;
}

lle_result_t lle_completion_unregister_source(const char *name) {
    if (!name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&g_custom_registry.mutex);

    custom_source_entry_t *entry = find_entry_by_name(name);
    if (!entry) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return LLE_ERROR_NOT_FOUND;
    }

    /* Call cleanup if provided */
    if (entry->source.cleanup) {
        entry->source.cleanup(entry->source.user_data);
    }

    /* Free strings */
    free(entry->name_copy);
    free(entry->description_copy);

    /* Mark as unregistered */
    entry->registered = false;

    pthread_mutex_unlock(&g_custom_registry.mutex);
    return LLE_SUCCESS;
}

void lle_completion_unregister_all_custom_sources(void) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    for (size_t i = 0; i < g_custom_registry.count; i++) {
        custom_source_entry_t *entry = &g_custom_registry.entries[i];
        if (entry->registered) {
            if (entry->source.cleanup) {
                entry->source.cleanup(entry->source.user_data);
            }
            free(entry->name_copy);
            free(entry->description_copy);
            entry->registered = false;
        }
    }

    g_custom_registry.count = 0;

    pthread_mutex_unlock(&g_custom_registry.mutex);
}

/* ============================================================================
 * PUBLIC API - QUERY
 * ============================================================================
 */

size_t lle_completion_get_source_count(void) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    if (!g_custom_registry.initialized || !g_custom_registry.source_manager) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return 0;
    }

    size_t count = g_custom_registry.source_manager->num_sources;
    pthread_mutex_unlock(&g_custom_registry.mutex);
    return count;
}

size_t lle_completion_get_custom_source_count(void) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    size_t count = 0;
    for (size_t i = 0; i < g_custom_registry.count; i++) {
        if (g_custom_registry.entries[i].registered) {
            count++;
        }
    }

    pthread_mutex_unlock(&g_custom_registry.mutex);
    return count;
}

const char *lle_completion_get_source_name(size_t index) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    if (!g_custom_registry.initialized || !g_custom_registry.source_manager) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return NULL;
    }

    if (index >= g_custom_registry.source_manager->num_sources) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return NULL;
    }

    const char *name = g_custom_registry.source_manager->sources[index]->name;
    pthread_mutex_unlock(&g_custom_registry.mutex);
    return name;
}

bool lle_completion_source_is_custom(size_t index) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    if (!g_custom_registry.initialized || !g_custom_registry.source_manager) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return false;
    }

    if (index >= g_custom_registry.source_manager->num_sources) {
        pthread_mutex_unlock(&g_custom_registry.mutex);
        return false;
    }

    bool is_custom = g_custom_registry.source_manager->sources[index]->type ==
                     LLE_SOURCE_CUSTOM;
    pthread_mutex_unlock(&g_custom_registry.mutex);
    return is_custom;
}

const char *lle_completion_get_custom_source_name(size_t index) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    size_t current = 0;
    for (size_t i = 0; i < g_custom_registry.count; i++) {
        if (g_custom_registry.entries[i].registered) {
            if (current == index) {
                const char *name = g_custom_registry.entries[i].name_copy;
                pthread_mutex_unlock(&g_custom_registry.mutex);
                return name;
            }
            current++;
        }
    }

    pthread_mutex_unlock(&g_custom_registry.mutex);
    return NULL;
}

const char *lle_completion_get_custom_source_description(size_t index) {
    pthread_mutex_lock(&g_custom_registry.mutex);

    size_t current = 0;
    for (size_t i = 0; i < g_custom_registry.count; i++) {
        if (g_custom_registry.entries[i].registered) {
            if (current == index) {
                const char *desc =
                    g_custom_registry.entries[i].description_copy;
                pthread_mutex_unlock(&g_custom_registry.mutex);
                return desc;
            }
            current++;
        }
    }

    pthread_mutex_unlock(&g_custom_registry.mutex);
    return NULL;
}

bool lle_completion_source_exists(const char *name) {
    if (!name) {
        return false;
    }

    pthread_mutex_lock(&g_custom_registry.mutex);
    bool exists = (find_entry_by_name(name) != NULL);
    pthread_mutex_unlock(&g_custom_registry.mutex);

    return exists;
}

/* ============================================================================
 * PUBLIC API - HELPERS
 * ============================================================================
 */

lle_result_t lle_completion_add_item(lle_completion_result_t *result,
                                     const char *text, const char *suffix,
                                     const char *description, int score) {
    if (!result || !text) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Check capacity */
    if (result->count >= result->capacity) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    lle_completion_item_t *item = &result->items[result->count];

    /* Copy text - use pool if available */
    if (result->memory_pool) {
        char *text_copy = lle_pool_alloc(strlen(text) + 1);
        if (!text_copy) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        strcpy(text_copy, text);
        item->text = text_copy;
        item->owns_text = false; /* Pool-allocated, not individually owned */

        if (suffix) {
            char *suffix_copy = lle_pool_alloc(strlen(suffix) + 1);
            if (suffix_copy) {
                strcpy(suffix_copy, suffix);
                item->suffix = suffix_copy;
            } else {
                item->suffix = strdup(" "); /* Fallback */
                item->owns_suffix = true;
            }
        } else {
            item->suffix = strdup(" ");
            item->owns_suffix = true;
        }
        item->owns_suffix = false;

        if (description) {
            char *desc_copy = lle_pool_alloc(strlen(description) + 1);
            if (desc_copy) {
                strcpy(desc_copy, description);
                item->description = desc_copy;
            } else {
                item->description = NULL;
            }
        } else {
            item->description = NULL;
        }
        item->owns_description = false;
    } else {
        /* No pool - duplicate strings (caller can free theirs) */
        item->text = strdup(text);
        item->owns_text = true;
        item->suffix = strdup(suffix ? suffix : " ");
        item->owns_suffix = true;
        item->description = description ? strdup(description) : NULL;
        item->owns_description = (description != NULL);
    }

    item->type = LLE_COMPLETION_TYPE_CUSTOM;
    item->relevance_score = score > 0 ? score : 500;
    item->type_indicator = NULL; /* Will be set by type system */

    result->count++;
    result->custom_count++;

    return LLE_SUCCESS;
}

lle_result_t lle_completion_add_typed_item(lle_completion_result_t *result,
                                           const char *text, const char *suffix,
                                           const char *description,
                                           lle_completion_type_t type,
                                           int score) {
    if (!result || !text) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Check capacity */
    if (result->count >= result->capacity) {
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    lle_completion_item_t *item = &result->items[result->count];

    /* Copy text - use pool if available */
    if (result->memory_pool) {
        char *text_copy = lle_pool_alloc(strlen(text) + 1);
        if (!text_copy) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        strcpy(text_copy, text);
        item->text = text_copy;
        item->owns_text = false;

        if (suffix) {
            char *suffix_copy = lle_pool_alloc(strlen(suffix) + 1);
            if (suffix_copy) {
                strcpy(suffix_copy, suffix);
                item->suffix = suffix_copy;
            } else {
                item->suffix = strdup(" ");
                item->owns_suffix = true;
            }
        } else {
            item->suffix = strdup(" ");
            item->owns_suffix = true;
        }
        item->owns_suffix = false;

        if (description) {
            char *desc_copy = lle_pool_alloc(strlen(description) + 1);
            if (desc_copy) {
                strcpy(desc_copy, description);
                item->description = desc_copy;
            } else {
                item->description = NULL;
            }
        } else {
            item->description = NULL;
        }
        item->owns_description = false;
    } else {
        /* No pool - duplicate strings */
        item->text = strdup(text);
        item->owns_text = true;
        item->suffix = strdup(suffix ? suffix : " ");
        item->owns_suffix = true;
        item->description = description ? strdup(description) : NULL;
        item->owns_description = (description != NULL);
    }

    item->type = type;
    item->relevance_score = score > 0 ? score : 500;
    item->type_indicator = NULL;

    result->count++;

    /* Update type-specific count */
    if (type == LLE_COMPLETION_TYPE_CUSTOM) {
        result->custom_count++;
    }

    return LLE_SUCCESS;
}
