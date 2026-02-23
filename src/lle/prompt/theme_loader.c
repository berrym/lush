/**
 * @file theme_loader.c
 * @brief LLE Theme File Loader - File I/O and Theme Loading Implementation
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Issue #21 - Theme File Loading System
 * Version: 1.0.0
 *
 * Implements file loading, directory scanning, hot reload, and theme export.
 */

#include "lle/prompt/theme_loader.h"
#include "lle/prompt/theme_parser.h"

#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Forward declaration for portability (see ht_fnv1a.c) */
int strcasecmp(const char *s1, const char *s2);

/* ============================================================================
 * Internal Helpers
 * ============================================================================
 */

/**
 * @brief Read entire file contents into a dynamically allocated buffer.
 *
 * @param filepath The path to the file to read.
 * @param content Output pointer to receive the allocated buffer.
 * @param content_len Output pointer to receive the content length.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t read_file_contents(const char *filepath, char **content,
                                       size_t *content_len) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        return LLE_ERROR_IO_ERROR;
    }

    /* Get file size */
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return LLE_ERROR_IO_ERROR;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return LLE_ERROR_IO_ERROR;
    }

    if ((size_t)file_size > LLE_THEME_FILE_MAX_SIZE) {
        fclose(fp);
        return LLE_ERROR_BUFFER_OVERFLOW;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return LLE_ERROR_IO_ERROR;
    }

    /* Allocate buffer */
    char *buffer = malloc((size_t)file_size + 1);
    if (!buffer) {
        fclose(fp);
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    /* Read file */
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return LLE_ERROR_IO_ERROR;
    }

    buffer[bytes_read] = '\0';
    *content = buffer;
    *content_len = bytes_read;

    return LLE_SUCCESS;
}

/**
 * @brief Check if a path has the .toml extension.
 *
 * @param path The file path to check.
 * @return true if the path ends with .toml, false otherwise.
 */
static bool has_toml_extension(const char *path) {
    size_t len = strlen(path);
    size_t ext_len = strlen(LLE_THEME_FILE_EXTENSION);

    if (len < ext_len) {
        return false;
    }

    return strcasecmp(path + len - ext_len, LLE_THEME_FILE_EXTENSION) == 0;
}

/**
 * @brief Get the user's home directory path.
 *
 * Checks the HOME environment variable first, then falls back
 * to the passwd entry.
 *
 * @return The home directory path, or NULL if not found.
 */
static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        return home;
    }

    /* Fallback to passwd entry */
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return pw->pw_dir;
    }

    return NULL;
}

/**
 * @brief Create a directory and all parent directories recursively.
 *
 * @param path The directory path to create.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
static lle_result_t mkdir_recursive(const char *path) {
    char tmp[LLE_THEME_PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return LLE_ERROR_PERMISSION_DENIED;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return LLE_ERROR_PERMISSION_DENIED;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * Core Loading API
 * ============================================================================
 */

/**
 * @brief Load a theme from a TOML file.
 *
 * Reads and parses a theme file, populating the theme structure
 * with the parsed configuration.
 *
 * @param filepath The path to the theme file.
 * @param theme Output structure to receive the loaded theme.
 * @param result Optional output for detailed load results and errors.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_load_from_file(const char *filepath, lle_theme_t *theme,
                                      lle_theme_load_result_t *result) {
    if (!filepath || !theme) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Initialize result if provided */
    if (result) {
        memset(result, 0, sizeof(*result));
        snprintf(result->filepath, sizeof(result->filepath), "%s", filepath);
    }

    /* Read file contents */
    char *content = NULL;
    size_t content_len = 0;
    lle_result_t status = read_file_contents(filepath, &content, &content_len);

    if (status != LLE_SUCCESS) {
        if (result) {
            result->status = status;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Failed to read file: %s", strerror(errno));
        }
        return status;
    }

    /* Parse the content */
    status = lle_theme_load_from_string(content, theme, result);
    free(content);

    if (status == LLE_SUCCESS) {
        /* Mark theme source as USER (loaded from file) */
        theme->source = LLE_THEME_SOURCE_USER;

        if (result) {
            result->status = LLE_SUCCESS;
            snprintf(result->theme_name, sizeof(result->theme_name), "%s",
                     theme->name);
        }
    }

    return status;
}

/**
 * @brief Load a theme from a TOML string.
 *
 * Parses TOML content and populates the theme structure.
 *
 * @param content The TOML content string.
 * @param theme Output structure to receive the loaded theme.
 * @param result Optional output for detailed load results and errors.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_load_from_string(const char *content, lle_theme_t *theme,
                                        lle_theme_load_result_t *result) {
    if (!content || !theme) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Initialize theme */
    memset(theme, 0, sizeof(*theme));
    lle_symbol_set_init_unicode(&theme->symbols);

    /* Initialize parser */
    lle_theme_parser_t parser;
    lle_result_t status = lle_theme_parser_init(&parser, content);

    if (status != LLE_SUCCESS) {
        if (result) {
            result->status = status;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Parser initialization failed");
        }
        return status;
    }

    /* Parse into theme */
    status = lle_theme_parser_parse_to_theme(&parser, theme);

    if (status != LLE_SUCCESS) {
        if (result) {
            result->status = status;
            result->error_line = lle_theme_parser_error_line(&parser);
            result->error_column = lle_theme_parser_error_column(&parser);
            snprintf(result->error_msg, sizeof(result->error_msg), "%s",
                     lle_theme_parser_error(&parser));
        }
        return status;
    }

    /* Validate theme */
    char validation_error[256];
    status = lle_theme_parser_validate(theme, validation_error,
                                       sizeof(validation_error));

    if (status != LLE_SUCCESS) {
        if (result) {
            result->status = status;
            snprintf(result->error_msg, sizeof(result->error_msg),
                     "Validation failed: %.230s", validation_error);
        }
        return status;
    }

    if (result) {
        result->status = LLE_SUCCESS;
        snprintf(result->theme_name, sizeof(result->theme_name), "%s",
                 theme->name);
    }

    return LLE_SUCCESS;
}

/**
 * @brief Load all theme files from a directory.
 *
 * Scans the directory for .toml files and loads each as a theme,
 * registering them with the provided registry.
 *
 * @param dirpath The directory path to scan.
 * @param registry The theme registry to register loaded themes with.
 * @param result Optional output for batch load results.
 * @return The number of themes successfully loaded.
 */
size_t lle_theme_load_directory(const char *dirpath,
                                lle_theme_registry_t *registry,
                                lle_theme_batch_result_t *result) {
    if (!dirpath || !registry) {
        return 0;
    }

    /* Initialize result if provided */
    if (result) {
        result->total_files = 0;
        result->loaded_count = 0;
        result->failed_count = 0;
        result->skipped_count = 0;
    }

    /* Open directory */
    DIR *dir = opendir(dirpath);
    if (!dir) {
        return 0;
    }

    size_t loaded = 0;
    size_t result_idx = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip non-TOML files */
        if (!has_toml_extension(entry->d_name)) {
            continue;
        }

        if (result) {
            result->total_files++;
        }

        /* Build full path */
        char filepath[LLE_THEME_PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        /* Check if it's a regular file */
        struct stat st;
        if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        /* Allocate theme */
        lle_theme_t *theme = calloc(1, sizeof(lle_theme_t));
        if (!theme) {
            if (result) {
                result->failed_count++;
            }
            continue;
        }

        /* Get result slot if available */
        lle_theme_load_result_t *load_result = NULL;
        if (result && result->results &&
            result_idx < result->results_capacity) {
            load_result = &result->results[result_idx++];
        }

        /* Load theme */
        lle_result_t status =
            lle_theme_load_from_file(filepath, theme, load_result);

        if (status != LLE_SUCCESS) {
            free(theme);
            if (result) {
                result->failed_count++;
            }
            continue;
        }

        /* Check if theme already exists */
        if (lle_theme_registry_find(registry, theme->name)) {
            if (result) {
                result->skipped_count++;
            }
            if (load_result) {
                load_result->status = LLE_ERROR_ALREADY_EXISTS;
                snprintf(load_result->error_msg, sizeof(load_result->error_msg),
                         "Theme '%s' already exists", theme->name);
            }
            free(theme);
            continue;
        }

        /* Register theme */
        status = lle_theme_registry_register(registry, theme);
        if (status != LLE_SUCCESS) {
            free(theme);
            if (result) {
                result->failed_count++;
            }
            if (load_result) {
                load_result->status = status;
                snprintf(load_result->error_msg, sizeof(load_result->error_msg),
                         "Failed to register theme");
            }
            continue;
        }

        loaded++;
        if (result) {
            result->loaded_count++;
        }
    }

    closedir(dir);
    return loaded;
}

/**
 * @brief Load user themes from standard locations.
 *
 * Loads themes from both the user directory (~/.config/lush/themes)
 * and the system directory (/usr/share/lush/themes).
 *
 * @param registry The theme registry to register loaded themes with.
 * @return The total number of themes loaded.
 */
size_t lle_theme_load_user_themes(lle_theme_registry_t *registry) {
    if (!registry) {
        return 0;
    }

    size_t total_loaded = 0;

    /* Load from user directory */
    char user_dir[LLE_THEME_PATH_MAX];
    if (lle_theme_get_user_dir(user_dir, sizeof(user_dir)) == LLE_SUCCESS) {
        total_loaded += lle_theme_load_directory(user_dir, registry, NULL);
    }

    /* Load from system directory */
    char system_dir[LLE_THEME_PATH_MAX];
    if (lle_theme_get_system_dir(system_dir, sizeof(system_dir)) ==
        LLE_SUCCESS) {
        total_loaded += lle_theme_load_directory(system_dir, registry, NULL);
    }

    return total_loaded;
}

/* ============================================================================
 * Hot Reload API
 * ============================================================================
 */

/**
 * @brief Reload all user themes from standard locations.
 *
 * Scans user and system directories for new themes and loads them.
 * Existing themes are not replaced.
 *
 * @param registry The theme registry to update.
 * @return The number of new themes loaded.
 */
size_t lle_theme_reload_user_themes(lle_theme_registry_t *registry) {
    if (!registry) {
        return 0;
    }

    size_t reloaded = 0;

    /* Get list of current user themes to potentially update */
    /* For now, we just load new themes - existing themes are not replaced */
    /* A more sophisticated implementation would track file paths and update */

    /* Load from user directory */
    char user_dir[LLE_THEME_PATH_MAX];
    if (lle_theme_get_user_dir(user_dir, sizeof(user_dir)) == LLE_SUCCESS) {
        reloaded += lle_theme_load_directory(user_dir, registry, NULL);
    }

    /* Load from system directory */
    char system_dir[LLE_THEME_PATH_MAX];
    if (lle_theme_get_system_dir(system_dir, sizeof(system_dir)) ==
        LLE_SUCCESS) {
        reloaded += lle_theme_load_directory(system_dir, registry, NULL);
    }

    return reloaded;
}

/**
 * @brief Reload a specific theme by name from its file.
 *
 * Finds the theme file and reloads its content, updating the
 * existing theme in the registry.
 *
 * @param registry The theme registry containing the theme.
 * @param name The name of the theme to reload.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_reload_by_name(lle_theme_registry_t *registry,
                                      const char *name) {
    if (!registry || !name) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Find existing theme */
    lle_theme_t *existing = lle_theme_registry_find(registry, name);
    if (!existing) {
        return LLE_ERROR_NOT_FOUND;
    }

    /* Only reload user themes */
    if (existing->source != LLE_THEME_SOURCE_USER) {
        return LLE_ERROR_INVALID_STATE;
    }

    /* Search for theme file */
    char user_dir[LLE_THEME_PATH_MAX];
    char filepath[LLE_THEME_PATH_MAX];

    if (lle_theme_get_user_dir(user_dir, sizeof(user_dir)) == LLE_SUCCESS) {
        /* Limit components to prevent truncation: path + "/" + name + ext */
        snprintf(filepath, sizeof(filepath), "%.4020s/%.64s%s", user_dir, name,
                 LLE_THEME_FILE_EXTENSION);

        if (lle_theme_file_exists(filepath)) {
            /* Load into temporary theme */
            lle_theme_t temp_theme;
            lle_result_t status =
                lle_theme_load_from_file(filepath, &temp_theme, NULL);

            if (status == LLE_SUCCESS) {
                /* Copy updated values to existing theme */
                bool was_active = existing->is_active;
                memcpy(existing, &temp_theme, sizeof(lle_theme_t));
                existing->is_active = was_active;
                existing->source = LLE_THEME_SOURCE_USER;
                return LLE_SUCCESS;
            }
            return status;
        }
    }

    return LLE_ERROR_NOT_FOUND;
}

/* ============================================================================
 * Theme Export API
 * ============================================================================
 */

/**
 * @brief Escape a string for TOML output with proper quoting.
 *
 * Adds surrounding quotes and escapes special characters.
 *
 * @param input The input string to escape.
 * @param output Buffer to receive the escaped string.
 * @param output_size Size of the output buffer.
 * @return The number of characters written.
 */
static size_t toml_escape_string(const char *input, char *output,
                                 size_t output_size) {
    if (!input || !output || output_size == 0) {
        return 0;
    }

    size_t out_len = 0;
    output[out_len++] = '"';

    for (size_t i = 0; input[i] != '\0' && out_len < output_size - 2; i++) {
        char c = input[i];
        switch (c) {
        case '\n':
            if (out_len + 2 < output_size - 1) {
                output[out_len++] = '\\';
                output[out_len++] = 'n';
            }
            break;
        case '\t':
            if (out_len + 2 < output_size - 1) {
                output[out_len++] = '\\';
                output[out_len++] = 't';
            }
            break;
        case '\r':
            if (out_len + 2 < output_size - 1) {
                output[out_len++] = '\\';
                output[out_len++] = 'r';
            }
            break;
        case '\\':
            if (out_len + 2 < output_size - 1) {
                output[out_len++] = '\\';
                output[out_len++] = '\\';
            }
            break;
        case '"':
            if (out_len + 2 < output_size - 1) {
                output[out_len++] = '\\';
                output[out_len++] = '"';
            }
            break;
        default:
            output[out_len++] = c;
            break;
        }
    }

    output[out_len++] = '"';
    output[out_len] = '\0';
    return out_len;
}

/**
 * @brief Format a color as a TOML inline table.
 *
 * Converts the color to TOML format like { fg = "red", bold = true }.
 *
 * @param color The color to format.
 * @param output Buffer to receive the formatted string.
 * @param output_size Size of the output buffer.
 * @return The number of characters written, or 0 on error.
 */
static size_t format_color_toml(const lle_color_t *color, char *output,
                                size_t output_size) {
    if (!color || !output || output_size == 0) {
        return 0;
    }

    if (color->mode == LLE_COLOR_MODE_NONE) {
        return 0;
    }

    char fg_str[64] = {0};

    switch (color->mode) {
    case LLE_COLOR_MODE_BASIC: {
        const char *names[] = {"black", "red",     "green", "yellow",
                               "blue",  "magenta", "cyan",  "white"};
        if (color->value.basic < 8) {
            snprintf(fg_str, sizeof(fg_str), "\"%s\"",
                     names[color->value.basic]);
        }
        break;
    }
    case LLE_COLOR_MODE_256:
        snprintf(fg_str, sizeof(fg_str), "%d", color->value.palette);
        break;
    case LLE_COLOR_MODE_TRUE:
        snprintf(fg_str, sizeof(fg_str), "\"#%02x%02x%02x\"",
                 color->value.rgb.r, color->value.rgb.g, color->value.rgb.b);
        break;
    default:
        return 0;
    }

    /* Build inline table */
    int len = snprintf(output, output_size, "{ fg = %s", fg_str);

    if (color->bold && (size_t)len < output_size - 20) {
        len +=
            snprintf(output + len, output_size - (size_t)len, ", bold = true");
    }
    if (color->italic && (size_t)len < output_size - 20) {
        len += snprintf(output + len, output_size - (size_t)len,
                        ", italic = true");
    }
    if (color->underline && (size_t)len < output_size - 20) {
        len += snprintf(output + len, output_size - (size_t)len,
                        ", underline = true");
    }
    if (color->dim && (size_t)len < output_size - 20) {
        len +=
            snprintf(output + len, output_size - (size_t)len, ", dim = true");
    }

    if ((size_t)len < output_size - 2) {
        len += snprintf(output + len, output_size - (size_t)len, " }");
    }

    return (size_t)len;
}

/**
 * @brief Convert a theme category enum to its string representation.
 *
 * @param category The theme category.
 * @return A static string with the category name.
 */
static const char *category_to_string(lle_theme_category_t category) {
    switch (category) {
    case LLE_THEME_CATEGORY_MINIMAL:
        return "minimal";
    case LLE_THEME_CATEGORY_CLASSIC:
        return "classic";
    case LLE_THEME_CATEGORY_MODERN:
        return "modern";
    case LLE_THEME_CATEGORY_POWERLINE:
        return "powerline";
    case LLE_THEME_CATEGORY_PROFESSIONAL:
        return "professional";
    case LLE_THEME_CATEGORY_CREATIVE:
        return "creative";
    case LLE_THEME_CATEGORY_CUSTOM:
    default:
        return "custom";
    }
}

/**
 * @brief Export a theme to TOML format string.
 *
 * Serializes the theme structure to a TOML-formatted string
 * suitable for saving to a file.
 *
 * @param theme The theme to export.
 * @param output Buffer to receive the TOML content.
 * @param output_size Size of the output buffer.
 * @return The number of characters written.
 */
size_t lle_theme_export_to_toml(const lle_theme_t *theme, char *output,
                                size_t output_size) {
    if (!theme || !output || output_size == 0) {
        return 0;
    }

    size_t pos = 0;
    char escaped[LLE_THEME_PARSER_STRING_MAX * 2];
    char color_buf[128];

#define APPEND(...)                                                            \
    do {                                                                       \
        int written = snprintf(output + pos, output_size - pos, __VA_ARGS__);  \
        if (written > 0 && (size_t)written < output_size - pos) {              \
            pos += (size_t)written;                                            \
        }                                                                      \
    } while (0)

    /* Header comment */
    APPEND("# LLE Theme File\n");
    APPEND("# Generated by lush theme system\n\n");

    /* [theme] section */
    APPEND("[theme]\n");

    toml_escape_string(theme->name, escaped, sizeof(escaped));
    APPEND("name = %s\n", escaped);

    if (strlen(theme->description) > 0) {
        toml_escape_string(theme->description, escaped, sizeof(escaped));
        APPEND("description = %s\n", escaped);
    }

    if (strlen(theme->author) > 0) {
        toml_escape_string(theme->author, escaped, sizeof(escaped));
        APPEND("author = %s\n", escaped);
    }

    if (strlen(theme->version) > 0) {
        toml_escape_string(theme->version, escaped, sizeof(escaped));
        APPEND("version = %s\n", escaped);
    }

    APPEND("category = \"%s\"\n", category_to_string(theme->category));

    if (strlen(theme->inherits_from) > 0) {
        toml_escape_string(theme->inherits_from, escaped, sizeof(escaped));
        APPEND("inherits_from = %s\n", escaped);
    }

    APPEND("\n");

    /* [capabilities] section */
    APPEND("[capabilities]\n");
    if (theme->capabilities & LLE_THEME_CAP_UNICODE) {
        APPEND("unicode = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_MULTILINE) {
        APPEND("multiline = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_TRANSIENT) {
        APPEND("transient = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_ASYNC_SEGMENTS) {
        APPEND("async_segments = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_POWERLINE) {
        APPEND("powerline = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_NERD_FONT) {
        APPEND("nerd_font = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_RIGHT_PROMPT) {
        APPEND("right_prompt = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_TRUE_COLOR) {
        APPEND("true_color = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_256_COLOR) {
        APPEND("256_color = true\n");
    }
    if (theme->capabilities & LLE_THEME_CAP_ASCII_FALLBACK) {
        APPEND("ascii_fallback = true\n");
    }
    APPEND("\n");

    /* [layout] section */
    APPEND("[layout]\n");

    if (strlen(theme->layout.ps1_format) > 0) {
        toml_escape_string(theme->layout.ps1_format, escaped, sizeof(escaped));
        APPEND("ps1 = %s\n", escaped);
    }

    if (strlen(theme->layout.ps2_format) > 0) {
        toml_escape_string(theme->layout.ps2_format, escaped, sizeof(escaped));
        APPEND("ps2 = %s\n", escaped);
    }

    if (strlen(theme->layout.rps1_format) > 0) {
        toml_escape_string(theme->layout.rps1_format, escaped, sizeof(escaped));
        APPEND("rprompt = %s\n", escaped);
    }

    if (strlen(theme->layout.transient_format) > 0) {
        toml_escape_string(theme->layout.transient_format, escaped,
                           sizeof(escaped));
        APPEND("transient = %s\n", escaped);
    }

    if (theme->layout.newline_before) {
        APPEND("newline_before = true\n");
    }
    if (theme->layout.newline_after) {
        APPEND("newline_after = true\n");
    }
    if (theme->layout.enable_multiline) {
        APPEND("multiline = true\n");
    }
    if (theme->layout.compact_mode) {
        APPEND("compact = true\n");
    }
    APPEND("\n");

    /* [segments] section */
    if (theme->enabled_segment_count > 0) {
        APPEND("[segments]\n");
        APPEND("enabled = [");
        for (size_t i = 0; i < theme->enabled_segment_count; i++) {
            if (i > 0) {
                APPEND(", ");
            }
            APPEND("\"%s\"", theme->enabled_segments[i]);
        }
        APPEND("]\n\n");
    }

    /* [segments.<name>] subsections */
    for (size_t si = 0; si < theme->segment_config_count; si++) {
        const lle_segment_config_t *seg = &theme->segment_configs[si];
        if (!seg->configured) {
            continue;
        }
        APPEND("[segments.%s]\n", seg->name);
        if (seg->show_set) {
            APPEND("show = %s\n", seg->show ? "true" : "false");
        }
        if (seg->truncation_length_set) {
            APPEND("truncation_length = %d\n", seg->truncation_length);
        }
        if (seg->style_set) {
            toml_escape_string(seg->style, escaped, sizeof(escaped));
            APPEND("style = %s\n", escaped);
        }
        if (seg->format_set) {
            toml_escape_string(seg->format, escaped, sizeof(escaped));
            APPEND("format = %s\n", escaped);
        }
        if (seg->home_symbol_set) {
            toml_escape_string(seg->home_symbol, escaped, sizeof(escaped));
            APPEND("home_symbol = %s\n", escaped);
        }
        if (seg->show_branch_set) {
            APPEND("show_branch = %s\n", seg->show_branch ? "true" : "false");
        }
        if (seg->show_status_set) {
            APPEND("show_status = %s\n", seg->show_status ? "true" : "false");
        }
        if (seg->show_ahead_behind_set) {
            APPEND("show_ahead_behind = %s\n",
                   seg->show_ahead_behind ? "true" : "false");
        }
        if (seg->show_stash_set) {
            APPEND("show_stash = %s\n", seg->show_stash ? "true" : "false");
        }
        APPEND("\n");
    }

    /* [colors] section */
    APPEND("[colors]\n");

#define EXPORT_COLOR(name, field)                                              \
    do {                                                                       \
        if (theme->colors.field.mode != LLE_COLOR_MODE_NONE) {                 \
            if (format_color_toml(&theme->colors.field, color_buf,             \
                                  sizeof(color_buf)) > 0) {                    \
                APPEND("%s = %s\n", name, color_buf);                          \
            }                                                                  \
        }                                                                      \
    } while (0)

    EXPORT_COLOR("primary", primary);
    EXPORT_COLOR("secondary", secondary);
    EXPORT_COLOR("success", success);
    EXPORT_COLOR("warning", warning);
    EXPORT_COLOR("error", error);
    EXPORT_COLOR("info", info);
    EXPORT_COLOR("text", text);
    EXPORT_COLOR("text_dim", text_dim);
    EXPORT_COLOR("text_bright", text_bright);
    EXPORT_COLOR("border", border);
    EXPORT_COLOR("background", background);
    EXPORT_COLOR("highlight", highlight);
    EXPORT_COLOR("git_clean", git_clean);
    EXPORT_COLOR("git_dirty", git_dirty);
    EXPORT_COLOR("git_staged", git_staged);
    EXPORT_COLOR("git_untracked", git_untracked);
    EXPORT_COLOR("git_branch", git_branch);
    EXPORT_COLOR("git_ahead", git_ahead);
    EXPORT_COLOR("git_behind", git_behind);
    EXPORT_COLOR("path_home", path_home);
    EXPORT_COLOR("path_root", path_root);
    EXPORT_COLOR("path_normal", path_normal);
    EXPORT_COLOR("path_separator", path_separator);
    EXPORT_COLOR("status_ok", status_ok);
    EXPORT_COLOR("status_error", status_error);
    EXPORT_COLOR("status_running", status_running);

#undef EXPORT_COLOR

    APPEND("\n");

    /* [symbols] section */
    APPEND("[symbols]\n");

#define EXPORT_SYMBOL(name, field)                                             \
    do {                                                                       \
        if (strlen(theme->symbols.field) > 0) {                                \
            toml_escape_string(theme->symbols.field, escaped,                  \
                               sizeof(escaped));                               \
            APPEND("%s = %s\n", name, escaped);                                \
        }                                                                      \
    } while (0)

    EXPORT_SYMBOL("prompt", prompt);
    EXPORT_SYMBOL("prompt_root", prompt_root);
    EXPORT_SYMBOL("continuation", continuation);
    EXPORT_SYMBOL("separator_left", separator_left);
    EXPORT_SYMBOL("separator_right", separator_right);
    EXPORT_SYMBOL("branch", branch);
    EXPORT_SYMBOL("staged", staged);
    EXPORT_SYMBOL("unstaged", unstaged);
    EXPORT_SYMBOL("untracked", untracked);
    EXPORT_SYMBOL("ahead", ahead);
    EXPORT_SYMBOL("behind", behind);
    EXPORT_SYMBOL("stash", stash);
    EXPORT_SYMBOL("conflict", conflict);
    EXPORT_SYMBOL("directory", directory);
    EXPORT_SYMBOL("home", home);
    EXPORT_SYMBOL("error", error);
    EXPORT_SYMBOL("success", success);
    EXPORT_SYMBOL("time", time);
    EXPORT_SYMBOL("jobs", jobs);

#undef EXPORT_SYMBOL

    /* [syntax] section - only if theme has syntax colors */
    if (theme->has_syntax_colors) {
        APPEND("\n[syntax]\n");

        /* Helper macro to export RGB color as hex */
#define EXPORT_SYNTAX_COLOR(name, field)                                       \
    do {                                                                       \
        if (theme->syntax_colors.field != 0) {                                 \
            APPEND("%s = \"#%06x\"\n", name, theme->syntax_colors.field);      \
        }                                                                      \
    } while (0)

        EXPORT_SYNTAX_COLOR("command_valid", command_valid);
        EXPORT_SYNTAX_COLOR("command_invalid", command_invalid);
        EXPORT_SYNTAX_COLOR("command_builtin", command_builtin);
        EXPORT_SYNTAX_COLOR("command_alias", command_alias);
        EXPORT_SYNTAX_COLOR("command_function", command_function);
        EXPORT_SYNTAX_COLOR("keyword", keyword);
        EXPORT_SYNTAX_COLOR("string", string);
        EXPORT_SYNTAX_COLOR("string_escape", string_escape);
        EXPORT_SYNTAX_COLOR("variable", variable);
        EXPORT_SYNTAX_COLOR("variable_special", variable_special);
        EXPORT_SYNTAX_COLOR("path_valid", path_valid);
        EXPORT_SYNTAX_COLOR("path_invalid", path_invalid);
        EXPORT_SYNTAX_COLOR("pipe", pipe);
        EXPORT_SYNTAX_COLOR("redirect", redirect);
        EXPORT_SYNTAX_COLOR("operator", operator_other);
        EXPORT_SYNTAX_COLOR("assignment", assignment);
        EXPORT_SYNTAX_COLOR("comment", comment);
        EXPORT_SYNTAX_COLOR("number", number);
        EXPORT_SYNTAX_COLOR("option", option);
        EXPORT_SYNTAX_COLOR("glob", glob);
        EXPORT_SYNTAX_COLOR("argument", argument);
        EXPORT_SYNTAX_COLOR("error", error);
        /* Here-documents and here-strings */
        EXPORT_SYNTAX_COLOR("heredoc_op", heredoc_op);
        EXPORT_SYNTAX_COLOR("heredoc_delim", heredoc_delim);
        EXPORT_SYNTAX_COLOR("heredoc_content", heredoc_content);
        EXPORT_SYNTAX_COLOR("herestring", herestring);
        /* Process substitution */
        EXPORT_SYNTAX_COLOR("procsub", procsub);
        /* ANSI-C quoting */
        EXPORT_SYNTAX_COLOR("string_ansic", string_ansic);
        /* Arithmetic expansion */
        EXPORT_SYNTAX_COLOR("arithmetic", arithmetic);

#undef EXPORT_SYNTAX_COLOR

        /* Export text attributes */
        if (theme->syntax_colors.keyword_bold) {
            APPEND("keyword_bold = true\n");
        }
        if (theme->syntax_colors.command_bold) {
            APPEND("command_bold = true\n");
        }
        if (theme->syntax_colors.error_underline) {
            APPEND("error_underline = true\n");
        }
        if (theme->syntax_colors.path_underline) {
            APPEND("path_underline = true\n");
        }
        if (theme->syntax_colors.comment_dim) {
            APPEND("comment_dim = true\n");
        }
    }

#undef APPEND

    return pos;
}

/**
 * @brief Export a theme to a TOML file.
 *
 * Serializes the theme and writes it to the specified file path.
 *
 * @param theme The theme to export.
 * @param filepath The destination file path.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_export_to_file(const lle_theme_t *theme,
                                      const char *filepath) {
    if (!theme || !filepath) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Generate TOML content */
    char *buffer = malloc(LLE_THEME_FILE_MAX_SIZE);
    if (!buffer) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    size_t content_len =
        lle_theme_export_to_toml(theme, buffer, LLE_THEME_FILE_MAX_SIZE);
    if (content_len == 0) {
        free(buffer);
        return LLE_ERROR_INVALID_STATE;
    }

    /* Write to file */
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        free(buffer);
        return LLE_ERROR_IO_ERROR;
    }

    size_t written = fwrite(buffer, 1, content_len, fp);
    fclose(fp);
    free(buffer);

    if (written != content_len) {
        return LLE_ERROR_IO_ERROR;
    }

    return LLE_SUCCESS;
}

/* ============================================================================
 * Path Utilities
 * ============================================================================
 */

/**
 * @brief Get the user theme directory path.
 *
 * Returns the path to the user's theme directory, typically
 * ~/.config/lush/themes or $XDG_CONFIG_HOME/lush/themes.
 *
 * @param buffer Buffer to receive the path.
 * @param size Size of the buffer.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_get_user_dir(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    /* Check XDG_CONFIG_HOME first */
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        snprintf(buffer, size, "%s/%s", xdg_config, LLE_THEME_USER_DIR);
        return LLE_SUCCESS;
    }

    /* Fall back to ~/.config */
    const char *home = get_home_dir();
    if (!home) {
        return LLE_ERROR_NOT_FOUND;
    }

    snprintf(buffer, size, "%s/.config/%s", home, LLE_THEME_USER_DIR);
    return LLE_SUCCESS;
}

/**
 * @brief Get the system theme directory path.
 *
 * Returns the path to the system-wide theme directory.
 *
 * @param buffer Buffer to receive the path.
 * @param size Size of the buffer.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_get_system_dir(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    snprintf(buffer, size, "%s", LLE_THEME_SYSTEM_DIR);
    return LLE_SUCCESS;
}

/**
 * @brief Check if a theme file exists and is a regular file.
 *
 * @param filepath The path to check.
 * @return true if the file exists and is a regular file.
 */
bool lle_theme_file_exists(const char *filepath) {
    if (!filepath) {
        return false;
    }

    struct stat st;
    return stat(filepath, &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * @brief Create the user theme directory if it doesn't exist.
 *
 * Creates the directory and all parent directories as needed.
 *
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_ensure_user_dir(void) {
    char user_dir[LLE_THEME_PATH_MAX];
    lle_result_t result = lle_theme_get_user_dir(user_dir, sizeof(user_dir));

    if (result != LLE_SUCCESS) {
        return result;
    }

    return mkdir_recursive(user_dir);
}

/* ============================================================================
 * Batch Result Helpers
 * ============================================================================
 */

/**
 * @brief Initialize a batch result structure for directory loading.
 *
 * @param result The batch result structure to initialize.
 * @param capacity Maximum number of individual results to store.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_theme_batch_result_init(lle_theme_batch_result_t *result,
                                         size_t capacity) {
    if (!result) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    memset(result, 0, sizeof(*result));

    if (capacity > 0) {
        result->results = calloc(capacity, sizeof(lle_theme_load_result_t));
        if (!result->results) {
            return LLE_ERROR_OUT_OF_MEMORY;
        }
        result->results_capacity = capacity;
    }

    return LLE_SUCCESS;
}

/**
 * @brief Free resources in a batch result structure.
 *
 * @param result The batch result structure to clean up.
 */
void lle_theme_batch_result_cleanup(lle_theme_batch_result_t *result) {
    if (!result) {
        return;
    }

    if (result->results) {
        free(result->results);
        result->results = NULL;
    }
    result->results_capacity = 0;
}
