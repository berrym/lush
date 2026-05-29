/**
 * @file strings.h
 * @brief String manipulation utilities for shell operations
 *
 * Provides string allocation, manipulation, and parsing utilities used
 * throughout the shell including quote handling, whitespace processing,
 * and escape sequence processing.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef STRINGS_H
#define STRINGS_H

#include <stdbool.h>
#include <sys/types.h>

/**
 * @brief Initialize the string symbol table
 *
 * Sets up internal data structures for string management.
 * Must be called during shell initialization.
 */
void init_str_symtable(void);

/**
 * @brief Allocate a string buffer
 *
 * @param len Size of buffer to allocate
 * @param exitflag If true, exit on allocation failure
 * @return Allocated buffer, or NULL on failure (if exitflag is false)
 */
char *alloc_str(size_t len, bool exitflag);

/**
 * @brief Free a string allocated with alloc_str
 *
 * @param s String to free
 */
void free_str(char *s);

/**
 * @brief Get an allocated copy of a string (direct)
 *
 * Creates a copy of the string without symbol table lookup.
 *
 * @param s String to copy
 * @return Allocated copy, or NULL on failure
 */
char *get_alloced_str_direct(char *s);

/**
 * @brief Get an allocated copy of a string
 *
 * Creates a copy of the string, potentially using the symbol table
 * for string interning.
 *
 * @param s String to copy
 * @return Allocated copy, or NULL on failure
 */
char *get_alloced_str(char *s);

/**
 * @brief Free a string obtained from get_alloced_str
 *
 * @param s String to free
 */
void free_alloced_str(char *s);

/**
 * @brief Convert a string to uppercase in place
 *
 * @param s String to convert
 * @return true on success, false on failure
 */
bool strupper(char *s);

/**
 * @brief Convert a string to lowercase in place
 *
 * @param s String to convert
 * @return true on success, false on failure
 */
bool strlower(char *s);

/**
 * @brief Strip leading and trailing whitespace
 *
 * Modifies the string in place, returning a pointer to
 * the first non-whitespace character.
 *
 * @param s String to strip
 * @return Pointer to stripped string (within original buffer)
 */
char *str_strip_whitespace(char *s);

/**
 * @brief Count leading whitespace characters
 *
 * @param s String to examine
 * @return Number of leading whitespace characters
 */
size_t str_skip_whitespace(char *s);

/**
 * @brief Remove leading whitespace in place
 *
 * Shifts string contents to remove leading whitespace.
 *
 * @param s String to modify
 * @return Number of characters removed
 */
size_t str_strip_leading_whitespace(char *s);

/**
 * @brief Remove trailing whitespace in place
 *
 * Replaces trailing whitespace with null terminators.
 *
 * @param s String to modify
 * @return New length of string, or -1 on error
 */
ssize_t str_strip_trailing_whitespace(char *s);

/**
 * @brief Replace newline with null terminator
 *
 * Finds the first newline in the string and replaces it
 * with a null terminator.
 *
 * @param s String to modify
 */
void null_replace_newline(char *s);

/**
 * @brief Ensure string is null-terminated
 *
 * @param s String to terminate
 */
void null_terminate_str(char *s);

/**
 * @brief Delete a character at a specific index
 *
 * Shifts remaining characters left to fill the gap.
 *
 * @param s String to modify
 * @param index Position of character to delete
 */
void delete_char_at(char *s, size_t index);

/**
 * @brief Find first occurrence of any character from a set
 *
 * Similar to strpbrk but with different parameter order.
 *
 * @param s String to search
 * @param chars Set of characters to find
 * @return Pointer to first matching character, or NULL if none found
 */
char *strchr_any(char *s, char *chars);

/**
 * @brief Find the type of opening quote in a string
 *
 * @param s String to examine
 * @return Quote character found (' or "), or 0 if none
 */
char find_opening_quote_type(char *s);

/**
 * @brief Find position of last quote in string
 *
 * @param s String to search
 * @return Index of last quote, or 0 if none found
 */
size_t find_last_quote(char *s);

/**
 * @brief Find position of closing quote matching first quote
 *
 * @param s String starting with a quote
 * @return Index of closing quote, or 0 if not found
 */
size_t find_closing_quote(char *s);

/**
 * @brief Find position of closing brace matching first brace
 *
 * Handles nested braces correctly.
 *
 * @param s String starting with an opening brace
 * @return Index of closing brace, or 0 if not found
 */
size_t find_closing_brace(char *s);

/**
 * @brief Quote a value for shell output
 *
 * Adds appropriate quoting to make a value safe for shell use.
 *
 * @param val Value to quote
 * @param add_quotes Whether to add surrounding quotes
 * @return Quoted string (caller must free), or NULL on error
 */
char *quote_val(char *val, bool add_quotes);

/**
 * @brief Substitute a portion of a string
 *
 * Replaces characters from start to end with the replacement string.
 *
 * @param s1 Original string
 * @param s2 Replacement string
 * @param start Start index of replacement
 * @param end End index of replacement
 * @return New string with substitution (caller must free), or NULL on error
 */
char *substitute_str(char *s1, char *s2, size_t start, size_t end);

/**
 * @brief Check and grow buffer if needed
 *
 * Ensures buffer has sufficient capacity, reallocating if necessary.
 *
 * @param count Current number of items
 * @param len Current capacity
 * @param buf Pointer to buffer array
 * @return true if buffer is valid, false on allocation failure
 */
bool check_buffer_bounds(const size_t *count, size_t *len, char ***buf);

/**
 * @brief Free an argument vector
 *
 * Frees all strings in the array and the array itself.
 *
 * @param argc Number of arguments
 * @param argv Argument array to free
 */
void free_argv(size_t argc, char **argv);

#endif
