/**
 * @file widget_system.c
 * @brief LLE Widget Registry System Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implementation of the widget registry system providing widget registration,
 * lookup, and execution with performance tracking.
 *
 * Specification: docs/lle_specification/07_extensibility_framework_complete.md
 * Section: 4 - Widget System Architecture
 */

#include "lle/widget_system.h"
#include "lle/lle_editor.h"
#include <string.h>
#include <time.h>

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get current time in microseconds for performance tracking
 * @return Current monotonic time in microseconds, or 0 on error
 */
static uint64_t get_time_microseconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

/**
 * @brief Duplicate string using memory pool
 * @param pool Memory pool (reserved for future pool-based allocation)
 * @param str String to duplicate
 * @return Duplicated string, or NULL on allocation failure or NULL input
 */
static char *pool_strdup(lle_memory_pool_t *pool, const char *str) {
    (void)pool; // Reserved for pool-based allocation
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str) + 1;
    char *dup = lle_pool_alloc(len);
    if (!dup) {
        return NULL;
    }

    memcpy(dup, str, len);
    return dup;
}

/* ============================================================================
 * REGISTRY LIFECYCLE
 * ============================================================================
 */

lle_result_t lle_widget_registry_init(lle_widget_registry_t **registry,
                                      lle_memory_pool_t *memory_pool) {
    if (!registry || !memory_pool) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Allocate registry structure
    lle_widget_registry_t *reg = lle_pool_alloc(sizeof(lle_widget_registry_t));
    if (!reg) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(reg, 0, sizeof(lle_widget_registry_t));

    // Initialize hash table (16 buckets initially, will grow as needed).
    // Passthrough callbacks (zero-initialized): the table owns neither the
    // widget->name keys nor the widget pointers; widget_list owns lifetime.
    reg->widgets = ht_create(&(ht_options_t){.hash = ht_hash_fnv1a,
                                             .keyeq = str_eq,
                                             .keylen = str_len,
                                             .initial_capacity = 16});
    if (!reg->widgets) {
        lle_pool_free(reg);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize state
    reg->widget_list = NULL;
    reg->widget_count = 0;
    reg->memory_pool = memory_pool;
    reg->registry_active = true;

    *registry = reg;
    return LLE_SUCCESS;
}

lle_result_t lle_widget_registry_destroy(lle_widget_registry_t *registry) {
    if (!registry) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Mark as inactive to prevent concurrent access
    registry->registry_active = false;

    // Free all widgets in linked list
    lle_widget_t *widget = registry->widget_list;
    while (widget) {
        lle_widget_t *next = widget->next;

        // Free widget name
        if (widget->name) {
            lle_pool_free(widget->name);
        }

        // Free widget structure
        lle_pool_free(widget);

        widget = next;
    }

    // Destroy hash table
    if (registry->widgets) {
        ht_destroy(registry->widgets);
    }

    // Free registry structure
    lle_pool_free(registry);

    return LLE_SUCCESS;
}

/* ============================================================================
 * WIDGET MANAGEMENT
 * ============================================================================
 */

lle_result_t lle_widget_register(lle_widget_registry_t *registry,
                                 const char *name,
                                 lle_widget_callback_t callback,
                                 lle_widget_type_t type, void *user_data) {
    if (!registry || !name || !callback) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!registry->registry_active) {
        return LLE_ERROR_INVALID_STATE;
    }

    // Check for duplicate name
    void *existing = ht_get(registry->widgets, name);
    if (existing != NULL) {
        return LLE_ERROR_ALREADY_EXISTS;
    }

    // Allocate widget structure
    lle_widget_t *widget = lle_pool_alloc(sizeof(lle_widget_t));
    if (!widget) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    memset(widget, 0, sizeof(lle_widget_t));

    // Copy widget name
    widget->name = pool_strdup(registry->memory_pool, name);
    if (!widget->name) {
        lle_pool_free(widget);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    // Initialize widget fields
    widget->callback = callback;
    widget->type = type;
    widget->user_data = user_data;
    widget->execution_count = 0;
    widget->total_execution_time_us = 0;
    widget->enabled = true;
    widget->next = NULL;

    // Insert into hash table
    ht_insert(registry->widgets, widget->name, widget);

    // Add to linked list at head
    widget->next = registry->widget_list;
    registry->widget_list = widget;
    registry->widget_count++;

    return LLE_SUCCESS;
}

lle_result_t lle_widget_unregister(lle_widget_registry_t *registry,
                                   const char *name) {
    if (!registry || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!registry->registry_active) {
        return LLE_ERROR_INVALID_STATE;
    }

    // Lookup widget in hash table
    lle_widget_t *widget = (lle_widget_t *)ht_get(registry->widgets, name);
    if (!widget) {
        return LLE_ERROR_NOT_FOUND;
    }

    // Remove from hash table
    ht_remove(registry->widgets, name);

    // Remove from linked list
    if (registry->widget_list == widget) {
        // Widget is at head of list
        registry->widget_list = widget->next;
    } else {
        // Find previous widget in list
        lle_widget_t *prev = registry->widget_list;
        while (prev && prev->next != widget) {
            prev = prev->next;
        }

        if (prev) {
            prev->next = widget->next;
        }
    }

    registry->widget_count--;

    // Free widget resources
    if (widget->name) {
        lle_pool_free(widget->name);
    }
    lle_pool_free(widget);

    return LLE_SUCCESS;
}

lle_widget_t *lle_widget_lookup(lle_widget_registry_t *registry,
                                const char *name) {
    if (!registry || !name) {
        return NULL;
    }

    if (!registry->registry_active) {
        return NULL;
    }

    return (lle_widget_t *)ht_get(registry->widgets, name);
}

lle_result_t lle_widget_execute(lle_widget_registry_t *registry,
                                const char *name, lle_editor_t *editor) {
    if (!registry || !name || !editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Lookup widget
    lle_widget_t *widget = lle_widget_lookup(registry, name);
    if (!widget) {
        return LLE_ERROR_NOT_FOUND;
    }

    // Execute widget directly
    return lle_widget_execute_direct(widget, editor);
}

lle_result_t lle_widget_execute_direct(lle_widget_t *widget,
                                       lle_editor_t *editor) {
    if (!widget || !editor) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    // Check if widget is enabled
    if (!widget->enabled) {
        return LLE_ERROR_DISABLED;
    }

    // Verify callback exists
    if (!widget->callback) {
        return LLE_ERROR_INVALID_STATE;
    }

    // Track execution time
    uint64_t start_time = get_time_microseconds();

    // Execute widget callback
    lle_result_t result = widget->callback(editor, widget->user_data);

    // Update statistics
    uint64_t end_time = get_time_microseconds();
    uint64_t execution_time = end_time - start_time;

    widget->execution_count++;
    widget->total_execution_time_us += execution_time;

    return result;
}

/* ============================================================================
 * QUERY FUNCTIONS
 * ============================================================================
 */

size_t lle_widget_registry_get_count(lle_widget_registry_t *registry) {
    if (!registry) {
        return 0;
    }

    return registry->widget_count;
}

bool lle_widget_exists(lle_widget_registry_t *registry, const char *name) {
    if (!registry || !name) {
        return false;
    }

    return lle_widget_lookup(registry, name) != NULL;
}

lle_result_t lle_widget_enable(lle_widget_registry_t *registry,
                               const char *name) {
    if (!registry || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_widget_t *widget = lle_widget_lookup(registry, name);
    if (!widget) {
        return LLE_ERROR_NOT_FOUND;
    }

    widget->enabled = true;
    return LLE_SUCCESS;
}

lle_result_t lle_widget_disable(lle_widget_registry_t *registry,
                                const char *name) {
    if (!registry || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_widget_t *widget = lle_widget_lookup(registry, name);
    if (!widget) {
        return LLE_ERROR_NOT_FOUND;
    }

    widget->enabled = false;
    return LLE_SUCCESS;
}
