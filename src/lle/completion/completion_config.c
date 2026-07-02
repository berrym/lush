/**
 * @file completion_config.c
 * @brief Completion Config File Parser
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Parses ~/.config/lush/completions.toml and registers command-based
 * completion sources using the custom source API (Layer 2).
 *
 * Config format:
 *   [sources.NAME]
 *   description = "Description"
 *   applies_to = ["cmd subcmd", "other"]
 *   argument = 2
 *   command = "shell command"
 *   suffix = " "
 *   cache_seconds = 5
 */

#include "lle/completion/custom_source.h"
#include "lle/error_handling.h"
#include "lle/prompt/theme_parser.h"
#include "lush_fork.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

#define MAX_CONFIG_SOURCES 64
#define MAX_APPLIES_TO 16
#define MAX_COMMAND_OUTPUT 4096
#define COMMAND_TIMEOUT_SECONDS 2
#define DEFAULT_CACHE_SECONDS 0
#define CONFIG_FILENAME "completions.toml"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================
 */

static struct {
    lle_completion_config_t config;
    pthread_mutex_t mutex;
    bool initialized;
} g_completion_config = {
    .config = {0}, .mutex = PTHREAD_MUTEX_INITIALIZER, .initialized = false};

/* ============================================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Get XDG config path for completions.toml
 *
 * Returns the path to the user's completion configuration file,
 * checking XDG_CONFIG_HOME first, then falling back to ~/.config.
 *
 * @return Allocated path string or NULL on failure
 */
static char *get_config_path(void) {
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char *path = NULL;

    if (config_home && config_home[0] != '\0') {
        size_t len = strlen(config_home) + strlen("/lush/") +
                     strlen(CONFIG_FILENAME) + 1;
        path = malloc(len);
        if (path) {
            snprintf(path, len, "%s/lush/%s", config_home, CONFIG_FILENAME);
        }
    } else if (home && home[0] != '\0') {
        size_t len = strlen(home) + strlen("/.config/lush/") +
                     strlen(CONFIG_FILENAME) + 1;
        path = malloc(len);
        if (path) {
            snprintf(path, len, "%s/.config/lush/%s", home, CONFIG_FILENAME);
        }
    }

    return path;
}

/**
 * @brief Read entire file into string
 *
 * Reads the contents of the specified file into a newly allocated
 * string buffer. Limited to 1MB maximum file size.
 *
 * @param path Path to the file to read
 * @param out_size Optional output for the number of bytes read
 * @return Allocated string with file contents, or NULL on error
 */
static char *read_file(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }

    /// Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0 || size > 1024 * 1024) { /// 1MB limit
        fclose(fp);
        return NULL;
    }

    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }

    size_t read_size = fread(content, 1, (size_t)size, fp);
    fclose(fp);

    content[read_size] = '\0';
    if (out_size) {
        *out_size = read_size;
    }

    return content;
}

/**
 * @brief Free a command source config
 *
 * Frees all memory associated with a command source configuration,
 * including the name, description, command, suffix, applies_to array,
 * and cached results.
 *
 * @param config Configuration to free
 */
void lle_command_source_config_free(lle_command_source_config_t *config) {
    if (!config) {
        return;
    }

    free(config->name);
    free(config->description);
    free(config->command);
    free(config->suffix);

    if (config->applies_to) {
        for (size_t i = 0; i < config->applies_to_count; i++) {
            free(config->applies_to[i]);
        }
        free(config->applies_to);
    }

    if (config->cached_results) {
        for (size_t i = 0; i < config->cached_count; i++) {
            free(config->cached_results[i]);
        }
        free(config->cached_results);
    }

    memset(config, 0, sizeof(*config));
}

/**
 * @brief Clear cached results for a config source
 *
 * Frees the cached completion results and resets the cache timestamp.
 *
 * @param config Configuration whose cache to clear
 */
void lle_command_source_clear_cache(lle_command_source_config_t *config) {
    if (!config || !config->cached_results) {
        return;
    }

    for (size_t i = 0; i < config->cached_count; i++) {
        free(config->cached_results[i]);
    }
    free(config->cached_results);

    config->cached_results = NULL;
    config->cached_count = 0;
    config->cache_time = 0;
}

/**
 * @brief Clear all config source caches
 *
 * Clears the cached results for all loaded completion sources.
 * Thread-safe via mutex protection.
 */
void lle_completion_clear_all_caches(void) {
    pthread_mutex_lock(&g_completion_config.mutex);

    for (size_t i = 0; i < g_completion_config.config.source_count; i++) {
        lle_command_source_clear_cache(&g_completion_config.config.sources[i]);
    }

    pthread_mutex_unlock(&g_completion_config.mutex);
}

/* ============================================================================
 * COMMAND EXECUTION ENGINE
 * ============================================================================
 */

/**
 * @brief Execute command and return output lines
 *
 * Executes a shell command with a timeout and returns the output
 * as an array of lines. Handles forking, pipe management, and
 * timeout enforcement.
 *
 * @param command Shell command to execute
 * @param out_lines Output array of line strings
 * @param out_count Output count of lines
 * @return LLE_SUCCESS or error code
 */
static lle_result_t execute_command(const char *command, char ***out_lines,
                                    size_t *out_count) {
    if (!command || !out_lines || !out_count) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    *out_lines = NULL;
    *out_count = 0;

    /// Create pipe for reading command output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return LLE_FAULT(LLE_ERROR_IO_ERROR, "completion",
                         "completion command IO failed");
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return LLE_FAULT(LLE_ERROR_IO_ERROR, "completion",
                         "completion command IO failed");
    }

    if (pid == 0) {
        /// Child process
        close(pipefd[0]); /// Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        /// Redirect stderr to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        /// The async worker blocks all async signals; clear the mask so the
        /// spawned shell does not inherit that block across exec.
        sigset_t empty;
        sigemptyset(&empty);
        pthread_sigmask(SIG_SETMASK, &empty, NULL);

        /// Execute command via shell
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    /// Parent process
    close(pipefd[1]); /// Close write end

    /// Set up timeout
    struct timeval tv;
    tv.tv_sec = COMMAND_TIMEOUT_SECONDS;
    tv.tv_usec = 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipefd[0], &readfds);

    /// Read output with timeout
    char buffer[MAX_COMMAND_OUTPUT];
    size_t total_read = 0;

    while (total_read < sizeof(buffer) - 1) {
        FD_ZERO(&readfds);
        FD_SET(pipefd[0], &readfds);

        int ret = select(pipefd[0] + 1, &readfds, NULL, NULL, &tv);
        if (ret <= 0) {
            /// Timeout or error
            break;
        }

        ssize_t n = read(pipefd[0], buffer + total_read,
                         sizeof(buffer) - 1 - total_read);
        if (n <= 0) {
            break;
        }
        total_read += (size_t)n;
    }
    buffer[total_read] = '\0';

    close(pipefd[0]);

    /// Kill child if still running (timeout)
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == 0) {
        /// Child still running, kill it
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }

    /// Parse output into lines
    size_t line_capacity = 64;
    char **lines = malloc(line_capacity * sizeof(char *));
    if (!lines) {
        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                         "completion config allocation failed");
    }

    size_t line_count = 0;
    char *line_start = buffer;

    for (char *p = buffer; *p; p++) {
        if (*p == '\n') {
            *p = '\0';

            /// Skip empty lines
            if (p > line_start) {
                if (line_count >= line_capacity) {
                    line_capacity *= 2;
                    char **new_lines =
                        realloc(lines, line_capacity * sizeof(char *));
                    if (!new_lines) {
                        for (size_t i = 0; i < line_count; i++) {
                            free(lines[i]);
                        }
                        free(lines);
                        return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                                         "completion config allocation failed");
                    }
                    lines = new_lines;
                }

                lines[line_count] = strdup(line_start);
                if (!lines[line_count]) {
                    for (size_t i = 0; i < line_count; i++) {
                        free(lines[i]);
                    }
                    free(lines);
                    return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                                     "completion config allocation failed");
                }
                line_count++;
            }

            line_start = p + 1;
        }
    }

    /// Handle last line without newline
    if (*line_start) {
        if (line_count >= line_capacity) {
            line_capacity++;
            char **new_lines = realloc(lines, line_capacity * sizeof(char *));
            if (!new_lines) {
                for (size_t i = 0; i < line_count; i++) {
                    free(lines[i]);
                }
                free(lines);
                return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                                 "completion config allocation failed");
            }
            lines = new_lines;
        }

        lines[line_count] = strdup(line_start);
        if (!lines[line_count]) {
            for (size_t i = 0; i < line_count; i++) {
                free(lines[i]);
            }
            free(lines);
            return LLE_FAULT(LLE_ERROR_OUT_OF_MEMORY, "completion",
                             "completion config allocation failed");
        }
        line_count++;
    }

    *out_lines = lines;
    *out_count = line_count;
    return LLE_SUCCESS;
}

/* ============================================================================
 * CONFIG SOURCE CALLBACKS
 * ============================================================================
 */

/**
 * @brief Check if pattern matches context
 *
 * Checks if a pattern matches the current command context.
 * Pattern format: "cmd" or "cmd subcmd"
 *
 * @param pattern Pattern string to match
 * @param command_name Current command name
 * @param argument_index Current argument position
 * @return true if pattern matches, false otherwise
 */
static bool pattern_matches(const char *pattern, const char *command_name,
                            int argument_index) {
    if (!pattern || !command_name) {
        return false;
    }

    /// Extract first word from pattern
    char pattern_cmd[64];
    const char *space = strchr(pattern, ' ');

    if (space) {
        size_t len = (size_t)(space - pattern);
        if (len >= sizeof(pattern_cmd)) {
            len = sizeof(pattern_cmd) - 1;
        }
        memcpy(pattern_cmd, pattern, len);
        pattern_cmd[len] = '\0';
    } else {
        snprintf(pattern_cmd, sizeof(pattern_cmd), "%s", pattern);
    }

    /// Check if command matches
    if (strcmp(pattern_cmd, command_name) != 0) {
        return false;
    }

    /// If pattern has subcommand, we need to be at argument >= 2
    if (space) {
        /// Count words in pattern
        int word_count = 1;
        for (const char *p = pattern; *p; p++) {
            if (*p == ' ' && *(p + 1) && *(p + 1) != ' ') {
                word_count++;
            }
        }

        /// Must be at or past the pattern's argument position
        if (argument_index < word_count) {
            return false;
        }
    }

    return true;
}

/**
 * @brief is_applicable callback for config-based sources
 *
 * Determines if a config-based completion source should be used
 * for the current completion context based on argument position
 * and applies_to patterns.
 *
 * @param user_data Pointer to lle_command_source_config_t
 * @param context Current completion context
 * @return true if source is applicable, false otherwise
 */
static bool config_source_is_applicable(void *user_data,
                                        const lle_word_context_t *context) {
    lle_command_source_config_t *config =
        (lle_command_source_config_t *)user_data;

    if (!config || !context || !context->command_name) {
        return false;
    }

    /// Check argument position constraint
    if (config->argument > 0 && context->arg_index != config->argument) {
        return false;
    }

    /// Check applies_to patterns
    for (size_t i = 0; i < config->applies_to_count; i++) {
        if (pattern_matches(config->applies_to[i], context->command_name,
                            context->arg_index)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief generate callback for config-based sources
 *
 * Generates completions by executing the configured shell command
 * and filtering the output by prefix. Supports result caching.
 *
 * @param user_data Pointer to lle_command_source_config_t
 * @param context Current completion context
 * @param prefix Prefix to filter completions by
 * @param result Result set to populate
 * @return LLE_SUCCESS or error code
 */
static lle_result_t config_source_generate(void *user_data,
                                           const lle_word_context_t *context,
                                           lle_completion_result_t *result) {
    lle_command_source_config_t *config =
        (lle_command_source_config_t *)user_data;

    if (!config || !context || !result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }
    const char *prefix = context->dequoted_filename_prefix
                             ? context->dequoted_filename_prefix
                             : "";

    char **lines = NULL;
    size_t line_count = 0;

    /// Check cache
    time_t now = time(NULL);
    bool use_cache = config->cache_seconds > 0 && config->cached_results &&
                     (now - config->cache_time) < config->cache_seconds;

    if (use_cache) {
        lines = config->cached_results;
        line_count = config->cached_count;
    } else {
        /// Execute command
        lle_result_t res =
            execute_command(config->command, &lines, &line_count);
        if (res != LLE_SUCCESS) {
            return LLE_SUCCESS; /// Return empty results on command failure
        }

        /// Update cache if enabled
        if (config->cache_seconds > 0) {
            /// Clear old cache
            lle_command_source_clear_cache(config);

            /// Store new cache
            config->cached_results = lines;
            config->cached_count = line_count;
            config->cache_time = now;
            use_cache = true; /// Don't free lines - they're cached
        }
    }

    /// Filter and add matching results
    size_t prefix_len = strlen(prefix);

    for (size_t i = 0; i < line_count; i++) {
        /// Skip if doesn't match prefix
        if (prefix_len > 0 && strncmp(lines[i], prefix, prefix_len) != 0) {
            continue;
        }

        /// Add to results
        lle_completion_add_item(
            result, lines[i], config->suffix ? config->suffix : " ",
            config->description, 700); /// Medium-high priority
    }

    /// Free lines if not cached
    if (!use_cache && lines) {
        for (size_t i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);
    }

    (void)context; /// Unused in generate
    return LLE_SUCCESS;
}

/* ============================================================================
 * CONFIG PARSER
 * ============================================================================
 */

/**
 * @brief Parser context for callback
 *
 * Holds state during TOML config file parsing.
 */
typedef struct {
    lle_completion_config_t *config;
    char current_source_name[64];
    lle_command_source_config_t *current_source;
    char error_msg[256];
    lle_result_t error;
} config_parser_ctx_t;

/**
 * @brief Find or create source entry
 *
 * Looks up an existing source by name or creates a new one
 * if not found. Manages array growth as needed.
 *
 * @param ctx Parser context containing config state
 * @param name Source name to find or create
 * @return Pointer to source config or NULL on error
 */
static lle_command_source_config_t *
get_or_create_source(config_parser_ctx_t *ctx, const char *name) {

    lle_completion_config_t *config = ctx->config;

    /// Check if source already exists
    for (size_t i = 0; i < config->source_count; i++) {
        if (config->sources[i].name &&
            strcmp(config->sources[i].name, name) == 0) {
            return &config->sources[i];
        }
    }

    /// Create new source
    if (config->source_count >= config->source_capacity) {
        size_t new_capacity =
            config->source_capacity == 0 ? 8 : config->source_capacity * 2;
        if (new_capacity > MAX_CONFIG_SOURCES) {
            new_capacity = MAX_CONFIG_SOURCES;
        }
        if (config->source_count >= new_capacity) {
            return NULL; /// At capacity
        }

        lle_command_source_config_t *new_sources =
            realloc(config->sources,
                    new_capacity * sizeof(lle_command_source_config_t));
        if (!new_sources) {
            return NULL;
        }

        memset(new_sources + config->source_capacity, 0,
               (new_capacity - config->source_capacity) *
                   sizeof(lle_command_source_config_t));

        config->sources = new_sources;
        config->source_capacity = new_capacity;
    }

    lle_command_source_config_t *source =
        &config->sources[config->source_count];
    memset(source, 0, sizeof(*source));
    source->name = strdup(name);
    config->source_count++;

    return source;
}

/**
 * @brief Config parser callback
 *
 * Called for each key-value pair during TOML parsing.
 * Processes [sources.NAME] sections and populates source configs.
 *
 * @param section Current section name
 * @param key Key name
 * @param value Parsed value
 * @param user_data Parser context pointer
 * @return LLE_SUCCESS or error code
 */
static lle_result_t config_parser_callback(const char *section, const char *key,
                                           const lle_theme_value_t *value,
                                           void *user_data) {
    config_parser_ctx_t *ctx = (config_parser_ctx_t *)user_data;

    /// Only process [sources.NAME] sections
    if (strncmp(section, "sources.", 8) != 0) {
        return LLE_SUCCESS;
    }

    const char *source_name = section + 8;
    if (strlen(source_name) == 0) {
        return LLE_SUCCESS;
    }

    /// Get or create source
    lle_command_source_config_t *source =
        get_or_create_source(ctx, source_name);
    if (!source) {
        ctx->error = LLE_ERROR_OUT_OF_MEMORY;
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Too many sources or out of memory");
        return ctx->error;
    }

    /// Handle key-value pairs
    if (strcmp(key, "description") == 0 &&
        value->type == LLE_THEME_VALUE_STRING) {
        free(source->description);
        source->description = strdup(value->data.string);
    } else if (strcmp(key, "command") == 0 &&
               value->type == LLE_THEME_VALUE_STRING) {
        free(source->command);
        source->command = strdup(value->data.string);
    } else if (strcmp(key, "suffix") == 0 &&
               value->type == LLE_THEME_VALUE_STRING) {
        free(source->suffix);
        source->suffix = strdup(value->data.string);
    } else if (strcmp(key, "argument") == 0 &&
               value->type == LLE_THEME_VALUE_INTEGER) {
        source->argument = (int)value->data.integer;
    } else if (strcmp(key, "cache_seconds") == 0 &&
               value->type == LLE_THEME_VALUE_INTEGER) {
        source->cache_seconds = (int)value->data.integer;
    } else if (strcmp(key, "applies_to") == 0 &&
               value->type == LLE_THEME_VALUE_ARRAY) {
        /// Free existing
        if (source->applies_to) {
            for (size_t i = 0; i < source->applies_to_count; i++) {
                free(source->applies_to[i]);
            }
            free(source->applies_to);
        }

        /// Parse array
        size_t count = value->data.array.count;
        if (count > MAX_APPLIES_TO) {
            count = MAX_APPLIES_TO;
        }

        source->applies_to = malloc(count * sizeof(char *));
        if (!source->applies_to) {
            ctx->error = LLE_ERROR_OUT_OF_MEMORY;
            return ctx->error;
        }

        source->applies_to_count = 0;
        for (size_t i = 0; i < count; i++) {
            if (value->data.array.items[i].type == LLE_THEME_VALUE_STRING) {
                source->applies_to[source->applies_to_count] =
                    strdup(value->data.array.items[i].data.string);
                if (source->applies_to[source->applies_to_count]) {
                    source->applies_to_count++;
                }
            }
        }
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

/**
 * @brief Load completion sources from config file
 *
 * Loads the completion configuration from the default path
 * (~/.config/lush/completions.toml or XDG equivalent).
 *
 * @return LLE_SUCCESS or error code (LLE_ERROR_NOT_FOUND is OK)
 */
lle_result_t lle_completion_load_config(void) {
    char *path = get_config_path();
    if (!path) {
        return LLE_SUCCESS; /// No config path, not an error
    }

    lle_result_t result = lle_completion_load_config_file(path);
    free(path);

    /// LLE_ERROR_NOT_FOUND is OK - config file is optional
    if (result == LLE_ERROR_NOT_FOUND) {
        return LLE_SUCCESS;
    }

    return result;
}

/**
 * @brief Load completion sources from specific path
 *
 * Parses a TOML config file and registers all defined completion
 * sources with the custom source API.
 *
 * @param path Path to the config file
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_load_config_file(const char *path) {
    if (!path) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /// Check if file exists
    struct stat st;
    if (stat(path, &st) != 0) {
        return LLE_ERROR_NOT_FOUND;
    }

    /// Read file
    size_t content_size;
    char *content = read_file(path, &content_size);
    if (!content) {
        return LLE_FAULT(LLE_ERROR_IO_ERROR, "completion",
                         "completion command IO failed");
    }

    pthread_mutex_lock(&g_completion_config.mutex);

    /// Clear existing config
    if (g_completion_config.initialized) {
        /// Unregister existing sources
        for (size_t i = 0; i < g_completion_config.config.source_count; i++) {
            lle_completion_unregister_source(
                g_completion_config.config.sources[i].name);
            lle_command_source_config_free(
                &g_completion_config.config.sources[i]);
        }
        free(g_completion_config.config.sources);
        free(g_completion_config.config.config_path);
        memset(&g_completion_config.config, 0,
               sizeof(g_completion_config.config));
    }

    /// Initialize parser
    lle_theme_parser_t parser;
    lle_result_t result = lle_theme_parser_init(&parser, content);
    if (result != LLE_SUCCESS) {
        free(content);
        pthread_mutex_unlock(&g_completion_config.mutex);
        return result;
    }

    /// Parse config
    config_parser_ctx_t ctx = {.config = &g_completion_config.config,
                               .current_source_name = {0},
                               .current_source = NULL,
                               .error_msg = {0},
                               .error = LLE_SUCCESS};

    result = lle_theme_parser_parse(&parser, config_parser_callback, &ctx);
    free(content);

    if (result != LLE_SUCCESS) {
        pthread_mutex_unlock(&g_completion_config.mutex);
        return result;
    }

    if (ctx.error != LLE_SUCCESS) {
        pthread_mutex_unlock(&g_completion_config.mutex);
        return ctx.error;
    }

    /// Register parsed sources with custom source API
    for (size_t i = 0; i < g_completion_config.config.source_count; i++) {
        lle_command_source_config_t *src =
            &g_completion_config.config.sources[i];

        /// Validate required fields
        if (!src->command || !src->applies_to || src->applies_to_count == 0) {
            continue; /// Skip invalid sources
        }

        /// Register with custom source API
        lle_custom_completion_source_t custom_source = {
            .name = src->name,
            .description = src->description,
            .priority = 600, /// Medium priority
            .generate = config_source_generate,
            .is_applicable = config_source_is_applicable,
            .cleanup = NULL, /// We manage cleanup ourselves
            .user_data = src};

        lle_completion_register_source(&custom_source);
    }

    /// Store config path and mtime
    g_completion_config.config.config_path = strdup(path);
    g_completion_config.config.config_mtime = st.st_mtime;
    g_completion_config.initialized = true;

    pthread_mutex_unlock(&g_completion_config.mutex);
    return LLE_SUCCESS;
}

/**
 * @brief Reload completion config
 *
 * Reloads the completion configuration from the previously
 * loaded path, or from the default path if none was loaded.
 *
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_completion_reload_config(void) {
    pthread_mutex_lock(&g_completion_config.mutex);

    char *path = NULL;
    if (g_completion_config.config.config_path) {
        path = strdup(g_completion_config.config.config_path);
    }

    pthread_mutex_unlock(&g_completion_config.mutex);

    if (path) {
        lle_result_t result = lle_completion_load_config_file(path);
        free(path);
        return result;
    }

    return lle_completion_load_config();
}

/**
 * @brief Get the currently loaded config
 *
 * Returns a pointer to the global completion configuration.
 * Thread-safe via mutex protection.
 *
 * @return Pointer to config or NULL if not initialized
 */
const lle_completion_config_t *lle_completion_get_config(void) {
    pthread_mutex_lock(&g_completion_config.mutex);

    const lle_completion_config_t *config =
        g_completion_config.initialized ? &g_completion_config.config : NULL;

    pthread_mutex_unlock(&g_completion_config.mutex);
    return config;
}
