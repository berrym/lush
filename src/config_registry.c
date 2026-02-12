/**
 * @file config_registry.c
 * @brief Unified Configuration Registry Implementation
 *
 * Single source of truth for all shell configuration. Provides:
 * - Centralized key-value storage organized by sections
 * - Change notification system for reactive updates
 * - TOML file loading and saving
 * - Typed value access with defaults
 */

#include "config_registry.h"
#include "toml_parser.h"

#include "lle/unicode_compare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Unicode String Comparison Helper
 * ============================================================================
 */

/**
 * @brief Unicode-aware string equality check
 *
 * Uses LLE's Unicode comparison with NFC normalization to ensure
 * equivalent Unicode sequences compare as equal (e.g., precomposed
 * vs decomposed characters).
 *
 * @param s1 First string
 * @param s2 Second string
 * @return true if strings are equal after Unicode normalization
 */
static inline bool unicode_streq(const char *s1, const char *s2) {
    if (s1 == s2) return true;
    if (!s1 || !s2) return false;
    return lle_unicode_strings_equal(s1, s2, &LLE_UNICODE_COMPARE_DEFAULT);
}

/* ============================================================================
 * Internal Structures
 * ============================================================================
 */

/**
 * @brief Stored option value with metadata
 */
typedef struct {
    char key[CREG_KEY_MAX];           /**< Full key path */
    creg_value_t value;               /**< Current value */
    creg_value_t default_val;         /**< Default value */
    const creg_option_t *option_def;  /**< Pointer to option definition */
    bool persisted;                     /**< Should be saved to file */
} stored_option_t;

/**
 * @brief Registered section with its options
 */
typedef struct {
    creg_section_t section;                      /**< Section definition */
    stored_option_t options[CREG_OPTIONS_PER_SECTION_MAX];
    size_t option_count;
} registered_section_t;

/**
 * @brief Change notification subscriber
 */
typedef struct {
    char pattern[CREG_KEY_MAX];
    creg_change_callback_t callback;
    void *user_data;
    bool active;
} subscriber_t;

/**
 * @brief Global registry state
 */
typedef struct {
    bool initialized;
    registered_section_t sections[CREG_SECTION_MAX];
    size_t section_count;
    subscriber_t subscribers[CREG_SUBSCRIBERS_MAX];
    size_t subscriber_count;
} registry_state_t;

static registry_state_t g_registry = {0};

/* ============================================================================
 * Internal Helpers
 * ============================================================================
 */

/**
 * @brief Parse a full key into section and option parts
 *
 * @param key    Full key (e.g., "shell.errexit")
 * @param section Output section name
 * @param section_len Section buffer length
 * @param option Output option name
 * @param option_len Option buffer length
 * @return true if parsed successfully
 */
static bool parse_key(const char *key, char *section, size_t section_len,
                      char *option, size_t option_len) {
    if (!key || !section || !option) {
        return false;
    }

    const char *dot = strchr(key, '.');
    if (!dot) {
        /* No section, just option name - use empty section */
        section[0] = '\0';
        snprintf(option, option_len, "%s", key);
        return true;
    }

    size_t section_part_len = (size_t)(dot - key);
    if (section_part_len >= section_len) {
        return false;
    }

    memcpy(section, key, section_part_len);
    section[section_part_len] = '\0';
    snprintf(option, option_len, "%s", dot + 1);
    return true;
}

/**
 * @brief Find a registered section by name
 */
static registered_section_t *find_section(const char *name) {
    for (size_t i = 0; i < g_registry.section_count; i++) {
        if (unicode_streq(g_registry.sections[i].section.name, name)) {
            return &g_registry.sections[i];
        }
    }
    return NULL;
}

/**
 * @brief Find a stored option by full key
 */
static stored_option_t *find_option(const char *key) {
    char section_name[CREG_KEY_MAX];
    char option_name[CREG_KEY_MAX];

    if (!parse_key(key, section_name, sizeof(section_name),
                   option_name, sizeof(option_name))) {
        return NULL;
    }

    registered_section_t *section = find_section(section_name);
    if (!section) {
        return NULL;
    }

    for (size_t i = 0; i < section->option_count; i++) {
        /* Compare just the option part of the stored key */
        const char *stored_dot = strchr(section->options[i].key, '.');
        const char *stored_option = stored_dot ? stored_dot + 1 : section->options[i].key;
        if (unicode_streq(stored_option, option_name)) {
            return &section->options[i];
        }
    }

    return NULL;
}

/**
 * @brief Check if a key matches a subscriber pattern
 *
 * Uses Unicode-aware prefix matching for section wildcards.
 */
static bool pattern_matches(const char *pattern, const char *key) {
    if (!pattern || !key) {
        return false;
    }

    /* Global wildcard */
    if (unicode_streq(pattern, "*")) {
        return true;
    }

    /* Section wildcard (e.g., "shell.*") */
    size_t plen = strlen(pattern);
    if (plen >= 2 && pattern[plen - 1] == '*' && pattern[plen - 2] == '.') {
        /* Extract prefix without the trailing '*' (keep the dot) */
        /* e.g., "shell.*" -> match prefix "shell." against key */
        size_t prefix_len = plen - 1;  /* Length without the '*' */
        return lle_unicode_is_prefix(pattern, prefix_len, key, strlen(key),
                                     &LLE_UNICODE_COMPARE_DEFAULT);
    }

    /* Exact match with Unicode normalization */
    return unicode_streq(pattern, key);
}

/**
 * @brief Notify subscribers of a value change
 *
 * Re-entrancy guard prevents infinite recursion if a callback
 * modifies a config value (which would trigger notify_change again).
 */
static void notify_change(const char *key, const creg_value_t *old_value,
                          const creg_value_t *new_value) {
    static bool in_notify = false;
    if (in_notify) {
        return;
    }

    in_notify = true;
    for (size_t i = 0; i < g_registry.subscriber_count; i++) {
        subscriber_t *sub = &g_registry.subscribers[i];
        if (sub->active && pattern_matches(sub->pattern, key)) {
            sub->callback(key, old_value, new_value, sub->user_data);
        }
    }
    in_notify = false;
}

/* ============================================================================
 * Registry Lifecycle
 * ============================================================================
 */

creg_result_t config_registry_init(void) {
    if (g_registry.initialized) {
        return CREG_SUCCESS; /* Already initialized */
    }

    memset(&g_registry, 0, sizeof(g_registry));
    g_registry.initialized = true;

    return CREG_SUCCESS;
}

void config_registry_cleanup(void) {
    memset(&g_registry, 0, sizeof(g_registry));
}

bool config_registry_is_initialized(void) {
    return g_registry.initialized;
}

/* ============================================================================
 * Section Registration
 * ============================================================================
 */

creg_result_t config_registry_register_section(const creg_section_t *section) {
    if (!section || !section->name) {
        return CREG_ERROR_INVALID_PARAM;
    }

    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }

    /* Check for duplicate */
    if (find_section(section->name) != NULL) {
        return CREG_SUCCESS; /* Already registered */
    }

    if (g_registry.section_count >= CREG_SECTION_MAX) {
        return CREG_ERROR_SECTION_FULL;
    }

    /* Create new registered section */
    registered_section_t *reg = &g_registry.sections[g_registry.section_count];
    reg->section = *section;
    reg->option_count = 0;

    /* Register all options with default values */
    for (size_t i = 0; i < section->option_count; i++) {
        if (reg->option_count >= CREG_OPTIONS_PER_SECTION_MAX) {
            return CREG_ERROR_OPTION_FULL;
        }

        const creg_option_t *opt = &section->options[i];
        stored_option_t *stored = &reg->options[reg->option_count];

        /* Build full key path */
        snprintf(stored->key, sizeof(stored->key), "%s.%s",
                 section->name, opt->name);

        stored->value = opt->default_val;
        stored->default_val = opt->default_val;
        stored->option_def = opt;
        stored->persisted = opt->persisted;

        reg->option_count++;
    }

    g_registry.section_count++;
    return CREG_SUCCESS;
}

const creg_section_t *config_registry_get_section(const char *name) {
    registered_section_t *reg = find_section(name);
    return reg ? &reg->section : NULL;
}

/* ============================================================================
 * Value Access
 * ============================================================================
 */

creg_result_t config_registry_set(const char *key, const creg_value_t *value) {
    if (!key || !value) {
        return CREG_ERROR_INVALID_PARAM;
    }

    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }

    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }

    /* Check type compatibility (allow setting any type if option accepts it) */
    if (opt->option_def && opt->option_def->type != CREG_VALUE_NONE &&
        opt->option_def->type != value->type) {
        return CREG_ERROR_TYPE_MISMATCH;
    }

    /* Check if value actually changed */
    if (creg_value_equal(&opt->value, value)) {
        return CREG_SUCCESS; /* No change */
    }

    /* Store old value for notification */
    creg_value_t old_value = opt->value;

    /* Update value */
    opt->value = *value;

    /* Notify subscribers */
    notify_change(key, &old_value, value);

    return CREG_SUCCESS;
}

creg_result_t config_registry_get(const char *key, creg_value_t *value) {
    if (!key || !value) {
        return CREG_ERROR_INVALID_PARAM;
    }

    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }

    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }

    *value = opt->value;
    return CREG_SUCCESS;
}

bool config_registry_exists(const char *key) {
    return find_option(key) != NULL;
}

/* ============================================================================
 * Typed Value Access
 * ============================================================================
 */

creg_result_t config_registry_set_string(const char *key, const char *value) {
    if (!value) {
        return CREG_ERROR_INVALID_PARAM;
    }
    creg_value_t v = creg_value_string(value);
    return config_registry_set(key, &v);
}

creg_result_t config_registry_get_string(const char *key, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return CREG_ERROR_INVALID_PARAM;
    }

    creg_value_t v;
    creg_result_t result = config_registry_get(key, &v);
    if (result != CREG_SUCCESS) {
        return result;
    }

    if (v.type != CREG_VALUE_STRING) {
        return CREG_ERROR_TYPE_MISMATCH;
    }

    snprintf(out, out_len, "%s", v.data.string);
    return CREG_SUCCESS;
}

creg_result_t config_registry_set_integer(const char *key, int64_t value) {
    creg_value_t v = creg_value_integer(value);
    return config_registry_set(key, &v);
}

creg_result_t config_registry_get_integer(const char *key, int64_t *out) {
    if (!out) {
        return CREG_ERROR_INVALID_PARAM;
    }

    creg_value_t v;
    creg_result_t result = config_registry_get(key, &v);
    if (result != CREG_SUCCESS) {
        return result;
    }

    if (v.type != CREG_VALUE_INTEGER) {
        return CREG_ERROR_TYPE_MISMATCH;
    }

    *out = v.data.integer;
    return CREG_SUCCESS;
}

creg_result_t config_registry_set_boolean(const char *key, bool value) {
    creg_value_t v = creg_value_boolean(value);
    return config_registry_set(key, &v);
}

creg_result_t config_registry_get_boolean(const char *key, bool *out) {
    if (!out) {
        return CREG_ERROR_INVALID_PARAM;
    }

    creg_value_t v;
    creg_result_t result = config_registry_get(key, &v);
    if (result != CREG_SUCCESS) {
        return result;
    }

    if (v.type != CREG_VALUE_BOOLEAN) {
        return CREG_ERROR_TYPE_MISMATCH;
    }

    *out = v.data.boolean;
    return CREG_SUCCESS;
}

/* ============================================================================
 * Change Notification
 * ============================================================================
 */

creg_result_t config_registry_subscribe(const char *pattern,
                                          creg_change_callback_t callback,
                                          void *user_data) {
    if (!pattern || !callback) {
        return CREG_ERROR_INVALID_PARAM;
    }

    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }

    /* Find free slot or reuse inactive slot */
    subscriber_t *slot = NULL;
    for (size_t i = 0; i < g_registry.subscriber_count; i++) {
        if (!g_registry.subscribers[i].active) {
            slot = &g_registry.subscribers[i];
            break;
        }
    }

    if (!slot) {
        if (g_registry.subscriber_count >= CREG_SUBSCRIBERS_MAX) {
            return CREG_ERROR_SUBSCRIBER_FULL;
        }
        slot = &g_registry.subscribers[g_registry.subscriber_count++];
    }

    snprintf(slot->pattern, sizeof(slot->pattern), "%s", pattern);
    slot->callback = callback;
    slot->user_data = user_data;
    slot->active = true;

    return CREG_SUCCESS;
}

creg_result_t config_registry_unsubscribe(creg_change_callback_t callback) {
    if (!callback) {
        return CREG_ERROR_INVALID_PARAM;
    }

    for (size_t i = 0; i < g_registry.subscriber_count; i++) {
        if (g_registry.subscribers[i].callback == callback) {
            g_registry.subscribers[i].active = false;
            return CREG_SUCCESS;
        }
    }

    return CREG_ERROR_NOT_FOUND;
}

/* ============================================================================
 * TOML Parser Callback
 * ============================================================================
 */

/**
 * @brief Context for TOML loading
 */
typedef struct {
    creg_result_t result;
    char error_msg[256];
} load_context_t;

/**
 * @brief TOML parser callback for loading config
 */
static toml_result_t load_callback(const char *section, const char *key,
                                   const toml_value_t *value, void *user_data) {
    load_context_t *ctx = (load_context_t *)user_data;

    /* Build full key path */
    char full_key[CREG_KEY_MAX];
    if (section && section[0]) {
        snprintf(full_key, sizeof(full_key), "%s.%s", section, key);
    } else {
        snprintf(full_key, sizeof(full_key), "%s", key);
    }

    /* Convert TOML value to config value */
    creg_value_t config_val = {0};

    switch (value->type) {
    case TOML_VALUE_STRING:
        config_val.type = CREG_VALUE_STRING;
        snprintf(config_val.data.string, sizeof(config_val.data.string),
                 "%s", value->data.string);
        break;

    case TOML_VALUE_INTEGER:
        config_val.type = CREG_VALUE_INTEGER;
        config_val.data.integer = value->data.integer;
        break;

    case TOML_VALUE_BOOLEAN:
        config_val.type = CREG_VALUE_BOOLEAN;
        config_val.data.boolean = value->data.boolean;
        break;

    default:
        /* Skip unsupported types (arrays, tables) */
        return TOML_SUCCESS;
    }

    /* Try to set the value (ignore errors for unknown keys) */
    creg_result_t result = config_registry_set(full_key, &config_val);
    if (result != CREG_SUCCESS && result != CREG_ERROR_NOT_FOUND) {
        ctx->result = result;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Error setting '%s'", full_key);
    }

    return TOML_SUCCESS;
}

/* ============================================================================
 * Persistence
 * ============================================================================
 */

creg_result_t config_registry_load(const char *path) {
    if (!path) {
        return CREG_ERROR_INVALID_PARAM;
    }

    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }

    /* Read file contents */
    FILE *file = fopen(path, "r");
    if (!file) {
        return CREG_ERROR_IO_FAILED;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size <= 0) {
        fclose(file);
        return CREG_SUCCESS; /* Empty file is valid */
    }

    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(file);
        return CREG_ERROR_OUT_OF_MEMORY;
    }

    size_t read = fread(content, 1, (size_t)size, file);
    content[read] = '\0';
    fclose(file);

    /* Parse TOML */
    toml_parser_t parser;
    toml_result_t toml_result = toml_parser_init(&parser, content);
    if (toml_result != TOML_SUCCESS) {
        free(content);
        return CREG_ERROR_PARSE_FAILED;
    }

    load_context_t ctx = {.result = CREG_SUCCESS};
    toml_result = toml_parser_parse(&parser, load_callback, &ctx);

    toml_parser_cleanup(&parser);
    free(content);

    if (toml_result != TOML_SUCCESS) {
        return CREG_ERROR_PARSE_FAILED;
    }

    /* Call on_load hooks for all sections */
    for (size_t i = 0; i < g_registry.section_count; i++) {
        if (g_registry.sections[i].section.on_load) {
            g_registry.sections[i].section.on_load();
        }
    }

    return ctx.result;
}

creg_result_t config_registry_save(const char *path) {
    if (!path) {
        return CREG_ERROR_INVALID_PARAM;
    }

    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }

    FILE *file = fopen(path, "w");
    if (!file) {
        return CREG_ERROR_IO_FAILED;
    }

    fprintf(file, "# Lush Shell Configuration\n");
    fprintf(file, "# Generated by lush - edit with care\n\n");

    /* Write each section */
    for (size_t i = 0; i < g_registry.section_count; i++) {
        registered_section_t *reg = &g_registry.sections[i];
        bool section_written = false;

        /* Check if section has custom save hook */
        if (reg->section.on_save) {
            fprintf(file, "[%s]\n", reg->section.name);
            reg->section.on_save(file);
            fprintf(file, "\n");
            continue;
        }

        /* Write non-default, persisted values */
        for (size_t j = 0; j < reg->option_count; j++) {
            stored_option_t *opt = &reg->options[j];

            /* Skip non-persisted options */
            if (!opt->persisted) {
                continue;
            }

            /* Skip options with default values (sparse format) */
            if (creg_value_equal(&opt->value, &opt->default_val)) {
                continue;
            }

            /* Write section header if not yet written */
            if (!section_written) {
                fprintf(file, "[%s]\n", reg->section.name);
                section_written = true;
            }

            /* Extract just the option name from full key */
            const char *dot = strchr(opt->key, '.');
            const char *option_name = dot ? dot + 1 : opt->key;

            /* Write value based on type */
            switch (opt->value.type) {
            case CREG_VALUE_STRING:
                fprintf(file, "%s = \"%s\"\n", option_name, opt->value.data.string);
                break;
            case CREG_VALUE_INTEGER:
                fprintf(file, "%s = %lld\n", option_name,
                        (long long)opt->value.data.integer);
                break;
            case CREG_VALUE_BOOLEAN:
                fprintf(file, "%s = %s\n", option_name,
                        opt->value.data.boolean ? "true" : "false");
                break;
            case CREG_VALUE_FLOAT:
                fprintf(file, "%s = %g\n", option_name, opt->value.data.floating);
                break;
            default:
                break;
            }
        }

        if (section_written) {
            fprintf(file, "\n");
        }
    }

    fclose(file);
    return CREG_SUCCESS;
}

/* ============================================================================
 * Sync Operations
 * ============================================================================
 */

void config_registry_sync_to_runtime(void) {
    for (size_t i = 0; i < g_registry.section_count; i++) {
        if (g_registry.sections[i].section.sync_to_runtime) {
            g_registry.sections[i].section.sync_to_runtime();
        }
    }
}

void config_registry_sync_from_runtime(void) {
    for (size_t i = 0; i < g_registry.section_count; i++) {
        if (g_registry.sections[i].section.sync_from_runtime) {
            g_registry.sections[i].section.sync_from_runtime();
        }
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

creg_result_t config_registry_reset(const char *key) {
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }

    /* Restore default value */
    creg_value_t old_value = opt->value;
    opt->value = opt->default_val;

    /* Notify if changed */
    if (!creg_value_equal(&old_value, &opt->value)) {
        notify_change(key, &old_value, &opt->value);
    }

    return CREG_SUCCESS;
}

creg_result_t config_registry_reset_section(const char *section_name) {
    registered_section_t *section = find_section(section_name);
    if (!section) {
        return CREG_ERROR_NOT_FOUND;
    }

    for (size_t i = 0; i < section->option_count; i++) {
        stored_option_t *opt = &section->options[i];
        creg_value_t old_value = opt->value;
        opt->value = opt->default_val;

        if (!creg_value_equal(&old_value, &opt->value)) {
            notify_change(opt->key, &old_value, &opt->value);
        }
    }

    return CREG_SUCCESS;
}

void config_registry_reset_all(void) {
    for (size_t i = 0; i < g_registry.section_count; i++) {
        config_registry_reset_section(g_registry.sections[i].section.name);
    }
}

creg_result_t config_registry_get_default(const char *key, creg_value_t *value) {
    if (!value) {
        return CREG_ERROR_INVALID_PARAM;
    }

    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }

    *value = opt->default_val;
    return CREG_SUCCESS;
}

bool config_registry_is_default(const char *key) {
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return false;
    }

    return creg_value_equal(&opt->value, &opt->default_val);
}

/* ============================================================================
 * Value Helpers
 * ============================================================================
 */

bool creg_value_equal(const creg_value_t *a, const creg_value_t *b) {
    if (!a || !b) {
        return a == b;
    }

    if (a->type != b->type) {
        return false;
    }

    switch (a->type) {
    case CREG_VALUE_NONE:
        return true;

    case CREG_VALUE_STRING:
        /* Use Unicode-aware comparison for string values */
        return unicode_streq(a->data.string, b->data.string);

    case CREG_VALUE_INTEGER:
        return a->data.integer == b->data.integer;

    case CREG_VALUE_BOOLEAN:
        return a->data.boolean == b->data.boolean;

    case CREG_VALUE_FLOAT:
        return a->data.floating == b->data.floating;

    default:
        return false;
    }
}
