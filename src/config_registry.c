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

#include <limits.h>
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
    if (s1 == s2)
        return true;
    if (!s1 || !s2)
        return false;
    return lle_unicode_strings_equal(s1, s2, &LLE_UNICODE_COMPARE_DEFAULT);
}

/* ============================================================================
 * Type Descriptors (the type vtable)
 * ============================================================================
 *
 * One canonical validate/describe per type, attached to a registered key and
 * consulted at the set chokepoint. The check op enforces the semantic
 * constraint (enum membership, int range); the registry checks the storage kind
 * separately. describe feeds typed error messages and the wizard.
 */

/// bool/int/string have no semantic constraint beyond storage kind: any
/// well-typed value is valid.
static bool creg_check_always(const creg_type_t *self, const creg_value_t *v,
                              char *err, size_t errlen) {
    (void)self;
    (void)v;
    (void)err;
    (void)errlen;
    return true;
}

static void creg_describe_bool(const creg_type_t *self, char *out,
                               size_t outlen) {
    (void)self;
    snprintf(out, outlen, "true or false");
}

static void creg_describe_int(const creg_type_t *self, char *out,
                              size_t outlen) {
    (void)self;
    snprintf(out, outlen, "an integer");
}

static void creg_describe_string(const creg_type_t *self, char *out,
                                 size_t outlen) {
    (void)self;
    snprintf(out, outlen, "a string");
}

/// enum: the value's string must equal one of the descriptor's pair names.
static bool creg_check_enum(const creg_type_t *self, const creg_value_t *v,
                            char *err, size_t errlen) {
    if (!self->pairs) {
        return true;
    }
    for (const creg_enum_pair_t *p = self->pairs; p->name; p++) {
        if (unicode_streq(p->name, v->data.string)) {
            return true;
        }
    }
    snprintf(err, errlen, "not one of the allowed values");
    return false;
}

static void creg_describe_enum(const creg_type_t *self, char *out,
                               size_t outlen) {
    size_t off = (size_t)snprintf(out, outlen, "one of:");
    for (const creg_enum_pair_t *p = self->pairs; p && p->name; p++) {
        if (off >= outlen) {
            break;
        }
        off += (size_t)snprintf(out + off, outlen - off, " %s", p->name);
    }
}

/// int_range: the integer must lie within [min, max] inclusive.
static bool creg_check_int_range(const creg_type_t *self, const creg_value_t *v,
                                 char *err, size_t errlen) {
    if (v->data.integer < self->min || v->data.integer > self->max) {
        snprintf(err, errlen, "out of range");
        return false;
    }
    return true;
}

static void creg_describe_int_range(const creg_type_t *self, char *out,
                                    size_t outlen) {
    /// Render open-ended bounds in plain language instead of the numeric limit.
    /// A top at or above INT_MAX is "open" (the 32-bit cell ceiling that
    /// int-backed keys use as their non-negative cap), as is INT64_MAX;
    /// symmetrically for the bottom.
    bool open_lo = self->min <= INT_MIN;
    bool open_hi = self->max >= INT_MAX;
    if (open_lo && open_hi) {
        snprintf(out, outlen, "an integer");
    } else if (open_hi) {
        if (self->min == 0) {
            snprintf(out, outlen, "a non-negative integer");
        } else {
            snprintf(out, outlen, "an integer >= %lld", (long long)self->min);
        }
    } else if (open_lo) {
        snprintf(out, outlen, "an integer <= %lld", (long long)self->max);
    } else {
        snprintf(out, outlen, "an integer in %lld..%lld", (long long)self->min,
                 (long long)self->max);
    }
}

static const creg_type_t g_type_bool = {.name = "bool",
                                        .storage = CREG_VALUE_BOOLEAN,
                                        .pairs = NULL,
                                        .min = 0,
                                        .max = 0,
                                        .check = creg_check_always,
                                        .describe = creg_describe_bool};
static const creg_type_t g_type_int = {.name = "int",
                                       .storage = CREG_VALUE_INTEGER,
                                       .pairs = NULL,
                                       .min = 0,
                                       .max = 0,
                                       .check = creg_check_always,
                                       .describe = creg_describe_int};
static const creg_type_t g_type_string = {.name = "string",
                                          .storage = CREG_VALUE_STRING,
                                          .pairs = NULL,
                                          .min = 0,
                                          .max = 0,
                                          .check = creg_check_always,
                                          .describe = creg_describe_string};

const creg_type_t *creg_type_bool(void) { return &g_type_bool; }
const creg_type_t *creg_type_int(void) { return &g_type_int; }
const creg_type_t *creg_type_string(void) { return &g_type_string; }

void creg_type_init_enum(creg_type_t *out, const creg_enum_pair_t *pairs) {
    out->name = "enum";
    out->storage = CREG_VALUE_STRING;
    out->pairs = pairs;
    out->min = 0;
    out->max = 0;
    out->check = creg_check_enum;
    out->describe = creg_describe_enum;
}

void creg_type_init_int_range(creg_type_t *out, int64_t min, int64_t max) {
    out->name = "int_range";
    out->storage = CREG_VALUE_INTEGER;
    out->pairs = NULL;
    out->min = min;
    out->max = max;
    out->check = creg_check_int_range;
    out->describe = creg_describe_int_range;
}

/* ============================================================================
 * Internal Structures
 * ============================================================================
 */

/**
 * @brief Stored option value with metadata
 */
typedef struct {
    char key[CREG_KEY_MAX]; ///< Full key path
    /// One value per layer; effective = highest present (resolve_effective).
    /// slots[CREG_LAYER_DEFAULT] is always present after registration.
    creg_layer_view_t slots[CREG_LAYER_COUNT];
    const creg_option_t *option_def; ///< Pointer to option definition
    bool persisted;                  ///< Should be saved to file
    const creg_type_t *type;         ///< Optional type descriptor (validation)
    creg_tier_t tier;                ///< Discoverability tier (UNSET if none)
} stored_option_t;

/**
 * @brief Registered section with its options
 */
typedef struct {
    creg_section_t section; ///< Section definition
    /// Heap-allocated, sized to the options actually registered. Each
    /// stored_option carries one value slot per layer sized for the largest
    /// value kind, so the prior fixed worst-case array per section wasted
    /// megabytes of mostly-empty storage; growing on demand (section_reserve)
    /// removes both that waste and the fixed per-section option ceiling.
    stored_option_t *options;
    size_t options_capacity;
    size_t option_count;
} registered_section_t;

/// Grow a section's option array to hold at least @p needed entries. New
/// capacity is geometric. Returns false on allocation failure. Safe to call
/// during registration only: find_option() hands out stored_option_t pointers,
/// and a realloc here would invalidate any held across the call -- all
/// registration completes at startup before any such pointer is retained.
static bool section_reserve(registered_section_t *reg, size_t needed) {
    if (reg->options_capacity >= needed) {
        return true;
    }
    size_t newcap = reg->options_capacity ? reg->options_capacity * 2 : 8;
    while (newcap < needed) {
        newcap *= 2;
    }
    stored_option_t *grown =
        realloc(reg->options, newcap * sizeof(stored_option_t));
    if (!grown) {
        return false;
    }
    reg->options = grown;
    reg->options_capacity = newcap;
    return true;
}

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
 * @brief Per-mode default override entry
 *
 * Each entry binds an option's full key to a specific mode and the
 * default value for that (key, mode) pair. apply_mode_defaults walks
 * this table when a mode change happens.
 */
typedef struct {
    char key[CREG_KEY_MAX];
    shell_mode_t mode;
    creg_value_t value;
    bool active;
} mode_default_t;

/**
 * @brief Runtime binding kinds (the cell's storage shape)
 */
typedef enum {
    CREG_BIND_BOOL,
    CREG_BIND_INT,
    CREG_BIND_STRING,     ///< fixed-size char buffer (cell_size bytes)
    CREG_BIND_STRING_PTR, ///< owned `char *` cell (registry frees/copies)
    CREG_BIND_ENUM_INT    ///< registry string mapped onto an engine enum (int)
} creg_bind_kind_t;

/**
 * @brief A binding from a registered key to a runtime cell
 *
 * On every change to the key the cell is written through (see apply_binding).
 * This is the typed, declarative replacement for the per-section
 * sync_to_runtime / sync_from_runtime hooks.
 */
typedef struct {
    char key[CREG_KEY_MAX];
    creg_bind_kind_t kind;
    void *cell;                    ///< the real runtime lvalue
    size_t cell_size;              ///< for STRING: buffer capacity
    const creg_enum_pair_t *pairs; ///< for ENUM_INT: string->int mapping
    int64_t fallback;              ///< for ENUM_INT: value when unmatched
    bool active;
} binding_t;

/**
 * @brief Global registry state
 */
typedef struct {
    bool initialized;
    registered_section_t sections[CREG_SECTION_MAX];
    size_t section_count;
    subscriber_t subscribers[CREG_SUBSCRIBERS_MAX];
    size_t subscriber_count;
    mode_default_t mode_defaults[CREG_MODE_DEFAULTS_MAX];
    size_t mode_default_count;
    binding_t bindings[CREG_BINDINGS_MAX];
    size_t binding_count;
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
        /// No section, just option name - use empty section
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

/// @brief Find a registered section by name
static registered_section_t *find_section(const char *name) {
    for (size_t i = 0; i < g_registry.section_count; i++) {
        if (unicode_streq(g_registry.sections[i].section.name, name)) {
            return &g_registry.sections[i];
        }
    }
    return NULL;
}

/// @brief Find a stored option by full key
static stored_option_t *find_option(const char *key) {
    char section_name[CREG_KEY_MAX];
    char option_name[CREG_KEY_MAX];

    if (!parse_key(key, section_name, sizeof(section_name), option_name,
                   sizeof(option_name))) {
        return NULL;
    }

    registered_section_t *section = find_section(section_name);
    if (!section) {
        return NULL;
    }

    for (size_t i = 0; i < section->option_count; i++) {
        /// Compare just the option part of the stored key
        const char *stored_dot = strchr(section->options[i].key, '.');
        const char *stored_option =
            stored_dot ? stored_dot + 1 : section->options[i].key;
        if (unicode_streq(stored_option, option_name)) {
            return &section->options[i];
        }
    }

    return NULL;
}

/// Resolve an option's effective value: the highest-precedence present layer.
/// slots[DEFAULT] is always present after registration, so this never returns
/// NULL for a registered option. Reports the winning layer when @p winning is
/// non-NULL.
static const creg_value_t *resolve_effective(const stored_option_t *opt,
                                             creg_layer_t *winning) {
    for (int layer = CREG_LAYER_COUNT - 1; layer >= 0; layer--) {
        if (opt->slots[layer].present) {
            if (winning) {
                *winning = (creg_layer_t)layer;
            }
            return &opt->slots[layer].value;
        }
    }
    if (winning) {
        *winning = CREG_LAYER_DEFAULT;
    }
    return &opt->slots[CREG_LAYER_DEFAULT].value;
}

const char *config_registry_layer_name(creg_layer_t layer) {
    switch (layer) {
    case CREG_LAYER_DEFAULT:
        return "default";
    case CREG_LAYER_MODE:
        return "mode";
    case CREG_LAYER_SYSTEM:
        return "system";
    case CREG_LAYER_USER:
        return "user-toml";
    case CREG_LAYER_SESSION:
        return "session";
    default:
        return "?";
    }
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

    /// Global wildcard
    if (unicode_streq(pattern, "*")) {
        return true;
    }

    /// Section wildcard (e.g., "shell.*")
    size_t plen = strlen(pattern);
    if (plen >= 2 && pattern[plen - 1] == '*' && pattern[plen - 2] == '.') {
        /// Extract prefix without the trailing '*' (keep the dot)
        /// e.g., "shell.*" -> match prefix "shell." against key
        size_t prefix_len = plen - 1; /// Length without the '*'
        return lle_unicode_is_prefix(pattern, prefix_len, key, strlen(key),
                                     &LLE_UNICODE_COMPARE_DEFAULT);
    }

    /// Exact match with Unicode normalization
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

/// Write a single binding's runtime cell from a registry value.
static void apply_binding(const binding_t *b, const creg_value_t *v) {
    switch (b->kind) {
    case CREG_BIND_BOOL:
        if (v->type == CREG_VALUE_BOOLEAN) {
            *(bool *)b->cell = v->data.boolean;
        }
        break;
    case CREG_BIND_INT:
        if (v->type == CREG_VALUE_INTEGER) {
            *(int *)b->cell = (int)v->data.integer;
        }
        break;
    case CREG_BIND_STRING:
        if (v->type == CREG_VALUE_STRING && b->cell && b->cell_size > 0) {
            snprintf((char *)b->cell, b->cell_size, "%s", v->data.string);
        }
        break;
    case CREG_BIND_STRING_PTR:
        if (v->type == CREG_VALUE_STRING) {
            char **pp = (char **)b->cell;
            char *prev = *pp;
            /// Empty string == unset: leave the cell NULL so the engine falls
            /// back to its own default rather than to a literal "".
            *pp = (v->data.string[0] != '\0') ? strdup(v->data.string) : NULL;
            free(prev);
        }
        break;
    case CREG_BIND_ENUM_INT:
        if (v->type == CREG_VALUE_STRING && b->pairs) {
            int64_t mapped = b->fallback;
            for (const creg_enum_pair_t *p = b->pairs; p->name; p++) {
                if (unicode_streq(p->name, v->data.string)) {
                    mapped = p->value;
                    break;
                }
            }
            *(int *)b->cell = (int)mapped;
        }
        break;
    }
}

/// Write through every binding registered for a key. Called from the single
/// writer (config_registry_set) on every change, so bound cells stay current
/// with no per-section sync hook.
static void apply_bindings_for_key(const char *key, const creg_value_t *v) {
    for (size_t i = 0; i < g_registry.binding_count; i++) {
        binding_t *b = &g_registry.bindings[i];
        if (b->active && unicode_streq(b->key, key)) {
            apply_binding(b, v);
        }
    }
}

/// The single write funnel: place @p value in @p layer of @p key, recompute the
/// effective value, and -- only if it actually changed -- write through bound
/// cells and notify subscribers. Every set path (interactive set, mode preset,
/// TOML load, default registration) routes here with its own layer, which is
/// what makes a mode preset unable to clobber an interactive tweak: each lands
/// in a different slot and the highest present wins.
static creg_result_t set_in_layer(const char *key, const creg_value_t *value,
                                  creg_layer_t layer, const char *origin) {
    if (!key || !value || layer >= CREG_LAYER_COUNT) {
        return CREG_ERROR_INVALID_PARAM;
    }
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }
    if (opt->option_def && opt->option_def->type != CREG_VALUE_NONE &&
        opt->option_def->type != value->type) {
        return CREG_ERROR_TYPE_MISMATCH;
    }

    /// When a type descriptor is attached, enforce its storage kind at the
    /// funnel -- regardless of the key's declared option_def type (which may be
    /// CREG_VALUE_NONE). This closes the typeless-key hole and lets the check
    /// op trust the union member it reads (enum -> data.string, int_range ->
    /// data.integer); without it a wrong-kind value could reach a check op that
    /// reinterprets bytes.
    if (opt->type && value->type != opt->type->storage) {
        return CREG_ERROR_TYPE_MISMATCH;
    }

    /// Semantic validation against the attached type descriptor (enum
    /// membership, int range). The storage kind is checked above; this rejects
    /// a well-typed but invalid value at the single write chokepoint, so config
    /// set, setopt, and a TOML load are all validated through one path.
    if (opt->type && opt->type->check) {
        char reason[64] = {0};
        if (!opt->type->check(opt->type, value, reason, sizeof(reason))) {
            return CREG_ERROR_INVALID_VALUE;
        }
    }

    creg_value_t old_eff = *resolve_effective(opt, NULL);

    opt->slots[layer].present = true;
    opt->slots[layer].value = *value;
    snprintf(opt->slots[layer].origin, sizeof(opt->slots[layer].origin), "%s",
             origin ? origin : config_registry_layer_name(layer));

    creg_value_t new_eff = *resolve_effective(opt, NULL);
    if (!creg_value_equal(&old_eff, &new_eff)) {
        apply_bindings_for_key(key, &new_eff);
        notify_change(key, &old_eff, &new_eff);
    }
    return CREG_SUCCESS;
}

/* ============================================================================
 * Registry Lifecycle
 * ============================================================================
 */

creg_result_t config_registry_init(void) {
    if (g_registry.initialized) {
        return CREG_SUCCESS; /// Already initialized
    }

    memset(&g_registry, 0, sizeof(g_registry));
    g_registry.initialized = true;

    return CREG_SUCCESS;
}

void config_registry_cleanup(void) {
    for (size_t i = 0; i < g_registry.section_count; i++) {
        free(g_registry.sections[i].options);
    }
    memset(&g_registry, 0, sizeof(g_registry));
}

bool config_registry_is_initialized(void) { return g_registry.initialized; }

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

    /// Check for duplicate
    if (find_section(section->name) != NULL) {
        return CREG_SUCCESS; /// Already registered
    }

    if (g_registry.section_count >= CREG_SECTION_MAX) {
        return CREG_ERROR_SECTION_FULL;
    }

    /// Create new registered section
    registered_section_t *reg = &g_registry.sections[g_registry.section_count];
    reg->section = *section;
    reg->options = NULL;
    reg->options_capacity = 0;
    reg->option_count = 0;

    if (section->option_count > 0 &&
        !section_reserve(reg, section->option_count)) {
        return CREG_ERROR_OPTION_FULL;
    }

    /// Register all options with default values
    for (size_t i = 0; i < section->option_count; i++) {
        const creg_option_t *opt = &section->options[i];
        stored_option_t *stored = &reg->options[reg->option_count];

        /// Heap memory is uninitialized; clear every layer slot so only the
        /// DEFAULT slot reads as present (the prior fixed array lived in the
        /// zero-initialized g_registry global).
        memset(stored, 0, sizeof(*stored));

        /// Build full key path
        snprintf(stored->key, sizeof(stored->key), "%s.%s", section->name,
                 opt->name);

        stored->slots[CREG_LAYER_DEFAULT].present = true;
        stored->slots[CREG_LAYER_DEFAULT].value = opt->default_val;
        snprintf(stored->slots[CREG_LAYER_DEFAULT].origin,
                 sizeof(stored->slots[CREG_LAYER_DEFAULT].origin), "default");
        stored->option_def = opt;
        stored->persisted = opt->persisted;

        reg->option_count++;
    }

    g_registry.section_count++;
    return CREG_SUCCESS;
}

creg_result_t config_registry_register_option(const char *section_name,
                                              const creg_option_t *option) {
    if (!g_registry.initialized || !section_name || !option || !option->name) {
        return CREG_ERROR_INVALID_PARAM;
    }
    registered_section_t *reg = find_section(section_name);
    if (!reg) {
        return CREG_ERROR_NOT_FOUND;
    }

    /// Re-registering an existing key is a no-op success.
    char full_key[CREG_KEY_MAX];
    snprintf(full_key, sizeof(full_key), "%s.%s", section_name, option->name);
    if (find_option(full_key) != NULL) {
        return CREG_SUCCESS;
    }

    if (!section_reserve(reg, reg->option_count + 1)) {
        return CREG_ERROR_OPTION_FULL;
    }

    stored_option_t *stored = &reg->options[reg->option_count];
    memset(stored, 0, sizeof(*stored));
    snprintf(stored->key, sizeof(stored->key), "%s", full_key);
    stored->slots[CREG_LAYER_DEFAULT].present = true;
    stored->slots[CREG_LAYER_DEFAULT].value = option->default_val;
    snprintf(stored->slots[CREG_LAYER_DEFAULT].origin,
             sizeof(stored->slots[CREG_LAYER_DEFAULT].origin), "default");
    stored->option_def = option;
    stored->persisted = option->persisted;
    reg->option_count++;
    return CREG_SUCCESS;
}

creg_result_t config_registry_set_mode_value(const char *key,
                                             const creg_value_t *value) {
    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }
    /// Writes the MODE layer directly (the same layer config_registry_apply_-
    /// mode_defaults uses). For option sets seeded per mode outside the static
    /// per-mode-default table -- the shell.feature.* matrix, which is far
    /// larger than CREG_MODE_DEFAULTS_MAX -- this lets the mode-change path
    /// push each feature's matrix default into the layered model so config get
    /// / show resolve through the registry rather than a special-case live
    /// read.
    return set_in_layer(key, value, CREG_LAYER_MODE, "mode preset");
}

const creg_section_t *config_registry_get_section(const char *name) {
    registered_section_t *reg = find_section(name);
    return reg ? &reg->section : NULL;
}

size_t config_registry_section_option_count(const char *section_name) {
    registered_section_t *reg = find_section(section_name);
    return reg ? reg->option_count : 0;
}

creg_result_t config_registry_section_option_key(const char *section_name,
                                                 size_t index, char *out,
                                                 size_t out_size) {
    if (!out || out_size == 0) {
        return CREG_ERROR_INVALID_PARAM;
    }
    registered_section_t *reg = find_section(section_name);
    if (!reg) {
        return CREG_ERROR_NOT_FOUND;
    }
    if (index >= reg->option_count) {
        return CREG_ERROR_INVALID_PARAM;
    }
    snprintf(out, out_size, "%s", reg->options[index].key);
    return CREG_SUCCESS;
}

/* ============================================================================
 * Value Access
 * ============================================================================
 */

creg_result_t config_registry_set(const char *key, const creg_value_t *value) {
    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }
    /// An interactive / runtime set lands in the SESSION layer -- the highest
    /// precedence, so it shadows mode presets and the config file until saved.
    return set_in_layer(key, value, CREG_LAYER_SESSION, "config set (session)");
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

    *value = *resolve_effective(opt, NULL);
    return CREG_SUCCESS;
}

bool config_registry_exists(const char *key) {
    return find_option(key) != NULL;
}

creg_result_t config_registry_inspect(const char *key, creg_inspect_t *out) {
    if (!key || !out) {
        return CREG_ERROR_INVALID_PARAM;
    }
    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }
    creg_layer_t winning = CREG_LAYER_DEFAULT;
    out->effective = *resolve_effective(opt, &winning);
    out->winning = winning;
    for (int layer = 0; layer < CREG_LAYER_COUNT; layer++) {
        out->layers[layer] = opt->slots[layer];
    }
    return CREG_SUCCESS;
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

creg_result_t config_registry_get_string(const char *key, char *out,
                                         size_t out_len) {
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
 * Runtime Bindings
 * ============================================================================
 */

/// Register a binding and immediately sync the cell to the key's current value.
/// Binding an unregistered key fails loudly -- this is what converts the old
/// "phantom sync" (silent no-op on a forgotten key) into a hard error.
static creg_result_t add_binding(const char *key, creg_bind_kind_t kind,
                                 void *cell, size_t cell_size,
                                 const creg_enum_pair_t *pairs,
                                 int64_t fallback) {
    if (!g_registry.initialized || !key || !cell) {
        return CREG_ERROR_INVALID_PARAM;
    }
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }
    if (g_registry.binding_count >= CREG_BINDINGS_MAX) {
        return CREG_ERROR_OPTION_FULL;
    }
    binding_t *b = &g_registry.bindings[g_registry.binding_count++];
    snprintf(b->key, sizeof(b->key), "%s", key);
    b->kind = kind;
    b->cell = cell;
    b->cell_size = cell_size;
    b->pairs = pairs;
    b->fallback = fallback;
    b->active = true;

    /// Initial sync: the cell takes the key's current effective value now.
    apply_binding(b, resolve_effective(opt, NULL));
    return CREG_SUCCESS;
}

creg_result_t config_registry_bind_boolean(const char *key, bool *cell) {
    return add_binding(key, CREG_BIND_BOOL, cell, 0, NULL, 0);
}

creg_result_t config_registry_bind_integer(const char *key, int *cell) {
    return add_binding(key, CREG_BIND_INT, cell, 0, NULL, 0);
}

creg_result_t config_registry_bind_string(const char *key, char *cell,
                                          size_t cell_size) {
    return add_binding(key, CREG_BIND_STRING, cell, cell_size, NULL, 0);
}

creg_result_t config_registry_bind_string_ptr(const char *key, char **cell) {
    return add_binding(key, CREG_BIND_STRING_PTR, cell, 0, NULL, 0);
}

creg_result_t config_registry_bind_enum(const char *key, int *cell,
                                        const creg_enum_pair_t *pairs,
                                        int64_t fallback) {
    return add_binding(key, CREG_BIND_ENUM_INT, cell, 0, pairs, fallback);
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

    /// Find free slot or reuse inactive slot
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
    creg_load_report_t
        *report; ///< Optional: records dropped keys (NULL = none)
} load_context_t;

/// @brief TOML parser callback for loading config
static toml_result_t load_callback(const char *section, const char *key,
                                   const toml_value_t *value, void *user_data) {
    load_context_t *ctx = (load_context_t *)user_data;

    /// Build full key path
    char full_key[CREG_KEY_MAX];
    if (section && section[0]) {
        snprintf(full_key, sizeof(full_key), "%s.%s", section, key);
    } else {
        snprintf(full_key, sizeof(full_key), "%s", key);
    }

    /// Convert TOML value to config value
    creg_value_t config_val = {0};

    switch (value->type) {
    case TOML_VALUE_STRING:
        config_val.type = CREG_VALUE_STRING;
        snprintf(config_val.data.string, sizeof(config_val.data.string), "%s",
                 value->data.string);
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
        /// Skip unsupported types (arrays, tables)
        return TOML_SUCCESS;
    }

    /// persisted=false keys never round-trip through the config file: persisted
    /// gates SAVE, so gate LOAD on it too. The three families are all
    /// runtime-determined rather than saved preferences: shell.monitor (job
    /// control, environmental -- set by interactivity), shell.posix (a pure
    /// projection of the active mode), and the 56 shell.feature.* keys (mode-
    /// derived; their persistence surface is `setopt` in the rc script, not the
    /// TOML). Skipping the load keeps a stale or hand-edited file from pinning
    /// any of them against the runtime. (An unknown key has no stored option;
    /// let it fall through to set_in_layer, which reports NOT_FOUND as before.)
    stored_option_t *known = find_option(full_key);
    if (known && !known->persisted) {
        return TOML_SUCCESS;
    }

    /// A config-file value lands in the USER layer: above mode presets and the
    /// schema default, below an interactive (SESSION) tweak.
    creg_result_t result =
        set_in_layer(full_key, &config_val, CREG_LAYER_USER, "lushrc.toml");
    /// A single bad key must not abort loading the rest of the file. Skip the
    /// expected per-key rejections -- an unknown key (NOT_FOUND), a wrong-kind
    /// value (TYPE_MISMATCH), or one that fails its type's constraint
    /// (INVALID_VALUE) -- and leave the prior layer's value in place. Record
    /// each in the report (if any) so the caller can warn; only a structural
    /// error (e.g. out of memory) propagates as a load failure.
    if (result == CREG_ERROR_NOT_FOUND || result == CREG_ERROR_TYPE_MISMATCH ||
        result == CREG_ERROR_INVALID_VALUE) {
        if (ctx->report) {
            if (ctx->report->skip_count < CREG_LOAD_SKIP_MAX) {
                creg_load_skip_t *s =
                    &ctx->report->skipped[ctx->report->skip_count];
                snprintf(s->key, sizeof(s->key), "%s", full_key);
                s->reason = result;
            }
            ctx->report->skip_count++;
        }
    } else if (result != CREG_SUCCESS) {
        ctx->result = result;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Error setting '%s'",
                 full_key);
    }

    return TOML_SUCCESS;
}

/* ============================================================================
 * Persistence
 * ============================================================================
 */

creg_result_t config_registry_load(const char *path) {
    return config_registry_load_reported(path, NULL);
}

creg_result_t config_registry_load_reported(const char *path,
                                            creg_load_report_t *report) {
    if (report) {
        report->skip_count = 0;
    }
    if (!path) {
        return CREG_ERROR_INVALID_PARAM;
    }

    if (!g_registry.initialized) {
        return CREG_ERROR_INVALID_PARAM;
    }

    /// Read file contents
    FILE *file = fopen(path, "r");
    if (!file) {
        return CREG_ERROR_IO_FAILED;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size <= 0) {
        fclose(file);
        return CREG_SUCCESS; /// Empty file is valid
    }

    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(file);
        return CREG_ERROR_OUT_OF_MEMORY;
    }

    size_t read = fread(content, 1, (size_t)size, file);
    content[read] = '\0';
    fclose(file);

    /// Parse TOML
    toml_parser_t parser;
    toml_result_t toml_result = toml_parser_init(&parser, content);
    if (toml_result != TOML_SUCCESS) {
        free(content);
        return CREG_ERROR_PARSE_FAILED;
    }

    load_context_t ctx = {.result = CREG_SUCCESS, .report = report};
    toml_result = toml_parser_parse(&parser, load_callback, &ctx);

    toml_parser_cleanup(&parser);
    free(content);

    if (toml_result != TOML_SUCCESS) {
        return CREG_ERROR_PARSE_FAILED;
    }

    /// Call on_load hooks for all sections
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

    /// Write each section
    for (size_t i = 0; i < g_registry.section_count; i++) {
        registered_section_t *reg = &g_registry.sections[i];
        bool section_written = false;

        /// Check if section has custom save hook
        if (reg->section.on_save) {
            fprintf(file, "[%s]\n", reg->section.name);
            reg->section.on_save(file);
            fprintf(file, "\n");
            continue;
        }

        /// Write non-default, persisted values
        for (size_t j = 0; j < reg->option_count; j++) {
            stored_option_t *opt = &reg->options[j];

            /// Skip non-persisted options
            if (!opt->persisted) {
                continue;
            }

            /// Persist the effective value, but only when it differs from the
            /// schema default (sparse file: a config save records the user's
            /// actual choices, not the whole schema).
            const creg_value_t *eff = resolve_effective(opt, NULL);
            if (creg_value_equal(eff, &opt->slots[CREG_LAYER_DEFAULT].value)) {
                continue;
            }

            /// Write section header if not yet written
            if (!section_written) {
                fprintf(file, "[%s]\n", reg->section.name);
                section_written = true;
            }

            /// Extract just the option name from full key
            const char *dot = strchr(opt->key, '.');
            const char *option_name = dot ? dot + 1 : opt->key;

            /// Write value based on type
            switch (eff->type) {
            case CREG_VALUE_STRING:
                fprintf(file, "%s = \"%s\"\n", option_name, eff->data.string);
                break;
            case CREG_VALUE_INTEGER:
                fprintf(file, "%s = %lld\n", option_name,
                        (long long)eff->data.integer);
                break;
            case CREG_VALUE_BOOLEAN:
                fprintf(file, "%s = %s\n", option_name,
                        eff->data.boolean ? "true" : "false");
                break;
            case CREG_VALUE_FLOAT:
                fprintf(file, "%s = %g\n", option_name, eff->data.floating);
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

/// Clear one layer of an option and re-resolve: if the effective value changes
/// (the cleared layer was winning), write through bindings and notify. Used by
/// reset to drop a layer so the value falls through to the one beneath it.
static void clear_layer(stored_option_t *opt, creg_layer_t layer) {
    if (layer == CREG_LAYER_DEFAULT || !opt->slots[layer].present) {
        return;
    }
    creg_value_t old_eff = *resolve_effective(opt, NULL);
    opt->slots[layer].present = false;
    creg_value_t new_eff = *resolve_effective(opt, NULL);
    if (!creg_value_equal(&old_eff, &new_eff)) {
        apply_bindings_for_key(opt->key, &new_eff);
        notify_change(opt->key, &old_eff, &new_eff);
    }
}

creg_result_t config_registry_reset(const char *key) {
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }
    /// Reset drops the interactive (SESSION) layer; the value falls through to
    /// the config file / mode preset / default beneath it.
    clear_layer(opt, CREG_LAYER_SESSION);
    return CREG_SUCCESS;
}

creg_result_t config_registry_set_type(const char *key,
                                       const creg_type_t *type) {
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }
    /// The check ops read a fixed union member (enum -> data.string, int_range
    /// -> data.integer), so the descriptor's storage must agree with the key's
    /// declared kind or a later set would reinterpret bytes. Fail loudly at
    /// attach time, mirroring the bind-an-unregistered-key loud failure.
    if (type && opt->option_def && opt->option_def->type != CREG_VALUE_NONE &&
        opt->option_def->type != type->storage) {
        return CREG_ERROR_TYPE_MISMATCH;
    }
    opt->type = type;
    return CREG_SUCCESS;
}

creg_result_t config_registry_describe_type(const char *key, char *out,
                                            size_t outlen) {
    if (!out || outlen == 0) {
        return CREG_ERROR_INVALID_PARAM;
    }
    out[0] = '\0';
    stored_option_t *opt = find_option(key);
    if (!opt || !opt->type || !opt->type->describe) {
        return CREG_ERROR_NOT_FOUND;
    }
    opt->type->describe(opt->type, out, outlen);
    return CREG_SUCCESS;
}

const char *config_registry_get_help(const char *key) {
    stored_option_t *opt = find_option(key);
    if (!opt || !opt->option_def) {
        return NULL;
    }
    return opt->option_def->help;
}

creg_result_t config_registry_set_tier(const char *key, creg_tier_t tier) {
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }
    opt->tier = tier;
    return CREG_SUCCESS;
}

creg_result_t config_registry_get_tier(const char *key, creg_tier_t *out) {
    if (!out) {
        return CREG_ERROR_INVALID_PARAM;
    }
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }
    *out = opt->tier;
    return CREG_SUCCESS;
}

size_t config_registry_collect_by_tier(creg_tier_t tier,
                                       char out[][CREG_KEY_MAX], size_t max) {
    size_t count = 0;
    for (size_t s = 0; s < g_registry.section_count; s++) {
        registered_section_t *reg = &g_registry.sections[s];
        for (size_t i = 0; i < reg->option_count; i++) {
            if (reg->options[i].tier != tier) {
                continue;
            }
            if (out && count < max) {
                snprintf(out[count], CREG_KEY_MAX, "%s", reg->options[i].key);
            }
            count++;
        }
    }
    return count;
}

/// Does the runtime cell @p b is bound to currently hold the value the registry
/// resolves as effective for that key? The reverse of apply_binding's forward
/// write: a mismatch is a phantom sync (the engine reads what the registry does
/// not hold).
static bool binding_cell_matches_effective(const binding_t *b,
                                           const creg_value_t *eff) {
    switch (b->kind) {
    case CREG_BIND_BOOL:
        return eff->type == CREG_VALUE_BOOLEAN &&
               *(const bool *)b->cell == eff->data.boolean;
    case CREG_BIND_INT:
        return eff->type == CREG_VALUE_INTEGER &&
               *(const int *)b->cell == (int)eff->data.integer;
    case CREG_BIND_STRING: {
        if (eff->type != CREG_VALUE_STRING || b->cell_size == 0) {
            return false;
        }
        /// apply_binding writes the cell with snprintf(cell, cell_size, ...),
        /// truncating to cell_size-1. Compare against the same truncation so a
        /// legitimately-truncated cell is not flagged -- the reverse must be
        /// the exact inverse of the forward write.
        char want[CREG_VALUE_STRING_MAX];
        snprintf(want,
                 b->cell_size > sizeof(want) ? sizeof(want) : b->cell_size,
                 "%s", eff->data.string);
        return strcmp((const char *)b->cell, want) == 0;
    }
    case CREG_BIND_STRING_PTR: {
        if (eff->type != CREG_VALUE_STRING) {
            return false;
        }
        const char *cell = *(const char *const *)b->cell;
        /// An empty effective string maps to a NULL cell (unset), per
        /// apply_binding.
        if (eff->data.string[0] == '\0') {
            return cell == NULL;
        }
        return cell != NULL && strcmp(cell, eff->data.string) == 0;
    }
    case CREG_BIND_ENUM_INT: {
        if (eff->type != CREG_VALUE_STRING || !b->pairs) {
            return false;
        }
        int64_t mapped = b->fallback;
        for (const creg_enum_pair_t *p = b->pairs; p->name; p++) {
            if (unicode_streq(p->name, eff->data.string)) {
                mapped = p->value;
                break;
            }
        }
        return *(const int *)b->cell == (int)mapped;
    }
    }
    return true;
}

size_t config_registry_validate_schema(creg_schema_violation_t *out,
                                       size_t max) {
    size_t count = 0;
#define CREG_ADD_VIOLATION(k, ...)                                             \
    do {                                                                       \
        if (out && count < max) {                                              \
            snprintf(out[count].key, sizeof(out[count].key), "%s", (k));       \
            snprintf(out[count].problem, sizeof(out[count].problem),           \
                     __VA_ARGS__);                                             \
        }                                                                      \
        count++;                                                               \
    } while (0)

    /// A: a typed key's own DEFAULT-layer value must satisfy its constraint.
    for (size_t s = 0; s < g_registry.section_count; s++) {
        registered_section_t *sec = &g_registry.sections[s];
        for (size_t o = 0; o < sec->option_count; o++) {
            stored_option_t *opt = &sec->options[o];
            if (!opt->type || !opt->type->check ||
                !opt->slots[CREG_LAYER_DEFAULT].present) {
                continue;
            }
            /// Guard the storage kind before the check op reads a fixed union
            /// member (as the forward funnel does): a wrong-kind default is
            /// itself a violation, and reporting it avoids the check op
            /// reinterpreting bytes.
            if (opt->slots[CREG_LAYER_DEFAULT].value.type !=
                opt->type->storage) {
                CREG_ADD_VIOLATION(opt->key,
                                   "default storage kind does not match its "
                                   "type");
                continue;
            }
            char reason[64] = {0};
            if (!opt->type->check(opt->type,
                                  &opt->slots[CREG_LAYER_DEFAULT].value, reason,
                                  sizeof(reason))) {
                CREG_ADD_VIOLATION(opt->key, "default fails its type (%s)",
                                   reason);
            }
        }
    }

    /// B: every per-mode default for a typed key must satisfy its constraint.
    for (size_t i = 0; i < g_registry.mode_default_count; i++) {
        const mode_default_t *md = &g_registry.mode_defaults[i];
        if (!md->active) {
            continue;
        }
        stored_option_t *opt = find_option(md->key);
        if (!opt) {
            CREG_ADD_VIOLATION(md->key, "mode default for an unregistered key");
            continue;
        }
        if (!opt->type || !opt->type->check) {
            continue;
        }
        if (md->value.type != opt->type->storage) {
            CREG_ADD_VIOLATION(md->key,
                               "mode default storage kind does not match its "
                               "type");
            continue;
        }
        char reason[64] = {0};
        if (!opt->type->check(opt->type, &md->value, reason, sizeof(reason))) {
            CREG_ADD_VIOLATION(md->key, "mode default fails its type (%s)",
                               reason);
        }
    }

    /// C: every bound runtime cell must equal its key's effective value.
    for (size_t i = 0; i < g_registry.binding_count; i++) {
        const binding_t *b = &g_registry.bindings[i];
        if (!b->active) {
            continue;
        }
        stored_option_t *opt = find_option(b->key);
        if (!opt) {
            CREG_ADD_VIOLATION(b->key, "binding to an unregistered key");
            continue;
        }
        const creg_value_t *eff = resolve_effective(opt, NULL);
        if (!binding_cell_matches_effective(b, eff)) {
            CREG_ADD_VIOLATION(b->key,
                               "bound cell out of sync with effective value");
        }
    }
#undef CREG_ADD_VIOLATION
    return count;
}

creg_result_t config_registry_reset_section(const char *section_name) {
    registered_section_t *section = find_section(section_name);
    if (!section) {
        return CREG_ERROR_NOT_FOUND;
    }

    for (size_t i = 0; i < section->option_count; i++) {
        clear_layer(&section->options[i], CREG_LAYER_SESSION);
    }

    return CREG_SUCCESS;
}

void config_registry_reset_all(void) {
    for (size_t i = 0; i < g_registry.section_count; i++) {
        config_registry_reset_section(g_registry.sections[i].section.name);
    }
}

/* ============================================================================
 * Mode-Aware Defaults
 * ============================================================================
 */

creg_result_t config_registry_set_mode_default(const char *key,
                                               shell_mode_t mode,
                                               const creg_value_t *value) {
    if (!key || !value || mode >= SHELL_MODE_COUNT) {
        return CREG_ERROR_INVALID_PARAM;
    }
    if (!g_registry.initialized) {
        return CREG_ERROR_NOT_FOUND;
    }
    if (strlen(key) >= CREG_KEY_MAX) {
        return CREG_ERROR_INVALID_PARAM;
    }

    /// Verify the option exists and that the type matches.
    creg_value_t probe;
    creg_result_t probe_result = config_registry_get_default(key, &probe);
    if (probe_result != CREG_SUCCESS) {
        return probe_result;
    }
    if (probe.type != value->type) {
        return CREG_ERROR_TYPE_MISMATCH;
    }

    /// Replace if a (key, mode) entry already exists.
    for (size_t i = 0; i < g_registry.mode_default_count; i++) {
        mode_default_t *md = &g_registry.mode_defaults[i];
        if (md->active && md->mode == mode && unicode_streq(md->key, key)) {
            md->value = *value;
            return CREG_SUCCESS;
        }
    }

    if (g_registry.mode_default_count >= CREG_MODE_DEFAULTS_MAX) {
        return CREG_ERROR_OPTION_FULL;
    }

    mode_default_t *slot =
        &g_registry.mode_defaults[g_registry.mode_default_count++];
    strncpy(slot->key, key, CREG_KEY_MAX - 1);
    slot->key[CREG_KEY_MAX - 1] = '\0';
    slot->mode = mode;
    slot->value = *value;
    slot->active = true;
    return CREG_SUCCESS;
}

creg_result_t config_registry_apply_mode_defaults(shell_mode_t mode) {
    if (mode >= SHELL_MODE_COUNT) {
        return CREG_ERROR_INVALID_PARAM;
    }
    if (!g_registry.initialized) {
        return CREG_SUCCESS; /// nothing to do
    }

    /// Drop the previous mode's overlay first: a mode switch replaces the MODE
    /// layer wholesale, so any key the previous mode set but this one does not
    /// falls back to the layer beneath. Keys with a SESSION/USER value above
    /// MODE are untouched -- that is the clobber fix: an interactive tweak sits
    /// above MODE and survives the switch.
    for (size_t s = 0; s < g_registry.section_count; s++) {
        registered_section_t *reg = &g_registry.sections[s];
        for (size_t o = 0; o < reg->option_count; o++) {
            clear_layer(&reg->options[o], CREG_LAYER_MODE);
        }
    }

    for (size_t i = 0; i < g_registry.mode_default_count; i++) {
        const mode_default_t *md = &g_registry.mode_defaults[i];
        if (!md->active || md->mode != mode) {
            continue;
        }
        /// Best-effort apply into the MODE layer; a single failure should not
        /// abort the whole walk. A curated mode default that violates its key's
        /// type is a registration-time programming error, caught by the planned
        /// CI schema invariant (default/mode-default correctness) rather than a
        /// runtime diagnostic this standalone library cannot report richly.
        (void)set_in_layer(md->key, &md->value, CREG_LAYER_MODE, "mode preset");
    }
    return CREG_SUCCESS;
}

creg_result_t config_registry_get_default(const char *key,
                                          creg_value_t *value) {
    if (!value) {
        return CREG_ERROR_INVALID_PARAM;
    }

    stored_option_t *opt = find_option(key);
    if (!opt) {
        return CREG_ERROR_NOT_FOUND;
    }

    *value = opt->slots[CREG_LAYER_DEFAULT].value;
    return CREG_SUCCESS;
}

bool config_registry_is_default(const char *key) {
    stored_option_t *opt = find_option(key);
    if (!opt) {
        return false;
    }

    /// "Is default" means the effective value resolves to the default layer's
    /// value -- no higher layer is overriding it.
    return creg_value_equal(resolve_effective(opt, NULL),
                            &opt->slots[CREG_LAYER_DEFAULT].value);
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
        /// Use Unicode-aware comparison for string values
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
