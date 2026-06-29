/**
 * @file config_wizard.c
 * @brief Interactive, schema-generated configuration wizard.
 *
 * The wizard is a view over the CREG schema, not a hand-coded settings list. It
 * asks the registry for the keys tagged CREG_TIER_BEGINNER
 * (config_registry_collect_by_tier) and, for each, prompts using that key's own
 * help text, current value, and type-described valid values. Every answer is
 * applied to the SESSION layer through the same validated set path `config set`
 * uses, then optionally persisted. Adding a setting to the wizard is a one-line
 * tier attachment in config.c; this file never names individual keys.
 *
 * Prompts read through lle_readline_no_history (the shell's only transient,
 * history-suppressing line reader), which also marks the editor as being at the
 * debug prompt -- so TAB completion at a value prompt currently offers the
 * debugger vocabulary. Decoupling history suppression from the debug-prompt
 * completion switch is a separate LLE change; it does not affect the values a
 * user types, their validation, or persistence.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "config.h"
#include "config_registry.h"
#include "lle/lle_readline.h"
#include "shell_error.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/// The wizard walks one curated tier; the beginner set is far smaller than this
/// bound, which only caps the on-stack key buffer.
#define WIZARD_MAX_KEYS 64

/// The outcome of prompting for a single key.
typedef enum {
    WIZARD_STEP_SET,    ///< A new value was accepted and applied
    WIZARD_STEP_KEEP,   ///< Enter pressed: current value kept
    WIZARD_STEP_CANCEL, ///< Ctrl-C / EOF: stop the whole wizard
} wizard_step_t;

/// Render the effective value of @p key into @p out for display.
static void wizard_current_value(const char *key, char *out, size_t n) {
    creg_inspect_t ins;
    if (config_registry_inspect(key, &ins) != CREG_SUCCESS) {
        snprintf(out, n, "?");
        return;
    }
    switch (ins.effective.type) {
    case CREG_VALUE_BOOLEAN:
        snprintf(out, n, "%s", ins.effective.data.boolean ? "true" : "false");
        break;
    case CREG_VALUE_INTEGER:
        snprintf(out, n, "%lld", (long long)ins.effective.data.integer);
        break;
    case CREG_VALUE_FLOAT:
        snprintf(out, n, "%g", ins.effective.data.floating);
        break;
    case CREG_VALUE_STRING:
        snprintf(out, n, "%s",
                 ins.effective.data.string[0] ? ins.effective.data.string
                                              : "(unset)");
        break;
    default:
        snprintf(out, n, "?");
        break;
    }
}

/// Strip leading and trailing ASCII whitespace in place; returns a pointer into
/// @p s. Only bytes < 0x80 are stripped, so a UTF-8 sequence is never split.
static char *wizard_trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                       end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

/// Set @p key from the user's text @p input, parsed against the key's declared
/// type and validated by the registry (SESSION layer). Returns the registry
/// result so the caller can re-prompt on a rejected value.
static creg_result_t wizard_set(const char *key, const char *input) {
    /// shell.* keys are mode projections / the single-valued editor pair / the
    /// feature matrix: config_set_value routes them through the
    /// projection-aware path so a SESSION write never pins a raw value against
    /// the active mode. The beginner tier is curated to registry-native scalar
    /// keys, so this is defensive -- it keeps a future shell.* tier addition
    /// correct instead of silently diverging from `config set`.
    if (strncmp(key, "shell.", 6) == 0) {
        config_set_value(key, input);
        return CREG_SUCCESS;
    }
    creg_value_t probe;
    if (config_registry_get(key, &probe) != CREG_SUCCESS) {
        return CREG_ERROR_NOT_FOUND;
    }
    switch (probe.type) {
    case CREG_VALUE_BOOLEAN: {
        bool b;
        if (!config_parse_bool_text(input, &b)) {
            return CREG_ERROR_INVALID_VALUE;
        }
        return config_registry_set_boolean(key, b);
    }
    case CREG_VALUE_INTEGER: {
        char *end = NULL;
        errno = 0;
        long long parsed = strtoll(input, &end, 10);
        if (end == input || *end != '\0' || errno != 0) {
            return CREG_ERROR_INVALID_VALUE;
        }
        return config_registry_set_integer(key, (int64_t)parsed);
    }
    case CREG_VALUE_FLOAT: {
        char *end = NULL;
        errno = 0;
        double parsed = strtod(input, &end);
        if (end == input || *end != '\0' || errno != 0) {
            return CREG_ERROR_INVALID_VALUE;
        }
        creg_value_t v = {.type = CREG_VALUE_FLOAT, .data.floating = parsed};
        return config_registry_set(key, &v);
    }
    case CREG_VALUE_STRING:
        return config_registry_set_string(key, input);
    case CREG_VALUE_NONE:
    default:
        return CREG_ERROR_INVALID_VALUE;
    }
}

/// Print one key's preamble (help text and valid values) before prompting.
static void wizard_describe(const char *key, creg_value_type_t type) {
    const char *help = config_registry_get_help(key);
    printf("\n%s\n", key);
    if (help && help[0]) {
        printf("  %s\n", help);
    }
    char choices[160];
    if (config_registry_describe_type(key, choices, sizeof(choices)) ==
            CREG_SUCCESS &&
        choices[0]) {
        printf("  values: %s\n", choices);
    } else if (type == CREG_VALUE_BOOLEAN) {
        printf("  values: true or false\n");
    }
}

/// Prompt for a single key, re-prompting until a valid value is entered, the
/// current value is kept (Enter), or the wizard is cancelled (Ctrl-C / EOF).
static wizard_step_t wizard_prompt_key(const char *key) {
    creg_inspect_t ins;
    if (config_registry_inspect(key, &ins) != CREG_SUCCESS) {
        return WIZARD_STEP_KEEP; /// unreadable key: leave it untouched
    }
    creg_value_type_t type = ins.effective.type;
    wizard_describe(key, type);

    for (;;) {
        char current[CREG_VALUE_STRING_MAX];
        wizard_current_value(key, current, sizeof(current));
        char prompt[CREG_VALUE_STRING_MAX + 32];
        snprintf(prompt, sizeof(prompt), "  value [%s]: ", current);

        char *line = lle_readline_no_history(prompt);
        /// Ctrl-C returns an empty string (not NULL) with the interrupt flag
        /// set; Ctrl-D/EOF returns NULL. Both stop the wizard. An empty Enter
        /// (no interrupt) keeps the current value. free(NULL) is a no-op.
        if (!line || lle_readline_interrupted()) {
            free(line);
            return WIZARD_STEP_CANCEL;
        }
        char *input = wizard_trim(line);
        if (input[0] == '\0') {
            free(line);
            return WIZARD_STEP_KEEP;
        }

        creg_result_t rc = wizard_set(key, input);
        free(line);
        if (rc == CREG_SUCCESS) {
            char applied[CREG_VALUE_STRING_MAX];
            wizard_current_value(key, applied, sizeof(applied));
            printf("  set %s = %s\n", key, applied);
            return WIZARD_STEP_SET;
        }

        char choices[160];
        if (config_registry_describe_type(key, choices, sizeof(choices)) ==
                CREG_SUCCESS &&
            choices[0]) {
            printf("  not a valid value -- expected %s\n", choices);
        } else {
            printf("  not a valid value\n");
        }
    }
}

int config_wizard_run(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                         SOURCE_LOC_UNKNOWN,
                         "config wizard requires an interactive terminal");
        return -1;
    }

    char keys[WIZARD_MAX_KEYS][CREG_KEY_MAX];
    size_t total = config_registry_collect_by_tier(CREG_TIER_BEGINNER, keys,
                                                   WIZARD_MAX_KEYS);
    if (total == 0) {
        printf("No guided settings are available.\n");
        return 0;
    }
    size_t walk = total < WIZARD_MAX_KEYS ? total : WIZARD_MAX_KEYS;

    printf("Lush setup -- personalize a few settings.\n");
    printf("Press Enter to keep the current value; Ctrl-C to stop.\n");

    size_t changed = 0;
    bool cancelled = false;
    for (size_t i = 0; i < walk; i++) {
        wizard_step_t step = wizard_prompt_key(keys[i]);
        if (step == WIZARD_STEP_CANCEL) {
            cancelled = true;
            break;
        }
        if (step == WIZARD_STEP_SET) {
            changed++;
        }
    }

    printf("\n");
    if (changed == 0) {
        printf(cancelled ? "Setup stopped; no changes made.\n"
                         : "No changes made.\n");
        return 0;
    }

    /// Apply every accepted change to the running session at once, mirroring
    /// the canonical set path (registry write-through plus
    /// config_apply_settings).
    config_registry_sync_to_runtime();
    config_apply_settings();
    printf("%zu setting%s changed for this session.\n", changed,
           changed == 1 ? "" : "s");

    if (cancelled) {
        printf("Setup stopped before the end; changes apply to this session "
               "only (not saved).\n");
        return 0;
    }

    char *answer =
        lle_readline_no_history("Save these to your config file? [y/N]: ");
    bool save = answer && (answer[0] == 'y' || answer[0] == 'Y');
    free(answer);
    if (save) {
        if (config_save_user() == 0) {
            printf("Saved.\n");
        } else {
            shell_error_emit(SHELL_ERR_INVALID_ARGUMENT, SHELL_SEVERITY_WARNING,
                             SOURCE_LOC_UNKNOWN,
                             "could not save the configuration file");
        }
    } else {
        printf("Not saved -- changes apply to this session only.\n");
    }
    return 0;
}
