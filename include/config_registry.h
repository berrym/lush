/**
 * @file config_registry.h
 * @brief Unified Configuration Registry - Single Source of Truth
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * The config registry provides a centralized store for all shell configuration
 * with change notification support. It serves as the single source of truth
 * for configuration values, enabling:
 *
 * - Bidirectional sync between config files and runtime state
 * - Change notifications for reactive updates
 * - Type-safe value access
 * - Section-based organization
 *
 * Architecture:
 *
 *     config.toml ──────► TOML Parser ──────► Config Registry
 *                                                    │
 *                              ┌─────────────────────┼─────────────────────┐
 *                              ▼                     ▼                     ▼
 *                         shell_opts            shell_mode             display
 *                        (subscribers)         (subscribers) (subscribers)
 *
 * Note: Types use "creg_" prefix to avoid collision with config.h types.
 */

#ifndef CONFIG_REGISTRY_H
#define CONFIG_REGISTRY_H

#include "shell_mode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

/** @brief Maximum length of a config key (including section prefix) */
#define CREG_KEY_MAX 128

/** @brief Maximum length of a string config value */
#define CREG_VALUE_STRING_MAX 1024

/** @brief Maximum number of registered sections */
#define CREG_SECTION_MAX 16

/** @brief Maximum number of options per section */
#define CREG_OPTIONS_PER_SECTION_MAX 64

/** @brief Maximum number of change subscribers */
#define CREG_SUBSCRIBERS_MAX 32

/** @brief Maximum number of registered per-mode default overrides */
#define CREG_MODE_DEFAULTS_MAX 64

/** @brief Maximum number of registered runtime bindings */
#define CREG_BINDINGS_MAX 256

/* ============================================================================
 * VALUE TYPES
 * ============================================================================
 */

/**
 * @brief Configuration value types
 */
typedef enum creg_value_type {
    CREG_VALUE_NONE = 0, ///< No value / unset
    CREG_VALUE_STRING,   ///< String value
    CREG_VALUE_INTEGER,  ///< Integer value (int64_t)
    CREG_VALUE_BOOLEAN,  ///< Boolean value
    CREG_VALUE_FLOAT     ///< Floating point value (double)
} creg_value_type_t;

/**
 * @brief Configuration value storage
 */
typedef struct creg_value {
    creg_value_type_t type;
    union {
        char string[CREG_VALUE_STRING_MAX];
        int64_t integer;
        bool boolean;
        double floating;
    } data;
} creg_value_t;

/* ============================================================================
 * CONFIGURATION LAYERS (precedence + provenance)
 * ============================================================================
 */

/**
 * @brief Configuration value layers, lowest to highest precedence
 *
 * Each option holds an independent value per layer; the EFFECTIVE value is the
 * highest-precedence present layer. Writes route to a layer by their source
 * (schema default, mode preset, config file, interactive set), which makes
 * precedence explicit, provenance free ("where did this value come from"), and
 * structurally prevents a mode preset from clobbering an interactive tweak.
 */
typedef enum creg_layer {
    CREG_LAYER_DEFAULT = 0, ///< Schema default (always present)
    CREG_LAYER_MODE,        ///< Active shell-mode preset
    CREG_LAYER_SYSTEM,      ///< System lushrc.toml (reserved)
    CREG_LAYER_USER,        ///< User lushrc.toml
    CREG_LAYER_SESSION,     ///< Interactive config set / setopt
    CREG_LAYER_COUNT
} creg_layer_t;

/** @brief Per-layer view of one option (for inspection / provenance) */
typedef struct {
    bool present;       ///< Whether this layer holds a value
    creg_value_t value; ///< The layer's value (valid when present)
    char origin[64];    ///< Human origin, e.g. "lushrc.toml", "mode:bash"
} creg_layer_view_t;

/** @brief Full inspection of one option across all layers */
typedef struct {
    creg_value_t effective; ///< Resolved effective value
    creg_layer_t winning;   ///< Layer the effective came from
    creg_layer_view_t layers[CREG_LAYER_COUNT]; ///< Every layer's state
} creg_inspect_t;

/* ============================================================================
 * OPTION DEFINITION
 * ============================================================================
 */

/**
 * @brief Configuration option definition
 *
 * Defines a single configuration option with its name, type, default value,
 * and optional help text.
 */
typedef struct creg_option {
    const char *name;         ///< Option name (e.g., "errexit")
    creg_value_type_t type;   ///< Expected value type
    creg_value_t default_val; ///< Default value
    const char *help;         ///< Help text for this option
    bool persisted;           ///< Whether to save to config file
} creg_option_t;

/* ============================================================================
 * SECTION DEFINITION
 * ============================================================================
 */

/**
 * @brief Configuration section definition
 *
 * A section groups related configuration options and provides lifecycle
 * hooks for loading, saving, and syncing with runtime state.
 */
typedef struct creg_section {
    const char *name;             ///< Section name (e.g., "shell")
    const creg_option_t *options; ///< Array of options in this section
    size_t option_count;          ///< Number of options

    /// Lifecycle hooks (all optional)
    void (*on_load)(void);           ///< Called after section is loaded
    void (*on_save)(FILE *file);     ///< Called during save (for custom output)
    void (*sync_to_runtime)(void);   ///< Apply config values to runtime state
    void (*sync_from_runtime)(void); ///< Read runtime state into config values
} creg_section_t;

/* ============================================================================
 * CHANGE NOTIFICATION
 * ============================================================================
 */

/**
 * @brief Change notification callback type
 *
 * Called when a configuration value changes. Subscribers can use this to
 * react to configuration changes in real-time.
 *
 * @param key       Full key path (e.g., "shell.errexit")
 * @param old_value Previous value (may be NULL for new keys)
 * @param new_value New value
 * @param user_data User-provided context from subscription
 */
typedef void (*creg_change_callback_t)(const char *key,
                                       const creg_value_t *old_value,
                                       const creg_value_t *new_value,
                                       void *user_data);

/* ============================================================================
 * RESULT CODES
 * ============================================================================
 */

/**
 * @brief Config registry result codes
 */
typedef enum creg_result {
    CREG_SUCCESS = 0,           ///< Operation succeeded
    CREG_ERROR_INVALID_PARAM,   ///< Invalid parameter
    CREG_ERROR_NOT_FOUND,       ///< Key or section not found
    CREG_ERROR_TYPE_MISMATCH,   ///< Value type doesn't match expected
    CREG_ERROR_OUT_OF_MEMORY,   ///< Memory allocation failed
    CREG_ERROR_SECTION_FULL,    ///< Too many sections registered
    CREG_ERROR_OPTION_FULL,     ///< Too many options in section
    CREG_ERROR_SUBSCRIBER_FULL, ///< Too many subscribers
    CREG_ERROR_PARSE_FAILED,    ///< Failed to parse config file
    CREG_ERROR_IO_FAILED        ///< File I/O error
} creg_result_t;

/* ============================================================================
 * REGISTRY LIFECYCLE
 * ============================================================================
 */

/**
 * @brief Initialize the config registry
 *
 * Must be called before any other registry functions. Initializes internal
 * storage and sets up default sections.
 *
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_init(void);

/**
 * @brief Clean up the config registry
 *
 * Frees all allocated resources and resets the registry to uninitialized state.
 */
void config_registry_cleanup(void);

/**
 * @brief Check if registry is initialized
 *
 * @return true if initialized, false otherwise
 */
bool config_registry_is_initialized(void);

/* ============================================================================
 * SECTION REGISTRATION
 * ============================================================================
 */

/**
 * @brief Register a configuration section
 *
 * Registers a section with its options and lifecycle hooks. Section options
 * are initialized to their default values.
 *
 * @param section Section definition to register
 * @return CREG_SUCCESS on success, error code on failure
 */
creg_result_t config_registry_register_section(const creg_section_t *section);

/**
 * @brief Get a registered section by name
 *
 * @param name Section name to look up
 * @return Pointer to section, or NULL if not found
 */
const creg_section_t *config_registry_get_section(const char *name);

/* ============================================================================
 * VALUE ACCESS
 * ============================================================================
 */

/**
 * @brief Set a configuration value
 *
 * Sets a value in the registry. The key should be in "section.option" format.
 * If the value differs from the current value, change notifications are fired.
 *
 * @param key   Full key path (e.g., "shell.errexit")
 * @param value Value to set
 * @return CREG_SUCCESS on success, error code on failure
 */
creg_result_t config_registry_set(const char *key, const creg_value_t *value);

/**
 * @brief Get a configuration value
 *
 * Retrieves a value from the registry. If the key doesn't exist, returns
 * CREG_ERROR_NOT_FOUND.
 *
 * @param key   Full key path (e.g., "shell.errexit")
 * @param value Output value (caller provides storage)
 * @return CREG_SUCCESS on success, error code on failure
 */
creg_result_t config_registry_get(const char *key, creg_value_t *value);

/**
 * @brief Check if a key exists in the registry
 *
 * @param key Full key path
 * @return true if key exists, false otherwise
 */
bool config_registry_exists(const char *key);

/**
 * @brief Inspect an option across all layers (effective + provenance)
 *
 * Fills @p out with the effective value, the winning layer, and each layer's
 * present/value/origin -- the data behind `config explain` (the shadowed stack
 * that answers "what is this, and where did it come from").
 *
 * @param key Full key path
 * @param out Output inspection (caller provides storage)
 * @return CREG_SUCCESS, or CREG_ERROR_NOT_FOUND if the key is unregistered
 */
creg_result_t config_registry_inspect(const char *key, creg_inspect_t *out);

/**
 * @brief Human-readable name of a layer (e.g. "session", "mode", "default")
 */
const char *config_registry_layer_name(creg_layer_t layer);

/* ============================================================================
 * TYPED VALUE ACCESS (CONVENIENCE)
 * ============================================================================
 */

/**
 * @brief Set a string value
 */
creg_result_t config_registry_set_string(const char *key, const char *value);

/**
 * @brief Get a string value
 *
 * @param key     Full key path
 * @param out     Output buffer
 * @param out_len Buffer size
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_get_string(const char *key, char *out,
                                         size_t out_len);

/**
 * @brief Set an integer value
 */
creg_result_t config_registry_set_integer(const char *key, int64_t value);

/**
 * @brief Get an integer value
 */
creg_result_t config_registry_get_integer(const char *key, int64_t *out);

/**
 * @brief Set a boolean value
 */
creg_result_t config_registry_set_boolean(const char *key, bool value);

/**
 * @brief Get a boolean value
 */
creg_result_t config_registry_get_boolean(const char *key, bool *out);

/* ============================================================================
 * RUNTIME BINDINGS (the keystone)
 *
 * A binding ties a registered key to a runtime cell (the real lvalue the engine
 * reads, e.g. &config.history_size). Once bound, EVERY change to the key from
 * ANY surface (config builtin, TOML load, mode preset) is written through to
 * the cell automatically -- the registry is the single writer and the cell is a
 * passive, always-current cache. This REPLACES the hand-written
 * sync_to_runtime / sync_from_runtime hooks and the whole class of "phantom
 * sync" bugs they caused: binding an UNREGISTERED key fails loudly
 * (CREG_ERROR_NOT_FOUND) instead of silently no-opping. Hot paths keep reading
 * the plain cell; the registry writes it once per change, never per read.
 * ============================================================================
 */

/**
 * @brief Generic enum mapping for an enum-as-string binding
 *
 * Decoupled from the engine's enum typedefs so the registry layer needs no
 * knowledge of config.h. Terminated by a {NULL, 0} sentinel.
 */
typedef struct {
    const char *name; ///< Registry string value (e.g. "fuzzy")
    int64_t value;    ///< Engine enum value it maps to
} creg_enum_pair_t;

/**
 * @brief Bind a boolean key to a runtime bool cell.
 * @return CREG_SUCCESS, or CREG_ERROR_NOT_FOUND if the key is not registered.
 */
creg_result_t config_registry_bind_boolean(const char *key, bool *cell);

/**
 * @brief Bind an integer key to a runtime int cell.
 */
creg_result_t config_registry_bind_integer(const char *key, int *cell);

/**
 * @brief Bind a string key to a fixed-size runtime char buffer.
 */
creg_result_t config_registry_bind_string(const char *key, char *cell,
                                          size_t cell_size);

/**
 * @brief Bind an enum-as-string key to a runtime int (enum) cell.
 *
 * The registry stores the value as a string; on each change the matching
 * pair's int value is written to the cell (mapping done once, here, instead
 * of a strcmp ladder in a sync hook). Unmatched strings write @p fallback.
 *
 * @param key      Registered key
 * @param cell     Address of the engine enum (read as int)
 * @param pairs    NULL-name-terminated mapping table
 * @param fallback Value written when the string matches no pair
 */
creg_result_t config_registry_bind_enum(const char *key, int *cell,
                                        const creg_enum_pair_t *pairs,
                                        int64_t fallback);

/* ============================================================================
 * CHANGE NOTIFICATION
 * ============================================================================
 */

/**
 * @brief Subscribe to configuration changes
 *
 * Registers a callback to be notified when configuration values matching
 * the pattern change. Pattern can be:
 * - Exact key: "shell.errexit"
 * - Section wildcard: "shell.*"
 * - Global wildcard: "*"
 *
 * @param pattern  Key pattern to match
 * @param callback Function to call on changes
 * @param user_data User context passed to callback
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_subscribe(const char *pattern,
                                        creg_change_callback_t callback,
                                        void *user_data);

/**
 * @brief Unsubscribe from configuration changes
 *
 * @param callback Callback to unsubscribe
 * @return CREG_SUCCESS if found and removed
 */
creg_result_t config_registry_unsubscribe(creg_change_callback_t callback);

/* ============================================================================
 * PERSISTENCE
 * ============================================================================
 */

/**
 * @brief Load configuration from a TOML file
 *
 * Parses the file and populates the registry with values. Unknown sections
 * and keys are ignored. After loading, calls on_load hooks for all sections.
 *
 * @param path Path to TOML config file
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_load(const char *path);

/**
 * @brief Save configuration to a TOML file
 *
 * Writes all registered sections and their options to the file in TOML format.
 * Only persisted options with non-default values are written (sparse format).
 *
 * @param path Path to output file
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_save(const char *path);

/* ============================================================================
 * SYNC OPERATIONS
 * ============================================================================
 */

/**
 * @brief Sync all sections to runtime state
 *
 * Calls sync_to_runtime for all registered sections that have this hook.
 */
void config_registry_sync_to_runtime(void);

/**
 * @brief Sync all sections from runtime state
 *
 * Calls sync_from_runtime for all registered sections that have this hook.
 */
void config_registry_sync_from_runtime(void);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Reset a key to its default value
 *
 * @param key Full key path
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_reset(const char *key);

/**
 * @brief Reset all keys in a section to defaults
 *
 * @param section_name Section to reset
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_reset_section(const char *section_name);

/**
 * @brief Reset entire registry to defaults
 */
void config_registry_reset_all(void);

/* ============================================================================
 * MODE-AWARE DEFAULTS
 * ============================================================================
 *
 * Lush modes are identity presets that re-seed configuration on switch.
 * Most options are mode-invariant (a single default works across all
 * modes). Some options diverge per mode -- e.g. lush curates a default
 * that differs from the bash/zsh consensus. Such options register
 * per-mode defaults via config_registry_set_mode_default(), and
 * apply_mode_preset() applies them on every mode change.
 *
 * Options without registered per-mode defaults are unaffected by mode
 * changes; their single default_val from the option definition stands.
 */

/**
 * @brief Register a per-mode default for an option.
 *
 * Associates a default value with a specific shell mode. When
 * config_registry_apply_mode_defaults(mode) is called, every option
 * with a registered per-mode default for that mode has its current
 * value reset to that default.
 *
 * The option must already be registered (via its section). Type must
 * match the option's declared type.
 *
 * Multiple per-mode defaults can be registered for the same key (one
 * per mode). Calling this with a mode that already has a default for
 * the given key replaces the prior value.
 *
 * @param key   Full key path (e.g., "completion.chain_directories")
 * @param mode  Shell mode this default applies to
 * @param value The default value for this option in this mode
 * @return CREG_SUCCESS on success, error code otherwise
 */
creg_result_t config_registry_set_mode_default(const char *key,
                                               shell_mode_t mode,
                                               const creg_value_t *value);

/**
 * @brief Apply all registered per-mode defaults for the given mode.
 *
 * For every option that has a registered per-mode default for the
 * given mode, sets the option's current value to that default.
 * Options without per-mode defaults are untouched.
 *
 * Called from apply_mode_preset() so mode change re-seeds the
 * registry. Mid-session mode changes overwrite any user tweaks to
 * mode-aware options (re-seed-every-time semantic).
 *
 * @param mode Mode whose per-mode defaults should be applied
 * @return CREG_SUCCESS on success
 */
creg_result_t config_registry_apply_mode_defaults(shell_mode_t mode);

/**
 * @brief Get the default value for a key
 *
 * @param key   Full key path
 * @param value Output value
 * @return CREG_SUCCESS if key found
 */
creg_result_t config_registry_get_default(const char *key, creg_value_t *value);

/**
 * @brief Check if a key has its default value
 *
 * @param key Full key path
 * @return true if value equals default
 */
bool config_registry_is_default(const char *key);

/* ============================================================================
 * VALUE HELPERS
 * ============================================================================
 */

/**
 * @brief Create a string config value
 */
static inline creg_value_t creg_value_string(const char *str) {
    creg_value_t v = {.type = CREG_VALUE_STRING};
    if (str) {
        snprintf(v.data.string, sizeof(v.data.string), "%s", str);
    }
    return v;
}

/**
 * @brief Create an integer config value
 */
static inline creg_value_t creg_value_integer(int64_t i) {
    creg_value_t v = {.type = CREG_VALUE_INTEGER, .data.integer = i};
    return v;
}

/**
 * @brief Create a boolean config value
 */
static inline creg_value_t creg_value_boolean(bool b) {
    creg_value_t v = {.type = CREG_VALUE_BOOLEAN, .data.boolean = b};
    return v;
}

/**
 * @brief Compare two config values for equality
 *
 * @return true if values are equal (same type and same data)
 */
bool creg_value_equal(const creg_value_t *a, const creg_value_t *b);

#ifdef __cplusplus
}
#endif

#endif /// CONFIG_REGISTRY_H
