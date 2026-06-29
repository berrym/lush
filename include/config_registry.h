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
 * TYPE DESCRIPTORS (the type vtable)
 * ============================================================================
 *
 * A creg_value_type_t names a value's STORAGE (which union slot). A
 * creg_type_t is the richer notion of the option's TYPE: storage plus the
 * canonical validate / describe behavior for that type, in one place. Attaching
 * a descriptor to a registered key (config_registry_set_type) makes the
 * registry self-validating: every write -- config set, setopt, a TOML load --
 * is checked at the single set chokepoint, and an out-of-range int or an
 * unknown enum string is rejected with CREG_ERROR_INVALID_VALUE instead of
 * silently stored. The describe op feeds the rejection a precise, typed message
 * ("one of: prefix, substring, fuzzy") and is the same source a wizard reads.
 *
 * This replaces the scattered, mostly-unwired config_validate_* helpers: the
 * validator for a type lives with the type, not at each call site.
 */

/** @brief Generic enum mapping (registry string <-> engine int). */
typedef struct {
    const char *name; ///< Registry string value (e.g. "fuzzy")
    int64_t value;    ///< Engine enum value it maps to
} creg_enum_pair_t;

typedef struct creg_type creg_type_t;

/**
 * @brief A configuration type: storage kind + validate/describe behavior.
 *
 * Built-in singletons (bool/int/string) and the enum / int-range builders
 * below fill this; callers hold the descriptor as a file-scope object that
 * outlives the registry and attach it with config_registry_set_type.
 */
struct creg_type {
    const char *name;          ///< "bool", "int", "string", "enum", ...
    creg_value_type_t storage; ///< Which union slot a value of this type uses
    const creg_enum_pair_t *pairs; ///< enum: allowed names (NULL-terminated)
    int64_t min;                   ///< int_range: inclusive lower bound
    int64_t max;                   ///< int_range: inclusive upper bound
    /// Validate @p v against this type's constraints. On failure writes a short
    /// reason into @p err. Storage kind is checked by the registry separately;
    /// this is the semantic constraint (range, enum membership).
    bool (*check)(const creg_type_t *self, const creg_value_t *v, char *err,
                  size_t errlen);
    /// Describe the valid values into @p out (for typed errors and the wizard).
    void (*describe)(const creg_type_t *self, char *out, size_t outlen);
};

/** @brief Shared descriptor for an unconstrained boolean. */
const creg_type_t *creg_type_bool(void);
/** @brief Shared descriptor for an unconstrained integer. */
const creg_type_t *creg_type_int(void);
/** @brief Shared descriptor for an unconstrained string. */
const creg_type_t *creg_type_string(void);

/**
 * @brief Initialize a caller-owned enum descriptor over @p pairs.
 *
 * Storage is string; a value is valid iff it equals one of the pair names.
 * @p pairs must outlive @p out (typically both are file-scope statics).
 */
void creg_type_init_enum(creg_type_t *out, const creg_enum_pair_t *pairs);

/**
 * @brief Initialize a caller-owned integer descriptor constrained to [min,max].
 */
void creg_type_init_int_range(creg_type_t *out, int64_t min, int64_t max);

/* config_registry_set_type / describe_type are declared with the other
 * registry functions below, after creg_result_t is defined. */

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
 * @brief Discoverability tier for a configuration option.
 *
 * Tiers let a surface present a curated subset of the schema rather than the
 * whole store. The wizard walks CREG_TIER_BEGINNER; a future `config show` can
 * group by tier. A tier is attached to a key after registration via
 * config_registry_set_tier (the same pattern as the type vtable). Tier queries
 * (get_tier / collect_by_tier) match the attached value EXACTLY; a key not yet
 * curated is CREG_TIER_UNSET, so it is never collected as BEGINNER and never
 * surfaced to a beginner. Tiers are assigned incrementally; only the curated
 * beginner set is attached today.
 */
typedef enum {
    CREG_TIER_UNSET = 0, ///< Not yet curated; never BEGINNER
    CREG_TIER_BEGINNER,  ///< A first-run essential: safe, visible, high-value
    CREG_TIER_COMMON,    ///< Everyday setting, not first-run essential
    CREG_TIER_ADVANCED,  ///< Power-user setting
    CREG_TIER_EXPERT,    ///< Rarely changed; deep behavior or diagnostics
} creg_tier_t;

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
    CREG_ERROR_TYPE_MISMATCH,   ///< Value's union kind != the option's storage
    CREG_ERROR_INVALID_VALUE,   ///< Right kind, but fails the type's constraint
                                ///< (enum membership, int range, ...)
    CREG_ERROR_OUT_OF_MEMORY,   ///< Memory allocation failed
    CREG_ERROR_SECTION_FULL,    ///< Too many sections registered
    CREG_ERROR_OPTION_FULL,     ///< Too many options in section
    CREG_ERROR_SUBSCRIBER_FULL, ///< Too many subscribers
    CREG_ERROR_PARSE_FAILED,    ///< Failed to parse config file
    CREG_ERROR_IO_FAILED        ///< File I/O error
} creg_result_t;

/* ============================================================================
 * TYPE DESCRIPTOR ATTACHMENT (declared here, after creg_result_t)
 * ============================================================================
 */

/**
 * @brief Attach a type descriptor to a registered key.
 *
 * Subsequent writes to @p key (config_registry_set / setters / TOML load) are
 * validated against @p type and rejected with CREG_ERROR_INVALID_VALUE on a
 * constraint failure. @p type must outlive the registry.
 *
 * @return CREG_SUCCESS, or CREG_ERROR_NOT_FOUND if the key is unregistered.
 */
creg_result_t config_registry_set_type(const char *key,
                                       const creg_type_t *type);

/**
 * @brief Describe a key's valid values into @p out (for errors / the wizard).
 *
 * @return CREG_SUCCESS, or CREG_ERROR_NOT_FOUND if the key is unregistered or
 *         has no type descriptor attached.
 */
creg_result_t config_registry_describe_type(const char *key, char *out,
                                            size_t outlen);

/**
 * @brief The help text registered for @p key, or NULL if none / unregistered.
 *
 * Returns the descriptor's static help pointer (no copy); valid for the life of
 * the registry. The same text `config show` prints; the wizard prompts with it.
 */
const char *config_registry_get_help(const char *key);

/* ============================================================================
 * DISCOVERABILITY TIER ATTACHMENT
 * ============================================================================
 */

/**
 * @brief Attach a discoverability tier to a registered key.
 *
 * Mirrors config_registry_set_type: the tier is metadata a surface reads to
 * present a curated subset of the schema. A key with no tier attached reports
 * CREG_TIER_UNSET.
 *
 * @return CREG_SUCCESS, or CREG_ERROR_NOT_FOUND if the key is unregistered.
 */
creg_result_t config_registry_set_tier(const char *key, creg_tier_t tier);

/**
 * @brief Read a key's discoverability tier into @p out.
 *
 * @return CREG_SUCCESS (with @p out set, CREG_TIER_UNSET if none attached), or
 *         CREG_ERROR_NOT_FOUND if the key is unregistered.
 */
creg_result_t config_registry_get_tier(const char *key, creg_tier_t *out);

/**
 * @brief Collect the keys attached to @p tier into @p out.
 *
 * Walks the live option store in registration order and fills @p out (each row
 * a full dotted key) with every key whose tier equals @p tier. This is the
 * schema-generation core the wizard walks. Writes at most @p max rows but
 * returns the total match count, which may exceed @p max.
 */
size_t config_registry_collect_by_tier(creg_tier_t tier,
                                       char out[][CREG_KEY_MAX], size_t max);

/* ============================================================================
 * SCHEMA INVARIANT VALIDATION
 * ============================================================================
 */

/** @brief One schema-consistency violation found by the validator. */
typedef struct {
    char key[CREG_KEY_MAX]; ///< The offending key
    char problem[96];       ///< Human-readable description
} creg_schema_violation_t;

/**
 * @brief Validate the registry's internal consistency.
 *
 * Three invariants, each catching a class of silent corruption:
 *  - every typed key's DEFAULT-layer value satisfies its own type constraint
 *    (the default seed bypasses the set chokepoint, so an out-of-spec curated
 *    default would otherwise ship unvalidated);
 *  - every per-mode default for a typed key satisfies its constraint (a failing
 *    one is silently dropped when applied, so a mode would lose its default);
 *  - every bound runtime cell equals its key's effective value (a divergence is
 *    a phantom sync: the engine reads a value the registry does not hold).
 *
 * Intended for a CI/unit test (assert the count is zero) and a future
 * `config doctor`. Writes up to @p max violations into @p out and returns the
 * total count (0 = consistent), which may exceed @p max.
 */
size_t config_registry_validate_schema(creg_schema_violation_t *out,
                                       size_t max);

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

/**
 * @brief Number of live options registered in a section.
 *
 * Counts the live option store, which -- unlike the section definition returned
 * by config_registry_get_section -- includes options added at runtime via
 * config_registry_register_option (e.g. the shell.feature.* keys).
 */
size_t config_registry_section_option_count(const char *section_name);

/**
 * @brief Full dotted key of the @p index-th live option in a section.
 *
 * Companion to config_registry_section_option_count for iterating a section's
 * live options. Fills @p out with the full key (e.g. "shell.feature.extglob").
 */
creg_result_t config_registry_section_option_key(const char *section_name,
                                                 size_t index, char *out,
                                                 size_t out_size);

/**
 * @brief Register one option into an already-registered section at runtime.
 *
 * Mirrors register_section's per-option setup but adds a single option, which
 * is how option sets too large to hand-list as a static array -- e.g. the
 * shell.feature.* keys generated from the shell-mode feature table -- become
 * first-class registry keys instead of ad-hoc special-cases. @p option must
 * outlive the registry (its pointer is stored). Re-registering a present key is
 * a no-op success.
 */
creg_result_t config_registry_register_option(const char *section_name,
                                              const creg_option_t *option);

/**
 * @brief Write a key's MODE-layer value directly.
 *
 * For per-mode defaults that cannot go through the static per-mode-default
 * table (CREG_MODE_DEFAULTS_MAX), such as the full shell feature matrix seeded
 * on every mode change.
 */
creg_result_t config_registry_set_mode_value(const char *key,
                                             const creg_value_t *value);

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

/* creg_enum_pair_t is defined with the type descriptors near the top, since the
 * type system and the enum bindings share the same string<->int mapping. */

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
 * @brief Bind a string key to an owned `char *` runtime cell.
 *
 * For runtime fields that hold a heap-allocated string pointer rather than a
 * fixed buffer. The registry owns the pointed-to memory: on each change it
 * frees the previous string and stores a fresh copy. An empty registry value
 * maps to a NULL cell, so "unset" round-trips as the engine's own default.
 */
creg_result_t config_registry_bind_string_ptr(const char *key, char **cell);

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

/** @brief Max per-load skipped keys recorded in a creg_load_report_t. */
#define CREG_LOAD_SKIP_MAX 32

/** @brief One key the load dropped, and why (NOT_FOUND / TYPE_MISMATCH /
 * INVALID_VALUE). */
typedef struct {
    char key[CREG_KEY_MAX];
    creg_result_t reason;
} creg_load_skip_t;

/**
 * @brief Report of keys a load dropped, for surfacing to the user.
 *
 * A load skips a key the registry rejects -- an unknown key, a wrong-kind
 * value, or one that fails its type constraint -- so a single bad line does not
 * abort the file. Without a report these skips are silent, diverging from the
 * interactive `config set` path, which errors. (Keys skipped because they are
 * persisted=false are by design and are NOT recorded.)
 */
typedef struct {
    creg_load_skip_t skipped[CREG_LOAD_SKIP_MAX];
    size_t skip_count; ///< Total skips, may exceed CREG_LOAD_SKIP_MAX
} creg_load_report_t;

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
 * @brief Load configuration from a TOML file, recording dropped keys.
 *
 * Like config_registry_load, but if @p report is non-NULL it is filled with the
 * keys the load dropped (and why), so the caller can warn the user. @p report
 * is zeroed first. Pass NULL for the silent behavior of config_registry_load.
 */
creg_result_t config_registry_load_reported(const char *path,
                                            creg_load_report_t *report);

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
