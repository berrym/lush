/**
 * @file template.h
 * @brief LLE Prompt Template Engine - Type Definitions and Function
 * Declarations
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Specification: Spec 25 Section 6 - Template Engine
 * Version: 1.0.0
 *
 * The template engine provides expressive format strings for prompt generation
 * with segment references, conditionals, color application, and property
 * access.
 *
 * Template Syntax:
 *   ${segment}              - Render segment (e.g., ${directory}, ${git})
 *   ${segment.property}     - Access segment property (e.g., ${git.branch})
 *   ${?segment:true:false}  - Conditional: if segment visible, show true
 *   ${?segment.prop:t:f}    - Conditional on property existence
 *   ${color:text}           - Apply theme color to text
 *   \n                      - Literal newline
 *   \\                      - Escaped backslash
 *   \$                      - Escaped dollar sign
 */

#ifndef LLE_PROMPT_TEMPLATE_H
#define LLE_PROMPT_TEMPLATE_H

#include "lle/error_handling.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================
 */

/** @brief Maximum template string length */
#define LLE_TEMPLATE_MAX 1024

/** @brief Maximum token text length */
#define LLE_TEMPLATE_TOKEN_MAX 256

/** @brief Maximum segment name length */
#define LLE_TEMPLATE_SEGMENT_MAX 32

/** @brief Maximum property name length */
#define LLE_TEMPLATE_PROPERTY_MAX 32

/** @brief Maximum literal text length */
#define LLE_TEMPLATE_LITERAL_MAX 256

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================
 */

/**
 * @brief Template token types
 */
typedef enum lle_template_token_type {
    LLE_TOKEN_LITERAL,     /**< Plain text */
    LLE_TOKEN_SEGMENT,     /**< ${segment} */
    LLE_TOKEN_PROPERTY,    /**< ${segment.property} */
    LLE_TOKEN_CONDITIONAL, /**< ${?condition:true:false} */
    LLE_TOKEN_COLOR,       /**< ${color:text} */
    LLE_TOKEN_NEWLINE,     /**< \n */
    LLE_TOKEN_END          /**< End of template */
} lle_template_token_type_t;

/**
 * @brief Parsed template token
 *
 * Tokens form a linked list representing the parsed template structure.
 */
typedef struct lle_template_token {
    lle_template_token_type_t type; /**< Token type */

    /** @brief Token data union based on type */
    union {
        /** @brief Literal text data */
        struct {
            char text[LLE_TEMPLATE_LITERAL_MAX];
            size_t length;
        } literal;

        /** @brief Segment reference data */
        struct {
            char segment_name[LLE_TEMPLATE_SEGMENT_MAX];
            char property_name[LLE_TEMPLATE_PROPERTY_MAX];
            bool has_property;
        } segment;

        /** @brief Conditional data */
        struct {
            char condition_segment[LLE_TEMPLATE_SEGMENT_MAX];
            char condition_property[LLE_TEMPLATE_PROPERTY_MAX];
            char true_value[LLE_TEMPLATE_LITERAL_MAX];
            char false_value[LLE_TEMPLATE_LITERAL_MAX];
            bool check_property;
        } conditional;

        /** @brief Color application data */
        struct {
            char color_name[LLE_TEMPLATE_SEGMENT_MAX];
            char text[LLE_TEMPLATE_LITERAL_MAX];
        } color;
    } data;

    struct lle_template_token *next; /**< Next token in list */
} lle_template_token_t;

/**
 * @brief Parsed template structure
 *
 * Contains a linked list of tokens representing the parsed template.
 */
typedef struct lle_parsed_template {
    lle_template_token_t *head;      /**< First token */
    lle_template_token_t *tail;      /**< Last token */
    size_t token_count;              /**< Number of tokens */
    char original[LLE_TEMPLATE_MAX]; /**< Original template string */
    bool valid;                      /**< Parse succeeded */
} lle_parsed_template_t;

// Forward declarations for render context
struct lle_segment_registry;
struct lle_prompt_context;
struct lle_theme;

/**
 * @brief Segment value provider callback
 *
 * Called by the template renderer to get segment output.
 *
 * @param segment_name  Name of the segment to render
 * @param property      Property name (NULL for full segment)
 * @param user_data     User-provided context
 * @return Rendered content (caller must free) or NULL
 */
typedef char *(*lle_template_segment_provider_t)(const char *segment_name,
                                                 const char *property,
                                                 void *user_data);

/**
 * @brief Segment visibility check callback
 *
 * Called by the template renderer to check if a segment is visible.
 *
 * @param segment_name  Name of the segment
 * @param property      Property name (NULL for segment visibility)
 * @param user_data     User-provided context
 * @return true if segment/property is visible, false otherwise
 */
typedef bool (*lle_template_segment_visible_t)(const char *segment_name,
                                               const char *property,
                                               void *user_data);

/**
 * @brief Color provider callback
 *
 * Called by the template renderer to get ANSI color codes.
 *
 * @param color_name  Semantic color name (e.g., "primary", "git_clean")
 * @param user_data   User-provided context
 * @return ANSI color code string (static, do not free) or empty string
 */
typedef const char *(*lle_template_color_provider_t)(const char *color_name,
                                                     void *user_data);

/**
 * @brief Template render context
 *
 * Provides callbacks for segment rendering, visibility checking,
 * and color lookup during template evaluation.
 */
typedef struct lle_template_render_ctx {
    lle_template_segment_provider_t get_segment; /**< Segment provider */
    lle_template_segment_visible_t is_visible;   /**< Visibility checker */
    lle_template_color_provider_t get_color;     /**< Color provider */
    void *user_data;                             /**< User context */
} lle_template_render_ctx_t;

/* ============================================================================
 * TEMPLATE PARSING API
 * ============================================================================
 */

/**
 * @brief Parse a template string into tokens
 *
 * @param template_str  Template string to parse
 * @param parsed        Output: parsed template structure
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_template_parse(const char *template_str,
                                lle_parsed_template_t **parsed);

/**
 * @brief Free a parsed template and all its tokens
 *
 * @param parsed  Parsed template to free
 */
void lle_template_free(lle_parsed_template_t *parsed);

/**
 * @brief Validate a template string without full parsing
 *
 * @param template_str  Template string to validate
 * @return true if template appears valid, false otherwise
 */
bool lle_template_validate(const char *template_str);

/* ============================================================================
 * TEMPLATE RENDERING API
 * ============================================================================
 */

/**
 * @brief Render a parsed template to output string
 *
 * @param tmpl         Parsed template
 * @param render_ctx   Render context with callbacks
 * @param output       Output buffer
 * @param output_size  Output buffer size
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_template_render(const lle_parsed_template_t *tmpl,
                                 const lle_template_render_ctx_t *render_ctx,
                                 char *output, size_t output_size);

/**
 * @brief Parse and render a template in one call
 *
 * Convenience function that parses, renders, and frees the template.
 *
 * @param template_str  Template string
 * @param render_ctx    Render context with callbacks
 * @param output        Output buffer
 * @param output_size   Output buffer size
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_template_evaluate(const char *template_str,
                                   const lle_template_render_ctx_t *render_ctx,
                                   char *output, size_t output_size);

/* ============================================================================
 * TOKEN CREATION HELPERS (INTERNAL)
 * ============================================================================
 */

/**
 * @brief Create a literal token
 *
 * @param text  Literal text
 * @param len   Text length
 * @return New token or NULL on error
 */
lle_template_token_t *lle_template_token_literal(const char *text, size_t len);

/**
 * @brief Create a segment token
 *
 * @param name  Segment name
 * @return New token or NULL on error
 */
lle_template_token_t *lle_template_token_segment(const char *name);

/**
 * @brief Create a property token
 *
 * @param segment   Segment name
 * @param property  Property name
 * @return New token or NULL on error
 */
lle_template_token_t *lle_template_token_property(const char *segment,
                                                  const char *property);

/**
 * @brief Create a conditional token
 *
 * @param segment    Condition segment name
 * @param property   Condition property (NULL for segment visibility)
 * @param true_val   Value if condition is true
 * @param false_val  Value if condition is false
 * @return New token or NULL on error
 */
lle_template_token_t *lle_template_token_conditional(const char *segment,
                                                     const char *property,
                                                     const char *true_val,
                                                     const char *false_val);

/**
 * @brief Create a color token
 *
 * @param color  Color name
 * @param text   Text to colorize
 * @return New token or NULL on error
 */
lle_template_token_t *lle_template_token_color(const char *color,
                                               const char *text);

/**
 * @brief Create a newline token
 *
 * @return New token or NULL on error
 */
lle_template_token_t *lle_template_token_newline(void);

/**
 * @brief Create an end token
 *
 * @return New token or NULL on error
 */
lle_template_token_t *lle_template_token_end(void);

/**
 * @brief Free a single token
 *
 * @param token  Token to free
 */
void lle_template_token_free(lle_template_token_t *token);

#ifdef __cplusplus
}
#endif

#endif // LLE_PROMPT_TEMPLATE_H
