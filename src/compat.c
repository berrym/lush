/**
 * @file compat.c
 * @brief Shell compatibility database implementation
 *
 * Loads and manages the shell compatibility database from TOML files.
 * Provides APIs for querying behavioral differences between shells and
 * checking scripts for portability issues.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "compat.h"
#include "toml_parser.h"
#include "lle/unicode_compare.h"

#include <dirent.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* Cross-platform forward declarations */
int strncasecmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);

/* ============================================================================
 * Constants
 * ============================================================================ */

/** @brief Maximum number of compatibility entries */
#define COMPAT_MAX_ENTRIES 2048

/** @brief Maximum path length */
#define COMPAT_PATH_MAX 1024

/** @brief XDG default for user data */
#define XDG_DATA_HOME_DEFAULT ".local/share"

/** @brief Subdirectory under data dirs for compat files */
#define COMPAT_SUBDIR "lush/compat"

/** @brief System data directory (XDG_DATA_DIRS default) */
#define COMPAT_SYSTEM_DATA_DIR "/usr/share/lush/compat"

/** @brief Local/alternate system data directory */
#define COMPAT_LOCAL_SYSTEM_DIR "/usr/local/share/lush/compat"

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/**
 * @brief Internal entry storage with owned strings
 */
typedef struct {
    char *id;
    compat_category_t category;
    char *feature;
    char *description;
    char *behavior_posix;
    char *behavior_bash;
    char *behavior_zsh;
    char *behavior_lush;
    compat_severity_t severity;
    char *lint_message;
    char *lint_suggestion;
    char *lint_pattern;
    compat_fix_class_t fix_class;  /**< Per-shell fix classification */
    char *fix_replacement;
    regex_t *compiled_regex;
    bool regex_valid;
} internal_entry_t;

/**
 * @brief Maximum length for target shell name
 */
#define COMPAT_TARGET_MAX 32

/**
 * @brief Database state
 */
typedef struct {
    bool initialized;
    internal_entry_t entries[COMPAT_MAX_ENTRIES];
    size_t entry_count;
    bool strict_mode;
    char target_shell[COMPAT_TARGET_MAX];  /**< Target shell name (string) */
    char data_dir[COMPAT_PATH_MAX];
} compat_state_t;

static compat_state_t g_compat = {0};

/* ============================================================================
 * String Tables
 * ============================================================================ */

static const char *category_names[] = {
    [COMPAT_CATEGORY_BUILTIN] = "builtin",
    [COMPAT_CATEGORY_EXPANSION] = "expansion",
    [COMPAT_CATEGORY_QUOTING] = "quoting",
    [COMPAT_CATEGORY_SYNTAX] = "syntax",
};

static const char *severity_names[] = {
    [COMPAT_SEVERITY_INFO] = "info",
    [COMPAT_SEVERITY_WARNING] = "warning",
    [COMPAT_SEVERITY_ERROR] = "error",
};

static const char *fix_type_names[] = {
    [FIX_TYPE_NONE] = "none",
    [FIX_TYPE_SAFE] = "safe",
    [FIX_TYPE_UNSAFE] = "unsafe",
    [FIX_TYPE_MANUAL] = "manual",
};

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * @brief Duplicate a string safely
 */
static char *safe_strdup(const char *s) {
    if (!s) return NULL;
    return strdup(s);
}

/**
 * @brief Free an internal entry
 */
static void free_internal_entry(internal_entry_t *entry) {
    if (!entry) return;
    
    free(entry->id);
    free(entry->feature);
    free(entry->description);
    free(entry->behavior_posix);
    free(entry->behavior_bash);
    free(entry->behavior_zsh);
    free(entry->behavior_lush);
    free(entry->lint_message);
    free(entry->lint_suggestion);
    free(entry->lint_pattern);
    free(entry->fix_replacement);
    
    if (entry->compiled_regex && entry->regex_valid) {
        regfree(entry->compiled_regex);
        free(entry->compiled_regex);
    }
    
    memset(entry, 0, sizeof(*entry));
}

/**
 * @brief Convert internal entry to public entry
 */
static void internal_to_public(const internal_entry_t *internal,
                               compat_entry_t *public) {
    public->id = internal->id;
    public->category = internal->category;
    public->feature = internal->feature;
    public->description = internal->description;
    public->behavior.posix = internal->behavior_posix;
    public->behavior.bash = internal->behavior_bash;
    public->behavior.zsh = internal->behavior_zsh;
    public->behavior.lush = internal->behavior_lush;
    public->lint.severity = internal->severity;
    public->lint.message = internal->lint_message;
    public->lint.suggestion = internal->lint_suggestion;
    public->lint.pattern = internal->lint_pattern;
    public->lint.fix = internal->fix_class;
    public->lint.replacement = internal->fix_replacement;
}

/**
 * @brief Compile regex pattern for an entry
 */
static void compile_entry_regex(internal_entry_t *entry) {
    if (!entry->lint_pattern || entry->lint_pattern[0] == '\0') {
        entry->regex_valid = false;
        return;
    }
    
    entry->compiled_regex = malloc(sizeof(regex_t));
    if (!entry->compiled_regex) {
        entry->regex_valid = false;
        return;
    }
    
    int ret = regcomp(entry->compiled_regex, entry->lint_pattern,
                      REG_EXTENDED | REG_NOSUB);
    if (ret != 0) {
        free(entry->compiled_regex);
        entry->compiled_regex = NULL;
        entry->regex_valid = false;
    } else {
        entry->regex_valid = true;
    }
}

/* ============================================================================
 * TOML Parsing
 * ============================================================================ */

/**
 * @brief Context for TOML parsing
 */
typedef struct {
    internal_entry_t *current_entry;
    char current_id[256];
    bool in_behavior;
    bool in_lint;
    bool in_fix;
} parse_context_t;

/**
 * @brief Find or create entry by ID
 */
static internal_entry_t *find_or_create_entry(const char *id) {
    /* Search existing */
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        if (g_compat.entries[i].id && strcmp(g_compat.entries[i].id, id) == 0) {
            return &g_compat.entries[i];
        }
    }
    
    /* Create new */
    if (g_compat.entry_count >= COMPAT_MAX_ENTRIES) {
        return NULL;
    }
    
    internal_entry_t *entry = &g_compat.entries[g_compat.entry_count++];
    memset(entry, 0, sizeof(*entry));
    entry->id = safe_strdup(id);
    return entry;
}

/**
 * @brief TOML parser callback
 */
static toml_result_t compat_toml_callback(const char *section, const char *key,
                                          const toml_value_t *value,
                                          void *user_data) {
    parse_context_t *ctx = (parse_context_t *)user_data;
    
    if (!section || !key || !value) {
        return TOML_SUCCESS;
    }
    
    /* Parse section path: entry_id or entry_id.behavior or entry_id.lint or entry_id.fix */
    char entry_id[256];
    const char *dot = strchr(section, '.');
    
    if (dot) {
        size_t id_len = (size_t)(dot - section);
        if (id_len >= sizeof(entry_id)) {
            id_len = sizeof(entry_id) - 1;
        }
        memcpy(entry_id, section, id_len);
        entry_id[id_len] = '\0';
        
        const char *subsection = dot + 1;
        ctx->in_behavior = (strcmp(subsection, "behavior") == 0);
        ctx->in_lint = (strcmp(subsection, "lint") == 0);
        ctx->in_fix = (strcmp(subsection, "fix") == 0);
    } else {
        snprintf(entry_id, sizeof(entry_id), "%s", section);
        ctx->in_behavior = false;
        ctx->in_lint = false;
        ctx->in_fix = false;
    }
    
    /* Find or create entry */
    internal_entry_t *entry = find_or_create_entry(entry_id);
    if (!entry) {
        return TOML_SUCCESS; /* Max entries reached */
    }
    
    const char *str_val = toml_value_get_string(value);
    
    if (ctx->in_behavior) {
        /* Behavior subsection */
        if (strcmp(key, "posix") == 0 && str_val) {
            free(entry->behavior_posix);
            entry->behavior_posix = safe_strdup(str_val);
        } else if (strcmp(key, "bash") == 0 && str_val) {
            free(entry->behavior_bash);
            entry->behavior_bash = safe_strdup(str_val);
        } else if (strcmp(key, "zsh") == 0 && str_val) {
            free(entry->behavior_zsh);
            entry->behavior_zsh = safe_strdup(str_val);
        } else if (strcmp(key, "lush") == 0 && str_val) {
            free(entry->behavior_lush);
            entry->behavior_lush = safe_strdup(str_val);
        }
    } else if (ctx->in_lint) {
        /* Lint subsection */
        if (strcmp(key, "severity") == 0 && str_val) {
            compat_severity_t sev;
            if (compat_severity_parse(str_val, &sev)) {
                entry->severity = sev;
            }
        } else if (strcmp(key, "message") == 0 && str_val) {
            free(entry->lint_message);
            entry->lint_message = safe_strdup(str_val);
        } else if (strcmp(key, "suggestion") == 0 && str_val) {
            free(entry->lint_suggestion);
            entry->lint_suggestion = safe_strdup(str_val);
        } else if (strcmp(key, "pattern") == 0 && str_val) {
            free(entry->lint_pattern);
            entry->lint_pattern = safe_strdup(str_val);
        }
    } else if (ctx->in_fix) {
        /* Fix subsection - per-shell classification */
        if (strcmp(key, "posix") == 0 && str_val) {
            fix_type_t ftype;
            if (compat_fix_type_parse(str_val, &ftype)) {
                entry->fix_class.posix = ftype;
            }
        } else if (strcmp(key, "bash") == 0 && str_val) {
            fix_type_t ftype;
            if (compat_fix_type_parse(str_val, &ftype)) {
                entry->fix_class.bash = ftype;
            }
        } else if (strcmp(key, "zsh") == 0 && str_val) {
            fix_type_t ftype;
            if (compat_fix_type_parse(str_val, &ftype)) {
                entry->fix_class.zsh = ftype;
            }
        } else if (strcmp(key, "lush") == 0 && str_val) {
            fix_type_t ftype;
            if (compat_fix_type_parse(str_val, &ftype)) {
                entry->fix_class.lush = ftype;
            }
        } else if (strcmp(key, "replacement") == 0 && str_val) {
            free(entry->fix_replacement);
            entry->fix_replacement = safe_strdup(str_val);
        }
    } else {
        /* Main entry section */
        if (strcmp(key, "category") == 0 && str_val) {
            compat_category_t cat;
            if (compat_category_parse(str_val, &cat)) {
                entry->category = cat;
            }
        } else if (strcmp(key, "feature") == 0 && str_val) {
            free(entry->feature);
            entry->feature = safe_strdup(str_val);
        } else if (strcmp(key, "description") == 0 && str_val) {
            free(entry->description);
            entry->description = safe_strdup(str_val);
        }
    }
    
    return TOML_SUCCESS;
}

/**
 * @brief Load a single TOML file
 */
static int load_toml_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    
    /* Read entire file */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size <= 0 || size > 1024 * 1024) { /* Max 1MB */
        fclose(fp);
        return -1;
    }
    
    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(fp);
        return -1;
    }
    
    size_t read = fread(content, 1, (size_t)size, fp);
    fclose(fp);
    
    if (read != (size_t)size) {
        free(content);
        return -1;
    }
    content[size] = '\0';
    
    /* Parse TOML */
    toml_parser_t parser;
    toml_result_t result = toml_parser_init(&parser, content);
    if (result != TOML_SUCCESS) {
        free(content);
        return -1;
    }
    
    parse_context_t ctx = {0};
    result = toml_parser_parse(&parser, compat_toml_callback, &ctx);
    
    toml_parser_cleanup(&parser);
    free(content);
    
    return (result == TOML_SUCCESS) ? 0 : -1;
}

/**
 * @brief Load all TOML files from a directory recursively
 */
static int load_directory(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }
    
    int files_loaded = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        char path[COMPAT_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            /* Recurse into subdirectory */
            files_loaded += load_directory(path);
        } else if (S_ISREG(st.st_mode)) {
            /* Check for .toml extension */
            size_t len = strlen(entry->d_name);
            if (len > 5 && strcmp(entry->d_name + len - 5, ".toml") == 0) {
                if (load_toml_file(path) == 0) {
                    files_loaded++;
                }
            }
        }
    }
    
    closedir(dir);
    return files_loaded;
}

/* ============================================================================
 * Public API - Database Management
 * ============================================================================ */

/**
 * @brief Try to load compat data from a directory
 * @return Number of files loaded, or 0 if directory doesn't exist
 */
static int try_load_from_dir(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        int loaded = load_directory(dir);
        if (loaded > 0) {
            snprintf(g_compat.data_dir, sizeof(g_compat.data_dir), "%s", dir);
            return loaded;
        }
    }
    return 0;
}

/**
 * @brief Get path to executable's directory
 * @param buf Buffer to store path
 * @param size Buffer size
 * @return true if successful
 */
static bool get_exe_dir(char *buf, size_t size) {
    char exe_path[COMPAT_PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) {
        return false;
    }
    exe_path[len] = '\0';
    
    /* Find last slash and truncate to get directory */
    char *last_slash = strrchr(exe_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        snprintf(buf, size, "%s", exe_path);
        return true;
    }
    return false;
}

int compat_init(const char *data_dir) {
    if (g_compat.initialized) {
        return 0;
    }
    
    /* Preserve target if it was set before init */
    char saved_target[COMPAT_TARGET_MAX] = {0};
    if (g_compat.target_shell[0] != '\0') {
        snprintf(saved_target, sizeof(saved_target), "%s", g_compat.target_shell);
    }
    
    memset(&g_compat, 0, sizeof(g_compat));
    
    /* Restore target or use default */
    if (saved_target[0] != '\0') {
        snprintf(g_compat.target_shell, sizeof(g_compat.target_shell), "%s", saved_target);
    } else {
        snprintf(g_compat.target_shell, sizeof(g_compat.target_shell), "%s", "posix");
    }
    
    int total_loaded = 0;
    char path_buf[COMPAT_PATH_MAX];
    
    /* If explicit path provided, use only that */
    if (data_dir && data_dir[0]) {
        total_loaded = try_load_from_dir(data_dir);
        goto finalize;
    }
    
    /*
     * XDG-compliant search order:
     * 1. $XDG_DATA_HOME/lush/compat (user customizations)
     * 2. $XDG_DATA_DIRS/lush/compat (each dir in colon-separated list)
     * 3. /usr/local/share/lush/compat (local installations)
     * 4. /usr/share/lush/compat (system packages)
     * 5. Relative to executable (development builds)
     * 6. ./data/compat (CWD fallback for development)
     */
    
    /* 1. XDG_DATA_HOME (user data) */
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home && xdg_data_home[0]) {
        snprintf(path_buf, sizeof(path_buf), "%s/%s", xdg_data_home, COMPAT_SUBDIR);
        total_loaded += try_load_from_dir(path_buf);
    } else {
        /* Default: ~/.local/share/lush/compat */
        const char *home = getenv("HOME");
        if (home && home[0]) {
            snprintf(path_buf, sizeof(path_buf), "%s/%s/%s", 
                     home, XDG_DATA_HOME_DEFAULT, COMPAT_SUBDIR);
            total_loaded += try_load_from_dir(path_buf);
        }
    }
    
    /* 2. XDG_DATA_DIRS (system data directories) */
    const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (xdg_data_dirs && xdg_data_dirs[0]) {
        char *dirs_copy = strdup(xdg_data_dirs);
        if (dirs_copy) {
            char *saveptr;
            char *dir = strtok_r(dirs_copy, ":", &saveptr);
            while (dir) {
                snprintf(path_buf, sizeof(path_buf), "%s/%s", dir, COMPAT_SUBDIR);
                total_loaded += try_load_from_dir(path_buf);
                dir = strtok_r(NULL, ":", &saveptr);
            }
            free(dirs_copy);
        }
    }
    
    /* 3. /usr/local/share/lush/compat */
    total_loaded += try_load_from_dir(COMPAT_LOCAL_SYSTEM_DIR);
    
    /* 4. /usr/share/lush/compat */
    total_loaded += try_load_from_dir(COMPAT_SYSTEM_DATA_DIR);
    
    /* 5. Relative to executable: ../share/lush/compat or ../data/compat */
    if (get_exe_dir(path_buf, sizeof(path_buf))) {
        char exe_relative[COMPAT_PATH_MAX];
        
        int ret;

        /* Try ../share/lush/compat (installed layout) */
        ret = snprintf(exe_relative, sizeof(exe_relative), "%s/../share/%s",
                       path_buf, COMPAT_SUBDIR);
        if (ret > 0 && (size_t)ret < sizeof(exe_relative))
            total_loaded += try_load_from_dir(exe_relative);

        /* Try ../data/compat (development layout) */
        ret = snprintf(exe_relative, sizeof(exe_relative), "%s/../data/compat",
                       path_buf);
        if (ret > 0 && (size_t)ret < sizeof(exe_relative))
            total_loaded += try_load_from_dir(exe_relative);
    }
    
    /* 6. CWD fallback: ./data/compat */
    total_loaded += try_load_from_dir("./data/compat");

finalize:
    /* Compile regex patterns for all loaded entries */
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        compile_entry_regex(&g_compat.entries[i]);
    }
    
    g_compat.initialized = true;
    
    /* Always succeed on init, even if no data files are loaded.
     * The module can operate with defaults when no compat data exists. */
    (void)total_loaded;
    return 0;
}

void compat_cleanup(void) {
    if (!g_compat.initialized) {
        return;
    }
    
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        free_internal_entry(&g_compat.entries[i]);
    }
    
    memset(&g_compat, 0, sizeof(g_compat));
}

int compat_reload(void) {
    char data_dir[COMPAT_PATH_MAX];
    snprintf(data_dir, sizeof(data_dir), "%s", g_compat.data_dir);
    
    compat_cleanup();
    return compat_init(data_dir);
}

/* ============================================================================
 * Public API - Entry Queries
 * ============================================================================ */

const compat_entry_t *compat_get_entry(const char *id) {
    if (!g_compat.initialized || !id) {
        return NULL;
    }
    
    static compat_entry_t result;
    
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        if (g_compat.entries[i].id &&
            strcmp(g_compat.entries[i].id, id) == 0) {
            internal_to_public(&g_compat.entries[i], &result);
            return &result;
        }
    }
    
    return NULL;
}

size_t compat_get_by_category(compat_category_t category,
                              const compat_entry_t **entries,
                              size_t max_entries) {
    if (!g_compat.initialized || !entries || max_entries == 0) {
        return 0;
    }
    
    static compat_entry_t results[COMPAT_MAX_ENTRIES];
    size_t count = 0;
    
    for (size_t i = 0; i < g_compat.entry_count && count < max_entries; i++) {
        if (g_compat.entries[i].category == category) {
            internal_to_public(&g_compat.entries[i], &results[count]);
            entries[count] = &results[count];
            count++;
        }
    }
    
    return count;
}

size_t compat_get_by_feature(const char *feature,
                             const compat_entry_t **entries,
                             size_t max_entries) {
    if (!g_compat.initialized || !feature || !entries || max_entries == 0) {
        return 0;
    }
    
    static compat_entry_t results[COMPAT_MAX_ENTRIES];
    size_t count = 0;
    
    for (size_t i = 0; i < g_compat.entry_count && count < max_entries; i++) {
        if (g_compat.entries[i].feature &&
            strcmp(g_compat.entries[i].feature, feature) == 0) {
            internal_to_public(&g_compat.entries[i], &results[count]);
            entries[count] = &results[count];
            count++;
        }
    }
    
    return count;
}

/**
 * @brief Get first entry matching a feature (for single lookups)
 *
 * Returns a pointer to a static entry that is valid until the next call.
 * Use this for single lookups to avoid the static array reuse issue in
 * compat_get_by_feature.
 */
const compat_entry_t *compat_get_first_by_feature(const char *feature) {
    if (!g_compat.initialized || !feature) {
        return NULL;
    }
    
    static compat_entry_t result;
    
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        if (g_compat.entries[i].feature &&
            lle_unicode_strings_equal(g_compat.entries[i].feature, feature,
                                      &LLE_UNICODE_COMPARE_DEFAULT)) {
            internal_to_public(&g_compat.entries[i], &result);
            return &result;
        }
    }
    
    return NULL;
}

size_t compat_get_entry_count(void) {
    return g_compat.entry_count;
}

void compat_foreach_entry(void (*callback)(const compat_entry_t *entry,
                                           void *user_data),
                          void *user_data) {
    if (!g_compat.initialized || !callback) {
        return;
    }
    
    compat_entry_t public_entry;
    
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        internal_to_public(&g_compat.entries[i], &public_entry);
        callback(&public_entry, user_data);
    }
}

/* ============================================================================
 * Public API - Portability Checking
 * ============================================================================ */

bool compat_is_portable(const char *construct, shell_mode_t target,
                        compat_result_t *result) {
    if (!g_compat.initialized || !construct) {
        return true;
    }
    
    /* Check against all entries with patterns */
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        internal_entry_t *entry = &g_compat.entries[i];
        
        if (!entry->regex_valid || !entry->compiled_regex) {
            continue;
        }
        
        if (regexec(entry->compiled_regex, construct, 0, NULL, 0) == 0) {
            /* Pattern matched - check if it's an issue for target */
            const char *target_behavior = NULL;
            switch (target) {
            case SHELL_MODE_POSIX:
                target_behavior = entry->behavior_posix;
                break;
            case SHELL_MODE_BASH:
                target_behavior = entry->behavior_bash;
                break;
            case SHELL_MODE_ZSH:
                target_behavior = entry->behavior_zsh;
                break;
            case SHELL_MODE_LUSH:
                target_behavior = entry->behavior_lush;
                break;
            default:
                break;
            }
            
            /* If behavior differs from lush default, it's a portability issue */
            if (target_behavior && entry->behavior_lush &&
                strcmp(target_behavior, entry->behavior_lush) != 0) {
                if (result) {
                    result->is_portable = false;
                    static compat_entry_t public_entry;
                    internal_to_public(entry, &public_entry);
                    result->entry = &public_entry;
                    result->target = target;
                    result->line = 0;
                    result->column = 0;
                }
                return false;
            }
        }
    }
    
    if (result) {
        result->is_portable = true;
        result->entry = NULL;
        result->target = target;
    }
    return true;
}

/**
 * @brief Check if a behavior string indicates the feature is unavailable
 *
 * Returns true if the behavior indicates the feature doesn't work in that shell.
 */
static bool behavior_indicates_unavailable(const char *behavior) {
    if (!behavior) {
        return false;  /* No info means we can't say it's unavailable */
    }
    
    /* Common patterns indicating feature is not available */
    if (strncasecmp(behavior, "Not in POSIX", 12) == 0 ||
        strncasecmp(behavior, "Not specified", 13) == 0 ||
        strncasecmp(behavior, "Not available", 13) == 0 ||
        strncasecmp(behavior, "Not supported", 13) == 0 ||
        strncasecmp(behavior, "Not applicable", 14) == 0 ||
        strncasecmp(behavior, "Not directly", 12) == 0 ||
        strncasecmp(behavior, "No built-in", 11) == 0 ||
        strncasecmp(behavior, "Use ", 4) == 0) {  /* "Use X instead" */
        return true;
    }
    
    return false;
}

/**
 * @brief Get behavior string for a target shell name
 *
 * Maps target shell name to the corresponding behavior field.
 */
static const char *get_behavior_for_target(const internal_entry_t *entry,
                                            const char *target) {
    if (!target || strcasecmp(target, "posix") == 0 || strcasecmp(target, "sh") == 0) {
        return entry->behavior_posix;
    } else if (strcasecmp(target, "bash") == 0) {
        return entry->behavior_bash;
    } else if (strcasecmp(target, "zsh") == 0) {
        return entry->behavior_zsh;
    } else if (strcasecmp(target, "lush") == 0) {
        return entry->behavior_lush;
    }
    /* Unknown target, default to POSIX (most conservative) */
    return entry->behavior_posix;
}

/**
 * @brief Check if a feature is problematic for the target shell
 *
 * Returns true if the entry describes a portability issue for the target.
 * If the feature works in the target shell, it's not a problem.
 */
static bool is_issue_for_target(const internal_entry_t *entry,
                                 const char *target) {
    const char *target_behavior = get_behavior_for_target(entry, target);
    
    /* If the feature is unavailable/problematic in target, it's an issue */
    return behavior_indicates_unavailable(target_behavior);
}

size_t compat_check_line(const char *line, shell_mode_t target,
                         compat_result_t *results, size_t max_results) {
    if (!g_compat.initialized || !line || !results || max_results == 0) {
        return 0;
    }
    
    /* Convert enum to string for internal comparison */
    const char *target_name = shell_mode_name(target);
    
    size_t found = 0;
    
    for (size_t i = 0; i < g_compat.entry_count && found < max_results; i++) {
        internal_entry_t *entry = &g_compat.entries[i];
        
        if (!entry->regex_valid || !entry->compiled_regex) {
            continue;
        }
        
        /* Skip if this isn't an issue for the target shell */
        if (!is_issue_for_target(entry, target_name)) {
            continue;
        }
        
        if (regexec(entry->compiled_regex, line, 0, NULL, 0) == 0) {
            static compat_entry_t public_entries[COMPAT_MAX_ENTRIES];
            internal_to_public(entry, &public_entries[found]);
            
            results[found].is_portable = false;
            results[found].entry = &public_entries[found];
            results[found].target = target;
            results[found].line = 0;
            results[found].column = 0;
            found++;
        }
    }
    
    return found;
}

size_t compat_check_script(const char *script, shell_mode_t target,
                           compat_result_t *results, size_t max_results) {
    if (!g_compat.initialized || !script || !results || max_results == 0) {
        return 0;
    }
    
    size_t found = 0;
    int line_num = 1;
    
    const char *line_start = script;
    const char *line_end;
    
    while (*line_start && found < max_results) {
        line_end = strchr(line_start, '\n');
        if (!line_end) {
            line_end = line_start + strlen(line_start);
        }
        
        /* Extract line */
        size_t line_len = (size_t)(line_end - line_start);
        char *line = malloc(line_len + 1);
        if (!line) {
            break;
        }
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        
        /* Check this line */
        size_t remaining = max_results - found;
        size_t line_found = compat_check_line(line, target,
                                               &results[found], remaining);
        
        /* Set line numbers */
        for (size_t i = 0; i < line_found; i++) {
            results[found + i].line = line_num;
        }
        
        found += line_found;
        free(line);
        
        line_num++;
        line_start = (*line_end) ? line_end + 1 : line_end;
    }
    
    return found;
}

/* ============================================================================
 * Public API - AST-Based Checking
 * ============================================================================ */

#include "node.h"

/**
 * @brief Map of node types to compatibility feature names
 *
 * This mapping connects AST node types to their corresponding features
 * in the TOML compatibility database for accurate portability detection.
 */
static const struct {
    node_type_t type;
    const char *feature;
    const char *description;
} ast_feature_map[] = {
    /* Extended test and arithmetic */
    { NODE_EXTENDED_TEST,      "extended_test",        "[[ ]] extended test" },
    { NODE_ARITH_CMD,          "arithmetic_command",   "(( )) arithmetic command" },
    { NODE_FOR_ARITH,          "arithmetic_for",       "C-style for loop" },

    /* Process substitution */
    { NODE_PROC_SUB_IN,        "process_substitution", "<() process substitution" },
    { NODE_PROC_SUB_OUT,       "process_substitution", ">() process substitution" },

    /* Arrays */
    { NODE_ARRAY_LITERAL,      "arrays",               "array literal" },
    { NODE_ARRAY_ACCESS,       "arrays",               "array access" },
    { NODE_ARRAY_ASSIGN,       "arrays",               "array assignment" },
    { NODE_ARRAY_APPEND,       "arrays",               "array append" },

    /* Redirections (non-POSIX) */
    { NODE_REDIR_HERESTRING,   "here_string",          "<<< here-string" },
    { NODE_REDIR_BOTH,         "redirect_both",        "&> redirect both streams" },
    { NODE_REDIR_BOTH_APPEND,  "redirect_append_both", "&>> append both streams" },
    { NODE_REDIR_FD_ALLOC,     "redirect_fd",          "{var}> fd allocation" },

    /* Control flow extensions */
    { NODE_COPROC,             "coproc",               "coproc coprocess" },
    { NODE_SELECT,             "select_loop",          "select loop" },
    { NODE_TIME,               "time_keyword",         "time keyword" },

    /* Functions */
    { NODE_ANON_FUNCTION,      "anonymous_function",   "anonymous function" },
};

#define AST_FEATURE_MAP_SIZE (sizeof(ast_feature_map) / sizeof(ast_feature_map[0]))

/**
 * @brief Check if a shell supports a feature by querying the TOML database
 *
 * Looks up the feature in the compatibility database and checks the behavior
 * description for the target shell. A feature is considered unsupported if
 * the behavior string starts with "Not" or "No " (e.g., "Not in POSIX").
 *
 * Note: Behavior strings are ASCII-only (hardcoded English), so simple
 * strncasecmp is appropriate here rather than Unicode comparison.
 */
static bool shell_supports_feature(shell_mode_t shell, const char *feature) {
    if (!feature) {
        return true;  /* Unknown feature, assume supported */
    }

    /* Look up feature in the TOML database */
    const compat_entry_t *entry = compat_get_first_by_feature(feature);
    if (!entry) {
        /* No TOML entry - fall back to basic heuristics */
        /* POSIX generally doesn't support the extended features we detect */
        return (shell != SHELL_MODE_POSIX);
    }

    /* Get the behavior string for the target shell */
    const char *behavior = NULL;
    switch (shell) {
        case SHELL_MODE_POSIX:
            behavior = entry->behavior.posix;
            break;
        case SHELL_MODE_BASH:
            behavior = entry->behavior.bash;
            break;
        case SHELL_MODE_ZSH:
            behavior = entry->behavior.zsh;
            break;
        case SHELL_MODE_LUSH:
            behavior = entry->behavior.lush;
            break;
        default:
            return true;  /* Unknown shell mode, assume supported */
    }

    if (!behavior) {
        return true;  /* No behavior defined, assume supported */
    }

    /* Use unified unavailability check */
    return !behavior_indicates_unavailable(behavior);
}

/**
 * @brief Recursive AST walker for compatibility checking
 */
static size_t compat_check_ast_node(node_t *node, shell_mode_t target,
                                     compat_result_t *results, size_t max_results,
                                     size_t found) {
    if (!node || found >= max_results) {
        return found;
    }
    
    /* Check if this node type maps to a non-portable feature */
    for (size_t i = 0; i < AST_FEATURE_MAP_SIZE; i++) {
        if (node->type == ast_feature_map[i].type) {
            /* Check if target shell supports this feature */
            if (!shell_supports_feature(target, ast_feature_map[i].feature)) {
                /* Look up the feature in the TOML database */
                const compat_entry_t *entry = compat_get_first_by_feature(
                    ast_feature_map[i].feature);
                
                /* Use first matching entry, or create a basic result */
                compat_result_t *result = &results[found];
                result->is_portable = false;
                result->target = target;
                result->line = (int)node->loc.line;
                result->column = (int)node->loc.column;
                result->entry = entry;  /* May be NULL if no TOML entry */
                
                found++;
                if (found >= max_results) {
                    return found;
                }
            }
            break;
        }
    }
    
    /* Recurse into children */
    node_t *child = node->first_child;
    while (child && found < max_results) {
        found = compat_check_ast_node(child, target, results, max_results, found);
        child = child->next_sibling;
    }
    
    /* Recurse into siblings (for command lists at same level) */
    if (node->next_sibling && found < max_results) {
        found = compat_check_ast_node(node->next_sibling, target, results, max_results, found);
    }
    
    return found;
}

size_t compat_check_ast(node_t *ast, shell_mode_t target,
                        compat_result_t *results, size_t max_results) {
    if (!g_compat.initialized || !ast || !results || max_results == 0) {
        return 0;
    }
    
    return compat_check_ast_node(ast, target, results, max_results, 0);
}

/**
 * @brief Recursive AST walker for collecting issues with stable strings
 */
static size_t compat_collect_ast_issues(node_t *node, shell_mode_t target,
                                         compat_ast_issue_t *issues,
                                         size_t max_issues, size_t found) {
    if (!node || found >= max_issues) {
        return found;
    }
    
    /* Check if this node type maps to a non-portable feature */
    for (size_t i = 0; i < AST_FEATURE_MAP_SIZE; i++) {
        if (node->type == ast_feature_map[i].type) {
            if (!shell_supports_feature(target, ast_feature_map[i].feature)) {
                compat_ast_issue_t *issue = &issues[found];
                issue->line = (int)node->loc.line;
                issue->column = (int)node->loc.column;
                issue->feature = ast_feature_map[i].feature;
                
                /* Find the entry in the internal database (stable pointers) */
                issue->severity = "warning";
                issue->message = "Non-portable construct";
                issue->suggestion = NULL;
                
                for (size_t j = 0; j < g_compat.entry_count; j++) {
                    if (g_compat.entries[j].feature &&
                        lle_unicode_strings_equal(g_compat.entries[j].feature, 
                                                  ast_feature_map[i].feature,
                                                  &LLE_UNICODE_COMPARE_DEFAULT)) {
                        issue->severity = compat_severity_name(
                            g_compat.entries[j].severity);
                        if (g_compat.entries[j].lint_message) {
                            issue->message = g_compat.entries[j].lint_message;
                        }
                        issue->suggestion = g_compat.entries[j].lint_suggestion;
                        break;
                    }
                }
                
                found++;
                if (found >= max_issues) {
                    return found;
                }
            }
            break;
        }
    }
    
    /* Recurse into children */
    node_t *child = node->first_child;
    while (child && found < max_issues) {
        found = compat_collect_ast_issues(child, target, issues, max_issues, found);
        child = child->next_sibling;
    }
    
    /* Recurse into siblings */
    if (node->next_sibling && found < max_issues) {
        found = compat_collect_ast_issues(node->next_sibling, target, issues, 
                                           max_issues, found);
    }
    
    return found;
}

size_t compat_check_ast_issues(node_t *ast, shell_mode_t target,
                               compat_ast_issue_t *issues, size_t max_issues) {
    if (!g_compat.initialized || !ast || !issues || max_issues == 0) {
        return 0;
    }
    
    return compat_collect_ast_issues(ast, target, issues, max_issues, 0);
}

/* ============================================================================
 * Public API - Strict Mode
 * ============================================================================ */

void compat_set_strict(bool strict) {
    g_compat.strict_mode = strict;
}

bool compat_is_strict(void) {
    return g_compat.strict_mode;
}

compat_severity_t compat_effective_severity(const compat_entry_t *entry) {
    if (!entry) {
        return COMPAT_SEVERITY_INFO;
    }
    
    if (g_compat.strict_mode && entry->lint.severity == COMPAT_SEVERITY_WARNING) {
        return COMPAT_SEVERITY_ERROR;
    }
    
    return entry->lint.severity;
}

/* ============================================================================
 * Public API - Target Shell
 * ============================================================================ */

void compat_set_target(const char *target) {
    if (target) {
        strncpy(g_compat.target_shell, target, COMPAT_TARGET_MAX - 1);
        g_compat.target_shell[COMPAT_TARGET_MAX - 1] = '\0';
    } else {
        strncpy(g_compat.target_shell, "posix", COMPAT_TARGET_MAX - 1);
    }
}

const char *compat_get_target(void) {
    if (g_compat.target_shell[0] == '\0') {
        return "posix";  /* Default if not set */
    }
    return g_compat.target_shell;
}

/* ============================================================================
 * Public API - Utilities
 * ============================================================================ */

const char *compat_category_name(compat_category_t category) {
    if (category >= COMPAT_CATEGORY_COUNT) {
        return "unknown";
    }
    return category_names[category];
}

const char *compat_severity_name(compat_severity_t severity) {
    if (severity >= COMPAT_SEVERITY_COUNT) {
        return "unknown";
    }
    return severity_names[severity];
}

bool compat_category_parse(const char *name, compat_category_t *category) {
    if (!name || !category) {
        return false;
    }
    
    for (int i = 0; i < COMPAT_CATEGORY_COUNT; i++) {
        if (strcmp(name, category_names[i]) == 0) {
            *category = (compat_category_t)i;
            return true;
        }
    }
    
    return false;
}

bool compat_severity_parse(const char *name, compat_severity_t *severity) {
    if (!name || !severity) {
        return false;
    }
    
    for (int i = 0; i < COMPAT_SEVERITY_COUNT; i++) {
        if (strcmp(name, severity_names[i]) == 0) {
            *severity = (compat_severity_t)i;
            return true;
        }
    }
    
    return false;
}

const char *compat_fix_type_name(fix_type_t type) {
    if (type > FIX_TYPE_MANUAL) {
        return "unknown";
    }
    return fix_type_names[type];
}

bool compat_fix_type_parse(const char *name, fix_type_t *type) {
    if (!name || !type) {
        return false;
    }
    
    /* Check known fix type names */
    if (strcmp(name, "safe") == 0) {
        *type = FIX_TYPE_SAFE;
        return true;
    } else if (strcmp(name, "unsafe") == 0) {
        *type = FIX_TYPE_UNSAFE;
        return true;
    } else if (strcmp(name, "manual") == 0) {
        *type = FIX_TYPE_MANUAL;
        return true;
    } else if (strcmp(name, "none") == 0) {
        *type = FIX_TYPE_NONE;
        return true;
    }
    
    return false;
}

fix_type_t compat_get_fix_type_for_target(const compat_fix_class_t *fix_class,
                                           const char *target) {
    if (!fix_class) {
        return FIX_TYPE_NONE;
    }
    
    if (!target || strcasecmp(target, "posix") == 0 || strcasecmp(target, "sh") == 0) {
        return fix_class->posix;
    } else if (strcasecmp(target, "bash") == 0) {
        return fix_class->bash;
    } else if (strcasecmp(target, "zsh") == 0) {
        return fix_class->zsh;
    } else if (strcasecmp(target, "lush") == 0) {
        return fix_class->lush;
    }
    /* Unknown target, default to POSIX (most conservative) */
    return fix_class->posix;
}

int compat_format_result(const compat_result_t *result, char *buffer,
                         size_t size) {
    if (!result || !buffer || size == 0) {
        return 0;
    }
    
    if (!result->entry) {
        return snprintf(buffer, size, "No compatibility issues found");
    }
    
    const compat_entry_t *entry = result->entry;
    compat_severity_t sev = compat_effective_severity(entry);
    
    int written = snprintf(buffer, size, "%s:%d: %s: %s",
                           entry->feature ? entry->feature : "unknown",
                           result->line,
                           compat_severity_name(sev),
                           entry->lint.message ? entry->lint.message : "");
    
    if (entry->lint.suggestion && written > 0 && (size_t)written < size - 1) {
        written += snprintf(buffer + written, size - (size_t)written,
                            " (%s)", entry->lint.suggestion);
    }
    
    return written;
}

/* ============================================================================
 * Public API - Debugging
 * ============================================================================ */

void compat_debug_print_stats(void) {
    fprintf(stderr, "Compatibility Database Statistics:\n");
    fprintf(stderr, "  Data directory: %s\n", g_compat.data_dir);
    fprintf(stderr, "  Initialized: %s\n", g_compat.initialized ? "yes" : "no");
    fprintf(stderr, "  Total entries: %zu\n", g_compat.entry_count);
    fprintf(stderr, "  Strict mode: %s\n", g_compat.strict_mode ? "yes" : "no");
    fprintf(stderr, "  Target shell: %s\n", compat_get_target());
    
    /* Count by category */
    size_t by_category[COMPAT_CATEGORY_COUNT] = {0};
    size_t with_pattern = 0;
    
    for (size_t i = 0; i < g_compat.entry_count; i++) {
        by_category[g_compat.entries[i].category]++;
        if (g_compat.entries[i].regex_valid) {
            with_pattern++;
        }
    }
    
    fprintf(stderr, "  Entries with valid patterns: %zu\n", with_pattern);
    fprintf(stderr, "  By category:\n");
    for (int i = 0; i < COMPAT_CATEGORY_COUNT; i++) {
        fprintf(stderr, "    %s: %zu\n", category_names[i], by_category[i]);
    }
}

void compat_debug_print_entry(const compat_entry_t *entry) {
    if (!entry) {
        fprintf(stderr, "Entry: (null)\n");
        return;
    }
    
    fprintf(stderr, "Entry: %s\n", entry->id ? entry->id : "(no id)");
    fprintf(stderr, "  Category: %s\n", compat_category_name(entry->category));
    fprintf(stderr, "  Feature: %s\n", entry->feature ? entry->feature : "(none)");
    fprintf(stderr, "  Description: %s\n",
            entry->description ? entry->description : "(none)");
    fprintf(stderr, "  Behavior:\n");
    fprintf(stderr, "    POSIX: %s\n",
            entry->behavior.posix ? entry->behavior.posix : "(none)");
    fprintf(stderr, "    Bash: %s\n",
            entry->behavior.bash ? entry->behavior.bash : "(none)");
    fprintf(stderr, "    Zsh: %s\n",
            entry->behavior.zsh ? entry->behavior.zsh : "(none)");
    fprintf(stderr, "    Lush: %s\n",
            entry->behavior.lush ? entry->behavior.lush : "(none)");
    fprintf(stderr, "  Lint:\n");
    fprintf(stderr, "    Severity: %s\n",
            compat_severity_name(entry->lint.severity));
    fprintf(stderr, "    Message: %s\n",
            entry->lint.message ? entry->lint.message : "(none)");
    fprintf(stderr, "    Suggestion: %s\n",
            entry->lint.suggestion ? entry->lint.suggestion : "(none)");
    fprintf(stderr, "    Pattern: %s\n",
            entry->lint.pattern ? entry->lint.pattern : "(none)");
}
