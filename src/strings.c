/**
 * @file strings.c
 * @brief String utility functions
 *
 * Provides string manipulation utilities including:
 * - String allocation and interning
 * - Whitespace stripping and handling
 * - Quote and brace matching
 * - Escape sequence processing
 * - Buffer management helpers
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "strings.h"

#include "errors.h"
#include "lush.h"
#include "symtable.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/** @brief Symbol table for string interning */
symtable_t *str_list = NULL;

/** @brief Shared empty string constant */
char *empty_str = "";

/** @brief Shared newline string constant */
char *newline_str = "\n";

/**
 * @brief Initialize string symbol table
 *
 * Creates the symbol table used for string interning.
 */
void init_str_symtable(void) { str_list = new_symtable(0); }

/**
 * @brief Allocate a string buffer
 *
 * @param len Size in bytes to allocate
 * @param exitflag If true, abort on allocation failure; if false, return NULL
 * @return Pointer to allocated buffer, or NULL on failure (if !exitflag)
 */
char *alloc_str(size_t len, bool exitflag) {
    char *s = NULL;

    s = calloc(len, sizeof(char));
    if (s == NULL) {
        if (exitflag) {
            error_syscall("error: `alloc_str`");
        } else {
            error_return("error: `alloc_str`");
            return NULL;
        }
    }

    return s;
}

/**
 * @brief Free a string
 *
 * @param s String to free (NULL is safe)
 */
void free_str(char *s) {
    if (s == NULL) {
        return;
    }

    free(s);
}

/**
 * @brief Allocate and copy a string directly
 *
 * Bypasses string interning and always allocates new memory.
 *
 * @param s String to copy
 * @return Newly allocated copy (caller must free)
 */
char *get_alloced_str_direct(char *s) {
    char *s2 = NULL;
    s2 = alloc_str(strlen(s) + 1, false);
    strcpy(s2, s);
    return s2;
}

/**
 * @brief Get an interned copy of a string
 *
 * Returns a shared string from the intern table if available,
 * otherwise adds the string to the table. Special cases for
 * empty string and newline return shared constants.
 *
 * @param s String to intern
 * @return Interned string (do not free directly, use free_alloced_str)
 */
char *get_alloced_str(char *s) {
    if (s == NULL) {
        return NULL;
    }

    if (!*s) {
        return empty_str;
    }

    if (*s == '\n' && s[1] == '\0') {
        return newline_str;
    }

    if (str_list) {
        symtable_entry_t *entry = get_symtable_entry(s);
        if (entry) {
            return entry->name;
        } else {
            entry = add_to_symtable(s);
            if (entry) {
                return entry->name;
            }
        }
    }

    return get_alloced_str_direct(s);
}

/**
 * @brief Free an interned string
 *
 * Removes the string from the intern table if present.
 * Handles shared constants (empty_str, newline_str) safely.
 *
 * @param s String to free
 */
void free_alloced_str(char *s) {
    if (s == NULL || s == empty_str || s == newline_str) {
        return;
    }

    if (str_list) {
        symtable_entry_t *entry = get_symtable_entry(s);
        if (entry) {
            remove_from_symtable(str_list, entry);
        }

        return;
    }

    free_str(s);
}

/**
 * @brief Convert string to uppercase in place
 *
 * @param s String to convert
 * @return true on success, false if s is NULL
 */
bool strupper(char *s) {
    if (s == NULL) {
        return false;
    }

    while (*s) {
        *s = toupper(*s);
        s++;
    }

    return true;
}

/**
 * @brief Convert string to lowercase in place
 *
 * @param s String to convert
 * @return true on success, false if s is NULL
 */
bool strlower(char *s) {
    if (s == NULL) {
        return false;
    }

    while (*s) {
        *s = tolower(*s);
        s++;
    }

    return true;
}

/**
 * @brief Strip leading and trailing whitespace
 *
 * Modifies the string in place by null-terminating after
 * trailing whitespace and returning pointer to first non-space.
 *
 * @param s String to strip
 * @return Pointer to first non-whitespace character
 */
char *str_strip_whitespace(char *s) {
    char *p = NULL, *t = NULL;

    for (p = s; isspace((int)*p); p++)
        ;

    if (*p == '\0') {
        return p;
    }

    t = p + strlen(p) - 1;
    while (t > p && isspace((int)*t)) {
        t--;
    }

    *++t = '\0';

    return p;
}

/**
 * @brief Count leading whitespace characters
 *
 * @param s String to examine
 * @return Number of leading whitespace characters
 */
size_t str_skip_whitespace(char *s) {
    size_t offset = 0;
    char c;

    while (((c = *s) != EOF) && isspace((int)c)) {
        s++;
        offset++;
    }

    return offset;
}

/**
 * @brief Strip leading whitespace from string in place
 *
 * Shifts string content left to remove leading whitespace.
 *
 * @param s String to modify
 * @return Number of characters stripped
 */
size_t str_strip_leading_whitespace(char *s) {
    char buf[MAXLINE + 1] = {'\0'}; /// buffer to store modified string
    size_t offset = 0;              /// loop counter

    /// Iterate over leading whitespace ignoring it
    for (offset = 0; offset <= strlen(s) && isspace((int)s[offset]); offset++)
        ;

    if (!offset) {
        return 0;
    }

    /// Copy the rest of the string into buf
    for (size_t i = 0; s[offset]; offset++, i++) {
        buf[i] = s[offset];
    }

    if (strcmp(buf, s) == 0) {
        return 0;
    }

    /// Overwrite s with buf
    memset(s, '\0', strlen(s));
    strcpy(s, buf);

    return offset;
}

/**
 * @brief Strip trailing whitespace from string in place
 *
 * @param s String to modify
 * @return Negative count of characters stripped
 */
ssize_t str_strip_trailing_whitespace(char *s) {
    ssize_t offset = 0;

    while (strlen(s) && isspace((int)s[strlen(s) - 1])) {
        s[strlen(s) - 1] = '\0';
        offset--;
    }

    return offset;
}

/**
 * @brief Replace trailing newline with null terminator
 *
 * @param s String to modify
 */
void null_replace_newline(char *s) {
    if (s == NULL || !*s) {
        return;
    }

    if (s[strlen(s) - 1] == '\n') {
        s[strlen(s) - 1] = '\0';
    }
}

/**
 * @brief Ensure string is null-terminated
 *
 * @param s String to terminate
 */
void null_terminate_str(char *s) {
    if (!*s) {
        return;
    }

    strncat(s, "\0", 1);
}

/**
 * @brief Delete character at given index
 *
 * Shifts all characters after index left by one.
 *
 * @param s String to modify
 * @param index Position of character to delete
 */
void delete_char_at(char *s, size_t index) {
    char *p1 = s + index;
    char *p2 = p1 + 1;
    while ((*p1++ = *p2++))
        ;
}

/**
 * @brief Search string for any of the given characters
 *
 * @param s String to search
 * @param chars Characters to search for
 * @return Pointer to first occurrence, or NULL if none found
 */
char *strchr_any(char *s, char *chars) {
    if (s == NULL || chars == NULL) {
        return NULL;
    }

    char *p = s;
    while (*p) {
        char *c = chars;
        while (*c) {
            if (*p == *c) {
                return p;
            }
            c++;
        }
        p++;
    }

    return NULL;
}

/**
 * @brief Find the type of opening quote in a string
 *
 * @param s String to examine
 * @return Quote character found (' or " or `), or '\0' if none
 */
char find_opening_quote_type(char *s) {
    for (char *p = s; *p; p++) {
        if (*p == '\'' || *p == '"' || *p == '`') {
            return *p;
        }
    }

    return '\0';
}

/**
 * @brief Find the last matching closing quote
 *
 * The opening quote must be the first character of the string.
 *
 * @param s String starting with a quote character
 * @return Index of last matching quote, or 0 if not found/imbalanced
 */
size_t find_last_quote(char *s) {
    /// check the type of quote we have
    char quote = s[0];
    if (quote != '\'' && quote != '"' && quote != '`') {
        return 0;
    }

    /// find the matching closing quote
    size_t i = 0, last = 0, count = 1, len = strlen(s);
    while (++i < len) {
        if (s[i] == quote) {
            if (s[i - 1] == '\\') {
                if (quote != '\'') {
                    continue;
                }
            }
            last = i;
            count++;
        }
    }

    /// if quotes are balanced return the index of the last quote
    if ((count % 2) == 0) {
        return last;
    }

    return 0;
}

/**
 * @brief Find the first matching closing quote
 *
 * The opening quote must be the first character of the string.
 * Handles escaped quotes properly.
 *
 * @param s String starting with a quote character
 * @return Index of closing quote, or 0 if not found
 */
size_t find_closing_quote(char *s) {
    /// check the type of quote we have
    char quote = s[0];
    if (quote != '\'' && quote != '"' && quote != '`') {
        return 0;
    }
    /// find the matching closing quote
    size_t i = 0, len = strlen(s);
    while (++i < len) {
        if (s[i] == '\\' && i + 1 < len) {
            /// Skip escaped character (including escaped quotes)
            i++;
            continue;
        }
        if (s[i] == quote) {
            return i;
        }
    }
    return 0;
}

/**
 * @brief Find the matching closing brace
 *
 * The opening brace ({ or () must be the first character.
 * Handles nested braces and quoted substrings.
 *
 * @param s String starting with an opening brace
 * @return Index of closing brace, or 0 if not found
 */
size_t find_closing_brace(char *s) {
    /// check the type of opening brace we have
    char opening_brace = s[0], closing_brace;
    if (opening_brace != '{' && opening_brace != '(') {
        return 0;
    }

    /// determine the closing brace according to the opening brace
    if (opening_brace == '{') {
        closing_brace = '}';
    } else {
        closing_brace = ')';
    }

    /// find the matching closing brace
    size_t ob_count = 1, cb_count = 0;
    size_t i = 0, len = strlen(s);
    while (++i < len) {
        if ((s[i] == '"') || (s[i] == '\'') || (s[i] == '`')) {
            /// skip escaped quotes
            if (s[i - 1] == '\\') {
                continue;
            }
            /// skip quoted substrings
            char quote = s[i];
            while (++i < len) {
                if (s[i] == quote && s[i - 1] != '\\') {
                    break;
                }
            }
            if (i == len) {
                return 0;
            }
            continue;
        }
        /// keep the count of opening and closing braces
        if (s[i - 1] != '\\') {
            if (s[i] == opening_brace) {
                ob_count++;
            } else if (s[i] == closing_brace) {
                cb_count++;
            }
        }

        /// break when we have a matching number of opening and closing braces
        if (ob_count == cb_count) {
            break;
        }
    }

    if (ob_count != cb_count) {
        return 0;
    }

    return i;
}

/**
 * @brief Quote a string value for shell reinput
 *
 * Escapes special characters (backslash, backtick, dollar, quote)
 * and optionally wraps in double quotes.
 *
 * @param val Value to quote
 * @param add_quotes If true, wrap result in double quotes
 * @return Quoted string (caller must free), or NULL on error
 */
char *quote_val(char *val, bool add_quotes) {
    char *res = NULL;
    size_t len;
    /// empty string
    if (val == NULL || !*val) {
        len = add_quotes ? 3 : 1;
        res = calloc(len, sizeof(char));
        if (res == NULL) {
            return NULL;
        }
        strcpy(res, add_quotes ? "\"\"" : "");
        return res;
    }

    /// count the number of quotes needed
    len = 0;
    char *v = val, *p;
    while (*v) {
        switch (*v) {
        case '\\':
        case '`':
        case '$':
        case '"':
            len++;
            break;
        default:
            break;
        }
        v++;
    }

    len += strlen(val);

    /// add two for the opening and closing quotes
    if (add_quotes) {
        len += 2;
    }

    /// alloc memory for quoted string
    res = alloc_str(len + 1, false);
    if (res == NULL) {
        return NULL;
    }

    p = res;

    /// add opening quote (optional)
    if (add_quotes) {
        *p++ = '"';
    }

    /// copy quoted val
    v = val;
    while (*v) {
        switch (*v) {
        case '\\':
        case '`':
        case '$':
        case '"':
            /// add '\' for quoting
            *p++ = '\\';
            /// copy char
            *p++ = *v++;
            break;
        default:
            /// copy next char
            *p++ = *v++;
            break;
        }
    }

    /// add closing quote (optional)
    if (add_quotes) {
        *p++ = '"';
    }
    *p = '\0';

    return res;
}

/**
 * @brief Check and extend buffer bounds if needed
 *
 * Allocates or doubles buffer size when count reaches len.
 * Initial allocation is 32 entries.
 *
 * @param count Current number of used entries
 * @param len Current buffer capacity (updated on resize)
 * @param buf Pointer to buffer pointer (updated on resize)
 * @return true if buffer is valid, false on allocation failure
 */
bool check_buffer_bounds(const size_t *count, size_t *len, char ***buf) {
    if (*count >= *len) {
        if ((*buf) == NULL) {
            /// first call. alloc memory for the buffer
            *buf = calloc(32, sizeof(char **));
            if ((*buf) == NULL) {
                return false;
            }
            *len = 32;
        } else {
            /// subsequent calls. extend the buffer
            const size_t newlen = (*len) * 2;
            char **tmp = realloc(*buf, newlen * sizeof(char **));
            if (tmp == NULL) {
                return false;
            }
            *buf = tmp;
            *len = newlen;
        }
    }
    return true;
}

/**
 * @brief Free an argument vector
 *
 * Frees each string in the vector and then the vector itself.
 *
 * @param argc Number of arguments
 * @param argv Argument vector to free
 */
void free_argv(size_t argc, char **argv) {
    if (!argc) {
        return;
    }

    while (argc--) {
        free(argv[argc]);
    }

    free(argv);
}
